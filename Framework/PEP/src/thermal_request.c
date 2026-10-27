#include "thermal_request.h"
#include "tsens.h"
#include "mmrm.h"
#include "resource_cache.h"
#include "stats.h"

#define THERMAL_REQ_MAX_SLOTS 64u
#define THERMAL_REQ_DEFAULT_POLL_MS 250u

typedef struct thermal_req_slot {
    BOOLEAN in_use;
    WDFREQUEST req;
    WDFDEVICE dev;
    ULONG req_id;
    ULONG sensor_id;
    LONG low_temp_deci_k;
    LONG high_temp_deci_k;
    LONG last_temp_deci_k;
    ULONGLONG enqueue_tick100ns;
    ULONGLONG expire_tick100ns;
} thermal_req_slot_t;

typedef struct thermal_req_ctx {
    volatile LONG infra_state;
    WDFSPINLOCK lock;
    WDFTIMER timer;
    ULONG poll_ms;
    ULONG active_cnt;
    volatile LONG64 next_req_id;
    volatile LONG64 total_queued;
    volatile LONG64 total_completed;
    volatile LONG64 total_cancelled;
    volatile LONG64 total_timeout;
    volatile LONG64 total_low_threshold_hits;
    volatile LONG64 total_high_threshold_hits;
    volatile LONG64 total_busy_rejected;
    volatile LONG64 total_no_slot_rejected;
    volatile LONG64 total_mark_cancelable_failures;
    volatile LONG64 event_sequence;
    volatile ULONG last_event_code;
    volatile LONG last_event_status;
    volatile ULONG last_event_req_id;
    volatile ULONG last_event_sensor_id;
    volatile LONG last_event_temp_deci_k;
    volatile LONG last_event_low_temp_deci_k;
    volatile LONG last_event_high_temp_deci_k;
    volatile ULONGLONG last_event_tick100ns;
    thermal_req_event_t history[THERMAL_REQ_HISTORY_CAPACITY];
    thermal_req_slot_t slots[THERMAL_REQ_MAX_SLOTS];
} thermal_req_ctx_t;

static thermal_req_ctx_t g_thermal_req;

EVT_WDF_TIMER thermal_req_timer_fn;
EVT_WDF_REQUEST_CANCEL thermal_req_cancel_fn;

static ULONG thermal_req_get_port_id(_In_opt_ WDFDEVICE dev)
{
    if (!dev)
        return 0;

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    return ctx ? ctx->port_id : 0;
}

static VOID thermal_req_publish_summary(_In_ const thermal_req_event_t* event)
{
    if (!event)
        return;

    res_key_t k;
    res_value_t v;

    k.res_id = 0x30514854u;
    k.sub_type = 0;
    v.value0 = g_thermal_req.active_cnt;
    v.value1 = (ULONG)g_thermal_req.total_queued;
    v.value2 = (ULONG)g_thermal_req.total_completed;
    v.value3 = (ULONG)g_thermal_req.total_cancelled;
    (VOID) res_set(k, v);

    k.res_id = 0x31514854u;
    k.sub_type = event->event_code;
    v.value0 = (ULONG)event->status;
    v.value1 = event->req_id;
    v.value2 = event->sensor_id;
    v.value3 = event->port_id;
    (VOID) res_set(k, v);

    k.res_id = 0x32514854u;
    k.sub_type = 0;
    v.value0 = (ULONG)g_thermal_req.total_timeout;
    v.value1 = (ULONG)g_thermal_req.total_low_threshold_hits;
    v.value2 = (ULONG)g_thermal_req.total_high_threshold_hits;
    v.value3 = (ULONG)g_thermal_req.total_busy_rejected;
    (VOID) res_set(k, v);
}

