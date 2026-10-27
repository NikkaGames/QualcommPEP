#include "oem_amc_command_queue.h"
#include <string.h>

/* not internally thread safe: caller holds the same completion/owner lock used for MMIO
 * status, W1C acknowledgement, and queue callbacks. */

static int valid_grant(const oem_amc_command_queue_t* q, uint8_t slot)
{
    uint8_t bit;
    if (!q || q->closed || q->admission_paused || q->faulted || !q->completion_handler_bound ||
        slot >= OEM_AMC_QUEUE_SLOTS || (q->external_grant_mask & (uint8_t)~7u))
        return 0;
    bit = (uint8_t)(1u << slot);
    return (q->external_grant_mask & bit) != 0;
}

int oem_amc_queue_init(oem_amc_command_queue_t* q, const oem_amc_cmd_ops_t* ops)
{
    if (!q || !ops || !ops->res_ready || !ops->submit_tcs || !ops->notify_client)
        return 0;
    memset(q, 0, sizeof(*q));
    q->ops = *ops;
    q->next_token = 1;
    return 1;
}

int oem_amc_queue_enqueue(oem_amc_command_queue_t* q, uint32_t req_id, uint32_t res_key,
                          uint8_t is_read, uint64_t* token)
{
    uint32_t i;
    uint32_t empty = OEM_AMC_QUEUE_MAX;
    if (token)
        *token = 0;
    if (!q || !token || !req_id || !res_key || is_read > 1 || q->closed || q->admission_paused ||
        q->faulted || !q->next_token || q->next_token == UINT64_MAX)
        return 0;
    for (i = 0; i < OEM_AMC_QUEUE_MAX; ++i) {
        if (q->commands[i].state == OEM_CMD_FREE) {
            if (empty == OEM_AMC_QUEUE_MAX)
                empty = i;
        } else if (q->commands[i].req_id == req_id) {
            return 0;
        }
    }
    if (empty == OEM_AMC_QUEUE_MAX)
        return 0;
    q->commands[empty].token = q->next_token++;
    q->commands[empty].req_id = req_id;
    q->commands[empty].res_key = res_key;
    q->commands[empty].is_read = is_read;
    q->commands[empty].slot = 0;
    q->commands[empty].state = OEM_CMD_QUEUED;
    *token = q->commands[empty].token;
    return 1;
}

oem_amc_queue_result_t oem_amc_queue_dispatch_head(oem_amc_command_queue_t* q, uint8_t slot)
{
    uint32_t i, head = OEM_AMC_QUEUE_MAX;
    oem_amc_cmd_t* c;
    int ready;
    if (!q || slot >= OEM_AMC_QUEUE_SLOTS)
        return CMD_INVALID;
    if (!valid_grant(q, slot))
        return CMD_BLOCKED;
    if (q->slot_entry[slot])
        return CMD_BLOCKED;
    for (i = 0; i < OEM_AMC_QUEUE_MAX; ++i)
        if (q->commands[i].state == OEM_CMD_QUEUED &&
            (head == OEM_AMC_QUEUE_MAX || q->commands[i].token < q->commands[head].token))
            head = i;
    if (head == OEM_AMC_QUEUE_MAX)
        return CMD_BLOCKED;
    c = &q->commands[head];
    /* the res-voter logic is more complex; a matching res/read cannot bypass an active
     * transaction. */

    for (i = 0; i < OEM_AMC_QUEUE_SLOTS; ++i) {
        const uint8_t entry = q->slot_entry[i];
        if (entry && q->commands[entry - 1].res_key == c->res_key)
            return CMD_BLOCKED;
        if (entry && c->is_read)
            return CMD_BLOCKED;
    }
    ready = q->ops.res_ready(q->ops.ctx, c->res_key, c->req_id, c->is_read);
    if (ready < 0) {
        q->faulted = 1;
        return CMD_FAULT;
    }
    if (ready == 0)
        return CMD_BLOCKED;
    /* on any failure, hardware may already own the slot; retain the exact token and never
     * re-submit automatically. */

    q->slot_entry[slot] = (uint8_t)(head + 1);
    c->slot = slot;
    c->state = OEM_CMD_SUBMITTING;
    {
        const int submission =
            q->ops.submit_tcs(q->ops.ctx, slot, c->token, c->req_id, c->res_key, c->is_read);
        if (submission == -1) {
            /* the physical adapter has explicitly proved that nothing reached MMIO/ledger or
             * asynchronous ownership. release only our provisional queue-side reservation. Preserve
             * FIFO token and request identity. Even a harmless read-only guard error must not let
             * newer requests jump the head of the queue. */
            c->slot = 0u;
            c->state = OEM_CMD_QUEUED;
            q->slot_entry[slot] = 0u;
            return CMD_BLOCKED;
        }
        if (submission != 1) {
            /* ambiguous failure MAY have issued a command. The res owner and queue slot remain
             * quarantined until independent reconciliation. */
            q->faulted = 1;
            return CMD_FAULT;
        }
    }
    c->state = OEM_CMD_INFLIGHT;
    return CMD_ACCEPTED;
}

