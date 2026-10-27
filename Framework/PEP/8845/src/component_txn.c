/* resolve PSTATE_ADJUST requests into one physical F-state transaction. The backend lock
 * is nonrecursive: NEVER run another complete power_execute_p_state from inside a
 * PSTATE_ADJUST apply callback. */

#include "state_txn.h"
#include <string.h>

static int valid_path(const char* path, size_t size)
{
    size_t i;
    if (!path || size < 6u || path[0] != '\\')
        return 0;
    for (i = 1u; i < size; ++i)
        if (!path[i])
            return i >= 5u;
    return 0;
}

static int same_path(const char* a, const char* b, size_t cap)
{
    size_t i;
    if (!valid_path(a, cap) || !valid_path(b, cap))
        return 0;
    for (i = 0; i < cap; ++i) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return 1;
    }
    return 0;
}

power_result_t power_expand_comp_f_state(const power_sequence_t* fstate, uint32_t comp,
                                         const power_pstate_program_t* const* programs,
                                         uint32_t program_cnt, uint32_t set_cnt,
                                         power_sequence_t* expanded,
                                         power_pstate_selection_t* selection)
{
    uint32_t i, j, k;
    power_result_t result = POWER_INVALID_PROGRAM;
    if (!expanded || !selection || expanded == fstate)
        return POWER_BAD_INPUT;
    memset(expanded, 0, sizeof(*expanded));
    memset(selection, 0, sizeof(*selection));
    if (!fstate || !valid_path(fstate->dev_path, sizeof(fstate->dev_path)) ||
        fstate->target_d_state >= 32u || fstate->action_cnt > POWER_MAX_ACTIONS || comp >= 32u ||
        set_cnt == 0u || set_cnt > 32u || program_cnt > 1024u || (program_cnt && !programs))
        return POWER_BAD_INPUT;
    memcpy(expanded->dev_path, fstate->dev_path, sizeof(expanded->dev_path));
    expanded->target_d_state = fstate->target_d_state;
    for (i = 0; i < fstate->action_cnt; ++i) {
        const power_action_t* action = &fstate->actions[i];
        if (action->kind == POWER_PSTATE_ADJUST) {
            uint32_t set = action->u.pstate_adjust.fields[0];
            uint32_t state = action->u.pstate_adjust.fields[1];
            const power_pstate_program_t* match = NULL;
            if (set >= set_cnt || state >= 32u || (selection->used_sets & (1u << set)))
                goto fail;
            for (j = 0; j < program_cnt; ++j) {
                const power_pstate_program_t* p = programs[j];
                if (!p || p->comp != comp || p->set != set || p->state != state)
                    continue;
                if (match)
                    goto fail;
                match = p;
            }
            if (!match || match->resources.target_d_state != state ||
                !same_path(match->resources.dev_path, fstate->dev_path, sizeof(fstate->dev_path)) ||
                !match->resources.action_cnt || match->resources.action_cnt > POWER_MAX_ACTIONS ||
                match->resources.action_cnt > POWER_MAX_ACTIONS - expanded->action_cnt)
                goto fail;
            for (k = 0; k < match->resources.action_cnt; ++k)
                if (match->resources.actions[k].kind == POWER_PSTATE_ADJUST)
                    goto fail;
            memcpy(&expanded->actions[expanded->action_cnt], match->resources.actions,
                   match->resources.action_cnt * sizeof(power_action_t));
            expanded->action_cnt += match->resources.action_cnt;
            selection->used_sets |= 1u << set;
            selection->target_state[set] = (uint8_t)state;
        } else {
            /* f-state direct actions are valid, but recursive/unknown kinds must never be passed to
             * a backend as arbitrary memory. */

            if (action->kind < POWER_CLOCK || action->kind > POWER_FOOTSWITCH ||
                expanded->action_cnt == POWER_MAX_ACTIONS)
                goto fail;
            expanded->actions[expanded->action_cnt++] = *action;
        }
    }
    result = POWER_OK;
    return result;
fail:
    memset(expanded, 0, sizeof(*expanded));
    memset(selection, 0, sizeof(*selection));
    return result;
}

power_result_t power_execute_comp_f_state(const power_sequence_t* fstate, uint32_t comp,
                                          const power_pstate_program_t* const* programs,
                                          uint32_t program_cnt, const power_dev_t* dev,
                                          power_dev_t* f_state, power_dev_t* sets, uint32_t set_cnt,
                                          const power_backend_t* backend, power_execution_t* trace,
                                          power_sequence_t* expanded, power_scratch_t* scratch)
{
    power_pstate_selection_t selection;
    power_result_t result;
    uint32_t i;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!dev || !f_state || !sets || !expanded || !scratch || !backend || !dev->state_known ||
        dev->quarantined || dev->current_d_state != 0u) {
        trace->result = POWER_NO_KNOWN_STATE;
        return trace->result;
    }
    result = power_expand_comp_f_state(fstate, comp, programs, program_cnt, set_cnt, expanded,
                                       &selection);
    if (result != POWER_OK)
        return (trace->result = result);
    /* validate EVERY set before touching any hardware. Nested P1 cannot be assumed to
     * originate from P0 without a verified owner state. */

    for (i = 0; i < set_cnt; ++i)
        if ((selection.used_sets & (1u << i)) != 0u) {
            if (sets[i].quarantined || (!sets[i].state_known && selection.target_state[i] != 0u) ||
                (sets[i].state_known && sets[i].current_d_state >= 32u))
                return (trace->result = POWER_NO_KNOWN_STATE);
        }
    /* execute ONCE under the backend's res lock: all three UFS P-state sets share snapshots, a
     * single atomic commit, and reverse rollback. current_f_state and per-set cache only
     * advance after physical verification. */

    result = power_execute_f_state(expanded, f_state, backend, trace, scratch);
    if (result == POWER_OK) {
        for (i = 0; i < set_cnt; ++i)
            if ((selection.used_sets & (1u << i)) != 0u) {
                sets[i].current_d_state = selection.target_state[i];
                sets[i].state_known = 1u;
            }
    } else if (result == POWER_ROLLBACK_FAILED || f_state->quarantined) {
        /* quarantine every participating set when the affected hardware subset is unknown. */

        for (i = 0; i < set_cnt; ++i)
            if ((selection.used_sets & (1u << i)) != 0u) {
                sets[i].state_known = 0u;
                sets[i].quarantined = 1u;
            }
    }
    return result;
}

