#include "rpmh_tcs.h"

/* signed SC8480 TCS init at RVA 0x23E7ACâ€“0x23E7F0, independently agreeing with
 * qcpep8280.sys.c tcs_init(), VA 0x1401471D0. v2 is decoded as (config >> 12) & 0x3F for
 * RSC_DRV_HLOS, and v7 as config >> 27. Both are checked against the selected driver
 * configuration before any TCS is programmed. */

int rpmh_tcs_check_hlos_geometry(uint32_t parent_child_config, uint8_t required_tcs,
                                 uint8_t required_commands, rsc_hlos_geometry_t* result)
{
    uint32_t tcs = (parent_child_config >> 12) & 0x3Fu;
    uint32_t commands = (parent_child_config >> 27) & 0x1Fu;
    if (!result)
        return 0;
    result->tcs_cnt = 0u;
    result->commands_per_tcs = 0u;
    if (!required_tcs || !required_commands || required_commands > TCS_BATCH_MAX ||
        tcs < required_tcs || commands < required_commands)
        return 0;
    result->tcs_cnt = (uint8_t)tcs;
    result->commands_per_tcs = (uint8_t)commands;
    return 1;
}

/* every command and address is checked before acquiring the RSC lock. The APSS RSC uses a
 * 0x18-byte stride for CMDn and a 16-bit per-TCS CMD_WAIT_FOR_CMPL / CMD_ENABLE mask (16
 * commands, reads). */

static int io_complete(const rpmh_tcs_io_t* io)
{
    return io->read32 && io->write32 && io->barrier && io->lock && io->unlock;
}

rpmh_tcs_result_t rpmh_tcs_prepare(rpmh_tcs_t* dev, uint8_t tcs)
{
    rpmh_tcs_result_t result = TCS_IO_FAILURE;
    rpmh_tcs_io_t* io;
    uint32_t status, irq, bit;
    int attempted_enable = 0;
    if (!dev || tcs >= 3u)
        return TCS_INVALID;
    /* a free AMC, PnP MMIO desc or UEFI cleanup is not ownership. */

    bit = 1u << tcs;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound)
        return TCS_NO_OWNERSHIP;
    io = &dev->io;
    if (!io_complete(io))
        return TCS_INVALID;
    if (!io->lock(io->ctx))
        return TCS_BUSY;
    /* the external grant may disappear while waiting for the transport lock. PnP teardown and
     * the provider handoff must use the same lock. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    /* never clear or disable that IRQ as part of a later failed prepare. A leftover IRQ-enable
     * cleanup obligation must be settled by restore. */

    if ((dev->owned_amc_mask | dev->irq_enabled_by_us) & (uint8_t)bit) {
        result = TCS_BUSY;
        goto done;
    }
    if (!io->read32(io->ctx, TCS_STATUS(tcs), &status))
        goto done;
    if (!(status & TCS_STATUS_IDLE)) {
        result = TCS_BUSY;
        goto done;
    }
    /* the slot must also be unprogrammed: a non-zero command-enable or wait-for-completion
     * mask means another agent is using this AMC, and claiming it would corrupt that
     * transaction. */

    {
        uint32_t enable = 0, wait = 0;
        if (!io->read32(io->ctx, TCS_ENABLE(tcs), &enable) ||
            !io->read32(io->ctx, TCS_WAIT(tcs), &wait))
            goto done;
        if (enable || wait) {
            result = TCS_BUSY;
            goto done;
        }
    }
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq))
        goto done;
    /* the firmware can leave an AMC IRQ-enabled even while its command and pending-status
     * registers are empty. Only the d0 native owner may record a stable baseline in
     * pre_enabled_irq_mask, and that record alone grants NOTHING. */

    if (dev->pre_enabled_irq_mask & (uint8_t)bit) {
        if (!(irq & bit)) {
            result = TCS_BUSY;
            goto done;
        }

    } else {
        if (irq & bit) {
            result = TCS_BUSY;
            goto done;
        }
        /* record cleanup debt BEFORE a potentially partial physical write. */
        attempted_enable = 1;
        dev->irq_enabled_by_us |= (uint8_t)bit;
        if (!io->write32(io->ctx, TCS_IRQ_ENABLE, irq | bit))
            goto done;
        io->barrier(io->ctx);
        if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq))
            goto done;
        if (!(irq & bit)) {
            result = TCS_IRQ_NOT_ENABLED;
            goto done;
        }
    }
    /* a stale completion from the prior owner must not be mistaken for the next request. */

    if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
        goto done;
    if (irq & bit) {
        /* it may belong to firmware or a prior driver epoch. */

        result = TCS_ACK_PENDING;
        goto done;
    }
    dev->completed_amc_mask &= (uint8_t)~bit;
    dev->last_completed_command_mask[tcs] = 0u;
    dev->last_completed_dependency_mask[tcs] = 0u;
    dev->owned_amc_mask |= (uint8_t)bit;
    result = TCS_OK;
