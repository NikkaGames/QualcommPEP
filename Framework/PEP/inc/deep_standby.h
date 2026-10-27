#pragma once
#include "driver.h"

NTSTATUS deep_standby_init(_In_ WDFDRIVER driver);
VOID deep_standby_deinit(VOID);

NTSTATUS deep_standby_attach_dev(_In_ WDFDEVICE dev);
VOID deep_standby_detach_dev(VOID);

VOID deep_standby_set_modem_req(_In_ ULONG req);
ULONG deep_standby_get_modem_req(VOID);

#define DEEP_STANDBY_HISTORY_CAPACITY 64u

typedef enum deep_standby_event_code {
    DEEP_STANDBY_EVENT_INIT = 1,
    DEEP_STANDBY_EVENT_ATTACH = 2,
    DEEP_STANDBY_EVENT_DETACH = 3,
    DEEP_STANDBY_EVENT_CALLBACK_SCREEN = 4,
    DEEP_STANDBY_EVENT_CALLBACK_USER = 5,
    DEEP_STANDBY_EVENT_CALLBACK_POWER = 6,
    DEEP_STANDBY_EVENT_CALLBACK_BATTERY = 7,
    DEEP_STANDBY_EVENT_EVALUATE = 8,
    DEEP_STANDBY_EVENT_MS_ON_DC_CHANGE = 9,
    DEEP_STANDBY_EVENT_TIMER_START = 10,
    DEEP_STANDBY_EVENT_TIMER_EXPIRY = 11,
    DEEP_STANDBY_EVENT_STATE_CHANGE = 12,
    DEEP_STANDBY_EVENT_WLAN_IOCTL = 13,
    DEEP_STANDBY_EVENT_MODEM_IOCTL = 14,
    DEEP_STANDBY_EVENT_MANUAL_MODEM_REQ = 15,
} deep_standby_event_code_t;

typedef struct deep_standby_history_entry {
    ULONGLONG tick100ns;
    ULONG event_code;
    ULONG status;
    ULONG prev_ms_on_dc_pwr;
    ULONG curr_ms_on_dc_pwr;
    ULONG prev_deep_standby_state;
    ULONG curr_deep_standby_state;
    ULONG aux0;
    ULONG aux1;
} deep_standby_history_entry_t;

typedef struct deep_standby_snapshot {
    ULONG version;
    ULONG feature_enabled;
    ULONG callbacks_enabled;
    ULONG modem_enabled;
    ULONG wlan_enabled;

    ULONG power_src;
    ULONG screen_state;
    ULONG user_state;
    ULONG ms_on_dc_pwr;
    ULONG deep_standby_state;

    ULONG timer_expired;
    ULONG batt_drain_exceeded_limit;
    ULONG batt_drain_since_tmr_start;
    ULONG tmr_starting_batt_pct;
    ULONG current_batt_pct;
    ULONG max_batt_drain_pct;
    ULONG timeout_seconds;
    ULONG modem_req;

    ULONG evaluate_cnt;
    ULONG callback_cnt;
    ULONG state_transition_cnt;
    ULONG enter_cnt;
    ULONG exit_cnt;
    ULONG wlan_ioctl_errors;
    ULONG modem_ioctl_errors;
    ULONG last_wlan_ioctl_status;
    ULONG last_modem_ioctl_status;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    ULONG last_event_status;

    ULONGLONG tmr_start_time100ns;
    ULONGLONG deep_standby_deadline100ns;
    ULONGLONG last_evaluate_tick100ns;
    ULONGLONG last_transition_tick100ns;
} deep_standby_snapshot_t;

NTSTATUS deep_standby_get_snapshot(_Out_ deep_standby_snapshot_t* snapshot);
NTSTATUS deep_standby_get_history(_Out_writes_to_(capacity, *cnt)
                                      deep_standby_history_entry_t* entries,
                                  _In_ ULONG capacity, _Out_ PULONG cnt, _Out_opt_ PULONG dropped);
