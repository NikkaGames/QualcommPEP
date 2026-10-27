#include "driver.h"
#include <pepfx.h> /* public Windows PEP_INFORMATION and PoFx ABI. */
#include "xo_diag.h"
#include "deep_standby.h"
#include "diag.h"
#include "peplog.h"
#include "soc_subsystem.h"
#include "dcvs.h"
#include "soc_transport.h"
#include "component_perf.h"
#include "perf_clock_win.h"
#include "resource_cache.h"
#include "power_control.h"
#include "interrupt.h"
#include "tsens.h"
#include "chipinfo.h"
#include "dpm_identity.h"
#include "dpm_verified.h"
#include "npa_client.h"
#include "init_gate.h"
#include "hw_backend.h"
#include "peer_component.h"
#include "runtime_component_win.h"
#include "runtime_dstate.h"
#include "ufs_pstate_graph_bridge.h"
#include "pofx_perf.h"
#include "hw_backend_windows_wdf.h"
#include "resource_ddi.h"

#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

#include "dispatch/limits.h"
#include "dispatch/dispatch_types.h"
#include "dispatch/dstate_types.h"
#include "dispatch/dump_types.h"
#include "dispatch/fstate_types.h"
#include "dispatch/idle_types.h"
#include "dispatch/node_types.h"
#include "dispatch/park_types.h"
#include "dispatch/ppm_types.h"
#include "dispatch/pstate_types.h"
#include "dispatch/work_types.h"
#include "dispatch/dstate_priv.h"
#include "dispatch/dump_priv.h"
#include "dispatch/fstate_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#include "dispatch/park_priv.h"
#include "dispatch/ppm_priv.h"
#include "dispatch/pstate_priv.h"
#include "dispatch/work_priv.h"
#include "trace.h"
#include "main.tmh"

ULONGLONG pep_now100ns(VOID);
static __forceinline PVOID pep_get_pep_handle_from_cpu_num(_In_ ULONG cpu);
PVOID pep_get_kernel_handle_from_cpu_num(_In_ ULONG cpu);
PVOID pep_extract_kernel_handle_from_pep_handle(_In_opt_ PVOID pep_handle);
ULONG pep_get_current_cpu_num(VOID);
BOOLEAN pep_is_msft_sd_p_states_enabled(VOID);
VOID pep_write_compat_status_no_bytes(_Inout_ pep_power_control_req_t* req, _In_ NTSTATUS status);
BOOLEAN pep_unicode_equals(_In_ const UNICODE_STRING* a, _In_ const UNICODE_STRING* b);

static VOID pep_free_unicode_string(_Inout_ UNICODE_STRING* us);
static NTSTATUS pep_clone_unicode_string(_In_ const UNICODE_STRING* in, _Out_ UNICODE_STRING* out);
pep_devnode_t* pep_find_dev_node_locked(_In_ const UNICODE_STRING* dev_id);
pep_devnode_t* pep_get_or_create_dev_node(_In_ const UNICODE_STRING* dev_id);
pep_devnode_t* pep_find_dev_node_by_id(_In_ const UNICODE_STRING* dev_id);
BOOLEAN pep_is_managed_node(_In_opt_ pep_devnode_t* node);
VOID pep_apply_prepare_states(_Inout_ pep_devnode_t* node);
VOID pep_apply_abandon_states(_Inout_ pep_devnode_t* node);

BOOLEAN pep_any_usb_sub_dx_mismatch(_In_ ULONG required_state);
DEVICE_POWER_STATE pep_apply_usb_sub_dx_state_hint(_Inout_ pep_devnode_t* node,
                                                   _In_ DEVICE_POWER_STATE requested_state);
