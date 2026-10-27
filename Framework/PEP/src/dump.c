#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "component_perf.h"
#include "dcvs.h"
#include "dpm_verified.h"
#include "peplog.h"
#include "resource_cache.h"
#include "dispatch/dump_types.h"
#include "dispatch/node_types.h"
#include "dispatch/dump_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

BOOLEAN pep_is_bugcheck_enabled(_In_ ULONG scope);
static LONG pep_crashdump_reg_exception_filter(_In_ ULONG code,
                                               _In_opt_ PEXCEPTION_POINTERS ex_ptr);
static VOID pep_bugcheck_reason_callback(_In_ KBUGCHECK_CALLBACK_REASON reason,
                                         _In_ PKBUGCHECK_REASON_CALLBACK_RECORD record,
                                         _Inout_opt_ PVOID reason_specific_data,
                                         _In_ ULONG reason_specific_data_length);
VOID pep_reg_bugcheck_callback_if_needed(VOID);
VOID pep_unregister_bugcheck_callback_if_needed(VOID);
VOID pep_transition_debugger_critical_res(_In_ UCHAR transition);
__declspec(noreturn) VOID pep_bug_check(_In_ ULONG src, _In_opt_ PVOID ctx);
__declspec(noreturn) VOID pep_bug_check_power_control(_In_opt_ PVOID ctx);
static VOID pep_store_ocd_dev_handle(_In_opt_ pep_devnode_t* node);
static VOID pep_prepare_crash_dump_resources(_Inout_opt_ pep_devnode_t* node);
BOOLEAN pep_has_verified_crashdump_backend(VOID);
BOOLEAN pep_enter_lockless_polling_mode(VOID);
VOID pep_crashdump_rpmh_prep(VOID);
BOOLEAN pep_set_crash_dump_dev_state(_Inout_opt_ pep_devnode_t* node);
BOOLEAN pep_ocd_timer_armed(VOID);
static BOOLEAN pep_ocd_storage_in_d3(VOID);
static BOOLEAN pep_ocd_storage_in_fn(VOID);
static BOOLEAN pep_ocd_display_in_psr(VOID);
static VOID pep_ocd_trigger_break(_In_ ULONG reason, _In_opt_ pep_devnode_t* node);
VOID pep_display_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_d_state_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_f_state_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_p_state_ocd_test(_In_opt_ pep_devnode_t* node, _In_ ULONG comp, _In_ ULONG p_state);
static VOID pep_ocd_timer_thread(_In_ WDFTIMER timer);
VOID pep_ocd_init(VOID);
VOID pep_ocd_deinit(VOID);
ULONG_PTR pep_reg_debugger(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_crashdump_dev(_Inout_opt_ PVOID data);

static PVOID g_pep_debugger_ctx;
static POHANDLE g_debug_critical_res;
static volatile LONG g_ocd_timer_fired;
static WDFTIMER g_ocd_test_timer;
static ULONG g_debugger_transport_selected;
static KBUGCHECK_REASON_CALLBACK_RECORD g_pep_bugcheck_callback_record;
static volatile LONG g_pep_bugcheck_callback_registered;
static volatile LONG g_pep_bugcheck_callback_invocations;
static volatile LONG g_pep_bugcheck_last_reason;
static volatile LONG g_pep_bugcheck_last_reason_data_length;
static volatile LONG64 g_pep_bugcheck_drips_residency100ns;
static volatile LONG64 g_pep_bugcheck_drips_transition_cnt;
static const GUID pep_bugcheck_snapshot_guid = {
    0x69E52A2A, 0x4413, 0x4BE4, {0xB8, 0x22, 0x6C, 0x63, 0x7E, 0xD4, 0x90, 0x7F}};
static pep_bugcheck_snapshot_v1_t g_pep_bugcheck_snapshot;

extern __forceinline BOOLEAN pep_is_bugcheck_enabled(_In_ ULONG scope)
{
    return (scope != 0) && ((scope & g_pep_bugcheck_enable_mask) == scope);
}

static LONG pep_crashdump_reg_exception_filter(_In_ ULONG code, _In_opt_ PEXCEPTION_POINTERS ex_ptr)
{
    if (code != STATUS_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;

    ULONG ex_lo = 0;
    ULONG ex_hi = 0;
    ULONG sp_lo = 0;
    ULONG sp_hi = 0;
    ULONG pc_lo = 0;
    ULONG pc_hi = 0;

    if (ex_ptr && ex_ptr->ExceptionRecord && ex_ptr->ContextRecord) {
        ULONG_PTR ex_addr = (ULONG_PTR)ex_ptr->ExceptionRecord->ExceptionAddress;
        ULONG_PTR sp = (ULONG_PTR)ex_ptr->ContextRecord->Sp;
        ULONG_PTR pc = (ULONG_PTR)ex_ptr->ContextRecord->Pc;
        ex_lo = (ULONG)ex_addr;
        ex_hi = (ULONG)(ex_addr >> 32);
        sp_lo = (ULONG)sp;
        sp_hi = (ULONG)(sp >> 32);
        pc_lo = (ULONG)pc;
        pc_hi = (ULONG)(pc >> 32);
    }

    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2A, code, ex_lo, sp_lo, pc_lo, 0);
    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2B, code, ex_hi, sp_hi, pc_hi, 0);
    return EXCEPTION_EXECUTE_HANDLER;
}

