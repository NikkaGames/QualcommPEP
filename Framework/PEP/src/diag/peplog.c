#include "peplog.h"

static WDFSPINLOCK g_pep_log_lock;
static peplog_entry_t* g_entries;
static ULONG g_capacity;
static ULONG g_head;
static ULONG g_enabled;
static ULONG g_flags;
static ULONG g_level;

static ULONG pep_log_clamp_cap(ULONG cap)
{
    if (cap < 64)
        cap = 64;
    if (cap > 8192)
        cap = 8192;
    return cap;
}

NTSTATUS pep_log_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);

    g_enabled = registry_query_u_long(L"PepLogEnabled", 1);
    g_flags = registry_query_u_long(L"PepLogFlags", 0xFFFFFFFFu);
    g_level = registry_query_u_long(L"PepLogLevel", 2);

    ULONG cap = registry_query_u_long(L"PepLogEntryCount", 512);
    g_capacity = pep_log_clamp_cap(cap);
    g_head = 0;

    NTSTATUS status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_pep_log_lock);
    if (!NT_SUCCESS(status))
        return status;

    g_entries = (peplog_entry_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                 sizeof(peplog_entry_t) * g_capacity, POOL_TAG);
    if (!g_entries)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_entries, sizeof(peplog_entry_t) * g_capacity);
    return STATUS_SUCCESS;
}

VOID pep_log_deinit(VOID)
{
    if (g_pep_log_lock) {
        WdfObjectDelete(g_pep_log_lock);
        g_pep_log_lock = NULL;
    }
    if (g_entries) {
        ExFreePool(g_entries);
        g_entries = NULL;
    }
    g_capacity = 0;
    g_head = 0;
    g_enabled = 0;
    g_flags = 0;
    g_level = 0;
}

static __forceinline BOOLEAN pep_log_event_enabled(_In_ ULONG event_id)
{
    if (!g_enabled)
        return FALSE;
    if (g_flags == 0)
        return FALSE;
    if (event_id == 0)
        return FALSE;
    if (event_id <= 32) {
        ULONG bit = 1u << (event_id - 1);
        return (g_flags & bit) != 0;
    }
    return TRUE;
}

VOID pep_log_write(_In_ ULONG event_id, _In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG arg0,
                   _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3)
{
    if (!g_entries || !g_pep_log_lock)
        return;

    if (!pep_log_event_enabled(event_id))
        return;

    WdfSpinLockAcquire(g_pep_log_lock);

    ULONG idx = g_head++ % g_capacity;

    peplog_entry_t* e = &g_entries[idx];
    e->tick = KeQueryInterruptTime();
    e->event_id = event_id;
    e->code = code;
    e->status = (ULONG)status;
    e->arg0 = arg0;
    e->arg1 = arg1;
    e->arg2 = arg2;
    e->arg3 = arg3;

    WdfSpinLockRelease(g_pep_log_lock);
}

VOID pep_log_clear(VOID)
{
    if (!g_entries || !g_pep_log_lock)
        return;

    WdfSpinLockAcquire(g_pep_log_lock);
    RtlZeroMemory(g_entries, sizeof(peplog_entry_t) * g_capacity);
    g_head = 0;
    WdfSpinLockRelease(g_pep_log_lock);
}

NTSTATUS pep_log_dump(_Out_writes_bytes_(out_bytes) VOID* out, _In_ size_t out_bytes,
                      _Out_ size_t* written)
{
    if (!out || !written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!g_entries || !g_pep_log_lock)
        return STATUS_DEVICE_NOT_READY;

    if (out_bytes < sizeof(peplog_dump_hdr_t))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(out, out_bytes);

    WdfSpinLockAcquire(g_pep_log_lock);

    ULONG head = g_head;
    ULONG cap = g_capacity;
    ULONG cnt = head < cap ? head : cap;

    size_t max_cnt = (out_bytes - sizeof(peplog_dump_hdr_t)) / sizeof(peplog_entry_t);
    if ((ULONG)max_cnt < cnt)
        cnt = (ULONG)max_cnt;

    peplog_dump_hdr_t* h = (peplog_dump_hdr_t*)out;
    h->magic = PEPLOG_MAGIC_DUMP;
    h->version = PEPLOG_VERSION;
    h->entry_cnt = cnt;
    h->entry_size = sizeof(peplog_entry_t);
    h->head = head;
    h->wrap_cnt = cap ? (head / cap) : 0;
    h->enabled = g_enabled;
    h->flags = g_flags;
    h->level = g_level;

    peplog_entry_t* d = (peplog_entry_t*)((PUCHAR)out + sizeof(*h));

    ULONG start;
    if (head <= cap)
        start = 0;
    else
        start = head % cap;

    for (ULONG i = 0; i < cnt; i++) {
        ULONG src_idx = (start + i) % cap;
        d[i] = g_entries[src_idx];
    }

    WdfSpinLockRelease(g_pep_log_lock);

    *written = sizeof(peplog_dump_hdr_t) + (size_t)cnt * sizeof(peplog_entry_t);
    return STATUS_SUCCESS;
}

