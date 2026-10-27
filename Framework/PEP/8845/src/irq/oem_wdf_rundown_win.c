/* WDF rundown requires independent hardware mask, DPC drain and PnP ownership evidence. */

#include <ntddk.h>
#include <wdf.h>
#include "oem_wdf_rundown_win.h"

static int passive(void)
{
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
}

static int owned(oem_wdf_rundown_win_t* b)
{
    return b && !b->faulted && passive() && b->proofs.exclusive_owner(b->proofs.ctx, b->epoch) == 1;
}

static int enter(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    return b && b->base.enter_queue_lock(b->base.ctx) == 1;
}

static void rundown_leave_queue_lock(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    if (b)
        b->base.leave_queue_lock(b->base.ctx);
}

static int drain(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    return owned(b) && b->base.drain_accepted(b->base.ctx) == 1;
}

static int owner(void* ctx, uint32_t epoch)
{
    oem_wdf_rundown_win_t* b = ctx;
    return b && epoch == b->epoch && owned(b) && b->base.exclusive_owner(b->base.ctx, epoch) == 1;
}

static int disabled(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    if (!owned(b) || b->disabled || !b->interrupt)
        return 0;
    /* caller is owner-controlled work item, NOT an arbitrary request thread. The actual
     * EvtInterruptDisable callback must mask the hardware. */

    WdfInterruptDisable((WDFINTERRUPT)b->interrupt);
    b->disabled = 1;
    if (b->proofs.hardware_irq_masked(b->proofs.ctx) != 1) {
        b->faulted = 1;
        return 0;
    }
    return 1;
}

static BOOLEAN synchronized_isr(WDFINTERRUPT interrupt, WDFCONTEXT ctx)
{
    oem_wdf_rundown_win_t* b = (oem_wdf_rundown_win_t*)ctx;
    /* called at DIRQL holding the WDF interrupt lock: no blocking, no owner callbacks, no
     * kernel allocations and no software lock nesting. */

    return b && b->interrupt == (void*)interrupt && b->disabled ? TRUE : FALSE;
}

static int sync(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    if (!owned(b) || !b->disabled || b->isr_synced)
        return 0;
    if (!WdfInterruptSynchronize((WDFINTERRUPT)b->interrupt, synchronized_isr, (WDFCONTEXT)b)) {
        b->faulted = 1;
        return 0;
    }
    /* no new ISR can queue once WDF disable, mask readback and WdfInterruptSynchronize have
     * all completed. */

    if (b->dpc_tracker && !oem_wdf_dpc_close_win(b->dpc_tracker, b->interrupt, b->epoch)) {
        b->faulted = 1;
        return 0;
    }
    b->isr_synced = 1;
    return 1;
}

static int dpcs(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    if (!owned(b) || !b->disabled || !b->isr_synced || b->dpcs_drained)
        return 0;
    /* no imaginary WdfInterruptFlushDpc function: owner must have a proven DPC
     * admission/rundown counter + synchronization for this interrupt. */

    if ((b->dpc_tracker ? oem_wdf_dpc_flush_proof_win(b->dpc_tracker, b->interrupt, b->epoch)
                        : b->proofs.interrupt_dpcs_drained(b->proofs.ctx)) != 1) {
        b->faulted = 1;
        return 0;
    }
    b->dpcs_drained = 1;
    return 1;
}

static int works(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    uint32_t i, cnt = 0;
    if (!owned(b) || !b->dpcs_drained || b->work_flushed ||
        b->proofs.work_enqueue_stopped(b->proofs.ctx) != 1) {
        if (b && b->dpcs_drained)
            b->faulted = 1;
        return 0;
    }
    if (!b->works || !b->works->published || b->works->faulted ||
        b->works->owner_epoch != (uint64_t)b->epoch) {
        b->faulted = 1;
        return 0;
    }
    for (i = 0; i <= OEM_RPMH_WORK_MAX_ID; ++i) {
        if (!b->works->present[i]) {
            if (b->works->amc[i] || b->works->timeout[i]) {
                b->faulted = 1;
                return 0;
            }
            continue;
        }
        ++cnt;
        if (!b->works->amc[i] || !b->works->timeout[i] ||
            !oem_wdf_work_flush_reusable_win(b->work_ops.ctx, b->works->amc[i]) ||
            !oem_wdf_work_flush_reusable_win(b->work_ops.ctx, b->works->timeout[i])) {
            b->faulted = 1;
            return 0;
        }
    }
    if (cnt != b->works->cnt || !cnt) {
        b->faulted = 1;
        return 0;
    }
    b->work_flushed = 1;
    return 1;
}

