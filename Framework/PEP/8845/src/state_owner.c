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

static power_result_t d_state_components(comp_state_owner_t* o, const void* desc, size_t bytes,
                                         uint32_t target, uint32_t states[32],
                                         comp_state_work_t* work)
{
    uint32_t c, i, f;
    power_result_t result;
    result = program_idx_compile(&o->idx, desc, bytes, PROGRAM_D_STATE, 0u, 0u, target,
                                 &work->expanded, NULL);
    if (result != POWER_OK)
        return result;
    for (i = 0u; i < work->expanded.action_cnt; ++i) {
        const power_action_t* a = &work->expanded.actions[i];
        uint32_t owners = 0u;
        if (a->kind != POWER_PSTATE_ADJUST)
            continue;
        if (a->u.pstate_adjust.fields[0] >= 32u || a->u.pstate_adjust.fields[1] >= 32u)
            return POWER_INVALID_PROGRAM;
        for (c = 0u; c < 32u; ++c)
            if ((o->comp_mask & (UINT32_C(1) << c)) &&
                (o->idx.p_state_set_mask[c] & (UINT32_C(1) << a->u.pstate_adjust.fields[0])) &&
                (o->idx.p_state_mask[c][a->u.pstate_adjust.fields[0]] &
                 (UINT32_C(1) << a->u.pstate_adjust.fields[1])))
                ++owners;
        if (owners != 1u)
            return POWER_INVALID_PROGRAM;
    }
    for (c = 0u; c < 32u; ++c) {
        uint32_t d_sets = 0u, d_targets[32] = {0}, matches = 0u;
        states[c] = UINT32_MAX;
        if (!(o->comp_mask & (UINT32_C(1) << c)))
            continue;
        for (i = 0u; i < work->expanded.action_cnt; ++i) {
            const power_action_t* a = &work->expanded.actions[i];
            uint32_t set, state;
            if (a->kind != POWER_PSTATE_ADJUST)
                continue;
            set = a->u.pstate_adjust.fields[0];
            state = a->u.pstate_adjust.fields[1];
            if (!(o->idx.p_state_set_mask[c] & (UINT32_C(1) << set)))
                continue;
            if (d_sets & (UINT32_C(1) << set))
                return POWER_INVALID_PROGRAM;
            d_sets |= UINT32_C(1) << set;
            d_targets[set] = state;
        }
        if (!d_sets)
            continue;
        for (f = 0u; f < 32u; ++f) {
            uint32_t f_sets = 0u;
            if (!(o->idx.f_state_mask[c] & (UINT32_C(1) << f)))
                continue;
            result = program_idx_compile(&o->idx, desc, bytes, PROGRAM_F_STATE, c, 0u, f,
                                         &work->f_state, NULL);
            if (result != POWER_OK)
                return result;
            for (i = 0u; i < work->f_state.action_cnt; ++i) {
                const power_action_t* a = &work->f_state.actions[i];
                uint32_t set, state;
                if (a->kind != POWER_PSTATE_ADJUST)
                    break;
                set = a->u.pstate_adjust.fields[0];
                state = a->u.pstate_adjust.fields[1];
                if (set >= 32u || !(d_sets & (UINT32_C(1) << set)) ||
                    (f_sets & (UINT32_C(1) << set)) || d_targets[set] != state)
                    break;
                f_sets |= UINT32_C(1) << set;
            }
            if (i == work->f_state.action_cnt && f_sets == d_sets) {
                states[c] = f;
                ++matches;
            }
        }
        if (matches != 1u)
            return POWER_UNSUPPORTED;
    }
    return POWER_OK;
}

