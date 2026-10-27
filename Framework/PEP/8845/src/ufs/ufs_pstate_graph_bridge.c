#include "ufs_pstate_graph_bridge.h"
#include <stdint.h>
#include <string.h>

#define PATH "\\_SB.UFS0"

static int provider_complete(const graph_provider_t* p)
{
    return p && p->ctx && p->supports && p->snapshot && p->apply && p->verify && p->restore &&
           p->verify_restored && p->verify_final;
}

static graph_provider_t* get_provider(ufs_pstate_graph_bridge_t* b, const power_action_t* a,
                                      uint32_t* graph_state)
{
    uintptr_t base, at, span, off;
    uint32_t idx;
    graph_provider_t* p;
    if (!b || !b->executing || !b->graph || !b->combined || !a || !b->expected_actions ||
        b->expected_actions > POWER_MAX_ACTIONS || b->combined->action_cnt != b->expected_actions)
        return NULL;
    base = (uintptr_t)&b->combined->actions[0];
    at = (uintptr_t)a;
    span = (uintptr_t)b->expected_actions * sizeof(power_action_t);
    if (at < base || at - base >= span)
        return NULL;
    off = at - base;
    if (off % sizeof(power_action_t))
        return NULL;
    idx = (uint32_t)(off / sizeof(power_action_t));
    if (a->kind <= 0 || (unsigned)a->kind >= GRAPH_PROVIDER_CNT ||
        (b->action_states[idx] != 0u && b->action_states[idx] != 3u))
        return NULL;
    p = &b->graph->providers[(unsigned)a->kind];
    if (!provider_complete(p))
        return NULL;
    if (graph_state)
        *graph_state = b->action_states[idx];
    return p;
}

static int lock(void* v)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t i, st;
    if (!b || !b->executing || !b->graph || !b->graph->acquire || !b->graph->release ||
        !b->graph->acquire(b->graph->lock_ctx))
        return 0;
    if (!b->graph->sealed || b->graph->retired || b->graph->quiescing ||
        !b->graph->ufs_catalog.compiled || !b->serial ||
        b->graph->backend_binding_serial != b->serial || !b->combined ||
        b->combined->action_cnt != b->expected_actions || strcmp(b->combined->dev_path, PATH))
        goto fail;
    /* bind every batch provider before the first snapshot or write. */

    for (i = 0; i < b->expected_actions; ++i) {
        const power_action_t* a = &b->combined->actions[i];
        graph_provider_t* p = get_provider(b, a, &st);
        if (!p || !p->supports(p->ctx, PATH, st, a))
            goto fail;
    }
    return 1;
fail:
    b->graph->release(b->graph->lock_ctx);
    return 0;
}

static void unlock(void* v)
{
    ufs_pstate_graph_bridge_t* b = v;
    if (b && b->graph && b->graph->release)
        b->graph->release(b->graph->lock_ctx);
}

static int supports(void* v, const power_action_t* a)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && p->supports(p->ctx, PATH, st, a);
}

static int snapshot(void* v, const power_action_t* a, power_snapshot_t* s)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && s && p->snapshot(p->ctx, PATH, st, a, s);
}

static int apply(void* v, const power_action_t* a)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && p->apply(p->ctx, PATH, st, a);
}

static int verify(void* v, const power_action_t* a)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && p->verify(p->ctx, PATH, st, a);
}

static int restore(void* v, const power_action_t* a, const power_snapshot_t* s)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && s && p->restore(p->ctx, PATH, st, a, s);
}

static int restored(void* v, const power_action_t* a, const power_snapshot_t* s)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t st;
    graph_provider_t* p = get_provider(b, a, &st);
    return p && s && p->verify_restored(p->ctx, PATH, st, a, s);
}