VOID pep_req_kernel_work(VOID);
static __forceinline ULONG pep_sdbus_get_state_freq(_In_opt_ pep_devnode_t* n, _In_ ULONG idx);
static VOID pep_sdbus_update_stats(_Inout_opt_ pep_devnode_t* n, _In_ ULONG new_idx);
static VOID pep_sdbus_mirror_res(_In_opt_ pep_devnode_t* n);
NTSTATUS pep_handle_sdbus_reg(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
NTSTATUS pep_handle_sdbus_mmc_info(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
NTSTATUS pep_handle_sdbus_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
const GUID* pep_stable_queued_power_code(_In_opt_ const GUID* code);
static BOOLEAN __fastcall pep_dev_notify(_In_ ULONG notify, _Inout_opt_ PVOID data);
static BOOLEAN __fastcall pep_proc_notify(_In_opt_ PVOID pep_handle, _In_ ULONG notify,
                                          _Inout_opt_ PVOID data);
static NTSTATUS reg_pep_plugin(VOID);
static int pep_npa_scope_ready_held(void* ctx_arg, const char* path);
NTSTATUS pep_publish_verified_molokai_graph(_Inout_ res_graph_t* graph,
                                            _Inout_ npa_registry_t* npa_clients);
NTSTATUS pep_unpublish_verified_molokai_graph(VOID);
BOOLEAN pep_resolve_molokai_dev(_In_ const UNICODE_STRING* dev_id,
                                _Out_writes_(256) char path[256]);
power_result_t pep_run_molokai_lifecycle(_In_ const UNICODE_STRING* dev_id, _In_ BOOLEAN prepare);
static power_result_t pep_run_molokai_d_state(_In_ const UNICODE_STRING* dev_id, _In_ ULONG state);
BOOLEAN pep_is_molokai_dpm_execution_ready(_In_ const UNICODE_STRING* dev_id);
int pep_claim_res_transaction(volatile LONG* slot, LONG target, void* ctx, int (*ready)(void*));
static int peer_ready(void* ctx);
static int peer_lock(void* v);
static void peer_unlock(void* v);
static int peer_action_state(peer_native_scope_t* s);
power_result_t pep_run_peer_f_state(peer_native_scope_t* s, ULONG state, power_execution_t* trace,
                                    power_scratch_t* scratch);
static power_result_t pep_run_peer_d_state(peer_native_scope_t* s, ULONG target);
static int peer_supports(void* v, const power_action_t* a);
static int peer_snapshot(void* v, const power_action_t* a, power_snapshot_t* snap);
static int peer_apply(void* v, const power_action_t* a);
static int peer_verify(void* v, const power_action_t* a);
static int peer_restore(void* v, const power_action_t* a, const power_snapshot_t* snap);
static int peer_verify_restored(void* v, const power_action_t* a, const power_snapshot_t* snap);
static int peer_verify_final(void* v, const power_sequence_t* sequence);
static VOID peer_init_adapter(peer_native_scope_t* s);
BOOLEAN runtime_acpi_path(const UNICODE_STRING* id, char out[256]);
static int runtime_d_lock(void* v);
static void runtime_d_unlock(void* v);
static int runtime_d_state(runtime_dstate_native_t* s);
static int runtime_d_supports(void* v, const power_action_t* a);
static int runtime_d_snapshot(void* v, const power_action_t* a, power_snapshot_t* snap);
static int runtime_d_apply(void* v, const power_action_t* a);
static int runtime_d_verify(void* v, const power_action_t* a);
static int runtime_d_restore(void* v, const power_action_t* a, const power_snapshot_t* snap);
static int runtime_d_verify_restored(void* v, const power_action_t* a,
                                     const power_snapshot_t* snap);
static int runtime_d_verify_final(void* v, const power_sequence_t* sequence);
static VOID runtime_d_init_adapter(runtime_dstate_native_t* s);
static int runtime_d_ready(void* ctx);
power_result_t pep_runtime_d_execute(runtime_dstate_native_t* scope, ULONG target);
static int runtime_clock_ready(void* ctx);
static int runtime_clock_begin(void* ctx);
static void runtime_clock_end(void* ctx);
static int runtime_clock_read(void* ctx, const char* name, uint64_t* out);
static int runtime_clock_dfs_catalog(void* ctx, const char* name, gcc_dfs_catalog_t* out);
static clock_perf_scope_t* runtime_clock_create(runtime_dstate_native_t* scope, const void* blob,
                                                size_t bytes);
ULONG_PTR pep_prepare_runtime_dstate(pep_prepare_dev_t* p, const char* path);
ULONG_PTR pep_prepare_reviewed_peer(pep_prepare_dev_t* p, const char* path);
ULONG_PTR pep_prepare_runtime_comp(pep_prepare_dev_t* p, const char* path);
BOOLEAN ascii_dev_id(_In_opt_ const UNICODE_STRING* id, char out[128]);
static NTSTATUS pep_ufs_src_copy(const char* method, void* buf, ULONG capacity, ULONG* bytes);
BOOLEAN pep_ufs_comp_desc_matches_graph(const char* path);
int pep_native_owner_lock(void* ctx);
void pep_native_owner_unlock(void* ctx);
static int pep_native_owner_supports(void* ctx, const power_action_t* action);
static int pep_native_owner_verify(void* ctx, const power_sequence_t* sequence);
static power_result_t pep_native_owner_execute(void* ctx, const power_sequence_t* sequence,
                                               power_execution_t* trace);
static graph_dev_t* pep_native_ufs_graph_dev(res_graph_t* graph);
BOOLEAN pep_prepare_native_ufs_scope(pep_devnode_t* n, const char* path);
static BOOLEAN pep_native_ufs_verify_f1(pep_native_producer_scope_t* scope,
                                        comp_state_work_t* work);
power_result_t pep_run_node_d_state(pep_devnode_t* n, ULONG target);
BOOLEAN pep_native_ufs_retire(pep_devnode_t* n, const UNICODE_STRING* dev_id);
NTSTATUS pep_native_perf_quiesce_gate(VOID);
UCHAR __fastcall
pep_power_on_dump_dev_callback(_In_opt_ pep_crashdump_information_t* crashdump_information);
static ULONG_PTR pep_dpm_query_soc_subsystem_cnt(_Inout_opt_ PVOID data);
static ULONG_PTR pep_dpm_query_soc_subsystem(_Inout_opt_ PVOID data);
static ULONG_PTR pep_dpm_reset_soc_subsystem_accounting(_Inout_opt_ PVOID data);
static ULONG_PTR pep_dpm_query_soc_subsystem_blocking_time(_Inout_opt_ PVOID data);
static ULONG_PTR pep_dpm_query_soc_subsystem_metadata(_Inout_opt_ PVOID data);
ULONG get_cpu_num_from_pep_handle(_In_opt_ PVOID pep_handle);
NTSTATUS pep_reg_veto_reason(_In_z_ PCWSTR reason, _Out_ PULONG veto_id);
static __forceinline VOID pep_dec_ref64(_Inout_ volatile LONG64* v);
VOID pep_dec_ref32(_Inout_ volatile LONG* v);
NTSTATUS pep_apply_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id);
NTSTATUS pep_remove_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id);
LONGLONG pep_get_veto_ref_cnt(_In_ ULONG veto_id);
VOID pep_query_veto_reasons_compat(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_veto_reason_compat(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
NTSTATUS pep_resolve_idle_veto_target(_In_ ULONG selector, _In_ ULONG mode_or_coordinated_idx,
                                      _Out_ PULONG lpr_id, _Out_ PULONG mode_id);
BOOLEAN pep_get_cpu_handles(_In_ ULONG cpu, _Out_ PVOID* pep_handle, _Out_ PVOID* kernel_handle);
BOOLEAN pep_get_proc_idle_state_cnt(_In_opt_ PVOID pep_handle, _Out_ PULONG cnt);
BOOLEAN pep_get_mode_veto_ref_cnt(_In_ ULONG lpr_id, _In_ ULONG mode_id, _Out_ PULONG ref_cnt);
NTSTATUS pep_apply_ex_soc_d_state(_In_z_ const CHAR* dev_id_a, _In_ UCHAR d_state);
ULONG pep_get_registry_test_veto_id(VOID);
NTSTATUS pep_get_park_selection_snapshot(_Out_ parksel_snapshot_t* snapshot);
NTSTATUS pep_get_park_selection_history(_Out_writes_to_(capacity, *cnt) parksel_event_t* events,
                                        _In_ ULONG capacity, _Out_ ULONG* cnt,
                                        _Out_ ULONG* dropped);
NTSTATUS pep_get_park_selection_policy(_Out_ parksel_policy_t* policy);
NTSTATUS pep_set_park_selection_policy(_In_ const parksel_policy_t* policy);
ULONG pep_get_current_cpu_number(VOID);
WCHAR upcase_w(_In_ WCHAR c);
BOOLEAN wcs_contains_insensitive(_In_opt_ PCWSTR haystack, _In_z_ PCWSTR needle);
BOOLEAN pep_is_molokai_idle_execution_ready(VOID);
VOID pep_query_proc_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_coordinated_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_platform_states_acpi_lpi_pep(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_proc_idle_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_coordinated_idle_dependency(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
NTSTATUS pep_on_hibernate_enter(VOID);
NTSTATUS pep_on_power_down_enter(VOID);
static VOID pep_drain_queued_work_items(VOID);
static NTSTATUS pep_prepare_for_power_down(VOID);
static VOID pep_restore_after_power_up(VOID);
VOID pep_on_hibernate_resume(VOID);
NTSTATUS pep_warmup_caches(VOID);
NTSTATUS pep_global_init(VOID);
VOID pep_global_deinit(VOID);
NTSTATUS pep_reg(_In_ WDFDEVICE dev);
VOID pep_unregister(_In_ WDFDEVICE dev);

volatile LONG g_hibernate_in_progress;
static volatile LONG g_hibernate_rpmh_synced;

static const WCHAR g_veto_reason_registry_test[] = L"Vetoed by Registry or Test IOCTL";

extern __forceinline ULONGLONG pep_now100ns(VOID)
{
    return KeQueryInterruptTime();
}

/* use the matching WDK output type, including RequestCommon at +0x58. The former 0x58-byte
 * V2 buf ended exactly at g_pep_dev_list_head in the tested ARM64 image; the V3 callback
 * write corrupted that list head and caused 0x7E in pep_find_dev_node_locked during PoFx
 * reg. */

PEP_KERNEL_INFORMATION g_kernel_info;
C_ASSERT(PEP_KERNEL_INFORMATION_VERSION == PEP_KERNEL_INFORMATION_V3);
C_ASSERT(sizeof(g_kernel_info) == 0x60);
C_ASSERT(FIELD_OFFSET(PEP_KERNEL_INFORMATION, RequestCommon) == 0x58);

PVOID g_cpu_pep_handle[256];
PVOID g_cpu_kernel_handle[256];

static __forceinline PVOID pep_get_pep_handle_from_cpu_num(_In_ ULONG cpu)
{
    if (cpu >= RTL_NUMBER_OF(g_cpu_pep_handle))
        return NULL;
    return g_cpu_pep_handle[cpu];
}

extern __forceinline PVOID pep_get_kernel_handle_from_cpu_num(_In_ ULONG cpu)
{
    if (cpu >= RTL_NUMBER_OF(g_cpu_kernel_handle))
        return NULL;
    return g_cpu_kernel_handle[cpu];
}

extern __forceinline PVOID pep_extract_kernel_handle_from_pep_handle(_In_opt_ PVOID pep_handle)
{
    if (!pep_handle)
        return NULL;

    __try {

        return ((PVOID*)pep_handle)[11];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
}

extern __forceinline ULONG pep_get_current_cpu_num(VOID)
{
    PROCESSOR_NUMBER pn;
    KeGetCurrentProcessorNumberEx(&pn);
    return (ULONG)pn.Number;
}

/* assert the callback payload layout before interpreting it. */

C_ASSERT(FIELD_OFFSET(pep_dev_reg_v2_t, comp_cnt) == 0x08);
C_ASSERT(FIELD_OFFSET(pep_dev_reg_v2_t, components) == 0x10);
C_ASSERT(FIELD_OFFSET(pep_reg_dev_v2_t, dev_accepted) == 0x20);

C_ASSERT(sizeof(pep_notify_comp_idle_state_t) == 0x18);

C_ASSERT(sizeof(pep_work_power_control_t) == 0x38);

C_ASSERT(sizeof(pep_work_active_complete_t) == 0x10);

C_ASSERT(sizeof(pep_work_complete_idle_state_t) == 0x10);

C_ASSERT(sizeof(pep_work_dev_power_t) == 0x10);

C_ASSERT(sizeof(pep_work_dev_idle_t) == 0x10);

C_ASSERT(sizeof(pep_work_idle_state_t) == 0x10);

C_ASSERT(sizeof(pep_work_complete_perf_state_t) == 0x10);

C_ASSERT(sizeof(pep_work_acpi_notify_t) == 0x10);

C_ASSERT(sizeof(pep_work_u_t) == 0x38);

C_ASSERT(sizeof(pep_work_information_t) == 0x40);

KSPIN_LOCK g_pep_work_lock;
LIST_ENTRY g_pep_work_queue;
NPAGED_LOOKASIDE_LIST g_pep_work_lookaside;
NPAGED_LOOKASIDE_LIST g_pep_result_tiny_lookaside;
NPAGED_LOOKASIDE_LIST g_pep_result_small_lookaside;
NPAGED_LOOKASIDE_LIST g_pep_result_large_lookaside;
NPAGED_LOOKASIDE_LIST g_pep_power_ctrl_ctx_lookaside;
volatile LONG g_pep_result_init;
volatile LONG g_pep_work_init;
/* one-way shutdown gate protects lookaside lifetime while producers build work and
 * pep_work consumers process dequeued entries. */

volatile LONG g_pep_work_shutdown;

KEVENT g_pep_work_drained_event;
volatile LONG g_pep_power_ctrl_ctx_init;

WDFWAITLOCK g_pep_dev_list_lock;
pep_devnode_t* g_pep_dev_list_head;
static volatile LONG g_pep_dev_id_alloc;
static pep_devnode_t* g_qc_sdc1_node;
static pep_devnode_t* g_qc_sdc2_node;

static ULONG g_sdc1_maximum_bus_freq;
static ULONG g_sdc2_maximum_bus_freq;
static ULONG g_sdc1_p_state_idx = 3;
static ULONG g_sdc2_p_state_idx = 3;
static ULONG g_enable_msft_sd_p_states = 1;

PVOID g_pep_crashdump_dev;
pep_devnode_t* g_storage_ocd_dev_handle;
pep_devnode_t* g_display_ocd_dev_handle;
pep_devnode_t* g_sdc2_ocd_dev_handle;

ULONG g_enable_pep_ocd_test;
ULONG g_enable_pep_ocd_test_low_perf;
ULONG g_pepocd_test_low_perf_idx;

volatile LONG g_debugger_present;

ULONG g_debugger_break_even_offset_us;
ULONG g_debugger_break_even_affects_all_states;
ULONG g_debugger_vote_critical_res_on_reg = 1;

ULONG g_pep_bugcheck_enable_mask = 1;
ULONG g_bugcheck_perf_boost_enable = 1;
ULONG g_bugcheck_perf_boost_state = 0xFFFFFFFFu;

extern __forceinline BOOLEAN pep_is_msft_sd_p_states_enabled(VOID)
{
    /* SD performance requires verified clock/voltage completion in both inline and deferred paths.
     * Registry preferences do not establish a provider. */

    UNREFERENCED_PARAMETER(g_enable_msft_sd_p_states);
    return FALSE;
}

extern __forceinline VOID pep_write_compat_status_no_bytes(_Inout_ pep_power_control_req_t* req,
                                                           _In_ NTSTATUS status)
{
    if (!req)
        return;

    if (req->out_buf && req->out_buf_size >= 12) {
        ULONG* out = (ULONG*)req->out_buf;
        out[0] = 3;
        out[1] = (ULONG)status;
        out[2] = 0;
    }

    req->status = status;
    req->bytes_returned = 0;
}

BOOLEAN pep_unicode_equals(_In_ const UNICODE_STRING* a, _In_ const UNICODE_STRING* b)
{
    if (!a || !b)
        return FALSE;
    return RtlCompareUnicodeString(a, b, TRUE) == 0;
}

static VOID pep_free_unicode_string(_Inout_ UNICODE_STRING* us)
{
    if (!us)
        return;
    if (us->Buffer)
        ExFreePool(us->Buffer);
    us->Buffer = NULL;
    us->Length = 0;
    us->MaximumLength = 0;
}

static NTSTATUS pep_clone_unicode_string(_In_ const UNICODE_STRING* in, _Out_ UNICODE_STRING* out)
{
    if (!in || !out)
        return STATUS_INVALID_PARAMETER;
    out->Length = 0;
    out->MaximumLength = 0;
    out->Buffer = NULL;
    if (!in->Buffer || in->Length == 0 || (in->Length & (sizeof(WCHAR) - 1u)) != 0 ||
        in->Length > in->MaximumLength || in->Length > (0xFFFFu - sizeof(WCHAR)))
        return STATUS_INVALID_PARAMETER;
    USHORT bytes = (USHORT)(in->Length + sizeof(WCHAR));
    PWCH buf = (PWCH)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(buf, bytes);
    RtlCopyMemory(buf, in->Buffer, in->Length);
    out->Buffer = buf;
    out->Length = in->Length;
    out->MaximumLength = bytes;
    return STATUS_SUCCESS;
}

pep_devnode_t* pep_find_dev_node_locked(_In_ const UNICODE_STRING* dev_id)
{
    for (pep_devnode_t* n = g_pep_dev_list_head; n; n = n->next) {
        if (RtlCompareUnicodeString(&n->instance_path, dev_id, TRUE) == 0)
            return n;
    }
    return NULL;
}

pep_devnode_t* pep_get_or_create_dev_node(_In_ const UNICODE_STRING* dev_id)
{
    if (!dev_id || !dev_id->Buffer)
        return NULL;
    if (!g_pep_dev_list_lock)
        return NULL;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    pep_devnode_t* n = pep_find_dev_node_locked(dev_id);
    if (n) {
        WdfWaitLockRelease(g_pep_dev_list_lock);
        return n;
    }
    n = (pep_devnode_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(pep_devnode_t), POOL_TAG);
    if (!n) {
        WdfWaitLockRelease(g_pep_dev_list_lock);
        return NULL;
    }
    RtlZeroMemory(n, sizeof(*n));
    if (!NT_SUCCESS(pep_clone_unicode_string(dev_id, &n->instance_path))) {
        ExFreePool(n);
        WdfWaitLockRelease(g_pep_dev_list_lock);
        return NULL;
    }
    n->dev_id = (ULONG)InterlockedIncrement(&g_pep_dev_id_alloc);
    n->reg_state = DEV_NOT_PREPARED;
    /* manual-reset event: every waiter for the same in-flight D-state transition must be
     * released by its completion (not just one waiter). Idle begins signaled; only a
     * successful ownership claim clears it. */

    KeInitializeEvent(&n->d_state_progress_event, NotificationEvent, TRUE);
    ExInitializeFastMutex(&n->d_state_change_f_mutex);
    KeInitializeSpinLock(&n->deferred_work_lock);
    KeInitializeEvent(&n->deferred_work_drained, NotificationEvent, TRUE);
    n->deferred_closed = TRUE;
    n->deferred_unregistering = FALSE;
    n->deferred_workers = 0;
    n->deferred_generation = 0;
    n->d_state_change_in_progress = 0;
    n->d_state_completion_claimed = 0;
    n->d_state_pending_power_state = PowerDeviceUnspecified;
    n->next = g_pep_dev_list_head;
    g_pep_dev_list_head = n;

    UNICODE_STRING sdc1;
    UNICODE_STRING sdc2;
    RtlInitUnicodeString(&sdc1, L"\\_SB.SDC1");
    RtlInitUnicodeString(&sdc2, L"\\_SB.SDC2");
    if (pep_unicode_equals(&n->instance_path, &sdc1))
        g_qc_sdc1_node = n;
    else if (pep_unicode_equals(&n->instance_path, &sdc2))
        g_qc_sdc2_node = n;

    WdfWaitLockRelease(g_pep_dev_list_lock);
    return n;
}

pep_devnode_t* pep_find_dev_node_by_id(_In_ const UNICODE_STRING* dev_id)
{
    if (!dev_id || !dev_id->Buffer || !g_pep_dev_list_lock)
        return NULL;

    pep_devnode_t* n = NULL;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    n = pep_find_dev_node_locked(dev_id);
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return n;
}

BOOLEAN pep_is_managed_node(_In_opt_ pep_devnode_t* node)
{
    if (!node || !g_pep_dev_list_lock || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;

    BOOLEAN found = FALSE;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (pep_devnode_t* it = g_pep_dev_list_head; it; it = it->next) {
        if (it == node) {
            found = TRUE;
            break;
        }
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return found;
}

VOID pep_apply_prepare_states(_Inout_ pep_devnode_t* node)
{
    if (!node)
        return;

    node->current_d_state = PowerDeviceD0;
    node->in_system_transition = 0;
    node->sub_dx_state_usb = USB_SUB_DX_STATE_INVALID;
    node->sub_dx_state_usb_valid = 0;

    if (node->current_p_state && node->comp_cnt)
        RtlZeroMemory(node->current_p_state, sizeof(ULONG) * node->comp_cnt);
    if (node->current_f_state && node->comp_cnt)
        RtlZeroMemory(node->current_f_state, sizeof(ULONG) * node->comp_cnt);

    res_key_t k;
    k.res_id = 0x41545344u;
    k.sub_type = (node->dev_id & 0xFFFFu);

    res_value_t v;
    v.value0 = (ULONG)node->current_d_state;
    v.value1 = 0;
    v.value2 = 0;
    v.value3 = 0;
    (VOID) res_set(k, v);
}

VOID pep_apply_abandon_states(_Inout_ pep_devnode_t* node)
{
    if (!node)
        return;

    node->current_d_state = PowerDeviceD3;
    dpm_verified_reset(&node->dpm_backend_state);
    node->in_system_transition = 0;
    node->sub_dx_state_usb = USB_SUB_DX_STATE_INVALID;
    node->sub_dx_state_usb_valid = 0;

    if (node->current_p_state && node->comp_cnt)
        RtlZeroMemory(node->current_p_state, sizeof(ULONG) * node->comp_cnt);
    if (node->current_f_state && node->comp_cnt)
        RtlZeroMemory(node->current_f_state, sizeof(ULONG) * node->comp_cnt);

    res_key_t k;
    k.res_id = 0x41545344u;
    k.sub_type = (node->dev_id & 0xFFFFu);

    res_value_t v;
    v.value0 = (ULONG)node->current_d_state;
    v.value1 = 0;
    v.value2 = 0;
    v.value3 = 0;
    (VOID) res_set(k, v);
}

BOOLEAN pep_any_usb_sub_dx_mismatch(_In_ ULONG required_state)
{
    if (!g_pep_dev_list_lock)
        return FALSE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;

    BOOLEAN mismatch = FALSE;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (pep_devnode_t* n = g_pep_dev_list_head; n; n = n->next) {
        if (!n->sub_dx_state_usb_valid)
            continue;
        if (n->sub_dx_state_usb != required_state) {
            mismatch = TRUE;
            break;
        }
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return mismatch;
}

extern __forceinline DEVICE_POWER_STATE
pep_apply_usb_sub_dx_state_hint(_Inout_ pep_devnode_t* node,
                                _In_ DEVICE_POWER_STATE requested_state)
{
    if (!node)
        return requested_state;

    if (requested_state <= PowerDeviceD0 || requested_state >= PowerDeviceMaximum)
        return requested_state;

    if (!node->sub_dx_state_usb_valid)
        return requested_state;

    DEVICE_POWER_STATE effective = requested_state;
    if (node->sub_dx_state_usb == USB_SUB_DX_STATE_CONNECTED)
        effective = PowerDeviceD1;
    else if (node->sub_dx_state_usb == USB_SUB_DX_STATE_DISCONNECTED)
        effective = PowerDeviceD2;

    node->sub_dx_state_usb = USB_SUB_DX_STATE_INVALID;
    node->sub_dx_state_usb_valid = 0;
    return effective;
}

extern __forceinline VOID pep_req_kernel_work(VOID)
{
    if (!g_kernel_info.RequestWorker || !g_kernel_info.Plugin)
        pep_bug_check(22u, &g_kernel_info);

    typedef VOID (*pfn_pep_req_worker_t)(PVOID plugin);
    ((pfn_pep_req_worker_t)g_kernel_info.RequestWorker)(g_kernel_info.Plugin);
}

/* pofx may retire its notify and perf_requests array immediately after
 * pep_req_comp_perf_state returns. The caller owns device rundown separately; this helper
 * does not confer PEP device ownership. */

static __forceinline ULONG pep_sdbus_get_state_freq(_In_opt_ pep_devnode_t* n, _In_ ULONG idx)
{
    if (!n || !n->sdbus_perf_states || idx >= n->sdbus_perf_state_cnt)
        return 0;
    __try {
        return n->sdbus_perf_states[idx].freq;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static VOID pep_sdbus_update_stats(_Inout_opt_ pep_devnode_t* n, _In_ ULONG new_idx)
{
    if (!n)
        return;
    ULONGLONG now = pep_now100ns();
    if (n->sdbus_last_change100ns) {
        ULONGLONG start = n->sdbus_last_change100ns;
        if (now > start)
            n->sdbus_residency100ns += (now - start);
    }
    if (n->sdbus_current_idx != new_idx)
        n->sdbus_transition_cnt++;
    n->sdbus_last_change100ns = now;
}

static VOID pep_sdbus_mirror_res(_In_opt_ pep_devnode_t* n)
{
    if (!n)
        return;

    ULONG rid = 0;
    if (n == g_qc_sdc1_node)
        rid = 0x31434453u;
    else if (n == g_qc_sdc2_node)
        rid = 0x32434453u;
    else
        return;

    res_key_t k;
    k.res_id = rid;
    k.sub_type = 0;

    res_value_t v;
    v.value0 = n->sdbus_current_idx;
    v.value1 = pep_sdbus_get_state_freq(n, n->sdbus_current_idx);
    v.value2 = n->sdbus_max_bus_freq;
    v.value3 = (ULONG)(n->sdbus_transition_cnt & 0xFFFFFFFFull);

    (VOID) res_set(k, v);
}

NTSTATUS pep_handle_sdbus_reg(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r)
{
    if (!n || !r || !r->out_buf || r->out_buf_size < sizeof(sdbus_pstate_reg_t))
        return STATUS_INVALID_PARAMETER;

    if (n != g_qc_sdc1_node && n != g_qc_sdc2_node)
        return STATUS_INVALID_DEVICE_REQUEST;

    const WCHAR* base = (n == g_qc_sdc1_node) ? L"SDC1" : L"SDC2";

    ULONG cnt = 0;
    NTSTATUS st = registry_query_u_long_in_sub_key(base, L"PerfStateCount", &cnt);
    if (!NT_SUCCESS(st))
        return st;

    ULONG sampling = 0;
    st = registry_query_u_long_in_sub_key(base, L"SamplingInterval", &sampling);
    if (!NT_SUCCESS(st))
        return st;

    if (cnt == 0 || cnt > 128)
        return STATUS_INVALID_PARAMETER;

    SIZE_T bytes = sizeof(sdbus_pstate_t) * (SIZE_T)cnt;
    ULONG tag = (n == g_qc_sdc1_node) ? 'Aepq' : 'Bepq';
    sdbus_pstate_t* states = (sdbus_pstate_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, tag);
    if (!states)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(states, bytes);

    WCHAR path[260];
    for (ULONG i = 0; i < cnt; ++i) {
        (VOID) RtlStringCchPrintfW(path, RTL_NUMBER_OF(path), L"%s\\P%u", base, (unsigned int)i);

        ULONG v = 0;
        st = registry_query_u_long_in_sub_key(path, L"MaxUtilizationThreshold", &v);
        if (!NT_SUCCESS(st))
            break;
        states[i].max_utilization_threshold = v;

        st = registry_query_u_long_in_sub_key(path, L"MinUtilizationThreshold", &v);
        if (!NT_SUCCESS(st))
            break;
        states[i].min_utilization_threshold = v;

        st = registry_query_u_long_in_sub_key(path, L"Frequency", &v);
        if (!NT_SUCCESS(st))
            break;
        states[i].freq = v;

        st = registry_query_u_long_in_sub_key(path, L"Performance", &v);
        if (!NT_SUCCESS(st))
            break;
        states[i].performance = v;

        states[i].nominal_power = 0;
    }

    if (!NT_SUCCESS(st)) {
        ExFreePool(states);
        return st;
    }

    if (n->sdbus_perf_states)
        ExFreePool(n->sdbus_perf_states);

    n->sdbus_perf_states = states;
    n->sdbus_perf_state_cnt = cnt;
    n->sdbus_sampling_interval = (ULONGLONG)sampling;

    if (n->sdbus_current_idx >= cnt)
        n->sdbus_current_idx = 0;
    if (!n->sdbus_last_change100ns)
        n->sdbus_last_change100ns = pep_now100ns();
    pep_sdbus_mirror_res(n);

    sdbus_pstate_reg_t* out = (sdbus_pstate_reg_t*)r->out_buf;
    out->sampling_interval = (ULONGLONG)sampling;
    out->perf_state_cnt = cnt;
    out->perf_states = states;

    r->bytes_returned = sizeof(sdbus_pstate_reg_t);
    return STATUS_SUCCESS;
}

NTSTATUS pep_handle_sdbus_mmc_info(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r)
{
    if (!r || !r->in_buf || r->in_buf_size < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;

    ULONG freq = *(volatile ULONG*)r->in_buf;

    if (n == g_qc_sdc1_node)
        g_sdc1_maximum_bus_freq = freq;
    else if (n == g_qc_sdc2_node)
        g_sdc2_maximum_bus_freq = freq;

    if (n)
        n->sdbus_max_bus_freq = freq;

    pep_sdbus_mirror_res(n);

    r->bytes_returned = 0;
    return STATUS_SUCCESS;
}

NTSTATUS pep_handle_sdbus_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r)
{
    if (!r || !r->in_buf || r->in_buf_size < sizeof(ULONG))
        return STATUS_INVALID_PARAMETER;

    ULONG req_idx = *(volatile ULONG*)r->in_buf;
    ULONG out_idx = req_idx;

    if (n && n->sdbus_perf_states && n->sdbus_perf_state_cnt) {
        ULONG cnt = n->sdbus_perf_state_cnt;
        if (out_idx >= cnt)
            out_idx = cnt - 1;

        ULONG max_bus = n->sdbus_max_bus_freq;
        ULONG target_freq = pep_sdbus_get_state_freq(n, out_idx);

        if (max_bus) {
            ULONG best_idx = out_idx;
            ULONG best_freq = 0;

            for (ULONG i = 0; i < cnt; i++) {
                ULONG f = pep_sdbus_get_state_freq(n, i);
                if (!f || f > max_bus)
                    continue;

                if (target_freq) {
                    if (f <= target_freq && f >= best_freq) {
                        best_freq = f;
                        best_idx = i;
                    }
                } else {
                    if (f >= best_freq) {
                        best_freq = f;
                        best_idx = i;
                    }
                }
            }

            out_idx = best_idx;
        }
    } else {

        if (n == g_qc_sdc1_node) {
            if (req_idx == 1) {
                out_idx = 3;
            } else if (req_idx == 2) {
                if (g_sdc1_p_state_idx == 3) {
                    if (g_sdc1_maximum_bus_freq == 400000)
                        out_idx = 0;
                    else if (g_sdc1_maximum_bus_freq == 200000)
                        out_idx = 1;
                } else if (g_sdc1_p_state_idx == 1) {
                    out_idx = 3;
                }
            }
        }
    }

    if (n == g_qc_sdc1_node)
        g_sdc1_p_state_idx = out_idx;
    else if (n == g_qc_sdc2_node)
        g_sdc2_p_state_idx = out_idx;

    if (n) {
        pep_sdbus_update_stats(n, out_idx);
        n->sdbus_current_idx = out_idx;

        if (n->current_p_state && n->comp_cnt)
            n->current_p_state[0] = out_idx;

        pep_sdbus_mirror_res(n);
    }

    if (r->out_buf && r->out_buf_size >= sizeof(ULONG)) {
        *(ULONG*)r->out_buf = out_idx;
        r->bytes_returned = sizeof(ULONG);
    } else {
        r->bytes_returned = 0;
    }

    return STATUS_SUCCESS;
}

const GUID guid_power_change_p_state = {
    872733356u, 36396u, 16868u, {132u, 88u, 49u, 88u, 229u, 173u, 68u, 43u}};
const GUID guid_power_change_p_state_v2 = {
    2571285598u, 11412u, 16883u, {161u, 92u, 193u, 165u, 145u, 199u, 4u, 105u}};

const GUID guid_sdbus_pstate_req = {
    1479800911u, 33071u, 19654u, {139u, 175u, 25u, 130u, 189u, 195u, 170u, 226u}};

/* canonical GUID pointer survives caller notify lifetime and remains valid after the
 * queued context is freed. Unknown commands cannot queue. */

const GUID* pep_stable_queued_power_code(_In_opt_ const GUID* code)
{
    if (!code)
        return NULL;
    __try {
        if (IsEqualGUID(code, &guid_power_change_p_state))
            return &guid_power_change_p_state;
        if (IsEqualGUID(code, &guid_power_change_p_state_v2))
            return &guid_power_change_p_state_v2;
        if (IsEqualGUID(code, &guid_sdbus_pstate_req))
            return &guid_sdbus_pstate_req;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return NULL;
    }
    return NULL;
}

static_assert(sizeof(pep_plugin_reg_t) == 32, "pep info ARM64 size");
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, dev_notify) == 8);
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, proc_notify) == 16);
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, acpi_notify) == 24);
/* check the PEP_INFORMATION layout against the WDK ABI at compile time. */

C_ASSERT(sizeof(pep_plugin_reg_t) == sizeof(PEP_INFORMATION));
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, dev_notify) ==
         FIELD_OFFSET(PEP_INFORMATION, AcceptDeviceNotification));
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, proc_notify) ==
         FIELD_OFFSET(PEP_INFORMATION, AcceptProcessorNotification));