static VOID thermal_req_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG req_id,
                                     _In_ ULONG sensor_id, _In_ LONG temp_deci_k,
                                     _In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k,
                                     _In_opt_ WDFDEVICE dev)
{
    ULONGLONG tick = KeQueryInterruptTime();
    ULONGLONG seq = (ULONGLONG)InterlockedIncrement64(&g_thermal_req.event_sequence);
    ULONGLONG idx = (seq - 1ull) % THERMAL_REQ_HISTORY_CAPACITY;
    thermal_req_event_t* e = &g_thermal_req.history[idx];

    e->sequence = seq;
    e->tick100ns = tick;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->req_id = req_id;
    e->sensor_id = sensor_id;
    e->temp_deci_k = temp_deci_k;
    e->low_temp_deci_k = low_temp_deci_k;
    e->high_temp_deci_k = high_temp_deci_k;
    e->active_cnt = g_thermal_req.active_cnt;
    e->port_id = thermal_req_get_port_id(dev);

    g_thermal_req.last_event_code = event_code;
    g_thermal_req.last_event_status = (LONG)status;
    g_thermal_req.last_event_req_id = req_id;
    g_thermal_req.last_event_sensor_id = sensor_id;
    g_thermal_req.last_event_temp_deci_k = temp_deci_k;
    g_thermal_req.last_event_low_temp_deci_k = low_temp_deci_k;
    g_thermal_req.last_event_high_temp_deci_k = high_temp_deci_k;
    g_thermal_req.last_event_tick100ns = tick;

    stats_record_event(0x200u + event_code, status, req_id, sensor_id, (ULONG)temp_deci_k,
                       (ULONG)e->port_id);

    thermal_req_publish_summary(e);
}

static ULONG thermal_read_temp_deci_k(_In_ ULONG sensor_id, _Out_ LONG* temp_deci_c_out)
{
    if (!temp_deci_c_out)
        return 2730u;

    SHORT idx = (SHORT)(sensor_id & 0x7FFFu);
    USHORT temp_k = 0;
    size_t written = 0;
    NTSTATUS st = tsens_get_temp_for_idx(&idx, sizeof(idx), &temp_k, sizeof(temp_k), &written);
    if (!NT_SUCCESS(st) || written < sizeof(temp_k) || temp_k == 0) {
        LONG t = (LONG)tsens_get_temp_deci_c();
        *temp_deci_c_out = t;
        return (t >= 0) ? (ULONG)(t + 2730) : (ULONG)(2730 - (-t));
    }

    *temp_deci_c_out = (LONG)temp_k - 2730;
    return (ULONG)temp_k;
}

static VOID thermal_complete_req(_In_ WDFREQUEST req, _In_ ULONG temp_deci_k)
{
    ULONG* out = NULL;
    size_t out_sz = 0;
    NTSTATUS st = WdfRequestRetrieveOutputBuffer(req, sizeof(ULONG), (PVOID*)&out, &out_sz);
    if (!NT_SUCCESS(st) || !out) {
        WdfRequestCompleteWithInformation(req, st, 0);
        return;
    }
    if (out_sz != sizeof(ULONG)) {
        WdfRequestCompleteWithInformation(req, STATUS_INVALID_PARAMETER, 0);
        return;
    }

    *out = temp_deci_k;
    WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, sizeof(ULONG));
}

static VOID thermal_req_reset_slot(_Out_ thermal_req_slot_t* slot)
{
    if (!slot)
        return;

    slot->in_use = FALSE;
    slot->req = NULL;
    slot->dev = NULL;
    slot->req_id = 0;
    slot->sensor_id = 0;
    slot->low_temp_deci_k = MAXLONG;
    slot->high_temp_deci_k = MAXLONG;
    slot->last_temp_deci_k = 0;
    slot->enqueue_tick100ns = 0;
    slot->expire_tick100ns = 0;
}

static LONG thermal_req_find_free_slot_locked(VOID)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        if (!g_thermal_req.slots[i].in_use)
            return (LONG)i;
    }

    return -1;
}

static LONG thermal_req_find_slot_by_req_locked(_In_ WDFREQUEST req)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        if (g_thermal_req.slots[i].in_use && g_thermal_req.slots[i].req == req)
            return (LONG)i;
    }

    return -1;
}

