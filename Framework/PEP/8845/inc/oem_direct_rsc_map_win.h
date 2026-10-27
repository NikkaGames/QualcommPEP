#ifndef OEM_DIRECT_RSC_MAP_WIN_H
#define OEM_DIRECT_RSC_MAP_WIN_H
/* requires actual trusted d0 lease provider. KeGetCurrentIrql must be PASSIVE_LEVEL for
 * map/unmap and host-lock usage. */

#include "oem_direct_rsc_map.h"
#include "oem_pnp_d0_lease.h"

typedef struct oem_rsc_win_map {
    oem_direct_rsc_map_t state;
    oem_pnp_d0_lease_t* lease;
    void* interrupt;
    uint64_t epoch;
    void* drain_ctx;
    int (*drain)(void*, uint64_t); /* trusted 209 rundown proof. */
} oem_rsc_win_map_t;

/* setup is not an authorization and cannot activate any hardware. */
int oem_rsc_win_map(oem_rsc_win_map_t*, oem_pnp_d0_lease_t*, void* interrupt, uint64_t epoch,
                    void* drain_ctx, int (*drain)(void*, uint64_t));

int oem_rsc_win_release(oem_rsc_win_map_t*);
#endif
