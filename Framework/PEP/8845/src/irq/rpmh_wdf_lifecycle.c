#include "rpmh_wdf_lifecycle.h"
#include <string.h>

void rpmh_wdf_lifecycle_init(rpmh_wdf_lifecycle_t* life)
{
    if (!life)
        return;
    memset(life, 0, sizeof(*life));
}

int rpmh_wdf_lifecycle_prepare(rpmh_wdf_lifecycle_t* life, uint64_t generation)
{
    if (!life || !generation || life->phase != RPMH_WDF_ABSENT)
        return 0;
    /* prepared is a Windows PnP *observation*, not WDF ownership. */
    life->pnp_generation = generation;
    life->candidate_observed = 0;
    life->win_vector = 0;
    life->win_level = 0;
    life->driver_id = 0;
    life->gic_intid = 0;
    life->phase = RPMH_WDF_PREPARED;
    return 1;
}

int rpmh_wdf_lifecycle_observe(rpmh_wdf_lifecycle_t* life,
                               const rpmh_oem_pnp_observation_t* observation)
{
    rpmh_oem_pnp_plan_t plan = {0};
    const rpmh_oem_pnp_match_t* m;
    if (!life || !observation || life->phase != RPMH_WDF_PREPARED || life->candidate_observed ||
        !life->pnp_generation ||
        !rpmh_oem_pnp_observation_read(observation, life->pnp_generation, &plan) || plan.cnt != 1u)
        return 0;
    m = &plan.matches[0];
    if (m->driver_id != RPMH_OEM_8480_DRIVER_ID || m->gic_intid != RPMH_OEM_8480_INTID ||
        m->owner_epoch != life->pnp_generation || !m->win_vector || !m->win_level)
        return 0;
    life->win_vector = m->win_vector;
    life->win_level = m->win_level;
    life->driver_id = m->driver_id;
    life->gic_intid = m->gic_intid;
    life->candidate_observed = 1;
    return 1;
}

int rpmh_wdf_lifecycle_enter_d0(rpmh_wdf_lifecycle_t* life)
{
    if (!life || life->phase != RPMH_WDF_PREPARED)
        return 0;
    ++life->power_generation;
    if (!life->power_generation)
        life->power_generation = 1;
    life->phase = RPMH_WDF_D0_OBSERVED;
    return 1;
}

void rpmh_wdf_lifecycle_leave_d0(rpmh_wdf_lifecycle_t* life)
{
    if (life && life->phase == RPMH_WDF_D0_OBSERVED)
        life->phase = RPMH_WDF_PREPARED;
}

void rpmh_wdf_lifecycle_release(rpmh_wdf_lifecycle_t* life)
{
    if (!life)
        return;
    life->phase = RPMH_WDF_ABSENT;
    life->pnp_generation = 0;
    life->candidate_observed = 0;
    life->win_vector = 0;
    life->win_level = 0;
    life->driver_id = 0;
    life->gic_intid = 0;
    /* retain the power generation across restart; it is distinct from the hardware owner epoch. */
}

int rpmh_wdf_lifecycle_read(const rpmh_wdf_lifecycle_t* life, uint64_t generation, uint32_t* vector,
                            uint32_t* level)
{
    if (vector)
        *vector = 0;
    if (level)
        *level = 0;
    if (!life || !vector || !level || !generation || life->pnp_generation != generation ||
        !life->candidate_observed ||
        (life->phase != RPMH_WDF_PREPARED && life->phase != RPMH_WDF_D0_OBSERVED))
        return 0;
    *vector = life->win_vector;
    *level = life->win_level;
    return 1;
}
