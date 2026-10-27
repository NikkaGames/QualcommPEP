/* qualcomm Molokai RSC DRV2 async TCS issue + deferred completion. It does not invent the
 * WDF owner or an ISR binding. */

#include "rpmh_tcs.h"
#include "rsc_irq_latched.h"

static int deferred_io_complete(const rpmh_tcs_io_t* io)
{
    /* the PASSIVE TCS mutex also serializes the OEM WDF ISR bridge and passive completion
     * worker. enter_irq_lock is an owner/epoch admission check only, performed UNDER that
     * mutex, not a KMDF lock acquire. */

    return io->read32 && io->write32 && io->barrier && io->lock && io->unlock &&
           io->enter_irq_lock && io->leave_irq_lock && io->irq_lock_ctx;
}

static int enter_irq(const rpmh_tcs_io_t* io)
{
    return io->enter_irq_lock(io->irq_lock_ctx);
}

static void leave_irq(const rpmh_tcs_io_t* io)
{
    io->leave_irq_lock(io->irq_lock_ctx);
}

/* the software ledger must be armed before the hardware trigger. Not a probe or a grant:
 * both the TCS owner and the IRQ consumer must be active independently. */

rpmh_tcs_result_t rpmh_tcs_arm_deferred(rpmh_tcs_t* dev, rsc_completion_t* ledger, uint32_t epoch,
                                        uint32_t req_id, uint8_t slot,
                                        const tcs_command_t* commands, uint32_t cnt,
                                        uint32_t dependency_mask)
{
    rpmh_tcs_result_t result = TCS_IO_FAILURE;
    rpmh_tcs_io_t* io;
    uint32_t mask, bit, n, status, control, irq, enable, wait;
    uint16_t read_mask = 0u;
    int touched = 0;
    int irq_locked = 0;
    if (!dev || !ledger || !epoch || !req_id || !commands || !cnt || cnt > TCS_BATCH_MAX ||
        slot >= 3u)
        return TCS_INVALID;
    mask = cnt == 16u ? 0xFFFFu : (1u << cnt) - 1u;
    if (dependency_mask & ~mask)
        return TCS_INVALID;
    for (n = 0u; n < cnt; ++n) {
        if ((commands[n].address & ~0x7FFFFu) || commands[n].read > 1u ||
            (commands[n].read && commands[n].data))
            return TCS_INVALID;
        if (commands[n].read)
            read_mask |= (uint16_t)(1u << n);
    }
    bit = 1u << slot;
    dev->last_arm_busy_reason[slot] = AMC_BUSY_NONE;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound)
        return TCS_NO_OWNERSHIP;
    if (!dev->dpc_consumes_completion)
        return TCS_COMPLETION_CONSUMER_CONFLICT;
    io = &dev->io;
    if (!deferred_io_complete(io))
        return TCS_INVALID;
    if (!io->lock(io->ctx)) {
        dev->last_arm_busy_reason[slot] = AMC_BUSY_TRANSPORT_LOCK;
        return TCS_BUSY;
    }
    /* recheck under the owner lock: PnP teardown, ISR capture, worker and transport handoff
     * MUST synchronize with this exact transaction. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound ||
        !(dev->owned_amc_mask & (uint8_t)bit) || ledger->epoch != epoch || !ledger->irq_bound ||
        !(ledger->granted_mask & (uint8_t)bit)) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    if (!dev->dpc_consumes_completion) {
        result = TCS_COMPLETION_CONSUMER_CONFLICT;
        goto done;
    }
    if ((dev->deferred_active_mask & (uint8_t)bit) || ledger->amc[slot].state != RSC_AMC_VACANT) {
        dev->last_arm_busy_reason[slot] = AMC_BUSY_LEDGER_TICKET;
        result = TCS_BUSY;
        goto done;
    }
    /* the physical eligibility snapshot belongs to the SAME OEM command mutex interval as the
     * ISR and publication. WDF connect/disconnect must synchronize their state changes through
     * the same owner mutex. */

    if (!enter_irq(io)) {
        uint32_t cause = io->get_irq_lock_refusal ? io->get_irq_lock_refusal(io->irq_lock_ctx) : 0u;
        /* no reader in the portable tests: retain the conservative generic result rather than
         * inventing an unobserved WDF failure category. */
        if (cause >= AMC_BUSY_IRQ_CTX && cause <= AMC_BUSY_IRQ_REVOKED)
            dev->last_arm_busy_reason[slot] = (uint8_t)cause;
        else
            dev->last_arm_busy_reason[slot] = AMC_BUSY_INTERRUPT_LOCK;
        result = TCS_BUSY;
        goto done;
    }
    irq_locked = 1;
    /* native owner and ticket must still match AFTER lock acquisition. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !(dev->owned_amc_mask & (uint8_t)bit) ||
        !dev->completion_handler_bound || !dev->dpc_consumes_completion || ledger->epoch != epoch ||
        !ledger->irq_bound || !(ledger->granted_mask & (uint8_t)bit) ||
        (dev->deferred_active_mask & (uint8_t)bit) || ledger->amc[slot].state != RSC_AMC_VACANT) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq))
        goto done;
    if (!(irq & bit)) {
        result = TCS_IRQ_NOT_ENABLED;
        goto done;
    }
    if (!io->read32(io->ctx, TCS_STATUS(slot), &status) ||
        !io->read32(io->ctx, TCS_CONTROL(slot), &control) ||
        !io->read32(io->ctx, TCS_ENABLE(slot), &enable) ||
        !io->read32(io->ctx, TCS_WAIT(slot), &wait) || !io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
        goto done;
    if (!(status & TCS_STATUS_IDLE) ||
        (control & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || (irq & bit)) {
        /* an IRQ belongs to the current deferred worker; this producer MUST NOT write
         * TCS_IRQ_CLEAR or race the other consumer. */

        dev->last_arm_busy_reason[slot] = AMC_BUSY_HARDWARE_STATE;
        result = TCS_BUSY;
        goto done;
    }
    if ((enable || wait) && (!(dev->completed_amc_mask & (uint8_t)bit) ||
                             enable != dev->last_completed_command_mask[slot] ||
                             wait != dev->last_completed_dependency_mask[slot])) {
        dev->last_arm_busy_reason[slot] = AMC_BUSY_INHERITED_MASK;
        result = TCS_BUSY;
        goto done;
    }
    touched = 1;
    dev->completed_amc_mask &= (uint8_t)~bit;
    if (!io->write32(io->ctx, TCS_WAIT(slot), dependency_mask) ||
        !io->write32(io->ctx, TCS_ENABLE(slot), mask))
        goto done;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, TCS_WAIT(slot), &wait) ||
        !io->read32(io->ctx, TCS_ENABLE(slot), &enable) || wait != dependency_mask ||
        enable != mask)
        goto done;
    for (n = 0u; n < cnt; ++n) {
        const uint32_t msgid = commands[n].read ? TCS_MSGID_READ_CMD : TCS_MSGID_WRITE_CMD;
        if (!io->write32(io->ctx, TCS_CMD_MSGID(slot, n), msgid) ||
            !io->write32(io->ctx, TCS_CMD_ADDR(slot, n), commands[n].address) ||
            !io->write32(io->ctx, TCS_CMD_DATA(slot, n), commands[n].read ? 0u : commands[n].data))
            goto done;
    }
    io->barrier(io->ctx);
    /* verify all programmed command words under the shared RPMh mutex before publishing ledger
     * and triggering physical AMC. */

    for (n = 0u; n < cnt; ++n) {
        uint32_t actual_msgid = 0u, actual_address = 0u, actual_data = 0u;
        const uint32_t expected_msgid = commands[n].read ? TCS_MSGID_READ_CMD : TCS_MSGID_WRITE_CMD;
        const uint32_t expected_data = commands[n].read ? 0u : commands[n].data;
        if (!io->read32(io->ctx, TCS_CMD_MSGID(slot, n), &actual_msgid) ||
            !io->read32(io->ctx, TCS_CMD_ADDR(slot, n), &actual_address) ||
            !io->read32(io->ctx, TCS_CMD_DATA(slot, n), &actual_data) ||
            actual_msgid != expected_msgid || actual_address != commands[n].address ||
            actual_data != expected_data)
            goto done;
    }
    /* the shared RPMh owner mutex remains held from first write through ledger publication and
     * the irreversible trigger. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !(dev->owned_amc_mask & (uint8_t)bit) ||
        !dev->completion_handler_bound || !dev->dpc_consumes_completion || ledger->epoch != epoch ||
        !ledger->irq_bound || !(ledger->granted_mask & (uint8_t)bit)) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    /* acquire a final physical eligibility snapshot AFTER the command-image verification but
     * BEFORE the ledger is published. An unexpected controller/IRQ transition at this point
     * must not result in a trigger for an unserviceable request. */

    {
        uint32_t final_irq_enable = 0u, final_irq_status = 0u;
        uint32_t final_status = 0u, final_enable = 0u, final_wait = 0u;
        if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &final_irq_enable) ||
            !io->read32(io->ctx, TCS_IRQ_STATUS, &final_irq_status) ||
            !io->read32(io->ctx, TCS_STATUS(slot), &final_status) ||
            !io->read32(io->ctx, TCS_ENABLE(slot), &final_enable) ||
            !io->read32(io->ctx, TCS_WAIT(slot), &final_wait) || !(final_irq_enable & bit) ||
            (final_irq_status & bit) || !(final_status & TCS_STATUS_IDLE) || final_enable != mask ||
            final_wait != dependency_mask)
            goto done;
    }
    /* publishing the immutable epoch+request+two command masks happens before the trigger
     * edge, so a fast IRQ never precedes its ledger. */

    if (!rsc_completion_submit_batch_with_dependencies(ledger, epoch, slot, req_id, (uint16_t)mask,
                                                       (uint16_t)dependency_mask, 1u))
        goto done;
    dev->deferred_epoch[slot] = epoch;
    dev->deferred_req_id[slot] = req_id;
    dev->deferred_command_cnt[slot] = (uint8_t)cnt;
    dev->deferred_read_mask[slot] = read_mask;
    dev->deferred_active_mask |= (uint8_t)bit;
    control &= ~TCS_CONTROL_AMC_TRIGGER;
    if (!io->write32(io->ctx, TCS_CONTROL(slot), control))
        goto done;
    control |= TCS_CONTROL_AMC_MODE_EN;
    if (!io->write32(io->ctx, TCS_CONTROL(slot), control))
        goto done;
    io->barrier(io->ctx);
    if (!io->write32(io->ctx, TCS_CONTROL(slot), control | TCS_CONTROL_AMC_TRIGGER))
        goto done;
    /* worker must finish. */
    result = TCS_OK;
