#pragma once
#include "driver.h"

/* crash-dump and live-kernel-dump request contracts. */

#define CRASH_SRC_CAPACITY 8u
#define CRASH_HISTORY_CAPACITY 128u

typedef enum crash_event_code {
    CRASH_EVENT_CREATE = 1u,
    CRASH_EVENT_OPEN = 2u,
    CRASH_EVENT_DESTROY = 3u,
    CRASH_EVENT_WRITE = 4u,
    CRASH_EVENT_READ = 5u,
    CRASH_EVENT_CAPTURE = 6u,
    CRASH_EVENT_LIVE_KERNEL_DUMP = 7u,
    CRASH_EVENT_FILE_CLOSE = 8u,
    CRASH_EVENT_SET_ACTIVE = 9u,
    CRASH_EVENT_NOT_SUPPORTED = 0xFFu
} crash_event_code_t;

typedef struct crash_src_info {
    ULONG idx;
    ULONG in_use;
    ULONG guid_valid;
    GUID src_guid;
    ULONG item_cnt;
    ULONG entry_size;
    ULONG blob_len;
    ULONG blob_capacity;
    ULONG ring_read_idx;
    ULONG ring_write_idx;
    ULONG ring_items_present;
    ULONGLONG last_write100ns;
    ULONG has_create_file_object;
    ULONG has_open_file_object;
    ULONG create_file_object_orphaned;
} crash_src_info_t;

typedef struct crash_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG src_idx;
    ULONG active_src;
    ULONG size;
    ULONG create_cnt;
    ULONG open_cnt;
    ULONG destroy_cnt;
    ULONG write_cnt;
    ULONG read_cnt;
    ULONG capture_cnt;
    ULONG live_kernel_create_cnt;
} crash_event_t;

typedef struct crash_snapshot {
    ULONG version;
    ULONG active_src;
    ULONG in_use_src_cnt;
    ULONG src_capacity;
    ULONG create_cnt;
    ULONG open_cnt;
    ULONG destroy_cnt;
    ULONG write_cnt;
    ULONG read_cnt;
    ULONG capture_cnt;
    ULONG live_kernel_create_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_src_idx;
    ULONG last_event_size;
    ULONGLONG last_event_tick100ns;
    crash_src_info_t sources[CRASH_SRC_CAPACITY];
} crash_snapshot_t;

NTSTATUS crash_dump_init(_In_ WDFDRIVER driver);
VOID crash_dump_deinit(VOID);

NTSTATUS crash_dump_handle_ioctl(_In_ ULONG ioctl_code, _In_opt_ PVOID file_object,
                                 _In_opt_ const VOID* in, _In_ size_t in_bytes, _Out_opt_ VOID* out,
                                 _In_ size_t out_bytes, _Out_ size_t* written);

VOID crash_dump_get_snapshot(_Out_ crash_snapshot_t* snapshot);
NTSTATUS crash_dump_get_history(_Out_writes_to_(capacity, *cnt) crash_event_t* events,
                                _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
NTSTATUS crash_dump_get_src_info(_In_ ULONG idx, _Out_ crash_src_info_t* info);
NTSTATUS crash_dump_set_active_src(_In_ ULONG idx);
