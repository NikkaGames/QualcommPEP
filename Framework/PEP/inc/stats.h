#pragma once
#include "driver.h"

#define STATS_HISTORY_CAPACITY 256u

typedef enum stats_event_code {
    STATS_EVENT_UNKNOWN = 0u,
    STATS_EVENT_IOCTL = 1u,
    STATS_EVENT_DEV_ATTACH = 2u,
    STATS_EVENT_DEV_DETACH = 3u,
    STATS_EVENT_D0_ENTRY = 4u,
    STATS_EVENT_D0_EXIT = 5u,
    STATS_EVENT_PO_FX_ACTIVE_CONDITION = 6u,
    STATS_EVENT_PO_FX_IDLE_CONDITION = 7u,
    STATS_EVENT_PO_FX_IDLE_STATE = 8u,
    STATS_EVENT_DEEP_STANDBY_BASE = 0x100u
} stats_event_code_t;

typedef struct stats_event {
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG arg0;
    ULONG arg1;
    ULONG arg2;
    ULONG arg3;
} stats_event_t;

typedef struct stats {
    volatile LONG ioctl_cnt;
    volatile LONG po_fx_power_required;
    volatile LONG po_fx_power_not_required;
    volatile LONG po_fx_comp_active;
    volatile LONG po_fx_comp_idle;
    volatile LONG po_fx_idle_state;
    volatile LONG d0_entry;
    volatile LONG d0_exit;
    volatile LONG dev_attached;
    volatile LONG64 event_cnt;
    volatile LONG64 error_cnt;
    volatile LONG64 ioctl_error_cnt;
    volatile ULONG last_event_code;
    volatile LONG last_event_status;
    volatile ULONG last_event_arg0;
    volatile ULONG last_event_arg1;
    volatile ULONG last_event_arg2;
    volatile ULONG last_event_arg3;
    volatile ULONGLONG last_event_tick100ns;
    volatile ULONG last_ioctl_code;
    volatile LONG last_ioctl_status;
    volatile ULONG last_ioctl_in_len;
    volatile ULONG last_ioctl_out_len;
} stats_t;

typedef struct stats_snapshot {
    ULONG version;
    ULONG ioctl_cnt;
    ULONG po_fx_power_required;
    ULONG po_fx_power_not_required;
    ULONG po_fx_comp_active;
    ULONG po_fx_comp_idle;
    ULONG po_fx_idle_state;
    ULONG d0_entry;
    ULONG d0_exit;
    ULONG dev_attached;
    ULONGLONG event_cnt;
    ULONGLONG error_cnt;
    ULONGLONG ioctl_error_cnt;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_arg0;
    ULONG last_event_arg1;
    ULONG last_event_arg2;
    ULONG last_event_arg3;
    ULONGLONG last_event_tick100ns;
    ULONG last_ioctl_code;
    LONG last_ioctl_status;
    ULONG last_ioctl_in_len;
    ULONG last_ioctl_out_len;
} stats_snapshot_t;

NTSTATUS stats_init(VOID);
VOID stats_deinit(VOID);
VOID stats_reset(VOID);

VOID stats_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG arg0,
                        _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3);

VOID stats_record_ioctl(_In_ ULONG ioctl_code, _In_ NTSTATUS status, _In_ ULONG input_length,
                        _In_ ULONG output_length);

NTSTATUS stats_get_snapshot(_Out_ stats_snapshot_t* snapshot);

NTSTATUS stats_get_history(_Out_writes_to_(capacity, *cnt) stats_event_t* entries,
                           _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);

extern stats_t g_stats;