done:
    if (touched && result != TCS_OK) {
        /* publish quarantine under the same ISR lock. If an owner was revoked and that lock cannot
         * be reacquired, do not mutate fields the ISR may read concurrently: keep the ambiguous
         * controller pinned for external teardown/reconciliation. */

        if (!irq_locked && enter_irq(io))
            irq_locked = 1;
        if (irq_locked) {
            dev->owned_amc_mask &= (uint8_t)~bit;
            dev->completed_amc_mask &= (uint8_t)~bit;
        }
    }
    if (irq_locked)
        leave_irq(io);
    io->unlock(io->ctx);
    return result;
}

rpmh_tcs_result_t rpmh_tcs_finish_deferred(rpmh_tcs_t* dev, rsc_completion_t* ledger,
                                           rsc_irq_latch_t* latched, uint32_t* responses)
{
    rpmh_tcs_result_t result = TCS_IO_FAILURE;
    rpmh_tcs_io_t* io;
    rsc_irq_io_t irq_io;
    rsc_irq_result_t irq_result;
    uint32_t staged[TCS_BATCH_MAX];
    uint32_t control, verified, n, bit;
    uint8_t slot;
    int irq_locked = 0;
    if (!dev || !ledger || !latched || !latched->valid || latched->slot >= 3u)
        return TCS_INVALID;
    slot = latched->slot;
    bit = 1u << slot;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound)
        return TCS_NO_OWNERSHIP;
    if (!dev->dpc_consumes_completion)
        return TCS_COMPLETION_CONSUMER_CONFLICT;
    io = &dev->io;
    if (!deferred_io_complete(io))
        return TCS_INVALID;
    if (!io->lock(io->ctx))
        return TCS_BUSY;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !(dev->owned_amc_mask & (uint8_t)bit) ||
        !dev->completion_handler_bound || !dev->dpc_consumes_completion ||
        !(dev->deferred_active_mask & (uint8_t)bit) || !latched->owner_epoch || !latched->req_id ||
        dev->deferred_epoch[slot] != latched->owner_epoch ||
        dev->deferred_req_id[slot] != latched->req_id ||
        ledger->amc[slot].expected_command_mask != latched->expected_command_mask ||
        ledger->amc[slot].expected_dependency_mask != latched->expected_dependency_mask) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    /* the completion worker already holds the shared RPMh mutex; the WDF owner proof must
     * still succeed before physical ACK/readback. */

    if (!enter_irq(io)) {
        result = TCS_BUSY;
        goto done;
    }
    irq_locked = 1;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !(dev->owned_amc_mask & (uint8_t)bit) ||
        !dev->completion_handler_bound || !dev->dpc_consumes_completion ||
        !(dev->deferred_active_mask & (uint8_t)bit) || ledger->epoch != latched->owner_epoch ||
        !ledger->irq_bound || !(ledger->granted_mask & (uint8_t)bit) ||
        dev->deferred_epoch[slot] != latched->owner_epoch ||
        dev->deferred_req_id[slot] != latched->req_id ||
        ledger->amc[slot].expected_command_mask != latched->expected_command_mask ||
        ledger->amc[slot].expected_dependency_mask != latched->expected_dependency_mask) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    /* read response registers only after command completion. An ISR capture alone can expose stale
     * data even if a later worker completes and acknowledges the command. */

    {
        uint32_t status = 0u, irq = 0u, enable = 0u, wait = 0u, cmd = 0u;
        if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq) ||
            !io->read32(io->ctx, TCS_STATUS(slot), &status) ||
            !io->read32(io->ctx, TCS_ENABLE(slot), &enable) ||
            !io->read32(io->ctx, TCS_WAIT(slot), &wait))
            goto fault;

        if (!(status & TCS_STATUS_IDLE) || enable != latched->expected_command_mask ||
            wait != latched->expected_dependency_mask) {
            result = TCS_BUSY;
            goto done;
        }
        for (n = 0u; n < dev->deferred_command_cnt[slot]; ++n) {
            if (!io->read32(io->ctx, TCS_CMD_STATUS_N(slot, n), &cmd))
                goto fault;
            if (!(cmd & TCS_CMD_COMPLETED)) {
                result = TCS_BUSY;
                goto done;
            }
        }
    }
    /* capture the responses while the checked command is still owned and under its lock. A
     * failed worker ACK must leave caller data unchanged. */

    for (n = 0u; responses && n < dev->deferred_command_cnt[slot]; ++n) {
        uint32_t offset = (dev->deferred_read_mask[slot] & (1u << n)) ? TCS_CMD_RESP_N(slot, n)
                                                                      : TCS_CMD_DATA(slot, n);
        if (!io->read32(io->ctx, offset, &staged[n]))
            goto fault;
    }
    /* retain the same RPMh mutex through W1C, immutable ledger retirement, AMC mode cleanup
     * and response publication. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !(dev->owned_amc_mask & (uint8_t)bit) ||
        !dev->completion_handler_bound || !dev->dpc_consumes_completion ||
        ledger->epoch != latched->owner_epoch || !ledger->irq_bound ||
        !(ledger->granted_mask & (uint8_t)bit) || dev->deferred_req_id[slot] != latched->req_id) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    irq_io.ctx = io->ctx;
    irq_io.read32 = io->read32;
    irq_io.write32 = io->write32;
    irq_io.barrier = io->barrier;
    irq_result = rsc_irq_finish_latched(ledger, &irq_io, latched);
    if (irq_result == RSC_IRQ_COMMANDS_PENDING || irq_result == RSC_IRQ_NO_PENDING) {
        result = TCS_BUSY;
        goto done;
    }
    if (irq_result == RSC_IRQ_ACK_PENDING) {

        result = TCS_ACK_PENDING;
        goto done;
    }
    if (irq_result == RSC_IRQ_IO_ERROR) {
        /* an uncertain W1C outcome is recoverable only through the SAME worker's sticky
         * ack_attempted latch and immutable owner epoch. */

        result = TCS_IO_FAILURE;
        goto done;
    }
    if (irq_result != RSC_IRQ_COMPLETED)
        goto fault;
    /* a failed cleanup must NOT publish responses or permit a new transaction. */

    if (!io->read32(io->ctx, TCS_CONTROL(slot), &control))
        goto fault;
    control &= ~(TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER);
    if (!io->write32(io->ctx, TCS_CONTROL(slot), control))
        goto fault;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, TCS_CONTROL(slot), &verified) || verified != control)
        goto fault;
    dev->last_completed_command_mask[slot] = latched->expected_command_mask;
    dev->last_completed_dependency_mask[slot] = latched->expected_dependency_mask;
    dev->completed_amc_mask |= (uint8_t)bit;
    dev->deferred_active_mask &= (uint8_t)~bit;
    dev->deferred_epoch[slot] = 0u;
    dev->deferred_req_id[slot] = 0u;
    if (responses)
        for (n = 0u; n < dev->deferred_command_cnt[slot]; ++n)
            responses[n] = staged[n];
    result = TCS_OK;
    goto done;
