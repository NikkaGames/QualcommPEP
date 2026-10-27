#pragma once
#include "driver.h"

#define BCL_MAX_THRESH_ENTRIES 64u
#define BCL_HISTORY_CAPACITY 128u

typedef enum bcl_event_code {
    BCL_EVENT_INIT = 1u,
    BCL_EVENT_DEINIT = 2u,
    BCL_EVENT_SUBMIT_ACCEPTED = 3u,
    BCL_EVENT_SUBMIT_REJECTED = 4u,
    BCL_EVENT_WORK_ITEM_RUN = 5u,
    BCL_EVENT_THRESHOLD_DOWNSTACK = 6u,
    BCL_EVENT_THRESHOLD_UPDATE = 7u
} bcl_event_code_t;

typedef enum bcl_parking_state {
    BCL_PARKING_INVALID = 0,
    BCL_PARKING_PARK_ON = 1,
    BCL_PARKING_UNPARK_START = 2,
    BCL_PARKING_UNPARKED = 3
} bcl_parking_state_t;

typedef struct bcl_threshold_snapshot {
    ULONG res_id;
    ULONG field;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG update_cnt;
    LONG last_status;
    ULONGLONG last_tick100ns;
    UCHAR flag;
    UCHAR reserved0;
    USHORT reserved1;
} bcl_threshold_snapshot_t;

typedef struct bcl_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG notify_type;
    ULONG parking_state;
    ULONG desired_parked_cpus;
    ULONG res_id;
    ULONG field;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG flag;
    ULONG aux0;
} bcl_event_t;

typedef struct bcl_snapshot {
    ULONG version;
    ULONG test_enabled;
    ULONG current_notify_type;
    ULONG parking_state;
    ULONG desired_parked_cpus;
    ULONG current_parked_cpus;
    ULONG throttle_permille;
    ULONG is_low_imax_state;
    ULONG mitigation_state;
    ULONG threshold_cnt;
    ULONGLONG submit_cnt;
    ULONGLONG submit_accepted;
    ULONGLONG submit_rejected;
    ULONGLONG work_item_run_cnt;
    ULONGLONG threshold_op_cnt;
    ULONGLONG threshold_downstack_success;
    ULONGLONG threshold_downstack_failure;
    ULONGLONG threshold_cache_hits;
    ULONGLONG threshold_cache_misses;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
    ULONG last_res_id;
    ULONG last_field;
    ULONG last_sub_type;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_flag;
    ULONG last_aux0;
    bcl_threshold_snapshot_t thresholds[BCL_MAX_THRESH_ENTRIES];
} bcl_snapshot_t;

NTSTATUS bcl_init(_In_ WDFDRIVER driver);
VOID bcl_deinit(VOID);

NTSTATUS bcl_submit_test(_In_ ULONG notify_type);

NTSTATUS bcl_read_set_threshold(_In_ WDFDEVICE dev, _In_ const ULONG* in, _In_ size_t in_bytes,
                                _Out_writes_bytes_(out_bytes) ULONG* out, _In_ size_t out_bytes,
                                _Out_ size_t* bytes_written);

VOID bcl_get_snapshot(_Out_ bcl_snapshot_t* snapshot);
NTSTATUS bcl_get_history(_Out_writes_to_(capacity, *cnt) bcl_event_t* entries, _In_ ULONG capacity,
                         _Out_ ULONG* cnt, _Out_ ULONG* dropped);