C_ASSERT(FIELD_OFFSET(pep_plugin_reg_t, acpi_notify) ==
         FIELD_OFFSET(PEP_INFORMATION, AcceptAcpiNotification));

static volatile LONG g_pep_plugin_registered;

static_assert(FIELD_OFFSET(pep_proc_query_caps_t, directed_p_state) == 8, "pep caps offset");

static pep_msg_handler_t dev_msg_handler_table[41] = {
    {0u, NULL},
    {1u, pep_prepare_dev},
    {2u, pep_abandon_dev},
    {3u, pep_reg_dev},
    {4u, pep_unregister_dev},
    {5u, pep_dev_power_state},
    {6u, NULL},
    {7u, NULL},
    {8u, NULL},
    {9u, NULL},
    {10u, NULL},
    {11u, NULL},
    {12u, NULL},
    {13u, pep_work},
    {14u, pep_power_control_req},
    {15u, pep_power_control_complete},
    {16u, pep_parse_error_data},
    {17u, NULL},
    {18u, NULL},
    {19u, pep_notify_comp_idle_state},
    {20u, NULL},
    {21u, pep_reg_debugger},
    {22u, NULL},
    {23u, NULL},
    {24u, NULL},
    {25u, pep_reg_crashdump_dev},
    {26u, pep_dev_idle_constraints},
    {27u, pep_comp_idle_constraints},
    {28u, pep_query_comp_perf_caps},
    {29u, pep_query_comp_perf_set},
    {30u, pep_query_comp_perf_set_name},
    {31u, pep_query_comp_perf_states},
    {32u, pep_reg_comp_perf_states},
    {33u, pep_req_comp_perf_state},
    {34u, pep_query_current_comp_perf_state},
    {35u, NULL},
    {36u, pep_dpm_query_soc_subsystem_cnt},
    {37u, pep_dpm_query_soc_subsystem},
    {38u, pep_dpm_reset_soc_subsystem_accounting},
    {39u, pep_dpm_query_soc_subsystem_blocking_time},
    {40u, pep_dpm_query_soc_subsystem_metadata},
};

static pep_proc_msg_handler_t proc_msg_handler_table[44] = {
    {0u, NULL},
    {1u, pep_query_caps},
    {2u, NULL},
    {3u, NULL},
    {4u, NULL},
    {5u, NULL},
    {6u, NULL},
    {7u, NULL},
    {8u, NULL},
    {9u, pep_query_feedback_counters},
    {10u, pep_feedback_read},
    {11u, pep_query_perf_caps},
    {12u, pep_perf_constraints},
    {13u, NULL},
    {14u, NULL},
    {15u, NULL},
    {16u, NULL},
    {17u, NULL},
    {18u, NULL},
    {19u, NULL},
    {20u, NULL},
    {21u, NULL},
    {22u, NULL},
    {23u, pep_query_platform_state_residencies},
    {24u, pep_query_veto_reasons},
    {25u, pep_query_veto_reason},
    {26u, pep_query_enumerate_boot_vetoes},
    {27u, NULL},
    {28u, NULL},
    {29u, NULL},
    {30u, NULL},
    {31u, pep_park_selection_v2},
    {32u, pep_park_mask},
    {33u, pep_perf_check_complete},
    {34u, pep_override_lpi_support},
    {35u, pep_update_lpi_proc_states},
    {36u, pep_update_lpi_coordinated_states},
    {37u, pep_idle_lpi_pre_execute},
    {38u, pep_idle_lpi_complete},
    {39u, pep_ppm_enter_system_state},
    {40u, pep_ppm_from_system_state},
    {41u, pep_query_discrete_perf_states},
    {42u, pep_query_domain_info},
    {43u, pep_perf_set_state},
};

static BOOLEAN __fastcall pep_dev_notify(_In_ ULONG notify, _Inout_opt_ PVOID data)
{
    ULONG_PTR rc;
    if (notify >= ARRAYSIZE(dev_msg_handler_table)) {
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, notify, STATUS_INVALID_PARAMETER, 0, 0, 0, 0);
        return 0;
    }
    if (notify == 7) {
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, notify, STATUS_SUCCESS, 1, 0, 0, 0);
        return 1;
    }
    pep_dev_handler_t fn = dev_msg_handler_table[notify].handler_func;
    if (!fn) {
        ULONG lo = (ULONG)(ULONG_PTR)data;
        ULONG hi = (ULONG)((ULONG_PTR)data >> 32);
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, notify, STATUS_INVALID_DEVICE_REQUEST, 0, lo, hi, 0);
        return 0;
    }

    if (notify != dev_msg_handler_table[notify].notify) {
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, notify, STATUS_INVALID_PARAMETER,
                      dev_msg_handler_table[notify].notify, 0, 0, 0);
    }

    rc = fn(data);
    {
        ULONG lo = (ULONG)(ULONG_PTR)data;
        ULONG hi = (ULONG)((ULONG_PTR)data >> 32);
        pep_log_write(PEPLOG_EVT_DEV_NOTIFY, notify, STATUS_SUCCESS, (ULONG)rc, lo, hi, 0);
    }
    return (BOOLEAN)(rc != 0);
}

static BOOLEAN __fastcall pep_proc_notify(_In_opt_ PVOID pep_handle, _In_ ULONG notify,
                                          _Inout_opt_ PVOID data)
{
    if (notify >= ARRAYSIZE(proc_msg_handler_table)) {
        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, notify, STATUS_INVALID_PARAMETER, 0, 0, 0, 0);
        return 0;
    }

    if (pep_handle) {
        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);
        if (cpu < RTL_NUMBER_OF(g_cpu_kernel_handle)) {
            PVOID kernel_handle = pep_extract_kernel_handle_from_pep_handle(pep_handle);
            g_cpu_pep_handle[cpu] = pep_handle;
            g_cpu_kernel_handle[cpu] = kernel_handle ? kernel_handle : pep_handle;
        }
        if (g_vetoes_enumerated)
            pep_flush_pending_vetoes();
    }

    pep_proc_handler_t fn = proc_msg_handler_table[notify].handler_func;
    if (!fn) {
        ULONG lo = (ULONG)(ULONG_PTR)data;
        ULONG hi = (ULONG)((ULONG_PTR)data >> 32);
        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, notify, STATUS_INVALID_DEVICE_REQUEST, 0, lo, hi, 0);
        return 0;
    }

    if (notify != proc_msg_handler_table[notify].notify) {
        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, notify, STATUS_INVALID_PARAMETER,
                      proc_msg_handler_table[notify].notify, 0, 0, 0);
    }

    fn(data, pep_handle);
    {
        ULONG lo = (ULONG)(ULONG_PTR)data;
        ULONG hi = (ULONG)((ULONG_PTR)data >> 32);
        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, notify, STATUS_SUCCESS, 1, lo, hi, 0);
    }
    return 1;
}

static NTSTATUS reg_pep_plugin(VOID)
{
    /* pofx reg is one-shot. A concurrent caller must not mistake a reg still in progress for a
     * successfully registered PEP. */

    LONG reg_state = InterlockedCompareExchange(&g_pep_plugin_registered, 1, 0);
    if (reg_state == 2)
        return STATUS_SUCCESS;
    if (reg_state != 0)
        return STATUS_DEVICE_NOT_READY;

    UNICODE_STRING us;
    RtlInitUnicodeString(&us, L"PoFxRegisterPluginEx");

    pfn_pofx_reg_plugin_ex_t pfn = (pfn_pofx_reg_plugin_ex_t)MmGetSystemRoutineAddress(&us);
    if (!pfn) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "driver: PoFxRegisterPluginEx unavailable");
        InterlockedExchange(&g_pep_plugin_registered, 0);
        return STATUS_PROCEDURE_NOT_FOUND;
    }

    /* let the WDK define the reg ABI. */

    PEP_INFORMATION reg;
    RtlZeroMemory(&reg, sizeof(reg));
    reg.Version = PEP_INFORMATION_VERSION;
    reg.Size = (USHORT)sizeof(reg);
    reg.AcceptDeviceNotification = pep_dev_notify;
    /* processor performance requires independently authorized DVFS transport and readback; RSC/DPM
     * admission does not establish it. */

    BOOLEAN cpu_owner_verified = molokai_transport_ownership_verified();
    ULONG dvfs_requested = registry_query_u_long(L"MolokaiDvfsEnable", 0);
    reg.AcceptProcessorNotification =
        (cpu_owner_verified && dvfs_requested == 1u) ? pep_proc_notify : NULL;
    if (!reg.AcceptProcessorNotification)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_PPM,
                    "ppm: performance callbacks disabled reason=DVFS-owner-unverified");
    /* firmware AML owns ACPI methods until an audited ACPI PEP provider exists. */
    reg.AcceptAcpiNotification = NULL;

    RtlZeroMemory(&g_kernel_info, sizeof(g_kernel_info));
    g_kernel_info.Version = PEP_KERNEL_INFORMATION_VERSION;
    g_kernel_info.Size = (USHORT)sizeof(g_kernel_info);

    /* pep_FLAG_WORKER_CONCURRENCY (bit 0) permits concurrent worker callbacks. The shared
     * worker queue and DPM paths have not been proven reentrant. */

    NTSTATUS status = pfn(&reg, 0, &g_kernel_info);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "driver: PoFxRegisterPluginEx failed status=%08X", status);
        InterlockedExchange(&g_pep_plugin_registered, 0);
        return status;
    }

    InterlockedExchange(&g_pep_plugin_registered, 2);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "driver: plugin registered");
    return STATUS_SUCCESS;
}

/* not a Windows PnP HWID and must not be reconstructed by fuzzy matching. The graph must
 * be constructed from live ACPI, all device power/clock/ICB/PMIC providers and component
 * F-states must be verified, and the graph plus its provider references must outlive all
 * PoFx callbacks before any boot. */

res_graph_t* g_molokai_dpm_authoritative_graph;
npa_registry_t* g_molokai_npa_authoritative_clients;

static int pep_npa_scope_ready_held(void* ctx_arg, const char* path)
{
    const dpm_scope_admission_t* ctx = (const dpm_scope_admission_t*)ctx_arg;
    return ctx && ctx->npa_clients && ctx->graph &&
           npa_dev_clients_ready_held(ctx->npa_clients, ctx->graph, path);
}

NTSTATUS pep_publish_verified_molokai_graph(_Inout_ res_graph_t* graph,
                                            _Inout_ npa_registry_t* npa_clients)
{
    if (!graph || !npa_clients || !npa_clients->active || npa_clients->quarantined ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !graph->comp_inventory_verified || !graph->sealed ||
        graph->retired || graph->dev_cnt == 0u || graph->dev_cnt > GRAPH_MAX_DEVICES ||

        InterlockedCompareExchange(&g_pep_plugin_registered, 0, 0) != 2 ||
        g_molokai_dpm_authoritative_graph)
        return STATUS_INVALID_DEVICE_STATE;
    /* atomic publication under ONE graph lock. The held variants must NOT reacquire this lock. */

    if (npa_clients->owner_graph != graph || !graph->acquire || !graph->release ||
        !graph->acquire(graph->lock_ctx))
        return STATUS_INVALID_DEVICE_STATE;
    dpm_scope_admission_t scope_ctx = {npa_clients, graph};
    uint32_t eligible_devices = 0u;
    NTSTATUS admission = STATUS_SUCCESS;
    if (!graph->sealed || !graph->comp_inventory_verified || graph->retired || graph->quiescing ||
        !npa_clients->active || npa_clients->quarantined ||
        !graph_audit_dstate_scopes_held(graph, &scope_ctx, pep_npa_scope_ready_held,
                                        &eligible_devices))
        admission = STATUS_NOT_SUPPORTED;
    /* the graph is published only after the entire NPA registry activated successfully;
     * exact-path PREPARE still declines any unsupported peer. Every COMPONENT/F/P/crashdump
     * scope stays declined until implemented. */

    /* physical built six graph devices, but the old "verified graph published" line did not
     * distinguish an admitted D-state-only scope from a COMPONENT/F/P-state scope. Inspect the
     * exact held graph/NPA client gates without issuing a provider vote. */

    {
        uint32_t i;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "resources: graph=%lu D-state-eligible=%lu clients=%lu status=%08X votes=0",
                    graph->dev_cnt, eligible_devices, npa_clients->cnt, admission);
        for (i = 0u; i < graph->dev_cnt; ++i) {
            const graph_dev_t* d = &graph->devices[i];
            int graph_claim = graph_can_claim_held(graph, d->path);
            int client_ready = graph_claim && pep_npa_scope_ready_held(&scope_ctx, d->path);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "fstate: path=%s empty-F0 profile=%u",
                        d->path, (unsigned)d->verified_ic_empty_f0);
            TraceEvents(
                TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                "resources: path=%s D0=%lu D3=%lu components=%u graph=%u clients=%u eligible=%u",
                d->path, (ULONG)d->d0.action_cnt, (ULONG)d->d3.action_cnt,
                (unsigned)d->requires_comp_runtime, (unsigned)graph_claim, (unsigned)client_ready,
                (unsigned)(graph_claim && client_ready));
        }
    }
    if (NT_SUCCESS(admission)) {
        /* freeze all provider slots BEFORE the graph becomes visible to PoFx callbacks. The AML
         * builder sealed the graph earlier, before native NPA client activation; allows only a
         * single prepublication kind-8 bind during that interval. */

        if (!graph_freeze_providers_held(graph))
            admission = STATUS_INVALID_DEVICE_STATE;
        else {
            g_molokai_npa_authoritative_clients = npa_clients;
            g_molokai_dpm_authoritative_graph = graph;
        }
    }
    graph->release(graph->lock_ctx);
    return admission;
}

/* the caller must retain the graph and its providers afterwards: nothing here runs down an
 * in-flight PoFx callback, so the storage can never be freed. */