static LONG thermal_req_find_slot_by_dev_locked(_In_ WDFDEVICE dev)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        if (g_thermal_req.slots[i].in_use && g_thermal_req.slots[i].dev == dev)
            return (LONG)i;
    }

    return -1;
}

static BOOLEAN thermal_req_compute_next_due_locked(_In_ ULONGLONG now100ns, _Out_ PULONG due_ms)
{
    if (!due_ms || g_thermal_req.active_cnt == 0)
        return FALSE;

    ULONG best_ms = MAXULONG;
    ULONG poll_ms = g_thermal_req.poll_ms ? g_thermal_req.poll_ms : THERMAL_REQ_DEFAULT_POLL_MS;

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        thermal_req_slot_t* s = &g_thermal_req.slots[i];
        if (!s->in_use)
            continue;

        ULONG due = poll_ms;
        if (s->expire_tick100ns != 0) {
            if (s->expire_tick100ns <= now100ns) {
                due = 1;
            } else {
                ULONGLONG rem100ns = s->expire_tick100ns - now100ns;
                ULONG rem_ms = (ULONG)(rem100ns / 10000ull);
                if (rem_ms == 0)
                    rem_ms = 1;
                if (rem_ms < due)
                    due = rem_ms;
            }
        }

        if (due == 0)
            due = 1;

        if (due < best_ms)
            best_ms = due;
    }

    if (best_ms == MAXULONG)
        best_ms = poll_ms ? poll_ms : 1;

    *due_ms = (best_ms == 0) ? 1 : best_ms;
    return TRUE;
}

NTSTATUS thermal_req_init(_In_ WDFDRIVER driver)
{
    if (InterlockedCompareExchange(&g_thermal_req.infra_state, 1, 0) != 0)
        return STATUS_SUCCESS;

    RtlZeroMemory(&g_thermal_req, sizeof(g_thermal_req));
    g_thermal_req.poll_ms =
        registry_query_u_long(L"ThermalPollIntervalMs", THERMAL_REQ_DEFAULT_POLL_MS);
    if (g_thermal_req.poll_ms == 0)
        g_thermal_req.poll_ms = THERMAL_REQ_DEFAULT_POLL_MS;

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++)
        thermal_req_reset_slot(&g_thermal_req.slots[i]);

    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = driver;

    NTSTATUS st = WdfSpinLockCreate(&attrs, &g_thermal_req.lock);
    if (!NT_SUCCESS(st))
        goto fail;

    WDF_TIMER_CONFIG cfg;
    WDF_TIMER_CONFIG_INIT(&cfg, thermal_req_timer_fn);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = driver;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;
    st = WdfTimerCreate(&cfg, &attrs, &g_thermal_req.timer);
    if (!NT_SUCCESS(st))
        goto fail;

    InterlockedExchange(&g_thermal_req.infra_state, 2);
    thermal_req_record_event(THERMAL_REQ_EVENT_INIT, STATUS_SUCCESS, 0, 0, 0, MAXLONG, MAXLONG,
                             NULL);
    return STATUS_SUCCESS;

fail:
    if (g_thermal_req.timer) {
        WdfObjectDelete(g_thermal_req.timer);
        g_thermal_req.timer = NULL;
    }
    if (g_thermal_req.lock) {
        WdfObjectDelete(g_thermal_req.lock);
        g_thermal_req.lock = NULL;
    }
    InterlockedExchange(&g_thermal_req.infra_state, 0);
    return st;
}

