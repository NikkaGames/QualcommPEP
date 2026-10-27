#ifndef RPMH_OEM_PNP_PLAN_H
#define RPMH_OEM_PNP_PLAN_H
/* a PURE, READ-ONLY PnP res correlation plan for the Qualcomm-style per-RPMh-driver
 * interrupt table. It NEVER grants DRV/TCS/IRQ ownership, NEVER invokes WDF, and must not
 * be used as the authorization predicate for WdfInterruptCreate or controller MMIO. */

#include <stdint.h>
#include "rsc_irq_assignment.h"
#include "rpmh_oem_irq_group.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rpmh_oem_pnp_req {
    uint32_t driver_id;
    uint32_t gic_intid;
    uint64_t owner_epoch;
} rpmh_oem_pnp_req_t;

typedef struct rpmh_oem_pnp_match {
    uint32_t driver_id;
    uint32_t gic_intid;
    uint64_t owner_epoch;
    uint32_t res_idx;
    uint32_t win_vector; /* not the firmware GIC INTID. */
    uint32_t win_level;
    uint16_t raw_flags;
    uint16_t translated_flags;
    uint8_t raw_share_disposition;
    uint8_t translated_share_disposition;
} rpmh_oem_pnp_match_t;

typedef struct rpmh_oem_pnp_plan {
    uint32_t cnt;
    rpmh_oem_pnp_match_t matches[RPMH_OEM_MAX_DRIVERS];
} rpmh_oem_pnp_plan_t;

/* this immutable OEM-table/PnP diagnostic correlation is NEVER a WDF interrupt handle or
 * controller grant. generation is ONLY the current PrepareHardware res-snapshot
 * generation, not a hardware owner epoch. */

#define RPMH_OEM_8480_DRIVER_ID 2u
#define RPMH_OEM_8480_INTID 37u

typedef struct rpmh_oem_pnp_observation {
    uint64_t generation;
    rpmh_oem_pnp_plan_t plan;
    uint8_t valid;
} rpmh_oem_pnp_observation_t;

void rpmh_oem_pnp_observation_init(rpmh_oem_pnp_observation_t* o);

void rpmh_oem_pnp_observation_revoke(rpmh_oem_pnp_observation_t* o);
/* cross-check the plan against an independent PnP res snapshot from the same generation. */

int rpmh_oem_pnp_observation_capture(rpmh_oem_pnp_observation_t* o, const rpmh_oem_pnp_plan_t* plan,
                                     const rsc_irq_report_t* irq, uint64_t generation);
/* copies only within the currently prepared res-generation lifetime. Callers must NOT
 * interpret a successful read as WDF/RSC/TCS ownership. */

int rpmh_oem_pnp_observation_read(const rpmh_oem_pnp_observation_t* o, uint64_t generation,
                                  rpmh_oem_pnp_plan_t* out);

/* a GICD line can only be masked by an independently established native controller owner
 * after a DEVICE-EXCLUSIVE Windows IRQ37 PnP assignment. The observation builder accepts
 * shared resources for diagnostic correlation, but they MUST NOT qualify for physical
 * owner publication. */

int rpmh_oem_exclusive_irq37_plan(const rpmh_oem_pnp_plan_t* plan, uint64_t pnp_generation);

/* firmware raw INTID correlation is necessary but NEVER sufficient to connect an ISR or
 * grant RPMh execution. */

int rpmh_oem_build_pnp_plan(const rsc_irq_pair_t* pairs, uint32_t pair_cnt,
                            const rpmh_oem_pnp_req_t* requests, uint32_t req_cnt,
                            rpmh_oem_pnp_plan_t* out);
#ifdef __cplusplus
}
#endif
#endif
