#include "perf_clock_win.h"
#include <string.h>

struct clock_perf_scope {
    clock_perf_catalog_t catalog;
    clock_perf_binding_t binding;
    gcc_dfs_catalog_t dfs;
    volatile LONG quarantined;
};

typedef struct clock_perf_work {
    power_plan_t plan;
    power_scratch_t scratch;
} clock_perf_work_t;

static int ready(clock_perf_scope_t* s)
{
    const power_dev_t* d;
    gcc_dfs_catalog_t dfs;
    if (!(s && !s->quarantined && s->catalog.cnt && KeGetCurrentIrql() == PASSIVE_LEVEL &&
          s->binding.ready(s->binding.ctx) && (d = s->binding.physical_state) != NULL &&
          !d->quarantined))
        return 0;
    if (!s->binding.read_dfs_catalog(s->binding.ctx, s->catalog.clock, &dfs))
        return 0;
    if (memcmp(&s->dfs, &dfs, sizeof(dfs))) {
        InterlockedExchange(&s->quarantined, 1);
        return 0;
    }
    return 1;
}

clock_perf_scope_t* clock_perf_create(const void* blob, size_t bytes, const program_idx_t* idx,
                                      const clock_perf_binding_t* binding)
{
    clock_perf_scope_t* s;
    if (!binding || !binding->backend || !binding->backend->lock || !binding->backend->unlock ||
        !binding->physical_state || !binding->ready || !binding->begin || !binding->end ||
        !binding->read_clock || !binding->read_dfs_catalog || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return NULL;
    s = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s), POOL_TAG);
    if (!s)
        return NULL;
    RtlZeroMemory(s, sizeof(*s));
    s->binding = *binding;
    if (!clock_perf_discover(&s->catalog, blob, bytes, idx) ||
        !s->binding.read_dfs_catalog(s->binding.ctx, s->catalog.clock, &s->dfs)) {
        ExFreePool(s);
        return NULL;
    }
    if (s->dfs.enabled) {
        if (!s->dfs.cnt || s->dfs.cnt > GCC_DFS_MAX_STATES) {
            ExFreePool(s);
            return NULL;
        }
        s->catalog.cnt = s->dfs.cnt;
        RtlCopyMemory(s->catalog.hertz, s->dfs.hertz, s->dfs.cnt * sizeof(s->dfs.hertz[0]));
    }
    if (!ready(s)) {
        ExFreePool(s);
        return NULL;
    }
    return s;
}

void clock_perf_destroy(clock_perf_scope_t* s)
{
    if (s)
        ExFreePool(s);
}

static int current(clock_perf_scope_t* s, ULONG* idx)
{
    uint64_t physical = 0u;
    ULONG i;
    int ok = 0;
    /* pofx may query an idle device, including during its d3 handoff. The provider serializes
     * a physical GCC configuration read with D-state writes; a gated branch still retains its
     * programmed RCG freq. */
    if (!ready(s) || !idx || !s->binding.read_clock(s->binding.ctx, s->catalog.clock, &physical))
        return 0;
    for (i = 0u; i < s->catalog.cnt; ++i)
        if (s->catalog.hertz[i] == (uint32_t)physical) {
            *idx = i;
            ok = 1;
            break;
        }
    return ok;
}

