#include "resource_cache.h"
#include <ntstrsafe.h>

typedef struct res_entry {
    res_key_t key;
    res_value_t val;
    ULONGLONG last_update_tick100ns;
    ULONG update_cnt;
    ULONG active;
} res_entry_t;

static WDFSPINLOCK g_lock;
static res_entry_t g_table[256];
static ULONG g_cnt;
static ULONGLONG g_event_sequence;
static res_branchtrack_event_t g_history[RES_HISTORY_CAPACITY];
static ULONG g_history_next;
static ULONG g_history_cnt;
static ULONG g_history_dropped;
static res_branchtrack_event_t g_last_event;

static res_entry_t* res_find_locked(_In_ res_key_t key)
{
    for (ULONG i = 0; i < g_cnt; i++) {
        if (g_table[i].key.res_id == key.res_id && g_table[i].key.sub_type == key.sub_type)
            return &g_table[i];
    }
    return NULL;
}

static __forceinline ULONGLONG res_now100ns(VOID)
{
    return KeQueryInterruptTime();
}

static __forceinline ULONG res_value_is_active(_In_ res_value_t val)
{
    return (val.value0 | val.value1 | val.value2 | val.value3) ? 1u : 0u;
}

static CHAR res_printable_ascii_a(_In_ UCHAR c)
{
    return (c >= 32u && c <= 126u) ? (CHAR)c : '.';
}

static VOID res_format_rid_sub_type_a(_In_ ULONG res_id, _In_ ULONG sub_type,
                                      _Out_writes_(buf_chars) CHAR* buf, _In_ size_t buf_chars)
{
    if (!buf || buf_chars == 0)
        return;

    (VOID) RtlStringCchPrintfA(buf, buf_chars, "%c%c%c%c/%08X",
                               res_printable_ascii_a((UCHAR)(res_id & 0xFF)),
                               res_printable_ascii_a((UCHAR)((res_id >> 8) & 0xFF)),
                               res_printable_ascii_a((UCHAR)((res_id >> 16) & 0xFF)),
                               res_printable_ascii_a((UCHAR)((res_id >> 24) & 0xFF)), sub_type);
}

static VOID res_record_branch_track_event_locked(_In_ const res_entry_t* entry)
{
    if (!entry)
        return;

    ULONGLONG seq = ++g_event_sequence;
    ULONGLONG tick = entry->last_update_tick100ns;
    if (tick == 0)
        tick = res_now100ns();

    res_branchtrack_event_t ev;
    RtlZeroMemory(&ev, sizeof(ev));
    ev.tick100ns = tick;
    ev.sequence = seq;
    ev.res_id = entry->key.res_id;
    ev.sub_type = entry->key.sub_type;
    ev.value0 = entry->val.value0;
    ev.value1 = entry->val.value1;
    ev.value2 = entry->val.value2;
    ev.value3 = entry->val.value3;
    ev.active = entry->active;
    ev.update_cnt = entry->update_cnt;

    g_last_event = ev;
    g_history[g_history_next] = ev;
    g_history_next = (g_history_next + 1u) % RES_HISTORY_CAPACITY;
    if (g_history_cnt < RES_HISTORY_CAPACITY)
        g_history_cnt++;
    else
        g_history_dropped++;
}

NTSTATUS res_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);

    RtlZeroMemory(g_table, sizeof(g_table));
    g_cnt = 0;
    g_event_sequence = 0;
    g_history_next = 0;
    g_history_cnt = 0;
    g_history_dropped = 0;
    RtlZeroMemory(g_history, sizeof(g_history));
    RtlZeroMemory(&g_last_event, sizeof(g_last_event));

    NTSTATUS st = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_lock);
    return st;
}

VOID res_deinit(VOID)
{
    if (g_lock) {
        WdfObjectDelete(g_lock);
        g_lock = NULL;
    }

    RtlZeroMemory(g_table, sizeof(g_table));
    g_cnt = 0;
    g_event_sequence = 0;
    g_history_next = 0;
    g_history_cnt = 0;
    g_history_dropped = 0;
    RtlZeroMemory(g_history, sizeof(g_history));
    RtlZeroMemory(&g_last_event, sizeof(g_last_event));
}

