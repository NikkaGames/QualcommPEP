#include "state_owner.h"
#include <string.h>

static power_result_t refuse(power_execution_t* t, power_result_t r)
{
    if (t) {
        memset(t, 0, sizeof(*t));
        t->failure_idx = UINT32_MAX;
        t->result = r;
    }
    return r;
}

static int ready(const comp_state_owner_t* o, const power_dev_t* d, const power_backend_t* b,
                 uint64_t epoch, uint32_t c)
{
    return o && o->bound && !o->quarantined && o->owner_epoch && o->owner_epoch == epoch &&
           c < 32u && (o->comp_mask & (UINT32_C(1) << c)) && d && d->state_known &&
           !d->quarantined && d->current_d_state == 0u && b && b->lock && b->unlock &&
           b->supports &&
           (b->execute_owned_program ||
            (b->snapshot && b->apply && b->verify && b->restore && b->verify_restored)) &&
           b->verify_current_state;
}

int comp_owner_bind(comp_state_owner_t* o, const program_idx_t* idx, uint64_t epoch)
{
    if (!o || !idx || !idx->valid || !idx->comp_mask || !epoch || o->bound || o->quarantined)
        return 0;
    memset(o, 0, sizeof(*o));
    o->idx = *idx;
    o->comp_mask = idx->comp_mask;
    o->owner_epoch = epoch;
    o->bound = 1u;
    return 1;
}

power_result_t comp_owner_execute_f_state(comp_state_owner_t* o, const void* desc, size_t bytes,
                                          uint32_t comp, uint32_t state, const power_dev_t* dev,
                                          const power_backend_t* backend, uint64_t epoch,
                                          comp_state_work_t* work, power_execution_t* trace)
{
    power_result_t r;
    uint32_t i, n = 0u, used = 0u;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!ready(o, dev, backend, epoch, comp) || !work || !desc || state >= 32u ||
        !(o->idx.f_state_mask[comp] & (UINT32_C(1) << state)))
        return refuse(trace, POWER_UNSUPPORTED);
    r = program_idx_compile(&o->idx, desc, bytes, PROGRAM_F_STATE, comp, 0u, state, &work->f_state,
                            NULL);
    if (r != POWER_OK)
        return refuse(trace, r);
    for (i = 0u; i < work->f_state.action_cnt; ++i) {
        const power_action_t* a = &work->f_state.actions[i];
        uint32_t set, pstate;
        if (a->kind != POWER_PSTATE_ADJUST)
            continue;
        set = a->u.pstate_adjust.fields[0];
        pstate = a->u.pstate_adjust.fields[1];
        if (set >= 32u || pstate >= 32u || (used & (UINT32_C(1) << set)) ||
            !(o->idx.p_state_set_mask[comp] & (UINT32_C(1) << set)) ||
            !(o->idx.p_state_mask[comp][set] & (UINT32_C(1) << pstate)) || n >= 32u)
            return refuse(trace, POWER_INVALID_PROGRAM);
        used |= UINT32_C(1) << set;
        r = program_idx_compile(&o->idx, desc, bytes, PROGRAM_P_STATE, comp, set, pstate, NULL,
                                &work->p_states[n]);
        if (r != POWER_OK)
            return refuse(trace, r);
        work->program_refs[n] = &work->p_states[n];
        ++n;
    }
    if (n) {
        /* expansion precedes the provider lock; the same immutable sequence must be used by
         * preflight, apply, and final readback. */

        r = power_execute_comp_f_state(&work->f_state, comp, work->program_refs, n, dev,
                                       &o->comp[comp], o->sets[comp], 32u, backend, trace,
                                       &work->expanded, &work->scratch);
    } else {
        r = power_execute_f_state(&work->f_state, &o->comp[comp], backend, trace, &work->scratch);
    }
    if (r == POWER_ROLLBACK_FAILED || o->comp[comp].quarantined)
        o->quarantined = 1u;
    return r;
}

power_result_t comp_owner_execute_p_states(comp_state_owner_t* o, const void* desc, size_t bytes,
                                           uint32_t comp, const uint32_t* sets,
                                           const uint32_t* states, uint32_t cnt,
                                           const power_dev_t* dev, const power_backend_t* backend,
                                           uint64_t epoch, comp_state_work_t* work,
                                           power_execution_t* trace)
{
    uint32_t i, mask = 0u;
    power_result_t r;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!ready(o, dev, backend, epoch, comp) || !work || !desc || !sets || !states || !cnt ||
        cnt > 32u || !o->comp[comp].state_known || o->comp[comp].quarantined ||
        o->comp[comp].current_d_state != 0u)
        return refuse(trace, POWER_UNSUPPORTED);
    for (i = 0u; i < cnt; ++i) {
        uint32_t set = sets[i], st = states[i];
        if (set >= 32u || st >= 32u || (mask & (UINT32_C(1) << set)) ||
            !(o->idx.p_state_set_mask[comp] & (UINT32_C(1) << set)) ||
            !(o->idx.p_state_mask[comp][set] & (UINT32_C(1) << st)))
            return refuse(trace, POWER_INVALID_PROGRAM);
        mask |= UINT32_C(1) << set;
        r = program_idx_compile(&o->idx, desc, bytes, PROGRAM_P_STATE, comp, set, st, NULL,
                                &work->p_states[i]);
        if (r != POWER_OK)
            return refuse(trace, r);
        work->program_refs[i] = &work->p_states[i];
    }
    r = power_execute_p_state_batch(work->program_refs, cnt, o->idx.path, comp, 32u, dev,
                                    &o->comp[comp], o->sets[comp], backend, trace, &work->expanded,
                                    &work->scratch);
    if (r == POWER_ROLLBACK_FAILED || o->comp[comp].quarantined)
        o->quarantined = 1u;
    return r;
}

power_result_t comp_owner_execute_po_fx_p_states(comp_state_owner_t* o, const void* desc,
                                                 size_t bytes, uint32_t comp, const uint32_t* sets,
                                                 const uint32_t* ordinals, uint32_t cnt,
                                                 const power_dev_t* dev,
                                                 const power_backend_t* backend, uint64_t epoch,
                                                 comp_state_work_t* work, power_execution_t* trace)
{
    uint32_t i;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!o || !o->bound || !work || !sets || !ordinals || !cnt || cnt > 32u || comp >= 32u)
        return refuse(trace, POWER_UNSUPPORTED);
    for (i = 0u; i < cnt; ++i)
        if (!program_p_state_from_ordinal(&o->idx, comp, sets[i], ordinals[i],
                                          &work->po_fx_selectors[i]))
            return refuse(trace, POWER_UNSUPPORTED);
    return comp_owner_execute_p_states(o, desc, bytes, comp, sets, work->po_fx_selectors, cnt, dev,
                                       backend, epoch, work, trace);
}
