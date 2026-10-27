#include "bcl.h"
#include "resource_cache.h"
#include "stats.h"
#include "trace.h"
#include "bcl.tmh"

typedef struct bcl_rs_rec {
    USHORT a0;
    USHORT pad0;
    ULONG field;
    USHORT a1;
    USHORT pad1;
    ULONG value;
} bcl_rs_rec_t;

typedef struct bcl_rs_in {
    ULONG magic;
    ULONG thz;
    ULONG len;
    ULONG cnt;
    bcl_rs_rec_t rec1;
    bcl_rs_rec_t rec2;
    ULONG tail[2];
} bcl_rs_in_t;

typedef struct bcl_rs_out {
    ULONG magic;
    ULONG size;
    ULONG one;
    ULONG rsvd;
    ULONG threshold_out;
} bcl_rs_out_t;

typedef struct thresh_entry {
    ULONG res_id;
    ULONG field;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG update_cnt;
    LONG last_status;
    ULONGLONG last_tick100ns;
    UCHAR flag;
    UCHAR reserved0;
    USHORT reserved1;
} thresh_entry_t;

typedef struct bcl_state {
    ULONG current_notify_type;
    ULONG parking_state;
    ULONG desired_parked_cpus;
    ULONG current_parked_cpus;
    ULONG throttle_permille;
    ULONG is_low_imax_state;
    ULONG mitigation_state;
    ULONG max_park_cpus;
    ULONG park_by_notif[5];
    ULONG throttle_by_notif_permille[5];
    ULONG last_res_id;
    ULONG last_field;
    ULONG last_sub_type;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_flag;
    ULONG last_aux0;
    LONG last_downstack_status;
    ULONGLONG last_downstack_tick100ns;

    ULONGLONG submit_cnt;
    ULONGLONG submit_accepted;
    ULONGLONG submit_rejected;
    ULONGLONG work_item_run_cnt;
    ULONGLONG threshold_op_cnt;
    ULONGLONG threshold_downstack_success;
    ULONGLONG threshold_downstack_failure;
    ULONGLONG threshold_cache_hits;
    ULONGLONG threshold_cache_misses;

    ULONGLONG event_sequence;
    ULONG history_head;
    ULONG history_cnt;
    ULONG history_dropped;
    bcl_event_t history[BCL_HISTORY_CAPACITY];
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
} bcl_state_t;

static WDFWORKITEM g_work_item;
static WDF_OBJECT_ATTRIBUTES g_work_attrs;
static WDFSPINLOCK g_thresh_lock;
static WDFWAITLOCK g_bcl_test_lock;

static volatile LONG g_bcl_notif_type;
static volatile LONG g_bcl_init_state;
static volatile LONG g_bcl_trace_init;

static KSPIN_LOCK g_bcl_trace_lock;
static bcl_state_t g_bcl;

static thresh_entry_t g_thresh_table[BCL_MAX_THRESH_ENTRIES];
static ULONG g_thresh_cnt;

