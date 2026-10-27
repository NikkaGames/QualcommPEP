#pragma once

#include "driver.h"

#define THERMAL_REQ_HISTORY_CAPACITY 128u

typedef enum thermal_req_event_code {
    THERMAL_REQ_EVENT_INIT = 1u,
    THERMAL_REQ_EVENT_DEINIT = 2u,
    THERMAL_REQ_EVENT_QUEUE_ACCEPTED = 3u,
    THERMAL_REQ_EVENT_QUEUE_BUSY = 4u,
    THERMAL_REQ_EVENT_QUEUE_NO_SLOT = 5u,
    THERMAL_REQ_EVENT_QUEUE_MARK_CANCELABLE_FAILED = 6u,
    THERMAL_REQ_EVENT_CANCEL_REQ = 7u,
    THERMAL_REQ_EVENT_CANCEL_DEV = 8u,
    THERMAL_REQ_EVENT_COMPLETE_TIMEOUT = 9u,
    THERMAL_REQ_EVENT_COMPLETE_LOW_THRESHOLD = 10u,
    THERMAL_REQ_EVENT_COMPLETE_HIGH_THRESHOLD = 11u
} thermal_req_event_code_t;

typedef struct thermal_req_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG req_id;
    ULONG sensor_id;
    LONG temp_deci_k;
    LONG low_temp_deci_k;
    LONG high_temp_deci_k;
    ULONG active_cnt;
    ULONG port_id;
} thermal_req_event_t;

typedef struct thermal_req_snapshot {
    ULONG version;
    ULONG poll_ms;
    ULONG active_cnt;
    ULONG last_req_id;
    ULONGLONG total_queued;
    ULONGLONG total_completed;
    ULONGLONG total_cancelled;
    ULONGLONG total_timeout;
    ULONGLONG total_low_threshold_hits;
    ULONGLONG total_high_threshold_hits;
    ULONGLONG total_busy_rejected;
    ULONGLONG total_no_slot_rejected;
    ULONGLONG total_mark_cancelable_failures;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_req_id;
    ULONG last_event_sensor_id;
    LONG last_event_temp_deci_k;
    LONG last_event_low_temp_deci_k;
    LONG last_event_high_temp_deci_k;
    ULONGLONG last_event_tick100ns;
} thermal_req_snapshot_t;

NTSTATUS thermal_req_init(_In_ WDFDRIVER driver);
VOID thermal_req_deinit(VOID);
VOID thermal_req_cancel_dev(_In_ WDFDEVICE dev);

NTSTATUS thermal_req_queue(_In_ WDFREQUEST req, _In_ WDFDEVICE dev, _In_ ULONG sensor_id,
                           _In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k,
                           _In_ ULONG timeout_ms);

VOID thermal_req_get_snapshot(_Out_ thermal_req_snapshot_t* snapshot);
NTSTATUS thermal_req_get_history(_Out_writes_to_(capacity, *cnt) thermal_req_event_t* entries,
                                 _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
