#include "tsens.h"
#include "resource_cache.h"
#include "stats.h"

static volatile LONG g_tsens_init;
static volatile LONG g_tsens_dsm_tried;
static tsens_settings_t g_tsens;
static LONG g_tsens_offsets_deci_c[64];
static LONG g_tsens_cache_deci_c[64];
static UCHAR g_tsens_cache_valid[64];
static ULONG g_tsens_per_sensor_enabled;
static LONG g_tsens_base_offset_deci_c;
static LONG g_tsens_gradient_deci_c;
static volatile LONG g_tsens_dsm_by_sensor_ready;
static volatile LONG g_tsens_dsm_package_ready;
static volatile LONG g_tsens_dsm_refresh_busy;
static GUID g_tsens_dsm_guid;
static ULONG g_tsens_dsm_revision;
static ULONG g_tsens_dsm_func_temp_by_sensor;
static ULONG g_tsens_dsm_func_temp_package;
static ULONG g_tsens_dsm_package_arg_cnt;
static ULONG g_tsens_dsm_package_args[4];
static ULONG g_tsens_dsm_package_refresh_ms;
static ULONG g_tsens_dsm_package_has_cnt_prefix;
static ULONG g_tsens_dsm_package_temp_in_deci_k;
static ULONG g_tsens_dsm_package_aggregate_mode;
static ULONGLONG g_tsens_dsm_last_refresh_tick100ns;
static WDFDEVICE g_tsens_dsm_dev;
static volatile LONG g_tsens_limit_init;
static KSPIN_LOCK g_tsens_limit_lock;

typedef struct tsens_limit_entry {
    BOOLEAN in_use;
    ULONG tsens_id;
    WDFOBJECT owner_obj;
    LONG low_temp;
    LONG high_temp;
    ULONGLONG expire_at_tick100ns;
    ULONG req_id;
} tsens_limit_entry_t;

static tsens_limit_entry_t g_tsens_limits[128];

typedef struct __declspec(align(2)) tsens_stable_read {
    SHORT temperature_degrees_c;
    UCHAR is_stable;
    UCHAR reserved;
} tsens_stable_read_t;

static volatile LONG g_tsens_stable_infra_state;
static WDFSPINLOCK g_tsens_stable_lock;
static WDFTIMER g_tsens_stable_timer;
static WDFREQUEST g_tsens_stable_req;
static tsens_stable_read_t* g_tsens_stable_out;
static ULONG g_tsens_stable_sensor_id;
static ULONG g_tsens_stable_samples_target;
static ULONG g_tsens_stable_samples_collected;
static LONG g_tsens_stable_variation_deci_c;
static LONG g_tsens_stable_min_deci_c;
static LONG g_tsens_stable_max_deci_c;
static LONG g_tsens_stable_latest_deci_c;
static ULONG g_tsens_stable_interval_ms;

static KSPIN_LOCK g_tsens_trace_lock;
static LONG g_tsens_range_min_deci_c[TSENS_MAX_SENSORS];
static LONG g_tsens_range_max_deci_c[TSENS_MAX_SENSORS];
static LONG g_tsens_last_deci_c[TSENS_MAX_SENSORS];
static LONG g_tsens_low_threshold_deci_c[TSENS_MAX_SENSORS];
static LONG g_tsens_high_threshold_deci_c[TSENS_MAX_SENSORS];
static UCHAR g_tsens_low_threshold_enable[TSENS_MAX_SENSORS];
static UCHAR g_tsens_high_threshold_enable[TSENS_MAX_SENSORS];
static UCHAR g_tsens_low_threshold_latched[TSENS_MAX_SENSORS];
static UCHAR g_tsens_high_threshold_latched[TSENS_MAX_SENSORS];
static ULONG g_tsens_low_hit_cnt[TSENS_MAX_SENSORS];
static ULONG g_tsens_high_hit_cnt[TSENS_MAX_SENSORS];
static LONG g_tsens_threshold_hysteresis_deci_c;
static volatile LONG64 g_tsens_read_cnt;
static volatile LONG64 g_tsens_dsm_refresh_cnt;
static volatile LONG64 g_tsens_threshold_hit_cnt;
static volatile LONG64 g_tsens_event_sequence;
static ULONG g_tsens_history_head;
static ULONG g_tsens_history_cnt;
static ULONG g_tsens_history_dropped;
static ULONG g_tsens_last_event_code;
static LONG g_tsens_last_event_status;
static ULONG g_tsens_last_event_sensor_id;
static LONG g_tsens_last_event_temp_deci_c;
static LONG g_tsens_last_event_low_deci_c;
static LONG g_tsens_last_event_high_deci_c;
static ULONGLONG g_tsens_last_event_tick100ns;
static tsens_event_t g_tsens_history[TSENS_HISTORY_CAPACITY];

EVT_WDF_TIMER tsens_stable_timer_fn;
EVT_WDF_REQUEST_CANCEL tsens_stable_cancel_fn;

static USHORT deci_c_to_deci_k(_In_ ULONG temp_deci_c)
{
    return (USHORT)(temp_deci_c + 2730);
}

static VOID tsens_ensure_limit_infra(VOID)
{
    if (InterlockedCompareExchange(&g_tsens_limit_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_tsens_limit_lock);
        RtlZeroMemory(g_tsens_limits, sizeof(g_tsens_limits));
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_tsens_limit_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_tsens_limit_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static LONG tsens_clamp_deci_c(_In_ LONG temp_deci_c)
{
    if (temp_deci_c < -2730)
        return -2730;
    if (temp_deci_c > 20000)
        return 20000;
    return temp_deci_c;
}

static USHORT signed_deci_c_to_deci_k(_In_ LONG temp_deci_c)
{
    LONG t = tsens_clamp_deci_c(temp_deci_c);
    if (t >= 0)
        return deci_c_to_deci_k((ULONG)t);
    return (USHORT)(2730u - (ULONG)(-t));
}

static USHORT signed_deci_c_to_deg_cu16(_In_ LONG temp_deci_c)
{
    LONG t = tsens_clamp_deci_c(temp_deci_c);
    if (t < 0)
        return 0;
    return (USHORT)(t / 10);
}

static VOID tsens_publish_sensor_state(_In_ ULONG sensor_id, _In_ LONG last_temp_deci_c,
                                       _In_ LONG min_temp_deci_c, _In_ LONG max_temp_deci_c,
                                       _In_ LONG low_threshold_deci_c,
                                       _In_ LONG high_threshold_deci_c, _In_ ULONG low_hit_cnt,
                                       _In_ ULONG high_hit_cnt, _In_ ULONG flags)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x31525354u;
    k.sub_type = sensor_id;
    v.value0 = (ULONG)last_temp_deci_c;
    v.value1 = (ULONG)min_temp_deci_c;
    v.value2 = (ULONG)max_temp_deci_c;
    v.value3 = flags;
    (VOID) res_set(k, v);

    k.res_id = 0x32525354u;
    k.sub_type = sensor_id;
    v.value0 = (ULONG)low_threshold_deci_c;
    v.value1 = (ULONG)high_threshold_deci_c;
    v.value2 = low_hit_cnt;
    v.value3 = high_hit_cnt;
    (VOID) res_set(k, v);
}

static VOID tsens_publish_summary(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG sensor_id,
                                  _In_ LONG temp_deci_c, _In_ ULONG history_dropped)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x30525354u;
    k.sub_type = 0;
    v.value0 = (ULONG)g_tsens_read_cnt;
    v.value1 = (ULONG)g_tsens_dsm_refresh_cnt;
    v.value2 = (ULONG)g_tsens_threshold_hit_cnt;
    v.value3 = history_dropped;
    (VOID) res_set(k, v);

    k.res_id = 0x33525354u;
    k.sub_type = event_code;
    v.value0 = (ULONG)status;
    v.value1 = sensor_id;
    v.value2 = (ULONG)temp_deci_c;
    v.value3 = (ULONG)g_tsens_event_sequence;
    (VOID) res_set(k, v);
}

