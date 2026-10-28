#pragma once
#include "driver.h"
#include <pepfx.h>
#include "npa_client.h"
#include "res_graph.h"
#include "state_txn.h"
#include "dstate_types.h"
#include "dump_types.h"
#include "node_types.h"
#include "work_types.h"

extern volatile LONG g_hibernate_in_progress;
extern PEP_KERNEL_INFORMATION g_kernel_info;
extern PVOID g_cpu_pep_handle[256];
extern PVOID g_cpu_kernel_handle[256];
extern KSPIN_LOCK g_pep_work_lock;
extern LIST_ENTRY g_pep_work_queue;
extern NPAGED_LOOKASIDE_LIST g_pep_work_lookaside;
extern NPAGED_LOOKASIDE_LIST g_pep_result_tiny_lookaside;
extern NPAGED_LOOKASIDE_LIST g_pep_result_small_lookaside;
extern NPAGED_LOOKASIDE_LIST g_pep_result_large_lookaside;
extern NPAGED_LOOKASIDE_LIST g_pep_power_ctrl_ctx_lookaside;
extern volatile LONG g_pep_result_init;
extern volatile LONG g_pep_work_init;
extern volatile LONG g_pep_work_shutdown;
extern KEVENT g_pep_work_drained_event;
extern volatile LONG g_pep_power_ctrl_ctx_init;
extern WDFWAITLOCK g_pep_dev_list_lock;
extern pep_devnode_t* g_pep_dev_list_head;
extern PVOID g_pep_crashdump_dev;
extern pep_devnode_t* g_storage_ocd_dev_handle;
extern pep_devnode_t* g_display_ocd_dev_handle;
extern pep_devnode_t* g_sdc2_ocd_dev_handle;
extern ULONG g_enable_pep_ocd_test;
extern ULONG g_enable_pep_ocd_test_low_perf;
extern ULONG g_pepocd_test_low_perf_idx;
extern volatile LONG g_debugger_present;
extern ULONG g_debugger_break_even_offset_us;
extern ULONG g_debugger_break_even_affects_all_states;
extern ULONG g_debugger_vote_critical_res_on_reg;
extern ULONG g_pep_bugcheck_enable_mask;
extern ULONG g_bugcheck_perf_boost_enable;
extern ULONG g_bugcheck_perf_boost_state;
extern const GUID guid_power_change_p_state;
extern const GUID guid_power_change_p_state_v2;
extern const GUID guid_sdbus_pstate_req;
extern res_graph_t* g_molokai_dpm_authoritative_graph;
extern npa_registry_t* g_molokai_npa_authoritative_clients;
extern ULONG g_molokai_disable_lpi_support;

ULONGLONG pep_now100ns(VOID);
PVOID pep_get_kernel_handle_from_cpu_num(_In_ ULONG cpu);
PVOID pep_extract_kernel_handle_from_pep_handle(_In_opt_ PVOID pep_handle);
ULONG pep_get_current_cpu_num(VOID);
BOOLEAN pep_is_msft_sd_p_states_enabled(VOID);
VOID pep_write_compat_status_no_bytes(_Inout_ pep_power_control_req_t* req, _In_ NTSTATUS status);
BOOLEAN pep_unicode_equals(_In_ const UNICODE_STRING* a, _In_ const UNICODE_STRING* b);
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
NTSTATUS pep_handle_sdbus_reg(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
NTSTATUS pep_handle_sdbus_mmc_info(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
NTSTATUS pep_handle_sdbus_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r);
const GUID* pep_stable_queued_power_code(_In_opt_ const GUID* code);
BOOLEAN pep_resolve_molokai_dev(_In_ const UNICODE_STRING* dev_id,
                                _Out_writes_(256) char path[256]);
power_result_t pep_run_molokai_lifecycle(_In_ const UNICODE_STRING* dev_id, _In_ BOOLEAN prepare);
BOOLEAN pep_is_molokai_dpm_execution_ready(_In_ const UNICODE_STRING* dev_id);
int pep_claim_res_transaction(volatile LONG* slot, LONG target, void* ctx, int (*ready)(void*));
power_result_t pep_run_peer_f_state(peer_native_scope_t* s, ULONG state, power_execution_t* trace,
                                    power_scratch_t* scratch);
BOOLEAN runtime_acpi_path(const UNICODE_STRING* id, char out[256]);
power_result_t pep_runtime_d_execute(runtime_dstate_native_t* scope, ULONG target);
ULONG_PTR pep_prepare_runtime_dstate(pep_prepare_dev_t* p, const char* path);
ULONG_PTR pep_prepare_reviewed_peer(pep_prepare_dev_t* p, const char* path);
ULONG_PTR pep_prepare_runtime_comp(pep_prepare_dev_t* p, const char* path);
BOOLEAN ascii_dev_id(_In_opt_ const UNICODE_STRING* id, char out[128]);
BOOLEAN pep_ufs_comp_desc_matches_graph(const char* path);
int pep_native_owner_lock(void* ctx);
void pep_native_owner_unlock(void* ctx);
BOOLEAN pep_prepare_native_ufs_scope(pep_devnode_t* n, const char* path);
BOOLEAN pep_node_d_state_declared(pep_devnode_t* n, ULONG target);
power_result_t pep_run_node_d_state(pep_devnode_t* n, ULONG target);
BOOLEAN pep_native_ufs_retire(pep_devnode_t* n, const UNICODE_STRING* dev_id);
UCHAR __fastcall
pep_power_on_dump_dev_callback(_In_opt_ pep_crashdump_information_t* crashdump_information);
ULONG get_cpu_num_from_pep_handle(_In_opt_ PVOID pep_handle);
VOID pep_dec_ref32(_Inout_ volatile LONG* v);
NTSTATUS pep_apply_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id);
NTSTATUS pep_remove_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id);
ULONG pep_get_current_cpu_number(VOID);
WCHAR upcase_w(_In_ WCHAR c);
BOOLEAN wcs_contains_insensitive(_In_opt_ PCWSTR haystack, _In_z_ PCWSTR needle);
BOOLEAN pep_is_molokai_idle_execution_ready(VOID);
VOID pep_query_platform_states_acpi_lpi_pep(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
NTSTATUS pep_on_hibernate_enter(VOID);
VOID pep_on_hibernate_resume(VOID);
