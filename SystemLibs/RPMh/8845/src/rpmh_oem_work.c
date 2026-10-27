#include "rpmh_oem_work.h"
#include <string.h>

int rpmh_work_bind(rpmh_oem_work_t* w, const rpmh_work_ops_t* o, uint64_t epoch)
{
    if (!w || !o || !epoch || w->state != RPMH_WORK_UNBOUND || !o->mask_owned_interrupt ||
        !o->queue_dpc || !o->queue_pep_work || !o->has_finished_amc || !o->finish_one_amc ||
        !o->unmask_owned_interrupt || !o->enter_completion_lock || !o->leave_completion_lock)
        return 0;
    w->ops = *o;
    w->owner_epoch = epoch;
    w->irq_masked = 0;
    w->finished = 0;
    w->ack_retry_cnt = 0;
    w->lock_retry_cnt = 0;
    w->completion_lock_held = 0;
    w->defer_unmask = 0;
    w->state = RPMH_WORK_READY;
    return 1;
}

int rpmh_work_on_isr(rpmh_oem_work_t* w, uint64_t epoch)
{
    if (!w || w->owner_epoch != epoch || !epoch || w->state != RPMH_WORK_READY || w->irq_masked ||
        w->completion_lock_held)
        return 0;
    /* ordering: mask at ISR, queue DPC, do not touch TCS W1C. */
    if (!w->ops.mask_owned_interrupt(w->ops.ctx)) {
        w->state = RPMH_WORK_QUARANTINED;
        return 0;
    }

    w->irq_masked = 1;
    w->state = RPMH_WORK_DPC_PENDING;
    if (!w->ops.queue_dpc(w->ops.ctx)) {
        w->state = RPMH_WORK_QUARANTINED;
        return 0;
    }
    return 1;
}

int rpmh_work_on_dpc(rpmh_oem_work_t* w, uint64_t epoch)
{
    if (!w || !epoch || w->owner_epoch != epoch || w->state != RPMH_WORK_DPC_PENDING ||
        !w->irq_masked)
        return 0;

    w->state = RPMH_WORK_WORKER_PENDING;
    if (!w->ops.queue_pep_work(w->ops.ctx)) {
        w->state = RPMH_WORK_QUARANTINED;
        return 0;
    }
    return 1;
}

int rpmh_work_on_worker(rpmh_oem_work_t* w, uint64_t epoch)
{
    unsigned n;
    int completed = 0, io_error = 0, retry_same_ticket = 0;
    if (!w || !epoch || w->owner_epoch != epoch || w->state != RPMH_WORK_WORKER_PENDING ||
        !w->irq_masked || w->completion_lock_held)
        return 0;
    /* the caller must additionally serialize WDF callbacks and rundown. */

    w->state = RPMH_WORK_WORKER_ACTIVE;
    if (!w->ops.enter_completion_lock(w->ops.ctx)) {
        w->state = RPMH_WORK_QUARANTINED;
        return 0; /* fail CLOSED: keep the IRQ masked. */
    }
    w->completion_lock_held = 1;
    for (n = 0; n < RPMH_WORK_DRAIN_BUDGET; ++n) {
        int pending = w->ops.has_finished_amc(w->ops.ctx);
        if (pending == -2 || pending == -3) {
            /* -2 means completion lock unavailable; -3 means IRQ claimed while AMC is still issued
             * without matching IRQ_STATUS. Retry only the same pinned masked-IRQ worker; do not ACK
             * or replay the vote. */

            if (w->lock_retry_cnt >= RPMH_WORK_IRQ_LOCK_RETRY_MAX)
                io_error = 1;
            else {
                ++w->lock_retry_cnt;
                retry_same_ticket = 1;
            }
            break;
        }
        if (pending < 0) {
            io_error = 1;
            break;
        }
        w->lock_retry_cnt = 0;
        if (!pending) {
            completed = 1;
            break;
        }
        {
            int r = w->ops.finish_one_amc(w->ops.ctx);
            if (r == 2) {
                /* a latched IRQ may need to repeat W1C readback or wait for individual command
                 * completion. only this same ticket may retry; the owner keeps its src masked
                 * throughout. */

                if (w->ack_retry_cnt >= RPMH_WORK_ACK_RETRY_MAX)
                    io_error = 1;
                else {
                    ++w->ack_retry_cnt;
                    retry_same_ticket = 1;
                }
                break;
            }
            if (r != 1) {
                io_error = 1;
                break;
            }
        }
        w->ack_retry_cnt = 0;
        ++w->finished;
    }
    /* never unmask with the controller completion lock held. */

    w->ops.leave_completion_lock(w->ops.ctx);
    w->completion_lock_held = 0;
    if (retry_same_ticket && !io_error) {
        /* the queue operation executes after releasing the completion lock. No transient result is
         * ever acknowledged as hardware success. */

        w->state = RPMH_WORK_WORKER_PENDING;
        if (w->ops.queue_pep_work(w->ops.ctx))
            return 2;
    } else if (completed) {
        w->ack_retry_cnt = 0;
        if (w->defer_unmask) {
            /* never unmask a WDF IRQ with its old immutable admission ticket still active. the
             * scheduler must retire it first. */

            w->state = RPMH_WORK_COMPLETION_READY;
            return 3;
        }
        if (w->ops.unmask_owned_interrupt(w->ops.ctx)) {
            w->irq_masked = 0;
            w->state = RPMH_WORK_READY;
            return 1;
        }
    } else if (!io_error && n == RPMH_WORK_DRAIN_BUDGET) {

        w->state = RPMH_WORK_WORKER_PENDING;
        if (w->ops.queue_pep_work(w->ops.ctx))
            return 2;
    }
    w->state = RPMH_WORK_QUARANTINED;
    return 0;
}

int rpmh_work_revoke(rpmh_oem_work_t* w, uint64_t epoch)
{
    if (!w || !epoch || w->owner_epoch != epoch || w->state != RPMH_WORK_READY || w->irq_masked ||
        w->completion_lock_held)
        return 0;
    memset(w, 0, sizeof(*w));
    return 1;
}
