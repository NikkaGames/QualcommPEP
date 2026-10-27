#include "stats.h"
#include "resource_cache.h"

stats_t g_stats = {0};

static volatile LONG g_stats_init;
static KSPIN_LOCK g_stats_lock;
static stats_event_t g_stats_history[STATS_HISTORY_CAPACITY];
static ULONG g_stats_head;
static ULONG g_stats_cnt;
static ULONG g_stats_dropped;

static __forceinline BOOLEAN stats_is_init(VOID)
{
    return (InterlockedCompareExchange(&g_stats_init, 0, 0) != 0);
}

static VOID stats_publish_summary(_In_ ULONG history_cnt, _In_ ULONG history_dropped,
                                  _In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG arg0,
                                  _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3,
                                  _In_ ULONGLONG tick100ns)
{
    res_key_t k;
    res_value_t v;

    ULONGLONG event_cnt = (ULONGLONG)g_stats.event_cnt;
    ULONGLONG error_cnt = (ULONGLONG)g_stats.error_cnt;

    k.res_id = 0x30535453u;
    k.sub_type = 0;
    v.value0 = (ULONG)g_stats.ioctl_cnt;
    v.value1 = (ULONG)event_cnt;
    v.value2 = (ULONG)error_cnt;
    v.value3 = history_dropped;
    (VOID) res_set(k, v);

    k.res_id = 0x31535453u;
    k.sub_type = event_code;
    v.value0 = (ULONG)status;
    v.value1 = arg0;
    v.value2 = arg1;
    v.value3 = arg2;
    (VOID) res_set(k, v);

    k.res_id = 0x32535453u;
    k.sub_type = history_cnt;
    v.value0 = arg3;
    v.value1 = (ULONG)(tick100ns & 0xFFFFFFFFull);
    v.value2 = (ULONG)(tick100ns >> 32);
    v.value3 = g_stats.last_ioctl_code;
    (VOID) res_set(k, v);
}

NTSTATUS stats_init(VOID)
{
    if (InterlockedCompareExchange(&g_stats_init, 1, 0) != 0)
        return STATUS_SUCCESS;

    KeInitializeSpinLock(&g_stats_lock);
    stats_reset();
    return STATUS_SUCCESS;
}

VOID stats_deinit(VOID)
{
    if (!stats_is_init())
        return;

    stats_reset();
    InterlockedExchange(&g_stats_init, 0);
}

VOID stats_reset(VOID)
{
    RtlZeroMemory(&g_stats, sizeof(g_stats));

    if (!stats_is_init())
        return;

    KIRQL irql;
    KeAcquireSpinLock(&g_stats_lock, &irql);
    RtlZeroMemory(g_stats_history, sizeof(g_stats_history));
    g_stats_head = 0;
    g_stats_cnt = 0;
    g_stats_dropped = 0;
    KeReleaseSpinLock(&g_stats_lock, irql);
}

VOID stats_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG arg0,
                        _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3)
{
    if (!stats_is_init())
        return;

    ULONGLONG tick = KeQueryInterruptTime();

    (VOID) InterlockedIncrement64(&g_stats.event_cnt);
    if (!NT_SUCCESS(status))
        (VOID) InterlockedIncrement64(&g_stats.error_cnt);

    g_stats.last_event_code = event_code;
    g_stats.last_event_status = (LONG)status;
    g_stats.last_event_arg0 = arg0;
    g_stats.last_event_arg1 = arg1;
    g_stats.last_event_arg2 = arg2;
    g_stats.last_event_arg3 = arg3;
    g_stats.last_event_tick100ns = tick;

    ULONG history_cnt = 0;
    ULONG history_dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_stats_lock, &irql);

    ULONG idx = g_stats_head;
    g_stats_history[idx].tick100ns = tick;
    g_stats_history[idx].event_code = event_code;
    g_stats_history[idx].status = (LONG)status;
    g_stats_history[idx].arg0 = arg0;
    g_stats_history[idx].arg1 = arg1;
    g_stats_history[idx].arg2 = arg2;
    g_stats_history[idx].arg3 = arg3;

    g_stats_head = (idx + 1u) % STATS_HISTORY_CAPACITY;
    if (g_stats_cnt < STATS_HISTORY_CAPACITY)
        g_stats_cnt++;
    else
        g_stats_dropped++;

    history_cnt = g_stats_cnt;
    history_dropped = g_stats_dropped;

    KeReleaseSpinLock(&g_stats_lock, irql);

    stats_publish_summary(history_cnt, history_dropped, event_code, status, arg0, arg1, arg2, arg3,
                          tick);
}

