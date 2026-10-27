#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "component_perf.h"
#include "init_gate.h"
#include "peplog.h"
#include "perf_clock_win.h"
#include "power_control.h"
#include "resource_cache.h"
#include "xo_diag.h"
#include "dispatch/dstate_types.h"
#include "dispatch/fstate_types.h"
#include "dispatch/node_types.h"
#include "dispatch/work_types.h"
#include "dispatch/dstate_priv.h"
#include "dispatch/dump_priv.h"
#include "dispatch/fstate_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#include "dispatch/pstate_priv.h"
#include "dispatch/work_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

BOOLEAN pep_work_init_if_needed(VOID);
static BOOLEAN pep_work_enter(VOID);
static VOID pep_work_leave(VOID);
BOOLEAN pep_power_ctrl_ctx_init_if_needed(VOID);
pep_work_power_control_ctx_t* pep_alloc_power_control_ctx(VOID);
VOID pep_free_power_control_ctx(_In_opt_ pep_work_power_control_ctx_t* ctx);
static __forceinline SIZE_T pep_get_work_payload_copy_size(_In_ ULONG work_type);
static pep_req_comp_perf_state_t*
pep_clone_perf_state_work(_In_ const pep_req_comp_perf_state_t* original);
static BOOLEAN pep_clone_power_control_work(_Inout_ pep_work_power_control_ctx_t* ctx,
                                            _In_ const pep_power_control_req_t* original);
static VOID pep_capture_deferred_dev(_Inout_ pep_work_item_t* item);
static BOOLEAN pep_deferred_dev_begin(_In_ pep_work_item_t* item);
static VOID pep_deferred_dev_end(_In_ pep_work_item_t* item);
BOOLEAN pep_sync_dpm_begin(_In_opt_ pep_devnode_t* candidate);
VOID pep_sync_dpm_end(_In_ pep_devnode_t* node);
BOOLEAN pep_enqueue_work(_In_ ULONG work_type, _In_ PVOID data, _In_ BOOLEAN add_to_head);
static pep_work_item_t* pep_dequeue_work(_Out_ PBOOLEAN more);
VOID pep_free_work_item(_In_ pep_work_item_t* item);
BOOLEAN pep_result_init_if_needed(VOID);
PVOID pep_alloc_result(_In_ ULONG size);
static VOID pep_free_result(_In_opt_ PVOID ptr);
static __forceinline VOID pep_fill_power_control_work_info(_Inout_opt_ pep_work_t* w,
                                                           _Inout_opt_ pep_power_control_req_t* r,
                                                           _In_opt_ pep_devnode_t* n);
ULONG_PTR pep_work(_Inout_opt_ PVOID data);
ULONG_PTR pep_power_control_req(_Inout_opt_ PVOID data);
ULONG_PTR pep_power_control_complete(_Inout_opt_ PVOID data);
ULONG_PTR pep_parse_error_data(_Inout_opt_ PVOID data);

static ULONG g_pep_work_outstanding;
static const GUID guid_clock_test = {
    3845751031u, 56204u, 19172u, {136u, 222u, 159u, 241u, 8u, 37u, 202u, 16u}};
static const GUID guid_power_set_selextsource = {
    3382942026u, 59180u, 20083u, {140u, 143u, 65u, 34u, 93u, 205u, 193u, 84u}};
static const GUID guid_log_command = {
    4052892314u, 54781u, 19497u, {167u, 46u, 171u, 201u, 242u, 248u, 163u, 185u}};
static const GUID guid_sdbus_pstate_reg = {
    452966471u, 47163u, 16536u, {188u, 13u, 41u, 53u, 192u, 94u, 198u, 180u}};
static const GUID guid_sdbus_pstate_mmc_info = {
    61598841u, 51386u, 16512u, {181u, 67u, 62u, 151u, 91u, 34u, 253u, 215u}};
static const GUID guid_xhci_sub_dx_state = {
    3234047663u, 2695u, 20289u, {165u, 29u, 33u, 189u, 226u, 235u, 191u, 179u}};

extern __forceinline BOOLEAN pep_work_init_if_needed(VOID)
{
    if (InterlockedCompareExchange(&g_pep_work_shutdown, 0, 0) != 0)
        return FALSE;
    int state = init_gate_begin(&g_pep_work_init);
    if (state != 1)
        return state == 2;
    KeInitializeSpinLock(&g_pep_work_lock);
    InitializeListHead(&g_pep_work_queue);
    g_pep_work_outstanding = 0;
    KeInitializeEvent(&g_pep_work_drained_event, NotificationEvent, TRUE);
    ExInitializeNPagedLookasideList(&g_pep_work_lookaside, NULL, NULL, 0, sizeof(pep_work_item_t),
                                    POOL_TAG, 0);
    /* no other callback can observe an initialized queue until the lock, list head AND
     * lookaside are ready. */

    init_gate_publish(&g_pep_work_init);
    return TRUE;
}