VOID thermal_req_deinit(VOID)
{
    if (InterlockedCompareExchange(&g_thermal_req.infra_state, 0, 0) != 2)
        return;

    typedef struct thermal_req_finish {
        WDFREQUEST req;
        WDFDEVICE dev;
        ULONG req_id;
        ULONG sensor_id;
    } thermal_req_finish_t;

    thermal_req_finish_t finish[THERMAL_REQ_MAX_SLOTS];
    ULONG finish_cnt = 0;

    if (g_thermal_req.lock) {
        WdfSpinLockAcquire(g_thermal_req.lock);
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
            thermal_req_slot_t* s = &g_thermal_req.slots[i];
            if (!s->in_use)
                continue;

            if (finish_cnt < RTL_NUMBER_OF(finish)) {
                finish[finish_cnt].req = s->req;
                finish[finish_cnt].dev = s->dev;
                finish[finish_cnt].req_id = s->req_id;
                finish[finish_cnt].sensor_id = s->sensor_id;
                finish_cnt++;
            }

            thermal_req_reset_slot(s);
        }

        g_thermal_req.active_cnt = 0;
        WdfSpinLockRelease(g_thermal_req.lock);
    }

    if (g_thermal_req.timer)
        (VOID) WdfTimerStop(g_thermal_req.timer, FALSE);

    for (ULONG i = 0; i < finish_cnt; i++) {
        if (!finish[i].req)
            continue;

        if (finish[i].dev)
            tsens_remove_limit(finish[i].sensor_id, finish[i].dev);

        NTSTATUS unmark = WdfRequestUnmarkCancelable(finish[i].req);
        if (unmark != STATUS_CANCELLED)
            WdfRequestComplete(finish[i].req, STATUS_CANCELLED);

        (VOID) InterlockedIncrement64(&g_thermal_req.total_cancelled);
        thermal_req_record_event(THERMAL_REQ_EVENT_CANCEL_DEV, STATUS_CANCELLED, finish[i].req_id,
                                 finish[i].sensor_id, 0, MAXLONG, MAXLONG, finish[i].dev);
    }

    if (g_thermal_req.timer) {
        WdfObjectDelete(g_thermal_req.timer);
        g_thermal_req.timer = NULL;
    }
    if (g_thermal_req.lock) {
        WdfObjectDelete(g_thermal_req.lock);
        g_thermal_req.lock = NULL;
    }

    thermal_req_record_event(THERMAL_REQ_EVENT_DEINIT, STATUS_SUCCESS, 0, 0, 0, MAXLONG, MAXLONG,
                             NULL);

    RtlZeroMemory(&g_thermal_req, sizeof(g_thermal_req));
    InterlockedExchange(&g_thermal_req.infra_state, 0);
}

VOID thermal_req_cancel_dev(_In_ WDFDEVICE dev)
{
    if (!dev)
        return;

    if (InterlockedCompareExchange(&g_thermal_req.infra_state, 0, 0) != 2 || !g_thermal_req.lock)
        return;

    typedef struct thermal_req_finish {
        WDFREQUEST req;
        WDFDEVICE dev;
        ULONG req_id;
        ULONG sensor_id;
    } thermal_req_finish_t;

    thermal_req_finish_t finish[THERMAL_REQ_MAX_SLOTS];
    ULONG finish_cnt = 0;
    ULONG active_left = 0;

    WdfSpinLockAcquire(g_thermal_req.lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        thermal_req_slot_t* s = &g_thermal_req.slots[i];
        if (!s->in_use || s->dev != dev)
            continue;

        if (finish_cnt < RTL_NUMBER_OF(finish)) {
            finish[finish_cnt].req = s->req;
            finish[finish_cnt].dev = s->dev;
            finish[finish_cnt].req_id = s->req_id;
            finish[finish_cnt].sensor_id = s->sensor_id;
            finish_cnt++;
        }

        thermal_req_reset_slot(s);
        if (g_thermal_req.active_cnt != 0)
            g_thermal_req.active_cnt--;
    }
    active_left = g_thermal_req.active_cnt;
    WdfSpinLockRelease(g_thermal_req.lock);

    if (active_left == 0 && g_thermal_req.timer)
        (VOID) WdfTimerStop(g_thermal_req.timer, FALSE);

    for (ULONG i = 0; i < finish_cnt; i++) {
        if (!finish[i].req)
            continue;

        if (finish[i].dev)
            tsens_remove_limit(finish[i].sensor_id, finish[i].dev);

        NTSTATUS unmark = WdfRequestUnmarkCancelable(finish[i].req);
        if (unmark != STATUS_CANCELLED)
            WdfRequestComplete(finish[i].req, STATUS_CANCELLED);

        (VOID) InterlockedIncrement64(&g_thermal_req.total_cancelled);
        thermal_req_record_event(THERMAL_REQ_EVENT_CANCEL_DEV, STATUS_CANCELLED, finish[i].req_id,
                                 finish[i].sensor_id, 0, MAXLONG, MAXLONG, finish[i].dev);
    }
}

