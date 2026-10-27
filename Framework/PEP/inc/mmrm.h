#pragma once

#include "driver.h"

#define MMRM_MAX_CLOCK_DOMAINS 12u
#define MMRM_HISTORY_CAPACITY 128u

typedef struct mmrm_policy_req {
    ULONG version;
    UCHAR wait_for_update;
    UCHAR hibernate;
    UCHAR critical;
    UCHAR thermal_standby;
    ULONG activation_reasons;
    ULONG passive_limit;
    ULONG reserved;
} mmrm_policy_req_t;

typedef struct mmrm_policy_reply {
    ULONG version;
    UCHAR wait_for_update;
    UCHAR hibernate;
    UCHAR critical;
    UCHAR thermal_standby;
    ULONG throttle_reason;
    ULONG throttle_percentage;
    ULONG updated;
    ULONG reserved;
} mmrm_policy_reply_t;

typedef VOID (*mmrm_update_callback_t)(_In_opt_ PVOID ctx);

typedef struct mmrm_domain_snapshot {
    ULONG freq_khz;
    ULONG power_milli_watts;
    ULONG flags;
    ULONG dyn_permille;
    ULONG lkg_permille;
    LONG temp_bias_deci_c;
} mmrm_domain_snapshot_t;

typedef enum mmrm_event_code {
    MMRM_EVENT_INIT = 1u,
    MMRM_EVENT_ATTACH_DEV = 2u,
    MMRM_EVENT_DETACH_DEV = 3u,
    MMRM_EVENT_POLICY_REQ = 4u,
    MMRM_EVENT_THERMAL_SAMPLE = 5u,
    MMRM_EVENT_THROTTLE_CHANGED = 6u,
    MMRM_EVENT_POLICY_WAIT_STATE = 7u,
    MMRM_EVENT_DSM_POLL = 8u
} mmrm_event_code_t;

typedef struct mmrm_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG reason;
    ULONG percent;
    ULONG updated;
    ULONG peak_milli_amps;
    LONG temp_deci_k;
    LONG low_temp_deci_k;
    LONG high_temp_deci_k;
    ULONG passive_limit;
    ULONG activation_reasons;
    ULONG feature_enabled;
    ULONG dsm_flags;
} mmrm_event_t;

typedef struct mmrm_snapshot {
    ULONG version;
    ULONG feature_enabled;
    ULONG reserved_power_milli_watts;
    ULONG sampling_period_milli_seconds;
    ULONG max_throttle_percentage;
    ULONG mitigation_start_threshold_milli_amps;
    ULONG mitigation_stop_threshold_milli_amps;
    ULONG temperature_sensor_id;
    ULONG video_encoding_state;
    ULONG video_decoding_state;
    ULONG last_total_power_milli_watts;
    ULONG last_peak_milli_amps;
    ULONG throttle_reason;
    ULONG throttle_percentage;
    ULONG updated;
    ULONG last_passive_limit;
    ULONG last_activation_reasons;
    ULONG last_version;
    ULONG policy_wait_active;
    ULONG sampling_timer_running;
    ULONG dsm_flags;
    ULONG dsm_refresh_cnt;
    ULONG last_dsm_status;
    ULONG dsm_poll_interval_milli_seconds;
    ULONG last_dsm_policy_item_cnt;
    ULONG last_dsm_clock_item_cnt;
    ULONG peak_voltage_milli_volts;
    ULONG global_dyn_permille;
    ULONG global_lkg_permille;
    ULONG last_camera_power_milli_watts;
    ULONG last_video_power_milli_watts;
    ULONG last_display_power_milli_watts;
    ULONG domain_cnt;
    LONG current_temp_deci_k;
    LONG threshold_low_deci_k;
    LONG threshold_high_deci_k;
    LONG camera_temp_deci_c;
    LONG video_temp_deci_c;
    LONG display_temp_deci_c;
    mmrm_domain_snapshot_t domains[MMRM_MAX_CLOCK_DOMAINS];
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_reason;
    ULONG last_event_percent;
    ULONG last_event_updated;
    ULONG last_event_peak_milli_amps;
    LONG last_event_temp_deci_k;
    LONG last_event_low_temp_deci_k;
    LONG last_event_high_temp_deci_k;
    ULONGLONG last_event_tick100ns;
} mmrm_snapshot_t;

NTSTATUS mmrm_init(_In_ WDFDRIVER driver);
VOID mmrm_deinit(VOID);
VOID mmrm_attach_dev(_In_ WDFDEVICE dev);
VOID mmrm_detach_dev(_In_opt_ WDFDEVICE dev);

VOID mmrm_capture_policy_req(_In_opt_ const mmrm_policy_req_t* req);
VOID mmrm_update_from_thermal_sample(_In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k,
                                     _In_ LONG current_temp_deci_c);
VOID mmrm_reg_update_callback(_In_opt_ mmrm_update_callback_t callback, _In_opt_ PVOID ctx);
VOID mmrm_set_policy_wait_active(_In_ BOOLEAN active);

VOID mmrm_get_throttle(_Out_opt_ PULONG reason, _Out_opt_ PULONG percent);
ULONG mmrm_query_updated_flag(VOID);
VOID mmrm_clear_updated_flag(VOID);
VOID mmrm_get_snapshot(_Out_ mmrm_snapshot_t* snapshot);
NTSTATUS mmrm_get_history(_Out_writes_to_(capacity, *cnt) mmrm_event_t* entries,
                          _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
