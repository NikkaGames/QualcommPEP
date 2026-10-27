/* the unique raw/translated PnP pair creates an inactive IRQ37 WDF child. Controller/TCS/MMIO
 * ownership is separate. */

#include <ntddk.h>
#include <wdf.h>
#include "oem_wdf_irq_create.h"
#include "rpmh_tcs.h"
#include "pnp_resources.h"
#include "oem_wdf_dpc_tracker_win.h"
#include "trace.h"
#include "oem_wdf_irq_win.tmh"

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(oem_wdf_irq_ctx_t, oem_wdf_interrupt_ctx);

#define AMC_WORK_BUDGET 64L

/* this WDF context owns no controller; it borrows a WDFINTERRUPT while the same PnP owner
 * guarantees work-item flush before interrupt deletion. */

typedef struct amc_wdf_work_ctx {
    WDFINTERRUPT interrupt;
} amc_wdf_work_ctx_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(amc_wdf_work_ctx_t, amc_wdf_work_ctx);

/* all PASSIVE callbacks go through the same hardware completion bridge as before; the WDF
 * item never invents a successful IRQ or vote. */

static VOID oem_wdf_amc_passive_worker(WDFWORKITEM item)
{
    amc_wdf_work_ctx_t* wc = amc_wdf_work_ctx(item);
    oem_wdf_irq_ctx_t* c = wc && wc->interrupt ? oem_wdf_interrupt_ctx(wc->interrupt) : NULL;
    LONG i;
    if (!c)
        return;
    for (i = 0; i < AMC_WORK_BUDGET; ++i) {
        LONG n = InterlockedCompareExchange(&c->completion_pending, 0, 0);
        if (n <= 0) {
            /* reopen before checking for a producer racing the last drain. */
            InterlockedExchange(&c->completion_queued, 0);
            if (InterlockedCompareExchange(&c->completion_pending, 0, 0) > 0 &&
                InterlockedCompareExchange(&c->completion_queued, 1, 0) == 0)
                continue;
            return;
        }
        if (InterlockedCompareExchange(&c->completion_pending, n - 1, n) != n) {
            --i;
            continue;
        }
        /* owner must remain pinned; a failed worker keeps all controller resources quarantined
         * rather than reporting completion. */

        if (!c->owner_epoch || !c->bridge || c->bridge->epoch != c->owner_epoch ||
            !oem_wdf_run_amc_worker_win(wc->interrupt)) {
            InterlockedExchange(&c->completion_faulted, 1);
            return;
        }
    }
    /* a fairness budget is not an AMC failure. Transfer queue ownership to a new callback
     * without consuming its ticket. KMDF permits enqueue while a callback runs; the new
     * callback can start immediately, so this callback must touch no pipeline state after the
     * enqueue. */
    InterlockedExchange(&c->completion_queued, 0);
    if (InterlockedCompareExchange(&c->completion_pending, 0, 0) > 0 &&
        InterlockedCompareExchange(&c->completion_queued, 1, 0) == 0)
        WdfWorkItemEnqueue(item);
}

/* wdf schedules read-only timeout review. The scheduler retains the exact request, TCS
 * reservation and owner until independently proven rundown. */
static VOID oem_wdf_timeout_passive_worker(WDFWORKITEM item)
{
    amc_wdf_work_ctx_t* wc = amc_wdf_work_ctx(item);
    oem_wdf_irq_ctx_t* c = wc && wc->interrupt ? oem_wdf_interrupt_ctx(wc->interrupt) : NULL;
    uint64_t epoch;
    uint32_t req;
    int observed = 0;
    if (!c)
        return;
    epoch = c->timeout_epoch;
    req = c->timeout_req;
    if (KeGetCurrentIrql() == PASSIVE_LEVEL && c->timeout_work_item == (void*)item &&
        c->owner_epoch == epoch && c->bridge && c->bridge->epoch == epoch && c->timeout_review &&
        c->timeout_review_ctx && req && InterlockedCompareExchange(&c->timeout_queued, 0, 0) == 1)
        observed = c->timeout_review(c->timeout_review_ctx, epoch, req);
    if (!observed)
        InterlockedExchange(&c->timeout_faulted, 1);
    TraceEvents((observed ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_WARNING), TRACE_RPMH,
                "rpmh: timeout review ticket=%lu epoch=%llX observed=%d queued=%ld pending=%ld "
                "fault=%ld owner=retained",
                (ULONG)req, (unsigned long long)epoch, observed,
                InterlockedCompareExchange(&c->completion_queued, 0, 0),
                InterlockedCompareExchange(&c->completion_pending, 0, 0),
                InterlockedCompareExchange(&c->completion_faulted, 0, 0));
    InterlockedExchange(&c->timeout_queued, 0);
}