static VOID pep_bugcheck_reason_callback(_In_ KBUGCHECK_CALLBACK_REASON reason,
                                         _In_ PKBUGCHECK_REASON_CALLBACK_RECORD record,
                                         _Inout_opt_ PVOID reason_specific_data,
                                         _In_ ULONG reason_specific_data_length)
{
    UNREFERENCED_PARAMETER(record);
    UNREFERENCED_PARAMETER(reason_specific_data);

    InterlockedIncrement(&g_pep_bugcheck_callback_invocations);
    InterlockedExchange(&g_pep_bugcheck_last_reason, (LONG)reason);
    InterlockedExchange(&g_pep_bugcheck_last_reason_data_length, (LONG)reason_specific_data_length);

    ULONGLONG resid = (ULONGLONG)g_resid.drips_residency100ns;
    ULONGLONG trans = (ULONGLONG)g_resid.drips_transition_cnt;
    ULONG cpu = 0;
    ULONG hz = 0;

    if (InterlockedCompareExchange(&g_resid.in_drips, 0, 0) == 1) {
        ULONGLONG now = pep_now100ns();
        ULONGLONG start = g_resid.drips_enter_time100ns;
        if (now > start)
            resid += (now - start);
        trans += 1;
    }

    InterlockedExchange64(&g_pep_bugcheck_drips_residency100ns, (LONG64)resid);
    InterlockedExchange64(&g_pep_bugcheck_drips_transition_cnt, (LONG64)trans);

    __try {
        PROCESSOR_NUMBER pn;
        KeGetCurrentProcessorNumberEx(&pn);
        cpu = (ULONG)pn.Number;

        if (g_bugcheck_perf_boost_enable) {
            ULONG cnt = dcvs_get_perf_level_cnt(cpu);
            if (cnt > 0 && cnt <= 256) {
                ULONG target = g_bugcheck_perf_boost_state;

                if (target == 0xFFFFFFFFu || target >= cnt)
                    target = 0;
                (VOID) dcvs_perf_set_state(cpu, target, target, target, 0);
            }
        }

        hz = dcvs_get_current_freq_hz(cpu);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cpu = 0;
        hz = 0;
    }

    g_pep_bugcheck_snapshot.signature = 0x50455042u;
    g_pep_bugcheck_snapshot.version = 1;
    g_pep_bugcheck_snapshot.size = sizeof(g_pep_bugcheck_snapshot);
    g_pep_bugcheck_snapshot.cpu = cpu;
    g_pep_bugcheck_snapshot.current_freq_hz = hz;
    g_pep_bugcheck_snapshot.callback_invocations =
        (ULONG)InterlockedCompareExchange(&g_pep_bugcheck_callback_invocations, 0, 0);
    g_pep_bugcheck_snapshot.last_reason = (ULONG)reason;
    g_pep_bugcheck_snapshot.last_reason_data_length = reason_specific_data_length;
    g_pep_bugcheck_snapshot.bugcheck_enable_mask = g_pep_bugcheck_enable_mask;
    g_pep_bugcheck_snapshot.debugger_present =
        (ULONG)InterlockedCompareExchange(&g_debugger_present, 0, 0);
    g_pep_bugcheck_snapshot.drips_residency100ns = resid;
    g_pep_bugcheck_snapshot.drips_transition_cnt = trans;
    g_pep_bugcheck_snapshot.snapshot_time100ns = pep_now100ns();

    if (reason == KbCallbackSecondaryDumpData && reason_specific_data &&
        reason_specific_data_length >= sizeof(KBUGCHECK_SECONDARY_DUMP_DATA)) {
        __try {
            KBUGCHECK_SECONDARY_DUMP_DATA* dump =
                (KBUGCHECK_SECONDARY_DUMP_DATA*)reason_specific_data;
            ULONG out_len = (ULONG)sizeof(g_pep_bugcheck_snapshot);

            if (dump->MaximumAllowed && out_len > dump->MaximumAllowed)
                out_len = dump->MaximumAllowed;

            if (out_len >= sizeof(ULONG) * 4) {
                dump->Guid = pep_bugcheck_snapshot_guid;
                dump->OutBuffer = &g_pep_bugcheck_snapshot;
                dump->OutBufferLength = out_len;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

VOID pep_reg_bugcheck_callback_if_needed(VOID)
{
    if (!pep_is_bugcheck_enabled(1u)) {
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x27, STATUS_NOT_SUPPORTED, g_pep_bugcheck_enable_mask,
                      1, 0, 0);
        return;
    }

    if (InterlockedCompareExchange(&g_pep_bugcheck_callback_registered, 1, 0) != 0)
        return;

    KeInitializeCallbackRecord(&g_pep_bugcheck_callback_record);
    if (!KeRegisterBugCheckReasonCallback(&g_pep_bugcheck_callback_record,
                                          pep_bugcheck_reason_callback, KbCallbackSecondaryDumpData,
                                          (PUCHAR) "PEP OS Crash Dump Directory")) {
        InterlockedExchange(&g_pep_bugcheck_callback_registered, 0);
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x27, STATUS_UNSUCCESSFUL, 0, 0, 0, 0);
        return;
    }

    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x27, STATUS_SUCCESS, 1, 0, 0, 0);
}

VOID pep_unregister_bugcheck_callback_if_needed(VOID)
{
    if (InterlockedCompareExchange(&g_pep_bugcheck_callback_registered, 0, 1) != 1)
        return;

    (VOID) KeDeregisterBugCheckReasonCallback(&g_pep_bugcheck_callback_record);
    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x28, STATUS_SUCCESS,
                  (ULONG)InterlockedCompareExchange(&g_pep_bugcheck_callback_invocations, 0, 0),
                  (ULONG)InterlockedCompareExchange(&g_pep_bugcheck_last_reason, 0, 0),
                  (ULONG)InterlockedCompareExchange(&g_pep_bugcheck_last_reason_data_length, 0, 0),
                  0);
}