NTSTATUS thermal_req_queue(_In_ WDFREQUEST req, _In_ WDFDEVICE dev, _In_ ULONG sensor_id,
                           _In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k,
                           _In_ ULONG timeout_ms)
{
    if (!req || !dev || timeout_ms == 0)
        return STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_thermal_req.infra_state, 0, 0) != 2 || !g_thermal_req.lock ||
        !g_thermal_req.timer)
        return STATUS_INVALID_DEVICE_STATE;

    ULONGLONG now = KeQueryInterruptTime();
    ULONGLONG delta = (ULONGLONG)timeout_ms * 10000ull;
    ULONGLONG expire_at = now + delta;
    if (expire_at < now)
        expire_at = (ULONGLONG)-1;

    LONG slot_idx = -1;
    ULONG req_id = 0;

    WdfSpinLockAcquire(g_thermal_req.lock);

    if (thermal_req_find_slot_by_dev_locked(dev) >= 0) {
        WdfSpinLockRelease(g_thermal_req.lock);
        (VOID) InterlockedIncrement64(&g_thermal_req.total_busy_rejected);
        thermal_req_record_event(THERMAL_REQ_EVENT_QUEUE_BUSY, STATUS_DEVICE_BUSY, 0, sensor_id, 0,
                                 low_temp_deci_k, high_temp_deci_k, dev);
        return STATUS_DEVICE_BUSY;
    }

    slot_idx = thermal_req_find_free_slot_locked();
    if (slot_idx < 0) {
        WdfSpinLockRelease(g_thermal_req.lock);
        (VOID) InterlockedIncrement64(&g_thermal_req.total_no_slot_rejected);
        thermal_req_record_event(THERMAL_REQ_EVENT_QUEUE_NO_SLOT, STATUS_INSUFFICIENT_RESOURCES, 0,
                                 sensor_id, 0, low_temp_deci_k, high_temp_deci_k, dev);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    req_id = (ULONG)InterlockedIncrement64(&g_thermal_req.next_req_id);
    thermal_req_slot_t* s = &g_thermal_req.slots[slot_idx];
    s->in_use = TRUE;
    s->req = req;
    s->dev = dev;
    s->req_id = req_id;
    s->sensor_id = sensor_id;
    s->low_temp_deci_k = low_temp_deci_k;
    s->high_temp_deci_k = high_temp_deci_k;
    s->last_temp_deci_k = 0;
    s->enqueue_tick100ns = now;
    s->expire_tick100ns = expire_at;
    g_thermal_req.active_cnt++;

    WdfSpinLockRelease(g_thermal_req.lock);

    (VOID) InterlockedIncrement64(&g_thermal_req.total_queued);
    thermal_req_record_event(THERMAL_REQ_EVENT_QUEUE_ACCEPTED, STATUS_SUCCESS, req_id, sensor_id, 0,
                             low_temp_deci_k, high_temp_deci_k, dev);

    NTSTATUS st = WdfRequestMarkCancelableEx(req, thermal_req_cancel_fn);
    if (!NT_SUCCESS(st)) {
        WDFDEVICE clear_dev = NULL;
        ULONG clear_sensor = 0;
        ULONG clear_req_id = req_id;

        WdfSpinLockAcquire(g_thermal_req.lock);
        LONG idx = thermal_req_find_slot_by_req_locked(req);
        if (idx >= 0) {
            thermal_req_slot_t* clear = &g_thermal_req.slots[idx];
            clear_dev = clear->dev;
            clear_sensor = clear->sensor_id;
            clear_req_id = clear->req_id;
            thermal_req_reset_slot(clear);
            if (g_thermal_req.active_cnt != 0)
                g_thermal_req.active_cnt--;
        }
        WdfSpinLockRelease(g_thermal_req.lock);

        if (clear_dev)
            tsens_remove_limit(clear_sensor, clear_dev);

        (VOID) InterlockedIncrement64(&g_thermal_req.total_mark_cancelable_failures);
        thermal_req_record_event(THERMAL_REQ_EVENT_QUEUE_MARK_CANCELABLE_FAILED, st, clear_req_id,
                                 clear_sensor ? clear_sensor : sensor_id, 0, low_temp_deci_k,
                                 high_temp_deci_k, clear_dev ? clear_dev : dev);

        if (st == STATUS_CANCELLED)
            return STATUS_PENDING;

        return st;
    }

    (VOID) WdfTimerStart(g_thermal_req.timer, WDF_REL_TIMEOUT_IN_MS(1));
    return STATUS_PENDING;
}

