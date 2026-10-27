#include "rsc_irq_latched.h"

static int has_owner(const rsc_completion_t* ledger, uint32_t epoch, uint8_t slot, uint32_t req_id)
{
    const rsc_amc_track_t* t;
    if (!ledger || !epoch || !req_id || slot >= RSC_AMCS || ledger->epoch != epoch ||
        !ledger->irq_bound || !(ledger->granted_mask & (1u << slot)))
        return 0;
    t = &ledger->amc[slot];
    return t->req_id == req_id && t->owner_epoch == epoch;
}

static rsc_irq_result_t capture_pending(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                        uint32_t epoch, uint8_t slot, uint32_t req_id,
                                        uint16_t command_mask, rsc_irq_latch_t* latched,
                                        uint32_t src_masked)
{
    uint32_t irq_enable, irq_status, bit;
    uint8_t transitioned;
    if (!ledger || !io || !io->read32 || !latched || latched->valid || !command_mask ||
        slot >= RSC_AMCS || !req_id)
        return RSC_IRQ_INVALID;
    if (!has_owner(ledger, epoch, slot, req_id) || ledger->amc[slot].state != RSC_AMC_IN_FLIGHT ||
        ledger->amc[slot].expected_command_mask != command_mask)
        return RSC_IRQ_NOT_OWNED;
    bit = 1u << slot;
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq_enable) ||
        !io->read32(io->ctx, TCS_IRQ_STATUS, &irq_status))
        return RSC_IRQ_IO_ERROR;
    if (src_masked & bit) {
        if (irq_enable & bit)
            return RSC_IRQ_NOT_OWNED;
    } else if (!(irq_enable & bit))
        return RSC_IRQ_NOT_OWNED;
    if (!(irq_status & bit))
        return RSC_IRQ_NO_PENDING;
    transitioned = rsc_completion_observe_irq(ledger, epoch, bit);
    if (!(transitioned & bit))
        return RSC_IRQ_NOT_OWNED;
    /* capture never acknowledges the hardware; only exact command completion and the same
     * immutable ACK ticket can do that. */

    if (!has_owner(ledger, epoch, slot, req_id) || ledger->amc[slot].state != RSC_AMC_AWAITING_ACK)
        return RSC_IRQ_NOT_OWNED;
    latched->owner_epoch = epoch;
    latched->req_id = req_id;
    latched->slot = slot;
    latched->expected_command_mask = command_mask;
    latched->expected_dependency_mask = ledger->amc[slot].expected_dependency_mask;
    latched->ack_attempted = 0u;
    latched->valid = 1u;
    return RSC_IRQ_DEFERRED;
}

rsc_irq_result_t rsc_irq_capture_pending(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                         uint32_t epoch, uint8_t slot, uint32_t req_id,
                                         uint16_t command_mask, rsc_irq_latch_t* latched)
{
    return capture_pending(ledger, io, epoch, slot, req_id, command_mask, latched, 0u);
}

rsc_irq_result_t rsc_irq_capture_owned_src(const rpmh_irq_src_t* src, uint64_t owner_epoch,
                                           uint8_t slot, uint32_t req_id, uint16_t command_mask,
                                           rsc_irq_latch_t* latched)
{
    const rpmh_tcs_t* d;
    rsc_irq_io_t io;
    if (!src || !owner_epoch || src->owner_epoch != owner_epoch || !src->irq_mask_active ||
        src->irq_mask_faulted || !(d = src->dev) || !src->ledger || !src->ledger_epoch ||
        src->ledger_epoch != src->ledger->epoch || !src->ledger->irq_bound ||
        !d->completion_handler_bound || !d->dpc_consumes_completion ||
        src->masked_amcs != src->ledger->granted_mask || src->masked_amcs != d->owned_amc_mask ||
        src->masked_amcs != d->external_grant_mask || slot >= RSC_AMCS ||
        !(src->masked_amcs & (1u << slot)) || (d->deferred_active_mask & ~src->masked_amcs) ||
        !(d->deferred_active_mask & (1u << slot)) || d->deferred_req_id[slot] != req_id ||
        d->deferred_epoch[slot] != src->ledger_epoch)
        return RSC_IRQ_NOT_OWNED;
    io.ctx = d->io.ctx;
    io.read32 = d->io.read32;
    io.write32 = d->io.write32;
    io.barrier = d->io.barrier;
    return capture_pending(src->ledger, &io, src->ledger_epoch, slot, req_id, command_mask, latched,
                           src->masked_amcs);
}

