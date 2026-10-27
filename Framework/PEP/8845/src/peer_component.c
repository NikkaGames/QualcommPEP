#include "peer_component.h"
#include "aeob_idx.h"
#include <string.h>

/* the compiled sequences are retained in nonpaged memory after the runtime provider
 * submission. */

static uint64_t sequence_fingerprint(const power_sequence_t* a, const power_sequence_t* b)
{
    const unsigned char* p;
    size_t i;
    uint64_t h = UINT64_C(14695981039346656037);
    p = (const unsigned char*)a;
    for (i = 0; i < sizeof(*a); ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    p = (const unsigned char*)b;
    for (i = 0; i < sizeof(*b); ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

/* select COMPONENT scopes from descriptors, not device names or CLOCK/BUSARB vote values. */

int peer_comp_compile(peer_comp_t* scope, const void* data, size_t bytes, const char* path,
                      runtime_desc_info_t* scratch)
{
    program_idx_t idx;
    uint32_t i, seen = 0u;
    if (!scope || !scratch)
        return 0;
    memset(scope, 0, sizeof(*scope));
    if (!program_idx_describe(data, bytes, path, scratch, &idx) || idx.has_d0 || idx.has_d3 ||
        idx.comp_mask != 1u || idx.f_state_mask[0] != 3u || idx.p_state_set_mask[0] != 0u)
        goto bad;
    for (i = 0u; i < scratch->dev_cnt; ++i) {
        if (strcmp(scratch->devices[i].path, idx.path))
            continue;
        ++seen;
        if (scratch->devices[i].crashdump_state != UINT32_MAX)
            goto bad;
    }
    if (seen != 1u ||
        program_idx_compile(&idx, data, bytes, PROGRAM_F_STATE, 0u, 0u, 0u, &scope->f0, 0) !=
            POWER_OK ||
        program_idx_compile(&idx, data, bytes, PROGRAM_F_STATE, 0u, 0u, 1u, &scope->f1, 0) !=
            POWER_OK ||
        /* a zero-action f0/f1 requires separate provider verification that this runtime-only lane
         * cannot establish. */

        !scope->f0.action_cnt || !scope->f1.action_cnt)
        goto bad;
    memcpy(scope->path, idx.path, sizeof(scope->path));
    scope->compiled_sequence_fingerprint = sequence_fingerprint(&scope->f0, &scope->f1);
    scope->valid = 1u;
    return 1;
bad:
    memset(scope, 0, sizeof(*scope));
    return 0;
}

power_result_t peer_comp_transition(peer_comp_t* scope, uint32_t fs, const power_backend_t* backend,
                                    power_execution_t* trace, power_scratch_t* scratch)
{
    power_result_t r;
    const power_sequence_t* sequence;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!scope || !scope->valid || fs > 1u || !backend || !scratch || scope->quarantined)
        return (trace->result = POWER_UNSUPPORTED);
    sequence = fs == 0u ? &scope->f0 : &scope->f1;
    if (scope->compiled_sequence_fingerprint != sequence_fingerprint(&scope->f0, &scope->f1) ||
        strcmp(sequence->dev_path, scope->path) || strcmp(scope->f0.dev_path, scope->path) ||
        strcmp(scope->f1.dev_path, scope->path) || sequence->target_d_state != fs ||
        scope->f0.target_d_state != 0u || scope->f1.target_d_state != 1u || !sequence->action_cnt ||
        sequence->action_cnt > POWER_MAX_ACTIONS)
        return (trace->result = POWER_INVALID_PROGRAM);
    r = power_execute_f_state(sequence, &scope->state, backend, trace, scratch);
    if (r == POWER_ROLLBACK_FAILED || scope->state.quarantined)
        scope->quarantined = 1u;
    return r;
}