static VOID bcl_publish_summary(_In_ ULONG event_code)
{
    res_key_t k;
    res_value_t v;

    ULONG notif = 0;
    ULONG parking_state = 0;
    ULONG desired_parked = 0;
    ULONG current_parked = 0;
    ULONG throttle = 0;
    ULONG low_imax = 0;
    ULONG mitigation = 0;
    ULONG dropped = 0;

    ULONGLONG accepted = 0;
    ULONGLONG rejected = 0;
    ULONGLONG th_ops = 0;
    ULONGLONG ds_ok = 0;
    ULONGLONG ds_fail = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

    notif = g_bcl.current_notify_type;
    parking_state = g_bcl.parking_state;
    desired_parked = g_bcl.desired_parked_cpus;
    current_parked = g_bcl.current_parked_cpus;
    throttle = g_bcl.throttle_permille;
    low_imax = g_bcl.is_low_imax_state;
    mitigation = g_bcl.mitigation_state;
    dropped = g_bcl.history_dropped;

    accepted = g_bcl.submit_accepted;
    rejected = g_bcl.submit_rejected;
    th_ops = g_bcl.threshold_op_cnt;
    ds_ok = g_bcl.threshold_downstack_success;
    ds_fail = g_bcl.threshold_downstack_failure;

    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    k.res_id = 0x304C4342u;
    k.sub_type = 0;
    v.value0 = (ULONG)accepted;
    v.value1 = (ULONG)rejected;
    v.value2 = (notif & 0xFFu) | ((parking_state & 0xFFu) << 8) | ((desired_parked & 0xFFu) << 16) |
               ((current_parked & 0xFFu) << 24);
    v.value3 = throttle;
    (VOID) res_set(k, v);

    k.res_id = 0x334C4342u;
    k.sub_type = 0;
    v.value0 = (ULONG)th_ops;
    v.value1 = (ULONG)ds_ok;
    v.value2 = (ULONG)ds_fail;
    v.value3 = dropped;
    (VOID) res_set(k, v);

    k.res_id = 0x344C4342u;
    k.sub_type = 0;
    v.value0 = low_imax;
    v.value1 = mitigation;
    v.value2 = (ULONG)g_thresh_cnt;
    v.value3 = event_code;
    (VOID) res_set(k, v);
}

static VOID bcl_publish_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG notify_type,
                              _In_ ULONG desired_parked, _In_ ULONG current_parked)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x314C4342u;
    k.sub_type = event_code;

    v.value0 = (ULONG)status;
    v.value1 = notify_type;
    v.value2 = desired_parked;
    v.value3 = current_parked;

    (VOID) res_set(k, v);
}

static VOID bcl_publish_threshold(_In_ ULONG rid, _In_ ULONG field, _In_ ULONG sub_type,
                                  _In_ ULONG value0, _In_ ULONG value1)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x324C4342u;
    k.sub_type = rid;

    v.value0 = field;
    v.value1 = sub_type;
    v.value2 = value0;
    v.value3 = value1;

    (VOID) res_set(k, v);
}

static VOID bcl_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG notify_type,
                             _In_ ULONG res_id, _In_ ULONG field, _In_ ULONG sub_type,
                             _In_ ULONG value0, _In_ ULONG value1, _In_ ULONG flag, _In_ ULONG aux0)
{
    ULONGLONG tick = KeQueryInterruptTime();
    ULONGLONG seq = 0;
    ULONG parking_state = 0;
    ULONG desired_parked = 0;
    ULONG current_parked = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

    seq = ++g_bcl.event_sequence;

    ULONG idx = g_bcl.history_head % BCL_HISTORY_CAPACITY;
    bcl_event_t* e = &g_bcl.history[idx];

    e->tick100ns = tick;
    e->sequence = seq;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->notify_type = notify_type;
    e->parking_state = g_bcl.parking_state;
    e->desired_parked_cpus = g_bcl.desired_parked_cpus;
    e->res_id = res_id;
    e->field = field;
    e->sub_type = sub_type;
    e->value0 = value0;
    e->value1 = value1;
    e->flag = flag;
    e->aux0 = aux0;

    g_bcl.history_head = (idx + 1u) % BCL_HISTORY_CAPACITY;
    if (g_bcl.history_cnt < BCL_HISTORY_CAPACITY)
        g_bcl.history_cnt++;
    else
        g_bcl.history_dropped++;

    g_bcl.last_event_code = event_code;
    g_bcl.last_event_status = (LONG)status;
    g_bcl.last_event_tick100ns = tick;

    g_bcl.last_res_id = res_id;
    g_bcl.last_field = field;
    g_bcl.last_sub_type = sub_type;
    g_bcl.last_value0 = value0;
    g_bcl.last_value1 = value1;
    g_bcl.last_flag = flag;
    g_bcl.last_aux0 = aux0;

    parking_state = g_bcl.parking_state;
    desired_parked = g_bcl.desired_parked_cpus;
    current_parked = g_bcl.current_parked_cpus;

    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    stats_record_event(0x400u + event_code, status, notify_type, res_id, field, sub_type);

    bcl_publish_event(event_code, status, notify_type, desired_parked, current_parked);
    bcl_publish_summary(event_code);

    if (res_id != 0)
        bcl_publish_threshold(res_id, field, sub_type, value0, value1);

    UNREFERENCED_PARAMETER(parking_state);
}

