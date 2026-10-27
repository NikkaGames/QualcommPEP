#include "oem_tcs_async.h"
#include <string.h>

/* every API below is called under the SAME verified controller/lock. The bridge is not
 * wired into production, and an observed IRQ37 or firmware HLOS DRV2 assignment is NOT
 * proof of rights to these registers. */

static int admitted(const oem_tcs_async_t* a, uint8_t slot)
{
    return a && !a->closed && !a->faulted && a->completion_handler_bound &&
           slot < OEM_TCS_ASYNC_SLOTS && (a->external_grant_mask & (uint8_t)~7u) == 0u &&
           (a->external_grant_mask & (1u << slot)) != 0u;
}

int oem_tcs_async_init(oem_tcs_async_t* a, const oem_tcs_async_ops_t* ops)
{
    if (!a || !ops || !ops->read32 || !ops->write32 || !ops->barrier || !ops->resolve_write)
        return 0;
    memset(a, 0, sizeof(*a));
    a->ops = *ops;
    return 1;
}

int oem_tcs_async_submit(void* opaque, uint8_t slot, uint64_t token, uint32_t req_id,
                         uint32_t res_key, uint8_t is_read)
{
    oem_tcs_async_t* a = opaque;
    const oem_tcs_async_ops_t* o;
    uint32_t addr = 0, data = 0, irq = 0, status = 0, ctrl = 0, en = 0, wait = 0, cmd = 0;
    uint32_t bit;
    if (!admitted(a, slot) || !token || !req_id || !res_key || is_read != 0 ||
        a->state[slot] != OEM_TCS_FREE)
        return 0;
    o = &a->ops;
    bit = 1u << slot;

    if (o->resolve_write(o->ctx, token, req_id, res_key, &addr, &data) != 1 || !addr || (addr & 3u))
        return 0;
    if (!o->read32(o->ctx, TCS_IRQ_STATUS, &irq) || !o->read32(o->ctx, TCS_STATUS(slot), &status) ||
        !o->read32(o->ctx, TCS_CONTROL(slot), &ctrl) || !o->read32(o->ctx, TCS_ENABLE(slot), &en) ||
        !o->read32(o->ctx, TCS_WAIT(slot), &wait) || !o->read32(o->ctx, TCS_IRQ_ENABLE, &cmd)) {
        a->faulted = 1;
        return 0; /* failed MMIO read: controller state unknown. */
    }
    if ((irq & bit) || !(status & TCS_STATUS_IDLE) ||
        (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || en || wait || !(cmd & bit))
        return 0;
    /* never claim a failed write left the hardware untouched. */

    a->token[slot] = token;
    a->state[slot] = OEM_TCS_SUBMISSION_UNCERTAIN;
    if (!o->write32(o->ctx, TCS_WAIT(slot), 1u) || !o->write32(o->ctx, TCS_ENABLE(slot), 1u) ||
        !o->write32(o->ctx, TCS_CMD_MSGID(slot, 0), TCS_MSGID_WRITE_CMD) ||
        !o->write32(o->ctx, TCS_CMD_ADDR(slot, 0), addr) ||
        !o->write32(o->ctx, TCS_CMD_DATA(slot, 0), data) ||
        !o->write32(o->ctx, TCS_CONTROL(slot), ctrl & ~TCS_CONTROL_AMC_TRIGGER) ||
        !o->write32(o->ctx, TCS_CONTROL(slot),
                    (ctrl & ~TCS_CONTROL_AMC_TRIGGER) | TCS_CONTROL_AMC_MODE_EN)) {
        a->faulted = 1;
        return 0;
    }
    o->barrier(o->ctx);
    if (!o->write32(o->ctx, TCS_CONTROL(slot),
                    (ctrl & ~TCS_CONTROL_AMC_TRIGGER) | TCS_CONTROL_AMC_MODE_EN |
                        TCS_CONTROL_AMC_TRIGGER)) {
        a->faulted = 1;
        return 0;
    }
    o->barrier(o->ctx);
    a->state[slot] = OEM_TCS_INFLIGHT;
    ++a->submitted;
    return 1;
}

int oem_tcs_async_validate_finished(oem_tcs_async_t* a, uint8_t slot, uint64_t token)
{
    uint32_t irq = 0, status = 0, command_status = 0;
    if (!a || a->faulted || slot >= OEM_TCS_ASYNC_SLOTS || !a->completion_handler_bound ||
        !(a->external_grant_mask & (1u << slot)) || a->state[slot] != OEM_TCS_INFLIGHT || !token ||
        a->token[slot] != token)
        return 0;
    if (!a->ops.read32(a->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        !a->ops.read32(a->ops.ctx, TCS_STATUS(slot), &status) ||
        !a->ops.read32(a->ops.ctx, TCS_CMD_STATUS_N(slot, 0), &command_status)) {
        a->faulted = 1;
        return 0;
    }
    if (!(irq & (1u << slot)) || !(status & TCS_STATUS_IDLE) ||
        !(command_status & TCS_CMD_COMPLETED)) {
        a->faulted = 1;
        return 0;
    }
    return 1;
}

int oem_tcs_async_retire(oem_tcs_async_t* a, uint8_t slot, uint64_t token)
{
    uint32_t irq = 0, ctrl = 0, enable = 0, wait = 0;
    if (!a || a->faulted || slot >= OEM_TCS_ASYNC_SLOTS || !a->completion_handler_bound ||
        !(a->external_grant_mask & (1u << slot)) || !token || a->token[slot] != token ||
        a->state[slot] != OEM_TCS_INFLIGHT)
        return 0;
    if (!a->ops.read32(a->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        !a->ops.read32(a->ops.ctx, TCS_CONTROL(slot), &ctrl)) {
        a->faulted = 1;
        return 0;
    }
    if (irq & (1u << slot))
        return 0; /* w1C MUST precede retirement. */
    a->state[slot] = OEM_TCS_RETIRE_UNCERTAIN;
    if (!a->ops.write32(a->ops.ctx, TCS_CONTROL(slot),
                        ctrl & ~(TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) ||
        !a->ops.write32(a->ops.ctx, TCS_ENABLE(slot), 0u) ||
        !a->ops.write32(a->ops.ctx, TCS_WAIT(slot), 0u)) {
        a->faulted = 1;
        return 0;
    }
    a->ops.barrier(a->ops.ctx);
    if (!a->ops.read32(a->ops.ctx, TCS_CONTROL(slot), &ctrl) ||
        !a->ops.read32(a->ops.ctx, TCS_ENABLE(slot), &enable) ||
        !a->ops.read32(a->ops.ctx, TCS_WAIT(slot), &wait) ||
        !a->ops.read32(a->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || enable || wait ||
        (irq & (1u << slot))) {
        a->faulted = 1;
        return 0;
    }
    a->token[slot] = 0;
    a->state[slot] = OEM_TCS_FREE;
    ++a->retired;
    return 1;
}

void oem_tcs_async_close(oem_tcs_async_t* a)
{
    if (a)
        a->closed = 1;
}

int oem_tcs_async_can_release(const oem_tcs_async_t* a)
{
    uint8_t slot;
    if (!a || !a->closed || a->faulted)
        return 0;
    for (slot = 0; slot < OEM_TCS_ASYNC_SLOTS; ++slot)
        if (a->state[slot] != OEM_TCS_FREE || a->token[slot])
            return 0;
    return 1;
}
