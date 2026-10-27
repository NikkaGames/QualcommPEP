#ifndef RPMH_PNP_RESOURCES_H
#define RPMH_PNP_RESOURCES_H
/* match the RPMh interrupt table to raw and translated PnP resources.
 * matches do not authorize WDF interrupt creation or controller access. */

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

typedef struct rpmh_pnp_resources {
    uint32_t cnt;
    rpmh_oem_pnp_match_t matches[RPMH_OEM_MAX_DRIVERS];
} rpmh_pnp_resources_t;

/* generation identifies the PrepareHardware snapshot, not a hardware owner epoch. */

#define RPMH_HLOS_DRIVER_ID 2u
#define RPMH_HLOS_INTID 37u

typedef struct rpmh_oem_pnp_observation {
    uint64_t generation;
    rpmh_pnp_resources_t resources;
    uint8_t valid;
} rpmh_oem_pnp_observation_t;

void rpmh_oem_pnp_observation_init(rpmh_oem_pnp_observation_t* o);

void rpmh_oem_pnp_observation_revoke(rpmh_oem_pnp_observation_t* o);
/* cross-check matches against an independent PnP snapshot from the same generation. */

int rpmh_oem_pnp_observation_capture(rpmh_oem_pnp_observation_t* o,
                                     const rpmh_pnp_resources_t* resources,
                                     const rsc_irq_report_t* irq, uint64_t generation);
/* copy only within the current PnP generation; a read does not establish ownership. */

int rpmh_oem_pnp_observation_read(const rpmh_oem_pnp_observation_t* o, uint64_t generation,
                                  rpmh_pnp_resources_t* out);

/* masking GICD requires an independent controller owner and exclusive IRQ37 resources.
 * shared matches remain valid for diagnostics only. */

int rpmh_has_exclusive_irq37_resource(const rpmh_pnp_resources_t* resources,
                                      uint64_t pnp_generation);

/* matching the raw INTID alone does not authorize ISR connection or RPMh execution. */

int rpmh_match_pnp_resources(const rsc_irq_pair_t* pairs, uint32_t pair_cnt,
                             const rpmh_oem_pnp_req_t* requests, uint32_t req_cnt,
                             rpmh_pnp_resources_t* out);
#ifdef __cplusplus
}
#endif
#endif