fault:
    /* all physical completion readback occurs under the ISR lock. A failed physical read or
     * control cleanup cannot safely restore the AMC slot to service. */

    if (irq_locked) {
        dev->owned_amc_mask &= (uint8_t)~bit;
        dev->completed_amc_mask &= (uint8_t)~bit;
    }
done:
    if (irq_locked)
        leave_irq(io);
    io->unlock(io->ctx);
    return result;
}

/* the owner's ISR lock must also serialize deferred ticket publication and retirement:
 * FAST_MUTEX cannot meet this contract. A valid WDF vector alone must never produce an AMC
 * completion ticket. */

int rpmh_tcs_read_owned_irq_status(void* src, uint64_t owner_epoch)
{
    const rpmh_irq_src_t* s = (const rpmh_irq_src_t*)src;
    const rpmh_tcs_t* d;
    const rsc_completion_t* l;
    uint32_t status = 0u, active, grant, slot;
    if (!s || s->irq_mask_faulted || !s->dev || !s->ledger || !owner_epoch ||
        s->owner_epoch != owner_epoch || !s->ledger_epoch)
        return -1;
    d = s->dev;
    l = s->ledger;
    if (!d->io.read32 || !d->completion_handler_bound || !d->dpc_consumes_completion ||
        !l->irq_bound || l->epoch != s->ledger_epoch)
        return -1;
    active = d->deferred_active_mask;
    grant = (uint32_t)d->owned_amc_mask & d->external_grant_mask & l->granted_mask & RSC_AMC_MASK;
    /* an in-flight AMC without its exact verified grant is an owner fault, not a spurious
     * interrupt or permission to read foreign TCS slots. */

    if ((active & ~grant) || (active & ~RSC_AMC_MASK))
        return -1;
    for (slot = 0; slot < RSC_AMCS; ++slot) {
        const uint32_t bit = 1u << slot;
        const rsc_amc_track_t* a = &l->amc[slot];
        if (!(active & bit))
            continue;
        if (!d->deferred_req_id[slot] || d->deferred_epoch[slot] != s->ledger_epoch ||
            a->owner_epoch != s->ledger_epoch || a->req_id != d->deferred_req_id[slot] ||
            (a->state != RSC_AMC_IN_FLIGHT && a->state != RSC_AMC_SIGNALED &&
             a->state != RSC_AMC_AWAITING_ACK))
            return -1;
    }
    if (!active)
        return 0;
    if (!d->io.read32(d->io.ctx, TCS_IRQ_STATUS, &status))
        return -1;
    /* a stale signal for an owned-but-not-in-flight AMC must not be accidentally matched to
     * the active request and W1C-acknowledged. */

    if ((status & grant & ~active) != 0u)
        return -1;
    return (status & active) != 0u ? 1 : 0;
}