int oem_wdf_queue_timeout_review_win(void* handle, uint64_t epoch, uint32_t req,
                                     int (*review)(void*, uint64_t, uint32_t), void* ctx)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !epoch || !req || !review || !ctx || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || !c->timeout_work_item || !c->completion_work_item || c->passive_irq_configured != 1 ||
        c->owner_epoch != epoch || !c->bridge || c->bridge->epoch != epoch || !c->dpc_tracker ||
        c->dpc_tracker->epoch != epoch ||
        InterlockedCompareExchange(&c->completion_closing, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->timeout_faulted, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->timeout_queued, 1, 0) != 0)
        return 0;
    c->timeout_review_ctx = ctx;
    c->timeout_review = review;
    c->timeout_epoch = epoch;
    c->timeout_req = req;
    WdfWorkItemEnqueue((WDFWORKITEM)c->timeout_work_item);
    return 1;
}

int oem_wdf_queue_amc_work_win(void* handle)
{
    WDFINTERRUPT irq = (WDFINTERRUPT)handle;
    oem_wdf_irq_ctx_t* c;
    LONG cnt;
    if (!irq || KeGetCurrentIrql() > DISPATCH_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx(irq);

    if (!c || c->passive_irq_configured != 1 || !c->completion_work_item || !c->owner_epoch ||
        !c->bridge || c->bridge->epoch != c->owner_epoch || !c->bridge->pipeline ||
        !c->bridge->pipeline->pending.active ||
        c->bridge->pipeline->pending.epoch != c->owner_epoch ||
        InterlockedCompareExchange(&c->completion_closing, 0, 0) ||
        InterlockedCompareExchange(&c->completion_faulted, 0, 0))
        return 0;
    cnt = InterlockedIncrement(&c->completion_pending);
    if (cnt > AMC_WORK_BUDGET) {
        InterlockedExchange(&c->completion_faulted, 1);
        return 0;
    }
    if (InterlockedCompareExchange(&c->completion_queued, 1, 0) == 0)
        WdfWorkItemEnqueue((WDFWORKITEM)c->completion_work_item);
    return 1;
}

/* the d0 backend may attempt hardware attestation only after the framework has actually
 * enabled this exact, fully attached WDF interrupt. Reading the existing physical mask
 * proof is distinct from manufacturing a controller/IRQ grant. */

int oem_wdf_d0_owner_ready_win(void* handle, uint64_t epoch)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    return c && c->passive_irq_configured == 1 && c->owner_epoch == epoch && c->bridge &&
           c->dpc_tracker && c->irq_mask_owner && c->bridge->epoch == epoch &&
           c->dpc_tracker->epoch == epoch && c->dpc_tracker->interrupt == handle &&
           InterlockedCompareExchange(&c->framework_enabled, 0, 0) == 1 &&
           InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 1 &&
           InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) == 1 &&
           InterlockedCompareExchange(&c->completion_closing, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->completion_faulted, 0, 0) == 0 &&
           oem_irq_bridge_valid(c->bridge) &&
           oem_irq_mask_read_proof(c->irq_mask_owner, handle, epoch, 0);
}

/* borrowed ISR/work callbacks must outlive RSC/GIC mappings. OwnerReady still requires complete
 * hardware and WDF authorization. */

int oem_wdf_d0_owner_matches_win(void* handle, uint64_t epoch, const oem_wdf_irq_bridge_t* bridge,
                                 const oem_wdf_dpc_tracker_win_t* tracker,
                                 const oem_irq_mask_owner_t* mask)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !epoch || !bridge || !tracker || !mask || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || c->owner_epoch != epoch || c->bridge != bridge ||
        c->dpc_tracker != tracker || c->irq_mask_owner != mask || bridge->epoch != epoch ||
        tracker->epoch != epoch || tracker->interrupt != handle || mask->epoch != epoch ||
        mask->interrupt != handle)
        return 0;
    return oem_wdf_d0_owner_ready_win(handle, epoch);
}

int oem_wdf_completion_idle_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    return c && c->completion_work_item && c->timeout_work_item &&
           !InterlockedCompareExchange(&c->timeout_queued, 0, 0) &&
           !InterlockedCompareExchange(&c->timeout_faulted, 0, 0) &&
           !InterlockedCompareExchange(&c->completion_faulted, 0, 0) &&
           !InterlockedCompareExchange(&c->completion_pending, 0, 0) &&
           !InterlockedCompareExchange(&c->completion_queued, 0, 0);
}

/* hardware-owned TCS bits may only be rolled back before WDF owner publication if no
 * callback could have observed them. This is a read-only check, not a blanket permission
 * to revoke a native owner. */