static int enabled(void* ctx)
{
    oem_wdf_rundown_win_t* b = ctx;
    if (!owned(b) || !b->disabled || !b->isr_synced || !b->dpcs_drained || !b->work_flushed ||
        b->proofs.work_enqueue_stopped(b->proofs.ctx) != 1)
        return 0;
    /* hardware is still masked: re-open ticket admission before enabling. */
    if (b->dpc_tracker && !oem_wdf_dpc_reopen_win(b->dpc_tracker, b->interrupt, b->epoch)) {
        b->faulted = 1;
        return 0;
    }
    WdfInterruptEnable((WDFINTERRUPT)b->interrupt);
    /* wdf returns VOID: the owner must read back the actual enable state. */
    if (b->proofs.hardware_irq_enabled(b->proofs.ctx) != 1) {
        if (b->dpc_tracker)
            oem_wdf_dpc_quarantine_win(b->dpc_tracker);
        b->faulted = 1;
        return 0;
    }
    b->disabled = b->isr_synced = b->dpcs_drained = b->work_flushed = 0;
    return 1;
}

int oem_wdf_rundown_init_win(oem_wdf_rundown_win_t* b, void* interrupt,
                             oem_rpmh_work_table_t* table, const oem_rpmh_work_ops_t* workops,
                             uint32_t epoch, const oem_host_fence_ops_t* base,
                             const oem_wdf_rundown_proofs_t* proofs)
{
    if (!b || !interrupt || !table || !workops || !workops->ctx || !epoch || !base ||
        !base->enter_queue_lock || !base->leave_queue_lock || !base->drain_accepted ||
        !base->exclusive_owner || !proofs || !proofs->exclusive_owner ||
        !proofs->hardware_irq_masked || !proofs->hardware_irq_enabled ||
        !proofs->work_enqueue_stopped || !table->published || table->faulted ||
        table->owner_epoch != (uint64_t)epoch || !table->cnt || !passive())
        return 0;
    RtlZeroMemory(b, sizeof(*b));
    b->interrupt = interrupt;
    b->works = table;
    b->work_ops = *workops;
    b->base = *base;
    b->proofs = *proofs;
    b->epoch = epoch;
    return 1;
}

int oem_wdf_rundown_bind_host_ops_win(oem_wdf_rundown_win_t* b, oem_host_fence_ops_t* out)
{
    if (!b || !out || !b->interrupt || !b->epoch || b->faulted ||
        (!b->dpc_tracker && !b->proofs.interrupt_dpcs_drained))
        return 0;
    RtlZeroMemory(out, sizeof(*out));
    out->ctx = b;
    out->enter_queue_lock = enter;
    out->leave_queue_lock = rundown_leave_queue_lock;
    out->drain_accepted = drain;
    out->disable_owned_interrupt = disabled;
    out->sync_isr = sync;
    out->flush_dpcs = dpcs;
    out->flush_deferred_work = works;
    out->exclusive_owner = owner;
    out->enable_owned_interrupt = enabled;
    return 1;
}

/* attach only before any interrupt admission, under the PnP owner lock. */

int oem_wdf_rundown_attach_dpc_tracker_win(oem_wdf_rundown_win_t* b, oem_wdf_dpc_tracker_win_t* t)
{
    if (!b || !t || !b->interrupt || b->faulted || b->disabled || b->dpc_tracker ||
        t->interrupt != b->interrupt || t->epoch != b->epoch ||
        InterlockedCompareExchange(&t->phase, 0, 0) != 1 ||
        InterlockedCompareExchange(&t->outstanding, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->queuing, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->running, 0, 0) != 0 || !passive())
        return 0;
    b->dpc_tracker = t;
    return 1;
}