NTSTATUS pep_unpublish_verified_molokai_graph(VOID)
{
    res_graph_t* graph = g_molokai_dpm_authoritative_graph;
    if (!graph || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return STATUS_INVALID_DEVICE_STATE;
    g_molokai_dpm_authoritative_graph = NULL;
    g_molokai_npa_authoritative_clients = NULL;
    graph->release(graph->lock_ctx);
    TraceEvents(TRACE_LEVEL_ERROR, TRACE_POWER, "dpm: graph withdrawn admission=closed");
    return STATUS_SUCCESS;
}

BOOLEAN pep_resolve_molokai_dev(_In_ const UNICODE_STRING* dev_id, _Out_writes_(256) char path[256])
{
    if (!dev_id || !dev_id->Buffer || !dev_id->Length || dev_id->Length > dev_id->MaximumLength ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !g_molokai_dpm_authoritative_graph)
        return FALSE;
    return (BOOLEAN)resolve_pep_dev_path(g_molokai_dpm_authoritative_graph,
                                         (const uint16_t*)dev_id->Buffer, (size_t)dev_id->Length,
                                         path);
}

power_result_t pep_run_molokai_lifecycle(_In_ const UNICODE_STRING* dev_id, _In_ BOOLEAN prepare)
{
    power_result_t result = POWER_UNSUPPORTED;
    power_scratch_t* scratch = NULL;
    power_execution_t trace;
    char path[256];
    if (!pep_resolve_molokai_dev(dev_id, path))
        return result;
    /* this Molokai scope is d0/d3 only (zero PoFx components). Only when the independently
     * verified upstream backend/graph/client checks have passed may prepare and verify d0. */

    if (prepare) {
        RtlZeroMemory(&trace, sizeof(trace));
        scratch =
            (power_scratch_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
        if (!scratch)
            return POWER_BAD_INPUT;
        RtlZeroMemory(scratch, sizeof(*scratch));
        result = graph_prepare_d0(g_molokai_dpm_authoritative_graph, path, &trace, scratch);
        ExFreePool(scratch);
        return result;
    }
    RtlZeroMemory(&trace, sizeof(trace));
    scratch = (power_scratch_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
    if (!scratch)
        return POWER_BAD_INPUT;
    RtlZeroMemory(scratch, sizeof(*scratch));
    result = graph_abandon_d3(g_molokai_dpm_authoritative_graph, path, &trace, scratch);
    ExFreePool(scratch);
    return result;
}

/* execute D-state sequences on PoFx transitions without repeating PREPARE's verified D0. Providers
 * retain their authorization and lifetime requirements. */

static power_result_t pep_run_molokai_d_state(_In_ const UNICODE_STRING* dev_id, _In_ ULONG state)
{
    power_scratch_t* scratch;
    power_sequence_t* expanded = NULL;
    power_execution_t trace;
    power_result_t result;
    ULONG i, nested = 0u;
    char path[256];
    RtlZeroMemory(&trace, sizeof(trace));
    if (!g_molokai_dpm_authoritative_graph || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !pep_resolve_molokai_dev(dev_id, path))
        return POWER_UNSUPPORTED;
    scratch = (power_scratch_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
    if (!scratch)
        return POWER_BAD_INPUT;
    RtlZeroMemory(scratch, sizeof(*scratch));
    result = graph_transition(g_molokai_dpm_authoritative_graph, path, state, &trace, scratch);
    ExFreePool(scratch);
    if (result == POWER_OK)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "dpm: path=%s D%lu actions=%lu verified",
                    path, state, trace.applied);
    else
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: path=%s D%lu result=%d action=%lu applied=%lu restored=%lu", path, state,
                    (int)result, trace.failure_idx, trace.applied, trace.restored);
    return result;
}

/* the callback bridge is compiled and exact-path matched, but must not claim even one
 * device until authoritative Windows RPMh, PPP/PMIC, GCC/GDSC, ICB/BCM, GPIO and component
 * providers are live and the WDK/hardware ABI is validated. Publishing a portable graph
 * alone cannot satisfy that obligation. */

BOOLEAN pep_is_molokai_dpm_execution_ready(_In_ const UNICODE_STRING* dev_id)
{
    UNREFERENCED_PARAMETER(dev_id);
    /* fail closed unless the verified production backend is registered. */
    if (!hw_backend_ready())
        return FALSE;
    return TRUE;
}

/* native execution of SMMC f0/f1 through the same epoch-verified provider as DSDT sequences.
 * The entire F-state transaction owns ONE provider rundown lease; every hardware callback
 * receives the exact peer path. */

/* callers retain device rundown while waiting. Never hold a graph, provider, node or list
 * lock across this wait: the current transaction needs them to complete. Contention alone
 * neither writes hardware nor quarantines it. */
int pep_claim_res_transaction(volatile LONG* slot, LONG target, void* ctx, int (*ready)(void*))
{
    ULONGLONG deadline, now;
    LARGE_INTEGER delay;
    if (!slot || !ready || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    deadline = KeQueryInterruptTime() + 20000000ULL;
    for (;;) {
        if (!ready(ctx))
            return 0;
        if (InterlockedCompareExchange(slot, target, -1) == -1) {
            if (ready(ctx))
                return 1;
            InterlockedExchange(slot, -1);
            return 0;
        }
        now = KeQueryInterruptTime();
        if (now >= deadline)
            return 0;
        delay.QuadPart = -(LONGLONG)((deadline - now) < 10000ULL ? deadline - now : 10000ULL);
        if (!NT_SUCCESS(KeDelayExecutionThread(KernelMode, FALSE, &delay)))
            return 0;
    }
}

static int peer_ready(void* ctx)
{
    peer_native_scope_t* s = ctx;
    return s && s->serial && s->comp.valid && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           hw_backend_ready() && s->serial == hw_backend_verified_serial();
}

static int peer_lock(void* v)
{
    peer_native_scope_t* s = (peer_native_scope_t*)v;
    if (!s || !s->graph || !s->serial || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        s->serial != hw_backend_verified_serial())
        return 0;
    if (InterlockedCompareExchange(&s->lease_state, 1, 0) != 0)
        return 0;
    if (!s->graph->acquire || !s->graph->release || !s->graph->acquire(s->graph->lock_ctx)) {
        InterlockedExchange(&s->lease_state, 0);
        return 0;
    }
    if (!hw_backend_begin_bound(s->serial, &s->leased)) {
        s->graph->release(s->graph->lock_ctx);
        RtlZeroMemory(&s->leased, sizeof(s->leased));
        InterlockedExchange(&s->lease_state, 0);
        return 0;
    }
    InterlockedExchange(&s->lease_state, 2);
    return 1;
}

static void peer_unlock(void* v)
{
    peer_native_scope_t* s = (peer_native_scope_t*)v;
    if (!s || InterlockedCompareExchange(&s->lease_state, 3, 2) != 2)
        return;
    hw_backend_end_bound();
    RtlZeroMemory(&s->leased, sizeof(s->leased));
    s->graph->release(s->graph->lock_ctx);
    InterlockedExchange(&s->lease_state, 0);
}

static int peer_action_state(peer_native_scope_t* s)
{
    LONG f;
    if (!s || s->lease_state != 2)
        return -1;
    f = InterlockedCompareExchange(&s->target_f_state, -1, -1);
    return (f == 0 || f == 1) ? (int)f : -1;
}

power_result_t pep_run_peer_f_state(peer_native_scope_t* s, ULONG state, power_execution_t* trace,
                                    power_scratch_t* scratch)
{
    power_result_t result;
    if (!s || state > 1u ||
        !pep_claim_res_transaction(&s->target_f_state, (LONG)state, s, peer_ready))
        return POWER_UNSUPPORTED;
    result = peer_comp_transition(&s->comp, state, &s->adapter, trace, scratch);
    InterlockedExchange(&s->target_f_state, -1);
    return result;
}

static power_result_t pep_run_peer_d_state(peer_native_scope_t* s, ULONG target)
{
    power_result_t result = POWER_UNSUPPORTED;
    const power_sequence_t* sequence;
    ULONG state;
    if (!s || (target != 0u && target != 3u) ||
        !pep_claim_res_transaction(&s->target_f_state, 0, s, peer_ready))
        return result;
    /* this desc owns f0/f1 and no independent Dx program. dev d0 permits an idle f1; a
     * repeated d0 must not force the component awake. dev d3 requires the declared f1
     * withdrawal to be verified first. */
    if (s->comp.quarantined || s->comp.state.quarantined)
        goto done;
    if (!s->comp.state.state_known) {
        if (target == 0u) {
            power_execution_t trace;
            power_scratch_t* scratch =
                ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
            if (scratch) {
                result = peer_comp_transition(&s->comp, 0u, &s->adapter, &trace, scratch);
                ExFreePool(scratch);
            }
        }
        goto done;
    }
    state = s->comp.state.current_d_state;
    if (state > 1u || (target == 3u && state != 1u))
        goto done;
    InterlockedExchange(&s->target_f_state, (LONG)state);
    sequence = state == 0u ? &s->comp.f0 : &s->comp.f1;
    if (s->adapter.lock && s->adapter.unlock && s->adapter.verify_current_state &&
        s->adapter.lock(s)) {
        if (s->adapter.verify_current_state(s, sequence))
            result = POWER_OK;
        else
            s->comp.state.state_known = 0u;
        s->adapter.unlock(s);
    }
done:
    InterlockedExchange(&s->target_f_state, -1);
    return result;
}

static int peer_supports(void* v, const power_action_t* a)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.supports &&
           s->leased.supports(s->leased.ctx, s->comp.path, a && (uint32_t)peer_action_state(s), a);
}

static int peer_snapshot(void* v, const power_action_t* a, power_snapshot_t* snap)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.snapshot &&
           s->leased.snapshot(s->leased.ctx, s->comp.path, (uint32_t)peer_action_state(s), a, snap);
}

static int peer_apply(void* v, const power_action_t* a)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.apply &&
           s->leased.apply(s->leased.ctx, s->comp.path, (uint32_t)peer_action_state(s), a);
}

static int peer_verify(void* v, const power_action_t* a)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.verify &&
           s->leased.verify(s->leased.ctx, s->comp.path, (uint32_t)peer_action_state(s), a);
}

static int peer_restore(void* v, const power_action_t* a, const power_snapshot_t* snap)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.restore &&
           s->leased.restore(s->leased.ctx, s->comp.path, (uint32_t)peer_action_state(s), a, snap);
}

static int peer_verify_restored(void* v, const power_action_t* a, const power_snapshot_t* snap)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && peer_action_state(s) >= 0 && s->leased.verify_restored &&
           s->leased.verify_restored(s->leased.ctx, s->comp.path, (uint32_t)peer_action_state(s), a,
                                     snap);
}

static int peer_verify_final(void* v, const power_sequence_t* sequence)
{
    peer_native_scope_t* s = v;
    return s && s->lease_state == 2 && s->leased.verify_final && sequence &&
           s->leased.verify_final(s->leased.ctx, s->comp.path, sequence->target_d_state, sequence);
}

static VOID peer_init_adapter(peer_native_scope_t* s)
{
    power_backend_t* b = &s->adapter;
    RtlZeroMemory(b, sizeof(*b));
    b->ctx = s;
    b->lock = peer_lock;
    b->unlock = peer_unlock;
    b->supports = peer_supports;
    b->snapshot = peer_snapshot;
    b->apply = peer_apply;
    b->verify = peer_verify;
    b->restore = peer_restore;
    b->verify_restored = peer_verify_restored;
    b->verify_current_state = peer_verify_final;
    s->target_f_state = -1;
}

BOOLEAN runtime_acpi_path(const UNICODE_STRING* id, char out[256])
{
    SIZE_T i, n;
    if (!out)
        return FALSE;
    out[0] = 0;
    if (!id || !id->Buffer || !id->Length || id->Length > id->MaximumLength || (id->Length & 1u))
        return FALSE;
    n = id->Length / sizeof(WCHAR);
    if (n < 5u || n >= 256u || id->Buffer[0] != L'\\')
        return FALSE;
    for (i = 0; i < n; ++i) {
        WCHAR ch = id->Buffer[i];
        if (ch < 0x21u || ch > 0x7eu) {
            out[0] = 0;
            return FALSE;
        }
        out[i] = (char)ch;
    }
    out[n] = 0;
    return TRUE;
}

/* generic runtime DSTATE provider adapter. The shared graph lock is acquired BEFORE the
 * provider lease; DSDT and runtime clients must not mutate the backend's shared last-vote
 * cache concurrently. */

static int runtime_d_lock(void* v)
{
    runtime_dstate_native_t* s = v;
    if (!s || !s->serial || !s->graph || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        s->serial != hw_backend_verified_serial() ||
        InterlockedCompareExchange(&s->lease_state, 1, 0) != 0)
        return 0;
    if (!s->graph->acquire || !s->graph->release || !s->graph->acquire(s->graph->lock_ctx)) {
        InterlockedExchange(&s->lease_state, 0);
        return 0;
    }
    if (!hw_backend_begin_bound(s->serial, &s->leased)) {
        s->graph->release(s->graph->lock_ctx);
        InterlockedExchange(&s->lease_state, 0);
        return 0;
    }
    InterlockedExchange(&s->lease_state, 2);
    return 1;
}

static void runtime_d_unlock(void* v)
{
    runtime_dstate_native_t* s = v;
    if (!s || InterlockedCompareExchange(&s->lease_state, 3, 2) != 2)
        return;
    hw_backend_end_bound();
    RtlZeroMemory(&s->leased, sizeof(s->leased));
    s->graph->release(s->graph->lock_ctx);
    InterlockedExchange(&s->lease_state, 0);
}

static int runtime_d_state(runtime_dstate_native_t* s)
{
    LONG state;
    if (!s || s->lease_state != 2)
        return -1;
    state = InterlockedCompareExchange(&s->target_d_state, -1, -1);
    return (state >= 0 && state <= 3 &&
            (s->program.idx.d_state_mask & (uint8_t)(1u << (uint32_t)state)))
               ? (int)state
               : -1;
}

static int runtime_d_supports(void* v, const power_action_t* a)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.supports &&
           s->leased.supports(s->leased.ctx, s->program.idx.path, (uint32_t)state, a);
}

static int runtime_d_snapshot(void* v, const power_action_t* a, power_snapshot_t* snap)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.snapshot &&
           s->leased.snapshot(s->leased.ctx, s->program.idx.path, (uint32_t)state, a, snap);
}

static int runtime_d_apply(void* v, const power_action_t* a)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.apply &&
           s->leased.apply(s->leased.ctx, s->program.idx.path, (uint32_t)state, a);
}

static int runtime_d_verify(void* v, const power_action_t* a)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.verify &&
           s->leased.verify(s->leased.ctx, s->program.idx.path, (uint32_t)state, a);
}

static int runtime_d_restore(void* v, const power_action_t* a, const power_snapshot_t* snap)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.restore &&
           s->leased.restore(s->leased.ctx, s->program.idx.path, (uint32_t)state, a, snap);
}

static int runtime_d_verify_restored(void* v, const power_action_t* a, const power_snapshot_t* snap)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    return state >= 0 && s->leased.verify_restored &&
           s->leased.verify_restored(s->leased.ctx, s->program.idx.path, (uint32_t)state, a, snap);
}

static int runtime_d_verify_final(void* v, const power_sequence_t* sequence)
{
    runtime_dstate_native_t* s = v;
    int state = runtime_d_state(s);
    if (state < 0 || !sequence || !s->leased.verify_final)
        return 0;
    if (sequence->action_cnt) {
        return s->leased.verify_final(s->leased.ctx, s->program.idx.path, (uint32_t)state,
                                      sequence);
    }
    /* under the owner/graph lock, verify the last resource-voting sequence. Empty actions cannot
     * establish success or authorize inferred withdrawals. */

    if ((state != 1 && state != 2) || !s->program.physical.state_known ||
        s->program.physical.quarantined)
        return 0;
    {
        const ULONG prev = s->program.last_nonempty_target;
        const power_sequence_t* prior = (prev == 0u)   ? &s->program.d0
                                        : (prev == 1u) ? &s->program.d1
                                        : (prev == 2u) ? &s->program.d2
                                                       : &s->program.d3;
        return prior->action_cnt && prior->target_d_state == prev &&
               s->leased.verify_final(s->leased.ctx, s->program.idx.path, prev, prior);
    }
}

static VOID runtime_d_init_adapter(runtime_dstate_native_t* s)
{
    power_backend_t* b = &s->adapter;
    RtlZeroMemory(b, sizeof(*b));
    b->ctx = s;
    b->lock = runtime_d_lock;
    b->unlock = runtime_d_unlock;
    b->supports = runtime_d_supports;
    b->snapshot = runtime_d_snapshot;
    b->apply = runtime_d_apply;
    b->verify = runtime_d_verify;
    b->restore = runtime_d_restore;
    b->verify_restored = runtime_d_verify_restored;
    b->verify_current_state = runtime_d_verify_final;
    s->target_d_state = -1;
}

static int runtime_d_ready(void* ctx)
{
    runtime_dstate_native_t* scope = ctx;
    return scope && scope->program.valid && scope->serial && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           hw_backend_ready() && scope->serial == hw_backend_verified_serial();
}

power_result_t pep_runtime_d_execute(runtime_dstate_native_t* scope, ULONG target)
{
    power_scratch_t* scratch;
    power_execution_t trace;
    power_result_t result;
    if (!scope || !scope->program.valid || !scope->serial || !hw_backend_ready() ||
        scope->serial != hw_backend_verified_serial() || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        target > 3u || !(scope->program.idx.d_state_mask & (uint8_t)(1u << target)))
        return POWER_UNSUPPORTED;
    if (!pep_claim_res_transaction(&scope->target_d_state, (LONG)target, scope, runtime_d_ready))
        return POWER_UNSUPPORTED;
    scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
    if (!scratch) {
        InterlockedExchange(&scope->target_d_state, -1);
        return POWER_BAD_INPUT;
    }
    RtlZeroMemory(&trace, sizeof(trace));
    result = runtime_dstate_execute(&scope->program, target, &scope->adapter, &trace, scratch);
    ExFreePool(scratch);
    InterlockedExchange(&scope->target_d_state, -1);
    if (result != POWER_OK &&
        (!(scope->program.quarantined || scope->program.physical.quarantined) ||
         InterlockedCompareExchange(&scope->terminal_failure_reported, 1, 0) == 0)) {
        const power_sequence_t* sequence = (target == 0u)   ? &scope->program.d0
                                           : (target == 1u) ? &scope->program.d1
                                           : (target == 2u) ? &scope->program.d2
                                                            : &scope->program.d3;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: path=%s D%lu result=%d action=%lu applied=%lu restored=%lu",
                    scope->program.idx.path, target, (int)result, trace.failure_idx, trace.applied,
                    trace.restored);
        /* an unsupported preflight action never touches MMIO. report its desc identity rather than
         * guessing which native provider failed. This is diagnostic only and never changes
         * admission. */
        if (result == POWER_UNSUPPORTED && trace.failure_idx < sequence->action_cnt) {
            const power_action_t* a = &sequence->actions[trace.failure_idx];
            if (a->kind == POWER_CLOCK)
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK, "clock: unsupported name=%s vote=%lu",
                            a->u.clock.name, (ULONG)a->u.clock.vote);
            else if (a->kind == POWER_ICB)
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                            "icb: path=%s preflight rejected route=%s->%s", scope->program.idx.path,
                            a->u.icb.master, a->u.icb.slave);
            else if (a->kind == POWER_TLMM_GPIO)
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "tlmm: unsupported pin=%lu",
                            (ULONG)a->u.gpio.fields[0]);
            else
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "resources: unsupported kind=%lu",
                            (ULONG)a->kind);
        }
    }
    return result;
}

/* the PEP never manufactures a successful d0 vote. */

static int runtime_clock_ready(void* ctx)
{
    runtime_dstate_native_t* scope = ctx;
    return runtime_d_ready(scope) && !scope->program.quarantined &&
           !scope->program.physical.quarantined;
}

