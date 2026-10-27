#pragma once
#include "driver.h"
#include "acpi.h"

#define TSENS_MAX_SENSORS 64u
#define TSENS_HISTORY_CAPACITY 128u

typedef struct tsens_settings {
    ULONG sensor_cnt;
    ULONG temp_deci_c;
} tsens_settings_t;

typedef struct tsens_threshold_data {
    int high_temp;
    int low_temp;
    WDFOBJECT high_temp_owner_obj;
    int tsens_temp10_c;
} tsens_threshold_data_t, *ptsens_threshold_data_t;

typedef enum tsens_event_code {
    TSENS_EVENT_INIT = 1u,
    TSENS_EVENT_DSM_REFRESH = 2u,
    TSENS_EVENT_READ_BULK = 3u,
    TSENS_EVENT_READ_IDX = 4u,
    TSENS_EVENT_BRACKET = 5u,
    TSENS_EVENT_THRESHOLD_HIT_LOW = 6u,
    TSENS_EVENT_THRESHOLD_HIT_HIGH = 7u,
    TSENS_EVENT_STABLE_COMPLETE = 8u
} tsens_event_code_t;

typedef struct tsens_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG sensor_id;
    LONG temp_deci_c;
    LONG low_deci_c;
    LONG high_deci_c;
    ULONG aux0;
    ULONG aux1;
} tsens_event_t;

typedef struct tsens_sensor_snapshot {
    LONG last_temp_deci_c;
    LONG min_temp_deci_c;
    LONG max_temp_deci_c;
    LONG low_threshold_deci_c;
    LONG high_threshold_deci_c;
    ULONG low_hit_cnt;
    ULONG high_hit_cnt;
    ULONG flags;
} tsens_sensor_snapshot_t;

typedef struct tsens_snapshot {
    ULONG version;
    ULONG sensor_cnt;
    LONG aggregate_temp_deci_c;
    ULONGLONG read_cnt;
    ULONGLONG dsm_refresh_cnt;
    ULONGLONG threshold_hit_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_sensor_id;
    LONG last_temp_deci_c;
    LONG last_low_deci_c;
    LONG last_high_deci_c;
    ULONGLONG last_event_tick100ns;
    tsens_sensor_snapshot_t sensors[TSENS_MAX_SENSORS];
} tsens_snapshot_t;

VOID tsens_add_limit(_In_ ULONG tsens_id, _In_ WDFDEVICE parent_dev);
VOID tsens_remove_limit(_In_ ULONG tsens_id, _In_ WDFDEVICE parent_dev);
VOID tsens_bracket(_In_ ULONG tsens_id, _In_ ptsens_threshold_data_t thresholds);

NTSTATUS tsens_init(_In_ WDFDRIVER driver);
NTSTATUS tsens_init_from_dev(_In_ WDFDEVICE dev);
VOID tsens_deinit(VOID);

NTSTATUS tsens_get_temperatures(_Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                                _Out_ size_t* written);
NTSTATUS tsens_get_temp_for_idx(_In_reads_bytes_(in_bytes) PVOID in, _In_ size_t in_bytes,
                                _Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                                _Out_ size_t* written);
NTSTATUS tsens_process_stable_req(_In_ WDFREQUEST req, _In_ WDFDEVICE dev,
                                  _Out_opt_ size_t* information);
NTSTATUS tsens_get_temp_range(_In_ ULONG sensor_id, _Out_ LONG* min_temp_deci_c,
                              _Out_ LONG* max_temp_deci_c);
NTSTATUS tsens_set_threshold_window(_In_ ULONG sensor_id, _In_ LONG low_threshold_deci_c,
                                    _In_ LONG high_threshold_deci_c, _In_ BOOLEAN enable_low,
                                    _In_ BOOLEAN enable_high);
VOID tsens_get_snapshot(_Out_ tsens_snapshot_t* snapshot);
NTSTATUS tsens_get_history(_Out_writes_to_(capacity, *cnt) tsens_event_t* events,
                           _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);

ULONG tsens_get_cnt(VOID);
ULONG tsens_get_temp_deci_c(VOID);