static int req(clock_perf_scope_t* s, pep_req_comp_perf_state_t* r)
{
    clock_perf_work_t* work;
    power_action_t* action;
    power_execution_t trace;
    power_result_t result;
    ULONG idx, target;
    uint64_t before, after;
    if (!r)
        return 0;
    r->completed = 1u;
    r->succeeded = 0u;
    if (!ready(s) || r->comp != 0u || !r->perf_requests || r->perf_requests_cnt != 1u ||
        r->perf_requests[0].set != 0u || (idx = r->perf_requests[0].u.state_idx) >= s->catalog.cnt)
        return 1;
    work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
    if (!work)
        return 1;
    RtlZeroMemory(work, sizeof(*work));
    RtlCopyMemory(work->plan.dev_path, s->catalog.path, sizeof(s->catalog.path));
    work->plan.action_cnt = 1u;
    action = &work->plan.actions[0];
    action->kind = POWER_CLOCK;
    RtlCopyMemory(action->u.clock.name, s->catalog.clock, sizeof(s->catalog.clock));
    action->u.clock.vote = 3u;
    action->u.clock.extra_cnt = 2u;
    action->u.clock.extra[0] = s->catalog.hertz[idx];
    action->u.clock.extra[1] = 4u;
    if (s->binding.begin(s->binding.ctx)) {
        if (!ready(s) || !s->binding.physical_state->state_known ||
            (target = s->binding.physical_state->current_d_state) > 3u ||
            !s->binding.read_clock(s->binding.ctx, s->catalog.clock, &before)) {
            s->binding.end(s->binding.ctx);
            ExFreePool(work);
            return 1;
        }
        work->plan.target_d_state = target;
        /* the existing transaction engine snapshots the original client vote, commits the named
         * clock through its owner and verifies physical readback. An uncertain rollback is
         * retained across later requests. */
        result = power_execute_clock_rate(&work->plan, s->binding.backend, &trace, &work->scratch);
        if (result == POWER_ROLLBACK_FAILED)
            InterlockedExchange(&s->quarantined, 1);
        if (result == POWER_OK) {
            /* rate-only commits must preserve the physical branch enable. Do not publish a state
             * idx from a client-vote cache. */
            if (ready(s) && s->binding.physical_state->state_known &&
                s->binding.physical_state->current_d_state == target &&
                s->binding.read_clock(s->binding.ctx, s->catalog.clock, &after) &&
                (uint32_t)after == s->catalog.hertz[idx] &&
                ((before ^ after) & (UINT64_C(1) << 33)) == 0u)
                r->succeeded = 1u;
            else
                InterlockedExchange(&s->quarantined, 1);
        }
        s->binding.end(s->binding.ctx);
    }
    ExFreePool(work);
    return 1;
}

ULONG_PTR clock_perf_handle(clock_perf_scope_t* s, ULONG notify, PVOID data)
{
    ULONG i, state;
    if (!data)
        return 0;
    /* a bound provider's failed request is still a handled notify. FALSE would tell PoFx that
     * its advertised callback is unsupported. */
    if (notify == 33u)
        return req(s, data);
    if (!ready(s))
        return 0;
    switch (notify) {
        case 28u: {
            pep_query_comp_perf_caps_t* q = data;
            q->set_cnt = q->comp == 0u ? 1u : 0u;
            return q->set_cnt != 0u;
        }
        case 29u: {
            pep_query_comp_perf_set_t* q = data;
            if (q->comp || q->set)
                return 0;
            q->flags = 0u;
            q->unit = PEP_PERF_STATE_UNIT_FREQ;
            q->type = PEP_PERF_STATE_TYPE_DISCRETE;
            q->u.discrete.cnt = s->catalog.cnt;
            return 1;
        }
        case 30u: {
            pep_query_comp_perf_set_name_t* q = data;
            USHORT bytes;
            if (q->comp || q->set)
                return 0;
            for (i = 0u; i < sizeof(s->catalog.clock) && s->catalog.clock[i]; ++i) {
            }
            if (i >= sizeof(s->catalog.clock))
                return 0;
            bytes = (USHORT)((i + 1u) * sizeof(WCHAR));
            if (!q->name || q->name_size < bytes) {
                q->name_size = bytes;
                return q->name ? 0u : 1u;
            }
            for (i = 0u; i < bytes / sizeof(WCHAR); ++i)
                q->name[i] = (WCHAR)s->catalog.clock[i];
            q->name_size = bytes;
            return 1;
        }
        case 31u: {
            pep_query_comp_perf_states_t* q = data;
            if (q->comp || q->set || !q->states)
                return 0;
            for (i = 0u; i < s->catalog.cnt; ++i) {
                q->states[i].value = s->catalog.hertz[i];
                q->states[i].ctx = (PVOID)(ULONG_PTR)i;
            }
            return 1;
        }
        case 32u: {
            pep_reg_comp_perf_states_t* r = data;
            const pep_comp_perf_set_t* set;
            if (r->comp || !r->perf_state_info || r->perf_state_info->set_cnt != 1u)
                return 0;
            set = &r->perf_state_info->perf_state_sets[0];
            if (set->flags || set->type != PEP_PERF_STATE_TYPE_DISCRETE ||
                set->unit != PEP_PERF_STATE_UNIT_FREQ || set->u.discrete.cnt != s->catalog.cnt ||
                !set->u.discrete.states)
                return 0;
            for (i = 0u; i < s->catalog.cnt; ++i)
                if (set->u.discrete.states[i].value != s->catalog.hertz[i])
                    return 0;
            return current(s, &state);
        }
        case 34u: {
            pep_query_current_comp_perf_state_t* q = data;
            if (q->comp || q->set || !current(s, &state))
                return 0;
            q->u.state_idx = state;
            return 1;
        }
        default:
            return 0;
    }
}
