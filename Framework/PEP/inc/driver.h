#pragma once
#include <ntddk.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <intrin.h>

#include "public.h"
#include "trace.h"
#include "etw.h"
#include "resource_ddi.h"
#include "rsc_irq_snapshot.h"
#include "pnp_resources.h"
#include "rpmh_wdf_lifecycle.h"
#include "oem_pnp_d0_lease.h"
#include "oem_rsc_identity_win.h"

#define POOL_TAG '0epq'

typedef struct dev_ctx {
    WDFDEVICE dev;
    WDFQUEUE ioctl_queue;
    WDFINTERRUPT interrupt;
    BOOLEAN is_control_dev;
    BOOLEAN ex_soc_registered;
    BOOLEAN pep_registered;
    BOOLEAN is_first_boot;

    BOOLEAN first_boot_idle_reference_held;
    ULONG port_id;
    UNICODE_STRING hid;
    PVOID po_fx_ctx;
    POHANDLE po_handle;
    /* po_handle can change during PoFx re-reg; pair activation with its ORIGINAL handle.
     * Cleared only after an actual PoFxIdleComponent (or when never acquired). */

    POHANDLE d0_activated_po_handle;
    /* kmdf power-framework settings can be assigned exactly once per WDFDEVICE lifetime,
     * including across ReleaseHardware/hibernate. Not a hardware grant. */

    volatile LONG po_fx_settings_state;
    LONG thermal_low_temp_deci_k;
    LONG thermal_high_temp_deci_k;
    ULONGLONG thermal_limit_expire_tick100ns;
    ULONG thermal_req_id;
    POWER_ACTION pre_power_action;
    /* observed PnP assignment for this exact WDFDEVICE, NOT RSC ownership. Never use this
     * status as proof that RPMh TCS, interrupts, or rails can be programmed. */

    LONG rsc_drv2_assignment_status; /* validity of PEP0 PnP census, not RSC owner. */
    LONG rsc_drv2_assignment_observed;
    LONG rsc_drv2_pn_p_window_present;
    /* snapshot of a PnP IRQ candidate; never a hardware grant. PnP callbacks only. */
    rsc_irq_snapshot_t rsc_irq_snapshot;
    /* per-controller diagnostic match; not an IRQ or TCS ownership grant. */
    rpmh_oem_pnp_observation_t rpmh_oem_pnp_observation;
    /* strictly diagnostic WDF PnP/d0 lifetime (NO ISR binding). */
    rpmh_wdf_lifecycle_t rpmh_wdf_lifecycle;
    /* PnP IRQ37 WDF child, initially inactive. RSC/TCS ownership is separate. */

    WDFINTERRUPT rpmh_observed_wdf_interrupt;
    ULONGLONG rpmh_observed_pnp_generation;
    oem_pnp_d0_lease_t rpmh_d0_lease; /* pnp-only unless separately attested. */
    /* read-only physical DRV2 identity from matching PnP d0 generation. NOT hardware ownership
     * or a write/interrupt authorization. */

    rsc_identity_t rpmh_rsc_identity;
    ULONGLONG rpmh_identity_pnp_generation;
} dev_ctx_t, *p_dev_ctx_t;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(dev_ctx_t, get_dev_ctx)

typedef struct globals {
    WDFDRIVER driver;
    PDRIVER_OBJECT driver_object;
    UNICODE_STRING services_registry_path;
    BOOLEAN control_dev_created;
    ULONGLONG spmi_connection_id;
    ioctl_config_t config;
} globals_t;

extern globals_t g;

EVT_WDF_DRIVER_DEVICE_ADD evt_dev_add;
EVT_WDF_OBJECT_CONTEXT_CLEANUP evt_driver_ctx_cleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP evt_dev_ctx_cleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE evt_dev_prepare_hardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE evt_dev_release_hardware;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT evt_dev_self_managed_io_init;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP evt_dev_self_managed_io_cleanup;
EVT_WDF_DEVICE_SURPRISE_REMOVAL evt_dev_surprise_removal;

