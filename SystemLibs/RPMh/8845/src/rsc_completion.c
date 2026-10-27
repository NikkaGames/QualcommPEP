#include "rsc_completion.h"
#include <string.h>

void rsc_completion_init(rsc_completion_t* p)
{
    if (p)
        memset(p, 0, sizeof(*p));
}

int rsc_completion_bind(rsc_completion_t* p, uint32_t epoch, uint8_t independently_granted_amcs,
                        uint8_t irq_bound)
{
    unsigned i;
    if (!p || !epoch || !independently_granted_amcs ||
        (independently_granted_amcs & (uint8_t)~RSC_AMC_MASK) || irq_bound != 1u ||
        p->granted_mask || p->irq_bound)
        return 0;
    /* never overwrite completions from the old owner; they must be drained and acknowledged
     * before a new epoch may be bound. */

    for (i = 0; i < RSC_AMCS; ++i)
        if (p->amc[i].state != RSC_AMC_VACANT)
            return 0;
    if (epoch <= p->epoch)
        return 0;
    p->epoch = epoch;
    p->granted_mask = independently_granted_amcs;
    p->irq_bound = 1u;
    return 1;
}

static int submit_bookkeeping(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                              uint16_t expected_command_mask, uint16_t expected_dependency_mask,
                              uint8_t stale_status_cleared)
{
    rsc_amc_track_t* t;
    if (!p || !req_id || slot >= RSC_AMCS || p->epoch != epoch || !p->irq_bound ||
        !(p->granted_mask & (1u << slot)) || stale_status_cleared != 1u)
        return 0;
    t = &p->amc[slot];
    if (t->state != RSC_AMC_VACANT)
        return 0;
    t->req_id = req_id;
    t->owner_epoch = epoch;
    t->expected_command_mask = expected_command_mask;
    t->expected_dependency_mask = expected_dependency_mask;
    t->state = RSC_AMC_IN_FLIGHT;
    return 1;
}

int rsc_completion_submit_batch(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                                uint16_t expected_command_mask, uint8_t stale_status_cleared)
{
    /* this compatibility API must not silently describe an dependency mask that differs from
     * CMD_ENABLE. */

    return rsc_completion_submit_batch_with_dependencies(
        p, epoch, slot, req_id, expected_command_mask, expected_command_mask, stale_status_cleared);
}

int rsc_completion_submit_batch_with_dependencies(rsc_completion_t* p, uint32_t epoch, uint8_t slot,
                                                  uint32_t req_id, uint16_t command_mask,
                                                  uint16_t dependency_mask,
                                                  uint8_t stale_status_cleared)
{
    /* dependency bits are command indices, never extra AMC/IRQ bits. The legal zero mask must
     * not be conflated with absent ownership. */

    if (!command_mask || (dependency_mask & (uint16_t)~command_mask))
        return 0;
    return submit_bookkeeping(p, epoch, slot, req_id, command_mask, dependency_mask,
                              stale_status_cleared);
}

int rsc_completion_submit(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                          uint8_t stale_status_cleared)
{

    return submit_bookkeeping(p, epoch, slot, req_id, 0u, 0u, stale_status_cleared);
}

uint8_t rsc_completion_observe_irq(rsc_completion_t* p, uint32_t epoch, uint32_t irq_status)
{
    uint8_t finished = 0u;
    uint8_t bits;
    unsigned slot;
    if (!p || !p->irq_bound || !p->granted_mask || p->epoch != epoch) {
        if (p && irq_status)
            ++p->stale_irq_cnt;
        return 0u;
    }
    bits = (uint8_t)(irq_status & RSC_AMC_MASK);
    if (irq_status & ~(uint32_t)RSC_AMC_MASK)
        ++p->unexpected_irq_cnt;
    for (slot = 0; slot < RSC_AMCS; ++slot) {
        rsc_amc_track_t* t = &p->amc[slot];
        uint8_t bit = (uint8_t)(1u << slot);
        if (!(bits & bit))
            continue;
        if (!(p->granted_mask & bit) || t->state != RSC_AMC_IN_FLIGHT || t->owner_epoch != epoch) {
            ++p->unexpected_irq_cnt;
            continue;
        }
        t->state = RSC_AMC_AWAITING_ACK;
        finished |= bit;
    }
    return finished;
}

int rsc_completion_confirm_ack(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                               uint8_t irq_acknowledged, uint8_t commands_complete)
{
    rsc_amc_track_t* t;
    if (!p || !req_id || slot >= RSC_AMCS || !p->irq_bound || p->epoch != epoch ||
        !(p->granted_mask & (1u << slot)) || irq_acknowledged != 1u || commands_complete != 1u)
        return 0;
    t = &p->amc[slot];
    if (t->owner_epoch != epoch || t->req_id != req_id || t->state != RSC_AMC_AWAITING_ACK)
        return 0;
    t->state = RSC_AMC_SIGNALED;
    return 1;
}

int rsc_completion_take(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t* req_id,
                        uint8_t* aborted)
{
    rsc_amc_track_t* t;
    if (!p || !req_id || !aborted || slot >= RSC_AMCS)
        return 0;
    t = &p->amc[slot];
    if (t->owner_epoch != epoch || (t->state != RSC_AMC_SIGNALED && t->state != RSC_AMC_ABORTED))
        return 0;
    *req_id = t->req_id;
    *aborted = (uint8_t)(t->state == RSC_AMC_ABORTED);
    memset(t, 0, sizeof(*t));
    return 1;
}

void rsc_completion_revoke(rsc_completion_t* p)
{
    unsigned slot;
    if (!p)
        return;
    p->granted_mask = 0u;
    p->irq_bound = 0u;
    for (slot = 0; slot < RSC_AMCS; ++slot)
        if (p->amc[slot].state == RSC_AMC_IN_FLIGHT || p->amc[slot].state == RSC_AMC_AWAITING_ACK)
            p->amc[slot].state = RSC_AMC_ABORTED;
    /* keep the old owner epoch for draining aborted requests. A new owner must select a higher
     * epoch, and cannot bind until all are drained. */
}