static int runtime_clock_begin(void* ctx)
{
    runtime_dstate_native_t* scope = ctx;
    ULONG state;
    if (!scope || !pep_claim_res_transaction(&scope->target_d_state, 0, scope, runtime_d_ready))
        return 0;
    /* read completed proof only after acquiring the shared D/F/clock slot. A predecessor may
     * temporarily invalidate it while changing state. */
    if (runtime_clock_ready(scope) && scope->program.physical.state_known &&
        (state = scope->program.physical.current_d_state) <= 3u &&
        (scope->program.idx.d_state_mask & (1u << state))) {
        InterlockedExchange(&scope->target_d_state, (LONG)state);
        return 1;
    }
    InterlockedExchange(&scope->target_d_state, -1);
    return 0;
}

static void runtime_clock_end(void* ctx)
{
    runtime_dstate_native_t* scope = ctx;
    InterlockedExchange(&scope->target_d_state, -1);
}

static int runtime_clock_read(void* ctx, const char* name, uint64_t* out)
{
    runtime_dstate_native_t* scope = ctx;
    uint64_t first, second;
    ULONG i;
    int ok = 0;
    if (!runtime_clock_ready(scope) || !scope->graph || !scope->graph->acquire ||
        !scope->graph->release || !scope->graph->acquire(scope->graph->lock_ctx))
        return 0;
    for (i = 0u; i < 4u && runtime_clock_ready(scope); ++i) {
        if (!hw_backend_win_clock_read(scope->serial, name, &first) ||
            !hw_backend_win_clock_read(scope->serial, name, &second))
            break;
        if ((uint32_t)first == (uint32_t)second) {
            *out = second;
            ok = 1;
            break;
        }
    }
    scope->graph->release(scope->graph->lock_ctx);
    return ok;
}

static int runtime_clock_dfs_catalog(void* ctx, const char* name, gcc_dfs_catalog_t* out)
{
    runtime_dstate_native_t* scope = ctx;
    return runtime_clock_ready(scope) && hw_backend_win_clock_dfs_catalog(scope->serial, name, out);
}

static clock_perf_scope_t* runtime_clock_create(runtime_dstate_native_t* scope, const void* blob,
                                                size_t bytes)
{
    clock_perf_binding_t binding = {0};
    binding.ctx = scope;
    binding.backend = &scope->adapter;
    binding.physical_state = &scope->program.physical;
    binding.ready = runtime_clock_ready;
    binding.begin = runtime_clock_begin;
    binding.read_dfs_catalog = runtime_clock_dfs_catalog;
    binding.end = runtime_clock_end;
    binding.read_clock = runtime_clock_read;
    return clock_perf_create(blob, bytes, &scope->program.idx, &binding);
}

ULONG_PTR pep_prepare_runtime_dstate(pep_prepare_dev_t* p, const char* path)
{
    runtime_dstate_native_t* scope = NULL;
    runtime_desc_info_t* info = NULL;
    ULONG i;
    pep_devnode_t* n = NULL;
    UCHAR* blob = NULL;
    ULONG bytes = 0u;
    NTSTATUS status;
    if (!p || !p->dev_id || !path || !g_molokai_dpm_authoritative_graph || !hw_backend_ready() ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 1;
    status = runtime_res_copy(path, NULL, 0, &bytes);
    if (status != STATUS_BUFFER_TOO_SMALL || bytes < 20u || bytes > AEOB_MAX_SIZE)
        return 1;
    blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    scope = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scope), POOL_TAG);
    if (!blob || !info || !scope)
        goto done;
    RtlZeroMemory(scope, sizeof(*scope));
    status = runtime_res_copy(path, blob, bytes, &bytes);
    if (!NT_SUCCESS(status) || !runtime_dstate_compile(&scope->program, blob, bytes, path, info))
        goto done;
    scope->graph = g_molokai_dpm_authoritative_graph;
    scope->serial = hw_backend_verified_serial();
    if (!scope->serial || !scope->graph->sealed || scope->graph->retired ||
        scope->graph->quiescing ||
        /* a graph device must use its already-provisioned D-state scope; inability to claim that
         * scope cannot grant a parallel owner. */

        scope->graph->dev_cnt > GRAPH_MAX_DEVICES)
        goto done;
    for (i = 0u; i < scope->graph->dev_cnt; ++i)
        if (strcmp(scope->graph->devices[i].path, path) == 0)
            goto done;
    runtime_d_init_adapter(scope);
    n = pep_get_or_create_dev_node(p->dev_id);
    if (!n || n->reg_state == DEV_REGISTERED_MINI_PEP || n->reg_state == DEV_PREPARE_ACCEPTED ||
        n->peer_scope || n->runtime_comp || n->native_owner)
        goto done;
    if (n->runtime_dstate_scope) {
        runtime_dstate_native_t* prev = n->runtime_dstate_scope;
        /* reuse only a completed lifecycle with the same immutable program and live epoch. A
         * failed/uncertain PREPARE cannot create a new scope to evade its retained votes or replay
         * an ambiguous write. */
        if ((n->reg_state != DEV_ABANDONED && n->reg_state != DEV_UNREGISTERED) ||
            prev->serial != scope->serial || prev->graph != scope->graph ||
            !prev->program.physical.state_known || prev->program.physical.quarantined ||
            prev->program.quarantined ||
            memcmp(&prev->program.idx, &scope->program.idx, sizeof(scope->program.idx)) ||
            memcmp(&prev->program.d0, &scope->program.d0, sizeof(scope->program.d0)) ||
            memcmp(&prev->program.d1, &scope->program.d1, sizeof(scope->program.d1)) ||
            memcmp(&prev->program.d2, &scope->program.d2, sizeof(scope->program.d2)) ||
            memcmp(&prev->program.d3, &scope->program.d3, sizeof(scope->program.d3)))
            goto done;
        ExFreePool(scope);
        scope = NULL;
    } else {
        n->runtime_dstate_scope = scope;
        scope = NULL;
    }
    if (pep_runtime_d_execute(n->runtime_dstate_scope, 0u) != POWER_OK)
        goto done;
    if (!n->clock_perf)
        n->clock_perf = runtime_clock_create(n->runtime_dstate_scope, blob, bytes);
    n->reg_state = DEV_PREPARE_ACCEPTED;
    pep_apply_prepare_states(n);
    ExAcquireFastMutex(&n->d_state_change_f_mutex);
    dpm_verified_record(&n->dpm_backend_state, 0u, TRUE);
    ExReleaseFastMutex(&n->d_state_change_f_mutex);
    p->dev_accepted = 1u;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, "dpm: path=%s D0 verified actions=%lu", path,
                n->runtime_dstate_scope->program.d0.action_cnt);
done:
    if (scope)
        ExFreePool(scope);
    if (info)
        ExFreePool(info);
    if (blob)
        ExFreePool(blob);
    return 1;
}

ULONG_PTR pep_prepare_reviewed_peer(pep_prepare_dev_t* p, const char* path)
{
    pep_devnode_t* n = NULL;
    peer_native_scope_t* scope = NULL;
    runtime_desc_info_t* info = NULL;
    power_scratch_t* scratch = NULL;
    power_execution_t trace;
    ULONG bytes = 0;
    UCHAR* blob = NULL;
    NTSTATUS status;
    power_result_t result;
    if (!p || !p->dev_id || !path || KeGetCurrentIrql() != PASSIVE_LEVEL || !hw_backend_ready())
        return 1;
    status = runtime_res_copy(path, NULL, 0, &bytes);
    if (status != STATUS_BUFFER_TOO_SMALL || bytes < 20u || bytes > AEOB_MAX_SIZE)
        return 1;
    blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    scope = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scope), POOL_TAG);
    scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
    if (!blob || !info || !scope || !scratch)
        goto fail;
    RtlZeroMemory(scope, sizeof(*scope));
    status = runtime_res_copy(path, blob, bytes, &bytes);
    if (!NT_SUCCESS(status) || !peer_comp_compile(&scope->comp, blob, bytes, path, info))
        goto fail;
    scope->serial = hw_backend_verified_serial();
    scope->graph = g_molokai_dpm_authoritative_graph;
    if (!scope->serial || !scope->graph || !scope->graph->sealed || scope->graph->retired ||
        scope->graph->quiescing)
        goto fail;
    /* a published DSDT graph device cannot admit a second peer client when its current claim fails.
     */

    {
        uint32_t i;
        for (i = 0u; i < scope->graph->dev_cnt; ++i)
            if (strcmp(scope->graph->devices[i].path, path) == 0)
                goto fail;
    }
    peer_init_adapter(scope);
    n = pep_get_or_create_dev_node(p->dev_id);
    if (!n || n->reg_state == DEV_REGISTERED_MINI_PEP || n->reg_state == DEV_PREPARE_ACCEPTED)
        goto fail;
    /* retain the original allocation after ABANDON: PoFx may still have stale pointers, so
     * never free and replace a previously published component scope. Reuse it only under the
     * same verified epoch and when the submitted desc is unchanged. */

    if (n->peer_scope) {
        peer_native_scope_t* prev = n->peer_scope;
        if (n->reg_state != DEV_ABANDONED || prev->serial != scope->serial || !prev->comp.valid ||
            !prev->comp.state.state_known || prev->comp.state.current_d_state != 1u ||
            prev->comp.quarantined || strcmp(prev->comp.path, scope->comp.path) ||
            memcmp(&prev->comp.f0, &scope->comp.f0, sizeof(scope->comp.f0)) ||
            memcmp(&prev->comp.f1, &scope->comp.f1, sizeof(scope->comp.f1)))
            goto fail;
        ExFreePool(scope);
        scope = prev;
    }
    RtlZeroMemory(&trace, sizeof(trace));
    result = pep_run_peer_f_state(scope, 0u, &trace, scratch);
    if (result != POWER_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "fstate: path=%s F0 failed result=%d applied=%lu restored=%lu", path,
                    (int)result, trace.applied, trace.restored);
        /* failed rollback has ambiguous hardware effects. Preserve the quarantined NEW scope on
         * its node instead of freeing its sticky record and permitting a fresh PREPARE to replay
         * the same bus/clock command. */

        if (n && scope->comp.quarantined && !n->peer_scope) {
            n->peer_scope = scope;
            scope = NULL;
        } else if (n && n->peer_scope == scope)
            scope = NULL;
        goto fail;
    }
    n->peer_scope = scope;
    n->reg_state = DEV_PREPARE_ACCEPTED;
    pep_apply_prepare_states(n);
    p->dev_accepted = 1u;
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                "fstate: path=%s F0 verified actions=%lu serial=%I64u", path,
                scope->comp.f0.action_cnt, scope->serial);
    scope = NULL;
fail:
    if (scratch)
        ExFreePool(scratch);
    if (info)
        ExFreePool(info);
    if (blob)
        ExFreePool(blob);
    if (scope)
        ExFreePool(scope);
    return 1;
}

ULONG_PTR pep_prepare_runtime_comp(pep_prepare_dev_t* p, const char* path)
{
    runtime_comp_win_t* scope = NULL;
    pep_devnode_t* n;
    UCHAR* blob = NULL;
    ULONG bytes = 0u, i;
    uint64_t serial = hw_backend_verified_serial();
    BOOLEAN accepted = FALSE;
    NTSTATUS status;
    if (!p || !path || !serial || !g_molokai_dpm_authoritative_graph ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 1;
    for (i = 0u; i < g_molokai_dpm_authoritative_graph->dev_cnt; ++i)
        if (!strcmp(path, g_molokai_dpm_authoritative_graph->devices[i].path))
            return 1;
    status = runtime_comp_res_copy(path, NULL, 0u, &bytes);
    if (status != STATUS_BUFFER_TOO_SMALL || !bytes || bytes > AEOB_MAX_SIZE)
        return 1;
    blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!blob)
        return 1;
    if (!NT_SUCCESS(runtime_comp_res_copy(path, blob, bytes, &bytes)))
        goto done;
    n = pep_get_or_create_dev_node(p->dev_id);
    if (!n || n->reg_state == DEV_REGISTERED_MINI_PEP || n->reg_state == DEV_PREPARE_ACCEPTED ||
        n->peer_scope || n->runtime_dstate_scope || n->native_owner)
        goto done;
    if (n->runtime_comp) {
        if (n->reg_state != DEV_ABANDONED || !runtime_comp_win_reopen(n->runtime_comp, blob, bytes))
            goto done;
        accepted = TRUE;
    } else {
        scope = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scope), POOL_TAG);
        if (!scope)
            goto done;
        accepted = runtime_comp_win_prepare(scope, blob, bytes, path,
                                            g_molokai_dpm_authoritative_graph, serial);
        if (accepted || scope->model.retain) {
            n->runtime_comp = scope;
            scope = NULL;
        }
    }
    if (accepted) {
        n->native_perf = &n->runtime_comp->model.endpoint;
        n->native_perf_backend_serial = serial;
        n->reg_state = DEV_PREPARE_ACCEPTED;
        pep_apply_prepare_states(n);
        ExAcquireFastMutex(&n->d_state_change_f_mutex);
        dpm_verified_record(&n->dpm_backend_state, 0u, TRUE);
        ExReleaseFastMutex(&n->d_state_change_f_mutex);
        p->dev_accepted = 1u;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "resources: path=%s components=%lu epoch=%llX", path,
                    n->runtime_comp->model.components, (unsigned long long)serial);
    }
done:
    if (scope) {
        if (scope->desc)
            ExFreePool(scope->desc);
        if (scope->workspace)
            ExFreePool(scope->workspace);
        ExFreePool(scope);
    }
    if (blob)
        ExFreePool(blob);
    return 1;
}

/* this is purely diagnostic: it neither resolves graph ownership nor authorizes a device.
 * Do not use %wZ on an unknown callback IRQL. */

BOOLEAN ascii_dev_id(_In_opt_ const UNICODE_STRING* id, char out[128])
{
    SIZE_T i, cnt;
    out[0] = '\0';
    if (!id || !id->Buffer || !id->Length || id->Length > id->MaximumLength || (id->Length & 1u) ||
        id->Length >= 256u)
        return FALSE;
    cnt = (SIZE_T)id->Length / 2u;
    for (i = 0u; i < cnt; ++i) {
        USHORT ch = (USHORT)id->Buffer[i];

        if (ch < 0x20u || ch > 0x7eu) {
            out[0] = '\0';
            return FALSE;
        }
        out[i] = (char)ch;
    }
    out[cnt] = '\0';
    return TRUE;
}

/* a component res submission can arrive independently of d0/d3. PREPARE must pin a desc
 * with exactly the generation already reviewed by the published physical graph. This is a
 * provenance gate, not a vote. */
static NTSTATUS pep_ufs_src_copy(const char* method, void* buf, ULONG capacity, ULONG* bytes)
{
    size_t length = 0u;
    uint64_t serial = hw_backend_verified_serial();
    int result;
    if (!bytes || !serial || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    result =
        graph_src_copy(g_molokai_dpm_authoritative_graph, method, serial, buf, capacity, &length);
    if (!result || length > AEOB_MAX_SIZE)
        return STATUS_NOT_FOUND;
    *bytes = (ULONG)length;
    return result == 2 ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

BOOLEAN pep_ufs_comp_desc_matches_graph(const char* path)
{
    runtime_desc_info_t* info = NULL;
    ufs_catalog_t* candidate = NULL;
    program_idx_t* idx = NULL;
    UCHAR* blob = NULL;
    ULONG bytes = 0u;
    NTSTATUS st;
    BOOLEAN matched = FALSE;
    if (!path)
        return FALSE;
    if (strcmp(path, "\\_SB.UFS0"))
        return TRUE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !g_molokai_dpm_authoritative_graph ||
        !g_molokai_dpm_authoritative_graph->sealed || g_molokai_dpm_authoritative_graph->retired ||
        g_molokai_dpm_authoritative_graph->quiescing ||
        !g_molokai_dpm_authoritative_graph->ufs_catalog.compiled)
        return FALSE;
    st = pep_ufs_src_copy("BPMD", NULL, 0, &bytes);
    if (st != STATUS_BUFFER_TOO_SMALL || bytes < 20u || bytes > AEOB_MAX_SIZE)
        return FALSE;
    blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    candidate = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*candidate), POOL_TAG);
    idx = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*idx), POOL_TAG);
    if (!blob || !info || !candidate || !idx)
        goto done;
    st = pep_ufs_src_copy("BPMD", blob, bytes, &bytes);
    if (NT_SUCCESS(st)) {
        res_graph_t* graph = g_molokai_dpm_authoritative_graph;
        if (graph && graph->acquire && graph->release && graph->acquire(graph->lock_ctx)) {
            if (graph->sealed && !graph->retired && !graph->quiescing &&
                graph->ufs_catalog.compiled)
                matched = (BOOLEAN)ufs_catalog_verify_pinned_desc(&graph->ufs_catalog, blob, bytes,
                                                                  info, candidate, idx);
            graph->release(graph->lock_ctx);
        }
    }
done:
    if (idx)
        ExFreePool(idx);
    if (candidate)
        ExFreePool(candidate);
    if (info)
        ExFreePool(info);
    if (blob)
        ExFreePool(blob);
    return matched;
}

int pep_native_owner_lock(void* ctx)
{
    pep_native_producer_scope_t* scope = ctx;
    LARGE_INTEGER timeout;
    if (!scope || KeGetCurrentIrql() != PASSIVE_LEVEL || !scope->serial ||
        scope->serial != hw_backend_verified_serial())
        return 0;
    timeout.QuadPart = -20000000LL;
    if (KeWaitForSingleObject(&scope->transaction_mutex, Executive, KernelMode, FALSE, &timeout) !=
        STATUS_SUCCESS)
        return 0;
    if (scope->serial == hw_backend_verified_serial())
        return 1;
    (VOID) KeReleaseMutex(&scope->transaction_mutex, FALSE);
    return 0;
}

void pep_native_owner_unlock(void* ctx)
{
    pep_native_producer_scope_t* scope = ctx;
    if (scope)
        (VOID) KeReleaseMutex(&scope->transaction_mutex, FALSE);
}