int oem_amc_queue_resolve_inflight(void* opaque, uint8_t slot, uint64_t* token)
{
    oem_amc_command_queue_t* q = opaque;
    uint8_t entry;
    if (token)
        *token = 0;
    if (!q || !token || q->faulted || slot >= OEM_AMC_QUEUE_SLOTS ||
        !(q->external_grant_mask & (1u << slot)) || !q->completion_handler_bound)
        return 0;
    entry = q->slot_entry[slot];
    if (!entry || entry > OEM_AMC_QUEUE_MAX)
        return 0;
    if (q->commands[entry - 1].state != OEM_CMD_INFLIGHT || q->commands[entry - 1].slot != slot ||
        !q->commands[entry - 1].token)
        return 0;
    *token = q->commands[entry - 1].token;
    return 1;
}

int oem_amc_queue_complete_inflight(void* opaque, uint8_t slot, uint64_t token)
{
    oem_amc_command_queue_t* q = opaque;
    oem_amc_cmd_t* c;
    uint8_t entry;
    if (!q || !token || q->faulted || slot >= OEM_AMC_QUEUE_SLOTS ||
        !(q->external_grant_mask & (1u << slot)) || !q->completion_handler_bound)
        return 0;
    entry = q->slot_entry[slot];
    if (!entry || entry > OEM_AMC_QUEUE_MAX)
        return 0;
    c = &q->commands[entry - 1];
    if (c->state != OEM_CMD_INFLIGHT || c->token != token || c->slot != slot)
        return 0;
    /* has already proven ACK readback before invoking this. A callback failure may mean
     * partial delivery: never retry blindly. */

    c->state = OEM_CMD_NOTIFYING;
    if (q->ops.notify_client(q->ops.ctx, c->token, c->req_id) != 1) {
        c->state = OEM_CMD_NOTIFY_UNCERTAIN;
        q->faulted = 1;
        return 0;
    }
    q->slot_entry[slot] = 0;
    memset(c, 0, sizeof(*c));
    ++q->completed;
    return 1;
}

uint8_t oem_amc_queue_inflight_mask(const oem_amc_command_queue_t* q)
{
    uint8_t mask = 0, slot;
    if (!q)
        return 0;
    for (slot = 0; slot < OEM_AMC_QUEUE_SLOTS; ++slot) {
        uint8_t entry = q->slot_entry[slot];
        if (entry && entry <= OEM_AMC_QUEUE_MAX && q->commands[entry - 1].state == OEM_CMD_INFLIGHT)
            mask |= (uint8_t)(1u << slot);
    }
    return mask;
}

int oem_amc_queue_pause(oem_amc_command_queue_t* q)
{
    if (!q || q->closed || q->faulted || q->admission_paused)
        return 0;
    q->admission_paused = 1;
    return 1;
}

int oem_amc_queue_resume(oem_amc_command_queue_t* q)
{
    if (!q || q->closed || q->faulted || !q->admission_paused)
        return 0;
    q->admission_paused = 0;
    return 1;
}

void oem_amc_queue_close(oem_amc_command_queue_t* q)
{
    if (q)
        q->closed = 1;
}

int oem_amc_queue_can_release(const oem_amc_command_queue_t* q)
{
    uint32_t i;
    if (!q || !q->closed || q->faulted)
        return 0;
    for (i = 0; i < OEM_AMC_QUEUE_SLOTS; ++i)
        if (q->slot_entry[i])
            return 0;
    for (i = 0; i < OEM_AMC_QUEUE_MAX; ++i)
        if (q->commands[i].state != OEM_CMD_FREE)
            return 0;
    return 1;
}
