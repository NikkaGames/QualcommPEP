#include "runtime_component.h"
#include <string.h>

static int live(runtime_comp_t* s)
{
    return s && s->bound && s->epoch && !s->owner.quarantined && s->live &&
           s->live(s->ctx, s->epoch);
}

static int enter(void* v)
{
    runtime_comp_t* s = v;
    if (!live(s) || s->locked || !s->enter(s->ctx, &s->provider))
        return 0;
    s->locked = 1u;
    return 1;
}

static void release_provider(void* v)
{
    runtime_comp_t* s = v;
    if (s && s->locked) {
        s->locked = 0u;
        s->leave_cb(s->ctx);
        memset(&s->provider, 0, sizeof(s->provider));
    }
}

static int held(void* v)
{
    runtime_comp_t* s = v;
    return live(s) && s->locked;
}

static void keep(void* v)
{
    (void)v;
}

static int identity(runtime_comp_t* s, uint32_t lane, char out[256])
{
    size_t n = 0u;
    uint32_t c = s->comp;
    if (c >= 32u || lane > 32u)
        return 0;
    while (n < 256u && s->owner.idx.path[n])
        ++n;
    if (!n || n > 245u)
        return 0;
    memcpy(out, s->owner.idx.path, n);
    out[n++] = '#';
    out[n++] = 'C';
    if (c >= 10u)
        out[n++] = (char)('0' + c / 10u);
    out[n++] = (char)('0' + c % 10u);
    out[n++] = '#';
    out[n++] = lane == 32u ? 'F' : 'S';
    if (lane != 32u) {
        if (lane >= 10u)
            out[n++] = (char)('0' + lane / 10u);
        out[n++] = (char)('0' + lane % 10u);
    }
    out[n] = 0;
    return 1;
}

static int action_idx(runtime_comp_t* s, const power_action_t* a)
{
    uint32_t i;
    if (!s || !s->executing || !a)
        return -1;
    for (i = 0u; i < s->executing->action_cnt; ++i)
        if (a == &s->executing->actions[i])
            return (int)i;
    return -1;
}

static int supports(void* v, const power_action_t* a)
{
    runtime_comp_t* s = v;
    char client[256];
    uint32_t i;
    int found = 0;
    if (!held(s) || !a || !s->provider.supports)
        return 0;
    for (i = 0u; i < s->expected.action_cnt; ++i)
        if (!memcmp(a, &s->expected.actions[i], sizeof(*a))) {
            found = 1;
            if (!identity(s, s->lane[i], client) ||
                !s->provider.supports(s->provider.ctx, client, 0u, a))
                return 0;
        }
    return found;
}

#define ACTION_CALLBACK(name, member)                                                              \
    static int name(void* v, const power_action_t* a)                                              \
    {                                                                                              \
        runtime_comp_t* s = v;                                                                     \
        char client[256];                                                                          \
        int i = action_idx(s, a);                                                                  \
        return held(s) && i >= 0 && identity(s, s->lane[i], client) && s->provider.member &&       \
               s->provider.member(s->provider.ctx, client, 0u, a);                                 \
    }
ACTION_CALLBACK(apply, apply)
ACTION_CALLBACK(verify, verify)
#define SNAPSHOT_CALLBACK(name, member, qualifier)                                                 \
    static int name(void* v, const power_action_t* a, qualifier power_snapshot_t* p)               \
    {                                                                                              \
        runtime_comp_t* s = v;                                                                     \
        char client[256];                                                                          \
        int i = action_idx(s, a);                                                                  \
        return held(s) && i >= 0 && identity(s, s->lane[i], client) && s->provider.member &&       \
               s->provider.member(s->provider.ctx, client, 0u, a, p);                              \
    }
SNAPSHOT_CALLBACK(snapshot, snapshot, )
SNAPSHOT_CALLBACK(restore, restore, const)
SNAPSHOT_CALLBACK(restored, verify_restored, const)

static int matches(runtime_comp_t* s, const power_plan_t* p)
{
    return p && p->action_cnt == s->expected.action_cnt &&
           !strcmp(p->dev_path, s->expected.dev_path) &&
           !memcmp(p->actions, s->expected.actions, p->action_cnt * sizeof(p->actions[0]));
}

