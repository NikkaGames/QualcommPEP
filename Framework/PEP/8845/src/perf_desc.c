#include "perf_desc.h"
#include <string.h>

static uint32_t bits(uint32_t mask)
{
    uint32_t cnt = 0u;
    while (mask) {
        cnt += mask & 1u;
        mask >>= 1u;
    }
    return cnt;
}

int po_fx_catalog_bind(pofx_perf_catalog_t* catalog, const program_idx_t* idx, uint32_t components)
{
    uint32_t c, s;
    if (!catalog || !idx || !idx->valid || !idx->desc_bytes || !idx->desc_fingerprint ||
        !components || components > 32u || catalog->bound)
        return 0;
    for (c = 0; c < 32u; ++c) {
        if (c >= components && (idx->comp_mask & (UINT32_C(1) << c)))
            return 0;
        if (c < components && !(idx->comp_mask & (UINT32_C(1) << c)))
            return 0;
        if (c >= components && (idx->p_state_set_mask[c] || idx->f_state_mask[c]))
            return 0;
    }
    memset(catalog, 0, sizeof(*catalog));
    catalog->idx = *idx;
    catalog->components = components;
    for (c = 0; c < components; ++c) {
        uint32_t mask = idx->p_state_set_mask[c];
        for (s = 0; s < 32u; ++s) {
            uint32_t states = idx->p_state_mask[c][s];
            if ((states && !(mask & (UINT32_C(1) << s))) ||
                (!states && (mask & (UINT32_C(1) << s))))
                goto fail;
            if (!(mask & (UINT32_C(1) << s)))
                continue;
            {
                uint32_t o = catalog->set_cnt[c]++;
                catalog->set_selector[c][o] = (uint8_t)s;
                catalog->state_cnt[c][o] = (uint8_t)bits(states);
            }
        }
    }
    catalog->bound = 1u;
    return 1;
fail:
    memset(catalog, 0, sizeof(*catalog));
    return 0;
}

int po_fx_catalog_resolve_set(const pofx_perf_catalog_t* catalog, uint32_t comp, uint32_t ordinal,
                              uint32_t* selector, uint32_t* state_cnt)
{
    if (!catalog || !catalog->bound || comp >= catalog->components ||
        ordinal >= catalog->set_cnt[comp] || !selector || !state_cnt)
        return 0;
    *selector = catalog->set_selector[comp][ordinal];
    *state_cnt = catalog->state_cnt[comp][ordinal];
    return *state_cnt != 0u;
}

int po_fx_catalog_resolve_state(const pofx_perf_catalog_t* catalog, uint32_t comp,
                                uint32_t set_ordinal, uint32_t state_ordinal,
                                uint32_t* set_selector, uint32_t* state_selector)
{
    uint32_t selector, cnt;
    if (!state_selector ||
        !po_fx_catalog_resolve_set(catalog, comp, set_ordinal, &selector, &cnt) ||
        state_ordinal >= cnt ||
        !program_p_state_from_ordinal(&catalog->idx, comp, selector, state_ordinal, state_selector))
        return 0;
    if (set_selector)
        *set_selector = selector;
    return 1;
}

int po_fx_catalog_validate(const pofx_perf_catalog_t* catalog, const program_idx_t* idx)
{
    uint32_t c, s;
    if (!catalog || !catalog->bound || !idx || !idx->valid ||
        catalog->idx.desc_fingerprint != idx->desc_fingerprint ||
        catalog->idx.desc_bytes != idx->desc_bytes ||
        memcmp(catalog->idx.path, idx->path, sizeof(idx->path)) ||
        catalog->idx.comp_mask != idx->comp_mask || catalog->idx.d_state_mask != idx->d_state_mask)
        return 0;
    for (c = 0; c < 32u; ++c) {
        if (catalog->idx.f_state_mask[c] != idx->f_state_mask[c] ||
            catalog->idx.p_state_set_mask[c] != idx->p_state_set_mask[c])
            return 0;
        for (s = 0; s < 32u; ++s)
            if (catalog->idx.p_state_mask[c][s] != idx->p_state_mask[c][s])
                return 0;
    }
    return 1;
}