static VOID pep_log_fill_reply(_Out_ peplog_command_reply_t* r, _In_ NTSTATUS st)
{
    RtlZeroMemory(r, sizeof(*r));
    r->magic = PEPLOG_MAGIC_REPLY;
    r->version = PEPLOG_VERSION;
    r->status = (ULONG)st;
    r->enabled = g_enabled;
    r->flags = g_flags;
    r->level = g_level;
    r->capacity = g_capacity;
    r->head = g_head;
    r->wrap_cnt = g_capacity ? (g_head / g_capacity) : 0;
}

NTSTATUS pep_log_handle_command(_In_opt_ const VOID* in_buf, _In_ size_t in_bytes,
                                _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    ULONG cmd = PEPLOG_CMD_QUERY;
    ULONG arg0 = 0;
    ULONG arg1 = 0;

    if (in_buf && in_bytes >= sizeof(ULONG)) {
        if (in_bytes >= sizeof(peplog_command_t)) {
            const peplog_command_t* c = (const peplog_command_t*)in_buf;
            if (c->version == PEPLOG_VERSION) {
                cmd = c->command;
                arg0 = c->arg0;
                arg1 = c->arg1;
            } else {
                const ULONG* p = (const ULONG*)in_buf;
                cmd = p[0];
                if (in_bytes >= 8)
                    arg0 = p[1];
                if (in_bytes >= 12)
                    arg1 = p[2];
            }
        } else {
            const ULONG* p = (const ULONG*)in_buf;
            cmd = p[0];
            if (in_bytes >= 8)
                arg0 = p[1];
            if (in_bytes >= 12)
                arg1 = p[2];
        }
    }

    pep_log_write(PEPLOG_EVT_LOG_COMMAND, cmd, STATUS_SUCCESS, arg0, arg1, (ULONG)in_bytes,
                  (ULONG)out_bytes);

    NTSTATUS status = STATUS_SUCCESS;

    switch (cmd) {
        case PEPLOG_CMD_QUERY:
            status = STATUS_SUCCESS;
            break;

        case PEPLOG_CMD_ENABLE:
            g_enabled = arg0 ? 1 : 0;
            (VOID) registry_set_u_long(L"PepLogEnabled", g_enabled);
            status = STATUS_SUCCESS;
            break;

        case PEPLOG_CMD_CLEAR:
            pep_log_clear();
            status = STATUS_SUCCESS;
            break;

        case PEPLOG_CMD_SET_FLAGS:
            g_flags = arg0;
            (VOID) registry_set_u_long(L"PepLogFlags", g_flags);
            status = STATUS_SUCCESS;
            break;

        case PEPLOG_CMD_SET_LEVEL:
            g_level = arg0;
            (VOID) registry_set_u_long(L"PepLogLevel", g_level);
            status = STATUS_SUCCESS;
            break;

        case PEPLOG_CMD_DUMP:
            if (!out_buf || out_bytes == 0)
                status = STATUS_INVALID_PARAMETER;
            else
                status = pep_log_dump(out_buf, out_bytes, written);
            return status;

        default:
            status = STATUS_INVALID_PARAMETER;
            break;
    }

    if (out_buf && out_bytes >= sizeof(peplog_command_reply_t)) {
        peplog_command_reply_t* rep = (peplog_command_reply_t*)out_buf;
        pep_log_fill_reply(rep, status);
        *written = sizeof(*rep);
        return status;
    }

    if (out_buf && out_bytes > 0) {
        *written = sizeof(peplog_command_reply_t);
        return STATUS_BUFFER_TOO_SMALL;
    }

    return status;
}
