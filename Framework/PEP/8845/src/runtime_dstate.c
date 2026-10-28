#include "runtime_dstate.h"
#include <string.h>

int runtime_dstate_compile(runtime_dstate_scope_t* s, const void* data, size_t bytes,
                           const char* path, runtime_desc_info_t* scratch)
{
    if (!s || !scratch)
        return 0;
    memset(s, 0, sizeof(*s));
    if (!program_idx_describe(data, bytes, path, scratch, &s->idx) || !s->idx.has_d0 ||
        (s->idx.comp_mask != 0u && s->idx.comp_mask != 1u) || s->idx.p_state_set_mask[0] != 0u ||
        (s->idx.f_state_mask[0] != 0u && s->idx.f_state_mask[0] != 1u))
        goto bad;
    {
        uint32_t i, found = 0;
        for (i = 0u; i < scratch->dev_cnt; ++i) {
            if (strcmp(scratch->devices[i].path, s->idx.path) != 0)
                continue;
            ++found;
            /* crashdump is its own res state, never elide it. */
            if (scratch->devices[i].crashdump_state != UINT32_MAX)
                goto bad;
        }
        if (found != 1u || (!s->idx.comp_mask && s->idx.f_state_mask[0]))
            goto bad;
    }
    if (s->idx.f_state_mask[0]) {
        /* reuse owned sequence storage: do not put a >10 KiB sequence on a Windows kernel stack
         * just to check the empty f0 contract. */

        if (program_idx_compile(&s->idx, data, bytes, PROGRAM_F_STATE, 0u, 0u, 0u, &s->d0, 0) !=
                POWER_OK ||
            s->d0.action_cnt)
            goto bad;
        s->empty_f0 = 1u;
    }
    /* d1/d2 are optional and must be compiled only when *present*. A missing d1/d2 is not an
     * alias for d3 and can never be accepted. */

    {
        uint32_t state;
        for (state = 0u; state <= 3u; ++state) {
            power_sequence_t* sequence = (state == 0u)   ? &s->d0
                                         : (state == 1u) ? &s->d1
                                         : (state == 2u) ? &s->d2
                                                         : &s->d3;
            if (!(s->idx.d_state_mask & (uint8_t)(1u << state)))
                continue;
            if (program_idx_compile(&s->idx, data, bytes, PROGRAM_D_STATE, 0u, 0u, state, sequence,
                                    0) != POWER_OK)
                goto bad;
        }
    }
    /* a power-on-only descriptor retains D0 resources. Missing D3 does not authorize an inferred
     * release sequence. */
    s->last_nonempty_target = UINT32_MAX;
    s->valid = 1u;
    return 1;
bad:
    memset(s, 0, sizeof(*s));
    return 0;
}

power_result_t runtime_dstate_execute(runtime_dstate_scope_t* s, uint32_t target,
                                      const power_backend_t* owner, power_execution_t* trace,
                                      power_scratch_t* scratch)
{
    power_result_t r;
    const power_sequence_t* sequence;
    if (!s || !trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!s->valid || s->quarantined || target > 3u ||
        !(s->idx.d_state_mask & (uint8_t)(1u << target)) || !owner || !scratch)
        return (trace->result = POWER_UNSUPPORTED);
    sequence = (target == 0u) ? &s->d0 : (target == 1u) ? &s->d1 : (target == 2u) ? &s->d2 : &s->d3;
    if (strcmp(s->idx.path, sequence->dev_path) || sequence->target_d_state != target)
        return (trace->result = POWER_INVALID_PROGRAM);
    r = power_execute(sequence, &s->physical, owner, trace, scratch);
    if (r == POWER_OK && sequence->action_cnt)
        s->last_nonempty_target = target;
    if (r == POWER_ROLLBACK_FAILED || s->physical.quarantined)
        s->quarantined = 1u;
    return r;
}
