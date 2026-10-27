#pragma once
#include "driver.h"

#define EXSOC_DEV_CAPACITY 32u
#define EXSOC_HISTORY_CAPACITY 128u

typedef struct exsoc_dev_state {
    ULONG dev_hash;
    CHAR dev_id[21];
    ULONG dev_id_length;
    ULONG last_d_state;
    ULONG last_ioctl_code;
    ULONG apply_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONGLONG last_tick100ns;
} exsoc_dev_state_t;

typedef struct exsoc_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG ioctl_code;
    LONG status;
    ULONG dev_hash;
    ULONG d_state;
    ULONG requested_output_bytes;
    ULONG returned_information;
} exsoc_event_t;

typedef struct exsoc_snapshot {
    ULONG version;
    ULONG dev_cnt;
    ULONG apply_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG adsp_state_ioctl_cnt;
    ULONG adsp_ctrl_ioctl_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_ioctl_code;
    LONG last_status;
    ULONG last_dev_hash;
    ULONG last_d_state;
    ULONGLONG last_tick100ns;
    exsoc_dev_state_t devices[EXSOC_DEV_CAPACITY];
} exsoc_snapshot_t;

NTSTATUS ex_soc_init(_In_ WDFDRIVER driver);
VOID ex_soc_deinit(VOID);
VOID ex_soc_record_apply(_In_ ULONG ioctl_code, _In_z_ const CHAR* dev_id, _In_ ULONG dev_hash,
                         _In_ ULONG d_state, _In_ NTSTATUS status,
                         _In_ ULONG requested_output_bytes, _In_ ULONG returned_information);
NTSTATUS ex_soc_get_snapshot(_Out_ exsoc_snapshot_t* snapshot);
NTSTATUS ex_soc_get_history(_Out_writes_to_(capacity, *cnt) exsoc_event_t* events,
                            _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