static BOOLEAN bcl_test_enabled(VOID)
{
    ULONG enabled =
        registry_query_u_long(L"BclTestEnable", registry_query_u_long(L"PepBclTestEnable", 1));
    return (enabled != 0);
}

static NTSTATUS bcl_downstack_read_set_threshold(_In_ WDFDEVICE dev, _In_ ULONG thz,
                                                 _In_ ULONG field, _In_ UCHAR flag,
                                                 _Inout_ PULONG threshold_in_out)
{
    if (!registry_query_u_long(L"ThresholdDownstackEnable", 0))
        return STATUS_SUCCESS;

    if (!threshold_in_out)
        return STATUS_INVALID_PARAMETER;

    WDFIOTARGET target = WdfDeviceGetIoTarget(dev);
    if (!target)
        return STATUS_INVALID_DEVICE_STATE;

    bcl_rs_in_t inb;
    RtlZeroMemory(&inb, sizeof(inb));

    inb.magic = 0x43696541u;
    inb.thz = thz;
    inb.cnt = 4;
    inb.len = 8 * inb.cnt;

    inb.rec1.a0 = 0;
    inb.rec1.field = field;
    inb.rec1.a1 = 0;
    inb.rec1.value = *threshold_in_out;

    inb.rec2.a0 = 0;
    inb.rec2.field = 56;
    inb.rec2.a1 = 0;
    inb.rec2.value = (ULONG)flag;

    bcl_rs_out_t outb;
    RtlZeroMemory(&outb, sizeof(outb));
    outb.magic = 0x426f6541u;
    outb.size = 20;
    outb.one = 1;

    WDF_MEMORY_DESCRIPTOR in_desc;
    WDF_MEMORY_DESCRIPTOR out_desc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in_desc, &inb, sizeof(inb));
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out_desc, &outb, sizeof(outb));

    ULONG_PTR br = 0;
    NTSTATUS st =
        WdfIoTargetSendIoctlSynchronously(target, NULL, 0x32C004u, &in_desc, &out_desc, NULL, &br);
    *threshold_in_out = outb.threshold_out;
    return st;
}

static thresh_entry_t* bcl_find_threshold(_In_ ULONG rid, _In_ ULONG field, _In_ ULONG sub_type)
{
    for (ULONG i = 0; i < g_thresh_cnt; i++) {
        if (g_thresh_table[i].res_id == rid && g_thresh_table[i].field == field &&
            g_thresh_table[i].sub_type == sub_type) {
            return &g_thresh_table[i];
        }
    }

    return NULL;
}

