#include "rpmh_amc_drain.h"
#include <string.h>

static rpmh_job_t* job_for_slot(rpmh_scheduler_t* s, uint64_t epoch, uint8_t slot)
{
    uint32_t i;
    if (!s || s->owner_epoch != epoch)
        return NULL;
    for (i = 0; i < RPMH_JOBS; ++i) {
        rpmh_job_t* j = &s->jobs[i];
        if (j->state == RPMH_JOB_EXECUTING && j->owner_epoch == epoch && j->slot == slot && j->id)
            return j;
    }
    return NULL;
}

int rpmh_amc_drain_bind(rpmh_amc_drain_t* d, rpmh_deferred_scheduler_t* b)
{
    if (!d || !b || !b->scheduler || !b->tcs || !b->ledger || !b->epoch ||
        !b->scheduler->arm_deferred || b->scheduler->owner_epoch != b->epoch ||
        b->ledger->epoch != b->ledger_epoch || !b->ledger->irq_bound ||
        !b->tcs->completion_handler_bound || !b->tcs->dpc_consumes_completion ||
        !b->tcs->io.read32 || !b->tcs->io.write32 || !b->tcs->io.barrier ||
        !b->tcs->external_grant_mask || d->binding || d->epoch)
        return 0;
    memset(d, 0, sizeof(*d));
    d->binding = b;
    d->epoch = b->ledger_epoch;
    d->owner_epoch = b->epoch;
    d->selected = 0xFFu;
    return 1;
}

int rpmh_amc_drain_has_finished(void* ctx)
{
    rpmh_amc_drain_t* d = ctx;
    rpmh_deferred_scheduler_t* b;
    rsc_irq_io_t io;
    uint32_t pending, slot;
    int outcome = 0;
    if (!d || !(b = d->binding) || !d->epoch || b->epoch != d->owner_epoch ||
        b->ledger->epoch != d->epoch || !b->ledger->irq_bound ||
        !b->tcs->completion_handler_bound || !b->tcs->dpc_consumes_completion ||
        !b->tcs->io.read32 || !b->scheduler->arm_deferred || !b->tcs->io.lock ||
        !b->tcs->io.unlock || !b->tcs->io.ctx)
        return -1;
    if (d->selected != 0xFFu) {
        if (d->selected >= RSC_AMCS || !d->latched[d->selected].valid)
            return -1;
        return 1;
    }
    io.ctx = b->tcs->io.ctx;
    io.read32 = b->tcs->io.read32;
    io.write32 = b->tcs->io.write32;
    io.barrier = b->tcs->io.barrier;
    /* the owner supplies the identical PASSIVE RPMh command mutex used by the issuer and WDF
     * ISR callback, independent of WDF's IRQ lock. No TCS read/ledger capture can race with
     * command publication. */
    if (!b->tcs->io.lock(b->tcs->io.ctx))
        return -2;
    if (!io.read32(io.ctx, TCS_IRQ_STATUS, &pending)) {
        outcome = -1;
        goto done;
    }
    /* a claimed physical IRQ may precede visibility of its RSC status bit. No pending bit is
     * NOT proof that an issued AMC has been retired. The passive worker will recheck the same
     * pinned ticket without ACK, unmask, or command reissue; a persistent mismatch remains
     * faulted. */
    if (!(pending & b->ledger->granted_mask) &&
        (b->scheduler->busy_mask & b->tcs->owned_amc_mask)) {
        outcome = -3;
        goto done;
    }
    for (slot = 0; slot < RSC_AMCS; ++slot) {
        rpmh_job_t* j;
        rsc_amc_track_t* entry;
        rsc_irq_result_t r;
        uint32_t bit = 1u << slot;
        if (!(pending & bit) || !(b->ledger->granted_mask & bit))
            continue;
        if (!(b->tcs->external_grant_mask & bit) || !(b->tcs->owned_amc_mask & bit)) {
            outcome = -1;
            goto done;
        }
        j = job_for_slot(b->scheduler, d->owner_epoch, (uint8_t)slot);
        entry = &b->ledger->amc[slot];
        if (!j || j->id != entry->req_id || entry->owner_epoch != d->epoch ||
            entry->state != RSC_AMC_IN_FLIGHT || d->latched[slot].valid) {
            outcome = -1;
            goto done;
        }
        if (d->irq_src) {
            if (d->irq_src->dev != b->tcs || d->irq_src->ledger != b->ledger ||
                d->irq_src->owner_epoch != d->owner_epoch || d->irq_src->ledger_epoch != d->epoch) {
                outcome = -1;
                goto done;
            }
            r = rsc_irq_capture_owned_src(d->irq_src, d->owner_epoch, (uint8_t)slot, j->id,
                                          entry->expected_command_mask, &d->latched[slot]);
        } else
            r = rsc_irq_capture_pending(b->ledger, &io, d->epoch, (uint8_t)slot, j->id,
                                        entry->expected_command_mask, &d->latched[slot]);
        if (r != RSC_IRQ_DEFERRED) {
            outcome = -1;
            goto done;
        }
        d->selected = (uint8_t)slot;
        outcome = 1;
        goto done;
    }
done:
    b->tcs->io.unlock(b->tcs->io.ctx);
    return outcome;
}

int rpmh_amc_drain_finish_one(void* ctx)
{
    rpmh_amc_drain_t* d = ctx;
    rsc_irq_latch_t* l;
    rpmh_tcs_result_t r;
    if (!d || !d->binding || d->binding->epoch != d->owner_epoch || d->selected >= RSC_AMCS)
        return 0;
    l = &d->latched[d->selected];
    if (!l->valid || l->owner_epoch != d->epoch || l->slot != d->selected)
        return 0;
    r = rpmh_deferred_scheduler_finish(d->binding, l);
    if (r == TCS_OK) {
        if (l->valid)
            return 0;
        d->selected = 0xFFu;
        return 1;
    }
    /* retry only while the IDENTICAL ticket still exists; an uncertain W1C readback is
     * eligible only if our sticky ACK attempt was recorded. An absent owner or unlatched
     * failure never becomes a new power vote. */

    if (l->valid &&
        (r == TCS_BUSY || r == TCS_ACK_PENDING || (r == TCS_IO_FAILURE && l->ack_attempted)))
        return 2;
    return 0;
}
