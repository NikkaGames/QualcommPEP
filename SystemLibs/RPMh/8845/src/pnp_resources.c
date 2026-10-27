#include "pnp_resources.h"
#include <string.h>

int rpmh_match_pnp_resources(const rsc_irq_pair_t* pairs, uint32_t pair_cnt,
                             const rpmh_oem_pnp_req_t* requests, uint32_t req_cnt,
                             rpmh_pnp_resources_t* out)
{
    rpmh_pnp_resources_t candidate = {0};
    uint32_t i, j;
    if (!out || !pairs || !requests || !pair_cnt || !req_cnt || pair_cnt > RSC_IRQ_RES_LIMIT ||
        req_cnt > RPMH_OEM_MAX_DRIVERS)
        return 0;

    for (i = 0; i < req_cnt; ++i) {
        const rpmh_oem_pnp_req_t* r = &requests[i];
        uint32_t match_cnt = 0;
        uint32_t selected = 0;
        if (!r->gic_intid || !r->owner_epoch)
            return 0;
        for (j = 0; j < i; ++j) {
            if (requests[j].driver_id == r->driver_id || requests[j].gic_intid == r->gic_intid)
                return 0;
        }
        for (j = 0; j < pair_cnt; ++j) {
            const rsc_irq_pair_t* p = &pairs[j];
            uint32_t k;
            /* a raw INTID alone is not a usable Windows res: its translated desc must be the same
             * (non-MSI) interrupt with a valid vector, IRQL, trigger and sharing policy. Never
             * accept edge-triggered IRQ37. */

            if (!p->raw_is_interrupt || p->raw_vector != r->gic_intid)
                continue;
            if (!p->translated_is_interrupt || p->raw_is_message || p->translated_is_message ||
                !p->translated_vector || !p->translated_level || (p->raw_flags & 0x0001u) ||
                (p->translated_flags & 0x0001u) ||
                p->raw_share_disposition != p->translated_share_disposition)
                return 0;
            /* otherwise two device interrupts could be attributed to one Windows vector in a
             * supposedly unique sequence. This is diagnostic safety, never permission to bind an
             * ISR.
             */

            for (k = 0; k < pair_cnt; ++k) {
                const rsc_irq_pair_t* other = &pairs[k];
                if (k == j)
                    continue;
                if (other->translated_is_interrupt && !other->translated_is_message &&
                    other->translated_vector == p->translated_vector)
                    return 0;
            }
            if (++match_cnt != 1u)
                return 0;
            selected = j;
        }
        if (match_cnt != 1u)
            return 0;
        {
            const rsc_irq_pair_t* p = &pairs[selected];
            rpmh_oem_pnp_match_t* m = &candidate.matches[i];
            m->driver_id = r->driver_id;
            m->gic_intid = r->gic_intid;
            m->owner_epoch = r->owner_epoch;
            m->res_idx = selected;
            m->win_vector = p->translated_vector;
            m->win_level = p->translated_level;
            m->raw_flags = p->raw_flags;
            m->translated_flags = p->translated_flags;
            m->raw_share_disposition = p->raw_share_disposition;
            m->translated_share_disposition = p->translated_share_disposition;
        }
    }
    candidate.cnt = req_cnt;
    memcpy(out, &candidate, sizeof(candidate));
    return 1;
}

/* per-PnP-generation identification observation. The actual owner credential is
 * intentionally absent from this entire interface. */

void rpmh_oem_pnp_observation_init(rpmh_oem_pnp_observation_t* o)
{
    if (!o)
        return;
    memset(o, 0, sizeof(*o));
    o->generation = 1;
}

void rpmh_oem_pnp_observation_revoke(rpmh_oem_pnp_observation_t* o)
{
    if (!o)
        return;
    o->valid = 0;
    memset(&o->resources, 0, sizeof(o->resources));
    ++o->generation;
    if (!o->generation)
        o->generation = 1;
}

int rpmh_oem_pnp_observation_capture(rpmh_oem_pnp_observation_t* o,
                                     const rpmh_pnp_resources_t* resources,
                                     const rsc_irq_report_t* irq, uint64_t generation)
{
    const rpmh_oem_pnp_match_t* m;
    if (!o || !resources || !irq || !generation || o->generation != generation ||
        resources->cnt != 1u || irq->finding != RSC_IRQ_UNIQUE_RAW_CANDIDATE ||
        irq->matching_raw_pairs != 1u || irq->raw_vector != RPMH_OEM_8480_INTID ||
        !irq->win_vector || !irq->win_level)
        return 0;
    m = &resources->matches[0];
    if (m->driver_id != RPMH_OEM_8480_DRIVER_ID || m->gic_intid != RPMH_OEM_8480_INTID ||
        m->owner_epoch != generation || /* pnp generation, NOT hardware ownership. */
        m->res_idx != irq->candidate_idx || m->win_vector != irq->win_vector ||
        m->win_level != irq->win_level || m->raw_flags != irq->raw_flags ||
        m->translated_flags != irq->translated_flags ||
        m->raw_share_disposition != irq->raw_share_disposition ||
        m->translated_share_disposition != irq->translated_share_disposition)
        return 0;
    o->resources = *resources;
    o->valid = 1;
    return 1;
}

int rpmh_oem_pnp_observation_read(const rpmh_oem_pnp_observation_t* o, uint64_t generation,
                                  rpmh_pnp_resources_t* out)
{
    if (out)
        memset(out, 0, sizeof(*out));
    if (!o || !out || !o->valid || !generation || o->generation != generation)
        return 0;
    *out = o->resources;
    return 1;
}

/* runtime GICD write ownership requires the device-exclusive share disposition at BOTH
 * desc ends, from the same pinned PnP generation. The distinct DRV2/TCS proof is still
 * required. */

int rpmh_has_exclusive_irq37_resource(const rpmh_pnp_resources_t* resources,
                                      uint64_t pnp_generation)
{
    const rpmh_oem_pnp_match_t* m;
    if (!resources || !pnp_generation || resources->cnt != 1u)
        return 0;
    m = &resources->matches[0];
    return m->driver_id == RPMH_OEM_8480_DRIVER_ID && m->gic_intid == RPMH_OEM_8480_INTID &&
           m->owner_epoch == pnp_generation && m->win_vector != 0u && m->win_level != 0u &&
           !(m->raw_flags & 1u) && !(m->translated_flags & 1u) && m->raw_share_disposition == 1u &&
           m->translated_share_disposition == 1u;
}
