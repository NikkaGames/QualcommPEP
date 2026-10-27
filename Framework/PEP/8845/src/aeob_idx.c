#include "aeob_idx.h"
#include <string.h>

/* the Windows DDI must pin the original immutable blob and supply a verified provider
 * epoch. */

static uint64_t fingerprint(const void* data, size_t bytes)
{
    const unsigned char* p = (const unsigned char*)data;
    uint64_t h = UINT64_C(14695981039346656037);
    size_t i;
    for (i = 0u; i < bytes; ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static int path_equal(const char* a, const char* b)
{
    size_t i;
    if (!a || !b || a[0] != '\\' || b[0] != '\\')
        return 0;
    for (i = 0u; i < 256u; ++i) {
        if (a[i] != b[i])
            return 0;
        if (!a[i])
            return i >= 5u;
        if ((unsigned char)a[i] < 0x21u || (unsigned char)a[i] > 0x7eu)
            return 0;
    }
    return 0;
}

int program_idx_describe(const void* data, size_t bytes, const char* path,
                         runtime_desc_info_t* scratch, program_idx_t* out)
{
    uint32_t i;
    const runtime_dev_info_t* match = 0;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!data || !scratch || !path || !path_equal(path, path) || bytes < 12u ||
        bytes > AEOB_MAX_SIZE || !validate_runtime_desc(data, bytes, scratch))
        return 0;
    for (i = 0u; i < scratch->dev_cnt; ++i) {
        const runtime_dev_info_t* d = &scratch->devices[i];
        if (!path_equal(path, d->path))
            continue;
        if (match || !d->supported || d->empty)
            return 0;
        match = d;
    }
    if (!match)
        return 0;
    memcpy(out->path, match->path, sizeof(out->path));
    out->desc_fingerprint = fingerprint(data, bytes);
    out->desc_bytes = bytes;
    out->comp_mask = match->comp_mask;
    memcpy(out->f_state_mask, match->comp_f_states, sizeof(out->f_state_mask));
    memcpy(out->p_state_set_mask, match->comp_pstate_sets, sizeof(out->p_state_set_mask));
    out->has_d0 = match->has_d0;
    out->has_d3 = match->has_d3;
    out->d_state_mask = match->d_state_mask;
    /* only desc-defined P-states may be advertised. set existence says nothing about state
     * membership. */
    for (uint32_t comp = 0u; comp < 32u; ++comp) {
        if (!(out->comp_mask & (UINT32_C(1) << comp)))
            continue;
        for (uint32_t set = 0u; set < 32u; ++set) {
            uint32_t states = 0u;
            if (!(out->p_state_set_mask[comp] & (UINT32_C(1) << set)))
                continue;
            for (uint32_t state = 0u; state < 32u; ++state) {
                uint32_t actions = 0u;
                aeob_status_t st = aeo_b_visit_comp_p_state(data, bytes, path, comp, set, state,
                                                            NULL, NULL, &actions);
                if (st == AEO_B_OK)
                    states |= UINT32_C(1) << state;
                else if (st != AEO_B_NOT_FOUND) {
                    memset(out, 0, sizeof(*out));
                    return 0;
                }
            }
            if (!states) {
                memset(out, 0, sizeof(*out));
                return 0;
            }
            out->p_state_mask[comp][set] = states;
        }
    }
    out->valid = 1u;
    return 1;
}

power_result_t program_idx_compile(const program_idx_t* idx, const void* data, size_t bytes,
                                   program_class_t cls, uint32_t comp, uint32_t set, uint32_t state,
                                   power_plan_t* plan, power_pstate_program_t* pprogram)
{
    if (!idx || !idx->valid || !data || !path_equal(idx->path, idx->path) ||
        bytes != idx->desc_bytes || fingerprint(data, bytes) != idx->desc_fingerprint)
        return POWER_INVALID_PROGRAM;
    if (cls == PROGRAM_D_STATE) {
        if (!plan || pprogram || comp || set || state > 3u ||
            !(idx->d_state_mask & (uint8_t)(1u << state)))
            return POWER_UNSUPPORTED;
        return power_compile_aeo_b(data, bytes, idx->path, state, plan);
    }
    if (comp >= 32u || !(idx->comp_mask & (UINT32_C(1) << comp)))
        return POWER_UNSUPPORTED;
    if (cls == PROGRAM_F_STATE) {
        if (!plan || pprogram || set || state >= 32u ||
            !(idx->f_state_mask[comp] & (UINT32_C(1) << state)))
            return POWER_UNSUPPORTED;
        return power_compile_aeo_bf_state(data, bytes, idx->path, comp, state, plan);
    }
    if (cls == PROGRAM_P_STATE) {
        if (plan || !pprogram || set >= 32u || state >= 32u ||
            !(idx->p_state_set_mask[comp] & (UINT32_C(1) << set)) ||
            !(idx->p_state_mask[comp][set] & (UINT32_C(1) << state)))
            return POWER_UNSUPPORTED;
        return power_compile_aeo_bp_state(data, bytes, idx->path, comp, set, state, pprogram);
    }
    return POWER_UNSUPPORTED;
}

uint32_t program_p_state_cnt(const program_idx_t* idx, uint32_t comp, uint32_t set)
{
    uint32_t mask, cnt = 0u;
    if (!idx || !idx->valid || comp >= 32u || set >= 32u ||
        !(idx->comp_mask & (UINT32_C(1) << comp)) ||
        !(idx->p_state_set_mask[comp] & (UINT32_C(1) << set)))
        return 0u;
    mask = idx->p_state_mask[comp][set];
    while (mask) {
        cnt += mask & 1u;
        mask >>= 1;
    }
    return cnt;
}

int program_p_state_from_ordinal(const program_idx_t* idx, uint32_t comp, uint32_t set,
                                 uint32_t ordinal, uint32_t* state)
{
    uint32_t mask, i, cnt = program_p_state_cnt(idx, comp, set);
    if (!state || ordinal >= cnt)
        return 0;
    mask = idx->p_state_mask[comp][set];
    for (i = 0u; i < 32u; ++i)
        if (mask & (UINT32_C(1) << i)) {
            if (!ordinal) {
                *state = i;
                return 1;
            }
            --ordinal;
        }
    return 0;
}

int program_p_state_to_ordinal(const program_idx_t* idx, uint32_t comp, uint32_t set,
                               uint32_t state, uint32_t* ordinal)
{
    uint32_t i, cnt = 0u, mask;
    if (!ordinal || state >= 32u || !program_p_state_cnt(idx, comp, set))
        return 0;
    mask = idx->p_state_mask[comp][set];
    if (!(mask & (UINT32_C(1) << state)))
        return 0;
    for (i = 0u; i < state; ++i)
        if (mask & (UINT32_C(1) << i))
            ++cnt;
    *ordinal = cnt;
    return 1;
}