static VOID tsens_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG sensor_id,
                               _In_ LONG temp_deci_c, _In_ LONG low_deci_c, _In_ LONG high_deci_c,
                               _In_ ULONG aux0, _In_ ULONG aux1)
{
    ULONGLONG tick = KeQueryInterruptTime();
    ULONGLONG seq = 0;
    ULONG history_dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);

    seq = (ULONGLONG)InterlockedIncrement64(&g_tsens_event_sequence);
    ULONG idx = g_tsens_history_head % TSENS_HISTORY_CAPACITY;
    tsens_event_t* e = &g_tsens_history[idx];
    e->tick100ns = tick;
    e->sequence = seq;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->sensor_id = sensor_id;
    e->temp_deci_c = temp_deci_c;
    e->low_deci_c = low_deci_c;
    e->high_deci_c = high_deci_c;
    e->aux0 = aux0;
    e->aux1 = aux1;

    g_tsens_history_head = (idx + 1u) % TSENS_HISTORY_CAPACITY;
    if (g_tsens_history_cnt < TSENS_HISTORY_CAPACITY)
        g_tsens_history_cnt++;
    else
        g_tsens_history_dropped++;

    g_tsens_last_event_code = event_code;
    g_tsens_last_event_status = (LONG)status;
    g_tsens_last_event_sensor_id = sensor_id;
    g_tsens_last_event_temp_deci_c = temp_deci_c;
    g_tsens_last_event_low_deci_c = low_deci_c;
    g_tsens_last_event_high_deci_c = high_deci_c;
    g_tsens_last_event_tick100ns = tick;
    history_dropped = g_tsens_history_dropped;

    KeReleaseSpinLock(&g_tsens_trace_lock, irql);

    stats_record_event(0x300u + event_code, status, sensor_id, (ULONG)temp_deci_c, aux0, aux1);
    tsens_publish_summary(event_code, status, sensor_id, temp_deci_c, history_dropped);
}

static VOID tsens_track_sensor_sample(_In_ ULONG sensor_id, _In_ LONG temp_deci_c)
{
    if (sensor_id >= TSENS_MAX_SENSORS)
        return;

    ULONG threshold_event = 0;
    LONG low = MAXLONG;
    LONG high = MAXLONG;
    ULONG flags = 0;
    ULONG low_hits = 0;
    ULONG high_hits = 0;
    LONG min_deci_c = temp_deci_c;
    LONG max_deci_c = temp_deci_c;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);

    if (g_tsens_range_min_deci_c[sensor_id] == MAXLONG ||
        temp_deci_c < g_tsens_range_min_deci_c[sensor_id])
        g_tsens_range_min_deci_c[sensor_id] = temp_deci_c;
    if (g_tsens_range_max_deci_c[sensor_id] == MINLONG ||
        temp_deci_c > g_tsens_range_max_deci_c[sensor_id])
        g_tsens_range_max_deci_c[sensor_id] = temp_deci_c;
    g_tsens_last_deci_c[sensor_id] = temp_deci_c;

    low = g_tsens_low_threshold_deci_c[sensor_id];
    high = g_tsens_high_threshold_deci_c[sensor_id];

    if (g_tsens_low_threshold_enable[sensor_id] && low != MAXLONG) {
        if (!g_tsens_low_threshold_latched[sensor_id] && temp_deci_c <= low) {
            g_tsens_low_threshold_latched[sensor_id] = 1;
            g_tsens_low_hit_cnt[sensor_id]++;
            threshold_event = TSENS_EVENT_THRESHOLD_HIT_LOW;
        } else if (g_tsens_low_threshold_latched[sensor_id] &&
                   temp_deci_c > (low + g_tsens_threshold_hysteresis_deci_c)) {
            g_tsens_low_threshold_latched[sensor_id] = 0;
        }
    }

    if (g_tsens_high_threshold_enable[sensor_id] && high != MAXLONG) {
        if (!g_tsens_high_threshold_latched[sensor_id] && temp_deci_c >= high) {
            g_tsens_high_threshold_latched[sensor_id] = 1;
            g_tsens_high_hit_cnt[sensor_id]++;
            threshold_event = TSENS_EVENT_THRESHOLD_HIT_HIGH;
        } else if (g_tsens_high_threshold_latched[sensor_id] &&
                   temp_deci_c < (high - g_tsens_threshold_hysteresis_deci_c)) {
            g_tsens_high_threshold_latched[sensor_id] = 0;
        }
    }

    min_deci_c = g_tsens_range_min_deci_c[sensor_id];
    max_deci_c = g_tsens_range_max_deci_c[sensor_id];
    low_hits = g_tsens_low_hit_cnt[sensor_id];
    high_hits = g_tsens_high_hit_cnt[sensor_id];
    flags = (g_tsens_low_threshold_enable[sensor_id] ? 1u : 0u) |
            ((g_tsens_high_threshold_enable[sensor_id] ? 1u : 0u) << 1) |
            ((g_tsens_low_threshold_latched[sensor_id] ? 1u : 0u) << 2) |
            ((g_tsens_high_threshold_latched[sensor_id] ? 1u : 0u) << 3);

    KeReleaseSpinLock(&g_tsens_trace_lock, irql);

    tsens_publish_sensor_state(sensor_id, temp_deci_c, min_deci_c, max_deci_c, low, high, low_hits,
                               high_hits, flags);
    if (threshold_event != 0) {
        (VOID) InterlockedIncrement64(&g_tsens_threshold_hit_cnt);
        tsens_record_event(threshold_event, STATUS_SUCCESS, sensor_id, temp_deci_c, low, high,
                           low_hits, high_hits);
    }
}

static VOID tsens_load_sensor_offsets(VOID)
{
    RtlZeroMemory(g_tsens_offsets_deci_c, sizeof(g_tsens_offsets_deci_c));
    g_tsens_per_sensor_enabled = registry_query_u_long(L"TsensPerSensorEnabled", 1);
    g_tsens_base_offset_deci_c = (LONG)registry_query_u_long(L"TsensBaseOffsetDeciC", 0);
    g_tsens_gradient_deci_c = (LONG)registry_query_u_long(L"TsensSensorGradientDeciC", 0);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_tsens_offsets_deci_c); i++) {
        LONG def = g_tsens_base_offset_deci_c + ((LONG)i * g_tsens_gradient_deci_c);
        WCHAR name[64];
        if (NT_SUCCESS(RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"TsensSensor%uOffsetDeciC",
                                           (unsigned int)i)))
            g_tsens_offsets_deci_c[i] = (LONG)registry_query_u_long(name, (ULONG)def);
        else
            g_tsens_offsets_deci_c[i] = def;
    }
}

static ULONG tsens_clamp_dsm_refresh_ms(_In_ ULONG refresh_ms)
{
    if (refresh_ms == 0)
        return 0;
    if (refresh_ms < 50)
        return 50;
    if (refresh_ms > 60000)
        return 60000;
    return refresh_ms;
}

static LONG tsens_normalize_dsm_temp_deci_c(_In_ ULONG raw_value)
{
    LONG value = (LONG)raw_value;
    if (g_tsens_dsm_package_temp_in_deci_k)
        value -= 2730;
    return tsens_clamp_deci_c(value);
}