static uint32_t src_grant(const rpmh_irq_src_t* s, uint64_t epoch)
{
    const rpmh_tcs_t* d;
    const rsc_completion_t* l;
    uint32_t grant;
    if (!s || !epoch || s->owner_epoch != epoch || !s->ledger_epoch || s->irq_mask_faulted ||
        !(d = s->dev) || !(l = s->ledger) || !l->irq_bound || l->epoch != s->ledger_epoch ||
        !d->completion_handler_bound || !d->dpc_consumes_completion || !d->io.read32 ||
        !d->io.write32 || !d->io.barrier)
        return 0u;
    grant = l->granted_mask;
    return grant && !(grant & ~RSC_AMC_MASK) && d->owned_amc_mask == grant &&
                   d->external_grant_mask == grant
               ? grant
               : 0u;
}

int rpmh_tcs_mask_owned_irq(rpmh_irq_src_t* s, uint64_t epoch)
{
    rpmh_tcs_io_t* io;
    uint32_t grant = src_grant(s, epoch), before, after;
    if (!grant || s->irq_mask_active || s->masked_amcs ||
        rpmh_tcs_read_owned_irq_status(s, epoch) != 1)
        return 0;
    io = &s->dev->io;
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &before) || (before & grant) != grant)
        return 0;
    /* a failed posted write can still have reached the controller. Keep its src reservation
     * and lifetime pin until independent owner recovery. */
    s->masked_amcs = grant;
    s->irq_mask_active = 1u;
    if (!io->write32(io->ctx, TCS_IRQ_ENABLE, before & ~grant))
        goto fault;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &after) || after != (before & ~grant) ||
        src_grant(s, epoch) != grant)
        goto fault;
    return 1;