static VOID bcl_cfg_policy(VOID)
{
    ULONG cpu_cnt = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (cpu_cnt > 0)
        g_bcl.max_park_cpus = cpu_cnt - 1;
    else
        g_bcl.max_park_cpus = 0;

    g_bcl.park_by_notif[0] = registry_query_u_long(L"BclParkCpusLevel5", 0);
    g_bcl.park_by_notif[1] = registry_query_u_long(L"BclParkCpusLevel6", 1);
    g_bcl.park_by_notif[2] = registry_query_u_long(L"BclParkCpusLevel7", 2);
    g_bcl.park_by_notif[3] = registry_query_u_long(L"BclParkCpusLevel8", 3);
    g_bcl.park_by_notif[4] = registry_query_u_long(L"BclParkCpusLevel9", 4);

    g_bcl.throttle_by_notif_permille[0] = registry_query_u_long(L"BclThrottlePermilleLevel5", 200);
    g_bcl.throttle_by_notif_permille[1] = registry_query_u_long(L"BclThrottlePermilleLevel6", 350);
    g_bcl.throttle_by_notif_permille[2] = registry_query_u_long(L"BclThrottlePermilleLevel7", 500);
    g_bcl.throttle_by_notif_permille[3] = registry_query_u_long(L"BclThrottlePermilleLevel8", 700);
    g_bcl.throttle_by_notif_permille[4] = registry_query_u_long(L"BclThrottlePermilleLevel9", 900);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_bcl.park_by_notif); i++) {
        if (g_bcl.park_by_notif[i] > g_bcl.max_park_cpus)
            g_bcl.park_by_notif[i] = g_bcl.max_park_cpus;
        if (g_bcl.throttle_by_notif_permille[i] > 1000u)
            g_bcl.throttle_by_notif_permille[i] = 1000u;
    }
}

static VOID bcl_work_item(_In_ WDFWORKITEM work_item)
{
    UNREFERENCED_PARAMETER(work_item);

    ULONG notif =
        (ULONG)InterlockedCompareExchange(&g_bcl_notif_type, g_bcl_notif_type, g_bcl_notif_type);

    ULONG idx = 0;
    ULONG desired_parked = 0;
    ULONG current_parked = 0;
    ULONG throttle = 0;
    ULONG parking_state = BCL_PARKING_INVALID;
    ULONG mitigation = 0;
    ULONG low_imax = 0;

    if (notif >= 5 && notif <= 9) {
        idx = notif - 5;
        desired_parked = g_bcl.park_by_notif[idx];
        throttle = g_bcl.throttle_by_notif_permille[idx];
        low_imax = (notif >= 8) ? 1u : 0u;
        mitigation = notif;

        KIRQL irql;
        KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

        if (desired_parked > g_bcl.current_parked_cpus)
            parking_state = BCL_PARKING_PARK_ON;
        else if (desired_parked < g_bcl.current_parked_cpus)
            parking_state = BCL_PARKING_UNPARK_START;
        else if (g_bcl.current_parked_cpus == 0)
            parking_state = BCL_PARKING_UNPARKED;
        else
            parking_state = g_bcl.parking_state;

        g_bcl.work_item_run_cnt++;
        g_bcl.current_notify_type = notif;
        g_bcl.parking_state = parking_state;
        g_bcl.desired_parked_cpus = desired_parked;
        g_bcl.current_parked_cpus = desired_parked;
        g_bcl.throttle_permille = throttle;
        g_bcl.is_low_imax_state = low_imax;
        g_bcl.mitigation_state = mitigation;

        current_parked = g_bcl.current_parked_cpus;

        KeReleaseSpinLock(&g_bcl_trace_lock, irql);

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_PPM,
                    "bcl: notification=%lu parked=%lu throttle=%lu state=%lu", notif,
                    desired_parked, throttle, parking_state);

        bcl_record_event(BCL_EVENT_WORK_ITEM_RUN, STATUS_SUCCESS, notif, 0, 0, 0, throttle,
                         current_parked, 0, mitigation);

        return;
    }

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
    g_bcl.work_item_run_cnt++;
    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    bcl_record_event(BCL_EVENT_WORK_ITEM_RUN, STATUS_INVALID_PARAMETER, notif, 0, 0, 0, 0, 0, 0, 0);
}