int oem_wdf_owner_publication_vacant_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    return c && c->passive_irq_configured == 1 && c->completion_work_item && !c->owner_epoch &&
           !c->bridge && !c->dpc_tracker && !c->irq_mask_owner &&
           InterlockedCompareExchange(&c->framework_enabled, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->completion_closing, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->completion_faulted, 0, 0) == 0 &&
           oem_wdf_completion_idle_win(handle);
}

int oem_wdf_retire_owner_win(void* handle, uint64_t epoch)
{
    oem_wdf_irq_ctx_t* c;
    oem_wdf_dpc_tracker_win_t* t;
    if (!handle || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || c->owner_epoch != epoch || !c->bridge ||
        !c->dpc_tracker || !c->irq_mask_owner || c->bridge->epoch != epoch ||
        !c->bridge->pipeline || c->bridge->pipeline->bound ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) ||
        !oem_wdf_completion_idle_win(handle) ||
        !oem_irq_mask_read_proof(c->irq_mask_owner, handle, epoch, 1))
        return 0;
    t = c->dpc_tracker;
    if (t->interrupt != handle || t->epoch != epoch ||
        InterlockedCompareExchange(&t->phase, 0, 0) != 2 ||
        !InterlockedCompareExchange(&t->flushed, 0, 0) ||
        InterlockedCompareExchange(&t->outstanding, 0, 0) ||
        InterlockedCompareExchange(&t->queuing, 0, 0) ||
        InterlockedCompareExchange(&t->running, 0, 0) ||
        InterlockedCompareExchange(&t->accepted, 0, 0) !=
            InterlockedCompareExchange(&t->finished, 0, 0))
        return 0;
    InterlockedExchange(&c->completion_closing, 1);
    /* the external controller owner remains responsible for releasing its mapping and pool
     * AFTER this returns. */

    c->timeout_review = NULL;
    c->timeout_review_ctx = NULL;
    c->timeout_epoch = 0;
    c->timeout_req = 0;
    c->bridge = NULL;
    c->dpc_tracker = NULL;
    c->irq_mask_owner = NULL;
    c->owner_epoch = 0;
    return 1;
}

/* retain the pipeline binding until the last worker retires its ACK and ledger ticket. */

int oem_wdf_published_owner_stopped_win(void* handle, uint64_t epoch)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    return c && c->passive_irq_configured == 1 && c->owner_epoch == epoch &&
           c->completion_work_item && c->bridge && c->bridge->epoch == epoch && c->dpc_tracker &&
           c->dpc_tracker->epoch == epoch && c->dpc_tracker->interrupt == handle &&
           c->irq_mask_owner && InterlockedCompareExchange(&c->framework_enabled, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->completion_closing, 0, 0) == 0 &&
           InterlockedCompareExchange(&c->completion_faulted, 0, 0) == 0 &&
           oem_irq_mask_read_proof(c->irq_mask_owner, handle, epoch, 1);
}

int oem_wdf_drain_published_work_win(void* handle, uint64_t epoch)
{
    oem_wdf_irq_ctx_t* c;
    oem_wdf_dpc_tracker_win_t* t;
    if (!oem_wdf_published_owner_stopped_win(handle, epoch))
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    t = c->dpc_tracker;
    if (!c->bridge->pipeline || !c->bridge->pipeline->bound ||
        c->bridge->pipeline->epoch != epoch || InterlockedCompareExchange(&t->phase, 0, 0) != 2 ||
        InterlockedCompareExchange(&t->flushed, 0, 0) != 1 ||
        InterlockedCompareExchange(&t->outstanding, 0, 0) ||
        InterlockedCompareExchange(&t->queuing, 0, 0) ||
        InterlockedCompareExchange(&t->running, 0, 0))
        return 0;

    WdfWorkItemFlush((WDFWORKITEM)c->completion_work_item);
    WdfWorkItemFlush((WDFWORKITEM)c->timeout_work_item);
    return oem_wdf_published_owner_stopped_win(handle, epoch) &&
           oem_wdf_completion_idle_win(handle) &&
           InterlockedCompareExchange(&t->phase, 0, 0) == 2 &&
           InterlockedCompareExchange(&t->flushed, 0, 0) == 1 &&
           InterlockedCompareExchange(&t->accepted, 0, 0) ==
               InterlockedCompareExchange(&t->finished, 0, 0);
}

/* retire completion state on every D0 cycle. WdfWorkItemFlush must run without lifecycle, RSC or
 * scheduler locks needed by its callback. */