int comp_owner_verify_f_state(comp_state_owner_t* o, const void* desc, size_t bytes, uint32_t comp,
                              uint32_t state, const power_backend_t* backend, uint64_t epoch,
                              comp_state_work_t* work)
{
    power_pstate_selection_t selection;
    uint32_t i, count = 0u, used = 0u;
    int ok;
    if (!o || !o->bound || o->quarantined || !epoch || o->owner_epoch != epoch || !desc || !work ||
        comp >= 32u || !(o->comp_mask & (UINT32_C(1) << comp)) || state >= 32u ||
        !o->comp[comp].state_known || o->comp[comp].quarantined ||
        o->comp[comp].current_d_state != state || !backend || !backend->lock || !backend->unlock ||
        !backend->verify_current_state ||
        program_idx_compile(&o->idx, desc, bytes, PROGRAM_F_STATE, comp, 0u, state, &work->f_state,
                            NULL) != POWER_OK)
        return 0;
    for (i = 0u; i < work->f_state.action_cnt; ++i) {
        const power_action_t* a = &work->f_state.actions[i];
        uint32_t set, target;
        if (a->kind != POWER_PSTATE_ADJUST)
            continue;
        set = a->u.pstate_adjust.fields[0];
        target = a->u.pstate_adjust.fields[1];
        if (set >= 32u || target >= 32u || (used & (UINT32_C(1) << set)) ||
            !o->sets[comp][set].state_known || o->sets[comp][set].quarantined ||
            o->sets[comp][set].current_d_state != target || count == 32u ||
            program_idx_compile(&o->idx, desc, bytes, PROGRAM_P_STATE, comp, set, target, NULL,
                                &work->p_states[count]) != POWER_OK)
            return 0;
        used |= UINT32_C(1) << set;
        work->program_refs[count] = &work->p_states[count];
        ++count;
    }
    if (power_expand_comp_f_state(&work->f_state, comp, work->program_refs, count, 32u,
                                  &work->expanded, &selection) != POWER_OK ||
        !backend->lock(backend->ctx))
        return 0;
    ok = o->owner_epoch == epoch && !o->quarantined &&
         backend->verify_current_state(backend->ctx, &work->expanded);
    backend->unlock(backend->ctx);
    return ok;
}

power_result_t comp_owner_execute_d_state(comp_state_owner_t* o, const void* desc, size_t bytes,
                                          uint32_t target, const power_dev_t* dev,
                                          const power_backend_t* backend, uint64_t epoch, void* ctx,
                                          power_result_t (*execute_device)(void*, uint32_t),
                                          comp_state_work_t* work, power_execution_t* trace)
{
    uint32_t states[32], c;
    power_result_t result;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!o || !o->bound || !desc || !bytes || target >= POWER_DSTATE_COUNT || !dev || !backend ||
        !epoch || o->owner_epoch != epoch || !execute_device || !work)
        return refuse(trace, POWER_BAD_INPUT);
    if (o->quarantined || dev->quarantined)
        return refuse(trace, POWER_QUARANTINED);
    result = d_state_components(o, desc, bytes, target, states, work);
    if (result != POWER_OK)
        return refuse(trace, result);
    if (target) {
        if (!dev->state_known || dev->current_d_state >= POWER_DSTATE_COUNT)
            return refuse(trace, POWER_NO_KNOWN_STATE);
        for (c = 0u; c < 32u; ++c) {
            if (states[c] == UINT32_MAX)
                continue;
            if (dev->state_known && dev->current_d_state == 0u) {
                result = comp_owner_execute_f_state(o, desc, bytes, c, states[c], dev, backend,
                                                    epoch, work, trace);
                if (result != POWER_OK)
                    return result;
            } else if (!comp_owner_verify_f_state(o, desc, bytes, c, states[c], backend, epoch,
                                                  work))
                return refuse(trace, POWER_NO_KNOWN_STATE);
        }
    }
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    result = execute_device(ctx, target);
    if (result != POWER_OK)
        return (trace->result = result);
    if (!dev->state_known || dev->quarantined || dev->current_d_state != target) {
        o->quarantined = 1u;
        return (trace->result = POWER_FINAL_STATE_FAILED);
    }
    if (!target)
        for (c = 0u; c < 32u; ++c) {
            if (states[c] == UINT32_MAX)
                continue;
            result = comp_owner_execute_f_state(o, desc, bytes, c, states[c], dev, backend, epoch,
                                                work, trace);
            if (result != POWER_OK)
                return result;
        }
    return (trace->result = POWER_OK);
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
