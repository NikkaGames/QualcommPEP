#pragma once
#include "driver.h"

#pragma pack(push, 1)

typedef struct peplog_entry {
    ULONGLONG tick;
    ULONG event_id;
    ULONG code;
    ULONG status;
    ULONG arg0;
    ULONG arg1;
    ULONG arg2;
    ULONG arg3;
} peplog_entry_t;

typedef struct peplog_dump_hdr {
    ULONG magic;
    ULONG version;
    ULONG entry_cnt;
    ULONG entry_size;
    ULONG head;
    ULONG wrap_cnt;
    ULONG enabled;
    ULONG flags;
    ULONG level;
} peplog_dump_hdr_t;

typedef struct peplog_command {
    ULONG version;
    ULONG command;
    ULONG arg0;
    ULONG arg1;
} peplog_command_t;

typedef struct peplog_command_reply {
    ULONG magic;
    ULONG version;
    ULONG status;
    ULONG enabled;
    ULONG flags;
    ULONG level;
    ULONG capacity;
    ULONG head;
    ULONG wrap_cnt;
} peplog_command_reply_t;

#pragma pack(pop)

#define PEPLOG_MAGIC_DUMP 0x314C5051u
#define PEPLOG_MAGIC_REPLY 0x31525051u
#define PEPLOG_VERSION 1u

#define PEPLOG_CMD_QUERY 0u
#define PEPLOG_CMD_ENABLE 1u
#define PEPLOG_CMD_CLEAR 2u
#define PEPLOG_CMD_SET_FLAGS 3u
#define PEPLOG_CMD_SET_LEVEL 4u
#define PEPLOG_CMD_DUMP 5u // dump to out buffer.

#define PEPLOG_EVT_DEV_NOTIFY 1u
#define PEPLOG_EVT_PROC_NOTIFY 2u
#define PEPLOG_EVT_POWER_CONTROL 3u
#define PEPLOG_EVT_WORK 4u
#define PEPLOG_EVT_LOG_COMMAND 5u

#define PEPLOG_EVT_IDLE 6u
NTSTATUS pep_log_init(_In_ WDFDRIVER driver);
VOID pep_log_deinit(VOID);

VOID pep_log_write(_In_ ULONG event_id, _In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG arg0,
                   _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3);

NTSTATUS pep_log_dump(_Out_writes_bytes_(out_bytes) VOID* out, _In_ size_t out_bytes,
                      _Out_ size_t* written);
VOID pep_log_clear(VOID);

NTSTATUS pep_log_handle_command(_In_opt_ const VOID* in_buf, _In_ size_t in_bytes,
                                _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                _In_ size_t out_bytes, _Out_ size_t* written);