int oem_wdf_finalize_published_owner_win(void* handle, uint64_t epoch)
{
    oem_wdf_irq_ctx_t* c;
    oem_wdf_dpc_tracker_win_t* t;
    if (!handle || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || c->owner_epoch != epoch ||
        !c->completion_work_item || !c->bridge || !c->dpc_tracker || !c->irq_mask_owner ||
        c->bridge->epoch != epoch || !c->bridge->pipeline || c->bridge->pipeline->bound ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_closing, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_faulted, 0, 0) != 0 ||
        !oem_irq_mask_read_proof(c->irq_mask_owner, handle, epoch, 1))
        return 0;
    t = c->dpc_tracker;
    if (t->interrupt != handle || t->epoch != epoch ||
        InterlockedCompareExchange(&t->phase, 0, 0) != 2 ||
        InterlockedCompareExchange(&t->flushed, 0, 0) != 1 ||
        InterlockedCompareExchange(&t->outstanding, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->queuing, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->running, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->accepted, 0, 0) !=
            InterlockedCompareExchange(&t->finished, 0, 0))
        return 0;
    /* in-flight WDF callbacks must return before the borrowed bridge and interrupt context can
     * be reused. The owner has already stopped all producers and released pipeline tickets
     * before this blocking flush. */

    WdfWorkItemFlush((WDFWORKITEM)c->completion_work_item);
    WdfWorkItemFlush((WDFWORKITEM)c->timeout_work_item);
    if (!oem_wdf_retire_owner_win(handle, epoch))
        return 0;
    /* reopening is legal ONLY after the actual worker has returned and no old owner pointers
     * remain. */

    InterlockedExchange(&c->completion_closing, 0);
    return 1;
}

static BOOLEAN oem_wdf_bridge_isr(WDFINTERRUPT interrupt, ULONG message_id)
{
    oem_wdf_irq_ctx_t* c = oem_wdf_interrupt_ctx(interrupt);
    oem_irq_claim_t claim;
    UNREFERENCED_PARAMETER(message_id);
    /* the framework interrupt lock is held. An ISR after disable/rundown must not access the ledger
     * even while callback pointers remain pinned. */

    if (!c || c->passive_irq_configured != 1 || !c->owner_epoch || !c->dpc_tracker ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 1 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 1 ||
        InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 1 ||
        c->dpc_tracker->interrupt != (void*)interrupt || c->dpc_tracker->epoch != c->owner_epoch ||
        !c->bridge || c->bridge->epoch != c->owner_epoch)
        return FALSE;
    InterlockedIncrement(&c->tcs_isr_observed);
    claim = oem_irq_bridge_wdf_locked_isr(c->bridge);
    /* wdf holds its interrupt lock already. Never acquire it twice. */

    return claim == OEM_IRQ_UNCLAIMED ? FALSE : TRUE;
}

static VOID oem_wdf_bridge_dpc(WDFINTERRUPT interrupt, WDFOBJECT object)
{
    oem_wdf_irq_ctx_t* c = oem_wdf_interrupt_ctx(interrupt);
    UNREFERENCED_PARAMETER(object);
    if (c && c->dpc_tracker) {
        if (!oem_wdf_dpc_enter_win(c->dpc_tracker, (void*)interrupt))
            return;
        /* an accepted DPC must be allowed to enqueue completion for an ALREADY issued AMC during
         * WDF disable/owner close (phase 2). Otherwise flush_dpcs can strand a power vote that had
         * already triggered. */

        if (c->passive_irq_configured == 1 && c->owner_epoch &&
            c->dpc_tracker->epoch == c->owner_epoch &&
            c->dpc_tracker->interrupt == (void*)interrupt &&
            InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 3 &&
            InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 1 && c->bridge &&
            c->bridge->epoch == c->owner_epoch)
            (void)oem_irq_bridge_dpc(c->bridge);
        (void)oem_wdf_dpc_leave_win(c->dpc_tracker, (void*)interrupt);
    }
    /* no untracked fallback: a DPC with no owner-bound tracker must not touch the pipeline
     * after PnP/owner teardown. */
}

/* kmdf owns its passive callback wait-lock here. Commit changes to the framework grant
 * under the *second*, OEM RPMh command mutex so a concurrent TCS issuer cannot
 * publish/trigger across EvtInterruptDisable. */
int oem_wdf_framework_connected_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    oem_wdf_irq_bridge_t* b;
    int good = 0;
    if (!handle || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1)
        return 0;
    if (!c->owner_epoch && !c->bridge && !c->dpc_tracker && !c->irq_mask_owner &&
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 0) {
        if (InterlockedCompareExchange(&c->framework_enabled, 1, 0) != 0)
            return 0;
        return 1;
    }
    b = c->bridge;
    if (!b || !b->enter || !b->leave_cb || !b->serialize_ctx || !b->enter(b->serialize_ctx))
        return 0;
    if (InterlockedCompareExchange(&c->framework_enabled, 0, 0) == 0 && c->owner_epoch &&
        c->irq_mask_owner && c->dpc_tracker && b->epoch == c->owner_epoch &&
        c->dpc_tracker->epoch == c->owner_epoch && c->dpc_tracker->interrupt == handle &&
        InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) == 1 &&
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 1 &&
        oem_irq_mask_observe_framework(c->irq_mask_owner, handle, c->owner_epoch, 0)) {
        InterlockedExchange(&c->framework_enabled, 1);
        good = 1;
    }
    b->leave_cb(b->serialize_ctx);
    if (!good) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: WDF connect rejected epoch=%llu",
                    (unsigned long long)c->owner_epoch);
        return 0;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: WDF IRQ37 enabled epoch=%llu",
                (unsigned long long)c->owner_epoch);
    return 1;
}