VOID pep_transition_debugger_critical_res(_In_ UCHAR transition)
{
    POHANDLE h = g_debug_critical_res;
    if (!h)
        return;
    if (!g_kernel_info.TransitionCriticalResource)
        return;
    __try {
        typedef VOID (*pfn_pep_transition_critical_res_t)(POHANDLE handle, ULONG flags,
                                                          UCHAR transition);
        ((pfn_pep_transition_critical_res_t)g_kernel_info.TransitionCriticalResource)(h, 0,
                                                                                      transition);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

__declspec(noreturn) VOID pep_bug_check(_In_ ULONG src, _In_opt_ PVOID ctx)
{
    KeBugCheckEx(334u, src, (ULONG_PTR)ctx, 0u, 0u);
}

__declspec(noreturn) VOID pep_bug_check_power_control(_In_opt_ PVOID ctx)
{

    pep_bug_check(23u, ctx);
}

static VOID pep_store_ocd_dev_handle(_In_opt_ pep_devnode_t* node)
{
    if (!node)
        return;

    UNICODE_STRING sdc2;
    RtlInitUnicodeString(&sdc2, L"\\_SB.SDC2");

    if (!g_storage_ocd_dev_handle) {
        UNICODE_STRING ufs0;
        UNICODE_STRING pci0;
        UNICODE_STRING pci1;
        UNICODE_STRING pci2;
        UNICODE_STRING pci3;
        RtlInitUnicodeString(&ufs0, L"\\_SB.UFS0");
        RtlInitUnicodeString(&pci0, L"\\_SB.PCI0");
        RtlInitUnicodeString(&pci1, L"\\_SB.PCI1");
        RtlInitUnicodeString(&pci2, L"\\_SB.PCI2");
        RtlInitUnicodeString(&pci3, L"\\_SB.PCI3");

        if (pep_unicode_equals(&node->instance_path, &ufs0) ||
            pep_unicode_equals(&node->instance_path, &sdc2) ||
            pep_unicode_equals(&node->instance_path, &pci0) ||
            pep_unicode_equals(&node->instance_path, &pci1) ||
            pep_unicode_equals(&node->instance_path, &pci2) ||
            pep_unicode_equals(&node->instance_path, &pci3)) {
            g_storage_ocd_dev_handle = node;
        }
    }

    if (!g_sdc2_ocd_dev_handle && pep_unicode_equals(&node->instance_path, &sdc2))
        g_sdc2_ocd_dev_handle = node;

    if (!g_display_ocd_dev_handle) {
        UNICODE_STRING gpu0;
        RtlInitUnicodeString(&gpu0, L"\\_SB.GPU0");
        if (pep_unicode_equals(&node->instance_path, &gpu0))
            g_display_ocd_dev_handle = node;
    }
}

static VOID pep_prepare_crash_dump_resources(_Inout_opt_ pep_devnode_t* node)
{
    if (!node || node->crashdump_resources_prepared)
        return;

    node->crashdump_resources_prepared = 1;
}

/* there is no verified crashdump-safe UFS PSTATE_SET, ACTIVE vote sequence, hardware
 * completion or independent state readback. Crashdump callbacks must fail before touching
 * the storage controller until these exist. */
BOOLEAN pep_has_verified_crashdump_backend(VOID)
{
    return FALSE;
}

BOOLEAN pep_enter_lockless_polling_mode(VOID)
{

    return TRUE;
}

VOID pep_crashdump_rpmh_prep(VOID)
{
}

BOOLEAN pep_set_crash_dump_dev_state(_Inout_opt_ pep_devnode_t* node)
{
    if (!node || KeGetCurrentIrql() > APC_LEVEL || !pep_has_verified_crashdump_backend())
        return FALSE;

    node->current_d_state = PowerDeviceD0;
    dpm_verified_reset(&node->dpm_backend_state);
    node->in_system_transition = 0;
    node->sub_dx_state_usb = USB_SUB_DX_STATE_INVALID;
    node->sub_dx_state_usb_valid = 0;

    ExAcquireFastMutex(&node->d_state_change_f_mutex);
    node->d_state_change_in_progress = 0;
    node->d_state_completion_claimed = 0;
    node->d_state_pending_power_state = PowerDeviceUnspecified;
    KeSetEvent(&node->d_state_progress_event, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&node->d_state_change_f_mutex);

    res_key_t k;
    k.res_id = 0x41545344u;
    k.sub_type = (node->dev_id & 0xFFFFu);

    res_value_t v;
    v.value0 = (ULONG)node->current_d_state;
    v.value1 = 0;
    v.value2 = 0;
    v.value3 = 0;
    (VOID) res_set(k, v);

    if (KeGetCurrentIrql() == PASSIVE_LEVEL && node->current_p_state && node->comp_cnt) {
        for (ULONG comp = 0; comp < node->comp_cnt; comp++) {
            ULONG p_state = node->current_p_state[comp];

            if (comp_perf_get_set_cnt(&node->comp_perf_ctx, comp) != 0) {
                pep_comp_perf_state_req_t req_data;
                req_data.set = 0;
                req_data.u.state_idx = p_state;

                pep_req_comp_perf_state_t req;
                RtlZeroMemory(&req, sizeof(req));
                req.dev_handle = node;
                req.comp = comp;
                req.perf_requests_cnt = 1;
                req.perf_requests = &req_data;
                (VOID) comp_perf_req_state(&node->comp_perf_ctx, &req);
            }

            res_key_t kp;
            kp.res_id = RESID_PSTATE;
            kp.sub_type = ((node->dev_id & 0xFFFFu) << 16) | (comp & 0xFFFFu);

            res_value_t vp;
            vp.value0 = p_state;
            vp.value1 = 0;
            vp.value2 = 0;
            vp.value3 = 0;
            (VOID) res_set(kp, vp);
        }
    }

    if (node->current_f_state && node->comp_cnt) {
        for (ULONG comp = 0; comp < node->comp_cnt; comp++) {
            res_key_t kf;
            kf.res_id = 0x41545346u;
            kf.sub_type = ((node->dev_id & 0xFFFFu) << 16) | (comp & 0xFFFFu);

            res_value_t vf;
            vf.value0 = node->current_f_state[comp];
            vf.value1 = 1u;
            vf.value2 = 0u;
            vf.value3 = 0u;
            (VOID) res_set(kf, vf);
        }
    }

    return TRUE;
}

extern __forceinline BOOLEAN pep_ocd_timer_armed(VOID)
{
    return InterlockedCompareExchange(&g_ocd_timer_fired, 0, 0) != 0;
}

static BOOLEAN pep_ocd_storage_in_d3(VOID)
{
    pep_devnode_t* n = g_storage_ocd_dev_handle;
    return (n && n->current_d_state == PowerDeviceD3) ? TRUE : FALSE;
}

static BOOLEAN pep_ocd_storage_in_fn(VOID)
{
    pep_devnode_t* n = g_storage_ocd_dev_handle;
    if (!n || !n->current_f_state || n->comp_cnt == 0)
        return FALSE;
    return (n->current_f_state[0] != 0) ? TRUE : FALSE;
}

static BOOLEAN pep_ocd_display_in_psr(VOID)
{
    pep_devnode_t* n = g_display_ocd_dev_handle;
    if (!n || !n->current_f_state || n->comp_cnt == 0)
        return FALSE;
    return (n->current_f_state[0] != 0) ? TRUE : FALSE;
}

static VOID pep_ocd_trigger_break(_In_ ULONG reason, _In_opt_ pep_devnode_t* node)
{
    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2Du, STATUS_SUCCESS, reason, (ULONG)(ULONG_PTR)node,
                  (ULONG)(((ULONG_PTR)node) >> 32), 0);

    if (!KdDebuggerNotPresent)
        DbgBreakPoint();
}

VOID pep_display_ocd_test(_In_opt_ pep_devnode_t* node)
{
    if (!node || !pep_ocd_timer_armed() || !g_enable_pep_ocd_test ||
        node != g_display_ocd_dev_handle)
        return;

    if (g_enable_pep_ocd_test == 3 && pep_ocd_display_in_psr())
        pep_ocd_trigger_break(0xD301u, node);
    else if (g_enable_pep_ocd_test == 4 && pep_ocd_display_in_psr() && pep_ocd_storage_in_d3())
        pep_ocd_trigger_break(0xD304u, node);
    else if (g_enable_pep_ocd_test == 5 && pep_ocd_display_in_psr() && !pep_ocd_storage_in_d3() &&
             pep_ocd_storage_in_fn())
        pep_ocd_trigger_break(0xD305u, node);
}

VOID pep_d_state_ocd_test(_In_opt_ pep_devnode_t* node)
{
    if (!node || !pep_ocd_timer_armed() || !g_enable_pep_ocd_test ||
        node != g_storage_ocd_dev_handle)
        return;

    if (g_enable_pep_ocd_test == 1 && pep_ocd_storage_in_d3())
        pep_ocd_trigger_break(0xDD01u, node);
    else if (g_enable_pep_ocd_test == 4 && pep_ocd_storage_in_d3() && pep_ocd_display_in_psr())
        pep_ocd_trigger_break(0xDD04u, node);
    else if (g_enable_pep_ocd_test != 3 && pep_ocd_storage_in_d3())
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2Eu, STATUS_SUCCESS, 0xDDFFu, (ULONG)(ULONG_PTR)node,
                      (ULONG)((ULONG_PTR)node >> 32), 0);
}