NTSTATUS res_set(_In_ res_key_t key, _In_ res_value_t val)
{
    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_lock);

    res_entry_t* e = res_find_locked(key);
    if (!e) {
        if (g_cnt >= RTL_NUMBER_OF(g_table)) {
            WdfSpinLockRelease(g_lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        e = &g_table[g_cnt++];
        e->key = key;
        e->update_cnt = 0;
        e->last_update_tick100ns = 0;
        e->active = 0;
    }

    e->val = val;
    e->update_cnt++;
    e->last_update_tick100ns = res_now100ns();
    e->active = res_value_is_active(val);
    res_record_branch_track_event_locked(e);

    WdfSpinLockRelease(g_lock);
    return STATUS_SUCCESS;
}

NTSTATUS res_get(_In_ res_key_t key, _Out_ res_value_t* val)
{
    if (!val)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(val, sizeof(*val));

    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_lock);
    res_entry_t* e = res_find_locked(key);
    if (e)
        *val = e->val;
    WdfSpinLockRelease(g_lock);

    return e ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

NTSTATUS res_get_branch_track_snapshot(_Out_ res_branchtrack_snapshot_t* snapshot)
{
    if (!snapshot)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = RES_BRANCHTRACK_VERSION;

    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_lock);

    ULONG active_cnt = 0;
    for (ULONG i = 0; i < g_cnt; i++) {
        if (g_table[i].active)
            active_cnt++;
    }

    snapshot->entry_cnt = g_cnt;
    snapshot->active_cnt = active_cnt;
    snapshot->event_sequence = g_event_sequence;
    snapshot->history_cnt = g_history_cnt;
    snapshot->history_dropped = g_history_dropped;
    snapshot->last_event_active = g_last_event.active;
    snapshot->last_event_update_cnt = g_last_event.update_cnt;
    snapshot->last_event_res_id = g_last_event.res_id;
    snapshot->last_event_sub_type = g_last_event.sub_type;
    snapshot->last_event_value0 = g_last_event.value0;
    snapshot->last_event_value1 = g_last_event.value1;
    snapshot->last_event_value2 = g_last_event.value2;
    snapshot->last_event_value3 = g_last_event.value3;
    snapshot->last_event_tick100ns = g_last_event.tick100ns;

    WdfSpinLockRelease(g_lock);

    return STATUS_SUCCESS;
}

NTSTATUS res_get_branch_track_history(_Out_writes_to_(capacity, *cnt)
                                          res_branchtrack_event_t* events,
                                      _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_lock);

    ULONG avail = g_history_cnt;
    ULONG copy_cnt = (capacity < avail) ? capacity : avail;
    ULONG start = (avail == RES_HISTORY_CAPACITY) ? g_history_next : 0u;
    if (copy_cnt < avail)
        start = (start + (avail - copy_cnt)) % RES_HISTORY_CAPACITY;

    for (ULONG i = 0; i < copy_cnt; i++) {
        ULONG idx = (start + i) % RES_HISTORY_CAPACITY;
        events[i] = g_history[idx];
    }

    *cnt = copy_cnt;
    *dropped = g_history_dropped;

    WdfSpinLockRelease(g_lock);

    return STATUS_SUCCESS;
}

#pragma pack(push, 1)

typedef struct res_snapshot_hdr {
    ULONG magic;
    ULONG version;
    ULONG cnt;
    ULONG entry_size;
} res_snapshot_hdr_t;

typedef struct res_snapshot_entry {
    ULONG res_id;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
} res_snapshot_entry_t;

#pragma pack(pop)