int oem_wdf_framework_disconnected_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    oem_wdf_irq_bridge_t* b;
    int good = 0;
    if (!handle || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1)
        return 0;
    if (!c->owner_epoch && !c->bridge && !c->dpc_tracker && !c->irq_mask_owner &&
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) == 0) {
        InterlockedExchange(&c->framework_enabled, 0);
        return 1;
    }
    b = c->bridge;
    if (!b || !b->enter || !b->leave_cb || !b->serialize_ctx || !b->enter(b->serialize_ctx))
        return 0;
    /* the physical IRQ must already be masked and the AMC src idle; never revoke a live
     * submission or synthesize its completion. */
    if (c->owner_epoch && c->irq_mask_owner && c->dpc_tracker && b->epoch == c->owner_epoch &&
        oem_irq_mask_observe_framework(c->irq_mask_owner, handle, c->owner_epoch, 1)) {
        InterlockedExchange(&c->framework_enabled, 0);
        good = 1;
    }
    b->leave_cb(b->serialize_ctx);
    if (!good) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: WDF disconnect rejected epoch=%llu owner=retained",
                    (unsigned long long)c->owner_epoch);
        return 0;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: WDF IRQ37 masked epoch=%llu",
                (unsigned long long)c->owner_epoch);
    return 1;
}

/* interrupt creation leaves epoch, bridge and tracker unbound; it does not activate RPMh. */

int oem_wdf_create_one_win(void* opaque, const oem_wdf_irq_req_t* req, void** out_handle,
                           oem_wdf_irq_ctx_t** out_ctx)
{
    WDFDEVICE dev = (WDFDEVICE)opaque;
    WDF_INTERRUPT_CONFIG cfg;
    WDF_OBJECT_ATTRIBUTES attrs;
    WDFINTERRUPT interrupt = NULL;
    WDFWORKITEM work_item = NULL;
    WDFWORKITEM timeout_item = NULL;
    WDF_WORKITEM_CONFIG work_cfg;
    WDF_OBJECT_ATTRIBUTES work_attrs;
    amc_wdf_work_ctx_t* work_ctx;
    amc_wdf_work_ctx_t* timeout_ctx;
    oem_wdf_irq_ctx_t* interrupt_ctx;
    NTSTATUS status;
    if (!dev || !req || !out_handle || !out_ctx || !req->interrupt_raw ||
        !req->interrupt_translated)
        return 0;
    *out_handle = NULL;
    *out_ctx = NULL;
    WDF_INTERRUPT_CONFIG_INIT(&cfg, oem_wdf_bridge_isr, oem_wdf_bridge_dpc);
    /* this owner-lifecycle callback coordinates the passive WDF ISR; deferred command
     * submission is serialized through the separate RPMh mutex. */

    cfg.PassiveHandling = TRUE;
    /* rpmh: do not override KMDF/HAL physical IRQ connect/disconnect. */
    cfg.InterruptRaw = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)req->interrupt_raw;
    cfg.InterruptTranslated = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)req->interrupt_translated;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, oem_wdf_irq_ctx_t);
    status = WdfInterruptCreate(dev, &cfg, &attrs, &interrupt);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: interrupt creation failed status=%08X",
                    status);
        return 0;
    }
    interrupt_ctx = oem_wdf_interrupt_ctx(interrupt);
    if (!interrupt_ctx) {
        WdfObjectDelete((WDFOBJECT)interrupt);
        return 0;
    }
    /* queue work only after the owner installs queue_pep_work and pins an active AMC ticket. */

    WDF_WORKITEM_CONFIG_INIT(&work_cfg, oem_wdf_amc_passive_worker);
    work_cfg.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&work_attrs, amc_wdf_work_ctx_t);
    work_attrs.ParentObject = (WDFOBJECT)dev;
    status = WdfWorkItemCreate(&work_cfg, &work_attrs, &work_item);
    if (!NT_SUCCESS(status) || !work_item) {
        WdfObjectDelete((WDFOBJECT)interrupt);
        return 0;
    }
    work_ctx = amc_wdf_work_ctx(work_item);
    if (!work_ctx) {
        WdfObjectDelete((WDFOBJECT)work_item);
        WdfObjectDelete((WDFOBJECT)interrupt);
        return 0;
    }
    work_ctx->interrupt = interrupt;
    WDF_WORKITEM_CONFIG_INIT(&work_cfg, oem_wdf_timeout_passive_worker);
    work_cfg.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&work_attrs, amc_wdf_work_ctx_t);
    work_attrs.ParentObject = (WDFOBJECT)dev;
    status = WdfWorkItemCreate(&work_cfg, &work_attrs, &timeout_item);
    if (!NT_SUCCESS(status) || !timeout_item) {
        WdfObjectDelete((WDFOBJECT)work_item);
        WdfObjectDelete((WDFOBJECT)interrupt);
        return 0;
    }
    timeout_ctx = amc_wdf_work_ctx(timeout_item);
    if (!timeout_ctx) {
        WdfObjectDelete((WDFOBJECT)timeout_item);
        WdfObjectDelete((WDFOBJECT)work_item);
        WdfObjectDelete((WDFOBJECT)interrupt);
        return 0;
    }
    timeout_ctx->interrupt = interrupt;
    interrupt_ctx->timeout_work_item = (void*)timeout_item;
    interrupt_ctx->completion_work_item = (void*)work_item;
    interrupt_ctx->passive_irq_configured = 1;
    *out_handle = (void*)interrupt;
    *out_ctx = interrupt_ctx;
    return 1;
}

