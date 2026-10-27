#include "rpmh_wdf_quiesce.h"
#include <string.h>

void rpmh_quiesce_init(rpmh_wdf_quiesce_t* q)
{
    if (q)
        memset(q, 0, sizeof(*q));
}

int rpmh_quiesce_begin(rpmh_wdf_quiesce_t* q, rpmh_irq_pipeline_t* p, uint64_t epoch)
{
    if (!q || !p || !epoch || epoch <= q->last_epoch ||
        (q->phase != RPMH_QUIESCE_IDLE && q->phase != RPMH_QUIESCE_RELEASED) || !p->bound ||
        p->faulted || p->epoch != epoch)
        return 0;
    /* is externally serialized with pipeline Dispatch* and the owner lifecycle. */

    if (!rpmh_pipeline_close(p, epoch))
        return 0;
    q->pipeline = p;
    q->epoch = epoch;
    q->last_epoch = epoch;
    q->phase = RPMH_QUIESCE_CLOSED;
    return 1;
}

int rpmh_quiesce_record(rpmh_wdf_quiesce_t* q, uint64_t epoch, rpmh_wdf_quiesce_step_t step)
{
    if (!q || !q->pipeline || !epoch || q->epoch != epoch || !q->pipeline->bound ||
        q->pipeline->epoch != epoch)
        return 0;
    /* reject duplicates and out-of-order assertions; there is no route to skip WDF
     * ISR/DPC/worker drain or resurrect a released epoch. */

    switch (step) {
        case RPMH_OBSERVE_INTERRUPT_DISABLED:
            if (q->phase != RPMH_QUIESCE_CLOSED)
                return 0;
            q->phase = RPMH_QUIESCE_INTERRUPT_DISABLED;
            return 1;
        case RPMH_OBSERVE_ISR_QUIESCENT:
            if (q->phase != RPMH_QUIESCE_INTERRUPT_DISABLED)
                return 0;
            q->phase = RPMH_QUIESCE_ISR_QUIESCENT;
            return 1;
        case RPMH_OBSERVE_DPC_FLUSHED:
            if (q->phase != RPMH_QUIESCE_ISR_QUIESCENT)
                return 0;
            q->phase = RPMH_QUIESCE_DPC_FLUSHED;
            return 1;
        case RPMH_OBSERVE_WORK_FLUSHED:
            /* never stamp work_flushed prematurely. */

            if (q->phase != RPMH_QUIESCE_DPC_FLUSHED || q->pipeline->faulted ||
                q->pipeline->pending.active || q->pipeline->pending.epoch ||
                q->pipeline->unmask_finalizing.active || q->pipeline->unmask_finalizing.epoch ||
                q->pipeline->work.state != RPMH_WORK_READY || q->pipeline->work.irq_masked ||
                q->pipeline->work.completion_lock_held ||
                !rpmh_irq_gate_closed_and_drained(&q->pipeline->gate, epoch))
                return 0;
            q->phase = RPMH_QUIESCE_WORK_FLUSHED;
            return 1;
        default:
            return 0;
    }
}

int rpmh_quiesce_release(rpmh_wdf_quiesce_t* q, uint64_t epoch)
{
    if (!q || !q->pipeline || !epoch || q->epoch != epoch ||
        q->phase != RPMH_QUIESCE_WORK_FLUSHED || !rpmh_pipeline_release(q->pipeline, epoch))
        return 0;
    q->pipeline = 0;
    q->epoch = 0;
    q->phase = RPMH_QUIESCE_RELEASED;
    return 1;
}

/* framework-neutral quiesce steps require separate WDF registration, controller grants and callback
 * rundown. */

int rpmh_quiesce_drive(rpmh_wdf_quiesce_t* q, uint64_t epoch, const rpmh_wdf_framework_ops_t* ops)
{
    if (!q || !epoch || !ops || !ops->disable_owned_interrupt || !ops->sync_isr ||
        !ops->flush_queued_dpcs || !ops->flush_deferred_work || !q->pipeline || q->epoch != epoch ||
        q->pipeline->epoch != epoch || !q->pipeline->bound)
        return 0;

    /* the owner must externally serialize with PnP, ISR/DPC state transitions and the callback
     * provider. This loop never spins on a pending AMC completion; the owner may retry after
     * it drains. */

    for (;;) {
        switch (q->phase) {
            case RPMH_QUIESCE_CLOSED:
                if (!ops->disable_owned_interrupt(ops->ctx) ||
                    !rpmh_quiesce_record(q, epoch, RPMH_OBSERVE_INTERRUPT_DISABLED))
                    return 0;
                break;
            case RPMH_QUIESCE_INTERRUPT_DISABLED:
                if (!ops->sync_isr(ops->ctx) ||
                    !rpmh_quiesce_record(q, epoch, RPMH_OBSERVE_ISR_QUIESCENT))
                    return 0;
                break;
            case RPMH_QUIESCE_ISR_QUIESCENT:
                if (!ops->flush_queued_dpcs(ops->ctx) ||
                    !rpmh_quiesce_record(q, epoch, RPMH_OBSERVE_DPC_FLUSHED))
                    return 0;
                break;
            case RPMH_QUIESCE_DPC_FLUSHED:
                if (!ops->flush_deferred_work(ops->ctx) ||
                    !rpmh_quiesce_record(q, epoch, RPMH_OBSERVE_WORK_FLUSHED))
                    return 0;
                break;
            case RPMH_QUIESCE_WORK_FLUSHED:
                return 1;
            default:
                return 0;
        }
    }
}