static LONG tsens_aggregate_dsm_temp_deci_c(_In_reads_(cnt) const LONG* values, _In_ ULONG cnt)
{
    if (!values || cnt == 0)
        return 250;

    switch (g_tsens_dsm_package_aggregate_mode) {
        case 1: {
            LONGLONG sum = 0;
            for (ULONG i = 0; i < cnt; i++)
                sum += values[i];
            return tsens_clamp_deci_c((LONG)(sum / (LONGLONG)cnt));
        }

        case 2: {
            LONG max_temp = values[0];
            for (ULONG i = 1; i < cnt; i++) {
                if (values[i] > max_temp)
                    max_temp = values[i];
            }
            return tsens_clamp_deci_c(max_temp);
        }

        case 0:
        default:
            return tsens_clamp_deci_c(values[0]);
    }
}

static BOOLEAN tsens_apply_dsm_package_temps(_In_reads_(value_cnt) const ULONG* values,
                                             _In_ ULONG value_cnt)
{
    if (!values || value_cnt == 0)
        return FALSE;

    ULONG start = 0;
    ULONG cnt = value_cnt;
    if (g_tsens_dsm_package_has_cnt_prefix && value_cnt > 1) {
        ULONG reported_cnt = values[0];
        ULONG avail = value_cnt - 1;
        if (reported_cnt == 0 || reported_cnt > avail)
            reported_cnt = avail;
        start = 1;
        cnt = reported_cnt;
    }

    if (cnt == 0)
        return FALSE;
    if (cnt > RTL_NUMBER_OF(g_tsens_cache_deci_c))
        cnt = RTL_NUMBER_OF(g_tsens_cache_deci_c);

    LONG normalized_temps[RTL_NUMBER_OF(g_tsens_cache_deci_c)];
    for (ULONG i = 0; i < cnt; i++) {
        LONG temp_deci_c = tsens_normalize_dsm_temp_deci_c(values[start + i]);
        normalized_temps[i] = temp_deci_c;
        g_tsens_cache_deci_c[i] = temp_deci_c;
        g_tsens_cache_valid[i] = 1;
        tsens_track_sensor_sample(i, temp_deci_c);
    }

    for (ULONG i = cnt; i < RTL_NUMBER_OF(g_tsens_cache_valid); i++)
        g_tsens_cache_valid[i] = 0;

    g_tsens.sensor_cnt = cnt;

    LONG aggregate = tsens_aggregate_dsm_temp_deci_c(normalized_temps, cnt);
    if (aggregate < 0)
        aggregate = 0;
    g_tsens.temp_deci_c = (ULONG)aggregate;
    return TRUE;
}

static BOOLEAN tsens_needs_dsm_package_refresh(_In_ BOOLEAN force)
{
    if (force)
        return TRUE;

    ULONG refresh_ms = g_tsens_dsm_package_refresh_ms;
    if (refresh_ms == 0)
        return FALSE;

    ULONGLONG last = g_tsens_dsm_last_refresh_tick100ns;
    if (last == 0)
        return TRUE;

    ULONGLONG now = KeQueryInterruptTime();
    ULONGLONG elapsed = (now > last) ? (now - last) : 0;
    ULONGLONG refresh_ticks = (ULONGLONG)refresh_ms * 10000ULL;
    return (elapsed >= refresh_ticks);
}

static BOOLEAN tsens_refresh_from_dsm_package(_In_ BOOLEAN force)
{
    if (InterlockedCompareExchange(&g_tsens_dsm_package_ready, 0, 0) == 0)
        return FALSE;
    if (!g_tsens_dsm_dev || g_tsens_dsm_func_temp_package == 0)
        return FALSE;
    if (!tsens_needs_dsm_package_refresh(force))
        return TRUE;

    if (InterlockedCompareExchange(&g_tsens_dsm_refresh_busy, 1, 0) != 0)
        return FALSE;

    BOOLEAN success = FALSE;
    ULONG values[RTL_NUMBER_OF(g_tsens_cache_deci_c)] = {0};
    ULONG value_cnt = 0;

    NTSTATUS st = acpi_eval_dsm_package_ulongs(
        g_tsens_dsm_dev, &g_tsens_dsm_guid, g_tsens_dsm_revision, g_tsens_dsm_func_temp_package,
        g_tsens_dsm_package_arg_cnt ? g_tsens_dsm_package_args : NULL, g_tsens_dsm_package_arg_cnt,
        values, RTL_NUMBER_OF(values), &value_cnt);

    if (NT_SUCCESS(st) && value_cnt != 0) {
        success = tsens_apply_dsm_package_temps(values, value_cnt);
        if (success) {
            g_tsens_dsm_last_refresh_tick100ns = KeQueryInterruptTime();
            (VOID) InterlockedIncrement64(&g_tsens_dsm_refresh_cnt);
            tsens_record_event(TSENS_EVENT_DSM_REFRESH, STATUS_SUCCESS, 0,
                               (LONG)g_tsens.temp_deci_c, 0, 0, value_cnt, g_tsens.sensor_cnt);
        }
    }

    InterlockedExchange(&g_tsens_dsm_refresh_busy, 0);
    return success;
}

static LONG tsens_get_sensor_temp_deci_c(_In_ ULONG sensor)
{
    LONG base = (LONG)tsens_get_temp_deci_c();
    LONG result = tsens_clamp_deci_c(base);
    if (sensor >= RTL_NUMBER_OF(g_tsens_offsets_deci_c))
        return result;

    if (InterlockedCompareExchange(&g_tsens_dsm_package_ready, 0, 0) != 0) {
        BOOLEAN force = (g_tsens_cache_valid[sensor] == 0);
        (VOID) tsens_refresh_from_dsm_package(force);
    }

    if (g_tsens_cache_valid[sensor]) {
        result = tsens_clamp_deci_c(g_tsens_cache_deci_c[sensor]);
    } else if (g_tsens_per_sensor_enabled) {
        result = tsens_clamp_deci_c(base + g_tsens_offsets_deci_c[sensor]);
    }

    tsens_track_sensor_sample(sensor, result);
    return result;
}

VOID tsens_add_limit(_In_ ULONG tsens_id, _In_ WDFDEVICE parent_dev)
{
    if (!parent_dev || tsens_id >= RTL_NUMBER_OF(g_tsens_offsets_deci_c))
        return;

    LONG low = MAXLONG;
    LONG high = MAXLONG;
    ULONGLONG expire_at = 0;
    ULONG req_id = 0;
    p_dev_ctx_t dev_ctx = get_dev_ctx(parent_dev);
    if (dev_ctx) {
        low = dev_ctx->thermal_low_temp_deci_k;
        high = dev_ctx->thermal_high_temp_deci_k;
        expire_at = dev_ctx->thermal_limit_expire_tick100ns;
        req_id = dev_ctx->thermal_req_id;
    }

    tsens_ensure_limit_infra();

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_limit_lock, &irql);

    LONG free_idx = -1;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_tsens_limits); i++) {
        if (g_tsens_limits[i].in_use) {
            if (g_tsens_limits[i].tsens_id == tsens_id &&
                g_tsens_limits[i].owner_obj == (WDFOBJECT)parent_dev) {
                g_tsens_limits[i].low_temp = low;
                g_tsens_limits[i].high_temp = high;
                g_tsens_limits[i].expire_at_tick100ns = expire_at;
                g_tsens_limits[i].req_id = req_id;
                KeReleaseSpinLock(&g_tsens_limit_lock, irql);
                return;
            }
        } else if (free_idx < 0) {
            free_idx = (LONG)i;
        }
    }

    if (free_idx >= 0) {
        tsens_limit_entry_t* e = &g_tsens_limits[free_idx];
        e->in_use = TRUE;
        e->tsens_id = tsens_id;
        e->owner_obj = (WDFOBJECT)parent_dev;
        e->low_temp = low;
        e->high_temp = high;
        e->expire_at_tick100ns = expire_at;
        e->req_id = req_id;
    }

    KeReleaseSpinLock(&g_tsens_limit_lock, irql);
}

