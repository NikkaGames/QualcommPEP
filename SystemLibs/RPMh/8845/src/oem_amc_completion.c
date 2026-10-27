#include "oem_amc_completion.h"
#include <string.h>

/* this primitive may only run inside the verified completion lock, with an externally
 * validated exclusive DRV/TCS and WDF interrupt owner. Neither firmware assignment nor PnP
 * IRQ37 is a live owner grant. */

static int admitted(const oem_amc_completion_t* c)
{
    return c && !c->faulted && c->completion_handler_bound && c->external_grant_mask &&
           !(c->external_grant_mask & (uint8_t)~0x07u) &&
           !(c->active_inflight_mask & (uint8_t)~c->external_grant_mask);
}

int oem_amc_init(oem_amc_completion_t* c, const oem_amc_completion_ops_t* ops)
{
    if (!c || !ops || !ops->read32 || !ops->write32 || !ops->barrier || !ops->resolve_inflight ||
        !ops->complete_inflight)
        return 0;
    memset(c, 0, sizeof(*c));
    c->ops = *ops;
    return 1;
}

int oem_amc_has_finished(void* opaque)
{
    oem_amc_completion_t* c = (oem_amc_completion_t*)opaque;
    uint32_t status = 0;
    if (!admitted(c))
        return -1;
    if (c->ack_state == AMC_ACK_OUTCOME_UNKNOWN)
        return -1;
    if (c->ack_state == AMC_ACK_CONFIRMED)
        return 1;
    if (!c->ops.read32(c->ops.ctx, TCS_IRQ_STATUS, &status)) {
        c->faulted = 1;
        return -1;
    }
    status &= 0x07u; /* hlos AMC 0..2; never touch sleep/wake slots. */
    if ((status & (uint32_t)~c->active_inflight_mask) != 0u ||
        (status & (uint32_t)~c->external_grant_mask) != 0u) {
        c->faulted = 1; /* unknown completion owner: do not acknowledge. */
        return -1;
    }
    return status != 0u ? 1 : 0;
}

int oem_amc_finish_one(void* opaque)
{
    oem_amc_completion_t* c = (oem_amc_completion_t*)opaque;
    uint32_t status = 0, after = 0;
    uint8_t slot = 0, bit = 0;
    uint64_t token = 0;
    if (!admitted(c))
        return 0;
    if (c->ack_state == AMC_ACK_OUTCOME_UNKNOWN)
        return 0;
    if (c->ack_state == AMC_ACK_CONFIRMED) {
        bit = (uint8_t)(1u << c->latched_slot);
        /* even an attempted recovery must NEVER write W1C for this slot again without a fresh
         * verified transaction. */

        if (!c->latched_token || !(c->active_inflight_mask & bit) ||
            !(c->external_grant_mask & bit)) {
            c->faulted = 1;
            return 0;
        }
    } else {
        if (!c->ops.read32(c->ops.ctx, TCS_IRQ_STATUS, &status)) {
            c->faulted = 1;
            return 0;
        }
        status &= 0x07u;
        if (!status || (status & (uint32_t)~c->active_inflight_mask) ||
            (status & (uint32_t)~c->external_grant_mask)) {
            c->faulted = 1;
            return 0;
        }
        while (!(status & (1u << slot)))
            ++slot;
        bit = (uint8_t)(1u << slot);
        if (!c->ops.resolve_inflight(c->ops.ctx, slot, &token) || !token) {
            c->faulted = 1;
            return 0;
        }
        c->latched_slot = slot;
        c->latched_token = token;
        /* set UNKNOWN BEFORE write: a failed write may already have taken effect and the same W1C
         * must not be retried blindly. */

        c->ack_state = AMC_ACK_OUTCOME_UNKNOWN;
        if (!c->ops.write32(c->ops.ctx, TCS_IRQ_CLEAR, bit)) {
            c->faulted = 1;
            return 0;
        }
        c->ops.barrier(c->ops.ctx);
        if (!c->ops.read32(c->ops.ctx, TCS_IRQ_STATUS, &after) || (after & bit)) {
            c->faulted = 1;
            return 0;
        }
        c->ack_state = AMC_ACK_CONFIRMED;
    }
    if (!c->ops.complete_inflight(c->ops.ctx, c->latched_slot, c->latched_token)) {
        c->faulted = 1; /* ack confirmed, but client completion unproven. */
        return 0;
    }

    c->active_inflight_mask &= (uint8_t)~bit;
    c->latched_token = 0;
    c->latched_slot = 0;
    c->ack_state = AMC_ACK_IDLE;
    ++c->finished;
    return 1;
}
