#pragma once
#include "driver.h"

#pragma pack(push, 1)

typedef struct diag_entry {
    ULONG ioctl;
    ULONG in_len;
    ULONG out_len;
    ULONG status;
    ULONG_PTR info;
    ULONGLONG tick;
    UCHAR in_head[32];
} diag_entry_t;

typedef struct diag_dump_hdr {
    ULONG magic;
    ULONG version;
    ULONG entry_cnt;
    ULONG entry_size;
} diag_dump_hdr_t;

#pragma pack(pop)

NTSTATUS diag_init(_In_ WDFDRIVER driver);
VOID diag_deinit(VOID);

VOID diag_record(_In_ ULONG ioctl, _In_ ULONG in_len, _In_ ULONG out_len, _In_ NTSTATUS status,
                 _In_ ULONG_PTR info, _In_opt_ const VOID* in_buf, _In_ size_t in_buf_bytes);

ULONG diag_all_enabled(VOID);
ULONG lenient_unknown_enabled(VOID);

NTSTATUS diag_read(_Out_writes_bytes_(out_bytes) VOID* out, _In_ size_t out_bytes,
                   _Out_ size_t* written);
