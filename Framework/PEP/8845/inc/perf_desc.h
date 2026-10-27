#ifndef POFX_PERF_CATALOG_H
#define POFX_PERF_CATALOG_H
#include "aeob_idx.h"
#ifdef __cplusplus
extern "C" {
#endif

#define PERF_CATALOG_MAX_COMPONENTS 32u
#define PERF_CATALOG_MAX_SETS 32u

typedef struct pofx_perf_catalog {
    program_idx_t idx;
    uint32_t components;
    uint32_t set_cnt[32];
    uint8_t set_selector[32][32];
    uint8_t state_cnt[32][32];
    uint8_t bound;
} pofx_perf_catalog_t;

int po_fx_catalog_bind(pofx_perf_catalog_t* catalog, const program_idx_t* idx, uint32_t comp_cnt);
int po_fx_catalog_resolve_set(const pofx_perf_catalog_t* catalog, uint32_t comp,
                              uint32_t set_ordinal, uint32_t* set_selector, uint32_t* state_cnt);
int po_fx_catalog_resolve_state(const pofx_perf_catalog_t* catalog, uint32_t comp,
                                uint32_t set_ordinal, uint32_t state_ordinal,
                                uint32_t* set_selector, uint32_t* state_selector);
int po_fx_catalog_validate(const pofx_perf_catalog_t* catalog, const program_idx_t* idx);

#ifdef __cplusplus
}
#endif
#endif