static int final(void* v, const power_plan_t* p)
{
    runtime_comp_t* s = v;
    uint32_t lane, i;
    if (!held(s) || !matches(s, p) || !s->provider.verify_final)
        return 0;
    /* keep each signed component/set's complete ordered action group: verify_final must see
     * the last enable/rate/config action for a clock. */
    for (lane = 0u; lane <= 32u; ++lane) {
        memset(&s->group, 0, sizeof(s->group));
        for (i = 0u; i < p->action_cnt; ++i)
            if (s->lane[i] == lane)
                s->group.actions[s->group.action_cnt++] = p->actions[i];
        if (!s->group.action_cnt)
            continue;
        if (!identity(s, lane, s->group.dev_path) ||
            !s->provider.verify_final(s->provider.ctx, s->group.dev_path, 0u, &s->group))
            return 0;
    }
    return live(s);
}

static power_result_t execute(void* v, const power_plan_t* p, power_execution_t* trace)
{
    runtime_comp_t* s = v;
    power_result_t result;
    if (!held(s) || !s->work || !matches(s, p))
        return POWER_UNSUPPORTED;
    /* an explicitly empty signed component program requires no res action, but still requires
     * the live desc/provider lease. */
    if (!p->action_cnt)
        return POWER_OK;
    s->batch = *p;
    s->batch.target_d_state = 0u;
    s->executing = &s->batch;
    /* inner callbacks operate under the outer graph/provider lease. They never acquire a
     * second graph lock or publish a separate F-state. */
    result = power_execute_action_batch(&s->batch, &s->inner, trace, &s->work->scratch);
    s->executing = NULL;
    return result;
}

static int append(runtime_comp_t* s, const power_plan_t* p, uint32_t lane)
{
    uint32_t i;
    if (!p || lane > 32u || p->action_cnt > POWER_MAX_ACTIONS - s->expected.action_cnt)
        return 0;
    for (i = 0u; i < p->action_cnt; ++i) {
        uint32_t n = s->expected.action_cnt++;
        s->expected.actions[n] = p->actions[i];
        s->lane[n] = (uint8_t)lane;
    }
    return 1;
}

static int map_f(runtime_comp_t* s, uint32_t comp, uint32_t state, comp_state_work_t* w,
                 int current)
{
    uint32_t i, used = 0u;
    program_idx_t* idx = &s->owner.idx;
    if (comp >= s->components || state >= 32u ||
        program_idx_compile(idx, s->desc, s->bytes, PROGRAM_F_STATE, comp, 0u, state, &w->f_state,
                            NULL) != POWER_OK)
        return 0;
    memset(&s->expected, 0, sizeof(s->expected));
    memcpy(s->expected.dev_path, idx->path, 256u);
    s->expected.target_d_state = state;
    s->comp = comp;
    s->work = w;
    for (i = 0u; i < w->f_state.action_cnt; ++i) {
        const power_action_t* a = &w->f_state.actions[i];
        if (a->kind == POWER_PSTATE_ADJUST) {
            uint32_t set = a->u.pstate_adjust.fields[0], target = a->u.pstate_adjust.fields[1];
            if (set >= 32u)
                return 0;
            if (used & (UINT32_C(1) << set))
                return 0;
            used |= UINT32_C(1) << set;
            if (current) {
                if (!s->owner.sets[comp][set].state_known || s->owner.sets[comp][set].quarantined)
                    return 0;
                target = s->owner.sets[comp][set].current_d_state;
            }
            if (program_idx_compile(idx, s->desc, s->bytes, PROGRAM_P_STATE, comp, set, target,
                                    NULL, &w->p_states[0]) != POWER_OK ||
                !append(s, &w->p_states[0].resources, set))
                return 0;
        } else {
            uint32_t n = s->expected.action_cnt;
            if (n >= POWER_MAX_ACTIONS)
                return 0;
            s->expected.actions[n] = *a;
            s->lane[n] = 32u;
            ++s->expected.action_cnt;
        }
    }
    return 1;
}