VOID thermal_req_cancel_fn(_In_ WDFREQUEST req)
{
    BOOLEAN found = FALSE;
    ULONG active_left = 0;
    WDFDEVICE clear_dev = NULL;
    ULONG clear_req_id = 0;
    ULONG clear_sensor = 0;
    LONG clear_low = MAXLONG;
    LONG clear_high = MAXLONG;

    if (g_thermal_req.lock) {
        WdfSpinLockAcquire(g_thermal_req.lock);

        LONG idx = thermal_req_find_slot_by_req_locked(req);
        if (idx >= 0) {
            thermal_req_slot_t* s = &g_thermal_req.slots[idx];
            clear_dev = s->dev;
            clear_req_id = s->req_id;
            clear_sensor = s->sensor_id;
            clear_low = s->low_temp_deci_k;
            clear_high = s->high_temp_deci_k;
            thermal_req_reset_slot(s);
            if (g_thermal_req.active_cnt != 0)
                g_thermal_req.active_cnt--;
            found = TRUE;
        }

        active_left = g_thermal_req.active_cnt;
        WdfSpinLockRelease(g_thermal_req.lock);
    }

    if (found && clear_dev)
        tsens_remove_limit(clear_sensor, clear_dev);

    if (active_left == 0 && g_thermal_req.timer)
        (VOID) WdfTimerStop(g_thermal_req.timer, FALSE);

    if (found) {
        (VOID) InterlockedIncrement64(&g_thermal_req.total_cancelled);
        thermal_req_record_event(THERMAL_REQ_EVENT_CANCEL_REQ, STATUS_CANCELLED, clear_req_id,
                                 clear_sensor, 0, clear_low, clear_high, clear_dev);
    }

    WdfRequestComplete(req, STATUS_CANCELLED);
}

