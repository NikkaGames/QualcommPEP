#include "oem_tcs_batch_async.h"
#include <string.h>

/* all entry points MUST be externally serialized, including callbacks. */
static int admitted(const oem_tcs_batch_t* b, uint8_t slot)
{
    return b && !b->closed && !b->faulted && b->completion_handler_bound &&
           slot < OEM_TCS_BATCH_SLOTS && !(b->external_grant_mask & (uint8_t)~7u) &&
           (b->external_grant_mask & (1u << slot));
}

int oem_tcs_batch_init(oem_tcs_batch_t* b, const oem_tcs_batch_ops_t* o)
{
    if (!b || !o || !o->read32 || !o->write32 || !o->barrier || !o->resolve_batch)
        return 0;
    memset(b, 0, sizeof(*b));
    b->ops = *o;
    return 1;
}

int oem_tcs_batch_submit(void* opaque, uint8_t slot, uint64_t token, uint32_t req_id,
                         uint32_t res_key, uint8_t is_read)
{
    oem_tcs_batch_t* b = opaque;
    oem_tcs_batch_slot_t* s;
    const oem_tcs_batch_ops_t* o;
    tcs_command_t commands[OEM_TCS_BATCH_COMMANDS];
    uint32_t cnt = OEM_TCS_BATCH_COMMANDS;
    uint32_t i, reads = 0, irq = 0, status = 0, ctrl = 0, en = 0, wait = 0, irq_enable = 0;
    uint32_t bit, mask;
    if (!admitted(b, slot) || !token || !req_id || !res_key || is_read > 1 ||
        b->slots[slot].state != OEM_BATCH_FREE)
        return 0;
    o = &b->ops;
    memset(commands, 0, sizeof(commands));
    if (o->resolve_batch(o->ctx, token, req_id, res_key, is_read, commands, &cnt) != 1 || !cnt ||
        cnt > OEM_TCS_BATCH_COMMANDS)
        return 0;
    for (i = 0; i < cnt; ++i) {
        if (commands[i].read > 1 || commands[i].address > 0x7ffffu ||
            (commands[i].read && commands[i].data))
            return 0;
        if (commands[i].read)
            ++reads;
    }
    if ((!is_read && reads) || (is_read && !reads))
        return 0;
    bit = 1u << slot;
    if (!o->read32(o->ctx, TCS_IRQ_STATUS, &irq) || !o->read32(o->ctx, TCS_STATUS(slot), &status) ||
        !o->read32(o->ctx, TCS_CONTROL(slot), &ctrl) || !o->read32(o->ctx, TCS_ENABLE(slot), &en) ||
        !o->read32(o->ctx, TCS_WAIT(slot), &wait) ||
        !o->read32(o->ctx, TCS_IRQ_ENABLE, &irq_enable)) {
        b->faulted = 1;
        return 0;
    }
    if ((irq & bit) || !(status & TCS_STATUS_IDLE) ||
        (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || en || wait ||
        !(irq_enable & bit))
        return 0;
    s = &b->slots[slot];
    s->token = token;
    s->req_id = req_id;
    s->res_key = res_key;
    s->cnt = (uint8_t)cnt;
    s->has_read = (uint8_t)(reads != 0);
    memcpy(s->commands, commands, cnt * sizeof(commands[0]));
    s->state = OEM_BATCH_SUBMIT_UNCERTAIN;
    mask = (1u << cnt) - 1u; /* count <=16, no shift overflow. */
    if (!o->write32(o->ctx, TCS_WAIT(slot), mask) || !o->write32(o->ctx, TCS_ENABLE(slot), mask))
        goto uncertain;
    for (i = 0; i < cnt; ++i) {
        const uint32_t msgid = commands[i].read ? TCS_MSGID_READ_CMD : TCS_MSGID_WRITE_CMD;
        if (!o->write32(o->ctx, TCS_CMD_MSGID(slot, i), msgid) ||
            !o->write32(o->ctx, TCS_CMD_ADDR(slot, i), commands[i].address) ||
            !o->write32(o->ctx, TCS_CMD_DATA(slot, i), commands[i].read ? 0u : commands[i].data))
            goto uncertain;
    }
    o->barrier(o->ctx);
    ctrl &= ~TCS_CONTROL_AMC_TRIGGER;
    if (!o->write32(o->ctx, TCS_CONTROL(slot), ctrl) ||
        !o->write32(o->ctx, TCS_CONTROL(slot), ctrl | TCS_CONTROL_AMC_MODE_EN))
        goto uncertain;
    o->barrier(o->ctx);
    if (!o->write32(o->ctx, TCS_CONTROL(slot),
                    ctrl | TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER))
        goto uncertain;
    o->barrier(o->ctx);
    s->state = OEM_BATCH_INFLIGHT;
    ++b->submitted;
    return 1;
uncertain:
    b->faulted = 1;
    return 0;
}

int oem_tcs_batch_validate_finished(oem_tcs_batch_t* b, uint8_t slot, uint64_t token)
{
    oem_tcs_batch_slot_t* s;
    uint32_t irq = 0, status = 0, cmdstatus = 0;
    uint32_t staged[OEM_TCS_BATCH_COMMANDS] = {0};
    uint32_t i;
    if (!b || b->faulted || slot >= OEM_TCS_BATCH_SLOTS || !b->completion_handler_bound ||
        !(b->external_grant_mask & (1u << slot)) || !token)
        return 0;
    s = &b->slots[slot];
    if (s->state != OEM_BATCH_INFLIGHT || s->token != token || !s->cnt || s->results_valid)
        return 0;
    if (!b->ops.read32(b->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        !b->ops.read32(b->ops.ctx, TCS_STATUS(slot), &status))
        goto fault;
    if (!(irq & (1u << slot)) || !(status & TCS_STATUS_IDLE))
        goto fault;
    for (i = 0; i < s->cnt; ++i) {
        if (!b->ops.read32(b->ops.ctx, TCS_CMD_STATUS_N(slot, i), &cmdstatus) ||
            !(cmdstatus & TCS_CMD_COMPLETED))
            goto fault;
        if (s->commands[i].read && !b->ops.read32(b->ops.ctx, TCS_CMD_RESP_N(slot, i), &staged[i]))
            goto fault;
    }
    memcpy(s->read_values, staged, sizeof(staged));
    s->results_valid = 1;
    return 1;
fault:
    b->faulted = 1;
    return 0;
}

int oem_tcs_batch_commit_ack(oem_tcs_batch_t* b, uint8_t slot, uint64_t token)
{
    oem_tcs_batch_slot_t* s;
    uint32_t irq = 0;
    if (!b || b->faulted || slot >= OEM_TCS_BATCH_SLOTS || !token || !b->completion_handler_bound ||
        !(b->external_grant_mask & (1u << slot)))
        return 0;
    s = &b->slots[slot];
    if (s->state != OEM_BATCH_INFLIGHT || s->token != token || !s->results_valid ||
        s->ack_confirmed)
        return 0;
    if (!b->ops.read32(b->ops.ctx, TCS_IRQ_STATUS, &irq)) {
        b->faulted = 1;
        return 0;
    }
    if (irq & (1u << slot))
        return 0;
    s->ack_confirmed = 1;
    return 1;
}

int oem_tcs_batch_copy_results(oem_tcs_batch_t* b, uint8_t slot, uint64_t token,
                               oem_tcs_batch_result_t* out, uint32_t capacity, uint32_t* cnt)
{
    oem_tcs_batch_slot_t* s;
    uint32_t irq = 0, i;
    if (cnt)
        *cnt = 0;
    if (!b || b->faulted || !out || !cnt || slot >= OEM_TCS_BATCH_SLOTS || !token ||
        !b->completion_handler_bound || !(b->external_grant_mask & (1u << slot)))
        return 0;
    s = &b->slots[slot];
    if (s->state != OEM_BATCH_INFLIGHT || s->token != token || !s->results_valid ||
        !s->ack_confirmed || capacity < s->cnt)
        return 0;
    if (!b->ops.read32(b->ops.ctx, TCS_IRQ_STATUS, &irq)) {
        b->faulted = 1;
        return 0;
    }
    if (irq & (1u << slot))
        return 0;
    for (i = 0; i < s->cnt; ++i) {
        out[i].is_read = s->commands[i].read;
        out[i].value = s->commands[i].read ? s->read_values[i] : 0u;
    }
    *cnt = s->cnt;
    s->results_copied = 1;
    return 1;
}

int oem_tcs_batch_retire(oem_tcs_batch_t* b, uint8_t slot, uint64_t token)
{
    oem_tcs_batch_slot_t* s;
    uint32_t irq = 0, ctrl = 0, en = 0, wait = 0;
    if (!b || b->faulted || slot >= OEM_TCS_BATCH_SLOTS || !token || !b->completion_handler_bound ||
        !(b->external_grant_mask & (1u << slot)))
        return 0;
    s = &b->slots[slot];
    if (s->state != OEM_BATCH_INFLIGHT || s->token != token || !s->results_valid ||
        !s->ack_confirmed || (s->has_read && !s->results_copied))
        return 0;
    if (!b->ops.read32(b->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        !b->ops.read32(b->ops.ctx, TCS_CONTROL(slot), &ctrl)) {
        b->faulted = 1;
        return 0;
    }
    if (irq & (1u << slot))
        return 0;
    s->state = OEM_BATCH_RETIRE_UNCERTAIN;
    if (!b->ops.write32(b->ops.ctx, TCS_CONTROL(slot),
                        ctrl & ~(TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) ||
        !b->ops.write32(b->ops.ctx, TCS_ENABLE(slot), 0u) ||
        !b->ops.write32(b->ops.ctx, TCS_WAIT(slot), 0u))
        goto fault;
    b->ops.barrier(b->ops.ctx);
    if (!b->ops.read32(b->ops.ctx, TCS_CONTROL(slot), &ctrl) ||
        !b->ops.read32(b->ops.ctx, TCS_ENABLE(slot), &en) ||
        !b->ops.read32(b->ops.ctx, TCS_WAIT(slot), &wait) ||
        !b->ops.read32(b->ops.ctx, TCS_IRQ_STATUS, &irq) ||
        (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || en || wait ||
        (irq & (1u << slot)))
        goto fault;
    memset(s, 0, sizeof(*s));
    ++b->retired;
    return 1;
fault:
    b->faulted = 1;
    return 0;
}

void oem_tcs_batch_close(oem_tcs_batch_t* b)
{
    if (b)
        b->closed = 1;
}

int oem_tcs_batch_can_release(const oem_tcs_batch_t* b)
{
    uint32_t i;
    if (!b || !b->closed || b->faulted)
        return 0;
    for (i = 0; i < OEM_TCS_BATCH_SLOTS; ++i)
        if (b->slots[i].state != OEM_BATCH_FREE || b->slots[i].token)
            return 0;
    return 1;
}
