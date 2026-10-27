#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef enum pep_dcvs_park_queries {
    DCVS_CPU_PARK_STATE = 0,
    DCVS_CPU_STATE_CHANGE = 1,
    DCVS_CLUSTER_PARK_STATE = 2,
    DCVS_CLUSTER_UNPARK_CNT = 3,
    DCVS_CLUSTER_STATE_CHANGE = 4,
} pep_dcvs_park_queries_t;

typedef enum core_rotation_stat {
    CORE_ROTATION_NONE = 0,
    CORE_ROTATION_NO_RECOMMENDATION = 1,
    CORE_ROTATION_NEW_RECOMMENDATION = 2,
    CORE_ROTATION_TOTAL = 3,
} core_rotation_stat_t;

typedef struct cpu_park_mask {
    UCHAR cpu_prev_park_state[PARK_MAX_CPUS_TRACKED];
    UCHAR cpu_cur_park_state[PARK_MAX_CPUS_TRACKED];
    ULONG cpu_transition_cnt[PARK_MAX_CPUS_TRACKED];
    UCHAR cpu_current_preference[PARK_MAX_CPUS_TRACKED];
} cpu_park_mask_t;

typedef struct park_sel_data {
    UCHAR last_pref[PARK_MAX_RANK_CPUS];
    UCHAR pref[PARK_MAX_RANK_CPUS];
    UCHAR cpu_rank[PARK_MAX_RANK_CPUS];
    LONG cpu_temp[PARK_MAX_RANK_CPUS];
    UCHAR core_rotation_stat[2];
    ULONG pref_rail_id;
    ULONG timer_rail_id;
    ULONG num_unpark_rail_id;
    ULONG core_rotation_rail_id;
    ULONGLONG last_evaluation_time[2];
    ULONG timer_threshold_sec[2];
    ULONG last_additional_unparked;
    ULONG last_evaluation_type;
    ULONGLONG last_selection_evaluation_time;
    ULONG active_cpu_cnt;
    ULONG toggle_state;
} park_sel_data_t;

typedef struct pep_proc_park_preference {
    union {
        PVOID proc;
        PVOID kernel_ctx;
    } u;

    UCHAR po_preference;
    UCHAR pep_preference;
    USHORT pad;
} pep_proc_park_preference_t;

typedef struct pep_ppm_park_selection_v2 {
    ULONG additional_unparked_processors;
    ULONG cnt;
    pep_proc_park_preference_t* processors;
    ULONGLONG evaluation_time;
    UCHAR evaluation_type;
    UCHAR pad[7];
} pep_ppm_park_selection_v2_t;

typedef struct pep_proc_park_state {
    union {
        PVOID proc;
        PVOID kernel_ctx;
    } u;

    UCHAR parked;
    UCHAR reserved[3];
} pep_proc_park_state_t;

typedef struct pep_ppm_park_mask {
    ULONG cnt;
    ULONGLONG evaluation_time;
    pep_proc_park_state_t* processors;
} pep_ppm_park_mask_t;
