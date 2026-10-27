#include "oem_rpmh_host_fence.h"
#include <string.h>

#if defined(_MSC_VER)
#include <ntddk.h>

static int64_t atomic_epoch(const volatile int64_t* v)
{
    return (int64_t)InterlockedCompareExchange64((volatile LONG64*)v, 0, 0);
}

static void set_epoch(volatile int64_t* v, int64_t e)
{
    (void)InterlockedExchange64((volatile LONG64*)v, (LONG64)e);
}

static int32_t atomic_phase(const volatile int32_t* v)
{
    return (int32_t)InterlockedCompareExchange((volatile LONG*)v, 0, 0);
}

static void set_phase(volatile int32_t* v, int32_t p)
{
    (void)InterlockedExchange((volatile LONG*)v, (LONG)p);
}
#else
static int64_t atomic_epoch(const volatile int64_t* v)
{
    return __atomic_load_n(v, __ATOMIC_SEQ_CST);
}

static void set_epoch(volatile int64_t* v, int64_t e)
{
    __atomic_store_n(v, e, __ATOMIC_SEQ_CST);
}

static int32_t atomic_phase(const volatile int32_t* v)
{
    return __atomic_load_n(v, __ATOMIC_SEQ_CST);
}

static void set_phase(volatile int32_t* v, int32_t p)
{
    __atomic_store_n(v, p, __ATOMIC_SEQ_CST);
}
#endif

/* serialize queue, batch and pipeline under one completion lock. A drained snapshot grants neither
 * controller ownership nor a PnP reference. */

static int idle_locked(const oem_host_fence_t* f)
{
    uint32_t i;
    const oem_amc_command_queue_t* q = f->queue;
    const oem_tcs_batch_t* b = f->batch;
    const rpmh_irq_pipeline_t* p = f->pipeline;
    if (q->faulted || !q->admission_paused || b->faulted || p->faulted || !p->bound ||
        p->epoch == 0 || p->pending.active || p->pending.epoch ||
        rpmh_irq_gate_outstanding(&p->gate) || p->work.state != RPMH_WORK_READY ||
        p->work.irq_masked || p->work.completion_lock_held)
        return 0;
    for (i = 0; i < OEM_AMC_QUEUE_MAX; ++i)
        if (q->commands[i].state != OEM_CMD_FREE)
            return 0;
    for (i = 0; i < OEM_AMC_QUEUE_SLOTS; ++i)
        if (q->slot_entry[i] || b->slots[i].state != OEM_BATCH_FREE)
            return 0;
    return 1;
}

static int all_idle(oem_host_fence_t* f)
{
    int ok;
    if (f->ops.enter_queue_lock(f->ops.ctx) != 1)
        return 0;
    ok = idle_locked(f);
    f->ops.leave_queue_lock(f->ops.ctx);
    return ok;
}

static void fault(oem_host_fence_t* f)
{
    set_phase(&f->phase, HOST_FENCE_FAULT);
}

int oem_host_fence_init(oem_host_fence_t* f, oem_amc_command_queue_t* q, oem_tcs_batch_t* b,
                        rpmh_irq_pipeline_t* p, uint32_t epoch, const oem_host_fence_ops_t* ops)
{
    if (!f || !q || !b || !p || !epoch || !ops || !ops->enter_queue_lock ||
        !ops->leave_queue_lock || !ops->drain_accepted || !ops->disable_owned_interrupt ||
        !ops->sync_isr || !ops->flush_dpcs || !ops->flush_deferred_work || !ops->exclusive_owner ||
        !ops->enable_owned_interrupt || !p->bound || !p->epoch)
        return 0;
    memset(f, 0, sizeof(*f));
    f->queue = q;
    f->batch = b;
    f->pipeline = p;
    f->ops = *ops;
    f->owner_epoch = epoch;
    set_epoch(&f->gate_epoch, 1);
    rpmh_irq_gate_init(&f->admission);
    if (!rpmh_irq_gate_open(&f->admission, f->gate_epoch))
        return 0;
    set_phase(&f->phase, HOST_FENCE_OPEN);
    return 1;
}

int oem_host_fence_enqueue(oem_host_fence_t* f, uint32_t req_id, uint32_t res_key, uint8_t is_read,
                           uint64_t* token)
{
    rpmh_irq_ticket_t pin = {0, 0};
    int accepted = 0;
    if (token)
        *token = 0;
    if (!f || !token || atomic_phase(&f->phase) != HOST_FENCE_OPEN ||
        !rpmh_irq_gate_try_pin(&f->admission, (uint64_t)atomic_epoch(&f->gate_epoch), &pin))
        return 0;
    /* close may race with this pin, but its ticket prevents drain from succeeding until
     * finish. The completion lock prevents data races on 's non-atomic queue. */

    if (f->ops.enter_queue_lock(f->ops.ctx) == 1) {
        accepted = oem_amc_queue_enqueue(f->queue, req_id, res_key, is_read, token);
        f->ops.leave_queue_lock(f->ops.ctx);
    }
    if (!rpmh_irq_gate_retire_ticket(&f->admission, &pin)) {
        fault(f);
        return 0;
    }
    return accepted;
}