static int pep_native_owner_supports(void* ctx, const power_action_t* action)
{
    pep_native_producer_scope_t* scope = ctx;
    uint32_t set, state, i;
    if (!scope || !scope->graph || !action || scope->serial != hw_backend_verified_serial() ||
        !scope->graph->ufs_catalog.compiled ||
        scope->graph->native_ufs_owner_epoch != scope->serial)
        return 0;
    /* every native action must be exactly present in the immutable BPMD catalog. A matching
     * res kind alone is not authorization. */
    for (set = 0u; set < UFS_PSTATE_SETS; ++set)
        for (state = 0u; state < UFS_STATES_PER_SET; ++state) {
            const power_pstate_program_t* p =
                ufs_catalog_get(&scope->graph->ufs_catalog, 0u, set, state);
            if (!p)
                return 0;
            for (i = 0u; i < p->resources.action_cnt; ++i)
                if (RtlCompareMemory(action, &p->resources.actions[i], sizeof(*action)) ==
                    sizeof(*action))
                    return 1;
        }
    return 0;
}

static int pep_native_owner_verify(void* ctx, const power_sequence_t* sequence)
{
    pep_native_producer_scope_t* scope = ctx;
    res_graph_t* graph;
    ULONG i, j;
    BOOLEAN desc_matches = FALSE;
    if (!scope || !scope->serial || !(graph = scope->graph) || !sequence ||
        graph->native_ufs_owner_epoch != scope->serial ||
        scope->serial != hw_backend_verified_serial())
        return 0;
    if (sequence->target_d_state != 3u)
        return hw_backend_win_ufs_program_verify(sequence, scope->serial);
    /* producerrelease must prove the d3 program is the retained graph d3. A matching path and
     * physical readback are insufficient provenance. */
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return 0;
    if (graph->sealed && !graph->retired && !graph->quiescing &&
        graph->dev_cnt <= GRAPH_MAX_DEVICES && sequence->action_cnt &&
        sequence->action_cnt <= POWER_MAX_ACTIONS) {
        for (i = 0u; i < graph->dev_cnt; ++i) {
            const graph_dev_t* dev = &graph->devices[i];
            if (strcmp(dev->path, "\\_SB.UFS0") || !dev->has_d3 ||
                sequence->action_cnt != dev->d3.action_cnt ||
                strcmp(sequence->dev_path, dev->d3.dev_path))
                continue;
            desc_matches = TRUE;
            for (j = 0u; j < sequence->action_cnt; ++j)
                if (RtlCompareMemory(&sequence->actions[j], &dev->d3.actions[j],
                                     sizeof(power_action_t)) != sizeof(power_action_t)) {
                    desc_matches = FALSE;
                    break;
                }
            break;
        }
    }
    graph->release(graph->lock_ctx);
    return desc_matches && graph_verify_native_ufs_d3(graph, "\\_SB.UFS0", scope->serial);
}

static power_result_t pep_native_owner_execute(void* ctx, const power_sequence_t* sequence,
                                               power_execution_t* trace)
{
    pep_native_producer_scope_t* scope = ctx;
    const power_dev_t* fstate;
    if (!scope || !scope->graph || !scope->verified_d0 || !scope->serial ||
        scope->graph->native_ufs_owner_epoch != scope->serial)
        return POWER_UNSUPPORTED;
    fstate = &scope->producer.owner.comp[0];
    if (!fstate->state_known)
        fstate = NULL; /* initial F0 requires verified D0. */
    return hw_backend_win_ufs_program_execute(sequence, scope->serial, scope->verified_d0, fstate,
                                              trace);
}

static graph_dev_t* pep_native_ufs_graph_dev(res_graph_t* graph)
{
    uint32_t i;
    if (!graph || !graph->sealed || graph->retired || graph->quiescing)
        return NULL;
    for (i = 0u; i < graph->dev_cnt && i < GRAPH_MAX_DEVICES; ++i)
        if (!strcmp(graph->devices[i].path, "\\_SB.UFS0"))
            return &graph->devices[i];
    return NULL;
}

BOOLEAN pep_prepare_native_ufs_scope(pep_devnode_t* n, const char* path)
{
    pep_native_producer_scope_t* scope = NULL;
    ufs_catalog_t* candidate = NULL;
    runtime_desc_info_t* info = NULL;
    program_idx_t* idx = NULL;
    comp_state_work_t* work = NULL;
    graph_dev_t* dev;
    ULONG bytes = 0u;
    NTSTATUS status;
    BOOLEAN accepted = FALSE, retained = FALSE;
    if (!n || !path || strcmp(path, "\\_SB.UFS0") || n->native_owner || n->native_perf ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !g_molokai_dpm_authoritative_graph)
        return FALSE;
    dev = pep_native_ufs_graph_dev(g_molokai_dpm_authoritative_graph);
    if (!dev || !dev->prepared || !dev->state.state_known || dev->state.quarantined ||
        dev->state.current_d_state != 0u)
        return FALSE;
    scope = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scope), POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    candidate = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*candidate), POOL_TAG);
    idx = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*idx), POOL_TAG);
    work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
    if (!scope || !info || !candidate || !idx || !work)
        goto done;
    RtlZeroMemory(scope, sizeof(*scope));
    KeInitializeMutex(&scope->transaction_mutex, 0u);
    scope->serial = hw_backend_verified_serial();
    scope->graph = g_molokai_dpm_authoritative_graph;
    scope->verified_d0 = &dev->state;
    if (!scope->serial || scope->graph->backend_binding_serial != scope->serial ||
        !hw_backend_win_ufs_owner_ready(scope->graph, scope->serial))
        goto done;
    status = pep_ufs_src_copy("BPMD", NULL, 0, &bytes);
    if (status != STATUS_BUFFER_TOO_SMALL || !bytes || bytes > AEOB_MAX_SIZE)
        goto done;
    scope->desc = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!scope->desc)
        goto done;
    scope->desc_bytes = bytes;
    status = pep_ufs_src_copy("BPMD", scope->desc, bytes, &bytes);
    if (!NT_SUCCESS(status) || bytes != scope->desc_bytes ||
        !ufs_catalog_verify_pinned_desc(&scope->graph->ufs_catalog, scope->desc, scope->desc_bytes,
                                        info, candidate, idx) ||
        idx->comp_mask != 1u)
        goto done;
    scope->backend.ctx = scope;
    scope->backend.lock = pep_native_owner_lock;
    scope->backend.unlock = pep_native_owner_unlock;
    scope->backend.supports = pep_native_owner_supports;
    scope->backend.verify_current_state = pep_native_owner_verify;
    scope->backend.execute_owned_program = pep_native_owner_execute;
    accepted = (BOOLEAN)po_fx_producer_prepare(&scope->producer, scope->desc, scope->desc_bytes,
                                               path, 1u, scope->verified_d0, &scope->backend,
                                               scope->serial, info, work);
    retained = scope->producer.retain != 0u;
    if (accepted || retained) {
        /* the endpoint pointer remains a teardown barrier even if binding failed AFTER initial
         * hardware dispatch. No graph-only fallback. */
        n->native_owner = scope;
        n->native_perf = &scope->producer.endpoint;
        n->native_perf_backend_serial = scope->serial;
        scope = NULL;
    }
done:
    if (info)
        ExFreePool(info);
    if (candidate)
        ExFreePool(candidate);
    if (idx)
        ExFreePool(idx);
    if (work)
        ExFreePool(work);
    if (scope) {
        if (scope->desc)
            ExFreePool(scope->desc);
        ExFreePool(scope);
    }
    return accepted;
}

/* called only after PoFx admissions are closed. The final d3 readback uses the graph's
 * retained, desc-derived d3, not a fabricated BPMD d3. */
/* recompile f1 from the pinned BPMD; a cached f1 selector does not independently prove
 * that all required P-state client votes were withdrawn. */
static BOOLEAN pep_native_ufs_verify_f1(pep_native_producer_scope_t* scope, comp_state_work_t* work)
{
    power_pstate_selection_t selection;
    const power_pstate_program_t* programs[3];
    uint32_t i;
    BOOLEAN valid = FALSE;
    if (!scope || !work || !scope->producer.owner.bound || scope->producer.owner.quarantined ||
        !scope->producer.owner.comp[0].state_known || scope->producer.owner.comp[0].quarantined ||
        scope->producer.owner.comp[0].current_d_state != 1u)
        return FALSE;
    for (i = 0u; i < 3u; ++i) {
        if (!scope->producer.owner.sets[0][i].state_known ||
            scope->producer.owner.sets[0][i].quarantined ||
            scope->producer.owner.sets[0][i].current_d_state != 1u ||
            program_idx_compile(&scope->producer.owner.idx, scope->desc, scope->desc_bytes,
                                PROGRAM_P_STATE, 0u, i, 1u, NULL, &work->p_states[i]) != POWER_OK)
            return FALSE;
        programs[i] = &work->p_states[i];
    }
    if (program_idx_compile(&scope->producer.owner.idx, scope->desc, scope->desc_bytes,
                            PROGRAM_F_STATE, 0u, 0u, 1u, &work->f_state, NULL) != POWER_OK ||
        power_expand_comp_f_state(&work->f_state, 0u, programs, 3u, 32u, &work->expanded,
                                  &selection) != POWER_OK ||
        selection.used_sets != 7u || !scope->backend.lock(scope))
        return FALSE;
    valid = (BOOLEAN)scope->backend.verify_current_state(scope, &work->expanded);
    scope->backend.unlock(scope);
    return valid;
}

power_result_t pep_run_node_d_state(pep_devnode_t* n, ULONG target)
{
    power_result_t result;
    comp_state_work_t* work = NULL;
    if (!n || target > 3u)
        return POWER_UNSUPPORTED;
    if (n->runtime_comp)
        return runtime_comp_win_dx(n->runtime_comp, target) ? POWER_OK : POWER_UNSUPPORTED;
    if (n->runtime_dstate_scope)
        return pep_runtime_d_execute(n->runtime_dstate_scope, target);
    if (n->peer_scope)
        return pep_run_peer_d_state(n->peer_scope, target);
    if (!n->native_owner)
        return pep_run_molokai_d_state(&n->instance_path, target);
    /* keep UFS f1 verification and d3 res withdrawal in one interval shared with the
     * producer's F/P transactions. The KMUTEX is recursive; VerifyF1 uses the same backend
     * lock for its independent readback. */
    if (!pep_native_owner_lock(n->native_owner))
        return POWER_UNSUPPORTED;
    if (target == 3u) {
        work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
        if (!work || !pep_native_ufs_verify_f1(n->native_owner, work)) {
            if (work)
                ExFreePool(work);
            pep_native_owner_unlock(n->native_owner);
            return POWER_UNSUPPORTED;
        }
    }
    result = pep_run_molokai_d_state(&n->instance_path, target);
    if (work)
        ExFreePool(work);
    pep_native_owner_unlock(n->native_owner);
    return result;
}

BOOLEAN pep_native_ufs_retire(pep_devnode_t* n, const UNICODE_STRING* dev_id)
{
    pep_native_producer_scope_t* scope;
    comp_state_work_t* work;
    runtime_desc_info_t* d3_info = NULL;
    power_sequence_t* d3_sequence = NULL;
    program_idx_t* d3_idx = NULL;
    UCHAR* d3_blob = NULL;
    ULONG d3_bytes = 0u;
    NTSTATUS status;
    power_execution_t trace;
    power_result_t result;
    BOOLEAN released = FALSE;
    if (!n || !dev_id || !n->native_owner || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        n->reg_state == DEV_REGISTERED_MINI_PEP)
        return FALSE;
    scope = n->native_owner;
    if (!scope->serial || !scope->graph || scope->graph != g_molokai_dpm_authoritative_graph ||
        scope->graph->native_ufs_owner_epoch != scope->serial ||
        scope->serial != hw_backend_verified_serial() || !scope->producer.retain ||
        scope->producer.owner.quarantined)
        return FALSE;
    /* unregister has drained its callbacks; rejected PREPARE never opened callbacks. Neither
     * path may release a still-registered native owner. */
    po_fx_producer_close(&scope->producer);
    if (scope->producer.published || scope->producer.endpoint.bound)
        return FALSE;
    /* retry after a physically completed d3 must not execute f1 against a no-longer-active d0.
     * Keep the f1 witness, but re-verify final state through ProducerRelease before retiring
     * any retained client. */
    status = pep_ufs_src_copy("BSMD", NULL, 0, &d3_bytes);
    if (status != STATUS_BUFFER_TOO_SMALL || !d3_bytes || d3_bytes > AEOB_MAX_SIZE)
        return FALSE;
    d3_blob = ExAllocatePool2(POOL_FLAG_NON_PAGED, d3_bytes, POOL_TAG);
    d3_info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d3_info), POOL_TAG);
    d3_sequence = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d3_sequence), POOL_TAG);
    d3_idx = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d3_idx), POOL_TAG);
    work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
    if (!d3_blob || !d3_info || !d3_sequence || !d3_idx || !work)
        goto release_done;
    status = pep_ufs_src_copy("BSMD", d3_blob, d3_bytes, &d3_bytes);
    if (!NT_SUCCESS(status))
        goto release_done;
    /* fail before f1/d3 if the recovered D-state desc cannot compile. */
    if (!program_idx_describe(d3_blob, d3_bytes, "\\_SB.UFS0", d3_info, d3_idx) ||
        !d3_idx->has_d3 ||
        program_idx_compile(d3_idx, d3_blob, d3_bytes, PROGRAM_D_STATE, 0u, 0u, 3u, d3_sequence,
                            NULL) != POWER_OK ||
        !d3_sequence->action_cnt)
        goto release_done;
    if (!scope->f1_release_verified) {
        if (!scope->verified_d0->state_known || scope->verified_d0->current_d_state != 0u)
            goto release_done;
        RtlZeroMemory(&trace, sizeof(trace));
        result = comp_owner_execute_f_state(&scope->producer.owner, scope->desc, scope->desc_bytes,
                                            0u, 1u, scope->verified_d0, &scope->backend,
                                            scope->serial, work, &trace);
        if (result == POWER_OK && pep_native_ufs_verify_f1(scope, work))
            scope->f1_release_verified = TRUE;
    }
    if (!scope->f1_release_verified)
        goto release_done;
    if (!graph_verify_native_ufs_d3(scope->graph, "\\_SB.UFS0", scope->serial)) {
        if (!scope->verified_d0->state_known || scope->verified_d0->current_d_state != 0u ||
            pep_run_molokai_lifecycle(dev_id, FALSE) != POWER_OK ||
            !graph_verify_native_ufs_d3(scope->graph, "\\_SB.UFS0", scope->serial))
            goto release_done;
    }
    released = (BOOLEAN)po_fx_producer_release(&scope->producer, scope->verified_d0, d3_blob,
                                               d3_bytes, d3_info, d3_sequence, work, scope->serial);
release_done:
    if (work)
        ExFreePool(work);
    if (d3_idx)
        ExFreePool(d3_idx);
    if (d3_sequence)
        ExFreePool(d3_sequence);
    if (d3_info)
        ExFreePool(d3_info);
    if (d3_blob)
        ExFreePool(d3_blob);
    if (!released)
        return FALSE;
    n->native_perf = NULL;
    n->native_owner = NULL;
    n->native_perf_backend_serial = 0u;
    ExFreePool(scope->desc);
    ExFreePool(scope);
    return TRUE;
}

NTSTATUS pep_native_perf_quiesce_gate(VOID)
{
    NTSTATUS status = STATUS_SUCCESS;
    pep_devnode_t* n;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!g_pep_dev_list_lock)
        return STATUS_SUCCESS;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (n = g_pep_dev_list_head; n; n = n->next) {
        /* endpoint teardown must own d3/f1/FOOTSWITCH retirement before the backend can revoke
         * clocks, RPMh or physical mappings. */
        if (n->native_perf) {
            status = STATUS_DEVICE_BUSY;
            break;
        }
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return status;
}