static int verify_final(void* v, const power_sequence_t* combined)
{
    ufs_pstate_graph_bridge_t* b = v;
    uint32_t kind, st, i;
    if (!b || !combined || combined != b->combined || !b->final ||
        combined->action_cnt != b->expected_actions || strcmp(combined->dev_path, PATH))
        return 0;
    /* providers ktheir own terminal semantics: e.g. multiple CLOCK config writes or GPIO reset
     * pulses cannot be validated by naively comparing every intermediate action against the
     * final hardware. */

    for (kind = 1u; kind < GRAPH_PROVIDER_CNT; ++kind)
        for (st = 0u; st <= 3u; st += 3u) {
            graph_provider_t* p = &b->graph->providers[kind];
            memset(b->final, 0, sizeof(*b->final));
            memcpy(b->final->dev_path, PATH, sizeof(PATH));
            b->final->target_d_state = st;
            for (i = 0u; i < combined->action_cnt; ++i)
                if ((unsigned)combined->actions[i].kind == kind && b->action_states[i] == st)
                    b->final->actions[b->final->action_cnt++] = combined->actions[i];
            if (b->final->action_cnt &&
                (!provider_complete(p) || !p->verify_final(p->ctx, PATH, st, b->final)))
                return 0;
        }
    return 1;
}

int ufs_pstate_graph_bridge_bind(ufs_pstate_graph_bridge_t* b, res_graph_t* g,
                                 power_sequence_t* combined, power_sequence_t* final,
                                 void* owner_ctx, int (*owner_acquire)(void*),
                                 void (*owner_release)(void*))
{
    if (!b || !g || !combined || !final || combined == final || !owner_acquire || !owner_release ||
        !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    if (!g->sealed || g->retired || g->quiescing || !g->ufs_catalog.compiled ||
        !g->backend_binding_serial) {
        g->release(g->lock_ctx);
        return 0;
    }
    memset(b, 0, sizeof(*b));
    b->graph = g;
    b->owner_lock_ctx = owner_ctx;
    b->owner_acquire = owner_acquire;
    b->owner_release = owner_release;
    b->serial = g->backend_binding_serial;
    b->combined = combined;
    b->final = final;
    g->release(g->lock_ctx);
    return 1;
}

/* the caller MUST hold the bridge's nonrecursive owner lease here. */
static power_result_t execute_owned(ufs_pstate_graph_bridge_t* b, const ufs_pstate_req_t* requests,
                                    uint32_t cnt, const power_dev_t* dev, const power_dev_t* comp,
                                    power_dev_t* per_set, power_execution_t* trace,
                                    power_scratch_t* scratch, uint32_t required_f_state)
{
    const power_pstate_program_t* programs[UFS_PSTATE_SETS];
    power_backend_t be;
    power_result_t r = POWER_BAD_INPUT;
    uint32_t i, j, k = 0, used = 0;
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        trace->result = POWER_BAD_INPUT;
    }
    if (!b || !b->owner_acquire || !b->owner_release || !b->combined || !b->final || !b->graph ||
        !requests || !cnt || cnt > UFS_PSTATE_SETS || !dev || !comp || !per_set || !trace ||
        !scratch || required_f_state > 1u)
        return r;
    if (b->executing || !b->graph->ufs_catalog.compiled)
        goto done;
    memset(b->combined, 0, sizeof(*b->combined));
    memset(b->final, 0, sizeof(*b->final));
    memset(b->action_states, 0, sizeof(b->action_states));
    for (i = 0u; i < cnt; ++i) {
        uint32_t set = requests[i].set, state = requests[i].state;
        const power_pstate_program_t* p;
        if (set >= UFS_PSTATE_SETS || state >= UFS_STATES_PER_SET || (used & (1u << set)))
            goto done;
        used |= 1u << set;
        p = ufs_catalog_get(&b->graph->ufs_catalog, 0u, set, state);
        if (!p || p->resources.action_cnt > POWER_MAX_ACTIONS - k)
            goto done;
        programs[i] = p;

        for (j = 0u; j < p->resources.action_cnt; ++j)
            b->action_states[k++] = (uint8_t)(state == 0u ? 0u : 3u);
    }
    b->expected_actions = k;
    b->executing = 1u;
    memset(&be, 0, sizeof(be));
    be.ctx = b;
    be.lock = lock;
    be.unlock = unlock;
    be.supports = supports;
    be.snapshot = snapshot;
    be.apply = apply;
    be.verify = verify;
    be.restore = restore;
    be.verify_restored = restored;
    be.verify_current_state = verify_final;
    r = power_execute_p_state_batch_from_f_state(programs, cnt, PATH, 0u, UFS_PSTATE_SETS, dev,
                                                 comp, required_f_state, per_set, &be, trace,
                                                 b->combined, scratch);
    b->executing = 0u;
done:
    b->expected_actions = 0u;
    memset(b->action_states, 0, sizeof(b->action_states));
    if (trace && r == POWER_BAD_INPUT)
        trace->result = r;
    return r;
}