int oem_host_fence_and_drain(void* ctx, uint32_t epoch, uint64_t* lease)
{
    oem_host_fence_t* f = ctx;
    if (lease)
        *lease = 0;
    if (!f || !lease || epoch != f->owner_epoch || atomic_phase(&f->phase) != HOST_FENCE_OPEN ||
        atomic_epoch(&f->gate_epoch) >= INT64_MAX - 1 || f->lease_sequence == UINT64_MAX ||
        f->ops.exclusive_owner(f->ops.ctx, epoch) != 1)
        return 0;
    /* pause the actual queue under its completion lock FIRST, so even a direct QueueEnqueue
     * caller cannot bypass the sleep fence. Any producer already inside the lock either
     * finished before pause or will be rejected. */

    if (f->ops.enter_queue_lock(f->ops.ctx) != 1) {
        fault(f);
        return 0;
    }
    {
        int paused = oem_amc_queue_pause(f->queue);
        f->ops.leave_queue_lock(f->ops.ctx);
        if (!paused) {
            fault(f);
            return 0;
        }
    }

    if (!rpmh_irq_gate_close(&f->admission, (uint64_t)atomic_epoch(&f->gate_epoch))) {
        fault(f);
        return 0;
    }
    set_phase(&f->phase, HOST_FENCE_CLOSED);
    /* recheck the admission gate and pin count after the callback wait. */

    if (f->ops.drain_accepted(f->ops.ctx) != 1 ||
        !rpmh_irq_gate_closed_and_drained(&f->admission, (uint64_t)atomic_epoch(&f->gate_epoch)) ||
        !all_idle(f)) {
        fault(f);
        return 0; /* never reopen automatically after uncertainty. */
    }
    /* interrupt disable only AFTER completions have reached terminal idle; otherwise a
     * queue/AMC completion can be stranded indefinitely. */

    if (f->ops.disable_owned_interrupt(f->ops.ctx) != 1) {
        fault(f);
        return 0;
    }
    f->interrupt_disabled = 1;
    if (f->ops.sync_isr(f->ops.ctx) != 1 || f->ops.flush_dpcs(f->ops.ctx) != 1 ||
        f->ops.flush_deferred_work(f->ops.ctx) != 1 || !all_idle(f) ||
        f->ops.exclusive_owner(f->ops.ctx, epoch) != 1) {
        fault(f);
        return 0;
    }
    f->lease = ++f->lease_sequence;
    set_phase(&f->phase, HOST_FENCE_DRAINED);
    *lease = f->lease;
    return 1;
}

int oem_host_fence_still_drained(void* ctx, uint32_t epoch, uint64_t lease)
{
    oem_host_fence_t* f = ctx;
    if (!f || !lease || epoch != f->owner_epoch || atomic_phase(&f->phase) != HOST_FENCE_DRAINED ||
        f->lease != lease || !f->interrupt_disabled ||
        !rpmh_irq_gate_closed_and_drained(&f->admission, (uint64_t)atomic_epoch(&f->gate_epoch)) ||
        f->ops.exclusive_owner(f->ops.ctx, epoch) != 1)
        return 0;
    return all_idle(f);
}

int oem_host_fence_release(void* ctx, uint32_t epoch, uint64_t lease)
{
    oem_host_fence_t* f = ctx;
    if (!oem_host_fence_still_drained(f, epoch, lease))
        return 0;
    /* only should call this after THREE independent firmware/resume proofs. alone does not
     * verify those external proofs. */

    if (atomic_epoch(&f->gate_epoch) >= INT64_MAX - 1 ||
        f->ops.enable_owned_interrupt(f->ops.ctx) != 1) {
        fault(f);
        return 0;
    }
    f->interrupt_disabled = 0;
    if (!rpmh_irq_gate_open(&f->admission, (uint64_t)atomic_epoch(&f->gate_epoch) + 1)) {
        fault(f);
        return 0; /* irq may be enabled: must retain owner, never retry. */
    }
    set_epoch(&f->gate_epoch, atomic_epoch(&f->gate_epoch) + 1);
    if (f->ops.enter_queue_lock(f->ops.ctx) != 1) {
        fault(f);
        return 0;
    }
    {
        int resumed = oem_amc_queue_resume(f->queue);
        f->ops.leave_queue_lock(f->ops.ctx);
        if (!resumed) {
            fault(f);
            return 0;
        }
    }
    f->lease = 0;
    set_phase(&f->phase, HOST_FENCE_OPEN);
    return 1;
}

static int bind_fence(void* ctx, uint32_t epoch, uint64_t* lease)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && oem_host_fence_and_drain(b->fence, epoch, lease);
}

static int bind_check(void* ctx, uint32_t epoch, uint64_t lease)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && oem_host_fence_still_drained(b->fence, epoch, lease);
}

static int bind_release(void* ctx, uint32_t epoch, uint64_t lease)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && oem_host_fence_release(b->fence, epoch, lease);
}

static int bind_never(void* ctx, uint32_t epoch, uint64_t token, uint64_t lease)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && b->never_started(b->firmware_ctx, epoch, token, lease);
}

static int bind_transfer(void* ctx, uint32_t epoch, uint64_t token, uint64_t lease, uint8_t slots)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && b->transfer_to_firmware(b->firmware_ctx, epoch, token, lease, slots);
}

static int bind_return(void* ctx, uint32_t epoch, uint64_t token, uint64_t lease)
{
    oem_host_fence_handoff_binding_t* b = ctx;
    return b && b->firmware_returned(b->firmware_ctx, epoch, token, lease);
}

int oem_host_fence_bind_handoff(oem_host_fence_handoff_binding_t* b, oem_sleepwake_handoff_ops_t* h)
{
    if (!b || !h || !b->fence || !b->never_started || !b->transfer_to_firmware ||
        !b->firmware_returned || atomic_phase(&b->fence->phase) != HOST_FENCE_OPEN)
        return 0;
    memset(h, 0, sizeof(*h));
    h->ctx = b;
    h->fence_and_drain = bind_fence;
    h->fence_still_drained = bind_check;
    h->release_fence = bind_release;
    h->never_started = bind_never;
    h->transfer_to_firmware = bind_transfer;
    h->firmware_returned = bind_return;
    return 1;
}