VOID pep_f_state_ocd_test(_In_opt_ pep_devnode_t* node)
{
    if (!node || !pep_ocd_timer_armed() || !g_enable_pep_ocd_test ||
        node != g_storage_ocd_dev_handle)
        return;

    if (g_enable_pep_ocd_test == 2 && pep_ocd_storage_in_fn() && !pep_ocd_storage_in_d3())
        pep_ocd_trigger_break(0xDF02u, node);
    else if (g_enable_pep_ocd_test == 5 && pep_ocd_storage_in_fn() && !pep_ocd_storage_in_d3() &&
             pep_ocd_display_in_psr())
        pep_ocd_trigger_break(0xDF05u, node);
    else if (g_enable_pep_ocd_test != 3 && pep_ocd_storage_in_fn())
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2Fu, STATUS_SUCCESS, 0xDFFFu, (ULONG)(ULONG_PTR)node,
                      (ULONG)((ULONG_PTR)node >> 32), 0);
}

VOID pep_p_state_ocd_test(_In_opt_ pep_devnode_t* node, _In_ ULONG comp, _In_ ULONG p_state)
{
    if (!node || !pep_ocd_timer_armed() || !g_enable_pep_ocd_test_low_perf)
        return;

    if (node == g_storage_ocd_dev_handle && comp == 0 && p_state == g_pepocd_test_low_perf_idx)
        pep_ocd_trigger_break(0xD057u, node);
}