done:

    if (result != TCS_OK && attempted_enable) {
        uint32_t enabled = 0;
        if (io->read32(io->ctx, TCS_IRQ_ENABLE, &enabled) &&
            io->write32(io->ctx, TCS_IRQ_ENABLE, enabled & ~(uint32_t)bit)) {
            io->barrier(io->ctx);
            if (io->read32(io->ctx, TCS_IRQ_ENABLE, &enabled) && !(enabled & bit))
                dev->irq_enabled_by_us &= (uint8_t)~bit;
        }
    }
    io->unlock(io->ctx);
    return result;
}

uint8_t rpmh_tcs_restore(rpmh_tcs_t* dev)
{
    rpmh_tcs_io_t* io;
    uint32_t irq = 0;
    uint8_t mine, cleanup_authorized;
    if (!dev)
        return 0x07u; /* invalid owner: fail closed. */
    io = &dev->io;
    /* a completed lock() acquisition serializes revocation against both prepare and a
     * synchronous transaction, including the W1C path. Never take a snapshot of
     * irq_enabled_by_us before taking that lock: a pending prepare may still enable an
     * interrupt before restore gets control. */

    if (!io_complete(io) || !io->lock(io->ctx)) {
        /* emergency software fail-closed fallback. No MMIO is safe here. */

        dev->owned_amc_mask = 0u;
        dev->external_grant_mask = 0u;
        dev->completion_handler_bound = 0u;
        dev->dpc_consumes_completion = 0u;
        dev->completed_amc_mask = 0u;
        dev->last_completed_command_mask[0] = 0u;
        dev->last_completed_dependency_mask[0] = 0u;
        dev->last_completed_command_mask[1] = 0u;
        dev->last_completed_dependency_mask[1] = 0u;
        dev->last_completed_command_mask[2] = 0u;
        dev->last_completed_dependency_mask[2] = 0u;
        /* a lock refusal may mean another thread is actively using this mapping. Even with zero
         * IRQ-enable debt, owners_destroy MUST retain the mapping until an independently
         * synchronized teardown. */

        return (uint8_t)(dev->irq_enabled_by_us | TCS_CLEANUP_UNSYNCHRONIZED);
    }
    mine = dev->irq_enabled_by_us;
    /* only service bits covered by the independently renewed, live grant, sampled under the
     * same owner lock as software revocation. All other bits survive for a future authorized
     * cleanup; never clear a foreign interrupt's bit on the basis of a stale cleanup record
     * alone. */

    cleanup_authorized = (uint8_t)(mine & dev->external_grant_mask);
    dev->owned_amc_mask = 0u;
    dev->external_grant_mask = 0u;
    dev->completion_handler_bound = 0u;
    dev->dpc_consumes_completion = 0u;
    /* an unretired deferred ticket is an actual hardware cleanup debt even when IRQ_ENABLE was
     * already set by firmware (irq_enabled_by_us == 0). Do not unmap RSC or free lock storage
     * just because no IRQ bit needs disable; the deferred worker has not ACKed or retired this
     * TCS. */

    if (dev->deferred_active_mask) {
        uint8_t pending = dev->irq_enabled_by_us;
        io->unlock(io->ctx);
        return (uint8_t)(pending | TCS_CLEANUP_UNSYNCHRONIZED);
    }
    dev->completed_amc_mask = 0u;
    dev->pre_enabled_irq_mask = 0u; /* d0 baseline expires with this owner. */
    dev->deferred_active_mask = 0u;
    dev->deferred_epoch[0] = dev->deferred_epoch[1] = dev->deferred_epoch[2] = 0u;
    dev->deferred_req_id[0] = dev->deferred_req_id[1] = dev->deferred_req_id[2] = 0u;
    dev->deferred_read_mask[0] = dev->deferred_read_mask[1] = dev->deferred_read_mask[2] = 0u;
    dev->deferred_command_cnt[0] = dev->deferred_command_cnt[1] = dev->deferred_command_cnt[2] = 0u;
    dev->last_completed_command_mask[0] = 0u;
    dev->last_completed_dependency_mask[0] = 0u;
    dev->last_completed_command_mask[1] = 0u;
    dev->last_completed_dependency_mask[1] = 0u;
    dev->last_completed_command_mask[2] = 0u;
    dev->last_completed_dependency_mask[2] = 0u;
    if (cleanup_authorized && io->read32(io->ctx, TCS_IRQ_ENABLE, &irq) &&
        io->write32(io->ctx, TCS_IRQ_ENABLE, irq & ~(uint32_t)cleanup_authorized)) {
        io->barrier(io->ctx);
        if (io->read32(io->ctx, TCS_IRQ_ENABLE, &irq) && !(irq & cleanup_authorized))
            dev->irq_enabled_by_us &= (uint8_t)~cleanup_authorized;
    }
    mine = dev->irq_enabled_by_us;
    io->unlock(io->ctx);
    return mine;
}