static int map_p(runtime_comp_t* s, uint32_t comp, const uint32_t* sets, const uint32_t* ordinals,
                 uint32_t cnt, comp_state_work_t* w)
{
    uint32_t i, set, state, used = 0u;
    if (!sets || !ordinals || !cnt || cnt > 32u || comp >= s->components)
        return 0;
    memset(&s->expected, 0, sizeof(s->expected));
    memcpy(s->expected.dev_path, s->owner.idx.path, 256u);
    s->comp = comp;
    s->work = w;
    for (i = 0u; i < cnt; ++i) {
        if (!po_fx_catalog_resolve_state(&s->catalog, comp, sets[i], ordinals[i], &set, &state) ||
            (used & (UINT32_C(1) << set)) ||
            program_idx_compile(&s->owner.idx, s->desc, s->bytes, PROGRAM_P_STATE, comp, set, state,
                                NULL, &w->p_states[0]) != POWER_OK ||
            !append(s, &w->p_states[0].resources, set))
            return 0;
        used |= UINT32_C(1) << set;
    }
    return 1;
}

static int establish(runtime_comp_t* s, comp_state_work_t* w)
{
    uint32_t c, i, set, state;
    power_execution_t trace;
    power_pstate_selection_t selection;
    for (c = 0u; c < s->components; ++c) {
        if (!map_f(s, c, 0u, w, 0))
            return 0;
        s->retain = 1u;
        if (power_execute_f_state(&s->expected, &s->owner.comp[c], &s->adapter, &trace,
                                  &w->scratch) != POWER_OK)
            return 0;

        if (program_idx_compile(&s->owner.idx, s->desc, s->bytes, PROGRAM_F_STATE, c, 0u, 0u,
                                &w->f_state, NULL) != POWER_OK)
            return 0;
        memset(&selection, 0, sizeof(selection));
        for (i = 0u; i < w->f_state.action_cnt; ++i) {
            const power_action_t* a = &w->f_state.actions[i];
            if (a->kind != POWER_PSTATE_ADJUST)
                continue;
            set = a->u.pstate_adjust.fields[0];
            state = a->u.pstate_adjust.fields[1];
            if (set >= 32u || state >= 32u || (selection.used_sets & (UINT32_C(1) << set)))
                return 0;
            selection.used_sets |= UINT32_C(1) << set;
            s->owner.sets[c][set].state_known = 1u;
            s->owner.sets[c][set].current_d_state = state;
        }
        /* some f0 programs enable resources without selecting a P-state. Establish the desc's
         * first discrete state physically before publishing its initial ordinal; never infer it
         * from an f0 cache. */
        for (set = 0u; set < s->catalog.set_cnt[c]; ++set) {
            uint32_t selector, cnt, ordinal = 0u, previous;
            if (!po_fx_catalog_resolve_set(&s->catalog, c, set, &selector, &cnt))
                return 0;
            if (selection.used_sets & (UINT32_C(1) << selector))
                continue;
            previous = s->owner.sets[c][selector].current_d_state;
            if (s->proof_invalidated &&
                !program_p_state_to_ordinal(&s->owner.idx, c, selector, previous, &ordinal))
                return 0;
            if (!map_p(s, c, &set, &ordinal, 1u, w) || !enter(s))
                return 0;
            {
                power_result_t r = execute(s, &s->expected, &trace);
                release_provider(s);
                if (r != POWER_OK)
                    return 0;
            }
            if (!po_fx_catalog_resolve_state(&s->catalog, c, set, ordinal, NULL, &state))
                return 0;
            s->owner.sets[c][selector].state_known = 1u;
            s->owner.sets[c][selector].current_d_state = state;
        }
    }
    s->dev_envelope.state_known = 1u;
    s->dev_in_d0 = 1u;
    s->proof_invalidated = 0u;
    return 1;
}

void runtime_comp_forget(runtime_comp_t* s)
{
    uint32_t c, set;
    if (!s || !s->bound)
        return;
    s->proof_invalidated = 1u;
    s->dev_in_d0 = 0u;
    s->dev_envelope.state_known = 0u;
    for (c = 0u; c < s->components; ++c) {
        s->owner.comp[c].state_known = 0u;
        for (set = 0u; set < 32u; ++set)
            s->owner.sets[c][set].state_known = 0u;
    }
}