static VOID pep_ocd_timer_thread(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);
    InterlockedExchange(&g_ocd_timer_fired, 1);
}

VOID pep_ocd_init(VOID)
{
    g_enable_pep_ocd_test = registry_query_u_long(L"EnablePEPOCDTest", 0);
    g_enable_pep_ocd_test_low_perf = registry_query_u_long(L"EnablePEPOCDTestLowPerf", 0);
    g_pepocd_test_low_perf_idx = registry_query_u_long(L"PEPOCDTestLowPerfIndex", 0);
    InterlockedExchange(&g_ocd_timer_fired, 0);

    if ((!g_enable_pep_ocd_test && !g_enable_pep_ocd_test_low_perf) || g_ocd_test_timer ||
        !g.driver)
        return;

    WDF_TIMER_CONFIG cfg;
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_TIMER_CONFIG_INIT(&cfg, pep_ocd_timer_thread);
    cfg.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.ParentObject = g.driver;
    attr.ExecutionLevel = WdfExecutionLevelPassive;
    attr.SynchronizationScope = WdfSynchronizationScopeNone;

    if (NT_SUCCESS(WdfTimerCreate(&cfg, &attr, &g_ocd_test_timer)))
        WdfTimerStart(g_ocd_test_timer, WDF_REL_TIMEOUT_IN_SEC(15));
}