NTSTATUS res_snapshot(_Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                      _Out_ size_t* written)
{
    if (!out || !written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    size_t need = sizeof(res_snapshot_hdr_t) + (size_t)g_cnt * sizeof(res_snapshot_entry_t);
    if (out_bytes < sizeof(res_snapshot_hdr_t))
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(out, out_bytes);

    WdfSpinLockAcquire(g_lock);
    ULONG cnt = g_cnt;
    size_t payload = sizeof(res_snapshot_hdr_t) + (size_t)cnt * sizeof(res_snapshot_entry_t);
    if (out_bytes < payload)
        cnt = (ULONG)((out_bytes - sizeof(res_snapshot_hdr_t)) / sizeof(res_snapshot_entry_t));

    res_snapshot_hdr_t* h = (res_snapshot_hdr_t*)out;
    h->magic = 0x50515152;
    h->version = 1;
    h->cnt = cnt;
    h->entry_size = sizeof(res_snapshot_entry_t);

    res_snapshot_entry_t* e = (res_snapshot_entry_t*)((PUCHAR)out + sizeof(*h));
    for (ULONG i = 0; i < cnt; i++) {
        e[i].res_id = g_table[i].key.res_id;
        e[i].sub_type = g_table[i].key.sub_type;
        e[i].value0 = g_table[i].val.value0;
        e[i].value1 = g_table[i].val.value1;
        e[i].value2 = g_table[i].val.value2;
        e[i].value3 = g_table[i].val.value3;
    }

    WdfSpinLockRelease(g_lock);

    *written = sizeof(res_snapshot_hdr_t) + (size_t)cnt * sizeof(res_snapshot_entry_t);
    return STATUS_SUCCESS;
}

NTSTATUS res_build_branch_track_status_buf(_Out_writes_bytes_(out_bytes) PVOID out_arg,
                                           _In_ size_t out_bytes, _Out_ size_t* written)
{
    static const CHAR k_name[] = "BranchTrack";
    const size_t name_len = RTL_NUMBER_OF(k_name) - 1u;
    const size_t required = 16u + name_len + 1u;

    if (!out_arg || !written)
        return STATUS_INVALID_PARAMETER;

    *written = 0;

    res_branchtrack_snapshot_t snap;
    RtlZeroMemory(&snap, sizeof(snap));
    NTSTATUS status = res_get_branch_track_snapshot(&snap);
    if (!NT_SUCCESS(status))
        return status;

    if (out_bytes < required)
        return STATUS_BUFFER_TOO_SMALL;

    UCHAR* out = (UCHAR*)out_arg;
    RtlZeroMemory(out, required);
    *(ULONG*)(out + 0) = 1u;
    *(ULONGLONG*)(out + 4) = (ULONGLONG)(name_len + 9u);
    *(ULONG*)(out + 8) = snap.entry_cnt;
    *(ULONG*)(out + 12) = snap.active_cnt;
    RtlCopyMemory(out + 16, k_name, name_len);
    out[16 + name_len] = '\0';

    *written = required;
    return STATUS_SUCCESS;
}

NTSTATUS res_build_branch_track_details_buf(_Out_writes_bytes_(out_bytes) PVOID out_arg,
                                            _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!out_arg || !written)
        return STATUS_INVALID_PARAMETER;

    *written = 0;

    if (!g_lock)
        return STATUS_DEVICE_NOT_READY;

    res_entry_t* copy = (res_entry_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(g_table), 'dRpq');
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;

    ULONG cnt = 0;
    WdfSpinLockAcquire(g_lock);
    cnt = g_cnt;
    if (cnt > RTL_NUMBER_OF(g_table))
        cnt = RTL_NUMBER_OF(g_table);
    if (cnt)
        RtlCopyMemory(copy, g_table, sizeof(g_table[0]) * cnt);
    WdfSpinLockRelease(g_lock);

    size_t required = sizeof(ULONG) * 2u;
    for (ULONG i = 0; i < cnt; i++) {
        CHAR s1[32];
        CHAR s2[64];
        CHAR s3[128];
        size_t l1 = 0, l2 = 0, l3 = 0;

        res_format_rid_sub_type_a(copy[i].key.res_id, copy[i].key.sub_type, s1, RTL_NUMBER_OF(s1));
        (VOID) RtlStringCchPrintfA(s2, RTL_NUMBER_OF(s2), "v0=%08X upd=%lu", copy[i].val.value0,
                                   copy[i].update_cnt);
        (VOID) RtlStringCchPrintfA(s3, RTL_NUMBER_OF(s3), "v1=%08X v2=%08X v3=%08X t=%llu",
                                   copy[i].val.value1, copy[i].val.value2, copy[i].val.value3,
                                   copy[i].last_update_tick100ns);

        (VOID) RtlStringCchLengthA(s1, RTL_NUMBER_OF(s1), &l1);
        (VOID) RtlStringCchLengthA(s2, RTL_NUMBER_OF(s2), &l2);
        (VOID) RtlStringCchLengthA(s3, RTL_NUMBER_OF(s3), &l3);

        size_t rec = l1 + l2 + l3 + 43u;
        if (required > ((size_t)-1) - rec) {
            ExFreePool(copy);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        required += rec;
    }

    if (out_bytes < required) {
        ExFreePool(copy);
        return STATUS_BUFFER_TOO_SMALL;
    }

    UCHAR* out = (UCHAR*)out_arg;
    RtlZeroMemory(out, required);
    *(ULONG*)(out + 0) = 2u;
    *(ULONG*)(out + 4) = 0u;

    size_t used = 0;
    ULONG* prev_next_offset = NULL;
    for (ULONG i = 0; i < cnt; i++) {
        CHAR s1[32];
        CHAR s2[64];
        CHAR s3[128];
        size_t l1 = 0, l2 = 0, l3 = 0;

        res_format_rid_sub_type_a(copy[i].key.res_id, copy[i].key.sub_type, s1, RTL_NUMBER_OF(s1));
        (VOID) RtlStringCchPrintfA(s2, RTL_NUMBER_OF(s2), "v0=%08X upd=%lu", copy[i].val.value0,
                                   copy[i].update_cnt);
        (VOID) RtlStringCchPrintfA(s3, RTL_NUMBER_OF(s3), "v1=%08X v2=%08X v3=%08X t=%llu",
                                   copy[i].val.value1, copy[i].val.value2, copy[i].val.value3,
                                   copy[i].last_update_tick100ns);

        (VOID) RtlStringCchLengthA(s1, RTL_NUMBER_OF(s1), &l1);
        (VOID) RtlStringCchLengthA(s2, RTL_NUMBER_OF(s2), &l2);
        (VOID) RtlStringCchLengthA(s3, RTL_NUMBER_OF(s3), &l3);

        size_t rec = l1 + l2 + l3 + 43u;
        UCHAR* rec_base = out + 8 + used;
        ULONG* next_off = (ULONG*)(rec_base + 8);

        if (prev_next_offset)
            *prev_next_offset = (ULONG)((rec_base + 8) - (UCHAR*)prev_next_offset);

        *next_off = 0;
        *(ULONG*)(rec_base + 12) = 40u;
        *(ULONG*)(rec_base + 16) = copy[i].key.sub_type;
        *(ULONG*)(rec_base + 20) = (ULONG)(l1 + 41u);
        *(ULONG*)(rec_base + 24) = (ULONG)(l1 + l2 + 42u);
        *(UCHAR*)(rec_base + 28) = (UCHAR)(copy[i].active ? 1u : 0u);
        *(ULONGLONG*)(rec_base + 32) = copy[i].last_update_tick100ns;
        *(ULONGLONG*)(rec_base + 40) = (ULONGLONG)copy[i].val.value0;

        RtlCopyMemory(rec_base + 48, s1, l1);
        *(rec_base + 48 + l1) = '\0';
        RtlCopyMemory(rec_base + 49 + l1, s2, l2);
        *(rec_base + 49 + l1 + l2) = '\0';
        RtlCopyMemory(rec_base + 50 + l1 + l2, s3, l3);
        *(rec_base + 50 + l1 + l2 + l3) = '\0';

        used += rec;
        prev_next_offset = next_off;
    }

    *(ULONG*)(out + 4) = (ULONG)used;
    *written = required;

    ExFreePool(copy);
    return STATUS_SUCCESS;
}