rsc_irq_result_t rsc_irq_capture_ack(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                     uint32_t epoch, uint8_t slot, uint32_t req_id,
                                     uint16_t command_mask, rsc_irq_latch_t* latched)
{
    return rsc_irq_capture_pending(ledger, io, epoch, slot, req_id, command_mask, latched);
}

rsc_irq_result_t rsc_irq_finish_latched(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                        rsc_irq_latch_t* latched)
{
    uint32_t mask, enabled, waiting, status, irq, cmd;
    uint32_t returned_id;
    uint8_t aborted;
    unsigned n;
    if (!ledger || !io || !io->read32 || !io->write32 || !io->barrier || !latched ||
        !latched->valid || !latched->expected_command_mask || latched->slot >= RSC_AMCS)
        return RSC_IRQ_INVALID;
    if (!has_owner(ledger, latched->owner_epoch, latched->slot, latched->req_id) ||
        ledger->amc[latched->slot].state != RSC_AMC_AWAITING_ACK ||
        ledger->amc[latched->slot].expected_command_mask != latched->expected_command_mask ||
        ledger->amc[latched->slot].expected_dependency_mask != latched->expected_dependency_mask)
        return RSC_IRQ_NOT_OWNED;
    mask = latched->expected_command_mask;
    if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq) ||
        !io->read32(io->ctx, TCS_STATUS(latched->slot), &status) ||
        !io->read32(io->ctx, TCS_ENABLE(latched->slot), &enabled) ||
        !io->read32(io->ctx, TCS_WAIT(latched->slot), &waiting))
        return RSC_IRQ_IO_ERROR;

    if (!(status & TCS_STATUS_IDLE) || (enabled & 0xffffu) != mask ||
        (waiting & 0xffffu) != latched->expected_dependency_mask)
        return RSC_IRQ_COMMANDS_PENDING;
    for (n = 0; n < TCS_BATCH_MAX; ++n) {
        if (!(mask & (1u << n)))
            continue;
        if (!io->read32(io->ctx, TCS_CMD_STATUS_N(latched->slot, n), &cmd))
            return RSC_IRQ_IO_ERROR;
        if (!(cmd & TCS_CMD_COMPLETED))
            return RSC_IRQ_COMMANDS_PENDING;
    }
    /* retain the exact request until all commands and physical W1C readback are verified,
     * regardless of the callback that drains it. */

    if (irq & (1u << latched->slot)) {
        latched->ack_attempted = 1u;
        io->barrier(io->ctx);
        if (!io->write32(io->ctx, TCS_IRQ_CLEAR, 1u << latched->slot))
            return RSC_IRQ_IO_ERROR;
        io->barrier(io->ctx);
        if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
            return RSC_IRQ_IO_ERROR;
        if (irq & (1u << latched->slot))
            return RSC_IRQ_ACK_PENDING;
    } else if (!latched->ack_attempted) {
        /* without our W1C attempt cannot authenticate an ACK. */

        return RSC_IRQ_NOT_OWNED;
    }
    /* caller externally holds owner/ISR lock across both checks and ledger retirement. */

    if (!rsc_completion_confirm_ack(ledger, latched->owner_epoch, latched->slot, latched->req_id,
                                    1u, 1u) ||
        !rsc_completion_take(ledger, latched->owner_epoch, latched->slot, &returned_id, &aborted) ||
        aborted || returned_id != latched->req_id)
        return RSC_IRQ_NOT_OWNED;
    latched->valid = 0u; /* token cannot be replayed against another DPC. */
    return RSC_IRQ_COMPLETED;
}