VOID pep_ocd_deinit(VOID)
{
    InterlockedExchange(&g_ocd_timer_fired, 0);
    if (g_ocd_test_timer) {
        (VOID) WdfTimerStop(g_ocd_test_timer, TRUE);
        WdfObjectDelete(g_ocd_test_timer);
        g_ocd_test_timer = NULL;
    }
}

ULONG_PTR pep_reg_debugger(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(22u, data);
    __try {
        PVOID ctx = *(PVOID*)data;
        if (!ctx)
            pep_bug_check(22u, data);
        g_pep_debugger_ctx = ctx;

        InterlockedExchange(&g_debugger_present, ctx ? 1 : 0);
        g_debugger_transport_selected = 0;
        g_debugger_break_even_offset_us = 0;
        g_debugger_break_even_affects_all_states = 0;
        g_debug_critical_res = NULL;

        if (ctx) {
            __try {
                g_debug_critical_res = *(POHANDLE*)((PUCHAR)ctx + 88);

                UNICODE_STRING* inst = (UNICODE_STRING*)((PUCHAR)ctx + 16);
                if (inst && inst->Buffer && inst->Length) {
                    if (wcs_contains_insensitive(inst->Buffer, L"KDNET") ||
                        wcs_contains_insensitive(inst->Buffer, L"EEM"))
                        g_debugger_transport_selected = 1;
                    else if (wcs_contains_insensitive(inst->Buffer, L"USB"))
                        g_debugger_transport_selected = 2;
                    else if (wcs_contains_insensitive(inst->Buffer, L"SERIAL") ||
                             wcs_contains_insensitive(inst->Buffer, L"COM"))
                        g_debugger_transport_selected = 3;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_debug_critical_res = NULL;
            }

            if (g_debugger_transport_selected) {
                g_debugger_break_even_offset_us =
                    registry_query_u_long(L"UsbDebuggerBreakEvenOffsetUs", 0);
                g_debugger_break_even_affects_all_states =
                    registry_query_u_long(L"DebuggerBreakEvenAffectsAllStates", 0);
            }

            if (g_debugger_vote_critical_res_on_reg && g_debug_critical_res) {
                pep_transition_debugger_critical_res(0);
                pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x2C, STATUS_SUCCESS, 0, 0, 0, 0);
            }
        }

        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x21, STATUS_SUCCESS,
                      (ULONG)(ULONG_PTR)g_pep_debugger_ctx, g_debugger_transport_selected,
                      g_debugger_break_even_offset_us, g_debugger_break_even_affects_all_states);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_reg_crashdump_dev(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;

    __try {
        pep_reg_crashdump_dev_t* reg = (pep_reg_crashdump_dev_t*)data;
        if (reg->dev_handle) {
            pep_devnode_t* n = (pep_devnode_t*)reg->dev_handle;
            g_pep_crashdump_dev = reg->dev_handle;
            if (pep_is_managed_node(n)) {
                pep_store_ocd_dev_handle(n);
                /* no owner for that mapping exists on Molokai. Keep reg but do not touch SDCC
                 * hardware. */

                pep_prepare_crash_dump_resources(n);
            }
            reg->power_on_dump_dev_callback = pep_power_on_dump_dev_callback;
            pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x25, STATUS_SUCCESS,
                          (ULONG)(ULONG_PTR)reg->dev_handle,
                          (ULONG)((ULONG_PTR)reg->dev_handle >> 32), 0, 0);
        }
        return 1;
    } __except (pep_crashdump_reg_exception_filter(GetExceptionCode(), GetExceptionInformation())) {
        return 0;
    }
}