/* clear the caller handle only after WDF object release is confirmed. */

int oem_wdf_try_destroy_unpublished_win(void* opaque, void* handle)
{
    oem_wdf_irq_ctx_t* c;
    WDFWORKITEM work;
    UNREFERENCED_PARAMETER(opaque);
    if (!handle || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    /* an owner epoch remains a live publication until the verified native WDF/AMC retirement
     * clears it. */

    if (!c || c->owner_epoch || c->bridge || c->dpc_tracker || c->irq_mask_owner ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0 || !c->timeout_work_item ||
        !oem_wdf_completion_idle_win(handle))
        return 0;

    InterlockedExchange(&c->completion_closing, 1);
    work = (WDFWORKITEM)c->completion_work_item;
    WdfWorkItemFlush(work);
    WdfWorkItemFlush((WDFWORKITEM)c->timeout_work_item);
    /* flush cannot validate the physical owner. Recheck queue state so a protocol error cannot
     * turn an unresolved worker into a freed one. */

    if (c->owner_epoch || c->bridge || c->dpc_tracker || c->irq_mask_owner ||
        InterlockedCompareExchange(&c->completion_faulted, 0, 0) ||
        InterlockedCompareExchange(&c->completion_pending, 0, 0) ||
        InterlockedCompareExchange(&c->completion_queued, 0, 0) ||
        InterlockedCompareExchange(&c->timeout_queued, 0, 0) ||
        InterlockedCompareExchange(&c->timeout_faulted, 0, 0))
        return 0;
    WdfObjectDelete((WDFOBJECT)c->timeout_work_item);
    c->timeout_work_item = NULL;
    c->completion_work_item = NULL;
    WdfObjectDelete((WDFOBJECT)work);
    WdfObjectDelete((WDFOBJECT)handle);
    return 1;
}

/* abi retained for the older portable WDF factory. Its void callback must not be used to
 * infer destruction success in production. */

void oem_wdf_destroy_unpublished_win(void* opaque, void* handle)
{
    (void)oem_wdf_try_destroy_unpublished_win(opaque, handle);
}

/* the boot path uses the inactive PnP object only. A subsequent owner must supply
 * synchronized ISR/DPC/AMC completion and prove teardown. */

/* context publication MUST happen while interrupt callbacks are externally stopped. Static
 * tables, PnP IRQ37, and epoch values do not establish ownership. */

/* the old three-step Attach API had no way to leave owner_epoch=0: each attach required an
 * epoch that no production caller could set. There is no fallible step after the last
 * proof and no partial ownership state to unwind. */

int oem_wdf_publish_native_owner_win(void* handle, uint64_t owner_epoch,
                                     oem_wdf_irq_bridge_t* bridge,
                                     oem_wdf_dpc_tracker_win_t* tracker, oem_irq_mask_owner_t* mask)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !owner_epoch || !bridge || !tracker || !mask ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !oem_irq_bridge_valid(bridge) ||
        bridge->epoch != owner_epoch || !bridge->pipeline || !bridge->pipeline->bound ||
        bridge->pipeline->epoch != owner_epoch || tracker->interrupt != handle ||
        tracker->epoch != owner_epoch || InterlockedCompareExchange(&tracker->phase, 0, 0) != 1 ||
        InterlockedCompareExchange(&tracker->outstanding, 0, 0) != 0 ||
        InterlockedCompareExchange(&tracker->queuing, 0, 0) != 0 ||
        InterlockedCompareExchange(&tracker->running, 0, 0) != 0 || mask->epoch != owner_epoch ||
        mask->interrupt != handle || !oem_irq_mask_read_proof(mask, handle, owner_epoch, 1))
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || !c->completion_work_item || !c->timeout_work_item ||
        InterlockedCompareExchange(&c->timeout_queued, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->timeout_faulted, 0, 0) != 0 ||
        c->gic_intid != RPMH_HLOS_INTID || c->driver_id != RPMH_HLOS_DRIVER_ID || c->owner_epoch ||
        c->bridge || c->dpc_tracker || c->irq_mask_owner ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_closing, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_faulted, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_pending, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_queued, 0, 0) != 0)
        return 0;
    /* only the synchronized owner d0 thread invokes this while KMDF's interrupt has not yet
     * been enabled. */

    InterlockedExchange(&c->bridge_admission_fault_reported, 0);
    c->owner_epoch = owner_epoch;
    c->dpc_tracker = tracker;
    c->irq_mask_owner = mask;
    c->bridge = bridge;

    InterlockedExchange(&c->bridge_enabled, 1);
    return 1;
}