power_result_t runtime_comp_f(runtime_comp_t* s, uint32_t comp, uint32_t state,
                              comp_state_work_t* w, power_execution_t* trace)
{
    int repeated, ok;
    if (!live(s) || !s->dev_in_d0 || !w || !trace || comp >= s->components)
        return POWER_UNSUPPORTED;
    repeated = s->owner.comp[comp].state_known && s->owner.comp[comp].current_d_state == state;
    if (!map_f(s, comp, state, w, repeated))
        return POWER_UNSUPPORTED;
    if (repeated) {
        if (!enter(s))
            return POWER_UNSUPPORTED;
        ok = final(s, &s->expected);
        release_provider(s);
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        if (!ok) {
            s->owner.quarantined = 1u;
            return (trace->result = POWER_STATE_DRIFT);
        }
        return (trace->result = POWER_OK);
    }
    return po_fx_endpoint_execute_f_state(&s->endpoint, s->epoch, comp, state, w, trace);
}

power_result_t runtime_comp_p(runtime_comp_t* s, uint32_t comp, const uint32_t* sets,
                              const uint32_t* states, uint32_t cnt, comp_state_work_t* w,
                              power_execution_t* trace)
{
    if (!live(s) || !s->dev_in_d0 || !w || !trace || !map_p(s, comp, sets, states, cnt, w))
        return POWER_UNSUPPORTED;
    return po_fx_endpoint_execute(&s->endpoint, s->epoch, comp, sets, states, cnt, w, trace);
}

int runtime_comp_query(runtime_comp_t* s, uint32_t comp, uint32_t set_ordinal, comp_state_work_t* w,
                       uint32_t* ordinal)
{
    uint32_t set, cnt, current;
    if (!live(s) || !w || !ordinal ||
        !po_fx_catalog_resolve_set(&s->catalog, comp, set_ordinal, &set, &cnt) ||
        !s->owner.sets[comp][set].state_known ||
        !program_p_state_to_ordinal(&s->owner.idx, comp, set,
                                    s->owner.sets[comp][set].current_d_state, &current) ||
        !map_p(s, comp, &set_ordinal, &current, 1u, w))
        return 0;
    return po_fx_endpoint_query_current(&s->endpoint, s->epoch, comp, set_ordinal, w, ordinal);
}

int runtime_comp_dx(runtime_comp_t* s, uint32_t target, comp_state_work_t* w)
{
    uint32_t c;
    if (!live(s) || !w || (target != 0u && target != 3u))
        return 0;
    if (s->proof_invalidated) {
        if (target != 0u)
            return 0;
        if (establish(s, w))
            return 1;
        s->owner.quarantined = 1u;
        return 0;
    }
    for (c = 0u; c < s->components; ++c) {
        const power_dev_t* actual = &s->owner.comp[c];
        if (!actual->state_known || actual->quarantined ||
            !map_f(s, c, actual->current_d_state, w, 1) ||
            (target == 3u && actual->current_d_state != 1u &&
             (s->expected.action_cnt || s->catalog.set_cnt[c])) ||
            !enter(s))
            return 0;
        {
            int ok = final(s, &s->expected);
            release_provider(s);
            if (!ok)
                return 0;
        }
    }
    s->dev_in_d0 = target == 0u;
    return 1;
}

int runtime_comp_close(runtime_comp_t* s, comp_state_work_t* w)
{
    uint32_t c;
    power_execution_t trace;
    if (!live(s) || !w)
        return 0;
    s->dev_in_d0 = 1u;
    for (c = 0u; c < s->components; ++c) {
        if (!s->owner.comp[c].state_known || !map_f(s, c, 1u, w, 0) ||
            comp_owner_execute_f_state(&s->owner, s->desc, s->bytes, c, 1u, &s->dev_envelope,
                                       &s->adapter, s->epoch, w, &trace) != POWER_OK)
            return 0;
    }
    if (!runtime_comp_dx(s, 3u, w))
        return 0;
    po_fx_endpoint_retire(&s->endpoint);
    s->retain = 0u;
    return 1;
}