static BOOLEAN pep_work_enter(VOID)
{
    KIRQL irql;
    BOOLEAN admitted = FALSE;
    if (!init_gate_ready(&g_pep_work_init))
        return FALSE;
    KeAcquireSpinLock(&g_pep_work_lock, &irql);
    if (InterlockedCompareExchange(&g_pep_work_shutdown, 0, 0) == 0 &&
        g_pep_work_outstanding != MAXULONG) {
        ++g_pep_work_outstanding;
        KeClearEvent(&g_pep_work_drained_event);
        admitted = TRUE;
    }
    KeReleaseSpinLock(&g_pep_work_lock, irql);
    return admitted;
}

static VOID pep_work_leave(VOID)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_pep_work_lock, &irql);
    NT_ASSERT(g_pep_work_outstanding != 0);
    if (g_pep_work_outstanding != 0 && --g_pep_work_outstanding == 0)
        KeSetEvent(&g_pep_work_drained_event, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&g_pep_work_lock, irql);
}

extern __forceinline BOOLEAN pep_power_ctrl_ctx_init_if_needed(VOID)
{
    int state = init_gate_begin(&g_pep_power_ctrl_ctx_init);
    if (state != 1)
        return state == 2;
    ExInitializeNPagedLookasideList(&g_pep_power_ctrl_ctx_lookaside, NULL, NULL, 0,
                                    sizeof(pep_work_power_control_ctx_t), POOL_TAG, 0);
    init_gate_publish(&g_pep_power_ctrl_ctx_init);
    return TRUE;
}

extern __forceinline pep_work_power_control_ctx_t* pep_alloc_power_control_ctx(VOID)
{
    if (!pep_power_ctrl_ctx_init_if_needed())
        return NULL;
    return (pep_work_power_control_ctx_t*)ExAllocateFromNPagedLookasideList(
        &g_pep_power_ctrl_ctx_lookaside);
}

extern __forceinline VOID pep_free_power_control_ctx(_In_opt_ pep_work_power_control_ctx_t* ctx)
{
    if (!ctx)
        return;
    if (ctx->owned_input) {
        ExFreePool(ctx->owned_input);
        ctx->owned_input = NULL;
    }
    ExFreeToNPagedLookasideList(&g_pep_power_ctrl_ctx_lookaside, ctx);
}

static __forceinline SIZE_T pep_get_work_payload_copy_size(_In_ ULONG work_type)
{
    switch (work_type) {
        case PEP_WORKPAYLOAD_DEVICEPOWERSTATE:
            return sizeof(pep_dev_power_state_t);
        case PEP_WORKPAYLOAD_NOTIFYIDLESTATE:
            return sizeof(pep_notify_comp_idle_state_t);
        case PEP_WORKPAYLOAD_PASSIVECALLBACK:
            return sizeof(pep_passive_callback_t);
            /* it MUST be cloned with the pointed-to request array, below. */

        default:
            return 0;
    }
}