NTSTATUS bcl_init(_In_ WDFDRIVER driver)
{
    NTSTATUS status;

    if (InterlockedCompareExchange(&g_bcl_init_state, 1, 0) != 0)
        return STATUS_SUCCESS;

    if (InterlockedCompareExchange(&g_bcl_trace_init, 1, 0) == 0)
        KeInitializeSpinLock(&g_bcl_trace_lock);

    RtlZeroMemory(&g_bcl, sizeof(g_bcl));

    g_bcl_notif_type = 0;
    g_bcl.parking_state = BCL_PARKING_INVALID;

    bcl_cfg_policy();

    WDF_OBJECT_ATTRIBUTES_INIT(&g_work_attrs);
    g_work_attrs.ParentObject = driver;

    WDF_WORKITEM_CONFIG cfg;
    WDF_WORKITEM_CONFIG_INIT(&cfg, bcl_work_item);

    status = WdfWorkItemCreate(&cfg, &g_work_attrs, &g_work_item);
    if (!NT_SUCCESS(status))
        goto fail;

    status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_thresh_lock);
    if (!NT_SUCCESS(status))
        goto fail;

    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_bcl_test_lock);
    if (!NT_SUCCESS(status))
        goto fail;

    g_thresh_cnt = 0;
    RtlZeroMemory(g_thresh_table, sizeof(g_thresh_table));

    bcl_record_event(BCL_EVENT_INIT, STATUS_SUCCESS, 0, 0, 0, 0, 0, 0, 0, 0);

    return STATUS_SUCCESS;

fail:
    if (g_work_item) {
        WdfObjectDelete(g_work_item);
        g_work_item = NULL;
    }

    if (g_thresh_lock) {
        WdfObjectDelete(g_thresh_lock);
        g_thresh_lock = NULL;
    }

    if (g_bcl_test_lock) {
        WdfObjectDelete(g_bcl_test_lock);
        g_bcl_test_lock = NULL;
    }

    InterlockedExchange(&g_bcl_init_state, 0);
    return status;
}

VOID bcl_deinit(VOID)
{
    if (InterlockedCompareExchange(&g_bcl_init_state, 0, 1) != 1)
        return;

    bcl_record_event(BCL_EVENT_DEINIT, STATUS_SUCCESS, g_bcl.current_notify_type, 0, 0, 0, 0, 0, 0,
                     0);

    if (g_work_item) {
        WdfObjectDelete(g_work_item);
        g_work_item = NULL;
    }

    if (g_thresh_lock) {
        WdfObjectDelete(g_thresh_lock);
        g_thresh_lock = NULL;
    }

    if (g_bcl_test_lock) {
        WdfObjectDelete(g_bcl_test_lock);
        g_bcl_test_lock = NULL;
    }
}

NTSTATUS bcl_submit_test(_In_ ULONG notify_type)
{
    KIRQL irql;

    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
    g_bcl.submit_cnt++;
    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    if (!bcl_test_enabled() || notify_type < 5 || notify_type > 9) {
        KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
        g_bcl.submit_rejected++;
        KeReleaseSpinLock(&g_bcl_trace_lock, irql);

        bcl_record_event(BCL_EVENT_SUBMIT_REJECTED, STATUS_NOT_SUPPORTED, notify_type, 0, 0, 0, 0,
                         0, 0, 0);

        return STATUS_NOT_SUPPORTED;
    }

    if (!g_work_item || !g_bcl_test_lock) {
        KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
        g_bcl.submit_rejected++;
        KeReleaseSpinLock(&g_bcl_trace_lock, irql);

        bcl_record_event(BCL_EVENT_SUBMIT_REJECTED, STATUS_DEVICE_NOT_READY, notify_type, 0, 0, 0,
                         0, 0, 0, 1);

        return STATUS_DEVICE_NOT_READY;
    }

    WdfWaitLockAcquire(g_bcl_test_lock, NULL);

    InterlockedExchange(&g_bcl_notif_type, (LONG)notify_type);
    g.config.PepBcl_Notification_Level = notify_type;
    (VOID) registry_set_u_long(L"PepBcl_Notification_Level", notify_type);

    WdfWorkItemEnqueue(g_work_item);

    WdfWaitLockRelease(g_bcl_test_lock);

    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
    g_bcl.submit_accepted++;
    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    bcl_record_event(BCL_EVENT_SUBMIT_ACCEPTED, STATUS_SUCCESS, notify_type, 0, 0, 0, 0, 0, 0, 0);

    return STATUS_SUCCESS;
}

