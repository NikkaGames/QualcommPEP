#pragma once
#include "driver.h"

typedef struct res_key {
    ULONG res_id;
    ULONG sub_type;
} res_key_t;

typedef struct res_value {
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
} res_value_t;

#define RES_BRANCHTRACK_VERSION 1u
#define RES_HISTORY_CAPACITY 256u

typedef struct res_branchtrack_snapshot {
    ULONG version;
    ULONG entry_cnt;
    ULONG active_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_active;
    ULONG last_event_update_cnt;
    ULONG last_event_res_id;
    ULONG last_event_sub_type;
    ULONG last_event_value0;
    ULONG last_event_value1;
    ULONG last_event_value2;
    ULONG last_event_value3;
    ULONGLONG last_event_tick100ns;
} res_branchtrack_snapshot_t;

typedef struct res_branchtrack_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG res_id;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
    ULONG active;
    ULONG update_cnt;
} res_branchtrack_event_t;

NTSTATUS res_init(_In_ WDFDRIVER driver);
VOID res_deinit(VOID);

NTSTATUS res_set(_In_ res_key_t key, _In_ res_value_t val);
NTSTATUS res_get(_In_ res_key_t key, _Out_ res_value_t* val);

NTSTATUS res_snapshot(_Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                      _Out_ size_t* written);
NTSTATUS res_get_branch_track_snapshot(_Out_ res_branchtrack_snapshot_t* snapshot);
NTSTATUS res_get_branch_track_history(_Out_writes_to_(capacity, *cnt)
                                          res_branchtrack_event_t* events,
                                      _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
NTSTATUS res_build_branch_track_status_buf(_Out_writes_bytes_(out_bytes) PVOID out,
                                           _In_ size_t out_bytes, _Out_ size_t* written);
NTSTATUS res_build_branch_track_details_buf(_Out_writes_bytes_(out_bytes) PVOID out,
                                            _In_ size_t out_bytes, _Out_ size_t* written);
