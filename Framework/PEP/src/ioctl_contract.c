#include "ioctl_contract.h"
#include "diag.h"
#include "stats.h"

NTSTATUS ioctl_require_exact_out(_In_ WDFREQUEST req, _In_ ULONG exact_bytes,
                                 _Outptr_result_bytebuffer_(exact_bytes) PVOID* out,
                                 _Out_ size_t* out_sz, _Out_ size_t* info)
{
    *out = NULL;
    *out_sz = 0;
    *info = 0;

    NTSTATUS st = WdfRequestRetrieveOutputBuffer(req, 1, out, out_sz);
    if (!NT_SUCCESS(st))
        return st;

    if ((ULONG)*out_sz < exact_bytes) {
        *info = exact_bytes;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *info = exact_bytes;
    return STATUS_SUCCESS;
}

NTSTATUS ioctl_require_min_out(_In_ WDFREQUEST req, _In_ ULONG min_bytes,
                               _Outptr_result_bytebuffer_(min_bytes) PVOID* out,
                               _Out_ size_t* out_sz, _Out_ size_t* info)
{
    *out = NULL;
    *out_sz = 0;
    *info = 0;

    NTSTATUS st = WdfRequestRetrieveOutputBuffer(req, 1, out, out_sz);
    if (!NT_SUCCESS(st))
        return st;

    if ((ULONG)*out_sz < min_bytes) {
        *info = min_bytes;
        return STATUS_BUFFER_TOO_SMALL;
    }

    *info = min_bytes;
    return STATUS_SUCCESS;
}

NTSTATUS ioctl_require_min_in(_In_ WDFREQUEST req, _In_ ULONG min_bytes,
                              _Outptr_result_bytebuffer_(min_bytes) PVOID* in, _Out_ size_t* in_sz)
{
    *in = NULL;
    *in_sz = 0;
    return WdfRequestRetrieveInputBuffer(req, min_bytes, in, in_sz);
}

VOID ioctl_complete(_In_ WDFREQUEST req, _In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG in_len,
                    _In_ size_t info)
{

    etw_write_ioctl(code, status, in_len, (ULONG)info);
    stats_record_ioctl(code, status, in_len, (ULONG)info);
    if (status != STATUS_PENDING)
        WdfRequestCompleteWithInformation(req, status, info);
}
