#include "rpmh_irq_pipeline.h"
#include <string.h>

int rpmh_pipeline_init(rpmh_irq_pipeline_t* p)
{
    if (!p || p->initialized || p->bound)
        return 0;
    memset(p, 0, sizeof(*p));
    rpmh_irq_gate_init(&p->gate);
    p->initialized = 1;
    return 1;
}

int rpmh_pipeline_bind_owner(rpmh_irq_pipeline_t* p, const rpmh_work_ops_t* ops, uint64_t epoch)
{
    if (!p || !p->initialized || p->bound || p->faulted || p->pending.active || !epoch || !ops)
        return 0;
    if (!rpmh_work_bind(&p->work, ops, epoch))
        return 0;
    if (!rpmh_irq_gate_open(&p->gate, epoch)) {
        (void)rpmh_work_revoke(&p->work, epoch);
        return 0;
    }
    /* retire the previous pinned ticket BEFORE issuing that physical unmask. */

    p->work.defer_unmask = 1;
    p->epoch = epoch;
    p->bound = 1;
    return 1;
}

int rpmh_pipeline_dispatch_isr(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!p || !p->bound || p->faulted || p->epoch != epoch || p->pending.active ||
        p->unmask_finalizing.active || p->work.state != RPMH_WORK_READY)
        return 0;
    if (!rpmh_irq_gate_try_pin(&p->gate, epoch, &p->pending))
        return 0;
    if (!rpmh_work_on_isr(&p->work, epoch)) {
        /* mask may have partially changed hardware, or scheduling failed. retain the owner
         * reference, even if a provider falsely reports that its mask operation failed without
         * touching hardware. */

        p->faulted = 1;
        return 0;
    }
    return 1;
}

int rpmh_pipeline_dispatch_dpc(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!p || !p->bound || p->faulted || p->epoch != epoch || !p->pending.active ||
        p->pending.epoch != epoch)
        return 0;
    if (!rpmh_work_on_dpc(&p->work, epoch)) {
        p->faulted = 1;
        return 0;
    }
    return 1;
}

int rpmh_pipeline_capture_passive(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!p || !p->bound || p->faulted || p->epoch != epoch || p->pending.active ||
        p->unmask_finalizing.active || p->work.state != RPMH_WORK_READY || p->work.irq_masked ||
        p->work.completion_lock_held || !rpmh_irq_gate_try_pin(&p->gate, epoch, &p->pending))
        return 0;
    /* mask_owned_interrupt independently rechecks the physical owned src. A partially failed
     * mask retains the same pin and faults admission. */
    if (!p->work.ops.mask_owned_interrupt(p->work.ops.ctx)) {
        p->work.state = RPMH_WORK_QUARANTINED;
        p->faulted = 1u;
        return 0;
    }
    p->work.irq_masked = 1u;
    p->work.state = RPMH_WORK_WORKER_PENDING;
    return 1;
}

int rpmh_pipeline_dispatch_worker(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    int result;
    if (!p || !p->bound || p->faulted || p->epoch != epoch || !p->pending.active ||
        p->pending.epoch != epoch)
        return 0;
    result = rpmh_work_on_worker(&p->work, epoch);
    if (result == 2)
        return 2; /* requeued: SAME lifetime ticket. */
    if (result == 3 && p->work.state == RPMH_WORK_COMPLETION_READY && p->work.irq_masked &&
        rpmh_irq_gate_retain_ticket(&p->gate, &p->pending, &p->unmask_finalizing) &&
        rpmh_irq_gate_retire_ticket(&p->gate, &p->pending)) {
        /* do not let owner Close/release mistake an empty pending for a fully drained callback.
         * The second gate reference survives until the physical unmask write/readback has
         * returned. */

        int unmasked;
        /* no new issuer can observe readiness before src restoration. */
        unmasked = p->work.ops.unmask_owned_interrupt(p->work.ops.ctx);
        if (unmasked) {
            /* physical unmask was proved. Publish readiness while the final ticket still blocks new
             * TCS/ISR admission. */
            p->work.irq_masked = 0;
            p->work.state = RPMH_WORK_READY;
            if (rpmh_irq_gate_retire_ticket(&p->gate, &p->unmask_finalizing)) {
                if (p->work.on_owner_irq_restored)
                    p->work.on_owner_irq_restored(p->work.restored_ctx);
                return 1;
            }
            p->work.state = RPMH_WORK_QUARANTINED;
        } else {
            /* ambiguous unmask outcome: fail closed and keep the second owner reference forever
             * pending an independent hardware recovery. Do not release MMIO with a possibly live
             * IRQ. */

            p->work.irq_masked = 1;
            p->work.state = RPMH_WORK_QUARANTINED;
        }
    } else if (result == 1 && !p->work.defer_unmask &&
               rpmh_irq_gate_retire_ticket(&p->gate, &p->pending))
        return 1;

    p->faulted = 1;
    return 0;
}

int rpmh_pipeline_close(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!p || !p->bound || p->epoch != epoch)
        return 0;
    return rpmh_irq_gate_close(&p->gate, epoch);
}

int rpmh_pipeline_release(rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!p || !p->bound || p->faulted || p->epoch != epoch || p->pending.active ||
        p->pending.epoch || p->unmask_finalizing.active || p->unmask_finalizing.epoch ||
        !rpmh_irq_gate_closed_and_drained(&p->gate, epoch) || !rpmh_work_revoke(&p->work, epoch))
        return 0;
    p->epoch = 0;
    p->bound = 0;
    return 1;
}