rpmh_tcs_result_t rpmh_tcs_issue_batch_with_dependencies(rpmh_tcs_t* dev, uint8_t tcs,
                                                         const tcs_command_t* commands,
                                                         uint32_t cnt, uint32_t dependency_mask,
                                                         uint32_t* responses)
{
    rpmh_tcs_result_t result = TCS_IO_FAILURE;
    rpmh_tcs_io_t* io;
    uint32_t status, command_status, control, irq, i, n, bit, mask;
    /* never expose a partly completed batch response to its caller. */
    uint32_t staged_responses[TCS_BATCH_MAX];
    int touched = 0;
    if (!dev || !commands || cnt == 0u || cnt > TCS_BATCH_MAX || tcs >= 3u || !dev->poll_limit)
        return TCS_INVALID;
    /* no MMIO, lock acquisition, or caller-response modification is allowed for malformed
     * descriptors, including a dangling dependency bit. */

    if (dependency_mask & ~((cnt >= 16u) ? 0xFFFFu : ((1u << cnt) - 1u)))
        return TCS_INVALID;
    /* reject noncanonical descriptors before the ownership gate, transport lock, or any
     * controller callback. */

    for (n = 0u; n < cnt; ++n)
        if ((commands[n].address & ~0x7ffffu) || commands[n].read > 1u ||
            (commands[n].read && commands[n].data != 0u))
            return TCS_INVALID;
    /* external privilege must still be valid on every transaction. */
    bit = 1u << tcs;
    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound)
        return TCS_NO_OWNERSHIP;
    /* the polled path and the ISR/DPC path must not consume the same W1C completion status.
     * Check before touching MMIO, then recheck under the owner-provided lock to cover a
     * handoff race. */

    if (dev->dpc_consumes_completion)
        return TCS_COMPLETION_CONSUMER_CONFLICT;
    io = &dev->io;
    if (!io_complete(io))
        return TCS_INVALID;
    if (!io->lock(io->ctx))
        return TCS_BUSY;
    if (dev->dpc_consumes_completion) {
        result = TCS_COMPLETION_CONSUMER_CONFLICT;
        goto done;
    }
    /* a caller might have passed the pre-lock grant/IRQ checks immediately before a concurrent
     * owner revocation. Recheck at the same admission point as the completion-consumer mode;
     * zero MMIO if it was revoked. */

    if (!(dev->external_grant_mask & (uint8_t)bit) || !dev->completion_handler_bound) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }
    mask = (cnt >= 16u) ? 0xFFFFu : ((1u << cnt) - 1u);
    if (!(dev->owned_amc_mask & (uint8_t)bit)) {
        result = TCS_NO_OWNERSHIP;
        goto done;
    }

    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq))
        goto done;
    if (!(irq & bit)) {
        result = TCS_IRQ_NOT_ENABLED;
        goto done;
    }
    if (!io->read32(io->ctx, TCS_STATUS(tcs), &status) ||
        !io->read32(io->ctx, TCS_CONTROL(tcs), &control))
        goto done;
    if (!(status & TCS_STATUS_IDLE)) {
        result = TCS_BUSY;
        goto done;
    }
    /* a foreign, unrecognized or partially programmed slot must never be overwritten. This is
     * not a hardware-ownership grant and does not bypass it. */

    {
        uint32_t enable = 0u, wait = 0u;
        if (!io->read32(io->ctx, TCS_ENABLE(tcs), &enable) ||
            !io->read32(io->ctx, TCS_WAIT(tcs), &wait))
            goto done;
        if (enable || wait) {
            if (!(dev->completed_amc_mask & (uint8_t)bit) ||
                enable != dev->last_completed_command_mask[tcs] ||
                wait != dev->last_completed_dependency_mask[tcs]) {
                result = TCS_BUSY;
                goto done;
            }
        }
    }
    if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
        goto done;
    if (irq & bit) {

        touched = 1;
        io->barrier(io->ctx);
        if (!io->write32(io->ctx, TCS_IRQ_CLEAR, bit))
            goto done;
        io->barrier(io->ctx);
        if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
            goto done;
        if (irq & bit) {
            result = TCS_ACK_PENDING;
            goto done;
        }
    }
    /* once any TCS configuration write is attempted, an I/O error leaves command state
     * uncertain and the slot cannot be reused automatically. */

    touched = 1;
    if (!io->write32(io->ctx, TCS_WAIT(tcs), dependency_mask) ||
        !io->write32(io->ctx, TCS_ENABLE(tcs), mask))
        goto done;

    {
        uint32_t got_wait = 0, got_enable = 0;
        io->barrier(io->ctx);
        if (!io->read32(io->ctx, TCS_WAIT(tcs), &got_wait) ||
            !io->read32(io->ctx, TCS_ENABLE(tcs), &got_enable) || got_wait != dependency_mask ||
            got_enable != mask)
            goto done;
    }
    for (n = 0u; n < cnt; ++n) {
        uint32_t msgid = commands[n].read ? TCS_MSGID_READ_CMD : TCS_MSGID_WRITE_CMD;
        if (!io->write32(io->ctx, TCS_CMD_MSGID(tcs, n), msgid) ||
            !io->write32(io->ctx, TCS_CMD_ADDR(tcs, n), commands[n].address) ||
            !io->write32(io->ctx, TCS_CMD_DATA(tcs, n), commands[n].read ? 0u : commands[n].data))
            goto done;
    }
    io->barrier(io->ctx);

    control &= ~(TCS_CONTROL_AMC_TRIGGER);
    if (!io->write32(io->ctx, TCS_CONTROL(tcs), control))
        goto done;
    control |= TCS_CONTROL_AMC_MODE_EN;
    if (!io->write32(io->ctx, TCS_CONTROL(tcs), control))
        goto done;
    io->barrier(io->ctx);
    if (!io->write32(io->ctx, TCS_CONTROL(tcs), control | TCS_CONTROL_AMC_TRIGGER))
        goto done;
    for (i = 0u; i < dev->poll_limit; ++i) {
        if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
            goto done;
        if (irq & bit) {
            if (!io->read32(io->ctx, TCS_STATUS(tcs), &status))
                goto done;
            if (status & TCS_STATUS_IDLE) {
                for (n = 0u; n < cnt; ++n) {
                    if (!io->read32(io->ctx, TCS_CMD_STATUS_N(tcs, n), &command_status))
                        goto done;
                    if (!(command_status & TCS_CMD_COMPLETED)) {
                        result = TCS_COMMAND_INCOMPLETE;
                        goto done;
                    }
                    if (responses) {
                        uint32_t off =
                            commands[n].read ? TCS_CMD_RESP_N(tcs, n) : TCS_CMD_DATA(tcs, n);
                        if (!io->read32(io->ctx, off, &staged_responses[n]))
                            goto done;
                    }
                }

                io->barrier(io->ctx);
                if (!io->write32(io->ctx, TCS_IRQ_CLEAR, bit))
                    goto done;
                io->barrier(io->ctx);
                if (!io->read32(io->ctx, TCS_IRQ_STATUS, &irq))
                    goto done;
                if (irq & bit) {
                    result = TCS_ACK_PENDING;
                    goto done;
                }

                control &= ~(TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER);
                if (!io->write32(io->ctx, TCS_CONTROL(tcs), control))
                    goto done;
                /* the owner grant, IRQ contract and idle status are checked again next time. */

                dev->last_completed_command_mask[tcs] = (uint16_t)mask;
                dev->last_completed_dependency_mask[tcs] = (uint16_t)dependency_mask;
                dev->completed_amc_mask |= (uint8_t)bit;
                /* commit output only after command completion, verified W1C ACK and successful AMC
                 * control cleanup. */

                if (responses)
                    for (n = 0u; n < cnt; ++n)
                        responses[n] = staged_responses[n];
                result = TCS_OK;
                goto done;
            }
        }
        if (io->spin_hint)
            io->spin_hint(io->ctx);
    }
    result = TCS_TIMEOUT;