VOID tsens_remove_limit(_In_ ULONG tsens_id, _In_ WDFDEVICE parent_dev)
{
    if (!parent_dev || tsens_id >= RTL_NUMBER_OF(g_tsens_offsets_deci_c))
        return;

    tsens_ensure_limit_infra();

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_limit_lock, &irql);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_tsens_limits); i++) {
        tsens_limit_entry_t* e = &g_tsens_limits[i];
        if (!e->in_use)
            continue;
        if (e->tsens_id == tsens_id && e->owner_obj == (WDFOBJECT)parent_dev) {
            e->in_use = FALSE;
            e->owner_obj = NULL;
            e->tsens_id = 0;
            e->low_temp = MAXLONG;
            e->high_temp = MAXLONG;
            e->expire_at_tick100ns = 0;
            e->req_id = 0;
        }
    }

    KeReleaseSpinLock(&g_tsens_limit_lock, irql);
}

VOID tsens_bracket(_In_ ULONG tsens_id, _In_ ptsens_threshold_data_t thresholds)
{
    if (!thresholds || tsens_id >= RTL_NUMBER_OF(g_tsens_offsets_deci_c))
        return;

    tsens_ensure_limit_infra();

    LONG low = thresholds->low_temp;
    LONG high = thresholds->high_temp;
    WDFOBJECT high_owner = thresholds->high_temp_owner_obj;
    ULONGLONG now = KeQueryInterruptTime();

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_limit_lock, &irql);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_tsens_limits); i++) {
        tsens_limit_entry_t* e = &g_tsens_limits[i];
        if (!e->in_use || e->tsens_id != tsens_id)
            continue;

        if (e->expire_at_tick100ns != 0 && now >= e->expire_at_tick100ns) {
            e->low_temp = MAXLONG;
            e->high_temp = MAXLONG;
            e->expire_at_tick100ns = 0;
        }

        if (e->low_temp != MAXLONG && low < e->low_temp)
            low = e->low_temp;

        if (e->high_temp != MAXLONG && high > e->high_temp) {
            high = e->high_temp;
            high_owner = e->owner_obj;
        }
    }

    KeReleaseSpinLock(&g_tsens_limit_lock, irql);

    thresholds->low_temp = low;
    thresholds->high_temp = high;
    thresholds->high_temp_owner_obj = high_owner;
    thresholds->tsens_temp10_c = tsens_get_sensor_temp_deci_c(tsens_id);
    (VOID) tsens_set_threshold_window(tsens_id, low, high, (low != MAXLONG), (high != MAXLONG));
}

static NTSTATUS tsens_ensure_stable_infra(_In_ WDFDEVICE dev)
{
    for (;;) {
        LONG state = InterlockedCompareExchange(&g_tsens_stable_infra_state, 0, 0);
        if (state == 2)
            return STATUS_SUCCESS;
        if (state == 1) {
            KeStallExecutionProcessor(50);
            continue;
        }
        if (InterlockedCompareExchange(&g_tsens_stable_infra_state, 1, 0) == 0)
            break;
    }

    g_tsens_stable_req = NULL;
    g_tsens_stable_out = NULL;
    g_tsens_stable_timer = NULL;
    g_tsens_stable_lock = NULL;
    g_tsens_stable_sensor_id = 0;
    g_tsens_stable_samples_target = 0;
    g_tsens_stable_samples_collected = 0;
    g_tsens_stable_variation_deci_c = 0;
    g_tsens_stable_min_deci_c = 0;
    g_tsens_stable_max_deci_c = 0;
    g_tsens_stable_latest_deci_c = 0;
    g_tsens_stable_interval_ms = 0;

    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = dev;

    NTSTATUS status = WdfSpinLockCreate(&attrs, &g_tsens_stable_lock);
    if (!NT_SUCCESS(status))
        goto fail;

    WDF_TIMER_CONFIG cfg;
    WDF_TIMER_CONFIG_INIT(&cfg, tsens_stable_timer_fn);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = dev;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(&cfg, &attrs, &g_tsens_stable_timer);
    if (!NT_SUCCESS(status))
        goto fail;

    InterlockedExchange(&g_tsens_stable_infra_state, 2);
    return STATUS_SUCCESS;

fail:
    if (g_tsens_stable_timer) {
        WdfObjectDelete(g_tsens_stable_timer);
        g_tsens_stable_timer = NULL;
    }
    if (g_tsens_stable_lock) {
        WdfObjectDelete(g_tsens_stable_lock);
        g_tsens_stable_lock = NULL;
    }
    InterlockedExchange(&g_tsens_stable_infra_state, 0);
    return status;
}

VOID tsens_stable_cancel_fn(_In_ WDFREQUEST req)
{
    BOOLEAN complete = FALSE;

    if (g_tsens_stable_lock) {
        WdfSpinLockAcquire(g_tsens_stable_lock);
        if (g_tsens_stable_req == req) {
            g_tsens_stable_req = NULL;
            g_tsens_stable_out = NULL;
            g_tsens_stable_samples_target = 0;
            g_tsens_stable_samples_collected = 0;
            complete = TRUE;
        }
        WdfSpinLockRelease(g_tsens_stable_lock);
    }

    if (g_tsens_stable_timer)
        (VOID) WdfTimerStop(g_tsens_stable_timer, FALSE);

    if (complete)
        WdfRequestComplete(req, STATUS_CANCELLED);
}

VOID tsens_stable_timer_fn(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (!g_tsens_stable_lock)
        return;

    WDFREQUEST req = NULL;
    tsens_stable_read_t* out = NULL;
    ULONG sensor_id = 0;
    ULONG samples_target = 0;
    ULONG samples_collected = 0;
    LONG variation = 0;

    WdfSpinLockAcquire(g_tsens_stable_lock);
    req = g_tsens_stable_req;
    out = g_tsens_stable_out;
    sensor_id = g_tsens_stable_sensor_id;
    samples_target = g_tsens_stable_samples_target;
    samples_collected = g_tsens_stable_samples_collected;
    variation = g_tsens_stable_variation_deci_c;
    WdfSpinLockRelease(g_tsens_stable_lock);

    if (!req)
        return;

    LONG sample_deci_c = tsens_get_sensor_temp_deci_c(sensor_id);
    BOOLEAN done = FALSE;
    BOOLEAN stable = FALSE;
    LONG latest_deci_c = sample_deci_c;

    WdfSpinLockAcquire(g_tsens_stable_lock);
    if (g_tsens_stable_req != req) {
        WdfSpinLockRelease(g_tsens_stable_lock);
        return;
    }

    if (g_tsens_stable_samples_collected == 0) {
        g_tsens_stable_min_deci_c = sample_deci_c;
        g_tsens_stable_max_deci_c = sample_deci_c;
    } else {
        if (sample_deci_c < g_tsens_stable_min_deci_c)
            g_tsens_stable_min_deci_c = sample_deci_c;
        if (sample_deci_c > g_tsens_stable_max_deci_c)
            g_tsens_stable_max_deci_c = sample_deci_c;
    }

    g_tsens_stable_latest_deci_c = sample_deci_c;
    g_tsens_stable_samples_collected++;
    samples_collected = g_tsens_stable_samples_collected;
    latest_deci_c = g_tsens_stable_latest_deci_c;

    if (samples_target == 0)
        samples_target = 1;

    if (samples_collected >= samples_target) {
        LONG span = g_tsens_stable_max_deci_c - g_tsens_stable_min_deci_c;
        if (span < 0)
            span = -span;
        stable = (span <= variation);
        done = TRUE;
        g_tsens_stable_req = NULL;
        g_tsens_stable_out = NULL;
        g_tsens_stable_samples_target = 0;
        g_tsens_stable_samples_collected = 0;
    }
    WdfSpinLockRelease(g_tsens_stable_lock);

    if (!done) {
        ULONG interval_ms = g_tsens_stable_interval_ms ? g_tsens_stable_interval_ms : 100;
        (VOID) WdfTimerStart(g_tsens_stable_timer, WDF_REL_TIMEOUT_IN_MS(interval_ms));
        return;
    }

    NTSTATUS status = WdfRequestUnmarkCancelable(req);
    if (status == STATUS_CANCELLED)
        return;
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(req, status);
        return;
    }

    if (out) {
        out->temperature_degrees_c = (SHORT)(latest_deci_c / 10);
        out->is_stable = stable ? 1 : 0;
        out->reserved = 0;
    }

    (VOID) InterlockedIncrement64(&g_tsens_read_cnt);
    tsens_record_event(TSENS_EVENT_STABLE_COMPLETE, STATUS_SUCCESS, sensor_id, latest_deci_c,
                       g_tsens_stable_min_deci_c, g_tsens_stable_max_deci_c, samples_collected,
                       stable ? 1u : 0u);

    WdfRequestCompleteWithInformation(req, STATUS_SUCCESS, sizeof(tsens_stable_read_t));
}