UCHAR __fastcall
pep_power_on_dump_dev_callback(_In_opt_ pep_crashdump_information_t* crashdump_information)
{
    PVOID dev = NULL;
    PVOID ctx = NULL;
    BOOLEAN crashdump_state_ok = TRUE;

    __try {
        if (crashdump_information) {
            dev = crashdump_information->dev_handle;
            ctx = crashdump_information->dev_ctx;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        dev = NULL;
        ctx = NULL;
    }

    /* a cache-only d0 transition cannot power up UFS during crashdump. */

    if (!dev || !pep_has_verified_crashdump_backend())
        return 0;

    g_pep_crashdump_dev = dev;

    if (!pep_enter_lockless_polling_mode())
        return 0;
    pep_crashdump_rpmh_prep();

    if (dev) {
        __try {
            crashdump_state_ok = pep_set_crash_dump_dev_state((pep_devnode_t*)dev);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            crashdump_state_ok = FALSE;
        }
    }

    pep_log_write(PEPLOG_EVT_DEV_NOTIFY, 0x26,
                  crashdump_state_ok ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL, (ULONG)(ULONG_PTR)dev,
                  (ULONG)((ULONG_PTR)dev >> 32), (ULONG)(ULONG_PTR)ctx,
                  (ULONG)((ULONG_PTR)ctx >> 32));

    return crashdump_state_ok ? 1 : 0;
}

static ULONG_PTR pep_dpm_query_soc_subsystem_cnt(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(7u, data);
    return soc_subsys_query_cnt((PEP_QUERY_SOC_SUBSYSTEM_COUNT*)data) ? 1 : 0;
}

static ULONG_PTR pep_dpm_query_soc_subsystem(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(7u, data);
    return soc_subsys_query_subsystem((PEP_QUERY_SOC_SUBSYSTEM*)data) ? 1 : 0;
}

static ULONG_PTR pep_dpm_reset_soc_subsystem_accounting(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(7u, data);

    if (soc_subsys_reset_accounting((PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING*)data))
        return 1;

    if (pep_is_bugcheck_enabled(0x10u))
        pep_bug_check(7u, data);

    return 0;
}

static ULONG_PTR pep_dpm_query_soc_subsystem_blocking_time(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(7u, data);
    return soc_subsys_query_blocking_time((PEP_QUERY_SOC_SUBSYSTEM_BLOCKING_TIME*)data) ? 1 : 0;
}

static ULONG_PTR pep_dpm_query_soc_subsystem_metadata(_Inout_opt_ PVOID data)
{
    if (!data)
        pep_bug_check(7u, data);
    return soc_subsys_query_metadata((PEP_QUERY_SOC_SUBSYSTEM_METADATA*)data) ? 1 : 0;
}

/* registry settings cached during PASSIVE_LEVEL driver init: PPM callbacks must not invoke
 * registry I/O at arbitrary IRQL. */

ULONG g_molokai_disable_lpi_support = 1;

extern __forceinline ULONG get_cpu_num_from_pep_handle(_In_opt_ PVOID pep_handle)
{
    if (!pep_handle)
        return 0;

    ULONG_PTR v = (ULONG_PTR)pep_handle;
    ULONG hi = (ULONG)(v & 0xFFFF0000ull);
    if (hi == 0x10000000u || hi == 0x20000000u)
        return (ULONG)(v & 0xFFFFu);

    __try {
        ULONG dev_type = ((ULONG*)pep_handle)[44];
        if (dev_type >= 16 && dev_type <= 23)
            return dev_type - 16;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    if (v < 0x1000)
        return (ULONG)v;
    return 0;
}

NTSTATUS pep_reg_veto_reason(_In_z_ PCWSTR reason, _Out_ PULONG veto_id)
{
    if (!reason || !veto_id)
        return STATUS_INVALID_PARAMETER;

    pep_veto_reason_init_if_needed();

    size_t cch = 0;
    NTSTATUS st = RtlStringCchLengthW(reason, 260, &cch);
    if (!NT_SUCCESS(st))
        return st;

    ExAcquireFastMutex(&g_veto_reason_lock);

    if (g_vetoes_enumerated) {
        *veto_id = MAX_VETO_REASONS;
        ExReleaseFastMutex(&g_veto_reason_lock);
        return STATUS_UNSUCCESSFUL;
    }

    ULONG slot = 0;
    for (ULONG i = 1; i <= MAX_VETO_REASONS; i++) {
        if (!g_veto_reason_names[i].Buffer) {
            slot = i;
            break;
        }
    }

    if (!slot) {
        *veto_id = MAX_VETO_REASONS;
        ExReleaseFastMutex(&g_veto_reason_lock);
        return STATUS_UNSUCCESSFUL;
    }

    USHORT bytes = (USHORT)((cch + 1) * sizeof(WCHAR));
    PWSTR buf = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!buf) {
        ExReleaseFastMutex(&g_veto_reason_lock);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(buf, reason, bytes);
    g_veto_reason_names[slot].Buffer = buf;
    g_veto_reason_names[slot].Length = (USHORT)(cch * sizeof(WCHAR));
    g_veto_reason_names[slot].MaximumLength = bytes;
    InterlockedIncrement(&g_veto_reason_cnt);

    *veto_id = slot;
    ExReleaseFastMutex(&g_veto_reason_lock);
    return STATUS_SUCCESS;
}

static __forceinline VOID pep_dec_ref64(_Inout_ volatile LONG64* v)
{
    LONG64 cur = *v;
    while (cur > 0) {
        LONG64 prev = InterlockedCompareExchange64((volatile LONG64*)v, cur - 1, cur);
        if (prev == cur)
            break;
        cur = prev;
    }
}

extern __forceinline VOID pep_dec_ref32(_Inout_ volatile LONG* v)
{
    LONG cur = *v;
    while (cur > 0) {
        LONG prev = InterlockedCompareExchange((volatile LONG*)v, cur - 1, cur);
        if (prev == cur)
            break;
        cur = prev;
    }
}

NTSTATUS pep_apply_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id)
{
    if (veto_id < 1 || veto_id > MAX_VETO_REASONS)
        return STATUS_INVALID_PARAMETER;

    InterlockedIncrement64(&g_veto.ref_counts[veto_id]);
    pep_log_write(PEPLOG_EVT_IDLE, 0x62, STATUS_SUCCESS, 1, lpr_id, mode_id, veto_id);

    if (!pep_send_idle_veto(lpr_id, mode_id, veto_id, 1))
        pep_queue_pending_veto(lpr_id, mode_id, veto_id, 1);

    return STATUS_SUCCESS;
}

NTSTATUS pep_remove_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id)
{
    if (veto_id < 1 || veto_id > MAX_VETO_REASONS)
        return STATUS_INVALID_PARAMETER;

    pep_dec_ref64(&g_veto.ref_counts[veto_id]);
    pep_log_write(PEPLOG_EVT_IDLE, 0x63, STATUS_SUCCESS, 0, lpr_id, mode_id, veto_id);

    if (!pep_send_idle_veto(lpr_id, mode_id, veto_id, 0))
        pep_queue_pending_veto(lpr_id, mode_id, veto_id, 0);

    return STATUS_SUCCESS;
}

LONGLONG pep_get_veto_ref_cnt(_In_ ULONG veto_id)
{
    if (veto_id < 1 || veto_id > MAX_VETO_REASONS)
        return 0;
    return (LONGLONG)g_veto.ref_counts[veto_id];
}

VOID pep_query_veto_reasons_compat(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    pep_query_veto_reasons(data, pep_handle);
}

VOID pep_query_veto_reason_compat(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    pep_query_veto_reason(data, pep_handle);
}

NTSTATUS pep_resolve_idle_veto_target(_In_ ULONG selector, _In_ ULONG mode_or_coordinated_idx,
                                      _Out_ PULONG lpr_id, _Out_ PULONG mode_id)
{
    if (!lpr_id || !mode_id)
        return STATUS_INVALID_PARAMETER;

    *lpr_id = 0xFFFFFFFFu;
    *mode_id = 0xFFFFFFFFu;

    idle_acpi_ensure_init();

    if (!g_idle_acpi.lpr_array || !g_idle_acpi.lpr_array_size)
        return STATUS_ARRAY_BOUNDS_EXCEEDED;

    if (selector == 0xFFFFFFFFu) {
        if (!g_idle_acpi.coordinated_modes_array ||
            mode_or_coordinated_idx >= g_idle_acpi.coordinated_modes_array_size)
            return STATUS_ARRAY_BOUNDS_EXCEEDED;

        idle_mode_t* m = g_idle_acpi.coordinated_modes_array[mode_or_coordinated_idx];
        if (!m)
            return STATUS_ARRAY_BOUNDS_EXCEEDED;

        *lpr_id = m->lpr_id;
        *mode_id = m->mode_id;
        return STATUS_SUCCESS;
    }

    for (ULONG i = 0; i < g_idle_acpi.lpr_array_size; i++) {
        idle_lpr_t* lpr = &g_idle_acpi.lpr_array[i];
        if (!lpr->mode_array || !lpr->mode_cnt)
            continue;
        if (lpr->core_mask != selector)
            continue;

        if (mode_or_coordinated_idx >= lpr->mode_cnt)
            return STATUS_ARRAY_BOUNDS_EXCEEDED;

        *lpr_id = lpr->lpr_id;
        *mode_id = mode_or_coordinated_idx;
        return STATUS_SUCCESS;
    }

    return STATUS_ARRAY_BOUNDS_EXCEEDED;
}

BOOLEAN pep_get_cpu_handles(_In_ ULONG cpu, _Out_ PVOID* pep_handle, _Out_ PVOID* kernel_handle)
{
    if (!pep_handle || !kernel_handle)
        return FALSE;

    *pep_handle = NULL;
    *kernel_handle = NULL;

    PVOID ph = pep_get_pep_handle_from_cpu_num(cpu);
    PVOID kh = pep_get_kernel_handle_from_cpu_num(cpu);
    if (!ph)
        return FALSE;

    *pep_handle = ph;
    *kernel_handle = kh;
    return TRUE;
}

BOOLEAN pep_get_proc_idle_state_cnt(_In_opt_ PVOID pep_handle, _Out_ PULONG cnt)
{
    if (!cnt)
        return FALSE;

    *cnt = 0;
    idle_acpi_ensure_init();

    if (!g_idle_acpi.lpr_array || !g_idle_acpi.lpr_array_size)
        return FALSE;

    ULONG proc = get_cpu_num_from_pep_handle(pep_handle);
    if (proc >= g_idle_acpi.lpr_array_size)
        return FALSE;

    idle_lpr_t* lpr = &g_idle_acpi.lpr_array[proc];
    if (!lpr->mode_array)
        return FALSE;

    *cnt = lpr->mode_cnt;
    return TRUE;
}

BOOLEAN pep_get_mode_veto_ref_cnt(_In_ ULONG lpr_id, _In_ ULONG mode_id, _Out_ PULONG ref_cnt)
{
    if (!ref_cnt)
        return FALSE;

    *ref_cnt = 0;
    idle_acpi_ensure_init();

    if (!g_idle_acpi.lpr_array || lpr_id >= g_idle_acpi.lpr_array_size)
        return FALSE;

    idle_lpr_t* lpr = &g_idle_acpi.lpr_array[lpr_id];
    if (!lpr->mode_array || mode_id >= lpr->mode_cnt)
        return FALSE;

    LONG rc = InterlockedCompareExchange(&lpr->mode_array[mode_id].veto_reference_cnt, 0, 0);
    if (rc < 0)
        rc = 0;
    *ref_cnt = (ULONG)rc;
    return TRUE;
}

NTSTATUS pep_apply_ex_soc_d_state(_In_z_ const CHAR* dev_id_a, _In_ UCHAR d_state)
{
    if (!dev_id_a || d_state > 3)
        return STATUS_UNSUCCESSFUL;
    if (!g_pep_dev_list_lock)
        return STATUS_UNSUCCESSFUL;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_UNSUCCESSFUL;

    ANSI_STRING a;
    RtlInitAnsiString(&a, dev_id_a);
    if (a.Length == 0)
        return STATUS_UNSUCCESSFUL;

    UNICODE_STRING u;
    RtlZeroMemory(&u, sizeof(u));
    NTSTATUS st = RtlAnsiStringToUnicodeString(&u, &a, TRUE);
    if (!NT_SUCCESS(st))
        return STATUS_UNSUCCESSFUL;

    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    pep_devnode_t* n = pep_find_dev_node_locked(&u);
    WdfWaitLockRelease(g_pep_dev_list_lock);
    st = STATUS_INVALID_DEVICE_STATE;
    if (n && pep_sync_dpm_begin(n)) {
        if (pep_dpm_begin_transition(n, (DEVICE_POWER_STATE)(PowerDeviceD0 + d_state))) {
            power_result_t result = pep_run_node_d_state(n, (ULONG)d_state);
            ExAcquireFastMutex(&n->d_state_change_f_mutex);
            dpm_verified_record(&n->dpm_backend_state, (ULONG)d_state,
                                result == POWER_OK && n->reg_state == DEV_REGISTERED_MINI_PEP);
            ExReleaseFastMutex(&n->d_state_change_f_mutex);
            pep_dpm_finish_transition(n);
            st = result == POWER_OK ? STATUS_SUCCESS : STATUS_DEVICE_POWER_FAILURE;
        } else
            st = STATUS_DEVICE_BUSY;
        pep_sync_dpm_end(n);
    }

    RtlFreeUnicodeString(&u);

    return st;
}

ULONG pep_get_registry_test_veto_id(VOID)
{
    ULONG id = g_registry_veto_id;
    if (id < 1 || id > MAX_VETO_REASONS)
        id = 1;
    return id;
}

NTSTATUS pep_get_park_selection_snapshot(_Out_ parksel_snapshot_t* snapshot)
{
    if (!snapshot)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(snapshot, sizeof(*snapshot));

    snapshot->version = 3;
    snapshot->cpu_cnt = pep_park_rank_cpu_cnt();
    snapshot->last_additional_unparked = g_park_sel.last_additional_unparked;
    snapshot->last_evaluation_type = g_park_sel.last_evaluation_type;
    snapshot->last_evaluation_time = g_park_sel.last_selection_evaluation_time;
    snapshot->timer_threshold0_sec = g_park_sel.timer_threshold_sec[0];
    snapshot->timer_threshold1_sec = g_park_sel.timer_threshold_sec[1];
    snapshot->core_rotation_stat0 = g_park_sel.core_rotation_stat[0];
    snapshot->core_rotation_stat1 = g_park_sel.core_rotation_stat[1];
    snapshot->cluster0_unparked = pep_dcvs_query_park_info(DCVS_CLUSTER_UNPARK_CNT, 0u);
    snapshot->cluster1_unparked = pep_dcvs_query_park_info(DCVS_CLUSTER_UNPARK_CNT, 4u);
    snapshot->pref_rail_id = g_park_sel.pref_rail_id;
    snapshot->timer_rail_id = g_park_sel.timer_rail_id;
    snapshot->num_unpark_rail_id = g_park_sel.num_unpark_rail_id;
    snapshot->core_rotation_rail_id = g_park_sel.core_rotation_rail_id;
    snapshot->last_cluster0_evaluation_time = g_park_sel.last_evaluation_time[0];
    snapshot->last_cluster1_evaluation_time = g_park_sel.last_evaluation_time[1];
    snapshot->active_cpu_cnt = g_park_sel.active_cpu_cnt;
    snapshot->toggle_state = g_park_sel.toggle_state;
    pep_park_ensure_trace_init();

    {
        KIRQL irql;
        KeAcquireSpinLock(&g_park_sel_trace_lock, &irql);
        snapshot->event_sequence = g_park_sel_event_sequence;
        snapshot->history_cnt = g_park_sel_history_cnt;
        snapshot->history_dropped = g_park_sel_history_dropped;
        snapshot->last_event_code = g_park_sel_last_event_code;
        snapshot->last_event_status = g_park_sel_last_event_status;
        snapshot->last_event_additional_unparked = g_park_sel_last_event_additional_unparked;
        snapshot->last_event_remaining_unparked = g_park_sel_last_event_remaining_unparked;
        snapshot->last_event_tick100ns = g_park_sel_last_event_tick100ns;
        snapshot->thermal_rank_enabled = g_park_sel_thermal_rank_enabled;
        snapshot->thermal_rank_hysteresis_deci_c = g_park_sel_thermal_rank_hysteresis_deci_c;
        snapshot->thermal_rank_update_interval_ms = g_park_sel_thermal_rank_update_interval_ms;
        snapshot->thermal_sensor_base_idx = g_park_sel_thermal_sensor_base_idx;
        snapshot->thermal_refresh_cnt =
            (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_refresh_cnt, 0, 0);
        snapshot->thermal_apply_cnt =
            (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_apply_cnt, 0, 0);
        snapshot->last_thermal_status = g_park_sel_last_thermal_status;
        KeReleaseSpinLock(&g_park_sel_trace_lock, irql);
    }

    for (ULONG i = 0; i < PARKSEL_MAX_CPU_SNAPSHOT; i++) {
        snapshot->rank_preference[i] = g_park_sel.pref[i];
        snapshot->previous_rank_preference[i] = g_park_sel.last_pref[i];
        snapshot->rank_cpu_temp_deci_c[i] = g_park_sel.cpu_temp[i];
    }

    for (ULONG i = 0; i < snapshot->cpu_cnt && i < PARKSEL_MAX_CPU_SNAPSHOT; i++) {
        snapshot->current_park_state[i] = g_cpu_park_state.cpu_cur_park_state[i];
        snapshot->previous_park_state[i] = g_cpu_park_state.cpu_prev_park_state[i];
        snapshot->current_preference[i] = g_cpu_park_state.cpu_current_preference[i];
        snapshot->current_rank[i] = g_park_sel.cpu_rank[i];
        snapshot->transition_cnt[i] = g_cpu_park_state.cpu_transition_cnt[i];
    }

    return STATUS_SUCCESS;
}