static pep_req_comp_perf_state_t*
pep_clone_perf_state_work(_In_ const pep_req_comp_perf_state_t* original)
{
    pep_req_comp_perf_state_t* copy = NULL;
    __try {
        ULONG cnt = original ? original->perf_requests_cnt : 0u;
        if (cnt >= 1u && cnt <= PEP_MAX_QUEUED_PERF_REQUESTS && original->perf_requests) {
            SIZE_T bytes = sizeof(*copy) + (SIZE_T)cnt * sizeof(pep_comp_perf_state_req_t);
            copy =
                (pep_req_comp_perf_state_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
            if (copy) {
                RtlCopyMemory(copy, original, sizeof(*copy));
                copy->perf_requests = (pep_comp_perf_state_req_t*)(copy + 1);
                RtlCopyMemory(copy->perf_requests, original->perf_requests,
                              (SIZE_T)cnt * sizeof(*copy->perf_requests));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (copy)
            ExFreePool(copy);
        return NULL;
    }
    return copy;
}

static BOOLEAN pep_clone_power_control_work(_Inout_ pep_work_power_control_ctx_t* ctx,
                                            _In_ const pep_power_control_req_t* original)
{
    if (!ctx || !original)
        return FALSE;
    __try {
        RtlCopyMemory(&ctx->req, original, sizeof(ctx->req));
        const GUID* stable_code = pep_stable_queued_power_code(ctx->req.power_control_code);
        if (!stable_code || ctx->req.in_buf_size > PEP_MAX_QUEUED_POWER_INPUT ||
            (ctx->req.in_buf_size != 0u && !ctx->req.in_buf))
            return FALSE;
        ctx->original_input_address = ctx->req.in_buf;
        ctx->original_input_size = ctx->req.in_buf_size;
        ctx->req.power_control_code = stable_code;
        ctx->req.out_buf = NULL;
        if (ctx->req.in_buf_size) {
            ctx->owned_input =
                ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)ctx->req.in_buf_size, POOL_TAG);
            if (!ctx->owned_input)
                return FALSE;
            RtlCopyMemory(ctx->owned_input, ctx->req.in_buf, (SIZE_T)ctx->req.in_buf_size);
            ctx->req.in_buf = ctx->owned_input;
        } else {
            ctx->req.in_buf = NULL;
        }
        return TRUE;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return FALSE;
    }
}

/* framework callback rundown is an independent requirement. */
static VOID pep_capture_deferred_dev(_Inout_ pep_work_item_t* item)
{
    pep_devnode_t* n = NULL;
    KIRQL irql;
    if (!item || !item->data)
        return;
    switch (item->work_type) {
        case PEP_WORKPAYLOAD_DEVICEPOWERSTATE:
            n = (pep_devnode_t*)((pep_dev_power_state_t*)item->data)->dev_handle;
            break;
        case PEP_WORKPAYLOAD_REQUESTPOWERCONTROL:
            n = (pep_devnode_t*)((pep_work_power_control_ctx_t*)item->data)->req.dev_handle;
            break;
        case PEP_WORKPAYLOAD_REQUESTPERFSTATE:
            n = (pep_devnode_t*)((pep_req_comp_perf_state_t*)item->data)->dev_handle;
            break;
        case PEP_WORKPAYLOAD_NOTIFYIDLESTATE:
            n = (pep_devnode_t*)((pep_notify_comp_idle_state_t*)item->data)->dev_handle;
            break;
        default:
            return;
    }
    if (!n)
        return;
    /* dev storage is retained through plugin shutdown; this spin lock is usable at
     * DISPATCH_LEVEL. */

    KeAcquireSpinLock(&n->deferred_work_lock, &irql);
    item->deferred_dev = n;
    item->deferred_generation = (ULONG)n->deferred_generation;
    item->original_kernel_handle = n->kernel_handle;
    KeReleaseSpinLock(&n->deferred_work_lock, irql);
}

static BOOLEAN pep_deferred_dev_begin(_In_ pep_work_item_t* item)
{
    KIRQL irql;
    BOOLEAN admitted = FALSE;
    pep_devnode_t* n = item ? item->deferred_dev : NULL;
    if (!n)
        return FALSE;
    KeAcquireSpinLock(&n->deferred_work_lock, &irql);
    if (!n->deferred_closed && !n->deferred_unregistering &&
        n->reg_state == DEV_REGISTERED_MINI_PEP && item->deferred_generation != 0 &&
        item->deferred_generation == (ULONG)n->deferred_generation &&
        n->deferred_workers != MAXULONG) {
        ++n->deferred_workers;
        KeClearEvent(&n->deferred_work_drained);
        admitted = TRUE;
    }
    KeReleaseSpinLock(&n->deferred_work_lock, irql);
    return admitted;
}

static VOID pep_deferred_dev_end(_In_ pep_work_item_t* item)
{
    KIRQL irql;
    pep_devnode_t* n = item ? item->deferred_dev : NULL;
    if (!n)
        return;
    KeAcquireSpinLock(&n->deferred_work_lock, &irql);
    NT_ASSERT(n->deferred_workers != 0);
    if (n->deferred_workers != 0 && --n->deferred_workers == 0)
        KeSetEvent(&n->deferred_work_drained, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&n->deferred_work_lock, irql);
}

/* hold the SAME per-device rundown count across the entire d0/Dx transaction. The list
 * lock protects handle membership before the first node dereference; the per-node lock
 * serializes admission with pep_unregister_dev. */
BOOLEAN pep_sync_dpm_begin(_In_opt_ pep_devnode_t* candidate)
{
    BOOLEAN admitted = FALSE;
    KIRQL irq;
    if (!candidate || !g_pep_dev_list_lock || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (pep_devnode_t* it = g_pep_dev_list_head; it; it = it->next) {
        if (it != candidate)
            continue;
        KeAcquireSpinLock(&it->deferred_work_lock, &irq);
        if (it->reg_state == DEV_REGISTERED_MINI_PEP && !it->deferred_closed &&
            !it->deferred_unregistering && it->deferred_generation != 0 &&
            it->deferred_workers != MAXULONG) {
            ++it->deferred_workers;
            KeClearEvent(&it->deferred_work_drained);
            admitted = TRUE;
        }
        KeReleaseSpinLock(&it->deferred_work_lock, irq);
        break;
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return admitted;
}

VOID pep_sync_dpm_end(_In_ pep_devnode_t* node)
{

    pep_work_item_t ticket;
    RtlZeroMemory(&ticket, sizeof(ticket));
    ticket.deferred_dev = node;
    pep_deferred_dev_end(&ticket);
}

BOOLEAN pep_enqueue_work(_In_ ULONG work_type, _In_ PVOID data, _In_ BOOLEAN add_to_head)
{
    if (work_type == PEP_WORKPAYLOAD_REQUESTPOWERCONTROL && !data)
        return FALSE;
    if (!pep_work_init_if_needed() || !pep_work_enter())
        return FALSE;
    pep_work_item_t* item =
        (pep_work_item_t*)ExAllocateFromNPagedLookasideList(&g_pep_work_lookaside);
    if (!item) {
        pep_work_leave();
        return FALSE;
    }
    RtlZeroMemory(item, sizeof(*item));
    item->work_type = work_type;
    item->data = data;
    item->data_owned = 0;

    SIZE_T copy_size = pep_get_work_payload_copy_size(work_type);
    if (copy_size != 0 && data != NULL) {
        PVOID copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, copy_size, POOL_TAG);
        if (!copy) {
            ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
            pep_work_leave();
            return FALSE;
        }

        __try {
            RtlCopyMemory(copy, data, copy_size);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ExFreePool(copy);
            ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
            pep_work_leave();
            return FALSE;
        }

        item->data = copy;
        item->data_owned = 1;
    }

    /* the only ownership-safe representation of a deferred perf request is its deep copy. */

    if (work_type == PEP_WORKPAYLOAD_REQUESTPERFSTATE) {
        item->data = pep_clone_perf_state_work((const pep_req_comp_perf_state_t*)data);
        if (!item->data) {
            ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
            pep_work_leave();
            return FALSE;
        }
        item->data_owned = 1;
    }

    if (work_type == PEP_WORKPAYLOAD_REQUESTPOWERCONTROL && data) {
        pep_work_power_control_ctx_t* ctx = pep_alloc_power_control_ctx();
        if (!ctx) {
            ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
            pep_work_leave();
            return FALSE;
        }
        RtlZeroMemory(ctx, sizeof(*ctx));
        if (!pep_clone_power_control_work(ctx, (const pep_power_control_req_t*)data)) {
            pep_free_power_control_ctx(ctx);
            ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
            pep_work_leave();
            return FALSE;
        }

        item->data = ctx;
        item->data_owned = 0;
    }
    pep_capture_deferred_dev(item);

    const ULONG_PTR payload_log_identity = (ULONG_PTR)item->data;
    KIRQL irql;
    BOOLEAN accepted = FALSE;
    KeAcquireSpinLock(&g_pep_work_lock, &irql);
    if (InterlockedCompareExchange(&g_pep_work_shutdown, 0, 0) == 0) {
        if (add_to_head)
            InsertHeadList(&g_pep_work_queue, &item->link);
        else
            InsertTailList(&g_pep_work_queue, &item->link);
        accepted = TRUE;
    }
    KeReleaseSpinLock(&g_pep_work_lock, irql);
    if (!accepted) {
        if (work_type == PEP_WORKPAYLOAD_REQUESTPOWERCONTROL && item->data)
            pep_free_power_control_ctx((pep_work_power_control_ctx_t*)item->data);
        else if (item->data_owned && item->data)
            ExFreePool(item->data);
        ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
        pep_work_leave();
        return FALSE;
    }
    {
        ULONG lo = (ULONG)payload_log_identity;
        ULONG hi = (ULONG)(payload_log_identity >> 32);
        pep_log_write(PEPLOG_EVT_WORK, work_type, STATUS_SUCCESS, lo, hi, 0, 0);
    }
    pep_req_kernel_work();
    pep_work_leave();
    return TRUE;
}

static pep_work_item_t* pep_dequeue_work(_Out_ PBOOLEAN more)
{
    *more = FALSE;
    if (!init_gate_ready(&g_pep_work_init))
        return NULL;
    KIRQL irql;
    KeAcquireSpinLock(&g_pep_work_lock, &irql);
    if (InterlockedCompareExchange(&g_pep_work_shutdown, 0, 0) != 0 ||
        IsListEmpty(&g_pep_work_queue) || g_pep_work_outstanding == MAXULONG) {
        KeReleaseSpinLock(&g_pep_work_lock, irql);
        return NULL;
    }
    ++g_pep_work_outstanding;
    KeClearEvent(&g_pep_work_drained_event);
    PLIST_ENTRY e = RemoveHeadList(&g_pep_work_queue);
    *more = !IsListEmpty(&g_pep_work_queue);
    KeReleaseSpinLock(&g_pep_work_lock, irql);
    return CONTAINING_RECORD(e, pep_work_item_t, link);
}

extern __forceinline VOID pep_free_work_item(_In_ pep_work_item_t* item)
{
    if (!item)
        return;
    ExFreeToNPagedLookasideList(&g_pep_work_lookaside, item);
}

extern __forceinline BOOLEAN pep_result_init_if_needed(VOID)
{
    int state = init_gate_begin(&g_pep_result_init);
    if (state != 1)
        return state == 2;
    ExInitializeNPagedLookasideList(&g_pep_result_tiny_lookaside, NULL, NULL, 0,
                                    sizeof(pep_result_alloc_t) + PEP_RESULT_TINY_CAP, POOL_TAG, 0);
    ExInitializeNPagedLookasideList(&g_pep_result_small_lookaside, NULL, NULL, 0,
                                    sizeof(pep_result_alloc_t) + PEP_RESULT_SMALL_CAP, POOL_TAG, 0);
    ExInitializeNPagedLookasideList(&g_pep_result_large_lookaside, NULL, NULL, 0,
                                    sizeof(pep_result_alloc_t) + PEP_RESULT_LARGE_CAP, POOL_TAG, 0);
    init_gate_publish(&g_pep_result_init);
    return TRUE;
}

PVOID pep_alloc_result(_In_ ULONG size)
{
    if (size == 0)
        size = 1;

    if (!pep_result_init_if_needed())
        return NULL;

    pep_result_alloc_t* a = NULL;
    ULONG cap = size;
    ULONG flags = 0;

    if (size <= PEP_RESULT_TINY_CAP) {
        a = (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_tiny_lookaside);
        cap = PEP_RESULT_TINY_CAP;
        flags = PEP_RESULT_FLAG_TINY;
    } else if (size <= PEP_RESULT_SMALL_CAP) {
        a = (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_small_lookaside);
        cap = PEP_RESULT_SMALL_CAP;
        flags = PEP_RESULT_FLAG_SMALL;
    } else if (size <= PEP_RESULT_LARGE_CAP) {
        a = (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_large_lookaside);
        cap = PEP_RESULT_LARGE_CAP;
        flags = PEP_RESULT_FLAG_LARGE;
    } else {
        ULONG total = sizeof(pep_result_alloc_t) + size;
        a = (pep_result_alloc_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, total, POOL_TAG);
        cap = size;
        flags = 0;
    }

    if (!a)
        return NULL;

    a->tag = 'PpeQ';
    a->size = size;
    a->capacity = cap;
    a->flags = flags;

    PVOID p = (PVOID)(a + 1);
    RtlZeroMemory(p, cap);
    return p;
}

static VOID pep_free_result(_In_opt_ PVOID ptr)
{
    if (!ptr)
        return;

    pep_result_alloc_t* a = (pep_result_alloc_t*)((UCHAR*)ptr - sizeof(pep_result_alloc_t));
    if (a->tag != 'PpeQ')
        return;

    ULONG flags = a->flags;
    a->tag = 0;

    if (flags & PEP_RESULT_FLAG_TINY)
        ExFreeToNPagedLookasideList(&g_pep_result_tiny_lookaside, a);
    else if (flags & PEP_RESULT_FLAG_SMALL)
        ExFreeToNPagedLookasideList(&g_pep_result_small_lookaside, a);
    else if (flags & PEP_RESULT_FLAG_LARGE)
        ExFreeToNPagedLookasideList(&g_pep_result_large_lookaside, a);
    else
        ExFreePool(a);
}

static __forceinline VOID pep_fill_power_control_work_info(_Inout_opt_ pep_work_t* w,
                                                           _Inout_opt_ pep_power_control_req_t* r,
                                                           _In_opt_ pep_devnode_t* n)
{
    if (!w || !w->work_information || !r)
        return;

    if (w->work_information->work_type == PepWorkRequestPowerControl)
        return;

    SIZE_T bytes = (SIZE_T)r->bytes_returned;
    PVOID out_copy = NULL;

    if (bytes > 4096)
        bytes = 4096;

    if (bytes && r->out_buf) {
        out_copy = pep_alloc_result((ULONG)bytes);
        if (out_copy) {
            __try {
                RtlCopyMemory(out_copy, r->out_buf, bytes);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                pep_free_result(out_copy);
                out_copy = NULL;
                bytes = 0;
            }
        }
    }

    if (!out_copy) {

        bytes = 12;
        out_copy = pep_alloc_result((ULONG)bytes);
        if (!out_copy)
            return;
        ((ULONG*)out_copy)[0] = 3;
        ((ULONG*)out_copy)[1] = (ULONG)r->status;
        ((ULONG*)out_copy)[2] = 0;
    }

    w->work_information->work_type = PepWorkRequestPowerControl;
    w->work_information->u1.power_control.dev_handle = n ? n->kernel_handle : NULL;
    w->work_information->u1.power_control.power_control_code = r->power_control_code;
    w->work_information->u1.power_control.req_ctx = out_copy;
    w->work_information->u1.power_control.in_buf = r->in_buf;
    w->work_information->u1.power_control.in_buf_size = r->in_buf_size;
    w->work_information->u1.power_control.out_buf = out_copy;
    w->work_information->u1.power_control.out_buf_size = (ULONGLONG)bytes;
}

ULONG_PTR pep_work(_Inout_opt_ PVOID data)
{
    pep_work_t* w = (pep_work_t*)data;
    if (!w)
        pep_bug_check(22u, data);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        pep_bug_check(22u, w);

    if (w->work_information)
        RtlZeroMemory(w->work_information, sizeof(*w->work_information));

    pep_flush_pending_vetoes();

    BOOLEAN more = FALSE;
    pep_work_item_t* item = pep_dequeue_work(&more);
    if (!item) {
        w->need_work = 0;
        return 0;
    }

    UCHAR need_work = 0;

    if (item->work_type == PEP_WORKPAYLOAD_DEVICEPOWERSTATE && item->data) {
        if (pep_deferred_dev_begin(item)) {
            pep_dev_power_state_work((pep_dev_power_state_t*)item->data);
            pep_deferred_dev_end(item);
        }
    } else if (item->work_type == PEP_WORKPAYLOAD_REQUESTPOWERCONTROL && item->data) {
        need_work = 1;
        /* power-control requests can be queued when called at elevated IRQL. */

        pep_work_power_control_ctx_t* ctx = (pep_work_power_control_ctx_t*)item->data;
        pep_power_control_req_t* r = ctx ? &ctx->req : NULL;
        if (r) {
            pep_devnode_t* n = (pep_devnode_t*)r->dev_handle;
            BOOLEAN valid_dev_work = pep_deferred_dev_begin(item);

            /* run handlers into a temporary buf so don't depend on the caller's out_buf. */
            UCHAR tmp_out[4096];
            PVOID saved_out = r->out_buf;
            ULONGLONG saved_out_size = r->out_buf_size;
            ULONGLONG saved_bytes = r->bytes_returned;
            NTSTATUS saved_status = r->status;
            /* the original pointer is an opaque completion identifier. */

            PVOID saved_in = ctx->original_input_address;
            ULONGLONG saved_in_size = ctx->original_input_size;

            r->out_buf = tmp_out;
            r->out_buf_size = sizeof(tmp_out);
            r->bytes_returned = 0;
            r->status = STATUS_SUCCESS;

            if (!valid_dev_work) {
                r->status = STATUS_INVALID_DEVICE_STATE;
                r->bytes_returned = 0;
            } else if (IsEqualGUID(r->power_control_code, &guid_power_change_p_state))
                pep_handle_p_state_req(n, r, w);
            else if (IsEqualGUID(r->power_control_code, &guid_power_change_p_state_v2))
                pep_handle_p_state_v2_req(n, r, w);
            else if (IsEqualGUID(r->power_control_code, &guid_power_set_selextsource)) {
                size_t written = 0;
                r->status = power_ctrl_handle_select_external_src(
                    n ? n->dev_id : 0, r->in_buf, (size_t)r->in_buf_size, r->out_buf,
                    (size_t)r->out_buf_size, &written);
                r->bytes_returned = written;
            } else if (IsEqualGUID(r->power_control_code, &guid_log_command)) {
                size_t written = 0;
                r->status =
                    power_ctrl_handle_log_command(r->in_buf, (size_t)r->in_buf_size, r->out_buf,
                                                  (size_t)r->out_buf_size, &written);
                r->bytes_returned = written;
            } else if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_reg))
                r->status = pep_is_msft_sd_p_states_enabled() ? pep_handle_sdbus_reg(n, r)
                                                              : STATUS_NOT_SUPPORTED;
            else if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_req))
                r->status = pep_is_msft_sd_p_states_enabled() ? pep_handle_sdbus_req(n, r)
                                                              : STATUS_NOT_SUPPORTED;
            else if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_mmc_info))
                r->status = pep_is_msft_sd_p_states_enabled() ? pep_handle_sdbus_mmc_info(n, r)
                                                              : STATUS_NOT_SUPPORTED;
            else {
                r->status = STATUS_NOT_SUPPORTED;
                r->bytes_returned = 0;
            }

            /* never restore the borrowed pointer into the work request. */

            pep_fill_power_control_work_info(w, r, n);
            if (w->work_information &&
                w->work_information->work_type == PepWorkRequestPowerControl) {
                w->work_information->u1.power_control.dev_handle = item->original_kernel_handle;
                w->work_information->u1.power_control.in_buf = saved_in;
                w->work_information->u1.power_control.in_buf_size = saved_in_size;
            }

            /* restore caller-provided buf pointers (never leave stack pointers in the request). */
            r->out_buf = saved_out;
            r->out_buf_size = saved_out_size;
            r->bytes_returned = saved_bytes;
            r->status = saved_status;

            if (valid_dev_work)
                pep_deferred_dev_end(item);
            pep_free_power_control_ctx(ctx);
        } else {
            if (ctx)
                pep_free_power_control_ctx(ctx);
        }
    } else if (item->work_type == PEP_WORKPAYLOAD_REQUESTPERFSTATE && item->data) {
        need_work = 1;
        pep_req_comp_perf_state_t* r = (pep_req_comp_perf_state_t*)item->data;
        pep_devnode_t* n = (pep_devnode_t*)r->dev_handle;
        BOOLEAN succeeded = FALSE;
        BOOLEAN valid_dev_work = pep_deferred_dev_begin(item);
        /* never rely on the original PoFx notify pointer lifetime. */

        if (valid_dev_work && item->data_owned &&
            r->perf_requests == (pep_comp_perf_state_req_t*)(r + 1) && r->perf_requests_cnt >= 1u &&
            r->perf_requests_cnt <= PEP_MAX_QUEUED_PERF_REQUESTS && n && r->comp < n->comp_cnt &&
            (n->clock_perf || n->native_perf || comp_perf_hardware_backend_ready())) {
            if (n->clock_perf)
                succeeded = clock_perf_handle(n->clock_perf, 33u, r) && r->succeeded;
            else if (n->native_perf)
                succeeded = pep_native_perf_req(n, r);
            else
                succeeded = comp_perf_req_state(&n->comp_perf_ctx, r) && r->succeeded;
        }
        if (valid_dev_work)
            pep_deferred_dev_end(item);
        r->completed = 1;
        r->succeeded = succeeded ? 1 : 0;
        if (w && w->work_information) {
            w->work_information->work_type = PepWorkCompletePerfState;
            w->work_information->u1.complete_perf_state.dev_handle = item->original_kernel_handle;
            w->work_information->u1.complete_perf_state.comp = r->comp;
            w->work_information->u1.complete_perf_state.succeeded = succeeded ? 1 : 0;
        }
    } else if (item->work_type == PEP_WORKPAYLOAD_NOTIFYIDLESTATE && item->data) {
        need_work = 1;
        pep_notify_comp_idle_state_t* s = (pep_notify_comp_idle_state_t*)item->data;
        if (pep_deferred_dev_begin(item)) {
            pep_notify_comp_idle_state_work(s, (w && w->work_information) ? w->work_information
                                                                          : NULL);
            pep_deferred_dev_end(item);
        } else if (w && w->work_information) {
            /* complete the queued notify without applying any F-state transition or provider vote.
             */

            w->work_information->work_type = PepWorkCompleteIdleState;
            w->work_information->u1.active_complete.dev_handle = item->original_kernel_handle;
            w->work_information->u1.active_complete.comp = s->comp;
        }
    } else if (item->work_type == PEP_WORKPAYLOAD_PASSIVECALLBACK && item->data) {
        pep_passive_callback_t* cb = (pep_passive_callback_t*)item->data;
        if (cb->callback)
            cb->callback(cb->ctx);
    }

    if (item->data_owned && item->data)
        ExFreePool(item->data);

    pep_free_work_item(item);

    pep_work_leave();

    w->need_work = need_work;
    if (more)
        pep_req_kernel_work();

    return 1;
}