NTSTATUS bcl_read_set_threshold(_In_ WDFDEVICE dev, _In_ const ULONG* in, _In_ size_t in_bytes,
                                _Out_writes_bytes_(out_bytes) ULONG* out, _In_ size_t out_bytes,
                                _Out_ size_t* bytes_written)
{
    if (!bytes_written)
        return STATUS_INVALID_PARAMETER;

    *bytes_written = 0;

    if (!in || in_bytes < 16 || !out || out_bytes < 16)
        return STATUS_BUFFER_TOO_SMALL;

    ULONG rid = in[0];
    ULONG field = in[1];
    ULONG threshold = in[2];
    ULONG value0 = in[3];
    ULONG flag_word = (in_bytes >= 20) ? in[4] : (ULONG)((PUCHAR)in)[12];
    UCHAR flag = (UCHAR)(flag_word & 0xFFu);

    NTSTATUS ds = bcl_downstack_read_set_threshold(dev, rid, field, flag, &threshold);

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

    g_bcl.threshold_op_cnt++;
    g_bcl.last_downstack_status = (LONG)ds;
    g_bcl.last_downstack_tick100ns = KeQueryInterruptTime();

    if (NT_SUCCESS(ds))
        g_bcl.threshold_downstack_success++;
    else
        g_bcl.threshold_downstack_failure++;

    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    bcl_record_event(BCL_EVENT_THRESHOLD_DOWNSTACK, ds, g_bcl.current_notify_type, rid, field,
                     threshold, value0, 0, flag, 0);

    if (!NT_SUCCESS(ds))
        return ds;

    ULONG subtype = threshold;
    ULONG value1 = 0;

    WdfSpinLockAcquire(g_thresh_lock);

    thresh_entry_t* entry = bcl_find_threshold(rid, field, subtype);
    if (entry) {
        entry->value0 = value0;
        entry->value1 = value1;
        entry->flag = flag;
        entry->update_cnt++;
        entry->last_status = STATUS_SUCCESS;
        entry->last_tick100ns = KeQueryInterruptTime();

        KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
        g_bcl.threshold_cache_hits++;
        KeReleaseSpinLock(&g_bcl_trace_lock, irql);
    } else {
        if (g_thresh_cnt < RTL_NUMBER_OF(g_thresh_table)) {
            entry = &g_thresh_table[g_thresh_cnt++];
            entry->res_id = rid;
            entry->field = field;
            entry->sub_type = subtype;
            entry->value0 = value0;
            entry->value1 = value1;
            entry->flag = flag;
            entry->update_cnt = 1;
            entry->last_status = STATUS_SUCCESS;
            entry->last_tick100ns = KeQueryInterruptTime();
        }

        KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
        g_bcl.threshold_cache_misses++;
        KeReleaseSpinLock(&g_bcl_trace_lock, irql);
    }

    WdfSpinLockRelease(g_thresh_lock);

    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);
    g_bcl.last_res_id = rid;
    g_bcl.last_field = field;
    g_bcl.last_sub_type = subtype;
    g_bcl.last_value0 = value0;
    g_bcl.last_value1 = value1;
    g_bcl.last_flag = flag;
    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    out[0] = rid;
    out[1] = field;
    out[2] = subtype;
    out[3] = value0;

    *bytes_written = 16;

    bcl_record_event(BCL_EVENT_THRESHOLD_UPDATE, STATUS_SUCCESS, g_bcl.current_notify_type, rid,
                     field, subtype, value0, value1, flag, (ULONG)g_thresh_cnt);

    return STATUS_SUCCESS;
}

