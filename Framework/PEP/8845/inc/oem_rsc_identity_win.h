#ifndef OEM_RSC_IDENTITY_WIN_H
#define OEM_RSC_IDENTITY_WIN_H
/* read-only HLOS DRV2 identity discovery grants no RSC write authority. */

#include <stdint.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsc_identity {
    uint32_t rsc_id;
    uint32_t driver_config;
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t amc_status[MOLOKAI_WIN_AMC_CNT];
    uint32_t tcs_cnt;
    uint32_t commands_per_tcs;
    uint32_t identity_matched;
} rsc_identity_t;

/* caller must be in a PnP-backed d0 callback for the matching IRQ37 WDF interrupt.
 * function maps/unmaps the firmware-known DRV2 window once, reads only immutable
 * identity/geometry and non-destructive live status. */

int rsc_read_only_identify_hlos_win(rsc_identity_t* out);
#ifdef __cplusplus
}
#endif
#endif