done:
    if (touched && result != TCS_OK) {
        dev->owned_amc_mask &= (uint8_t)~bit;
        dev->completed_amc_mask &= (uint8_t)~bit;
        dev->last_completed_command_mask[tcs] = 0u;
        dev->last_completed_dependency_mask[tcs] = 0u;
    }
    io->unlock(io->ctx);
    return result;
}

/* dependency masks must be supplied through the explicit entry point. */

rpmh_tcs_result_t rpmh_tcs_issue_batch_ex(rpmh_tcs_t* dev, uint8_t tcs,
                                          const tcs_command_t* commands, uint32_t cnt,
                                          uint32_t* responses)
{
    return rpmh_tcs_issue_batch_with_dependencies(dev, tcs, commands, cnt, 0u, responses);
}

rpmh_tcs_result_t rpmh_tcs_issue_batch(rpmh_tcs_t* dev, uint8_t tcs, const tcs_command_t* commands,
                                       uint32_t cnt)
{
    return rpmh_tcs_issue_batch_ex(dev, tcs, commands, cnt, 0);
}

rpmh_tcs_result_t rpmh_tcs_issue(rpmh_tcs_t* dev, uint8_t tcs, uint32_t address, uint32_t data,
                                 uint8_t response_required)
{
    tcs_command_t command;
    if (response_required != 1u)
        return TCS_INVALID;
    command.address = address;
    command.data = data;
    command.read = 0u;
    return rpmh_tcs_issue_batch_with_dependencies(dev, tcs, &command, 1u, 0u, 0);
}

rpmh_tcs_result_t rpmh_tcs_read(rpmh_tcs_t* dev, uint8_t tcs, uint32_t address, uint32_t* data)
{
    tcs_command_t command;
    uint32_t response = 0;
    rpmh_tcs_result_t r;
    if (!data)
        return TCS_INVALID;
    command.address = address;
    command.data = 0u;
    command.read = 1u;
    r = rpmh_tcs_issue_batch_with_dependencies(dev, tcs, &command, 1u, 0u, &response);
    if (r == TCS_OK)
        *data = response;
    return r;
}