static NTSTATUS try_tsens_dsm(_In_ WDFDEVICE dev, _Inout_ tsens_settings_t* out)
{
    UNICODE_STRING guid_str = {0};
    NTSTATUS st = registry_query_unicode_string_alloc(L"TsensDsmGuid", &guid_str, 64);
    if (!NT_SUCCESS(st))
        return STATUS_NOT_FOUND;

    GUID g;
    st = RtlGUIDFromString(&guid_str, &g);
    ExFreePool(guid_str.Buffer);
    if (!NT_SUCCESS(st))
        return STATUS_NOT_FOUND;

    ULONG rev = registry_query_u_long(L"TsensDsmRevision", 0);
    ULONG f_cnt = registry_query_u_long(L"TsensDsmFuncSensorCount", 0);
    ULONG f_temp = registry_query_u_long(L"TsensDsmFuncTempDeciC", 0);
    ULONG f_temp_by_sensor = registry_query_u_long(L"TsensDsmFuncTempDeciCBySensor", 0);
    ULONG f_temp_package = registry_query_u_long(L"TsensDsmFuncTempDeciCPackage", 0);

    if (f_temp_by_sensor != 0 || f_temp_package != 0) {
        g_tsens_dsm_guid = g;
        g_tsens_dsm_revision = rev;
    }

    if (f_temp_by_sensor != 0) {
        g_tsens_dsm_func_temp_by_sensor = f_temp_by_sensor;
        InterlockedExchange(&g_tsens_dsm_by_sensor_ready, 1);
    }

    if (f_temp_package != 0) {
        g_tsens_dsm_dev = dev;
        g_tsens_dsm_func_temp_package = f_temp_package;
        g_tsens_dsm_package_arg_cnt =
            registry_query_u_long(L"TsensDsmFuncTempDeciCPackageArgCount", 0);
        if (g_tsens_dsm_package_arg_cnt > RTL_NUMBER_OF(g_tsens_dsm_package_args))
            g_tsens_dsm_package_arg_cnt = RTL_NUMBER_OF(g_tsens_dsm_package_args);

        RtlZeroMemory(g_tsens_dsm_package_args, sizeof(g_tsens_dsm_package_args));
        for (ULONG i = 0; i < g_tsens_dsm_package_arg_cnt; i++) {
            WCHAR name[72];
            if (NT_SUCCESS(RtlStringCchPrintfW(name, RTL_NUMBER_OF(name),
                                               L"TsensDsmFuncTempDeciCPackageArg%u",
                                               (unsigned int)i))) {
                g_tsens_dsm_package_args[i] = registry_query_u_long(name, 0);
            }
        }

        g_tsens_dsm_package_refresh_ms =
            tsens_clamp_dsm_refresh_ms(registry_query_u_long(L"TsensDsmPackageRefreshMs", 1000));
        g_tsens_dsm_package_has_cnt_prefix =
            registry_query_u_long(L"TsensDsmPackageHasCountPrefix", 0) ? 1u : 0u;
        g_tsens_dsm_package_temp_in_deci_k =
            registry_query_u_long(L"TsensDsmPackageTempInDeciK", 0) ? 1u : 0u;
        g_tsens_dsm_package_aggregate_mode =
            registry_query_u_long(L"TsensDsmPackageAggregateMode", 2);
        if (g_tsens_dsm_package_aggregate_mode > 2)
            g_tsens_dsm_package_aggregate_mode = 2;

        g_tsens_dsm_last_refresh_tick100ns = 0;
        InterlockedExchange(&g_tsens_dsm_package_ready, 1);
    }

    ULONG v = 0;
    if (f_cnt) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_cnt, &v)))
            out->sensor_cnt = v;
    }
    if (f_temp) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_temp, &v)))
            out->temp_deci_c = v;
    }

    return STATUS_SUCCESS;
}

NTSTATUS tsens_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);

    if (InterlockedCompareExchange(&g_tsens_init, 1, 0) != 0)
        return STATUS_SUCCESS;

    KeInitializeSpinLock(&g_tsens_trace_lock);
    RtlZeroMemory(&g_tsens, sizeof(g_tsens));
    RtlZeroMemory(g_tsens_cache_deci_c, sizeof(g_tsens_cache_deci_c));
    RtlZeroMemory(g_tsens_cache_valid, sizeof(g_tsens_cache_valid));
    RtlZeroMemory(g_tsens_history, sizeof(g_tsens_history));
    RtlZeroMemory(g_tsens_last_deci_c, sizeof(g_tsens_last_deci_c));
    RtlZeroMemory(g_tsens_low_hit_cnt, sizeof(g_tsens_low_hit_cnt));
    RtlZeroMemory(g_tsens_high_hit_cnt, sizeof(g_tsens_high_hit_cnt));
    RtlZeroMemory(g_tsens_low_threshold_enable, sizeof(g_tsens_low_threshold_enable));
    RtlZeroMemory(g_tsens_high_threshold_enable, sizeof(g_tsens_high_threshold_enable));
    RtlZeroMemory(g_tsens_low_threshold_latched, sizeof(g_tsens_low_threshold_latched));
    RtlZeroMemory(g_tsens_high_threshold_latched, sizeof(g_tsens_high_threshold_latched));
    g_tsens_threshold_hysteresis_deci_c =
        (LONG)registry_query_u_long(L"TsensThresholdHysteresisDeciC", 5);
    if (g_tsens_threshold_hysteresis_deci_c < 0)
        g_tsens_threshold_hysteresis_deci_c = -g_tsens_threshold_hysteresis_deci_c;

    for (ULONG i = 0; i < TSENS_MAX_SENSORS; i++) {
        g_tsens_range_min_deci_c[i] = MAXLONG;
        g_tsens_range_max_deci_c[i] = MINLONG;
        g_tsens_low_threshold_deci_c[i] = MAXLONG;
        g_tsens_high_threshold_deci_c[i] = MAXLONG;
    }

    g_tsens_read_cnt = 0;
    g_tsens_dsm_refresh_cnt = 0;
    g_tsens_threshold_hit_cnt = 0;
    g_tsens_event_sequence = 0;
    g_tsens_history_head = 0;
    g_tsens_history_cnt = 0;
    g_tsens_history_dropped = 0;
    g_tsens_last_event_code = 0;
    g_tsens_last_event_status = 0;
    g_tsens_last_event_sensor_id = 0;
    g_tsens_last_event_temp_deci_c = 0;
    g_tsens_last_event_low_deci_c = MAXLONG;
    g_tsens_last_event_high_deci_c = MAXLONG;
    g_tsens_last_event_tick100ns = 0;

    InterlockedExchange(&g_tsens_dsm_by_sensor_ready, 0);
    InterlockedExchange(&g_tsens_dsm_package_ready, 0);
    InterlockedExchange(&g_tsens_dsm_refresh_busy, 0);
    g_tsens_dsm_revision = 0;
    g_tsens_dsm_func_temp_by_sensor = 0;
    g_tsens_dsm_func_temp_package = 0;
    g_tsens_dsm_package_arg_cnt = 0;
    g_tsens_dsm_package_refresh_ms = 0;
    g_tsens_dsm_package_has_cnt_prefix = 0;
    g_tsens_dsm_package_temp_in_deci_k = 0;
    g_tsens_dsm_package_aggregate_mode = 0;
    g_tsens_dsm_last_refresh_tick100ns = 0;
    g_tsens_dsm_dev = NULL;
    RtlZeroMemory(g_tsens_dsm_package_args, sizeof(g_tsens_dsm_package_args));
    RtlZeroMemory(&g_tsens_dsm_guid, sizeof(g_tsens_dsm_guid));

    g_tsens.sensor_cnt = registry_query_u_long(L"TsensSensorCount", 24);
    g_tsens.temp_deci_c = registry_query_u_long(L"TsensTempDeciC", 250);
    tsens_load_sensor_offsets();
    tsens_ensure_limit_infra();

    if (g_tsens.sensor_cnt == 0)
        g_tsens.sensor_cnt = 24;
    if (g_tsens.sensor_cnt > 64)
        g_tsens.sensor_cnt = 64;

    tsens_record_event(TSENS_EVENT_INIT, STATUS_SUCCESS, 0, (LONG)g_tsens.temp_deci_c, 0, 0,
                       g_tsens.sensor_cnt, 0);

    return STATUS_SUCCESS;
}

