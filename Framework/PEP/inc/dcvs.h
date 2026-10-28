#pragma once
#include "driver.h"

#define DCVS_CPU_CAPACITY 64u
#define DCVS_PERF_CAPACITY 64u
#define DCVS_HISTORY_CAPACITY 128u

typedef enum dcvs_event_code {
    DCVS_EVENT_INIT = 1u,
    DCVS_EVENT_DEINIT = 2u,
    DCVS_EVENT_PERF_SET = 3u,
    DCVS_EVENT_RESET_FEEDBACK = 4u,
    DCVS_EVENT_POLICY_UPDATE = 5u
} dcvs_event_code_t;

typedef struct dcvs_cpu_snapshot {
    ULONG cpu;
    ULONG domain_idx;
    ULONG current_perf_level;
    ULONG current_freq_hz;
    ULONG requested_perf_state_idx;
    ULONG min_perf;
    ULONG max_perf;
    ULONG desired_perf;
    ULONG epp;
    ULONGLONG enter_time100ns;
    ULONGLONG residency100ns;
    ULONGLONG transition_cnt;
    ULONG normalized_performance;
    ULONG reserved;
} dcvs_cpu_snapshot_t;

typedef struct dcvs_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG cpu;
    ULONG min_perf_state;
    ULONG max_perf_state;
    ULONG desired_perf_state;
    ULONG applied_perf_level;
    ULONG applied_freq_hz;
    ULONG thermal_cap_percent;
    ULONG epp;
    ULONG aux0;
    ULONG aux1;
} dcvs_event_t;

typedef struct dcvs_snapshot {
    ULONG version;
    ULONG initialized;
    ULONG cpu_cnt;
    ULONG domain_id;
    ULONG domain_members;
    ULONG coordination_type;
    ULONG worst_case_transition_latency;
    ULONG worst_case_transition_overhead;
    ULONG enable_perf_selection;
    ULONG enable_thermal_perf_selection;
    ULONG enable_boost;
    ULONG boost_freq_hz;
    ULONG max_freq_hz;
    ULONG min_freq_hz;
    ULONG nominal_freq_hz;
    ULONG lowest_nonlinear_freq_hz;
    ULONG thermal_hot_deci_c;
    ULONG thermal_critical_deci_c;
    ULONG thermal_cap_hot_percent;
    ULONG thermal_cap_critical_percent;
    ULONG current_thermal_cap_percent;
    ULONG perf_table_cnt;
    ULONG perf_table_hz[DCVS_PERF_CAPACITY];
    ULONG cpu_entry_cnt;
    dcvs_cpu_snapshot_t cpus[DCVS_CPU_CAPACITY];
    ULONG policy_update_cnt;
    ULONGLONG last_policy_tick100ns;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_cpu;
    ULONG last_event_min_perf_state;
    ULONG last_event_max_perf_state;
    ULONG last_event_desired_perf_state;
    ULONG last_event_applied_perf_level;
    ULONG last_event_applied_freq_hz;
    ULONG last_event_thermal_cap_percent;
    ULONG last_event_epp;
    ULONG last_event_aux0;
    ULONG last_event_aux1;
    ULONGLONG last_event_tick100ns;
} dcvs_snapshot_t;

NTSTATUS dcvs_init(VOID);
BOOLEAN dcvs_ready(VOID);
VOID dcvs_deinit(VOID);

ULONG dcvs_get_cpu_cnt(VOID);
ULONG dcvs_get_domain_id_for_cpu(_In_ ULONG cpu);
ULONG dcvs_get_domain_members_for_cpu(_In_ ULONG cpu);
ULONG dcvs_get_domain_coordination_type(VOID);
ULONG dcvs_get_worst_case_transition_latency(_In_ ULONG cpu);
ULONG dcvs_get_worst_case_transition_overhead(_In_ ULONG cpu);

ULONG dcvs_get_perf_level_cnt(_In_ ULONG cpu);
ULONG dcvs_get_perf_freq_hz_by_perf_level(_In_ ULONG cpu, _In_ ULONG perf_level);
ULONG dcvs_get_perf_freq_hz_by_perf_state_idx(_In_ ULONG cpu, _In_ ULONG perf_state_idx);

ULONG dcvs_get_max_freq_hz(_In_ ULONG cpu);
ULONG dcvs_get_max_non_boost_freq_hz(_In_ ULONG cpu);
ULONG dcvs_get_min_freq_hz(_In_ ULONG cpu);
ULONG dcvs_get_nominal_freq_hz(_In_ ULONG cpu);
ULONG dcvs_get_lowest_nonlinear_freq_hz(_In_ ULONG cpu);

ULONG dcvs_get_current_freq_hz(_In_ ULONG cpu);
ULONG dcvs_get_current_perf_level(_In_ ULONG cpu);
ULONG dcvs_get_cpu_normalized_performance(_In_ ULONG cpu, _In_ ULONG freq_hz);

/* returns status from the Molokai PDP fast-channel submission; do not log a success if
 * firmware mapping or request submission failed. */

NTSTATUS dcvs_perf_set_state_checked(_In_ ULONG cpu, _In_ ULONG minimum_performance_state,
                                     _In_ ULONG maximum_performance_state,
                                     _In_ ULONG desired_performance_state,
                                     _In_ ULONG energy_performance_preference);

VOID dcvs_perf_set_state(_In_ ULONG cpu, _In_ ULONG minimum_performance_state,
                         _In_ ULONG maximum_performance_state, _In_ ULONG desired_performance_state,
                         _In_ ULONG energy_performance_preference);

VOID dcvs_reset_feedback_counters(VOID);

VOID dcvs_get_snapshot(_Out_ dcvs_snapshot_t* snapshot);
NTSTATUS dcvs_get_history(_Out_writes_to_(capacity, *cnt) dcvs_event_t* events, _In_ ULONG capacity,
                          _Out_ ULONG* cnt, _Out_ ULONG* dropped);
NTSTATUS dcvs_get_perf_table(_Out_writes_to_(capacity, *cnt) ULONG* frequencies_hz,
                             _In_ ULONG capacity, _Out_ ULONG* cnt);
NTSTATUS dcvs_set_policy(_In_ ULONG flags, _In_ ULONG enable_perf_selection,
                         _In_ ULONG enable_thermal_perf_selection, _In_ ULONG thermal_hot_deci_c,
                         _In_ ULONG thermal_critical_deci_c, _In_ ULONG thermal_cap_hot_percent,
                         _In_ ULONG thermal_cap_critical_percent);
