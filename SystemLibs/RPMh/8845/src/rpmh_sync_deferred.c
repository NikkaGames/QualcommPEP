#include "rpmh_sync_deferred.h"
#include <string.h>

/* called from rpmh_scheduler_finish_deferred after accepting an exact owner/request/slot
 * completion; the caller already holds the same lock. */

static void notify_finished(void* ctx, uint64_t epoch, uint32_t req)
{
    rpmh_sync_deferred_t* b = ctx;
    if (!b || !epoch || epoch != b->owner_epoch || !req || req != b->active_req)
        return;
    if (b->timed_out_req == req) {
        /* called only after the physical completion ledger accepted this same ticket. record
         * evidence for passive timeout review, but do not wake an already-returned caller or
         * declare vote success. */
        b->late_completion_seen = 1u;
    } else if (!b->faulted) {
        if (b->io.defer_signal_until_irq_restored)
            b->awaiting_irq_restore = req;
        else
            b->io.signal(b->io.ctx);
    }
}

int rpmh_sync_deferred_finalized(rpmh_sync_deferred_t* b, uint64_t epoch)
{
    uint32_t id, i;
    int found = 0;
    if (!b || !epoch || b->owner_epoch != epoch || !b->scheduler ||
        !b->io.defer_signal_until_irq_restored || b->scheduler->owner_epoch != epoch ||
        !(id = b->awaiting_irq_restore) || id != b->active_req)
        return 0;
    /* must not release an ACK-pending or still executing command. */
    for (i = 0; i < RPMH_JOBS; ++i) {
        const rpmh_job_t* j = &b->scheduler->jobs[i];
        if (j->id == id && j->owner_epoch == epoch &&
            (j->state == RPMH_JOB_COMPLETED || j->state == RPMH_JOB_FAILED)) {
            found = 1;
            break;
        }
    }
    if (!found)
        return 0;
    b->awaiting_irq_restore = 0;
    if (b->timed_out_req == id) {
        b->late_completion_seen = 1u;
        return 1;
    }
    if (b->faulted)
        return 1;
    b->io.signal(b->io.ctx);
    return 1;
}

int rpmh_sync_deferred_bind(rpmh_sync_deferred_t* b, rpmh_scheduler_t* s, uint64_t epoch,
                            const rpmh_sync_wait_io_t* io)
{
    if (!b || !s || !io || b->scheduler || b->active_req || b->awaiting_irq_restore ||
        b->timed_out_req || b->late_completion_seen || b->faulted || !epoch ||
        s->owner_epoch != epoch || !s->arm_deferred || !s->execution_authorized ||
        s->on_deferred_finished || !io->enter || !io->leave_cb || !io->reset || !io->signal ||
        !io->wait_ms)
        return 0;
    /* owner must hold its admission mutex across promotion and this bind. The callbacks remain
     * pinned until worker + PnP rundown is proved. */

    b->scheduler = s;
    b->io = *io;
    b->owner_epoch = epoch;
    s->on_deferred_finished = notify_finished;
    s->on_deferred_finished_ctx = b;
    return 1;
}

int rpmh_sync_deferred_timeout_inspect(const rpmh_sync_deferred_t* b, uint64_t epoch, uint32_t req,
                                       rpmh_tcs_result_t* verified_result)
{
    const rpmh_scheduler_t* s;
    uint32_t i;
    if (!b || !epoch || !req || !verified_result || !b->faulted || b->timed_out_req != req ||
        b->active_req != req || b->owner_epoch != epoch || !(s = b->scheduler) ||
        s->owner_epoch != epoch || s->on_deferred_finished != notify_finished ||
        s->on_deferred_finished_ctx != b)
        return 0;
    for (i = 0u; i < RPMH_JOBS; ++i) {
        const rpmh_job_t* j = &s->jobs[i];
        if (j->id != req || j->owner_epoch != epoch)
            continue;
        if (j->state == RPMH_JOB_EXECUTING && j->slot < s->driver.active_cnt &&
            (s->busy_mask & (uint8_t)(1u << j->slot)))
            return 1;
        if (j->state == RPMH_JOB_COMPLETED && b->late_completion_seen && j->result == TCS_OK) {
            *verified_result = j->result;
            return 2;
        }
        if (j->state == RPMH_JOB_FAILED && b->late_completion_seen) {
            *verified_result = j->result;
            return 3;
        }
    }
    /* missing or mismatched ticket is NOT a verified retirement. */
    return 0;
}

int rpmh_sync_deferred_can_retire(const rpmh_sync_deferred_t* w, const rpmh_scheduler_t* s,
                                  uint64_t epoch)
{
    return w && s && epoch && w->scheduler == s && w->owner_epoch == epoch && !w->active_req &&
           !w->awaiting_irq_restore && !w->timed_out_req && !w->late_completion_seen &&
           !w->faulted && s->owner_epoch == epoch && s->on_deferred_finished == notify_finished &&
           s->on_deferred_finished_ctx == w;
}

