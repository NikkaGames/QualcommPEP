#include "oem_wdf_irq_bridge.h"

int oem_irq_bridge_valid(const oem_wdf_irq_bridge_t* b)
{
    return b && b->pipeline && b->epoch && b->enter && b->leave_cb &&
           b->read_owned_completion_pending && b->pipeline->initialized && b->pipeline->bound &&
           !b->pipeline->faulted && b->pipeline->epoch == b->epoch;
}

static oem_irq_claim_t dispatch_isr_locked(oem_wdf_irq_bridge_t* b, int passive_drain)
{
    rpmh_irq_pipeline_t* p;
    if (!oem_irq_bridge_valid(b))
        return OEM_IRQ_UNCLAIMED;
    p = b->pipeline;
    /* do not mask that interrupt, issue a DPC or consume an AMC ticket unless the owner sees
     * its actual IRQ_STATUS assertion. A failed src read must not be misinterpreted as an IRQ. */

    {
        int pending = b->read_owned_completion_pending(b->serialize_ctx, b->epoch);
        if (pending != 1) {
            if (pending < 0 && p->pending.active && p->pending.epoch == b->epoch) {
                /* do not free its lifetime token or clear hardware. */

                p->faulted = 1;
                return OEM_IRQ_QUARANTINED;
            }
            return OEM_IRQ_UNCLAIMED;
        }
    }
    if (passive_drain ? rpmh_pipeline_capture_passive(p, b->epoch)
                      : rpmh_pipeline_dispatch_isr(p, b->epoch))
        return OEM_IRQ_CLAIMED;
    if (p->pending.active && p->pending.epoch == b->epoch) {

        return OEM_IRQ_QUARANTINED;
    }
    return OEM_IRQ_UNCLAIMED;
}

oem_irq_claim_t oem_irq_bridge_wdf_locked_isr(oem_wdf_irq_bridge_t* b)
{
    /* kmdf serializes the passive ISR. Its enter callback takes only the separate RPMh mutex,
     * never the WDF lock again. */
    oem_irq_claim_t result;
    if (!b || !b->enter || !b->leave_cb || !b->enter(b->serialize_ctx))
        return OEM_IRQ_UNCLAIMED;
    result = dispatch_isr_locked(b, b->passive_drain == 1u);
    b->leave_cb(b->serialize_ctx);
    /* this passive callback acquires scheduler then transport locks without waiting on the WDF
     * lock. An incomplete ACK retains its ticket and queues work. */
    if (result == OEM_IRQ_CLAIMED && b->passive_drain == 1u &&
        !rpmh_pipeline_dispatch_worker(b->pipeline, b->epoch))
        return OEM_IRQ_QUARANTINED;
    return result;
}

oem_irq_claim_t oem_irq_bridge_isr(oem_wdf_irq_bridge_t* b)
{
    oem_irq_claim_t r;
    if (!b || !b->enter || !b->leave_cb || !b->enter(b->serialize_ctx))
        return OEM_IRQ_UNCLAIMED;
    r = dispatch_isr_locked(b, 0);
    b->leave_cb(b->serialize_ctx);
    return r;
}

int oem_irq_bridge_dpc(oem_wdf_irq_bridge_t* b)
{
    rpmh_irq_pipeline_t* p;
    /* no external DIRQL lock: the pending epoch ticket was pinned BEFORE ISR masked and queued
     * this DPC. PnP cannot release while pinned. */

    if (!oem_irq_bridge_valid(b))
        return 0;
    p = b->pipeline;
    if (!p->pending.active || p->pending.epoch != b->epoch)
        return 0;
    return rpmh_pipeline_dispatch_dpc(p, b->epoch);
}

int oem_irq_bridge_worker(oem_wdf_irq_bridge_t* b)
{
    rpmh_irq_pipeline_t* p;
    /* passive completion/MMIO cannot run holding KMDF's DIRQL lock. The masked IRQ and pinned
     * ticket prevent ISR admission or owner release while the worker processes that exact
     * transaction. */

    if (!b || !b->pipeline || !b->epoch || b->pipeline->epoch != b->epoch || !b->pipeline->bound)
        return 0;
    p = b->pipeline;
    if (!p->pending.active || p->pending.epoch != b->epoch)
        return 0;
    return rpmh_pipeline_dispatch_worker(p, b->epoch);
}