VOID thermal_req_timer_fn(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (!g_thermal_req.lock)
        return;

    typedef struct thermal_req_snapshot {
        WDFREQUEST req;
        WDFDEVICE dev;
        ULONG req_id;
        ULONG sensor_id;
        LONG low_temp_deci_k;
        LONG high_temp_deci_k;
        ULONGLONG expire_tick100ns;
    } thermal_req_snapshot_t;

    thermal_req_snapshot_t snapshot[THERMAL_REQ_MAX_SLOTS];
    ULONG snapshot_cnt = 0;

    WdfSpinLockAcquire(g_thermal_req.lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_thermal_req.slots); i++) {
        thermal_req_slot_t* s = &g_thermal_req.slots[i];
        if (!s->in_use || !s->req)
            continue;

        if (snapshot_cnt < RTL_NUMBER_OF(snapshot)) {
            snapshot[snapshot_cnt].req = s->req;
            snapshot[snapshot_cnt].dev = s->dev;
            snapshot[snapshot_cnt].req_id = s->req_id;
            snapshot[snapshot_cnt].sensor_id = s->sensor_id;
            snapshot[snapshot_cnt].low_temp_deci_k = s->low_temp_deci_k;
            snapshot[snapshot_cnt].high_temp_deci_k = s->high_temp_deci_k;
            snapshot[snapshot_cnt].expire_tick100ns = s->expire_tick100ns;
            snapshot_cnt++;
        }
    }
    WdfSpinLockRelease(g_thermal_req.lock);

    for (ULONG i = 0; i < snapshot_cnt; i++) {
        thermal_req_snapshot_t* s = &snapshot[i];
        LONG temp_c = 0;
        ULONG temp_k = thermal_read_temp_deci_k(s->sensor_id, &temp_c);

        mmrm_update_from_thermal_sample(s->low_temp_deci_k, s->high_temp_deci_k, temp_c);

        ULONGLONG now = KeQueryInterruptTime();
        BOOLEAN timeout = (s->expire_tick100ns != 0 && now >= s->expire_tick100ns);
        BOOLEAN hit_low = (s->low_temp_deci_k != MAXLONG && (LONG)temp_k <= s->low_temp_deci_k);
        BOOLEAN hit_high = (s->high_temp_deci_k != MAXLONG && (LONG)temp_k >= s->high_temp_deci_k);
        ULONG complete_event = 0;

        if (!timeout && !hit_low && !hit_high)
            continue;

        if (timeout)
            complete_event = THERMAL_REQ_EVENT_COMPLETE_TIMEOUT;
        else if (hit_low)
            complete_event = THERMAL_REQ_EVENT_COMPLETE_LOW_THRESHOLD;
        else
            complete_event = THERMAL_REQ_EVENT_COMPLETE_HIGH_THRESHOLD;

        WDFDEVICE clear_dev = NULL;
        ULONG clear_sensor = 0;
        ULONG clear_req_id = s->req_id;
        LONG clear_low = s->low_temp_deci_k;
        LONG clear_high = s->high_temp_deci_k;
        BOOLEAN removed = FALSE;

        WdfSpinLockAcquire(g_thermal_req.lock);
        LONG idx = thermal_req_find_slot_by_req_locked(s->req);
        if (idx >= 0) {
            thermal_req_slot_t* clear = &g_thermal_req.slots[idx];
            clear_dev = clear->dev;
            clear_req_id = clear->req_id;
            clear_sensor = clear->sensor_id;
            clear_low = clear->low_temp_deci_k;
            clear_high = clear->high_temp_deci_k;
            thermal_req_reset_slot(clear);
            if (g_thermal_req.active_cnt != 0)
                g_thermal_req.active_cnt--;
            removed = TRUE;
        }
        WdfSpinLockRelease(g_thermal_req.lock);

        if (!removed)
            continue;

        if (clear_dev)
            tsens_remove_limit(clear_sensor, clear_dev);

        NTSTATUS st = WdfRequestUnmarkCancelable(s->req);
        if (st == STATUS_CANCELLED)
            continue;

        if (!NT_SUCCESS(st)) {
            WdfRequestComplete(s->req, st);
            continue;
        }

        thermal_complete_req(s->req, temp_k);
        (VOID) InterlockedIncrement64(&g_thermal_req.total_completed);
        if (complete_event == THERMAL_REQ_EVENT_COMPLETE_TIMEOUT)
            (VOID) InterlockedIncrement64(&g_thermal_req.total_timeout);
        else if (complete_event == THERMAL_REQ_EVENT_COMPLETE_LOW_THRESHOLD)
            (VOID) InterlockedIncrement64(&g_thermal_req.total_low_threshold_hits);
        else if (complete_event == THERMAL_REQ_EVENT_COMPLETE_HIGH_THRESHOLD)
            (VOID) InterlockedIncrement64(&g_thermal_req.total_high_threshold_hits);

        thermal_req_record_event(complete_event, STATUS_SUCCESS, clear_req_id, clear_sensor,
                                 (LONG)temp_k, clear_low, clear_high, clear_dev);
    }

    ULONGLONG now = KeQueryInterruptTime();
    ULONG due_ms = 0;
    BOOLEAN has_pending = FALSE;

    WdfSpinLockAcquire(g_thermal_req.lock);
    has_pending = thermal_req_compute_next_due_locked(now, &due_ms);
    WdfSpinLockRelease(g_thermal_req.lock);

    if (has_pending)
        (VOID) WdfTimerStart(g_thermal_req.timer, WDF_REL_TIMEOUT_IN_MS(due_ms));
}

