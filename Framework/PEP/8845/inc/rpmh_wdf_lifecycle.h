#ifndef RPMH_WDF_LIFECYCLE_H
#define RPMH_WDF_LIFECYCLE_H
/* pep0 KMDF PnP / power lifecycle observation for a POSSIBLE DRV2 interrupt. This
 * structure has no access token, hardware pointer, WDFINTERRUPT handle, or API capable of
 * activating an interrupt. */

#include <stdint.h>
#include "pnp_resources.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum rpmh_wdf_phase {
    RPMH_WDF_ABSENT = 0,
    RPMH_WDF_PREPARED = 1,
    RPMH_WDF_D0_OBSERVED = 2
} rpmh_wdf_phase_t;

typedef struct rpmh_wdf_lifecycle {
    uint64_t pnp_generation;
    uint64_t power_generation;
    uint32_t win_vector;
    uint32_t win_level;
    uint32_t driver_id;
    uint32_t gic_intid;
    rpmh_wdf_phase_t phase;
    uint8_t candidate_observed;

} rpmh_wdf_lifecycle_t;

void rpmh_wdf_lifecycle_init(rpmh_wdf_lifecycle_t* life);
/* the generation MUST be the diagnostic PnP generation, never an RPMh grant. */
int rpmh_wdf_lifecycle_prepare(rpmh_wdf_lifecycle_t* life, uint64_t pnp_generation);

int rpmh_wdf_lifecycle_observe(rpmh_wdf_lifecycle_t* life,
                               const rpmh_oem_pnp_observation_t* observation);
int rpmh_wdf_lifecycle_enter_d0(rpmh_wdf_lifecycle_t* life);
void rpmh_wdf_lifecycle_leave_d0(rpmh_wdf_lifecycle_t* life);
void rpmh_wdf_lifecycle_release(rpmh_wdf_lifecycle_t* life);

int rpmh_wdf_lifecycle_read(const rpmh_wdf_lifecycle_t* life, uint64_t generation, uint32_t* vector,
                            uint32_t* level);
#ifdef __cplusplus
}
#endif
#endif
