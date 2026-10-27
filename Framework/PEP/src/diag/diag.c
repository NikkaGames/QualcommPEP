#include "diag.h"

static WDFSPINLOCK g_diag_lock;
static diag_entry_t* g_entries;
static ULONG g_capacity;
static ULONG g_head;

static ULONG g_diag_all;
static ULONG g_lenient_unknown;

NTSTATUS diag_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);

    g_diag_all = registry_query_u_long(L"DiagAllIoctls", 0);
    g_lenient_unknown = registry_query_u_long(L"LenientUnknownIoctl", 0);

    ULONG cap = registry_query_u_long(L"DiagEntryCount", 128);
    if (cap < 16)
        cap = 16;
    if (cap > 2048)
        cap = 2048;

    g_capacity = cap;
    g_head = 0;

    NTSTATUS status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_diag_lock);
    if (!NT_SUCCESS(status))
        return status;

    g_entries = (diag_entry_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                               sizeof(diag_entry_t) * g_capacity, POOL_TAG);
    if (!g_entries)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_entries, sizeof(diag_entry_t) * g_capacity);
    return STATUS_SUCCESS;
}

VOID diag_deinit(VOID)
{
    if (g_diag_lock) {
        WdfObjectDelete(g_diag_lock);
        g_diag_lock = NULL;
    }
    if (g_entries) {
        ExFreePool(g_entries);
        g_entries = NULL;
    }
    g_capacity = 0;
    g_head = 0;
}

static VOID copy_head(_Out_writes_bytes_(32) UCHAR* dst, _In_opt_ const VOID* src,
                      _In_ size_t src_bytes)
{
    RtlZeroMemory(dst, 32);
    if (!src || src_bytes == 0)
        return;
    size_t n = src_bytes < 32 ? src_bytes : 32;
    RtlCopyMemory(dst, src, n);
}

VOID diag_record(_In_ ULONG ioctl, _In_ ULONG in_len, _In_ ULONG out_len, _In_ NTSTATUS status,
                 _In_ ULONG_PTR info, _In_opt_ const VOID* in_buf, _In_ size_t in_buf_bytes)
{
    if (!g_entries || !g_diag_lock)
        return;

    WdfSpinLockAcquire(g_diag_lock);

    ULONG idx = g_head++ % g_capacity;

    diag_entry_t* e = &g_entries[idx];
    e->ioctl = ioctl;
    e->in_len = in_len;
    e->out_len = out_len;
    e->status = (ULONG)status;
    e->info = info;
    e->tick = KeQueryInterruptTime();
    copy_head(e->in_head, in_buf, in_buf_bytes);

    WdfSpinLockRelease(g_diag_lock);
}

NTSTATUS diag_read(_Out_writes_bytes_(out_bytes) VOID* out, _In_ size_t out_bytes,
                   _Out_ size_t* written)
{
    if (!out || !written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!g_entries || !g_diag_lock)
        return STATUS_DEVICE_NOT_READY;

    size_t need = sizeof(diag_dump_hdr_t) + (size_t)g_capacity * sizeof(diag_entry_t);
    if (out_bytes < sizeof(diag_dump_hdr_t))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(out, out_bytes);

    WdfSpinLockAcquire(g_diag_lock);

    ULONG cnt = g_capacity;
    size_t payload = sizeof(diag_dump_hdr_t) + (size_t)cnt * sizeof(diag_entry_t);
    if (out_bytes < payload)
        cnt = (ULONG)((out_bytes - sizeof(diag_dump_hdr_t)) / sizeof(diag_entry_t));

    diag_dump_hdr_t* h = (diag_dump_hdr_t*)out;
    h->magic = 0x31474451;
    h->version = 1;
    h->entry_cnt = cnt;
    h->entry_size = sizeof(diag_entry_t);

    diag_entry_t* d = (diag_entry_t*)((PUCHAR)out + sizeof(*h));
    for (ULONG i = 0; i < cnt; i++)
        d[i] = g_entries[i];

    WdfSpinLockRelease(g_diag_lock);

    *written = sizeof(diag_dump_hdr_t) + (size_t)cnt * sizeof(diag_entry_t);
    return STATUS_SUCCESS;
}

ULONG diag_all_enabled(VOID)
{
    return g_diag_all;
}

ULONG lenient_unknown_enabled(VOID)
{
    return g_lenient_unknown;
}