VOID thermal_req_get_snapshot(_Out_ thermal_req_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    RtlZeroMemory(snapshot, sizeof(*snapshot));

    snapshot->version = 2;
    snapshot->poll_ms = g_thermal_req.poll_ms;
    snapshot->active_cnt = g_thermal_req.active_cnt;
    snapshot->last_req_id = (ULONG)g_thermal_req.next_req_id;
    snapshot->total_queued = (ULONGLONG)g_thermal_req.total_queued;
    snapshot->total_completed = (ULONGLONG)g_thermal_req.total_completed;
    snapshot->total_cancelled = (ULONGLONG)g_thermal_req.total_cancelled;
    snapshot->total_timeout = (ULONGLONG)g_thermal_req.total_timeout;
    snapshot->total_low_threshold_hits = (ULONGLONG)g_thermal_req.total_low_threshold_hits;
    snapshot->total_high_threshold_hits = (ULONGLONG)g_thermal_req.total_high_threshold_hits;
    snapshot->total_busy_rejected = (ULONGLONG)g_thermal_req.total_busy_rejected;
    snapshot->total_no_slot_rejected = (ULONGLONG)g_thermal_req.total_no_slot_rejected;
    snapshot->total_mark_cancelable_failures =
        (ULONGLONG)g_thermal_req.total_mark_cancelable_failures;
    snapshot->event_sequence = (ULONGLONG)g_thermal_req.event_sequence;
    snapshot->last_event_code = g_thermal_req.last_event_code;
    snapshot->last_event_status = g_thermal_req.last_event_status;
    snapshot->last_event_req_id = g_thermal_req.last_event_req_id;
    snapshot->last_event_sensor_id = g_thermal_req.last_event_sensor_id;
    snapshot->last_event_temp_deci_k = g_thermal_req.last_event_temp_deci_k;
    snapshot->last_event_low_temp_deci_k = g_thermal_req.last_event_low_temp_deci_k;
    snapshot->last_event_high_temp_deci_k = g_thermal_req.last_event_high_temp_deci_k;
    snapshot->last_event_tick100ns = g_thermal_req.last_event_tick100ns;

    ULONGLONG seq = snapshot->event_sequence;
    ULONGLONG avail = (seq < THERMAL_REQ_HISTORY_CAPACITY) ? seq : THERMAL_REQ_HISTORY_CAPACITY;
    snapshot->history_cnt = (ULONG)avail;
    snapshot->history_dropped =
        (seq > THERMAL_REQ_HISTORY_CAPACITY) ? (ULONG)(seq - THERMAL_REQ_HISTORY_CAPACITY) : 0u;
}

NTSTATUS thermal_req_get_history(_Out_writes_to_(capacity, *cnt) thermal_req_event_t* entries,
                                 _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!entries || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    ULONGLONG seq = (ULONGLONG)g_thermal_req.event_sequence;
    if (seq == 0)
        return STATUS_SUCCESS;

    ULONGLONG avail = (seq < THERMAL_REQ_HISTORY_CAPACITY) ? seq : THERMAL_REQ_HISTORY_CAPACITY;
    ULONGLONG take = (capacity < avail) ? capacity : avail;
    ULONGLONG skip = avail - take;
    ULONGLONG start_seq = (seq - avail) + skip + 1ull;

    for (ULONGLONG i = 0; i < take; i++) {
        ULONGLONG current_seq = start_seq + i;
        ULONGLONG idx = (current_seq - 1ull) % THERMAL_REQ_HISTORY_CAPACITY;
        entries[i] = g_thermal_req.history[idx];
    }

    *cnt = (ULONG)take;
    *dropped =
        (ULONG)((seq > THERMAL_REQ_HISTORY_CAPACITY) ? (seq - THERMAL_REQ_HISTORY_CAPACITY + skip)
                                                     : skip);
    return STATUS_SUCCESS;
}