VOID stats_record_ioctl(_In_ ULONG ioctl_code, _In_ NTSTATUS status, _In_ ULONG input_length,
                        _In_ ULONG output_length)
{
    (VOID) InterlockedIncrement(&g_stats.ioctl_cnt);
    if (!NT_SUCCESS(status))
        (VOID) InterlockedIncrement64(&g_stats.ioctl_error_cnt);

    g_stats.last_ioctl_code = ioctl_code;
    g_stats.last_ioctl_status = (LONG)status;
    g_stats.last_ioctl_in_len = input_length;
    g_stats.last_ioctl_out_len = output_length;

    stats_record_event(STATS_EVENT_IOCTL, status, ioctl_code, input_length, output_length,
                       (ULONG)g_stats.ioctl_cnt);
}

NTSTATUS stats_get_snapshot(_Out_ stats_snapshot_t* snapshot)
{
    if (!snapshot)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 2;
    snapshot->ioctl_cnt = (ULONG)g_stats.ioctl_cnt;
    snapshot->po_fx_power_required = (ULONG)g_stats.po_fx_power_required;
    snapshot->po_fx_power_not_required = (ULONG)g_stats.po_fx_power_not_required;
    snapshot->po_fx_comp_active = (ULONG)g_stats.po_fx_comp_active;
    snapshot->po_fx_comp_idle = (ULONG)g_stats.po_fx_comp_idle;
    snapshot->po_fx_idle_state = (ULONG)g_stats.po_fx_idle_state;
    snapshot->d0_entry = (ULONG)g_stats.d0_entry;
    snapshot->d0_exit = (ULONG)g_stats.d0_exit;
    snapshot->dev_attached = (ULONG)g_stats.dev_attached;
    snapshot->event_cnt = (ULONGLONG)g_stats.event_cnt;
    snapshot->error_cnt = (ULONGLONG)g_stats.error_cnt;
    snapshot->ioctl_error_cnt = (ULONGLONG)g_stats.ioctl_error_cnt;
    snapshot->last_event_code = g_stats.last_event_code;
    snapshot->last_event_status = g_stats.last_event_status;
    snapshot->last_event_arg0 = g_stats.last_event_arg0;
    snapshot->last_event_arg1 = g_stats.last_event_arg1;
    snapshot->last_event_arg2 = g_stats.last_event_arg2;
    snapshot->last_event_arg3 = g_stats.last_event_arg3;
    snapshot->last_event_tick100ns = g_stats.last_event_tick100ns;
    snapshot->last_ioctl_code = g_stats.last_ioctl_code;
    snapshot->last_ioctl_status = g_stats.last_ioctl_status;
    snapshot->last_ioctl_in_len = g_stats.last_ioctl_in_len;
    snapshot->last_ioctl_out_len = g_stats.last_ioctl_out_len;

    if (stats_is_init()) {
        KIRQL irql;
        KeAcquireSpinLock(&g_stats_lock, &irql);
        snapshot->history_cnt = g_stats_cnt;
        snapshot->history_dropped = g_stats_dropped;
        KeReleaseSpinLock(&g_stats_lock, irql);
    }

    return STATUS_SUCCESS;
}

NTSTATUS stats_get_history(_Out_writes_to_(capacity, *cnt) stats_event_t* entries,
                           _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!entries || capacity == 0 || !cnt || !dropped)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    if (!stats_is_init())
        return STATUS_SUCCESS;

    KIRQL irql;
    KeAcquireSpinLock(&g_stats_lock, &irql);

    ULONG avail = g_stats_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;
    ULONG start = (g_stats_head + STATS_HISTORY_CAPACITY - avail + skip) % STATS_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % STATS_HISTORY_CAPACITY;
        entries[i] = g_stats_history[idx];
    }

    *cnt = take;
    *dropped = g_stats_dropped + skip;

    KeReleaseSpinLock(&g_stats_lock, irql);
    return STATUS_SUCCESS;
}