NTSTATUS tsens_init_from_dev(_In_ WDFDEVICE dev)
{

    if (InterlockedCompareExchange(&g_tsens_dsm_tried, 1, 0) != 0)
        return STATUS_SUCCESS;

    tsens_settings_t tmp;
    RtlZeroMemory(&tmp, sizeof(tmp));
    (VOID) try_tsens_dsm(dev, &tmp);

    if (tmp.sensor_cnt != 0)
        g_tsens.sensor_cnt = tmp.sensor_cnt;
    if (tmp.temp_deci_c != 0)
        g_tsens.temp_deci_c = tmp.temp_deci_c;

    if (g_tsens.sensor_cnt == 0)
        g_tsens.sensor_cnt = 24;
    if (g_tsens.sensor_cnt > 64)
        g_tsens.sensor_cnt = 64;
    if (g_tsens.temp_deci_c == 0)
        g_tsens.temp_deci_c = 250;

    if (InterlockedCompareExchange(&g_tsens_dsm_package_ready, 0, 0) != 0)
        (VOID) tsens_refresh_from_dsm_package(TRUE);

    if (InterlockedCompareExchange(&g_tsens_dsm_by_sensor_ready, 0, 0) != 0 &&
        g_tsens_dsm_func_temp_by_sensor != 0) {
        ULONG cnt = g_tsens.sensor_cnt;
        if (cnt > RTL_NUMBER_OF(g_tsens_cache_valid))
            cnt = RTL_NUMBER_OF(g_tsens_cache_valid);

        for (ULONG i = 0; i < cnt; i++) {
            if (g_tsens_cache_valid[i])
                continue;

            ULONG v = 0;
            if (NT_SUCCESS(
                    acpi_eval_dsm_ulong_with_arg_ulong(dev, &g_tsens_dsm_guid, g_tsens_dsm_revision,
                                                       g_tsens_dsm_func_temp_by_sensor, i, &v))) {
                g_tsens_cache_deci_c[i] = (LONG)v;
                g_tsens_cache_valid[i] = 1;
            }
        }
    }

    return STATUS_SUCCESS;
}

VOID tsens_deinit(VOID)
{
    WDFREQUEST req = NULL;

    if (g_tsens_stable_lock) {
        WdfSpinLockAcquire(g_tsens_stable_lock);
        req = g_tsens_stable_req;
        g_tsens_stable_req = NULL;
        g_tsens_stable_out = NULL;
        WdfSpinLockRelease(g_tsens_stable_lock);
    }

    if (req) {
        NTSTATUS unmark = WdfRequestUnmarkCancelable(req);
        if (unmark != STATUS_CANCELLED)
            WdfRequestComplete(req, STATUS_CANCELLED);
    }

    if (g_tsens_stable_timer) {
        (VOID) WdfTimerStop(g_tsens_stable_timer, FALSE);
        WdfObjectDelete(g_tsens_stable_timer);
        g_tsens_stable_timer = NULL;
    }

    if (g_tsens_stable_lock) {
        WdfObjectDelete(g_tsens_stable_lock);
        g_tsens_stable_lock = NULL;
    }

    InterlockedExchange(&g_tsens_stable_infra_state, 0);
    InterlockedExchange(&g_tsens_dsm_tried, 0);
    InterlockedExchange(&g_tsens_dsm_by_sensor_ready, 0);
    InterlockedExchange(&g_tsens_dsm_package_ready, 0);
    InterlockedExchange(&g_tsens_dsm_refresh_busy, 0);
    g_tsens_dsm_revision = 0;
    g_tsens_dsm_func_temp_by_sensor = 0;
    g_tsens_dsm_func_temp_package = 0;
    g_tsens_dsm_package_arg_cnt = 0;
    g_tsens_dsm_package_refresh_ms = 0;
    g_tsens_dsm_package_has_cnt_prefix = 0;
    g_tsens_dsm_package_temp_in_deci_k = 0;
    g_tsens_dsm_package_aggregate_mode = 0;
    g_tsens_dsm_last_refresh_tick100ns = 0;
    g_tsens_dsm_dev = NULL;
    g_tsens_per_sensor_enabled = 0;
    g_tsens_base_offset_deci_c = 0;
    g_tsens_gradient_deci_c = 0;
    g_tsens_stable_sensor_id = 0;
    g_tsens_stable_samples_target = 0;
    g_tsens_stable_samples_collected = 0;
    g_tsens_stable_variation_deci_c = 0;
    g_tsens_stable_min_deci_c = 0;
    g_tsens_stable_max_deci_c = 0;
    g_tsens_stable_latest_deci_c = 0;
    g_tsens_stable_interval_ms = 0;
    g_tsens_threshold_hysteresis_deci_c = 0;
    g_tsens_read_cnt = 0;
    g_tsens_dsm_refresh_cnt = 0;
    g_tsens_threshold_hit_cnt = 0;
    g_tsens_event_sequence = 0;
    g_tsens_history_head = 0;
    g_tsens_history_cnt = 0;
    g_tsens_history_dropped = 0;
    g_tsens_last_event_code = 0;
    g_tsens_last_event_status = 0;
    g_tsens_last_event_sensor_id = 0;
    g_tsens_last_event_temp_deci_c = 0;
    g_tsens_last_event_low_deci_c = 0;
    g_tsens_last_event_high_deci_c = 0;
    g_tsens_last_event_tick100ns = 0;
    InterlockedExchange(&g_tsens_limit_init, 0);
    RtlZeroMemory(&g_tsens_dsm_guid, sizeof(g_tsens_dsm_guid));
    RtlZeroMemory(g_tsens_dsm_package_args, sizeof(g_tsens_dsm_package_args));
    RtlZeroMemory(g_tsens_offsets_deci_c, sizeof(g_tsens_offsets_deci_c));
    RtlZeroMemory(g_tsens_cache_deci_c, sizeof(g_tsens_cache_deci_c));
    RtlZeroMemory(g_tsens_cache_valid, sizeof(g_tsens_cache_valid));
    RtlZeroMemory(g_tsens_history, sizeof(g_tsens_history));
    RtlZeroMemory(g_tsens_range_min_deci_c, sizeof(g_tsens_range_min_deci_c));
    RtlZeroMemory(g_tsens_range_max_deci_c, sizeof(g_tsens_range_max_deci_c));
    RtlZeroMemory(g_tsens_last_deci_c, sizeof(g_tsens_last_deci_c));
    RtlZeroMemory(g_tsens_low_threshold_deci_c, sizeof(g_tsens_low_threshold_deci_c));
    RtlZeroMemory(g_tsens_high_threshold_deci_c, sizeof(g_tsens_high_threshold_deci_c));
    RtlZeroMemory(g_tsens_low_threshold_enable, sizeof(g_tsens_low_threshold_enable));
    RtlZeroMemory(g_tsens_high_threshold_enable, sizeof(g_tsens_high_threshold_enable));
    RtlZeroMemory(g_tsens_low_threshold_latched, sizeof(g_tsens_low_threshold_latched));
    RtlZeroMemory(g_tsens_high_threshold_latched, sizeof(g_tsens_high_threshold_latched));
    RtlZeroMemory(g_tsens_low_hit_cnt, sizeof(g_tsens_low_hit_cnt));
    RtlZeroMemory(g_tsens_high_hit_cnt, sizeof(g_tsens_high_hit_cnt));
    RtlZeroMemory(g_tsens_limits, sizeof(g_tsens_limits));
    RtlZeroMemory(&g_tsens, sizeof(g_tsens));
    InterlockedExchange(&g_tsens_init, 0);
}