/* pofx V2 can update several independent UFS PSTATE_SETs at once. The older generic
 * CompPerf path cannot represent this: its per-component scalar cache and CPU-derived OPP
 * table do not describe the UFS AEOB. */

power_result_t power_execute_p_state_batch_from_f_state(
    const power_pstate_program_t* const* programs, uint32_t cnt, const char* dev_path,
    uint32_t comp, uint32_t set_cnt, const power_dev_t* dev, const power_dev_t* fstate,
    uint32_t expected_current_f_state, power_dev_t* sets, const power_backend_t* backend,
    power_execution_t* trace, power_sequence_t* combined, power_scratch_t* scratch)
{
    uint32_t used = 0u, i, j, total_delay = 0u;
    power_result_t result;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!programs || !cnt || cnt > 32u || !set_cnt || set_cnt > 32u || cnt > set_cnt ||
        comp >= 32u || !dev_path || !valid_path(dev_path, 256u) || !dev || !fstate || !sets ||
        expected_current_f_state >= 32u || !backend || !combined || !scratch)
        return (trace->result = POWER_BAD_INPUT);
    memset(combined, 0, sizeof(*combined));
    if (!dev->state_known || dev->quarantined || dev->current_d_state != 0u ||
        !fstate->state_known || fstate->quarantined ||
        fstate->current_d_state != expected_current_f_state)
        return (trace->result = POWER_NO_KNOWN_STATE);
    memcpy(combined->dev_path, dev_path, strlen(dev_path) + 1u);

    combined->target_d_state = 0u;
    for (i = 0u; i < cnt; ++i) {
        const power_pstate_program_t* p = programs[i];
        uint32_t mask;
        if (!p || p->comp != comp || p->set >= set_cnt || p->state >= 32u ||
            p->resources.target_d_state != p->state ||
            !same_path(p->resources.dev_path, dev_path, 256u) || !p->resources.action_cnt ||
            p->resources.action_cnt > POWER_MAX_ACTIONS ||
            p->resources.action_cnt > POWER_MAX_ACTIONS - combined->action_cnt)
            goto invalid;
        mask = 1u << p->set;
        if (used & mask)
            goto invalid;
        used |= mask;
        if (sets[p->set].quarantined || (!sets[p->set].state_known && p->state != 0u) ||
            (sets[p->set].state_known && sets[p->set].current_d_state >= 32u)) {
            result = POWER_NO_KNOWN_STATE;
            goto failed;
        }
        for (j = 0u; j < p->resources.action_cnt; ++j) {
            const power_action_t* a = &p->resources.actions[j];
            if (a->kind < POWER_CLOCK || a->kind > POWER_FOOTSWITCH ||
                a->kind == POWER_PSTATE_ADJUST)
                goto invalid;
            if (a->kind == POWER_DELAY) {
                if (a->u.delay.ms > 5000u || total_delay > 10000u - a->u.delay.ms)
                    goto invalid;
                total_delay += a->u.delay.ms;
            }
            combined->actions[combined->action_cnt++] = *a;
        }
    }
    /* the common executor serializes the entire list under one lock, and includes failing
     * partially-written actions in reverse rollback. */

    result = power_execute_action_batch(combined, backend, trace, scratch);
    if (result == POWER_OK) {
        for (i = 0u; i < cnt; ++i) {
            const power_pstate_program_t* p = programs[i];
            sets[p->set].state_known = 1u;
            sets[p->set].current_d_state = p->state;
        }
    } else if (result == POWER_ROLLBACK_FAILED) {
        /* hardware state is ambiguous for ALL participants: retain sticky quarantine rather than
         * replaying an unknown subset of votes. */

        for (i = 0u; i < cnt; ++i) {
            const power_pstate_program_t* p = programs[i];
            sets[p->set].state_known = 0u;
            sets[p->set].quarantined = 1u;
        }
    }
    return result;
invalid:
    result = POWER_INVALID_PROGRAM;
failed:
    memset(combined, 0, sizeof(*combined));
    return (trace->result = result);
}

/* normal PoFx P-state requests must still prove active f0. */

power_result_t power_execute_p_state_batch(const power_pstate_program_t* const* programs,
                                           uint32_t cnt, const char* dev_path, uint32_t comp,
                                           uint32_t set_cnt, const power_dev_t* dev,
                                           const power_dev_t* fstate, power_dev_t* sets,
                                           const power_backend_t* backend, power_execution_t* trace,
                                           power_sequence_t* combined, power_scratch_t* scratch)
{
    return power_execute_p_state_batch_from_f_state(programs, cnt, dev_path, comp, set_cnt, dev,
                                                    fstate, 0u, sets, backend, trace, combined,
                                                    scratch);
}