int oem_wdf_attach_bridge_win(void* handle, oem_wdf_irq_bridge_t* bridge)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !oem_irq_bridge_valid(bridge))
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || !c->dpc_tracker ||
        c->dpc_tracker->interrupt != handle || c->dpc_tracker->epoch != bridge->epoch ||
        InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 1 ||
        c->owner_epoch != bridge->epoch || c->bridge ||
        /* the bridge, tracker and mask owner must be published in D0Entry, before the KMDF
         * EvtInterruptEnable callback and ISR admission. Do not replace an active interrupt's
         * pointers without a separate ISR-safe publication/rundown protocol. */

        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0)
        return 0;
    c->bridge = bridge;
    InterlockedExchange(&c->bridge_enabled, 1);
    return 1;
}

void oem_wdf_disable_bridge_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle)
        return;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (c)
        InterlockedExchange(&c->bridge_enabled, 0);
}

int oem_wdf_run_amc_worker_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || !c->bridge)
        return 0;
    return oem_irq_bridge_worker(c->bridge);
}

int oem_wdf_attach_dpc_tracker_win(void* handle, oem_wdf_dpc_tracker_win_t* t)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !t)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || !c->owner_epoch || c->owner_epoch != t->epoch ||
        t->interrupt != handle || c->dpc_tracker || c->bridge ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&t->phase, 0, 0) != 1)
        return 0;
    c->dpc_tracker = t;
    return 1;
}

int oem_wdf_attach_irq_mask_owner_win(void* handle, oem_irq_mask_owner_t* o)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle || !o)
        return 0;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    if (!c || c->passive_irq_configured != 1 || !c->owner_epoch || c->irq_mask_owner ||
        InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 0 ||
        !oem_irq_mask_read_proof(o, handle, c->owner_epoch, 1))
        return 0;
    c->irq_mask_owner = o;
    return 1;
}

/* caller holds the RPMh mutex. Framework enable/disable uses the same mutex; never invert
 * WDF-ISR -> RPMh lock order. This check does not replace epoch, ledger or TCS proof. */
int oem_wdf_check_tcs_command_owner_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    WDFINTERRUPT irq = (WDFINTERRUPT)handle;
    if (!irq)
        return 0;
    c = oem_wdf_interrupt_ctx(irq);
    if (!c)
        return 0;
    InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_NONE);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || c->passive_irq_configured != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_CTX);
        return 0;
    }
    if (!c->owner_epoch || !c->bridge || !c->dpc_tracker || c->dpc_tracker->interrupt != handle ||
        c->dpc_tracker->epoch != c->owner_epoch) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_OWNER);
        return 0;
    }
    if (InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_FRAMEWORK);
        return 0;
    }
    if (InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_TRACKER);
        return 0;
    }
    if (InterlockedCompareExchange(&c->completion_closing, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->completion_faulted, 0, 0) != 0 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 1 ||
        c->bridge->epoch != c->owner_epoch || !oem_irq_bridge_valid(c->bridge)) {
        /* the OEM RPMh command mutex is held by this caller. snapshot the precise state which
         * first invalidated the interrupt/completion contract. An idle TCS after this event is NOT
         * a valid reset grant. */
        if (InterlockedCompareExchange(&c->bridge_admission_fault_reported, 1, 0) == 0) {
            const rpmh_irq_pipeline_t* p = c->bridge->pipeline;
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_RPMH,
                        "rpmh: IRQ bridge rejected epoch=%llx WDF=%ld bridge=%ld closing=%ld "
                        "work-fault=%ld bridge-epoch=%llx pipeline=%p initialized=%u bound=%u "
                        "pipeline-fault=%u pipeline-epoch=%llx work=%u pending=%u "
                        "unmask-pending=%u mask-phase=%lu mask-failure=%lu",
                        (unsigned long long)c->owner_epoch,
                        InterlockedCompareExchange(&c->framework_enabled, 0, 0),
                        InterlockedCompareExchange(&c->bridge_enabled, 0, 0),
                        InterlockedCompareExchange(&c->completion_closing, 0, 0),
                        InterlockedCompareExchange(&c->completion_faulted, 0, 0),
                        (unsigned long long)c->bridge->epoch, (void*)p,
                        (unsigned)(p ? p->initialized : 0u), (unsigned)(p ? p->bound : 0u),
                        (unsigned)(p ? p->faulted : 0u), (unsigned long long)(p ? p->epoch : 0u),
                        (unsigned)(p ? p->work.state : 0u), (unsigned)(p ? p->pending.active : 0u),
                        (unsigned)(p ? p->unmask_finalizing.active : 0u),
                        (ULONG)(c->irq_mask_owner ? c->irq_mask_owner->phase : 0u),
                        (ULONG)(c->irq_mask_owner ? c->irq_mask_owner->last_failure : 0u));
        }
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_BRIDGE);
        return 0;
    }
    /* physical AMC completion may precede RSC src restoration. Keep the terminal token
     * exclusive until its hardware proof and retirement. */
    if (c->bridge->pipeline && c->bridge->pipeline->unmask_finalizing.active) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_BRIDGE);
        return 0;
    }
    return 1;
}

