#include "rsc_irq_processor.h"

rsc_irq_result_t rsc_irq_process_with_dependencies(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                                   uint32_t owner_epoch, uint8_t slot,
                                                   uint32_t req_id, uint16_t expected_command_mask,
                                                   uint16_t expected_dependency_mask)
{
    uint32_t bit, irq, irq_enable, enable, wait, status, command;
    uint32_t returned_id;
    uint8_t aborted;
    unsigned i;
    rsc_amc_track_t* track;

    if (!ledger || !io || !io->read32 || !io->write32 || !io->barrier || slot >= RSC_AMCS ||
        !req_id || !expected_command_mask ||
        (expected_dependency_mask & (uint16_t)~expected_command_mask))
        return RSC_IRQ_INVALID;
    bit = 1u << slot;
    if (ledger->epoch != owner_epoch || !ledger->irq_bound || !(ledger->granted_mask & bit))
        return RSC_IRQ_NOT_OWNED;
    track = &ledger->amc[slot];
    if (track->req_id != req_id || track->owner_epoch != owner_epoch ||
        track->expected_command_mask != expected_command_mask ||
        track->expected_dependency_mask != expected_dependency_mask ||
        (track->state != RSC_AMC_IN_FLIGHT && track->state != RSC_AMC_AWAITING_ACK))
        return RSC_IRQ_NOT_OWNED;

    /* only the matching AMC status bit may advance its request. An unrelated vector or active AMC
     * cannot authorize clearing this interrupt. */

    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq_enable) ||
        !io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
        return RSC_IRQ_IO_ERROR;
    if (!(irq_enable & bit))
        return RSC_IRQ_NOT_OWNED;
    if (!(irq & bit))
        return RSC_IRQ_NO_PENDING;
    if (track->state == RSC_AMC_IN_FLIGHT &&
        !(rsc_completion_observe_irq(ledger, owner_epoch, irq & bit) & bit))
        return RSC_IRQ_NOT_OWNED;

    if (!io->read32(io->ctx, TCS_STATUS(slot), &status) ||
        !io->read32(io->ctx, TCS_ENABLE(slot), &enable) ||
        !io->read32(io->ctx, TCS_WAIT(slot), &wait))
        return RSC_IRQ_IO_ERROR;
    if (!(status & TCS_STATUS_IDLE) || (enable & 0xffffu) != expected_command_mask ||
        (wait & 0xffffu) != expected_dependency_mask)
        return RSC_IRQ_COMMANDS_PENDING;

    for (i = 0; i < TCS_BATCH_MAX; ++i) {
        if (!(expected_command_mask & (1u << i)))
            continue;
        if (!io->read32(io->ctx, TCS_CMD_STATUS_N(slot, i), &command))
            return RSC_IRQ_IO_ERROR;
        if (!(command & TCS_CMD_COMPLETED))
            return RSC_IRQ_COMMANDS_PENDING;
    }
    /* readback is required; a successful write call alone does not establish cleared IRQ
     * state. */

    io->barrier(io->ctx);
    if (!io->write32(io->ctx, TCS_IRQ_CLEAR, bit))
        return RSC_IRQ_IO_ERROR;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
        return RSC_IRQ_IO_ERROR;
    if (irq & bit)
        return RSC_IRQ_ACK_PENDING;
    /* caller remains responsible for exclusion against concurrent teardown. */

    if (!rsc_completion_confirm_ack(ledger, owner_epoch, slot, req_id, 1u, 1u))
        return RSC_IRQ_NOT_OWNED;
    if (!rsc_completion_take(ledger, owner_epoch, slot, &returned_id, &aborted) || aborted ||
        returned_id != req_id)
        return RSC_IRQ_NOT_OWNED;
    return RSC_IRQ_COMPLETED;
}

/* source-compatible legacy all-WAIT entrypoint; new hardware clients that program
 * per-command dependencies MUST use WithDependencies. */

rsc_irq_result_t rsc_irq_process(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                 uint32_t owner_epoch, uint8_t slot, uint32_t req_id,
                                 uint16_t expected_command_mask)
{
    return rsc_irq_process_with_dependencies(ledger, io, owner_epoch, slot, req_id,
                                             expected_command_mask, expected_command_mask);
}