ULONG tsens_get_cnt(VOID)
{
    return g_tsens.sensor_cnt ? g_tsens.sensor_cnt : 24;
}

ULONG tsens_get_temp_deci_c(VOID)
{
    return g_tsens.temp_deci_c ? g_tsens.temp_deci_c : 250;
}

NTSTATUS tsens_process_stable_req(_In_ WDFREQUEST req, _In_ WDFDEVICE dev,
                                  _Out_opt_ size_t* information)
{
    if (information)
        *information = 0;

    NTSTATUS status = tsens_ensure_stable_infra(dev);
    if (!NT_SUCCESS(status))
        return status;

    tsens_stable_read_t* out = NULL;
    size_t out_sz = 0;
    status =
        WdfRequestRetrieveOutputBuffer(req, sizeof(tsens_stable_read_t), (PVOID*)&out, &out_sz);
    if (!NT_SUCCESS(status)) {
        if (information && status == STATUS_BUFFER_TOO_SMALL)
            *information = sizeof(tsens_stable_read_t);
        return status;
    }
    if (!out || out_sz < sizeof(tsens_stable_read_t))
        return STATUS_ACCESS_VIOLATION;

    ULONG sample_cnt = registry_query_u_long(L"StableTsensMeasurementCount", 5);
    if (sample_cnt == 0)
        sample_cnt = 1;
    if (sample_cnt > 64)
        sample_cnt = 64;

    ULONG interval_ms = registry_query_u_long(L"StableTsensIntervalMs", 100);
    if (interval_ms == 0)
        interval_ms = 1;
    if (interval_ms > 5000)
        interval_ms = 5000;

    LONG variation_deci_c = (LONG)registry_query_u_long(L"StableTsensVariationDeciC", 20);
    if (variation_deci_c < 0)
        variation_deci_c = -variation_deci_c;

    ULONG sensor_id = registry_query_u_long(L"StableTsensSensorIndex", 0);
    if (sensor_id >= tsens_get_cnt())
        sensor_id = 0;

    WdfSpinLockAcquire(g_tsens_stable_lock);
    if (g_tsens_stable_req != NULL) {
        WdfSpinLockRelease(g_tsens_stable_lock);
        return STATUS_CANCELLED;
    }
    g_tsens_stable_req = req;
    g_tsens_stable_out = out;
    g_tsens_stable_sensor_id = sensor_id;
    g_tsens_stable_samples_target = sample_cnt;
    g_tsens_stable_samples_collected = 0;
    g_tsens_stable_variation_deci_c = variation_deci_c;
    g_tsens_stable_min_deci_c = 0;
    g_tsens_stable_max_deci_c = 0;
    g_tsens_stable_latest_deci_c = 0;
    g_tsens_stable_interval_ms = interval_ms;
    WdfSpinLockRelease(g_tsens_stable_lock);

    status = WdfRequestMarkCancelableEx(req, tsens_stable_cancel_fn);
    if (!NT_SUCCESS(status)) {
        WdfSpinLockAcquire(g_tsens_stable_lock);
        if (g_tsens_stable_req == req) {
            g_tsens_stable_req = NULL;
            g_tsens_stable_out = NULL;
            g_tsens_stable_samples_target = 0;
            g_tsens_stable_samples_collected = 0;
        }
        WdfSpinLockRelease(g_tsens_stable_lock);
        return status;
    }

    (VOID) WdfTimerStart(g_tsens_stable_timer, WDF_REL_TIMEOUT_IN_MS(1));

    return STATUS_PENDING;
}