static int find_issued(const rpmh_scheduler_t* s, uint32_t req, uint64_t epoch)
{
    uint32_t i;
    for (i = 0u; i < RPMH_JOBS; ++i)
        if (s->jobs[i].id == req && s->jobs[i].state == RPMH_JOB_EXECUTING &&
            s->jobs[i].owner_epoch == epoch)
            return 1;
    return 0;
}

rpmh_tcs_result_t rpmh_sync_deferred_execute(rpmh_sync_deferred_t* b, uint8_t client,
                                             const tcs_command_t* commands, uint32_t cnt,
                                             uint32_t* responses, uint32_t timeout_ms)
{
    rpmh_scheduler_t* s;
    uint32_t req = 0u;
    uint32_t i;
    rpmh_tcs_result_t result = TCS_INVALID;
    int waited;
    if (!b || !b->scheduler || !timeout_ms || !commands || !cnt || cnt > TCS_BATCH_MAX ||
        !b->io.enter(b->io.ctx))
        return TCS_INVALID;
    s = b->scheduler;
    if (b->faulted || b->active_req || b->awaiting_irq_restore || b->timed_out_req ||
        s->owner_epoch != b->owner_epoch || !s->arm_deferred || !s->execution_authorized ||
        s->on_deferred_finished != notify_finished || s->on_deferred_finished_ctx != b) {
        b->io.leave_cb(b->io.ctx);
        return TCS_NO_OWNERSHIP;
    }

    b->io.reset(b->io.ctx);
    if (!rpmh_scheduler_submit(s, client, RPMH_ACTIVE, commands, cnt, &req)) {
        b->io.leave_cb(b->io.ctx);
        return TCS_INVALID;
    }
    b->active_req = req;
    if (!responses) {
        for (i = 0u; i < RPMH_JOBS; ++i)
            if (s->jobs[i].id == req) {
                s->jobs[i].capture_responses = 0u;
                break;
            }
    }
    (void)rpmh_scheduler_pump(s);
    if ((!b->io.defer_signal_until_irq_restored || !b->awaiting_irq_restore) &&
        rpmh_scheduler_take(s, req, &result, responses, cnt)) {
        b->active_req = 0;
        b->io.leave_cb(b->io.ctx);
        return result;
    }
    /* a queued command NEVER may fire after returning BUSY to its caller. */
    if (rpmh_scheduler_cancel_queued_active(s, client, req)) {
        b->active_req = 0;
        b->io.leave_cb(b->io.ctx);
        return TCS_BUSY;
    }
    if (!find_issued(s, req, b->owner_epoch)) {
        b->faulted = 1;
        rpmh_scheduler_revoke(s);
        b->active_req = 0;
        b->io.leave_cb(b->io.ctx);
        return TCS_IO_FAILURE;
    }
    b->io.leave_cb(b->io.ctx);
    /* never sleep holding the shared scheduler mutex: the WDF worker must acquire it to retire
     * and signal the immutable request ticket. */

    waited = b->io.wait_ms(b->io.ctx, timeout_ms);
    if (!b->io.enter(b->io.ctx)) {
        /* the immutable active_req was published under the owner mutex before release. Keep it
         * pinned when lock acquisition fails; no unguarded state edits, no replay and no owner
         * retirement. */
        return TCS_IO_FAILURE;
    }
    /* kewaitforsingleobject can report timeout just before an exact worker finalization sets
     * the event. Once the owner mutex is reacquired, the completed ticket plus cleared restore
     * barrier are authoritative. Do not quarantine a command whose IRQ was already restored. */
    if ((!b->io.defer_signal_until_irq_restored || !b->awaiting_irq_restore) &&
        rpmh_scheduler_take(s, req, &result, responses, cnt)) {
        b->active_req = 0;
        b->io.leave_cb(b->io.ctx);
        return result;
    }
    /* a timeout is not proof a physical vote failed: retain any issued AMC reservation and
     * quarantine admission. Never cancel/replay it. */

    (void)rpmh_scheduler_cancel_queued_active(s, client, req);
    b->faulted = 1;
    b->timed_out_req = req;
    /* preserve active_req and the immutable hardware ticket for an eventual exact physical
     * IRQ/ACK. A caller that saw TIMEOUT may not infer success even after the late completion
     * is proved. */
    rpmh_scheduler_revoke(s);
    b->io.leave_cb(b->io.ctx);
    /* wdf queueing is outside the scheduler mutex. failure leaves this request/epoch/fault
     * pinned; it is NEVER a reason to retry the vote. */
    if (b->io.queue_timeout_review)
        (void)b->io.queue_timeout_review(b->io.ctx, b->owner_epoch, req);
    return waited ? TCS_IO_FAILURE : TCS_TIMEOUT;
}