power_result_t ufs_pstate_graph_execute(ufs_pstate_graph_bridge_t* b,
                                        const ufs_pstate_req_t* requests, uint32_t cnt,
                                        const power_dev_t* dev, const power_dev_t* comp,
                                        power_dev_t* per_set, power_execution_t* trace,
                                        power_scratch_t* scratch)
{
    power_result_t r = POWER_BAD_INPUT;
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        trace->result = r;
    }
    if (!b || !b->owner_acquire || !b->owner_release || !trace ||
        !b->owner_acquire(b->owner_lock_ctx))
        return r;
    r = execute_owned(b, requests, cnt, dev, comp, per_set, trace, scratch, 0u);
    b->owner_release(b->owner_lock_ctx);
    return r;
}

power_result_t ufs_fstate_graph_execute(ufs_pstate_graph_bridge_t* b, uint32_t target_f_state,
                                        const power_dev_t* dev, power_dev_t* comp,
                                        power_dev_t* per_set, power_execution_t* trace,
                                        power_scratch_t* scratch)
{
    ufs_pstate_req_t req[UFS_PSTATE_SETS];
    const power_sequence_t* f;
    power_result_t r = POWER_BAD_INPUT;
    uint32_t i, seen = 0u;
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        trace->result = r;
    }
    if (!b || !b->owner_acquire || !b->owner_release || !trace ||
        !b->owner_acquire(b->owner_lock_ctx))
        return r;
    /* the component state is an independently VERIFIED provider fact. */

    if (target_f_state >= UFS_FSTATES || !comp || !comp->state_known || comp->quarantined ||
        comp->current_d_state >= UFS_FSTATES || !b->graph || !b->graph->ufs_catalog.compiled)
        goto done;
    f = ufs_catalog_f_state(&b->graph->ufs_catalog, 0u, target_f_state);
    if (!f || f->action_cnt != UFS_PSTATE_SETS || !per_set)
        goto done;
    /* F1 resume cannot initialize an unknown P-state by invoking P0. Preserve prior hardware proof;
     * subsequent P-state requests independently require F0. */

    for (i = 0; i < UFS_PSTATE_SETS; ++i) {
        if (!per_set[i].state_known || per_set[i].quarantined ||
            per_set[i].current_d_state >= UFS_STATES_PER_SET ||
            (comp->current_d_state == 1u && per_set[i].current_d_state != 1u))
            goto done;
    }
    for (i = 0; i < UFS_PSTATE_SETS; ++i) {
        const power_action_t* a = &f->actions[i];
        uint32_t set = a->u.pstate_adjust.fields[0];
        if (a->kind != POWER_PSTATE_ADJUST || set >= UFS_PSTATE_SETS || (seen & (1u << set)) ||
            a->u.pstate_adjust.fields[1] != target_f_state)
            goto done;
        seen |= 1u << set;
        req[i].set = set;
        req[i].state = target_f_state;
    }
    if (seen != ((1u << UFS_PSTATE_SETS) - 1u))
        goto done;
    r = execute_owned(b, req, UFS_PSTATE_SETS, dev, comp, per_set, trace, scratch,
                      comp->current_d_state);
    if (r == POWER_OK) {
        comp->current_d_state = target_f_state;
        comp->state_known = 1u;
    } else if (r == POWER_ROLLBACK_FAILED) {
        comp->state_known = 0u;
        comp->quarantined = 1u;
    }
done:
    b->owner_release(b->owner_lock_ctx);
    return r;
}