EVT_WDF_DEVICE_D0_ENTRY evt_dev_d0_entry;
EVT_WDF_DEVICE_D0_EXIT evt_dev_d0_exit;
EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED evt_dev_d0_entry_post_interrupts_enabled;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED evt_dev_d0_exit_pre_interrupts_disabled;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL evt_io_dev_control;
EVT_WDF_IO_QUEUE_IO_STOP evt_io_stop;

NTSTATUS registry_init(_In_ WDFDRIVER driver);
VOID registry_deinit(VOID);
NTSTATUS registry_read_config(_Out_ ioctl_config_t* cfg);
ULONG registry_query_u_long(_In_z_ PCWSTR name, _In_ ULONG default_value);
NTSTATUS registry_query_unicode_string_alloc(_In_z_ PCWSTR name, _Out_ UNICODE_STRING* out,
                                             _In_ USHORT max_chars);
NTSTATUS registry_query_u_long_in_sub_key(_In_z_ PCWSTR sub_key_path, _In_z_ PCWSTR value_name,
                                          _Out_ PULONG value);

NTSTATUS pep_reg_veto_reason(_In_z_ PCWSTR reason, _Out_ PULONG veto_id);
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
ULONG pep_get_registry_test_veto_id(VOID);
NTSTATUS pep_apply_ex_soc_d_state(_In_z_ const CHAR* dev_id_a, _In_ UCHAR d_state);

#define PARKSEL_MAX_CPU_SNAPSHOT 8u
#define PARKSEL_HISTORY_CAPACITY 128u

typedef enum parksel_event_code {
    PARK_SEL_EVENT_INIT = 1u,
    PARK_SEL_EVENT_SELECTION = 2u,
    PARK_SEL_EVENT_MASK = 3u,
    PARK_SEL_EVENT_CORE_ROTATION = 4u,
    PARK_SEL_EVENT_DEINIT = 5u,
    PARK_SEL_EVENT_THERMAL_RANK = 6u
} parksel_event_code_t;

typedef struct parksel_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG cpu;
    ULONG cpu_cnt;
    ULONG additional_unparked;
    ULONG remaining_unparked;
    ULONG evaluation_type;
    ULONGLONG evaluation_time;
    ULONG cluster0_unparked;
    ULONG cluster1_unparked;
    ULONG aux0;
    ULONG aux1;
} parksel_event_t;

typedef struct parksel_policy {
    ULONG version;
    ULONG flags;
    ULONG thermal_rank_enabled;
    LONG thermal_rank_hysteresis_deci_c;
    ULONG thermal_rank_update_interval_ms;
    ULONG thermal_sensor_base_idx;
    ULONG timer_threshold0_sec;
    ULONG timer_threshold1_sec;
    ULONG thermal_refresh_cnt;
    ULONG thermal_apply_cnt;
    LONG last_thermal_status;
} parksel_policy_t;

typedef struct parksel_snapshot {
    ULONG version;
    ULONG cpu_cnt;
    ULONG last_additional_unparked;
    ULONG last_evaluation_type;
    ULONGLONG last_evaluation_time;
    ULONG timer_threshold0_sec;
    ULONG timer_threshold1_sec;
    ULONG core_rotation_stat0;
    ULONG core_rotation_stat1;
    ULONG cluster0_unparked;
    ULONG cluster1_unparked;
    ULONG pref_rail_id;
    ULONG timer_rail_id;
    ULONG num_unpark_rail_id;
    ULONG core_rotation_rail_id;
    ULONGLONG last_cluster0_evaluation_time;
    ULONGLONG last_cluster1_evaluation_time;
    ULONG active_cpu_cnt;
    ULONG toggle_state;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_additional_unparked;
    ULONG last_event_remaining_unparked;
    ULONGLONG last_event_tick100ns;
    ULONG thermal_rank_enabled;
    LONG thermal_rank_hysteresis_deci_c;
    ULONG thermal_rank_update_interval_ms;
    ULONG thermal_sensor_base_idx;
    ULONG thermal_refresh_cnt;
    ULONG thermal_apply_cnt;
    LONG last_thermal_status;
    UCHAR rank_preference[PARKSEL_MAX_CPU_SNAPSHOT];
    UCHAR previous_rank_preference[PARKSEL_MAX_CPU_SNAPSHOT];
    LONG rank_cpu_temp_deci_c[PARKSEL_MAX_CPU_SNAPSHOT];
    UCHAR current_park_state[PARKSEL_MAX_CPU_SNAPSHOT];
    UCHAR previous_park_state[PARKSEL_MAX_CPU_SNAPSHOT];
    UCHAR current_preference[PARKSEL_MAX_CPU_SNAPSHOT];
    UCHAR current_rank[PARKSEL_MAX_CPU_SNAPSHOT];
    ULONG transition_cnt[PARKSEL_MAX_CPU_SNAPSHOT];
} parksel_snapshot_t;

