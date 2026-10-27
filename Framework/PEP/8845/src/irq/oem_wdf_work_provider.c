/* runnable WDF work-item provider, intentionally NOT installed in the production PEP or
 * DRV2 PnP path. WDF's callback is PASSIVE_LEVEL; an external owner must prove work is
 * drained before tearing down interrupt contexts and freeing this provider. */

#include <ntddk.h>
#include <wdf.h>
#include "oem_wdf_work_provider.h"

#define OEM_WORK_TAG 0x35575051u

typedef struct oem_wdf_work_entry {
    WDFWORKITEM work_item;
    WDFDEVICE parent_dev; /* must match the creating KMDF provider. */
    KSPIN_LOCK lock;
    oem_rpmh_work_cfg_t config;
    ULONG pending;
    ULONG queued;
    ULONG faulted;
    ULONG drained;
} oem_wdf_work_entry_t;

typedef struct oem_wdf_work_ctx {
    oem_wdf_work_entry_t* owner;
} oem_wdf_work_ctx_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(oem_wdf_work_ctx_t, oem_wdf_work_ctx);

static VOID oem_wdf_work_callback(WDFWORKITEM work_item)
{
    oem_wdf_work_ctx_t* c = oem_wdf_work_ctx(work_item);
    oem_wdf_work_entry_t* w = c ? c->owner : NULL;
    ULONG pass;
    if (!w)
        return;
    for (pass = 0; pass < OEM_WDF_WORK_PASSES_MAX; ++pass) {
        KIRQL old_irql;
        KeAcquireSpinLock(&w->lock, &old_irql);
        if (w->pending == 0) {
            /* under same lock as enqueue: do not lose the last wakeup. */
            w->queued = 0;
            KeReleaseSpinLock(&w->lock, old_irql);
            return;
        }
        --w->pending;
        KeReleaseSpinLock(&w->lock, old_irql);
        w->config.handler(w->config.handler_param);
    }

    {
        KIRQL old_irql;
        KeAcquireSpinLock(&w->lock, &old_irql);
        if (w->pending)
            w->faulted = 1;
        w->queued = 0;
        KeReleaseSpinLock(&w->lock, old_irql);
    }
}

static int work_create(void* ctx, const oem_rpmh_work_cfg_t* config, void** out)
{
    oem_wdf_work_provider_t* provider = ctx;
    oem_wdf_work_entry_t* w;
    oem_wdf_work_ctx_t* wctx;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG work_cfg;
    NTSTATUS status;
    if (!provider || !provider->parent_dev || !config || !out || !config->handler ||
        config->work_type != OEM_RPMH_WORK_CRITICAL)
        return 0;
    *out = NULL;
    w = (oem_wdf_work_entry_t*)ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*w), OEM_WORK_TAG);
    if (!w)
        return 0;
    RtlZeroMemory(w, sizeof(*w));
    KeInitializeSpinLock(&w->lock);
    w->config = *config;
    w->parent_dev = (WDFDEVICE)provider->parent_dev;
    WDF_WORKITEM_CONFIG_INIT(&work_cfg, oem_wdf_work_callback);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, oem_wdf_work_ctx_t);
    attributes.ParentObject = (WDFOBJECT)provider->parent_dev;
    status = WdfWorkItemCreate(&work_cfg, &attributes, &w->work_item);
    if (!NT_SUCCESS(status) || !w->work_item) {
        ExFreePoolWithTag(w, OEM_WORK_TAG);
        return 0;
    }
    wctx = oem_wdf_work_ctx(w->work_item);
    if (!wctx) {
        WdfObjectDelete((WDFOBJECT)w->work_item);
        ExFreePoolWithTag(w, OEM_WORK_TAG);
        return 0;
    }
    wctx->owner = w;
    *out = w;
    return 1;
}

static int work_enqueue(void* ctx, void* handle)
{
    oem_wdf_work_entry_t* w = handle;
    ULONG schedule = 0;
    KIRQL old_irql;
    UNREFERENCED_PARAMETER(ctx);
    if (!w)
        return 0;
    KeAcquireSpinLock(&w->lock, &old_irql);
    if (w->faulted || w->drained || w->pending >= OEM_WDF_WORK_PENDING_MAX) {
        KeReleaseSpinLock(&w->lock, old_irql);
        return 0;
    }
    ++w->pending;
    if (!w->queued) {
        w->queued = 1;
        schedule = 1;
    }
    KeReleaseSpinLock(&w->lock, old_irql);
    if (schedule)
        WdfWorkItemEnqueue(w->work_item);
    return 1;
}

int oem_wdf_work_flush_reusable_win(void* provider, void* handle)
{
    oem_wdf_work_provider_t* p = provider;
    oem_wdf_work_entry_t* w = handle;
    KIRQL old_irql;
    int ok;
    if (!p || !p->parent_dev || !w || !w->work_item || w->parent_dev != (WDFDEVICE)p->parent_dev ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    /* no new admissions while flushing: external fence holds. */

    WdfWorkItemFlush(w->work_item);
    KeAcquireSpinLock(&w->lock, &old_irql);
    ok = !w->faulted && !w->drained && !w->queued && !w->pending;
    KeReleaseSpinLock(&w->lock, old_irql);
    return ok;
}

static int work_wait(void* ctx, void* handle)
{
    oem_wdf_work_entry_t* w = handle;
    KIRQL old_irql;
    int drained;
    UNREFERENCED_PARAMETER(ctx);
    if (!w)
        return 0;
    /* passive ONLY; never wait in EvtWorkItem, ISR, DPC or under a lock. */

    WdfWorkItemFlush(w->work_item);
    KeAcquireSpinLock(&w->lock, &old_irql);
    drained = !w->faulted && !w->pending && !w->queued;
    if (drained)
        w->drained = 1;
    KeReleaseSpinLock(&w->lock, old_irql);
    return drained;
}

static int work_destroy(void* ctx, void* handle)
{
    oem_wdf_work_entry_t* w = handle;
    KIRQL old_irql;
    int allowed;
    UNREFERENCED_PARAMETER(ctx);
    if (!w)
        return 0;
    KeAcquireSpinLock(&w->lock, &old_irql);
    allowed = w->drained && !w->faulted && !w->pending && !w->queued;
    KeReleaseSpinLock(&w->lock, old_irql);
    if (!allowed)
        return 0;
    WdfObjectDelete((WDFOBJECT)w->work_item);
    ExFreePoolWithTag(w, OEM_WORK_TAG);
    return 1;
}

int oem_wdf_work_provider_init_win(oem_wdf_work_provider_t* p, void* parent_dev)
{
    if (!p || !parent_dev)
        return 0;
    p->parent_dev = parent_dev;
    return 1;
}

oem_rpmh_work_ops_t oem_wdf_work_provider_ops_win(oem_wdf_work_provider_t* p)
{
    oem_rpmh_work_ops_t ops = {0};
    if (p && p->parent_dev) {
        ops.ctx = p;
        ops.create = work_create;
        ops.enqueue = work_enqueue;
        ops.wait = work_wait;
        ops.destroy = work_destroy;
    }
    return ops;
}
