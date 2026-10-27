#pragma once
#include "driver.h"
#include <pepfx.h>
#include "park_types.h"

extern park_sel_data_t g_park_sel;
extern cpu_park_mask_t g_cpu_park_state;
extern KSPIN_LOCK g_park_sel_trace_lock;
extern parksel_event_t g_park_sel_history[PARKSEL_HISTORY_CAPACITY];
extern ULONG g_park_sel_history_head;
extern ULONG g_park_sel_history_cnt;
extern ULONG g_park_sel_history_dropped;
extern ULONGLONG g_park_sel_event_sequence;
extern ULONG g_park_sel_last_event_code;
extern LONG g_park_sel_last_event_status;
extern ULONG g_park_sel_last_event_additional_unparked;
extern ULONG g_park_sel_last_event_remaining_unparked;
extern ULONGLONG g_park_sel_last_event_tick100ns;
extern ULONG g_park_sel_thermal_rank_enabled;
extern LONG g_park_sel_thermal_rank_hysteresis_deci_c;
extern ULONG g_park_sel_thermal_rank_update_interval_ms;
extern ULONG g_park_sel_thermal_sensor_base_idx;
extern volatile LONG g_park_sel_thermal_refresh_cnt;
extern volatile LONG g_park_sel_thermal_apply_cnt;
extern LONG g_park_sel_last_thermal_status;

BOOLEAN pep_is_core_rotation_disabled(VOID);
ULONG pep_park_rank_cpu_cnt(VOID);
ULONG pep_dcvs_query_park_info(_In_ pep_dcvs_park_queries_t query_type, _In_ ULONG cpu_num);
VOID pep_park_ensure_trace_init(VOID);
ULONG pep_park_clamp_thermal_update_interval_ms(_In_ ULONG value);
LONG pep_park_clamp_thermal_hysteresis_deci_c(_In_ LONG value);
NTSTATUS pep_park_refresh_thermal_ranking(_In_ BOOLEAN force, _In_ ULONG first_cpu,
                                          _In_ UCHAR evaluation_type,
                                          _In_ ULONGLONG evaluation_time);
VOID pep_park_init_state(VOID);
VOID pep_park_deinit_state(VOID);
VOID pep_park_selection_v2(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_park_mask(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
