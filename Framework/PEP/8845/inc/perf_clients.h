#ifndef POFX_REQUIRED_CLIENTS_H
#define POFX_REQUIRED_CLIENTS_H

#include "aeob_idx.h"
#include "ufs_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POFX_REQUIRED_MAX_ACTIONS 128u

typedef struct pofx_required_action {
    power_action_t action;
    uint32_t comp;
    uint32_t set;
    uint32_t state;
    uint8_t program_class;
} pofx_required_action_t;

typedef struct pofx_required_catalog {
    pofx_required_action_t actions[POFX_REQUIRED_MAX_ACTIONS];
    uint32_t cnt;
    uint32_t comp_mask;
    uint32_t set_masks[32];
    uint32_t f_state_masks[32];
    uint32_t p_state_masks[32][32];
    uint8_t bound;
} pofx_required_catalog_t;

/* no hardware owner or NPA client is conferred by this manifest. */
int po_fx_required_compile(pofx_required_catalog_t* catalog, const program_idx_t* idx,
                           const void* pinned_desc, size_t desc_bytes,
                           power_sequence_t* sequence_work, power_pstate_program_t* p_state_work);

/* only a provider with complete client-specific vote and readback semantics may claim
 * readiness. No generic Windows last-vote cache is sufficient. */
typedef struct pofx_required_provider {
    void* ctx;
    int (*owns)(void*, const pofx_required_action_t*);
    void* (*create)(void*, const pofx_required_action_t*);
    int (*validate)(void*, const pofx_required_action_t*, void*);
    /* provider identity and per-client reads must be independently backed by the native owner,
     * not by aggregate vote cache or local handle tables. */
    int (*read_owner_epoch)(void*, uint64_t*);
    int (*read_client)(void*, const pofx_required_action_t*, void*, uint32_t* outstanding);
    int (*is_released)(void*, const pofx_required_action_t*, void*);
    int (*destroy)(void*, const pofx_required_action_t*, void*);
} pofx_required_provider_t;

typedef struct pofx_required_owner {
    const pofx_required_catalog_t* catalog;
    pofx_required_provider_t provider;
    void* handles[POFX_REQUIRED_MAX_ACTIONS];
    uint64_t epoch;
    uint32_t live;
    uint8_t quarantined;
    uint8_t bound;
} pofx_required_owner_t;

/* failed post-create validation retains every unresolved client. */
int po_fx_required_open(pofx_required_owner_t* owner, const pofx_required_catalog_t* catalog,
                        const pofx_required_provider_t* provider, uint64_t epoch);
int po_fx_required_ready(const pofx_required_owner_t* owner, uint64_t epoch);
/* close only after PoFx rundown and independent per-client release proof. */
int po_fx_required_close(pofx_required_owner_t* owner, uint64_t epoch);

#ifdef __cplusplus
}
#endif
#endif
