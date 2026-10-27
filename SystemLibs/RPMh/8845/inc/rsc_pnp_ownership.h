#ifndef RSC_PNP_OWNERSHIP_H
#define RSC_PNP_OWNERSHIP_H
/* translated PnP assignment check for HLOS driver2; a NECESSARY condition, not an
 * independent entitlement to firmware TCS slots, IRQ programming, command DB/RPMh voting
 * or interconnect ownership. */

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RSC_PNP_MAX_MEMORY 32u
#define RSC_DRV2_PNP_BASE UINT64_C(0x18920000)
#define RSC_DRV2_PNP_SIZE UINT64_C(0x10000)

typedef struct rsc_pnp_memory {
    uint64_t start;
    uint64_t length;
} rsc_pnp_memory_t;

typedef enum rsc_pnp_result {
    RSC_PNP_MISSING = 0,
    RSC_PNP_ASSIGNED_MEMORY = 1,
    RSC_PNP_INVALID = 2,
    RSC_PNP_AMBIGUOUS = 3,
    RSC_PNP_NO_INTERRUPT = 4
} rsc_pnp_result_t;

/* memory resources must be taken directly from the translated PnP res list assigned to
 * THIS WDFDEVICE, not firmware DTS, a hard-coded address, or another driver's res list.
 * success still does not grant any TCS slots; owned_amc_mask stays zero. */

rsc_pnp_result_t rsc_check_translated_assignment(const rsc_pnp_memory_t* memory, size_t cnt,
                                                 unsigned interrupt_cnt);
#ifdef __cplusplus
}
#endif
#endif
