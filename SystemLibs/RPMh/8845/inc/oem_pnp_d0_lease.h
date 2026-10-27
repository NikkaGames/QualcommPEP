#ifndef OEM_PNP_D0_LEASE_H
#define OEM_PNP_D0_LEASE_H
/* verified per-PnP-generation / d0-execution LEASE GATE. Not the missing Molokai DRV2
 * owner or a grant discoverer. */

#include <stdint.h>
#include "rpmh_oem_pnp_plan.h"
#ifdef __cplusplus
extern "C" {
#endif
#define D0_RIGHT_DRV2 1u
#define D0_RIGHT_TCS 2u
#define D0_RIGHT_IRQ 4u
#define D0_RIGHT_ALL 7u
#define D0_LEASE_COLD 0u
#define D0_LEASE_PREPARED 1u
#define D0_LEASE_ACTIVE 2u
#define D0_LEASE_CLOSED 3u
#define D0_LEASE_FAULT 4u

typedef struct oem_d0_proof {
    uint64_t pnp_generation;
    uint64_t owner_epoch; /* independent HW owner epoch; NOT PnP generation. */
    uint64_t nonce;       /* owner-only, changes across rights handoffs. */
    void* wdf_interrupt;
    uint32_t driver_id, gic_intid, win_vector, win_level;
    uint32_t res_idx;
    uint32_t rights;
    uint32_t exclusive; /* independently attested hardware exclusivity. */
    uint32_t d0_active;
} oem_d0_proof_t;

typedef struct oem_d0_probe {
    void* ctx;
    /* dirql-safe if used as exclusive_owner hook; nonpageable, nonblocking, fixed-size, and
     * MUST consult authoritative OWNER state. Returning a cached PnP observation as HW proof
     * is invalid. */

    int (*read)(void*, oem_d0_proof_t*);
} oem_d0_probe_t;

typedef struct oem_pnp_d0_lease {
    uint32_t phase;
    uint64_t pnp_generation;
    rpmh_oem_pnp_match_t match;
    oem_d0_probe_t provider;
    oem_d0_proof_t proof;
} oem_pnp_d0_lease_t;

/* preparehardware only: a PnP correlation observation, NOT exclusive rights. The
 * provider/IRQ handle must not be accepted until d0 is independently attested. */

int oem_d0_lease_prepare(oem_pnp_d0_lease_t*, const rpmh_oem_pnp_observation_t*,
                         uint64_t generation);
int oem_d0_lease_enter(oem_pnp_d0_lease_t*, const oem_d0_probe_t*, void* wdf_interrupt,
                       uint64_t owner_epoch);
/* canuse rereads OWNER-PROVIDED attestation twice; stale/revoked rights quarantine. Caller
 * holds serialization covering probe, WDF callback and PnP teardown. */

int oem_d0_lease_can_use(oem_pnp_d0_lease_t*, void* wdf_interrupt, uint64_t owner_epoch,
                         uint32_t rights);
/* compatible with exclusive_owner(ctx,epoch,interrupt). The mask owner's
 * read_masked/write_masked callbacks remain independently required. */

int oem_d0_lease_irq_owner(void* ctx, uint64_t epoch, void* interrupt);
/* exitd0 revokes admission before WDF interrupt synchronization; callers must still
 * mask/flush hardware and all ISR/DPC/work activity themselves. */

void oem_d0_lease_exit(oem_pnp_d0_lease_t*);
void oem_d0_lease_revoke(oem_pnp_d0_lease_t*);
#ifdef __cplusplus
}
#endif
#endif