NTSTATUS tsens_get_temperatures(_Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                                _Out_ size_t* written)
{
    if (!out || !written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (InterlockedCompareExchange(&g_tsens_dsm_package_ready, 0, 0) != 0)
        (VOID) tsens_refresh_from_dsm_package(FALSE);

    ULONG cnt = tsens_get_cnt();
    size_t need = sizeof(USHORT) + (size_t)cnt * sizeof(USHORT);
    if (out_bytes < need) {
        *written = need;
        return STATUS_BUFFER_TOO_SMALL;
    }

    USHORT* w = (USHORT*)out;
    w[0] = (USHORT)cnt;

    for (ULONG i = 0; i < cnt; i++) {
        LONG deci_c = tsens_get_sensor_temp_deci_c(i);
        w[1 + i] = signed_deci_c_to_deg_cu16(deci_c);
        (VOID) InterlockedIncrement64(&g_tsens_read_cnt);
    }

    tsens_record_event(TSENS_EVENT_READ_BULK, STATUS_SUCCESS, cnt, (LONG)g_tsens.temp_deci_c, 0, 0,
                       cnt, (ULONG)need);

    *written = need;
    return STATUS_SUCCESS;
}

NTSTATUS tsens_get_temp_for_idx(_In_reads_bytes_(in_bytes) PVOID in, _In_ size_t in_bytes,
                                _Out_writes_bytes_(out_bytes) PVOID out, _In_ size_t out_bytes,
                                _Out_ size_t* written)
{
    if (!in || !out || !written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (in_bytes < sizeof(USHORT))
        return STATUS_BUFFER_TOO_SMALL;

    SHORT idx = *(SHORT*)in;

    if (out_bytes < sizeof(USHORT)) {
        *written = sizeof(USHORT);
        return STATUS_BUFFER_TOO_SMALL;
    }

    USHORT t = 0;
    LONG temp_deci_c = 0;
    NTSTATUS event_status = STATUS_SUCCESS;
    if (idx >= 0 && (ULONG)idx < tsens_get_cnt()) {
        if (InterlockedCompareExchange(&g_tsens_dsm_package_ready, 0, 0) != 0)
            (VOID) tsens_refresh_from_dsm_package(g_tsens_cache_valid[(ULONG)idx] == 0);
        temp_deci_c = tsens_get_sensor_temp_deci_c((ULONG)idx);
        t = signed_deci_c_to_deci_k(temp_deci_c);
        (VOID) InterlockedIncrement64(&g_tsens_read_cnt);
    } else {
        event_status = STATUS_INVALID_PARAMETER;
    }

    *(USHORT*)out = t;
    *written = sizeof(USHORT);
    tsens_record_event(TSENS_EVENT_READ_IDX, event_status, (idx >= 0) ? (ULONG)idx : MAXULONG,
                       temp_deci_c, 0, 0, (ULONG)t, 0);
    return STATUS_SUCCESS;
}

NTSTATUS tsens_get_temp_range(_In_ ULONG sensor_id, _Out_ LONG* min_temp_deci_c,
                              _Out_ LONG* max_temp_deci_c)
{
    if (!min_temp_deci_c || !max_temp_deci_c)
        return STATUS_INVALID_PARAMETER;
    if (sensor_id >= tsens_get_cnt() || sensor_id >= TSENS_MAX_SENSORS)
        return STATUS_INVALID_PARAMETER;

    LONG min_deci_c;
    LONG max_deci_c;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);
    min_deci_c = g_tsens_range_min_deci_c[sensor_id];
    max_deci_c = g_tsens_range_max_deci_c[sensor_id];
    KeReleaseSpinLock(&g_tsens_trace_lock, irql);

    if (min_deci_c == MAXLONG || max_deci_c == MINLONG) {
        LONG current = tsens_get_sensor_temp_deci_c(sensor_id);
        min_deci_c = current;
        max_deci_c = current;
    }

    *min_temp_deci_c = min_deci_c;
    *max_temp_deci_c = max_deci_c;
    return STATUS_SUCCESS;
}

NTSTATUS tsens_set_threshold_window(_In_ ULONG sensor_id, _In_ LONG low_threshold_deci_c,
                                    _In_ LONG high_threshold_deci_c, _In_ BOOLEAN enable_low,
                                    _In_ BOOLEAN enable_high)
{
    if (sensor_id >= tsens_get_cnt() || sensor_id >= TSENS_MAX_SENSORS)
        return STATUS_INVALID_PARAMETER;

    LONG low = enable_low ? tsens_clamp_deci_c(low_threshold_deci_c) : MAXLONG;
    LONG high = enable_high ? tsens_clamp_deci_c(high_threshold_deci_c) : MAXLONG;
    if (enable_low && enable_high && low > high) {
        LONG t = low;
        low = high;
        high = t;
    }

    LONG last_deci_c = 0;
    LONG min_deci_c = 0;
    LONG max_deci_c = 0;
    ULONG low_hits = 0;
    ULONG high_hits = 0;
    ULONG flags = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);
    g_tsens_low_threshold_deci_c[sensor_id] = low;
    g_tsens_high_threshold_deci_c[sensor_id] = high;
    g_tsens_low_threshold_enable[sensor_id] = enable_low ? 1u : 0u;
    g_tsens_high_threshold_enable[sensor_id] = enable_high ? 1u : 0u;
    g_tsens_low_threshold_latched[sensor_id] = 0;
    g_tsens_high_threshold_latched[sensor_id] = 0;

    last_deci_c = g_tsens_last_deci_c[sensor_id];
    min_deci_c = g_tsens_range_min_deci_c[sensor_id];
    max_deci_c = g_tsens_range_max_deci_c[sensor_id];
    low_hits = g_tsens_low_hit_cnt[sensor_id];
    high_hits = g_tsens_high_hit_cnt[sensor_id];
    flags = (enable_low ? 1u : 0u) | ((enable_high ? 1u : 0u) << 1);
    KeReleaseSpinLock(&g_tsens_trace_lock, irql);

    tsens_publish_sensor_state(sensor_id, last_deci_c, min_deci_c, max_deci_c, low, high, low_hits,
                               high_hits, flags);
    tsens_record_event(TSENS_EVENT_BRACKET, STATUS_SUCCESS, sensor_id, last_deci_c, low, high,
                       enable_low ? 1u : 0u, enable_high ? 1u : 0u);
    return STATUS_SUCCESS;
}

VOID tsens_get_snapshot(_Out_ tsens_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    RtlZeroMemory(snapshot, sizeof(*snapshot));

    snapshot->version = 2;
    snapshot->sensor_cnt = tsens_get_cnt();
    snapshot->aggregate_temp_deci_c = (LONG)tsens_get_temp_deci_c();
    snapshot->read_cnt = (ULONGLONG)g_tsens_read_cnt;
    snapshot->dsm_refresh_cnt = (ULONGLONG)g_tsens_dsm_refresh_cnt;
    snapshot->threshold_hit_cnt = (ULONGLONG)g_tsens_threshold_hit_cnt;
    snapshot->event_sequence = (ULONGLONG)g_tsens_event_sequence;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);
    snapshot->history_cnt = g_tsens_history_cnt;
    snapshot->history_dropped = g_tsens_history_dropped;
    snapshot->last_event_code = g_tsens_last_event_code;
    snapshot->last_event_status = g_tsens_last_event_status;
    snapshot->last_sensor_id = g_tsens_last_event_sensor_id;
    snapshot->last_temp_deci_c = g_tsens_last_event_temp_deci_c;
    snapshot->last_low_deci_c = g_tsens_last_event_low_deci_c;
    snapshot->last_high_deci_c = g_tsens_last_event_high_deci_c;
    snapshot->last_event_tick100ns = g_tsens_last_event_tick100ns;

    ULONG cnt = snapshot->sensor_cnt;
    if (cnt > TSENS_MAX_SENSORS)
        cnt = TSENS_MAX_SENSORS;

    for (ULONG i = 0; i < cnt; i++) {
        snapshot->sensors[i].last_temp_deci_c = g_tsens_last_deci_c[i];
        snapshot->sensors[i].min_temp_deci_c = g_tsens_range_min_deci_c[i];
        snapshot->sensors[i].max_temp_deci_c = g_tsens_range_max_deci_c[i];
        snapshot->sensors[i].low_threshold_deci_c = g_tsens_low_threshold_deci_c[i];
        snapshot->sensors[i].high_threshold_deci_c = g_tsens_high_threshold_deci_c[i];
        snapshot->sensors[i].low_hit_cnt = g_tsens_low_hit_cnt[i];
        snapshot->sensors[i].high_hit_cnt = g_tsens_high_hit_cnt[i];
        snapshot->sensors[i].flags = (g_tsens_low_threshold_enable[i] ? 1u : 0u) |
                                     ((g_tsens_high_threshold_enable[i] ? 1u : 0u) << 1) |
                                     ((g_tsens_low_threshold_latched[i] ? 1u : 0u) << 2) |
                                     ((g_tsens_high_threshold_latched[i] ? 1u : 0u) << 3);
    }
    KeReleaseSpinLock(&g_tsens_trace_lock, irql);
}

NTSTATUS tsens_get_history(_Out_writes_to_(capacity, *cnt) tsens_event_t* events,
                           _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_tsens_trace_lock, &irql);

    ULONG avail = g_tsens_history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;
    ULONG start =
        (g_tsens_history_head + TSENS_HISTORY_CAPACITY - avail + skip) % TSENS_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % TSENS_HISTORY_CAPACITY;
        events[i] = g_tsens_history[idx];
    }

    *cnt = take;
    *dropped = g_tsens_history_dropped + skip;

    KeReleaseSpinLock(&g_tsens_trace_lock, irql);
    return STATUS_SUCCESS;
}