fault:
    s->irq_mask_faulted = 1u;
    return 0;
}

int rpmh_tcs_restore_owned_irq(rpmh_irq_src_t* s, uint64_t epoch)
{
    rpmh_tcs_t* d;
    rpmh_tcs_io_t* io;
    uint32_t grant = src_grant(s, epoch), before, after, pending, slot;
    if (!grant || !s->irq_mask_active || s->masked_amcs != grant)
        return 0;
    d = s->dev;
    io = &d->io;
    if (d->deferred_active_mask || !io->read32(io->ctx, TCS_IRQ_STATUS, &pending) ||
        (pending & grant))
        goto fault;
    for (slot = 0u; slot < RSC_AMCS; ++slot) {
        uint32_t status, control, enable, wait, bit = 1u << slot;
        if (!(grant & bit))
            continue;
        if (s->ledger->amc[slot].state != RSC_AMC_VACANT || d->deferred_req_id[slot] ||
            d->deferred_epoch[slot] || !io->read32(io->ctx, TCS_STATUS(slot), &status) ||
            !io->read32(io->ctx, TCS_CONTROL(slot), &control) ||
            !io->read32(io->ctx, TCS_ENABLE(slot), &enable) ||
            !io->read32(io->ctx, TCS_WAIT(slot), &wait) || !(status & TCS_STATUS_IDLE) ||
            (control & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) ||
            ((enable || wait) &&
             (!(d->completed_amc_mask & bit) || enable != d->last_completed_command_mask[slot] ||
              wait != d->last_completed_dependency_mask[slot])))
            goto fault;
    }
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &before) || (before & grant) ||
        !io->read32(io->ctx, TCS_IRQ_STATUS, &pending) || (pending & grant) ||
        src_grant(s, epoch) != grant)
        goto fault;
    if (!io->write32(io->ctx, TCS_IRQ_ENABLE, before | grant))
        goto fault;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &after) || after != (before | grant) ||
        !io->read32(io->ctx, TCS_IRQ_STATUS, &pending) || (pending & grant) ||
        src_grant(s, epoch) != grant)
        goto fault;
    s->irq_mask_active = 0u;
    s->masked_amcs = 0u;
    return 1;
fault:
    s->irq_mask_faulted = 1u;
    return 0;
}