NTSTATUS pep_get_park_selection_snapshot(_Out_ parksel_snapshot_t* snapshot);
NTSTATUS pep_get_park_selection_history(_Out_writes_to_(capacity, *cnt) parksel_event_t* events,
                                        _In_ ULONG capacity, _Out_ ULONG* cnt,
                                        _Out_ ULONG* dropped);
NTSTATUS pep_get_park_selection_policy(_Out_ parksel_policy_t* policy);
NTSTATUS pep_set_park_selection_policy(_In_ const parksel_policy_t* policy);

VOID pep_query_proc_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_coordinated_state_name(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_platform_states_acpi_lpi_pep(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_proc_idle_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_coordinated_idle_dependency(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

NTSTATUS create_control_dev(_In_ WDFDRIVER driver);

NTSTATUS acpi_query_port_id(_In_ WDFDEVICE dev, _Out_ PULONG port_id);
NTSTATUS query_hardware_id(_In_ WDFDEVICE dev, _Out_ UNICODE_STRING* out_id, _In_ USHORT max_chars);

NTSTATUS pep_global_init(VOID);
VOID pep_global_deinit(VOID);
NTSTATUS pep_reg(_In_ WDFDEVICE dev);
VOID pep_unregister(_In_ WDFDEVICE dev);
NTSTATUS pep_warmup_caches(VOID);
/* a live endpoint retains its provider even after PoFx unregister rundown. */
NTSTATUS pep_native_perf_quiesce_gate(VOID);
NTSTATUS pep_on_hibernate_enter(VOID);
NTSTATUS pep_on_power_down_enter(VOID);
VOID pep_on_hibernate_resume(VOID);

NTSTATUS etw_reg(VOID);
VOID etw_unregister(VOID);

EVT_WDF_DEVICE_FILE_CREATE evt_file_create;
EVT_WDF_FILE_CLOSE evt_file_close;

NTSTATUS tsens_init_from_dev(_In_ WDFDEVICE dev);
VOID tsens_deinit(VOID);

ULONG diag_all_enabled(VOID);
ULONG lenient_unknown_enabled(VOID);

NTSTATUS crash_dump_init(_In_ WDFDRIVER driver);
VOID crash_dump_deinit(VOID);
VOID crash_dump_on_file_close(_In_opt_ PVOID file_object);

NTSTATUS forward_init_from_dev(_In_ WDFDEVICE dev);
VOID forward_prepare_for_hibernate(VOID);
VOID forward_deinit(VOID);
BOOLEAN forward_enabled(VOID);
BOOLEAN forward_is_interesting_ioctl(_In_ ULONG ioctl);
NTSTATUS forward_send_guid(_In_ WDFDEVICE dev, _In_ const GUID* interface_guid, _In_ ULONG ioctl,
                           _In_opt_ PVOID in_buf, _In_ ULONG in_len, _Out_opt_ PVOID out_buf,
                           _In_ ULONG out_len, _Out_opt_ PULONG bytes_returned);
NTSTATUS forward_send(_In_ WDFDEVICE dev, _In_ ULONG ioctl, _In_opt_ PVOID in_buf,
                      _In_ ULONG in_len, _Out_opt_ PVOID out_buf, _In_ ULONG out_len,
                      _Out_opt_ PULONG bytes_returned);

NTSTATUS registry_set_u_long(_In_ PCWSTR name, _In_ ULONG value);

NTSTATUS deep_standby_init(_In_ WDFDRIVER driver);
VOID deep_standby_deinit(VOID);
VOID deep_standby_set_modem_req(_In_ ULONG req);
ULONG deep_standby_get_modem_req(VOID);