ULONG_PTR pep_power_control_req(_Inout_opt_ PVOID data)
{
    pep_power_control_req_t* r = (pep_power_control_req_t*)data;
    if (!r)
        pep_bug_check_power_control(data);

    r->status = STATUS_SUCCESS;
    r->bytes_returned = 0;

    if (!r->dev_handle) {
        pep_bug_check_power_control(r);
    }

    if (!r->power_control_code)
        pep_bug_check_power_control(r);

    pep_devnode_t* n = (pep_devnode_t*)r->dev_handle;
    BOOLEAN is_passive = (KeGetCurrentIrql() == PASSIVE_LEVEL);

    {
        const GUID* g = r->power_control_code;
        ULONG sig = g ? g->Data1 : 0;
        ULONG sig2 = g ? (((ULONG)g->Data2) << 16) | (ULONG)g->Data3 : 0;
        pep_log_write(PEPLOG_EVT_POWER_CONTROL, sig, STATUS_SUCCESS, sig2, (ULONG)is_passive,
                      (ULONG)r->in_buf_size, (ULONG)r->out_buf_size);
    }

    if (IsEqualGUID(r->power_control_code, &guid_power_change_p_state)) {
        if (is_passive) {
            pep_handle_p_state_req(n, r, NULL);
            return 1;
        }
        if (!r->out_buf || r->out_buf_size < 12)
            pep_bug_check_power_control(r);
        ULONG* out = (ULONG*)r->out_buf;
        out[0] = 1;
        out[1] = 0;
        out[2] = 0;
        r->bytes_returned = 12;
        r->status = 0;
        if (!pep_enqueue_work(PEP_WORKPAYLOAD_REQUESTPOWERCONTROL, r, FALSE))
            pep_bug_check_power_control(r);
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_power_change_p_state_v2)) {
        if (is_passive) {
            pep_handle_p_state_v2_req(n, r, NULL);
            return 1;
        }
        if (!r->in_buf || !r->out_buf || r->in_buf_size < 24 || r->out_buf_size < 24)
            pep_bug_check_power_control(r);
        ULONG* out = (ULONG*)r->out_buf;
        ULONG* in = (ULONG*)r->in_buf;
        out[0] = 2;
        out[1] = in[1];
        out[2] = 0;
        out[3] = 1;
        *((ULONGLONG*)out + 2) = *((ULONGLONG*)in + 2);
        r->bytes_returned = 24;
        r->status = 0;
        if (!pep_enqueue_work(PEP_WORKPAYLOAD_REQUESTPOWERCONTROL, r, FALSE))
            pep_bug_check_power_control(r);
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_clock_test)) {
        /* a PEP power-control callback may execute above PASSIVE_LEVEL: never call it there and
         * never ACK absent/malformed input. */

        LONG req = 0;
        if (!is_passive)
            r->status = STATUS_INVALID_DEVICE_STATE;
        else if (!r->in_buf || r->in_buf_size < sizeof(req))
            r->status = STATUS_INVALID_PARAMETER;
        else {
            RtlCopyMemory(&req, r->in_buf, sizeof(req));
            xo_test_handle_req(req);
            r->status = STATUS_SUCCESS; /* gate API cannot confirm acquisition. */
        }
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_power_set_selextsource)) {
        if (!is_passive) {
            /* reject invalid IRQL on this path. */
            pep_bug_check_power_control(r);
        }

        size_t written = 0;
        r->status = power_ctrl_handle_select_external_src(n ? n->dev_id : 0, r->in_buf,
                                                          (size_t)r->in_buf_size, r->out_buf,
                                                          (size_t)r->out_buf_size, &written);
        r->bytes_returned = written;
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_log_command)) {
        if (!is_passive) {
            /* reject invalid IRQL on this path. */
            pep_bug_check_power_control(r);
        }

        size_t written = 0;
        r->status = power_ctrl_handle_log_command(r->in_buf, (size_t)r->in_buf_size, r->out_buf,
                                                  (size_t)r->out_buf_size, &written);
        r->bytes_returned = written;
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_xhci_sub_dx_state)) {
        if (!n || !r->in_buf || r->in_buf_size < sizeof(ULONG))
            pep_bug_check_power_control(r);

        ULONG s = *(volatile ULONG*)r->in_buf;
        if (s != USB_SUB_DX_STATE_CONNECTED && s != USB_SUB_DX_STATE_DISCONNECTED) {

            pep_bug_check_power_control(r);
        }
        n->sub_dx_state_usb = s;
        n->sub_dx_state_usb_valid = 1;

        res_key_t k;
        k.res_id = 0x49434858u;
        k.sub_type = n->dev_id;
        res_value_t v;
        v.value0 = s;
        ULONGLONG t = KeQueryInterruptTime();
        v.value1 = (ULONG)t;
        v.value2 = (ULONG)(t >> 32);
        v.value3 = 0;
        (VOID) res_set(k, v);

        size_t written = 0;
        r->status = power_ctrl_write_simple_status(STATUS_SUCCESS, r->out_buf,
                                                   (size_t)r->out_buf_size, &written);
        r->bytes_returned = written;
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_reg)) {
        if (!pep_is_msft_sd_p_states_enabled()) {
            pep_write_compat_status_no_bytes(r, STATUS_NOT_SUPPORTED);
            return 1;
        }

        r->status = pep_handle_sdbus_reg(n, r);
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_mmc_info)) {
        if (!pep_is_msft_sd_p_states_enabled()) {
            r->status = STATUS_NOT_SUPPORTED;
            r->bytes_returned = 0;
            return 1;
        }

        r->status = pep_handle_sdbus_mmc_info(n, r);
        return 1;
    }

    if (IsEqualGUID(r->power_control_code, &guid_sdbus_pstate_req)) {
        if (!pep_is_msft_sd_p_states_enabled()) {
            r->status = STATUS_NOT_SUPPORTED;
            r->bytes_returned = 0;
            return 1;
        }

        if (!pep_enqueue_work(PEP_WORKPAYLOAD_REQUESTPOWERCONTROL, r, FALSE))
            r->status = STATUS_UNSUCCESSFUL;
        return 1;
    }

    r->status = STATUS_NOT_SUPPORTED;
    r->bytes_returned = 0;
    return 1;
}

ULONG_PTR pep_power_control_complete(_Inout_opt_ PVOID data)
{
    pep_power_control_complete_t* c = (pep_power_control_complete_t*)data;
    if (!c)
        pep_bug_check_power_control(data);
    if (!c->dev_handle)
        pep_bug_check_power_control(c);
    if (!c->power_control_code)
        pep_bug_check_power_control(c);

    const GUID* g = c->power_control_code;
    ULONG sig = g ? g->Data1 : 0;
    ULONG sig2 = g ? (((ULONG)g->Data2) << 16) | (ULONG)g->Data3 : 0;
    ULONG lo = (ULONG)(ULONG_PTR)c->dev_handle;
    ULONG hi = (ULONG)((ULONG_PTR)c->dev_handle >> 32);
    pep_log_write(PEPLOG_EVT_POWER_CONTROL, sig, c->status, sig2, (ULONG)c->bytes_returned, lo, hi);
    pep_free_result(c->req_ctx);
    return 1;
}

ULONG_PTR pep_parse_error_data(_Inout_opt_ PVOID data)
{
    UNREFERENCED_PARAMETER(data);

    return 1;
}