void oem_wdf_leave_tcs_command_owner_win(void* handle)
{
    /* the caller retains its io.lock until the physical transaction ends. This no-op releases
     * NO WDF lock and NO owner mutex. */
    UNREFERENCED_PARAMETER(handle);
}

/* legacy WDF passive try-lock helper is used only for verified shutdown pre-disable, not
 * for live command submission. acquires this FIRST and then the RPMh mutex to preserve the
 * ISR lock order. */

uint32_t oem_wdf_read_last_tcs_lock_refusal_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    if (!handle)
        return AMC_BUSY_IRQ_CTX;
    c = oem_wdf_interrupt_ctx((WDFINTERRUPT)handle);
    return c ? (uint32_t)InterlockedCompareExchange(&c->last_tcs_irq_lock_refusal, 0, 0)
             : AMC_BUSY_IRQ_CTX;
}

int oem_wdf_enter_tcs_irq_lock_win(void* handle)
{
    oem_wdf_irq_ctx_t* c;
    WDFINTERRUPT interrupt = (WDFINTERRUPT)handle;
    if (!interrupt)
        return 0;
    c = oem_wdf_interrupt_ctx(interrupt);
    if (!c)
        return 0;
    InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_NONE);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || c->passive_irq_configured != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_CTX);
        return 0;
    }
    if (!c->owner_epoch || !c->bridge || !c->dpc_tracker || c->dpc_tracker->interrupt != handle ||
        c->dpc_tracker->epoch != c->owner_epoch) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_OWNER);
        return 0;
    }
    if (InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_FRAMEWORK);
        return 0;
    }
    if (InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 1) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_TRACKER);
        return 0;
    }
    if (InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 1 ||
        !oem_irq_bridge_valid(c->bridge) || c->bridge->epoch != c->owner_epoch) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_BRIDGE);
        return 0;
    }
    /* wdf explicitly requires this non-blocking form on arbitrary DPM/PoFx threads. Do not
     * replace it with AcquireLock, which can deadlock if the caller is re-entered from an
     * ISR's synchronous completion path. */
    if (!WdfInterruptTryToAcquireLock(interrupt)) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_TRY_CONTENDED);
        return 0;
    }
    /* the lock is held; teardown can have revoked this owner while acquiring. */
    if (InterlockedCompareExchange(&c->framework_enabled, 0, 0) != 1 ||
        InterlockedCompareExchange(&c->bridge_enabled, 0, 0) != 1 || !c->dpc_tracker ||
        !c->bridge || InterlockedCompareExchange(&c->dpc_tracker->phase, 0, 0) != 1 ||
        c->dpc_tracker->epoch != c->owner_epoch || !oem_irq_bridge_valid(c->bridge) ||
        c->bridge->epoch != c->owner_epoch) {
        InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_IRQ_REVOKED);
        WdfInterruptReleaseLock(interrupt);
        return 0;
    }
    InterlockedExchange(&c->last_tcs_irq_lock_refusal, AMC_BUSY_NONE);
    return 1;
}

void oem_wdf_leave_tcs_irq_lock_win(void* handle)
{
    /* only called after enter returned 1; owner pins WDF object until exit. PASSIVE_LEVEL:
     * release exactly the passive ISR wait lock acquired above. */

    WdfInterruptReleaseLock((WDFINTERRUPT)handle);
}
