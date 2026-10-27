#pragma once
#include "driver.h"

typedef struct ioctl_requirements {
    ULONG Ioctl;
    ULONG MinIn;
    ULONG MinOut;
    ULONG ExactOut;
} ioctl_requirements_t;

NTSTATUS ioctl_require_exact_out(_In_ WDFREQUEST req, _In_ ULONG exact_bytes,
                                 _Outptr_result_bytebuffer_(exact_bytes) PVOID* out,
                                 _Out_ size_t* out_sz, _Out_ size_t* info);
NTSTATUS ioctl_require_min_out(_In_ WDFREQUEST req, _In_ ULONG min_bytes,
                               _Outptr_result_bytebuffer_(min_bytes) PVOID* out,
                               _Out_ size_t* out_sz, _Out_ size_t* info);
NTSTATUS ioctl_require_min_in(_In_ WDFREQUEST req, _In_ ULONG min_bytes,
                              _Outptr_result_bytebuffer_(min_bytes) PVOID* in, _Out_ size_t* in_sz);

VOID ioctl_complete(_In_ WDFREQUEST req, _In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG in_len,
                    _In_ size_t info);