VOID bcl_get_snapshot(_Out_ bcl_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    RtlZeroMemory(snapshot, sizeof(*snapshot));

    snapshot->version = 2;
    snapshot->test_enabled = bcl_test_enabled() ? 1u : 0u;

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

    snapshot->current_notify_type = g_bcl.current_notify_type;
    snapshot->parking_state = g_bcl.parking_state;
    snapshot->desired_parked_cpus = g_bcl.desired_parked_cpus;
    snapshot->current_parked_cpus = g_bcl.current_parked_cpus;
    snapshot->throttle_permille = g_bcl.throttle_permille;
    snapshot->is_low_imax_state = g_bcl.is_low_imax_state;
    snapshot->mitigation_state = g_bcl.mitigation_state;

    snapshot->submit_cnt = g_bcl.submit_cnt;
    snapshot->submit_accepted = g_bcl.submit_accepted;
    snapshot->submit_rejected = g_bcl.submit_rejected;
    snapshot->work_item_run_cnt = g_bcl.work_item_run_cnt;
    snapshot->threshold_op_cnt = g_bcl.threshold_op_cnt;
    snapshot->threshold_downstack_success = g_bcl.threshold_downstack_success;
    snapshot->threshold_downstack_failure = g_bcl.threshold_downstack_failure;
    snapshot->threshold_cache_hits = g_bcl.threshold_cache_hits;
    snapshot->threshold_cache_misses = g_bcl.threshold_cache_misses;

    snapshot->event_sequence = g_bcl.event_sequence;
    snapshot->history_cnt = g_bcl.history_cnt;
    snapshot->history_dropped = g_bcl.history_dropped;
    snapshot->last_event_code = g_bcl.last_event_code;
    snapshot->last_event_status = g_bcl.last_event_status;
    snapshot->last_event_tick100ns = g_bcl.last_event_tick100ns;

    snapshot->last_res_id = g_bcl.last_res_id;
    snapshot->last_field = g_bcl.last_field;
    snapshot->last_sub_type = g_bcl.last_sub_type;
    snapshot->last_value0 = g_bcl.last_value0;
    snapshot->last_value1 = g_bcl.last_value1;
    snapshot->last_flag = g_bcl.last_flag;
    snapshot->last_aux0 = g_bcl.last_aux0;

    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    WdfSpinLockAcquire(g_thresh_lock);

    snapshot->threshold_cnt = g_thresh_cnt;
    if (snapshot->threshold_cnt > RTL_NUMBER_OF(snapshot->thresholds))
        snapshot->threshold_cnt = RTL_NUMBER_OF(snapshot->thresholds);

    for (ULONG i = 0; i < snapshot->threshold_cnt; i++) {
        snapshot->thresholds[i].res_id = g_thresh_table[i].res_id;
        snapshot->thresholds[i].field = g_thresh_table[i].field;
        snapshot->thresholds[i].sub_type = g_thresh_table[i].sub_type;
        snapshot->thresholds[i].value0 = g_thresh_table[i].value0;
        snapshot->thresholds[i].value1 = g_thresh_table[i].value1;
        snapshot->thresholds[i].update_cnt = g_thresh_table[i].update_cnt;
        snapshot->thresholds[i].last_status = g_thresh_table[i].last_status;
        snapshot->thresholds[i].last_tick100ns = g_thresh_table[i].last_tick100ns;
        snapshot->thresholds[i].flag = g_thresh_table[i].flag;
    }

    WdfSpinLockRelease(g_thresh_lock);
}

NTSTATUS bcl_get_history(_Out_writes_to_(capacity, *cnt) bcl_event_t* entries, _In_ ULONG capacity,
                         _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!entries || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_bcl_trace_lock, &irql);

    ULONG avail = g_bcl.history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;

    ULONG start = (g_bcl.history_head + BCL_HISTORY_CAPACITY - avail + skip) % BCL_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % BCL_HISTORY_CAPACITY;
        entries[i] = g_bcl.history[idx];
    }

    *cnt = take;
    *dropped = g_bcl.history_dropped + skip;

    KeReleaseSpinLock(&g_bcl_trace_lock, irql);

    return STATUS_SUCCESS;
}