int runtime_comp_bind(runtime_comp_t* s, const void* blob, size_t bytes, const char* path,
                      uint64_t epoch, void* ctx, int (*begin)(void*, hw_backend_t*),
                      void (*end)(void*), int (*is_live)(void*, uint64_t),
                      runtime_desc_info_t* info, comp_state_work_t* w)
{
    uint32_t c, i, set, state;
    if (!s || !blob || !bytes || !path || !epoch || !begin || !end || !is_live || !info || !w ||
        s->bound || !program_idx_describe(blob, bytes, path, info, &s->owner.idx) ||
        s->owner.idx.d_state_mask || !s->owner.idx.comp_mask)
        return 0;
    for (i = 0u; i < info->dev_cnt; ++i)
        if (!strcmp(info->devices[i].path, path) && info->devices[i].crashdump_state != UINT32_MAX)
            return 0;
    while (s->components < 32u && (s->owner.idx.comp_mask & (UINT32_C(1) << s->components)))
        ++s->components;
    if (s->components < 32u && (s->owner.idx.comp_mask >> s->components))
        return 0;
    if (!po_fx_catalog_bind(&s->catalog, &s->owner.idx, s->components))
        return 0;
    /* bind copies Index; don't pass the destination's own storage to a binder that clears it
     * before copying. catalog retains the same idx. */
    if (!comp_owner_bind(&s->owner, &s->catalog.idx, epoch))
        return 0;
    s->desc = blob;
    s->bytes = bytes;
    s->epoch = epoch;
    s->ctx = ctx;
    s->enter = begin;
    s->leave_cb = end;
    s->live = is_live;
    s->bound = 1u;
    s->adapter.ctx = s;
    s->adapter.lock = enter;
    s->adapter.unlock = release_provider;
    s->adapter.supports = supports;
    s->adapter.verify_current_state = final;
    s->adapter.execute_owned_program = execute;
    s->inner = s->adapter;
    s->inner.lock = held;
    s->inner.unlock = keep;
    s->inner.execute_owned_program = NULL;
    s->inner.snapshot = snapshot;
    s->inner.apply = apply;
    s->inner.verify = verify;
    s->inner.restore = restore;
    s->inner.verify_restored = restored;
    /* preflight every advertised F/P program before the first res vote. */
    for (c = 0u; c < s->components; ++c) {
        if ((s->owner.idx.f_state_mask[c] & 3u) != 3u)
            goto fail;
        for (state = 0u; state < 32u; ++state)
            if (s->owner.idx.f_state_mask[c] & (UINT32_C(1) << state)) {
                if (!map_f(s, c, state, w, 0) || !enter(s))
                    goto fail;
                for (i = 0u; i < s->expected.action_cnt; ++i)
                    if (!supports(s, &s->expected.actions[i]))
                        break;
                release_provider(s);
                if (i != s->expected.action_cnt)
                    goto fail;
            }
        for (set = 0u; set < s->catalog.set_cnt[c]; ++set) {
            uint32_t selector, cnt;
            if (!po_fx_catalog_resolve_set(&s->catalog, c, set, &selector, &cnt))
                goto fail;
            for (state = 0u; state < cnt; ++state) {
                if (!map_p(s, c, &set, &state, 1u, w) || !enter(s))
                    goto fail;
                for (i = 0u; i < s->expected.action_cnt; ++i)
                    if (!supports(s, &s->expected.actions[i]))
                        break;
                release_provider(s);
                if (i != s->expected.action_cnt)
                    goto fail;
            }
        }
    }
    if (!establish(s, w))
        goto fail;
    /* the device has no independent Dx script. Its execution envelope is established only
     * after all component f0 res programs verified. */
    s->dev_envelope.state_known = 1u;
    s->dev_in_d0 = 1u;
    s->endpoint.catalog = &s->catalog;
    s->endpoint.owner = &s->owner;
    s->endpoint.desc = blob;
    s->endpoint.desc_bytes = bytes;
    s->endpoint.physical_d0 = &s->dev_envelope;
    s->endpoint.backend = &s->adapter;
    s->endpoint.owner_epoch = epoch;
    s->endpoint.bound = 1u;
    for (c = 0u; c < s->components; ++c)
        for (set = 0u; set < s->catalog.set_cnt[c]; ++set)
            if (!runtime_comp_query(s, c, set, w, &state))
                goto fail;
    return 1;
fail:
    if (s->locked)
        release_provider(s);
    if (s->retain) {
        s->owner.quarantined = 1u;
        s->endpoint.bound = 0u;
    }
    return 0;
}