NTSTATUS pep_get_park_selection_history(_Out_writes_to_(capacity, *cnt) parksel_event_t* events,
                                        _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    pep_park_ensure_trace_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_park_sel_trace_lock, &irql);

    ULONG avail = g_park_sel_history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;
    ULONG start = (g_park_sel_history_head + PARKSEL_HISTORY_CAPACITY - avail + skip) %
                  PARKSEL_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % PARKSEL_HISTORY_CAPACITY;
        events[i] = g_park_sel_history[idx];
    }

    *cnt = take;
    *dropped = g_park_sel_history_dropped + skip;

    KeReleaseSpinLock(&g_park_sel_trace_lock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS pep_get_park_selection_policy(_Out_ parksel_policy_t* policy)
{
    if (!policy)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(policy, sizeof(*policy));
    policy->version = 1;
    policy->flags = 0xFFu;
    policy->thermal_rank_enabled = g_park_sel_thermal_rank_enabled;
    policy->thermal_rank_hysteresis_deci_c = g_park_sel_thermal_rank_hysteresis_deci_c;
    policy->thermal_rank_update_interval_ms = g_park_sel_thermal_rank_update_interval_ms;
    policy->thermal_sensor_base_idx = g_park_sel_thermal_sensor_base_idx;
    policy->timer_threshold0_sec = g_park_sel.timer_threshold_sec[0];
    policy->timer_threshold1_sec = g_park_sel.timer_threshold_sec[1];
    policy->thermal_refresh_cnt =
        (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_refresh_cnt, 0, 0);
    policy->thermal_apply_cnt =
        (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_apply_cnt, 0, 0);
    policy->last_thermal_status = g_park_sel_last_thermal_status;
    return STATUS_SUCCESS;
}

NTSTATUS pep_set_park_selection_policy(_In_ const parksel_policy_t* policy)
{
    if (!policy)
        return STATUS_INVALID_PARAMETER;
    if (policy->version != 0 && policy->version != 1)
        return STATUS_NOT_SUPPORTED;

    ULONG flags = policy->flags;

    if (flags & 0x1u)
        g_park_sel_thermal_rank_enabled = policy->thermal_rank_enabled ? 1u : 0u;
    if (flags & 0x2u)
        g_park_sel_thermal_rank_hysteresis_deci_c =
            pep_park_clamp_thermal_hysteresis_deci_c(policy->thermal_rank_hysteresis_deci_c);
    if (flags & 0x4u)
        g_park_sel_thermal_rank_update_interval_ms =
            pep_park_clamp_thermal_update_interval_ms(policy->thermal_rank_update_interval_ms);
    if (flags & 0x8u) {
        g_park_sel_thermal_sensor_base_idx = policy->thermal_sensor_base_idx;
        if (g_park_sel_thermal_sensor_base_idx >= TSENS_MAX_SENSORS)
            g_park_sel_thermal_sensor_base_idx = 0u;
    }
    if (flags & 0x10u) {
        g_park_sel.timer_threshold_sec[0] = policy->timer_threshold0_sec;
        g_park_sel.timer_threshold_sec[1] = policy->timer_threshold1_sec;
    }
    if (flags & 0x40u) {
        ULONG rank_cnt = pep_park_rank_cpu_cnt();
        for (ULONG i = 0; i < rank_cnt && i < PARK_MAX_RANK_CPUS; i++)
            g_park_sel.cpu_rank[i] = (UCHAR)i;
    }

    if (flags & 0x20u) {
        (VOID) registry_set_u_long(L"ParkSelThermalRankEnabled", g_park_sel_thermal_rank_enabled);
        (VOID) registry_set_u_long(L"ParkSelThermalRankHysteresisDeciC",
                                   (ULONG)g_park_sel_thermal_rank_hysteresis_deci_c);
        (VOID) registry_set_u_long(L"ParkSelThermalRankUpdateIntervalMs",
                                   g_park_sel_thermal_rank_update_interval_ms);
        (VOID) registry_set_u_long(L"ParkSelThermalSensorBase", g_park_sel_thermal_sensor_base_idx);
        (VOID) registry_set_u_long(L"ParkSelTimerApc0Sec", g_park_sel.timer_threshold_sec[0]);
        (VOID) registry_set_u_long(L"ParkSelTimerApc1Sec", g_park_sel.timer_threshold_sec[1]);
    }

    if (flags & 0x80u)
        (VOID) pep_park_refresh_thermal_ranking(TRUE, 0u, 0u, pep_now100ns());

    return STATUS_SUCCESS;
}

extern __forceinline ULONG pep_get_current_cpu_number(VOID)
{
    PROCESSOR_NUMBER pn;
    KeGetCurrentProcessorNumberEx(&pn);
    return pn.Number;
}

extern __forceinline WCHAR upcase_w(_In_ WCHAR c)
{
    if (c >= L'a' && c <= L'z')
        return (WCHAR)(c - (L'a' - L'A'));
    return c;
}

BOOLEAN wcs_contains_insensitive(_In_opt_ PCWSTR haystack, _In_z_ PCWSTR needle)
{
    if (!haystack || !needle || !needle[0])
        return FALSE;

    for (SIZE_T i = 0; haystack[i]; i++) {
        SIZE_T j = 0;
        while (needle[j] && haystack[i + j] && (upcase_w(haystack[i + j]) == upcase_w(needle[j])))
            j++;
        if (!needle[j])
            return TRUE;
    }
    return FALSE;
}

/* the native Windows RPMh+clock idle entry/exit and ownership handshake have not been
 * implemented. This gate MUST remain false until both entry and exit are verifiably
 * hardware-backed and DSDT _LPI contracts match. */

BOOLEAN pep_is_molokai_idle_execution_ready(VOID)
{
    return FALSE;
}

VOID pep_query_proc_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;
    idle_acpi_ensure_init();
    ULONG proc = get_cpu_num_from_pep_handle(pep_handle);
    idle_internal_query_state_name(&g_idle_acpi, proc, (pep_ppm_query_state_name_t*)data);
}

VOID pep_query_coordinated_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;
    idle_acpi_ensure_init();
    idle_internal_query_state_name(&g_idle_acpi, 0xFFFFFFFFu, (pep_ppm_query_state_name_t*)data);
}

VOID pep_query_platform_states_acpi_lpi_pep(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;
    idle_acpi_ensure_init();
    *(ULONG*)data = g_idle_acpi.coordinated_modes_array_size;
}

VOID pep_query_proc_idle_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;
    idle_acpi_ensure_init();
    ULONG proc = get_cpu_num_from_pep_handle(pep_handle);
    if (proc >= g_idle_acpi.lpr_array_size)
        return;
    pep_ppm_query_idle_states_v2_t* q = (pep_ppm_query_idle_states_v2_t*)data;
    ULONG cnt = q->cnt;
    if (!cnt)
        return;
    idle_internal_get_proc_state_data(&g_idle_acpi, proc, cnt, q->idle_states);
}

VOID pep_query_coordinated_idle_dependency(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;
    idle_acpi_ensure_init();
    idle_internal_get_coordinated_state_dependency(&g_idle_acpi,
                                                   (pep_ppm_query_coordinated_dependency_t*)data);
}

NTSTATUS pep_on_hibernate_enter(VOID)
{
    NTSTATUS st;

    power_ctrl_deinit();
    pep_init_and_apply_registry_vetoes_if_needed();
    forward_prepare_for_hibernate();
    pep_apply_boot_veto_if_possible();
    st = pep_prepare_for_power_down();
    InterlockedExchange(&g_hibernate_in_progress, 1);
    return st;
}

NTSTATUS pep_on_power_down_enter(VOID)
{
    return pep_prepare_for_power_down();
}

static VOID pep_drain_queued_work_items(VOID)
{
    if (!init_gate_ready(&g_pep_work_init))
        return;

    /* detach under the queue spin lock. Queued work that never started holds no active-worker
     * ticket. */

    LIST_ENTRY retired;
    KIRQL irql;
    InitializeListHead(&retired);
    KeAcquireSpinLock(&g_pep_work_lock, &irql);
    while (!IsListEmpty(&g_pep_work_queue))
        InsertTailList(&retired, RemoveHeadList(&g_pep_work_queue));
    KeReleaseSpinLock(&g_pep_work_lock, irql);
    while (!IsListEmpty(&retired)) {
        pep_work_item_t* it = CONTAINING_RECORD(RemoveHeadList(&retired), pep_work_item_t, link);
        if (it->work_type == PEP_WORKPAYLOAD_REQUESTPOWERCONTROL && it->data)
            pep_free_power_control_ctx((pep_work_power_control_ctx_t*)it->data);
        else if (it->data_owned && it->data)
            ExFreePool(it->data);
        pep_free_work_item(it);
    }
}

static NTSTATUS pep_prepare_for_power_down(VOID)
{
    idle_ensure_init();
    if (!pep_is_molokai_idle_execution_ready() || !init_gate_ready(&g_idle.initialized))
        return STATUS_NOT_SUPPORTED;
    NTSTATUS st = idle_rpmh_sync_enter(0);
    if (NT_SUCCESS(st))
        InterlockedExchange(&g_hibernate_rpmh_synced, 1);
    else
        InterlockedExchange(&g_hibernate_rpmh_synced, 0);
    return st;
}

static VOID pep_restore_after_power_up(VOID)
{
    if (InterlockedCompareExchange(&g_hibernate_rpmh_synced, 0, 1) == 1)
        idle_rpmh_sync_exit(0);
}

VOID pep_on_hibernate_resume(VOID)
{

    pep_drain_queued_work_items();
    (VOID) power_ctrl_init();
    pep_restore_after_power_up();
    InterlockedExchange(&g_hibernate_in_progress, 0);
    pep_remove_boot_veto_if_applied();
}

NTSTATUS pep_warmup_caches(VOID)
{

    if (!pep_work_init_if_needed() || !pep_result_init_if_needed() ||
        !pep_power_ctrl_ctx_init_if_needed())
        return STATUS_DEVICE_NOT_READY;

    ULONG bwmon_cnt = registry_query_u_long(L"\\_SB.BWMON_COUNT", 0);

    ULONG prealloc = 0;
    if (bwmon_cnt != 0) {
        ULONGLONG scaled = (ULONGLONG)bwmon_cnt * 16ull;
        prealloc = (scaled > MAXULONG) ? MAXULONG : (ULONG)scaled;
    }

    ULONG dpm_prealloc = prealloc;
    ULONG tiny_prealloc = prealloc;
    ULONG small_prealloc = prealloc;
    ULONG large_prealloc = prealloc;

    for (ULONG i = 0; i < dpm_prealloc; i++) {
        pep_work_power_control_ctx_t* ctx = pep_alloc_power_control_ctx();
        if (!ctx)
            return STATUS_INSUFFICIENT_RESOURCES;
        pep_free_power_control_ctx(ctx);
    }

    for (ULONG i = 0; i < tiny_prealloc; i++) {
        pep_result_alloc_t* a =
            (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_tiny_lookaside);
        if (!a)
            return STATUS_INSUFFICIENT_RESOURCES;
        ExFreeToNPagedLookasideList(&g_pep_result_tiny_lookaside, a);
    }

    for (ULONG i = 0; i < small_prealloc; i++) {
        pep_result_alloc_t* a =
            (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_small_lookaside);
        if (!a)
            return STATUS_INSUFFICIENT_RESOURCES;
        ExFreeToNPagedLookasideList(&g_pep_result_small_lookaside, a);
    }

    for (ULONG i = 0; i < large_prealloc; i++) {
        pep_result_alloc_t* a =
            (pep_result_alloc_t*)ExAllocateFromNPagedLookasideList(&g_pep_result_large_lookaside);
        if (!a)
            return STATUS_INSUFFICIENT_RESOURCES;
        ExFreeToNPagedLookasideList(&g_pep_result_large_lookaside, a);
    }

    return STATUS_SUCCESS;
}

NTSTATUS pep_global_init(VOID)
{
    NTSTATUS st;

    if (!g_pep_dev_list_lock) {
        WDF_OBJECT_ATTRIBUTES a;
        WDF_OBJECT_ATTRIBUTES_INIT(&a);
        a.ParentObject = g.driver;
        (VOID) WdfWaitLockCreate(&a, &g_pep_dev_list_lock);
    }

    g_molokai_disable_lpi_support = registry_query_u_long(L"DisableLpiSupport", 1);
    g_boot_veto_enabled = registry_query_u_long(L"BootVetoEnabled", 1);
    g_boot_veto_id = registry_query_u_long(L"BootVetoId", 1);
    g_enable_msft_sd_p_states = registry_query_u_long(L"EnableMsftSdPStates", 1);
    g_pep_bugcheck_enable_mask = registry_query_u_long(L"EnableBugcheck", 1);
    g_bugcheck_perf_boost_enable = registry_query_u_long(L"BugcheckPerfBoostEnable", 1);
    g_bugcheck_perf_boost_state = registry_query_u_long(L"BugcheckPerfBoostState", 0xFFFFFFFFu);
    g_debugger_vote_critical_res_on_reg =
        registry_query_u_long(L"DebuggerVoteCriticalResourceOnRegister", 1);
    pep_ocd_init();
    if (g_boot_veto_id < 1 || g_boot_veto_id > MAX_VETO_REASONS)
        g_boot_veto_id = MAX_VETO_REASONS;

    InterlockedExchange(&g_boot_veto_cleared, 0);
    InterlockedExchange(&g_veto.boot_veto_enumerated, 0);
    InterlockedExchange(&g_vetoes_enumerated, 0);
    InterlockedExchange(&g_registry_veto_applied, 0);
    InterlockedExchange(&g_hibernate_in_progress, 0);
    InterlockedExchange(&g_hibernate_rpmh_synced, 0);
    RtlZeroMemory(g_cpu_pep_handle, sizeof(g_cpu_pep_handle));
    RtlZeroMemory(g_cpu_kernel_handle, sizeof(g_cpu_kernel_handle));
    pep_park_init_state();

    pep_veto_reason_init_if_needed();

    g_registry_veto_id = 1;
    (VOID) pep_reg_veto_reason(g_veto_reason_registry_test, &g_registry_veto_id);
    if (g_registry_veto_id < 1 || g_registry_veto_id > MAX_VETO_REASONS)
        g_registry_veto_id = 1;

    if (g_boot_veto_id >= 1 && g_boot_veto_id <= MAX_VETO_REASONS &&
        !g_veto_reason_names[g_boot_veto_id].Buffer) {
        ULONG id = 0;
        (VOID) pep_reg_veto_reason(g_veto_reason_boot_drips, &id);
        if (id != g_boot_veto_id && id >= 1 && id <= MAX_VETO_REASONS &&
            g_veto_reason_names[id].Buffer) {
            ExAcquireFastMutex(&g_veto_reason_lock);
            if (!g_veto_reason_names[g_boot_veto_id].Buffer) {
                g_veto_reason_names[g_boot_veto_id] = g_veto_reason_names[id];
                g_veto_reason_names[id].Buffer = NULL;
                g_veto_reason_names[id].Length = 0;
                g_veto_reason_names[id].MaximumLength = 0;
            }
            ExReleaseFastMutex(&g_veto_reason_lock);
        }
    }

    st = dcvs_init();
    if (!NT_SUCCESS(st))
        return st;

    st = reg_pep_plugin();
    if (NT_SUCCESS(st))
        pep_reg_bugcheck_callback_if_needed();
    return st;
}

VOID pep_global_deinit(VOID)
{
    /* closing first prevents producer admission during lookaside rundown. */

    InterlockedExchange(&g_pep_work_shutdown, 1);
    pep_ocd_deinit();
    pep_unregister_bugcheck_callback_if_needed();
    pep_restore_after_power_up();
    InterlockedExchange(&g_hibernate_in_progress, 0);
    g_pep_crashdump_dev = NULL;
    g_storage_ocd_dev_handle = NULL;
    g_display_ocd_dev_handle = NULL;
    g_sdc2_ocd_dev_handle = NULL;
    g_enable_pep_ocd_test = 0;
    g_enable_pep_ocd_test_low_perf = 0;
    g_pepocd_test_low_perf_idx = 0;

    while (InterlockedCompareExchange(&g_pep_work_init, 0, 0) == 1) {
        LARGE_INTEGER delay;
        delay.QuadPart = -10000LL;
        (VOID) KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    if (init_gate_ready(&g_pep_work_init)) {
        pep_drain_queued_work_items();

        KeWaitForSingleObject(&g_pep_work_drained_event, Executive, KernelMode, FALSE, NULL);
        ExDeleteNPagedLookasideList(&g_pep_work_lookaside);
        InterlockedExchange(&g_pep_work_init, 0);
    }

    if (init_gate_ready(&g_pep_result_init)) {
        ExDeleteNPagedLookasideList(&g_pep_result_tiny_lookaside);
        ExDeleteNPagedLookasideList(&g_pep_result_small_lookaside);
        ExDeleteNPagedLookasideList(&g_pep_result_large_lookaside);
        InterlockedExchange(&g_pep_result_init, 0);
    }

    if (init_gate_ready(&g_pep_power_ctrl_ctx_init)) {
        ExDeleteNPagedLookasideList(&g_pep_power_ctrl_ctx_lookaside);
        InterlockedExchange(&g_pep_power_ctrl_ctx_init, 0);
    }

    pep_devnode_t* head = NULL;
    if (g_pep_dev_list_lock) {
        WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
        head = g_pep_dev_list_head;
        g_pep_dev_list_head = NULL;
        g_qc_sdc1_node = NULL;
        g_qc_sdc2_node = NULL;
        WdfWaitLockRelease(g_pep_dev_list_lock);
    }
    while (head) {
        pep_devnode_t* n = head;
        head = head->next;
        if (n->runtime_comp) {
            /* retained votes or an uncertain transaction still own this desc and endpoint. Never
             * free them during teardown. */
            if (n->runtime_comp->model.retain)
                continue;
            if (n->runtime_comp->desc)
                ExFreePool(n->runtime_comp->desc);
            if (n->runtime_comp->workspace)
                ExFreePool(n->runtime_comp->workspace);
            ExFreePool(n->runtime_comp);
        }
        if (n->current_p_state)
            ExFreePool(n->current_p_state);
        if (n->current_f_state)
            ExFreePool(n->current_f_state);
        if (n->sdbus_perf_states)
            ExFreePool(n->sdbus_perf_states);
        if (n->comp_perf_ctx)
            comp_perf_deinit_for_dev(&n->comp_perf_ctx);
        if (n->peer_scope)
            ExFreePool(n->peer_scope);
        clock_perf_destroy(n->clock_perf);
        if (n->runtime_dstate_scope)
            ExFreePool(n->runtime_dstate_scope);
        pep_free_unicode_string(&n->instance_path);
        ExFreePool(n);
    }
    if (g_pep_dev_list_lock) {
        WdfObjectDelete(g_pep_dev_list_lock);
        g_pep_dev_list_lock = NULL;
    }

    idle_unmap();
    idle_acpi_free();

    dcvs_deinit();

    if (g_veto_reason_init) {
        ExAcquireFastMutex(&g_veto_reason_lock);
        for (ULONG i = 1; i <= MAX_VETO_REASONS; i++) {
            if (g_veto_reason_names[i].Buffer) {
                ExFreePool(g_veto_reason_names[i].Buffer);
                g_veto_reason_names[i].Buffer = NULL;
                g_veto_reason_names[i].Length = 0;
                g_veto_reason_names[i].MaximumLength = 0;
            }
        }
        g_veto_reason_cnt = 0;
        ExReleaseFastMutex(&g_veto_reason_lock);
    }

    g_registry_veto_id = 1;
    InterlockedExchange(&g_vetoes_enumerated, 0);
    InterlockedExchange(&g_registry_veto_applied, 0);
    InterlockedExchange(&g_hibernate_rpmh_synced, 0);
    RtlZeroMemory(g_cpu_pep_handle, sizeof(g_cpu_pep_handle));
    RtlZeroMemory(g_cpu_kernel_handle, sizeof(g_cpu_kernel_handle));
    pep_park_deinit_state();
}

NTSTATUS pep_reg(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!ctx)
        return STATUS_INVALID_PARAMETER;

    if (ctx->pep_registered)
        return STATUS_SUCCESS;

    NTSTATUS status = reg_pep_plugin();
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "driver: plugin registration failed status=%08X", status);
        return status;
    }

    ctx->pep_registered = TRUE;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "driver: plugin registration complete");
    return STATUS_SUCCESS;
}

VOID pep_unregister(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!ctx)
        return;

    ctx->pep_registered = FALSE;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "driver: plugin unregister");
}
