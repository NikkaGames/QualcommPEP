#pragma once
#include "driver.h"
#include <pepfx.h>
#include "idle_types.h"

extern pep_veto_ctx_t g_veto;
extern FAST_MUTEX g_veto_reason_lock;
extern volatile LONG g_veto_reason_init;
extern UNICODE_STRING g_veto_reason_names[MAX_VETO_REASONS + 1];
extern volatile LONG g_veto_reason_cnt;
extern pep_residency_ctx_t g_resid;
extern ULONG g_drips_coordinated_mode_idx;
extern ULONG g_boot_veto_enabled;
extern ULONG g_boot_veto_id;
extern ULONG g_registry_veto_id;
extern volatile LONG g_registry_veto_applied;
extern volatile LONG g_boot_veto_cleared;
extern const WCHAR g_veto_reason_boot_drips[];
extern volatile LONG g_vetoes_enumerated;
extern idle_engine_t g_idle;
extern idle_ctx_t g_idle_acpi;

VOID pep_veto_reason_init_if_needed(VOID);
VOID pep_queue_pending_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id,
                            _In_ UCHAR increment);
VOID pep_query_platform_state_residencies(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_veto_reasons(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_veto_reason(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_enumerate_boot_vetoes(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID idle_acpi_ensure_init(VOID);
VOID idle_acpi_free(VOID);
VOID idle_internal_query_state_name(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num,
                                    _Inout_ pep_ppm_query_state_name_t* state_name);
VOID idle_internal_get_proc_state_data(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num, _In_ ULONG cnt,
                                       _Out_writes_(cnt) pep_proc_idle_state_v2_t* idle_states);
VOID idle_internal_get_coordinated_state_dependency(
    _In_ idle_ctx_t* cntxt, _Inout_ pep_ppm_query_coordinated_dependency_t* dep);
BOOLEAN pep_send_idle_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id,
                           _In_ UCHAR increment);
VOID pep_flush_pending_vetoes(VOID);
VOID pep_init_and_apply_registry_vetoes_if_needed(VOID);
VOID pep_remove_boot_veto_if_applied(VOID);
VOID pep_apply_boot_veto_if_possible(VOID);
VOID idle_unmap(VOID);
NTSTATUS idle_rpmh_sync_enter(_In_ ULONG coordinated_mode_idx);
VOID idle_rpmh_sync_exit(_In_ ULONG coordinated_mode_idx);
VOID idle_ensure_init(VOID);
VOID pep_override_lpi_support(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_update_lpi_proc_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_update_lpi_coordinated_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_idle_lpi_pre_execute(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_idle_lpi_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_ppm_enter_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_ppm_from_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
