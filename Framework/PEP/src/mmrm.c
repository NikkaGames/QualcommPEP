#include "mmrm.h"
#include "resource_cache.h"
#include "tsens.h"
#include "acpi.h"

typedef struct mmrm_state {
    ULONG feature_enabled;
    ULONG reserved_power_milli_watts;
    ULONG sampling_period_milli_seconds;
    ULONG max_throttle_percentage;
    ULONG mitigation_start_threshold_milli_amps;
    ULONG mitigation_stop_threshold_milli_amps;
    ULONG temperature_sensor_id;
    ULONG test_mode_enabled;
    ULONG test_simulate_nom_freq_for_clocks;
    LONG test_camera_temp_deci_c;
    LONG test_video_temp_deci_c;
    LONG test_display_temp_deci_c;
    ULONG video_encoding_state;
    ULONG video_decoding_state;

    LONG threshold_low_deci_k;
    LONG threshold_high_deci_k;
    LONG tzt_temperature_deci_k;
    BOOLEAN tzt_above_threshold;

    LONG camera_temp_deci_c;
    LONG video_temp_deci_c;
    LONG display_temp_deci_c;
    ULONG last_total_power_milli_watts;
    ULONG last_peak_milli_amps;
    ULONG last_camera_power_milli_watts;
    ULONG last_video_power_milli_watts;
    ULONG last_display_power_milli_watts;
    ULONG peak_voltage_milli_volts;
    ULONG global_dyn_permille;
    ULONG global_lkg_permille;
    ULONG clock_freq_khz[MMRM_MAX_CLOCK_DOMAINS];
    ULONG clock_power_milli_watts[MMRM_MAX_CLOCK_DOMAINS];
    ULONG clock_dyn_permille[MMRM_MAX_CLOCK_DOMAINS];
    ULONG clock_lkg_permille[MMRM_MAX_CLOCK_DOMAINS];
    LONG clock_temp_bias_deci_c[MMRM_MAX_CLOCK_DOMAINS];
    ULONG clock_enabled[MMRM_MAX_CLOCK_DOMAINS];

    ULONG throttle_reason;
    ULONG throttle_percentage;
    ULONG updated;
    ULONGLONG last_update_tick100ns;

    ULONG last_version;
    ULONG last_activation_reasons;
    ULONG last_passive_limit;
    ULONG last_reserved;
    LONG last_dsm_status;
    ULONG dsm_refresh_cnt;
    ULONG dsm_poll_interval_milli_seconds;
    ULONG last_dsm_policy_item_cnt;
    ULONG last_dsm_clock_item_cnt;
    UCHAR last_thermal_standby;
    BOOLEAN policy_wait_active;
    BOOLEAN sampling_timer_running;
    BOOLEAN dsm_configured;
    BOOLEAN dsm_enabled;
    WDFDEVICE attached_dev;
    ULONGLONG last_dsm_refresh_tick100ns;
    ULONGLONG event_sequence;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_reason;
    ULONG last_event_percent;
    ULONG last_event_updated;
    ULONG last_event_peak_milli_amps;
    LONG last_event_temp_deci_k;
    LONG last_event_low_temp_deci_k;
    LONG last_event_high_temp_deci_k;
    ULONGLONG last_event_tick100ns;
    mmrm_event_t history[MMRM_HISTORY_CAPACITY];
    mmrm_update_callback_t update_callback;
    PVOID update_callback_ctx;
} mmrm_state_t;

typedef struct mmrm_dsm_config {
    volatile LONG init_state;
    BOOLEAN enabled;
    UCHAR reserved0[3];
    GUID guid;
    ULONG revision;
    ULONG poll_interval_milli_seconds;
    ULONG func_feature_enabled;
    ULONG func_reserved_power_milli_watts;
    ULONG func_sampling_period_milli_seconds;
    ULONG func_max_throttle_percentage;
    ULONG func_mitigation_start_threshold_milli_amps;
    ULONG func_mitigation_stop_threshold_milli_amps;
    ULONG func_temperature_sensor_id;
    ULONG func_video_encoding_state;
    ULONG func_video_decoding_state;
    ULONG func_policy_package;
    ULONG func_clock_freq_khz_table;
    ULONG func_clock_freq_khz_by_idx;
} mmrm_dsm_config_t;

static volatile LONG g_mmrm_init;
static KSPIN_LOCK g_mmrm_lock;
static mmrm_state_t g_mmrm;
static mmrm_dsm_config_t g_mmrm_dsm;
static WDFTIMER g_mmrm_timer;
static const LONG mmrm_temp_invalid = (LONG)0x80000000L;

static const ULONG g_mmrm_display_tsens_list[2] = {35u, 38u};
static const ULONG g_mmrm_camera_tsens_list[2] = {13u, 27u};

typedef struct mmrm_clock_domain_desc {
    const CHAR* name;
    ULONG ref_freq_khz;
    ULONG ref_dyn_milli_watts;
    ULONG ref_lkg_milli_watts;
} mmrm_clock_domain_desc_t;

static const mmrm_clock_domain_desc_t g_mmrm_clock_domains[MMRM_MAX_CLOCK_DOMAINS] = {
    {"cam_cc_ife_0_clk_src", 760000u, 221u, 2u},
    {"cam_cc_ife_1_clk_src", 760000u, 221u, 2u},
    {"cam_cc_ife_2_clk_src", 760000u, 221u, 2u},
    {"cam_cc_ife_3_clk_src", 760000u, 221u, 2u},
    {"cam_cc_ipe_0_clk_src", 600000u, 194u, 2u},
    {"cam_cc_ipe_1_clk", 600000u, 194u, 2u},
    {"cam_cc_bps_clk_src", 600000u, 194u, 1u},
    {"cam_cc_jpeg_clk_src", 600000u, 15u, 0u},
    {"cam_cc_camnoc_axi_clk_src", 400000u, 90u, 3u},
    {"video_cc_mvs0_clk_src", 1332000u, 655u, 9u},
    {"video_cc_mvs1_clk_src", 1332000u, 290u, 9u},
    {"mdss_0_disp_cc_mdss_mdp_clk_src", 500000u, 337u, 4u}};

#define MMRM_VIDEO_DISABLED 0u
#define MMRM_VIDEO_ENABLED 1u
#define MMRM_MIN_PERMILLE 100u
#define MMRM_MAX_DYN_PERMILLE 8000u
#define MMRM_MAX_LKG_PERMILLE 12000u
#define MMRM_DEFAULT_PEAK_VOLTAGE_MV 752u

EVT_WDF_TIMER mmrm_timer_fn;

static ULONG mmrm_compute_dsm_flags_locked(VOID)
{
    ULONG flags = 0;
    if (g_mmrm.dsm_configured)
        flags |= 1u;
    if (g_mmrm.dsm_enabled)
        flags |= 2u;
    return flags;
}

static VOID mmrm_record_event_locked(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG reason,
                                     _In_ ULONG percent, _In_ ULONG updated,
                                     _In_ ULONG peak_milli_amps, _In_ LONG temp_deci_k,
                                     _In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k)
{
    ULONGLONG seq = ++g_mmrm.event_sequence;
    ULONGLONG idx = (seq - 1ull) % MMRM_HISTORY_CAPACITY;
    mmrm_event_t* e = &g_mmrm.history[idx];

    e->sequence = seq;
    e->tick100ns = KeQueryInterruptTime();
    e->event_code = event_code;
    e->status = (LONG)status;
    e->reason = reason;
    e->percent = percent;
    e->updated = updated;
    e->peak_milli_amps = peak_milli_amps;
    e->temp_deci_k = temp_deci_k;
    e->low_temp_deci_k = low_temp_deci_k;
    e->high_temp_deci_k = high_temp_deci_k;
    e->passive_limit = g_mmrm.last_passive_limit;
    e->activation_reasons = g_mmrm.last_activation_reasons;
    e->feature_enabled = g_mmrm.feature_enabled;
    e->dsm_flags = mmrm_compute_dsm_flags_locked();

    g_mmrm.last_event_code = event_code;
    g_mmrm.last_event_status = (LONG)status;
    g_mmrm.last_event_reason = reason;
    g_mmrm.last_event_percent = percent;
    g_mmrm.last_event_updated = updated;
    g_mmrm.last_event_peak_milli_amps = peak_milli_amps;
    g_mmrm.last_event_temp_deci_k = temp_deci_k;
    g_mmrm.last_event_low_temp_deci_k = low_temp_deci_k;
    g_mmrm.last_event_high_temp_deci_k = high_temp_deci_k;
    g_mmrm.last_event_tick100ns = e->tick100ns;
}

static ULONG mmrm_clamp_percent(_In_ ULONG percent)
{
    if (percent == 0)
        return 1;
    if (percent > 100)
        return 100;
    return percent;
}

static LONG mmrm_clamp_temp_deci_c(_In_ LONG temp_deci_c)
{
    if (temp_deci_c < -500)
        return -500;
    if (temp_deci_c > 1500)
        return 1500;
    return temp_deci_c;
}

static ULONG mmrm_clamp_permille(_In_ ULONG value, _In_ ULONG min_value, _In_ ULONG max_value,
                                 _In_ ULONG fallback)
{
    if (value == 0)
        value = fallback;
    if (value < min_value)
        value = min_value;
    if (value > max_value)
        value = max_value;
    return value;
}

static ULONG mmrm_clamp_peak_voltage_mv(_In_ ULONG milli_volts)
{
    if (milli_volts < 100u)
        return 100u;
    if (milli_volts > 5000u)
        return 5000u;
    return milli_volts;
}

static LONG mmrm_temp_deci_k_to_deci_c(_In_ LONG temp_deci_k)
{
    return temp_deci_k - 2730;
}

static LONG mmrm_read_sensor_temp_deci_c(_In_ ULONG sensor_id, _In_ LONG fallback_deci_c)
{
    if (sensor_id >= tsens_get_cnt() || sensor_id > 0x7FFFu)
        return fallback_deci_c;

    SHORT idx = (SHORT)sensor_id;
    USHORT out = 0;
    size_t written = 0;
    NTSTATUS st = tsens_get_temp_for_idx(&idx, sizeof(idx), &out, sizeof(out), &written);
    if (!NT_SUCCESS(st) || written < sizeof(out) || out == 0)
        return fallback_deci_c;

    return ((LONG)out - 2730);
}

static LONG mmrm_read_max_list_temp_deci_c(_In_reads_(cnt) const ULONG* sensors, _In_ ULONG cnt,
                                           _In_ LONG fallback_deci_c)
{
    LONG best = mmrm_temp_invalid;
    if (!sensors || cnt == 0)
        return fallback_deci_c;

    for (ULONG i = 0; i < cnt; i++) {
        LONG t = mmrm_read_sensor_temp_deci_c(sensors[i], fallback_deci_c);
        if (best == mmrm_temp_invalid || t > best)
            best = t;
    }

    if (best == mmrm_temp_invalid)
        return fallback_deci_c;
    return best;
}

static ULONG mmrm_query_clock_freq_khz(_In_ ULONG idx, _In_ ULONG default_khz)
{
    WCHAR value_name[48];
    NTSTATUS st =
        RtlStringCchPrintfW(value_name, RTL_NUMBER_OF(value_name), L"MmrmClockFreqKhz%u", idx);
    if (!NT_SUCCESS(st))
        return default_khz;
    return registry_query_u_long(value_name, default_khz);
}

static ULONG mmrm_query_indexed_u_long(_In_ PCWSTR prefix, _In_ ULONG idx, _In_ ULONG default_value)
{
    WCHAR value_name[64];
    if (!NT_SUCCESS(RtlStringCchPrintfW(value_name, RTL_NUMBER_OF(value_name), L"%ls%u", prefix,
                                        (unsigned int)idx)))
        return default_value;
    return registry_query_u_long(value_name, default_value);
}

#define MMRM_DSM_DEFAULT_POLL_MS 5000u
#define MMRM_DSM_MAX_POLL_MS 60000u

static ULONG mmrm_clamp_dsm_poll_ms(_In_ ULONG poll_ms)
{
    if (poll_ms == 0)
        return MMRM_DSM_DEFAULT_POLL_MS;
    if (poll_ms > MMRM_DSM_MAX_POLL_MS)
        return MMRM_DSM_MAX_POLL_MS;
    return poll_ms;
}

static VOID mmrm_ensure_dsm_config(VOID)
{
    for (;;) {
        LONG state = InterlockedCompareExchange(&g_mmrm_dsm.init_state, 0, 0);
        if (state == 2)
            return;
        if (state == 1) {
            KeStallExecutionProcessor(50);
            continue;
        }
        if (InterlockedCompareExchange(&g_mmrm_dsm.init_state, 1, 0) == 0)
            break;
    }

    mmrm_dsm_config_t cfg;
    RtlZeroMemory(&cfg, sizeof(cfg));
    cfg.poll_interval_milli_seconds = mmrm_clamp_dsm_poll_ms(
        registry_query_u_long(L"MmrmDsmPollIntervalMilliSeconds", MMRM_DSM_DEFAULT_POLL_MS));
    cfg.revision = registry_query_u_long(L"MmrmDsmRevision", 0);
    cfg.func_feature_enabled = registry_query_u_long(L"MmrmDsmFuncFeatureEnabled", 0);
    cfg.func_reserved_power_milli_watts =
        registry_query_u_long(L"MmrmDsmFuncReservedPowerMilliWatts", 0);
    cfg.func_sampling_period_milli_seconds =
        registry_query_u_long(L"MmrmDsmFuncSamplingPeriodMilliSeconds", 0);
    cfg.func_max_throttle_percentage =
        registry_query_u_long(L"MmrmDsmFuncMaxThrottlePercentage", 0);
    cfg.func_mitigation_start_threshold_milli_amps =
        registry_query_u_long(L"MmrmDsmFuncMitigationStartThresholdMilliAmps", 0);
    cfg.func_mitigation_stop_threshold_milli_amps =
        registry_query_u_long(L"MmrmDsmFuncMitigationStopThresholdMilliAmps", 0);
    cfg.func_temperature_sensor_id = registry_query_u_long(L"MmrmDsmFuncTemperatureSensorId", 0);
    cfg.func_video_encoding_state = registry_query_u_long(L"MmrmDsmFuncVideoEncodingState", 0);
    cfg.func_video_decoding_state = registry_query_u_long(L"MmrmDsmFuncVideoDecodingState", 0);
    cfg.func_policy_package = registry_query_u_long(L"MmrmDsmFuncPolicyPackage", 0);
    cfg.func_clock_freq_khz_table = registry_query_u_long(L"MmrmDsmFuncClockFreqKhzTable", 0);
    cfg.func_clock_freq_khz_by_idx = registry_query_u_long(L"MmrmDsmFuncClockFreqKhzByIndex", 0);

    UNICODE_STRING guid_str = {0};
    NTSTATUS st = registry_query_unicode_string_alloc(L"MmrmDsmGuid", &guid_str, 64);
    if (NT_SUCCESS(st) && guid_str.Buffer != NULL) {
        GUID g;
        st = RtlGUIDFromString(&guid_str, &g);
        if (NT_SUCCESS(st)) {
            cfg.guid = g;
            cfg.enabled = TRUE;
        }
        ExFreePool(guid_str.Buffer);
    }

    g_mmrm_dsm = cfg;
    InterlockedExchange(&g_mmrm_dsm.init_state, 2);
}

static BOOLEAN mmrm_eval_dsm_ulong(_In_ WDFDEVICE dev, _In_ const mmrm_dsm_config_t* dsm,
                                   _In_ ULONG function_idx, _Out_ PULONG value,
                                   _Inout_ PNTSTATUS last_status)
{
    if (!dev || !dsm || !value || function_idx == 0)
        return FALSE;

    NTSTATUS st = acpi_eval_dsm_ulong(dev, &dsm->guid, dsm->revision, function_idx, value);
    if (!NT_SUCCESS(st)) {
        if (last_status)
            *last_status = st;
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN mmrm_eval_dsm_ulong_with_arg(_In_ WDFDEVICE dev, _In_ const mmrm_dsm_config_t* dsm,
                                            _In_ ULONG function_idx, _In_ ULONG arg,
                                            _Out_ PULONG value, _Inout_ PNTSTATUS last_status)
{
    if (!dev || !dsm || !value || function_idx == 0)
        return FALSE;

    NTSTATUS st = acpi_eval_dsm_ulong_with_arg_ulong(dev, &dsm->guid, dsm->revision, function_idx,
                                                     arg, value);
    if (!NT_SUCCESS(st)) {
        if (last_status)
            *last_status = st;
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN mmrm_eval_dsm_ulong_array(_In_ WDFDEVICE dev, _In_ const mmrm_dsm_config_t* dsm,
                                         _In_ ULONG function_idx,
                                         _In_reads_opt_(arg_cnt) const ULONG* args,
                                         _In_ ULONG arg_cnt,
                                         _Out_writes_to_(value_capacity, *value_cnt) PULONG values,
                                         _In_ ULONG value_capacity, _Out_ PULONG value_cnt,
                                         _Inout_ PNTSTATUS last_status)
{
    if (!dev || !dsm || !values || !value_cnt || function_idx == 0 || value_capacity == 0)
        return FALSE;

    NTSTATUS st = acpi_eval_dsm_package_ulongs(dev, &dsm->guid, dsm->revision, function_idx, args,
                                               arg_cnt, values, value_capacity, value_cnt);
    if (!NT_SUCCESS(st)) {
        if (last_status)
            *last_status = st;
        return FALSE;
    }

    if (*value_cnt == 0)
        return FALSE;

    return TRUE;
}

static BOOLEAN mmrm_begin_dsm_poll(_In_ ULONG poll_ms, _Out_ WDFDEVICE* dev_out)
{
    if (!dev_out)
        return FALSE;

    *dev_out = NULL;
    ULONGLONG now = KeQueryInterruptTime();
    ULONGLONG interval100ns = (ULONGLONG)poll_ms * 10000ull;
    BOOLEAN should_poll = FALSE;
    WDFDEVICE dev = NULL;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    dev = g_mmrm.attached_dev;
    if (dev != NULL) {
        ULONGLONG last = g_mmrm.last_dsm_refresh_tick100ns;
        if (last == 0 || now <= last || (now - last) >= interval100ns) {
            g_mmrm.last_dsm_refresh_tick100ns = now;
            should_poll = TRUE;
        }
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    if (should_poll)
        *dev_out = dev;
    return should_poll;
}

static VOID mmrm_record_dsm_poll_result(_In_ BOOLEAN configured, _In_ BOOLEAN enabled,
                                        _In_ NTSTATUS status, _In_ ULONG poll_ms,
                                        _In_ ULONG policy_item_cnt, _In_ ULONG clock_item_cnt)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.dsm_configured = configured ? TRUE : FALSE;
    g_mmrm.dsm_enabled = enabled ? TRUE : FALSE;
    g_mmrm.last_dsm_status = (LONG)status;
    g_mmrm.dsm_poll_interval_milli_seconds = poll_ms;
    g_mmrm.last_dsm_policy_item_cnt = policy_item_cnt;
    g_mmrm.last_dsm_clock_item_cnt = clock_item_cnt;
    if (configured)
        g_mmrm.dsm_refresh_cnt++;
    mmrm_record_event_locked(MMRM_EVENT_DSM_POLL, status, g_mmrm.throttle_reason,
                             g_mmrm.throttle_percentage, g_mmrm.updated,
                             g_mmrm.last_peak_milli_amps, g_mmrm.tzt_temperature_deci_k,
                             g_mmrm.threshold_low_deci_k, g_mmrm.threshold_high_deci_k);
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

static VOID mmrm_apply_dsm_overrides(
    _Inout_ PULONG feature_enabled, _Inout_ PULONG reserved_power_milli_watts,
    _Inout_ PULONG sampling_period_milli_seconds, _Inout_ PULONG max_throttle_percentage,
    _Inout_ PULONG mitigation_start_threshold_milli_amps,
    _Inout_ PULONG mitigation_stop_threshold_milli_amps, _Inout_ PULONG temperature_sensor_id,
    _Inout_ PULONG video_encoding_state, _Inout_ PULONG video_decoding_state,
    _Inout_updates_(RTL_NUMBER_OF(g_mmrm_clock_domains)) PULONG clock_freq_khz)
{
    mmrm_ensure_dsm_config();

    if (InterlockedCompareExchange(&g_mmrm_dsm.init_state, 0, 0) != 2 || !g_mmrm_dsm.enabled) {
        mmrm_record_dsm_poll_result(FALSE, FALSE, STATUS_NOT_SUPPORTED, 0, 0, 0);
        return;
    }

    mmrm_dsm_config_t dsm = g_mmrm_dsm;
    ULONG poll_ms = mmrm_clamp_dsm_poll_ms(dsm.poll_interval_milli_seconds);

    WDFDEVICE dev = NULL;
    if (!mmrm_begin_dsm_poll(poll_ms, &dev))
        return;

    NTSTATUS last_status = STATUS_NOT_FOUND;
    BOOLEAN any_success = FALSE;
    ULONG value = 0;
    ULONG policy_item_cnt = 0;
    ULONG clock_item_cnt = 0;
    ULONG values[RTL_NUMBER_OF(g_mmrm_clock_domains)] = {0};
    ULONG value_cnt = 0;

    if (dsm.func_policy_package != 0 &&
        mmrm_eval_dsm_ulong_array(dev, &dsm, dsm.func_policy_package, NULL, 0, values,
                                  RTL_NUMBER_OF(values), &value_cnt, &last_status)) {
        policy_item_cnt = value_cnt;
        if (value_cnt > 0)
            *mitigation_start_threshold_milli_amps = values[0];
        if (value_cnt > 1)
            *mitigation_stop_threshold_milli_amps = values[1];
        if (value_cnt > 2)
            *max_throttle_percentage = values[2];
        if (value_cnt > 3)
            *reserved_power_milli_watts = values[3];
        if (value_cnt > 4)
            *sampling_period_milli_seconds = values[4];
        if (value_cnt > 5)
            *temperature_sensor_id = values[5];
        if (value_cnt > 6)
            *feature_enabled = values[6] ? 1u : 0u;
        if (value_cnt > 7)
            *video_encoding_state = values[7];
        if (value_cnt > 8)
            *video_decoding_state = values[8];
        any_success = TRUE;
    }

    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_feature_enabled, &value, &last_status)) {
        *feature_enabled = value ? 1u : 0u;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_reserved_power_milli_watts, &value, &last_status)) {
        *reserved_power_milli_watts = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_sampling_period_milli_seconds, &value,
                            &last_status)) {
        *sampling_period_milli_seconds = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_max_throttle_percentage, &value, &last_status)) {
        *max_throttle_percentage = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_mitigation_start_threshold_milli_amps, &value,
                            &last_status)) {
        *mitigation_start_threshold_milli_amps = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_mitigation_stop_threshold_milli_amps, &value,
                            &last_status)) {
        *mitigation_stop_threshold_milli_amps = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_temperature_sensor_id, &value, &last_status)) {
        *temperature_sensor_id = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_video_encoding_state, &value, &last_status)) {
        *video_encoding_state = value;
        any_success = TRUE;
    }
    if (mmrm_eval_dsm_ulong(dev, &dsm, dsm.func_video_decoding_state, &value, &last_status)) {
        *video_decoding_state = value;
        any_success = TRUE;
    }

    value_cnt = 0;
    if (dsm.func_clock_freq_khz_table != 0 &&
        mmrm_eval_dsm_ulong_array(dev, &dsm, dsm.func_clock_freq_khz_table, NULL, 0, values,
                                  RTL_NUMBER_OF(values), &value_cnt, &last_status)) {
        clock_item_cnt = value_cnt;
        for (ULONG i = 0; i < value_cnt && i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
            if (values[i] == 0)
                continue;
            clock_freq_khz[i] = values[i];
            any_success = TRUE;
        }
    }

    if (dsm.func_clock_freq_khz_by_idx != 0) {
        ULONG by_idx_cnt = 0;
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
            if (!mmrm_eval_dsm_ulong_with_arg(dev, &dsm, dsm.func_clock_freq_khz_by_idx, i, &value,
                                              &last_status))
                continue;
            if (value == 0)
                continue;
            clock_freq_khz[i] = value;
            by_idx_cnt++;
            any_success = TRUE;
        }
        if (clock_item_cnt < by_idx_cnt)
            clock_item_cnt = by_idx_cnt;
    }

    if (*sampling_period_milli_seconds == 0)
        *sampling_period_milli_seconds = 1;

    ULONG tsens_cnt = tsens_get_cnt();
    if (tsens_cnt == 0)
        tsens_cnt = 1;
    if (*temperature_sensor_id >= tsens_cnt)
        *temperature_sensor_id = 0;

    mmrm_record_dsm_poll_result(TRUE, any_success, any_success ? STATUS_SUCCESS : last_status,
                                poll_ms, policy_item_cnt, clock_item_cnt);
}

static VOID mmrm_refresh_runtime_profile(VOID)
{
    ULONG feature_enabled =
        registry_query_u_long(L"MmrmFeatureEnabled", g_mmrm.feature_enabled ? 1u : 0u);
    ULONG reserved_power_milli_watts =
        registry_query_u_long(L"MmrmReservedPowerMilliWatts", g_mmrm.reserved_power_milli_watts);
    ULONG sampling_period_milli_seconds = registry_query_u_long(
        L"MmrmSamplingPeriodMilliSeconds", g_mmrm.sampling_period_milli_seconds);
    ULONG max_throttle = mmrm_clamp_percent(
        registry_query_u_long(L"MmrmMaxThrottlePercentage", g_mmrm.max_throttle_percentage));
    ULONG start_threshold = registry_query_u_long(L"MmrmMitigationStartThresholdMilliAmps",
                                                  g_mmrm.mitigation_start_threshold_milli_amps);
    ULONG stop_threshold = registry_query_u_long(L"MmrmMitigationStopThresholdMilliAmps",
                                                 g_mmrm.mitigation_stop_threshold_milli_amps);
    ULONG sensor_id = registry_query_u_long(L"ThermalSensorIndex", g_mmrm.temperature_sensor_id);
    ULONG test_mode =
        registry_query_u_long(L"MmrmTestModeEnabled", g_mmrm.test_mode_enabled ? 1u : 0u);
    ULONG test_nom = registry_query_u_long(L"MmrmTestSimulateNomFreqForClocks",
                                           g_mmrm.test_simulate_nom_freq_for_clocks ? 1u : 0u);
    LONG test_cam = (LONG)registry_query_u_long(L"MmrmTestCameraTempC",
                                                (ULONG)(g_mmrm.test_camera_temp_deci_c / 10)) *
                    10;
    LONG test_vid = (LONG)registry_query_u_long(L"MmrmTestVideoTempC",
                                                (ULONG)(g_mmrm.test_video_temp_deci_c / 10)) *
                    10;
    LONG test_disp = (LONG)registry_query_u_long(L"MmrmTestDisplayTempC",
                                                 (ULONG)(g_mmrm.test_display_temp_deci_c / 10)) *
                     10;
    ULONG venc = registry_query_u_long(L"MmrmVideoEncodingState", g_mmrm.video_encoding_state);
    ULONG vdec = registry_query_u_long(L"MmrmVideoDecodingState", g_mmrm.video_decoding_state);
    ULONG peak_voltage_mv = mmrm_clamp_peak_voltage_mv(
        registry_query_u_long(L"MmrmPeakVoltageMilliVolts", g_mmrm.peak_voltage_milli_volts));
    ULONG global_dyn_permille = mmrm_clamp_permille(
        registry_query_u_long(L"MmrmGlobalDynPermille", g_mmrm.global_dyn_permille),
        MMRM_MIN_PERMILLE, MMRM_MAX_DYN_PERMILLE, 1000u);
    ULONG global_lkg_permille = mmrm_clamp_permille(
        registry_query_u_long(L"MmrmGlobalLkgPermille", g_mmrm.global_lkg_permille),
        MMRM_MIN_PERMILLE, MMRM_MAX_LKG_PERMILLE, 1000u);
    ULONG freqs[RTL_NUMBER_OF(g_mmrm_clock_domains)];
    ULONG dyn_permille[RTL_NUMBER_OF(g_mmrm_clock_domains)];
    ULONG lkg_permille[RTL_NUMBER_OF(g_mmrm_clock_domains)];
    LONG temp_bias_deci_c[RTL_NUMBER_OF(g_mmrm_clock_domains)];
    ULONG enabled[RTL_NUMBER_OF(g_mmrm_clock_domains)];

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
        freqs[i] = mmrm_query_clock_freq_khz(i, g_mmrm_clock_domains[i].ref_freq_khz);
        dyn_permille[i] = mmrm_clamp_permille(
            mmrm_query_indexed_u_long(L"MmrmClockDynPermille", i, g_mmrm.clock_dyn_permille[i]),
            MMRM_MIN_PERMILLE, MMRM_MAX_DYN_PERMILLE, 1000u);
        lkg_permille[i] = mmrm_clamp_permille(
            mmrm_query_indexed_u_long(L"MmrmClockLkgPermille", i, g_mmrm.clock_lkg_permille[i]),
            MMRM_MIN_PERMILLE, MMRM_MAX_LKG_PERMILLE, 1000u);
        temp_bias_deci_c[i] = (LONG)mmrm_query_indexed_u_long(
            L"MmrmClockTempBiasDeciC", i, (ULONG)g_mmrm.clock_temp_bias_deci_c[i]);
        temp_bias_deci_c[i] = mmrm_clamp_temp_deci_c(temp_bias_deci_c[i]);
        enabled[i] =
            mmrm_query_indexed_u_long(L"MmrmClockEnable", i, g_mmrm.clock_enabled[i]) ? 1u : 0u;
    }

    mmrm_apply_dsm_overrides(&feature_enabled, &reserved_power_milli_watts,
                             &sampling_period_milli_seconds, &max_throttle, &start_threshold,
                             &stop_threshold, &sensor_id, &venc, &vdec, freqs);

    if (sampling_period_milli_seconds == 0)
        sampling_period_milli_seconds = 1;

    ULONG tsens_cnt = tsens_get_cnt();
    if (tsens_cnt == 0)
        tsens_cnt = 1;
    if (sensor_id >= tsens_cnt)
        sensor_id = 0;

    if (stop_threshold >= start_threshold) {
        stop_threshold = (start_threshold > 400u) ? (start_threshold - 400u) : 0u;
    }

    test_cam = mmrm_clamp_temp_deci_c(test_cam);
    test_vid = mmrm_clamp_temp_deci_c(test_vid);
    test_disp = mmrm_clamp_temp_deci_c(test_disp);

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.feature_enabled = feature_enabled ? 1u : 0u;
    g_mmrm.reserved_power_milli_watts = reserved_power_milli_watts;
    g_mmrm.sampling_period_milli_seconds = sampling_period_milli_seconds;
    g_mmrm.max_throttle_percentage = max_throttle;
    g_mmrm.mitigation_start_threshold_milli_amps = start_threshold;
    g_mmrm.mitigation_stop_threshold_milli_amps = stop_threshold;
    g_mmrm.temperature_sensor_id = sensor_id;
    g_mmrm.test_mode_enabled = test_mode ? 1u : 0u;
    g_mmrm.test_simulate_nom_freq_for_clocks = test_nom ? 1u : 0u;
    g_mmrm.test_camera_temp_deci_c = test_cam;
    g_mmrm.test_video_temp_deci_c = test_vid;
    g_mmrm.test_display_temp_deci_c = test_disp;
    g_mmrm.video_encoding_state = venc;
    g_mmrm.video_decoding_state = vdec;
    g_mmrm.peak_voltage_milli_volts = peak_voltage_mv;
    g_mmrm.global_dyn_permille = global_dyn_permille;
    g_mmrm.global_lkg_permille = global_lkg_permille;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
        g_mmrm.clock_freq_khz[i] = freqs[i];
        g_mmrm.clock_dyn_permille[i] = dyn_permille[i];
        g_mmrm.clock_lkg_permille[i] = lkg_permille[i];
        g_mmrm.clock_temp_bias_deci_c[i] = temp_bias_deci_c[i];
        g_mmrm.clock_enabled[i] = enabled[i];
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

static ULONG mmrm_scale_by_permille(_In_ ULONG value, _In_ ULONG permille)
{
    ULONGLONG scaled = ((ULONGLONG)value * (ULONGLONG)permille + 500ull) / 1000ull;
    if (scaled > MAXULONG)
        scaled = MAXULONG;
    return (ULONG)scaled;
}

static ULONG mmrm_compute_domain_power_milli_watts(_In_ ULONG clock_idx, _In_ ULONG freq_khz,
                                                   _In_ LONG temp_deci_c)
{
    if (clock_idx >= RTL_NUMBER_OF(g_mmrm_clock_domains))
        return 0;
    if (!g_mmrm.clock_enabled[clock_idx])
        return 0;

    const mmrm_clock_domain_desc_t* d = &g_mmrm_clock_domains[clock_idx];
    if (d->ref_freq_khz == 0)
        return 0;

    if (g_mmrm.test_simulate_nom_freq_for_clocks)
        freq_khz = d->ref_freq_khz;
    if (freq_khz == 0)
        return 0;

    if (clock_idx == 9 && g_mmrm.video_encoding_state == MMRM_VIDEO_DISABLED)
        return 0;
    if (clock_idx == 10 && g_mmrm.video_decoding_state == MMRM_VIDEO_DISABLED)
        return 0;

    temp_deci_c = mmrm_clamp_temp_deci_c(temp_deci_c + g_mmrm.clock_temp_bias_deci_c[clock_idx]);

    LONG delta_deci = temp_deci_c - 250;
    if (delta_deci < 0)
        delta_deci = 0;

    ULONGLONG dyn =
        ((ULONGLONG)d->ref_dyn_milli_watts * (ULONGLONG)freq_khz + (d->ref_freq_khz / 2u)) /
        (ULONGLONG)d->ref_freq_khz;
    ULONG dyn_permille = 1000u + (ULONG)(delta_deci / 2);
    ULONG lkg_permille = 1000u + (ULONG)(delta_deci * 2);
    dyn_permille = mmrm_scale_by_permille(dyn_permille, g_mmrm.global_dyn_permille);
    lkg_permille = mmrm_scale_by_permille(lkg_permille, g_mmrm.global_lkg_permille);
    dyn_permille = mmrm_scale_by_permille(dyn_permille, g_mmrm.clock_dyn_permille[clock_idx]);
    lkg_permille = mmrm_scale_by_permille(lkg_permille, g_mmrm.clock_lkg_permille[clock_idx]);
    dyn_permille =
        mmrm_clamp_permille(dyn_permille, MMRM_MIN_PERMILLE, MMRM_MAX_DYN_PERMILLE, 1000u);
    lkg_permille =
        mmrm_clamp_permille(lkg_permille, MMRM_MIN_PERMILLE, MMRM_MAX_LKG_PERMILLE, 1000u);

    ULONG dyn_mw = mmrm_scale_by_permille((ULONG)dyn, dyn_permille);
    ULONG lkg_mw = mmrm_scale_by_permille(d->ref_lkg_milli_watts, lkg_permille);

    ULONGLONG total = (ULONGLONG)dyn_mw + (ULONGLONG)lkg_mw;
    if (total > MAXULONG)
        total = MAXULONG;
    return (ULONG)total;
}

static ULONG mmrm_compute_peak_milli_amps_locked(VOID)
{
    LONG camera_deci_c =
        g_mmrm.test_mode_enabled ? g_mmrm.test_camera_temp_deci_c : g_mmrm.camera_temp_deci_c;
    LONG video_deci_c =
        g_mmrm.test_mode_enabled ? g_mmrm.test_video_temp_deci_c : g_mmrm.video_temp_deci_c;
    LONG display_deci_c =
        g_mmrm.test_mode_enabled ? g_mmrm.test_display_temp_deci_c : g_mmrm.display_temp_deci_c;
    ULONGLONG total_mw = g_mmrm.reserved_power_milli_watts;
    ULONG camera_power_mw = 0;
    ULONG video_power_mw = 0;
    ULONG display_power_mw = 0;

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
        LONG temp_deci_c = camera_deci_c;
        if (i == 9 || i == 10)
            temp_deci_c = video_deci_c;
        else if (i == 11)
            temp_deci_c = display_deci_c;

        ULONG power_mw =
            mmrm_compute_domain_power_milli_watts(i, g_mmrm.clock_freq_khz[i], temp_deci_c);

        g_mmrm.clock_power_milli_watts[i] = power_mw;
        total_mw += power_mw;
        if (i == 9 || i == 10)
            video_power_mw += power_mw;
        else if (i == 11)
            display_power_mw += power_mw;
        else
            camera_power_mw += power_mw;
    }

    if (total_mw > MAXULONG)
        total_mw = MAXULONG;
    g_mmrm.last_total_power_milli_watts = (ULONG)total_mw;
    g_mmrm.last_camera_power_milli_watts = camera_power_mw;
    g_mmrm.last_video_power_milli_watts = video_power_mw;
    g_mmrm.last_display_power_milli_watts = display_power_mw;

    ULONG voltage_mv = g_mmrm.peak_voltage_milli_volts ? g_mmrm.peak_voltage_milli_volts
                                                       : MMRM_DEFAULT_PEAK_VOLTAGE_MV;
    ULONGLONG peak = (total_mw * 1000ull + ((ULONGLONG)voltage_mv - 1ull)) / (ULONGLONG)voltage_mv;
    if (peak > MAXULONG)
        peak = MAXULONG;
    return (ULONG)peak;
}

static VOID mmrm_schedule_timer(_In_ ULONG due_ms)
{
    if (!g_mmrm_timer)
        return;

    if (due_ms == 0)
        due_ms = 1;

    (VOID) WdfTimerStart(g_mmrm_timer, WDF_REL_TIMEOUT_IN_MS(due_ms));
}

static VOID mmrm_publish_state_snapshot(_In_ ULONG reason, _In_ ULONG percent, _In_ ULONG updated,
                                        _In_ ULONG peak_milli_amps, _In_ LONG tzt_temp_deci_k,
                                        _In_ LONG low_deci_k, _In_ LONG high_deci_k,
                                        _In_ LONG camera_temp_deci_c, _In_ LONG video_temp_deci_c,
                                        _In_ LONG display_temp_deci_c)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x31524D4Du;
    k.sub_type = 0;
    v.value0 = reason;
    v.value1 = percent;
    v.value2 = peak_milli_amps;
    v.value3 = updated;
    (VOID) res_set(k, v);

    k.res_id = 0x32524D4Du;
    k.sub_type = 0;
    v.value0 = (ULONG)tzt_temp_deci_k;
    v.value1 = (ULONG)low_deci_k;
    v.value2 = (ULONG)high_deci_k;
    v.value3 = g_mmrm.temperature_sensor_id;
    (VOID) res_set(k, v);

    k.res_id = 0x33524D4Du;
    k.sub_type = 0;
    v.value0 = (ULONG)(camera_temp_deci_c + 2730);
    v.value1 = (ULONG)(video_temp_deci_c + 2730);
    v.value2 = (ULONG)(display_temp_deci_c + 2730);
    v.value3 = g_mmrm.last_passive_limit;
    (VOID) res_set(k, v);

    k.res_id = 0x34524D4Du;
    k.sub_type = 0;
    v.value0 = g_mmrm.last_total_power_milli_watts;
    v.value1 = g_mmrm.video_encoding_state;
    v.value2 = g_mmrm.video_decoding_state;
    v.value3 = g_mmrm.test_mode_enabled;
    (VOID) res_set(k, v);

    k.res_id = 0x35524D4Du;
    k.sub_type = 0;
    v.value0 = g_mmrm.last_camera_power_milli_watts;
    v.value1 = g_mmrm.last_video_power_milli_watts;
    v.value2 = g_mmrm.last_display_power_milli_watts;
    v.value3 = g_mmrm.peak_voltage_milli_volts;
    (VOID) res_set(k, v);

    k.res_id = 0x36524D4Du;
    k.sub_type = 0;
    v.value0 = (g_mmrm.dsm_configured ? 1u : 0u) | ((g_mmrm.dsm_enabled ? 1u : 0u) << 1);
    v.value1 = g_mmrm.dsm_refresh_cnt;
    v.value2 = (ULONG)g_mmrm.last_dsm_status;
    v.value3 = g_mmrm.dsm_poll_interval_milli_seconds;
    (VOID) res_set(k, v);

    k.res_id = 0x37524D4Du;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
        k.sub_type = i;
        v.value0 = g_mmrm.clock_freq_khz[i];
        v.value1 = g_mmrm.clock_power_milli_watts[i];
        v.value2 = (g_mmrm.clock_enabled[i] ? 1u : 0u) |
                   ((g_mmrm.clock_dyn_permille[i] & 0x7FFFu) << 1) |
                   ((g_mmrm.clock_lkg_permille[i] & 0x7FFFu) << 16);
        v.value3 = (ULONG)(LONG)g_mmrm.clock_temp_bias_deci_c[i];
        (VOID) res_set(k, v);
    }

    k.res_id = 0x38524D4Du;
    k.sub_type = 0;
    v.value0 = (ULONG)g_mmrm.event_sequence;
    v.value1 = (g_mmrm.event_sequence < MMRM_HISTORY_CAPACITY) ? (ULONG)g_mmrm.event_sequence
                                                               : MMRM_HISTORY_CAPACITY;
    v.value2 = (g_mmrm.event_sequence > MMRM_HISTORY_CAPACITY)
                   ? (ULONG)(g_mmrm.event_sequence - MMRM_HISTORY_CAPACITY)
                   : 0u;
    v.value3 = g_mmrm.last_event_code;
    (VOID) res_set(k, v);

    k.res_id = 0x39524D4Du;
    k.sub_type = g_mmrm.last_event_code;
    v.value0 = (ULONG)g_mmrm.last_event_status;
    v.value1 = g_mmrm.last_event_reason;
    v.value2 = g_mmrm.last_event_percent;
    v.value3 = g_mmrm.last_event_peak_milli_amps;
    (VOID) res_set(k, v);
}

static VOID mmrm_ensure_init(VOID)
{
    for (;;) {
        LONG state = InterlockedCompareExchange(&g_mmrm_init, 0, 0);
        if (state == 2)
            return;
        if (state == 1) {
            KeStallExecutionProcessor(50);
            continue;
        }
        if (InterlockedCompareExchange(&g_mmrm_init, 1, 0) == 0)
            break;
    }

    KeInitializeSpinLock(&g_mmrm_lock);
    RtlZeroMemory(&g_mmrm, sizeof(g_mmrm));

    g_mmrm.feature_enabled = registry_query_u_long(L"MmrmFeatureEnabled", 1);
    g_mmrm.reserved_power_milli_watts = registry_query_u_long(L"MmrmReservedPowerMilliWatts", 0);
    g_mmrm.sampling_period_milli_seconds =
        registry_query_u_long(L"MmrmSamplingPeriodMilliSeconds", 1000);
    g_mmrm.max_throttle_percentage =
        mmrm_clamp_percent(registry_query_u_long(L"MmrmMaxThrottlePercentage", 80));
    g_mmrm.mitigation_start_threshold_milli_amps =
        registry_query_u_long(L"MmrmMitigationStartThresholdMilliAmps", 9000);
    g_mmrm.mitigation_stop_threshold_milli_amps =
        registry_query_u_long(L"MmrmMitigationStopThresholdMilliAmps", 8600);
    if (g_mmrm.mitigation_stop_threshold_milli_amps >=
        g_mmrm.mitigation_start_threshold_milli_amps) {
        g_mmrm.mitigation_stop_threshold_milli_amps =
            (g_mmrm.mitigation_start_threshold_milli_amps > 400)
                ? (g_mmrm.mitigation_start_threshold_milli_amps - 400)
                : 0;
    }
    g_mmrm.temperature_sensor_id = registry_query_u_long(L"ThermalSensorIndex", 0);
    g_mmrm.test_mode_enabled = registry_query_u_long(L"MmrmTestModeEnabled", 0) ? 1u : 0u;
    g_mmrm.test_simulate_nom_freq_for_clocks =
        registry_query_u_long(L"MmrmTestSimulateNomFreqForClocks", 0) ? 1u : 0u;
    g_mmrm.test_camera_temp_deci_c =
        mmrm_clamp_temp_deci_c((LONG)registry_query_u_long(L"MmrmTestCameraTempC", 25) * 10);
    g_mmrm.test_video_temp_deci_c =
        mmrm_clamp_temp_deci_c((LONG)registry_query_u_long(L"MmrmTestVideoTempC", 25) * 10);
    g_mmrm.test_display_temp_deci_c =
        mmrm_clamp_temp_deci_c((LONG)registry_query_u_long(L"MmrmTestDisplayTempC", 25) * 10);
    g_mmrm.video_encoding_state =
        registry_query_u_long(L"MmrmVideoEncodingState", MMRM_VIDEO_DISABLED);
    g_mmrm.video_decoding_state =
        registry_query_u_long(L"MmrmVideoDecodingState", MMRM_VIDEO_DISABLED);
    g_mmrm.peak_voltage_milli_volts = mmrm_clamp_peak_voltage_mv(
        registry_query_u_long(L"MmrmPeakVoltageMilliVolts", MMRM_DEFAULT_PEAK_VOLTAGE_MV));
    g_mmrm.global_dyn_permille =
        mmrm_clamp_permille(registry_query_u_long(L"MmrmGlobalDynPermille", 1000),
                            MMRM_MIN_PERMILLE, MMRM_MAX_DYN_PERMILLE, 1000u);
    g_mmrm.global_lkg_permille =
        mmrm_clamp_permille(registry_query_u_long(L"MmrmGlobalLkgPermille", 1000),
                            MMRM_MIN_PERMILLE, MMRM_MAX_LKG_PERMILLE, 1000u);
    g_mmrm.threshold_low_deci_k = MAXLONG;
    g_mmrm.threshold_high_deci_k = MAXLONG;
    g_mmrm.tzt_temperature_deci_k = (LONG)tsens_get_temp_deci_c() + 2730;
    g_mmrm.camera_temp_deci_c = (LONG)tsens_get_temp_deci_c();
    g_mmrm.video_temp_deci_c = (LONG)tsens_get_temp_deci_c();
    g_mmrm.display_temp_deci_c = (LONG)tsens_get_temp_deci_c();
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_mmrm_clock_domains); i++) {
        g_mmrm.clock_freq_khz[i] =
            mmrm_query_clock_freq_khz(i, g_mmrm_clock_domains[i].ref_freq_khz);
        g_mmrm.clock_dyn_permille[i] =
            mmrm_clamp_permille(mmrm_query_indexed_u_long(L"MmrmClockDynPermille", i, 1000),
                                MMRM_MIN_PERMILLE, MMRM_MAX_DYN_PERMILLE, 1000u);
        g_mmrm.clock_lkg_permille[i] =
            mmrm_clamp_permille(mmrm_query_indexed_u_long(L"MmrmClockLkgPermille", i, 1000),
                                MMRM_MIN_PERMILLE, MMRM_MAX_LKG_PERMILLE, 1000u);
        g_mmrm.clock_temp_bias_deci_c[i] = mmrm_clamp_temp_deci_c(
            (LONG)mmrm_query_indexed_u_long(L"MmrmClockTempBiasDeciC", i, 0));
        g_mmrm.clock_enabled[i] = mmrm_query_indexed_u_long(L"MmrmClockEnable", i, 1) ? 1u : 0u;
    }
    g_mmrm.last_peak_milli_amps = mmrm_compute_peak_milli_amps_locked();
    g_mmrm.throttle_reason = registry_query_u_long(L"MmrmThrottleReason", 0);
    g_mmrm.throttle_percentage =
        mmrm_clamp_percent(registry_query_u_long(L"MmrmThrottlePercent", 100));
    g_mmrm.updated = registry_query_u_long(L"MmrmPolicyUpdated", 0) ? 1u : 0u;
    g_mmrm.last_update_tick100ns = KeQueryInterruptTime();
    g_mmrm.attached_dev = NULL;
    g_mmrm.last_dsm_refresh_tick100ns = 0;
    g_mmrm.last_dsm_status = STATUS_NOT_SUPPORTED;
    g_mmrm.dsm_refresh_cnt = 0;
    g_mmrm.dsm_enabled = FALSE;

    mmrm_ensure_dsm_config();
    g_mmrm.dsm_configured = g_mmrm_dsm.enabled ? TRUE : FALSE;
    g_mmrm.dsm_poll_interval_milli_seconds =
        g_mmrm_dsm.enabled ? mmrm_clamp_dsm_poll_ms(g_mmrm_dsm.poll_interval_milli_seconds) : 0u;

    {
        KIRQL irql;
        KeAcquireSpinLock(&g_mmrm_lock, &irql);
        mmrm_record_event_locked(MMRM_EVENT_INIT, STATUS_SUCCESS, g_mmrm.throttle_reason,
                                 g_mmrm.throttle_percentage, g_mmrm.updated,
                                 g_mmrm.last_peak_milli_amps, g_mmrm.tzt_temperature_deci_k,
                                 g_mmrm.threshold_low_deci_k, g_mmrm.threshold_high_deci_k);
        KeReleaseSpinLock(&g_mmrm_lock, irql);
    }

    InterlockedExchange(&g_mmrm_init, 2);
}

static BOOLEAN mmrm_update_throttle_with_peak_locked(_In_ ULONG peak_milli_amps)
{
    ULONG old_reason = g_mmrm.throttle_reason;
    ULONG old_percent = g_mmrm.throttle_percentage;

    ULONG reason = old_reason;
    ULONG percent = old_percent ? old_percent : 100;

    if (!g_mmrm.feature_enabled) {
        reason = 0;
        percent = 100;
    } else if (g_mmrm.tzt_above_threshold) {
        reason = 1;
        percent = g_mmrm.max_throttle_percentage;
    } else {
        if (peak_milli_amps >= g_mmrm.mitigation_start_threshold_milli_amps) {
            reason = 2;
            percent = g_mmrm.max_throttle_percentage;
        } else if (reason != 0 && peak_milli_amps <= g_mmrm.mitigation_stop_threshold_milli_amps) {
            reason = 0;
            percent = 100;
        }
    }

    if (g_mmrm.last_passive_limit != 0 && g_mmrm.last_passive_limit <= 100) {
        if (percent > g_mmrm.last_passive_limit) {
            percent = g_mmrm.last_passive_limit;
            if (reason == 0)
                reason = 3;
        }
    }

    percent = mmrm_clamp_percent(percent);

    g_mmrm.throttle_reason = reason;
    g_mmrm.throttle_percentage = percent;
    g_mmrm.last_peak_milli_amps = peak_milli_amps;

    if (reason != old_reason || percent != old_percent) {
        g_mmrm.updated = 1;
        g_mmrm.last_update_tick100ns = KeQueryInterruptTime();
        return TRUE;
    }

    return FALSE;
}

VOID mmrm_timer_fn(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (InterlockedCompareExchange(&g_mmrm_init, 0, 0) != 2)
        return;

    mmrm_refresh_runtime_profile();

    ULONG sensor_id = 0;
    ULONG period_ms = 1000;
    BOOLEAN active = FALSE;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    active = (g_mmrm.policy_wait_active && g_mmrm.feature_enabled);
    if (active) {
        sensor_id = g_mmrm.temperature_sensor_id;
        period_ms =
            g_mmrm.sampling_period_milli_seconds ? g_mmrm.sampling_period_milli_seconds : 1000;
    } else {
        g_mmrm.sampling_timer_running = FALSE;
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    if (!active)
        return;

    LONG temp_c = mmrm_read_sensor_temp_deci_c(sensor_id, (LONG)tsens_get_temp_deci_c());
    mmrm_update_from_thermal_sample(MAXLONG, MAXLONG, temp_c);

    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    active = (g_mmrm.policy_wait_active && g_mmrm.feature_enabled);
    if (active) {
        period_ms =
            g_mmrm.sampling_period_milli_seconds ? g_mmrm.sampling_period_milli_seconds : 1000;
    } else {
        g_mmrm.sampling_timer_running = FALSE;
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    if (active)
        mmrm_schedule_timer(period_ms);
}

NTSTATUS mmrm_init(_In_ WDFDRIVER driver)
{
    mmrm_ensure_init();

    if (g_mmrm_timer != NULL)
        return STATUS_SUCCESS;

    WDF_TIMER_CONFIG cfg;
    WDF_TIMER_CONFIG_INIT(&cfg, mmrm_timer_fn);

    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = driver;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;

    NTSTATUS status = WdfTimerCreate(&cfg, &attrs, &g_mmrm_timer);
    if (!NT_SUCCESS(status)) {
        KIRQL irql;
        KeAcquireSpinLock(&g_mmrm_lock, &irql);
        g_mmrm.feature_enabled = 0;
        g_mmrm.policy_wait_active = FALSE;
        g_mmrm.sampling_timer_running = FALSE;
        KeReleaseSpinLock(&g_mmrm_lock, irql);
    }

    return status;
}

VOID mmrm_deinit(VOID)
{
    if (g_mmrm_timer)
        (VOID) WdfTimerStop(g_mmrm_timer, FALSE);

    RtlZeroMemory(&g_mmrm, sizeof(g_mmrm));
    RtlZeroMemory(&g_mmrm_dsm, sizeof(g_mmrm_dsm));

    if (g_mmrm_timer) {
        WdfObjectDelete(g_mmrm_timer);
        g_mmrm_timer = NULL;
    }

    InterlockedExchange(&g_mmrm_init, 0);
}

VOID mmrm_attach_dev(_In_ WDFDEVICE dev)
{
    if (!dev)
        return;

    mmrm_ensure_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.attached_dev = dev;
    g_mmrm.last_dsm_refresh_tick100ns = 0;
    mmrm_record_event_locked(MMRM_EVENT_ATTACH_DEV, STATUS_SUCCESS, g_mmrm.throttle_reason,
                             g_mmrm.throttle_percentage, g_mmrm.updated,
                             g_mmrm.last_peak_milli_amps, g_mmrm.tzt_temperature_deci_k,
                             g_mmrm.threshold_low_deci_k, g_mmrm.threshold_high_deci_k);
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

VOID mmrm_detach_dev(_In_opt_ WDFDEVICE dev)
{
    if (InterlockedCompareExchange(&g_mmrm_init, 0, 0) != 2)
        return;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    if (dev == NULL || g_mmrm.attached_dev == dev)
        g_mmrm.attached_dev = NULL;
    mmrm_record_event_locked(MMRM_EVENT_DETACH_DEV, STATUS_SUCCESS, g_mmrm.throttle_reason,
                             g_mmrm.throttle_percentage, g_mmrm.updated,
                             g_mmrm.last_peak_milli_amps, g_mmrm.tzt_temperature_deci_k,
                             g_mmrm.threshold_low_deci_k, g_mmrm.threshold_high_deci_k);
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

VOID mmrm_capture_policy_req(_In_opt_ const mmrm_policy_req_t* req)
{
    if (!req)
        return;

    mmrm_ensure_init();
    mmrm_refresh_runtime_profile();

    ULONG reason;
    ULONG percent;
    ULONG updated;
    ULONG peak;
    LONG tzt_k;
    LONG low_k;
    LONG high_k;
    LONG cam;
    LONG vid;
    LONG disp;
    BOOLEAN changed;
    BOOLEAN invoke_callback = FALSE;
    mmrm_update_callback_t callback = NULL;
    PVOID callback_ctx = NULL;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.last_version = req->version;
    g_mmrm.last_activation_reasons = req->activation_reasons;
    g_mmrm.last_passive_limit = req->passive_limit;
    g_mmrm.last_thermal_standby = req->thermal_standby;
    g_mmrm.last_reserved = req->reserved;

    peak = mmrm_compute_peak_milli_amps_locked();
    changed = mmrm_update_throttle_with_peak_locked(peak);

    reason = g_mmrm.throttle_reason;
    percent = g_mmrm.throttle_percentage;
    updated = g_mmrm.updated;
    peak = g_mmrm.last_peak_milli_amps;
    tzt_k = g_mmrm.tzt_temperature_deci_k;
    low_k = g_mmrm.threshold_low_deci_k;
    high_k = g_mmrm.threshold_high_deci_k;
    cam = g_mmrm.camera_temp_deci_c;
    vid = g_mmrm.video_temp_deci_c;
    disp = g_mmrm.display_temp_deci_c;
    if (changed && g_mmrm.policy_wait_active && g_mmrm.update_callback) {
        invoke_callback = TRUE;
        callback = g_mmrm.update_callback;
        callback_ctx = g_mmrm.update_callback_ctx;
    }
    mmrm_record_event_locked(MMRM_EVENT_POLICY_REQ, STATUS_SUCCESS, reason, percent, updated, peak,
                             tzt_k, low_k, high_k);
    if (changed) {
        mmrm_record_event_locked(MMRM_EVENT_THROTTLE_CHANGED, STATUS_SUCCESS, reason, percent,
                                 updated, peak, tzt_k, low_k, high_k);
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    (VOID) registry_set_u_long(L"MmrmThrottleReason", reason);
    (VOID) registry_set_u_long(L"MmrmThrottlePercent", percent);
    (VOID) registry_set_u_long(L"MmrmPolicyUpdated", updated);
    mmrm_publish_state_snapshot(reason, percent, updated, peak, tzt_k, low_k, high_k, cam, vid,
                                disp);

    if (invoke_callback)
        callback(callback_ctx);
}

VOID mmrm_update_from_thermal_sample(_In_ LONG low_temp_deci_k, _In_ LONG high_temp_deci_k,
                                     _In_ LONG current_temp_deci_c)
{
    mmrm_ensure_init();
    mmrm_refresh_runtime_profile();

    LONG current_k = current_temp_deci_c + 2730;
    if (current_k < 0)
        current_k = 0;

    LONG fallback = mmrm_temp_deci_k_to_deci_c(current_k);
    LONG cam = mmrm_read_max_list_temp_deci_c(g_mmrm_camera_tsens_list,
                                              RTL_NUMBER_OF(g_mmrm_camera_tsens_list), fallback);
    LONG disp = mmrm_read_max_list_temp_deci_c(g_mmrm_display_tsens_list,
                                               RTL_NUMBER_OF(g_mmrm_display_tsens_list), fallback);
    LONG vid = fallback;
    ULONG peak = 0;

    ULONG reason;
    ULONG percent;
    ULONG updated;
    LONG low_k;
    LONG high_k;
    BOOLEAN changed;
    BOOLEAN invoke_callback = FALSE;
    mmrm_update_callback_t callback = NULL;
    PVOID callback_ctx = NULL;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    if (low_temp_deci_k != MAXLONG)
        g_mmrm.threshold_low_deci_k = low_temp_deci_k;
    if (high_temp_deci_k != MAXLONG)
        g_mmrm.threshold_high_deci_k = high_temp_deci_k;
    if (g_mmrm.threshold_low_deci_k != MAXLONG && g_mmrm.threshold_high_deci_k != MAXLONG &&
        g_mmrm.threshold_low_deci_k > g_mmrm.threshold_high_deci_k) {
        LONG t = g_mmrm.threshold_low_deci_k;
        g_mmrm.threshold_low_deci_k = g_mmrm.threshold_high_deci_k;
        g_mmrm.threshold_high_deci_k = t;
    }

    g_mmrm.tzt_temperature_deci_k = current_k;
    g_mmrm.camera_temp_deci_c = cam;
    g_mmrm.video_temp_deci_c = vid;
    g_mmrm.display_temp_deci_c = disp;

    if (!g_mmrm.tzt_above_threshold) {
        if (g_mmrm.threshold_high_deci_k != MAXLONG)
            g_mmrm.tzt_above_threshold = (current_k >= g_mmrm.threshold_high_deci_k);
        else if (g_mmrm.threshold_low_deci_k != MAXLONG)
            g_mmrm.tzt_above_threshold = (current_k >= g_mmrm.threshold_low_deci_k);
    } else {
        if (g_mmrm.threshold_low_deci_k != MAXLONG)
            g_mmrm.tzt_above_threshold = (current_k > g_mmrm.threshold_low_deci_k);
    }

    peak = mmrm_compute_peak_milli_amps_locked();
    changed = mmrm_update_throttle_with_peak_locked(peak);

    reason = g_mmrm.throttle_reason;
    percent = g_mmrm.throttle_percentage;
    updated = g_mmrm.updated;
    low_k = g_mmrm.threshold_low_deci_k;
    high_k = g_mmrm.threshold_high_deci_k;
    if (changed && g_mmrm.policy_wait_active && g_mmrm.update_callback) {
        invoke_callback = TRUE;
        callback = g_mmrm.update_callback;
        callback_ctx = g_mmrm.update_callback_ctx;
    }
    mmrm_record_event_locked(MMRM_EVENT_THERMAL_SAMPLE, STATUS_SUCCESS, reason, percent, updated,
                             peak, current_k, low_k, high_k);
    if (changed) {
        mmrm_record_event_locked(MMRM_EVENT_THROTTLE_CHANGED, STATUS_SUCCESS, reason, percent,
                                 updated, peak, current_k, low_k, high_k);
    }
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    (VOID) registry_set_u_long(L"MmrmThrottleReason", reason);
    (VOID) registry_set_u_long(L"MmrmThrottlePercent", percent);
    (VOID) registry_set_u_long(L"MmrmPolicyUpdated", updated);
    mmrm_publish_state_snapshot(reason, percent, updated, peak, current_k, low_k, high_k, cam, vid,
                                disp);

    if (invoke_callback)
        callback(callback_ctx);
}

VOID mmrm_reg_update_callback(_In_opt_ mmrm_update_callback_t callback, _In_opt_ PVOID ctx)
{
    mmrm_ensure_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.update_callback = callback;
    g_mmrm.update_callback_ctx = ctx;
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

VOID mmrm_set_policy_wait_active(_In_ BOOLEAN active)
{
    mmrm_ensure_init();

    BOOLEAN start = FALSE;
    BOOLEAN stop = FALSE;
    ULONG due_ms = 1;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.policy_wait_active = active ? TRUE : FALSE;

    if (g_mmrm.policy_wait_active && g_mmrm.feature_enabled) {
        if (!g_mmrm.sampling_timer_running) {
            g_mmrm.sampling_timer_running = TRUE;
            start = TRUE;
        }
    } else if (g_mmrm.sampling_timer_running) {
        g_mmrm.sampling_timer_running = FALSE;
        stop = TRUE;
    }

    if (start)
        due_ms = 1;

    mmrm_record_event_locked(MMRM_EVENT_POLICY_WAIT_STATE, STATUS_SUCCESS, g_mmrm.throttle_reason,
                             g_mmrm.throttle_percentage, g_mmrm.updated,
                             g_mmrm.last_peak_milli_amps, g_mmrm.tzt_temperature_deci_k,
                             g_mmrm.threshold_low_deci_k, g_mmrm.threshold_high_deci_k);

    KeReleaseSpinLock(&g_mmrm_lock, irql);

    if (stop && g_mmrm_timer)
        (VOID) WdfTimerStop(g_mmrm_timer, FALSE);
    if (start && g_mmrm_timer)
        mmrm_schedule_timer(due_ms);
}

VOID mmrm_get_throttle(_Out_opt_ PULONG reason_arg, _Out_opt_ PULONG percent_arg)
{
    mmrm_ensure_init();

    ULONG reason;
    ULONG percent;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    reason = g_mmrm.throttle_reason;
    percent = g_mmrm.throttle_percentage;
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    if (reason_arg)
        *reason_arg = reason;
    if (percent_arg)
        *percent_arg = percent;
}

VOID mmrm_get_snapshot(_Out_ mmrm_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    mmrm_ensure_init();

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 2;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    snapshot->feature_enabled = g_mmrm.feature_enabled;
    snapshot->reserved_power_milli_watts = g_mmrm.reserved_power_milli_watts;
    snapshot->sampling_period_milli_seconds = g_mmrm.sampling_period_milli_seconds;
    snapshot->max_throttle_percentage = g_mmrm.max_throttle_percentage;
    snapshot->mitigation_start_threshold_milli_amps = g_mmrm.mitigation_start_threshold_milli_amps;
    snapshot->mitigation_stop_threshold_milli_amps = g_mmrm.mitigation_stop_threshold_milli_amps;
    snapshot->temperature_sensor_id = g_mmrm.temperature_sensor_id;
    snapshot->video_encoding_state = g_mmrm.video_encoding_state;
    snapshot->video_decoding_state = g_mmrm.video_decoding_state;
    snapshot->last_total_power_milli_watts = g_mmrm.last_total_power_milli_watts;
    snapshot->last_peak_milli_amps = g_mmrm.last_peak_milli_amps;
    snapshot->throttle_reason = g_mmrm.throttle_reason;
    snapshot->throttle_percentage = g_mmrm.throttle_percentage;
    snapshot->updated = g_mmrm.updated;
    snapshot->last_passive_limit = g_mmrm.last_passive_limit;
    snapshot->last_activation_reasons = g_mmrm.last_activation_reasons;
    snapshot->last_version = g_mmrm.last_version;
    snapshot->policy_wait_active = g_mmrm.policy_wait_active ? 1u : 0u;
    snapshot->sampling_timer_running = g_mmrm.sampling_timer_running ? 1u : 0u;
    snapshot->dsm_flags = (g_mmrm.dsm_configured ? 1u : 0u) | ((g_mmrm.dsm_enabled ? 1u : 0u) << 1);
    snapshot->dsm_refresh_cnt = g_mmrm.dsm_refresh_cnt;
    snapshot->last_dsm_status = (ULONG)g_mmrm.last_dsm_status;
    snapshot->dsm_poll_interval_milli_seconds = g_mmrm.dsm_poll_interval_milli_seconds;
    snapshot->last_dsm_policy_item_cnt = g_mmrm.last_dsm_policy_item_cnt;
    snapshot->last_dsm_clock_item_cnt = g_mmrm.last_dsm_clock_item_cnt;
    snapshot->peak_voltage_milli_volts = g_mmrm.peak_voltage_milli_volts;
    snapshot->global_dyn_permille = g_mmrm.global_dyn_permille;
    snapshot->global_lkg_permille = g_mmrm.global_lkg_permille;
    snapshot->last_camera_power_milli_watts = g_mmrm.last_camera_power_milli_watts;
    snapshot->last_video_power_milli_watts = g_mmrm.last_video_power_milli_watts;
    snapshot->last_display_power_milli_watts = g_mmrm.last_display_power_milli_watts;
    snapshot->domain_cnt = RTL_NUMBER_OF(g_mmrm_clock_domains);
    snapshot->current_temp_deci_k = g_mmrm.tzt_temperature_deci_k;
    snapshot->threshold_low_deci_k = g_mmrm.threshold_low_deci_k;
    snapshot->threshold_high_deci_k = g_mmrm.threshold_high_deci_k;
    snapshot->camera_temp_deci_c = g_mmrm.camera_temp_deci_c;
    snapshot->video_temp_deci_c = g_mmrm.video_temp_deci_c;
    snapshot->display_temp_deci_c = g_mmrm.display_temp_deci_c;
    for (ULONG i = 0;
         i < RTL_NUMBER_OF(g_mmrm_clock_domains) && i < RTL_NUMBER_OF(snapshot->domains); i++) {
        snapshot->domains[i].freq_khz = g_mmrm.clock_freq_khz[i];
        snapshot->domains[i].power_milli_watts = g_mmrm.clock_power_milli_watts[i];
        snapshot->domains[i].flags = (g_mmrm.clock_enabled[i] ? 1u : 0u) |
                                     ((i == 9) ? (1u << 8) : 0u) | ((i == 10) ? (1u << 9) : 0u) |
                                     ((i == 11) ? (1u << 10) : 0u);
        snapshot->domains[i].dyn_permille = g_mmrm.clock_dyn_permille[i];
        snapshot->domains[i].lkg_permille = g_mmrm.clock_lkg_permille[i];
        snapshot->domains[i].temp_bias_deci_c = g_mmrm.clock_temp_bias_deci_c[i];
    }
    snapshot->event_sequence = g_mmrm.event_sequence;
    snapshot->history_cnt = (g_mmrm.event_sequence < MMRM_HISTORY_CAPACITY)
                                ? (ULONG)g_mmrm.event_sequence
                                : MMRM_HISTORY_CAPACITY;
    snapshot->history_dropped = (g_mmrm.event_sequence > MMRM_HISTORY_CAPACITY)
                                    ? (ULONG)(g_mmrm.event_sequence - MMRM_HISTORY_CAPACITY)
                                    : 0u;
    snapshot->last_event_code = g_mmrm.last_event_code;
    snapshot->last_event_status = g_mmrm.last_event_status;
    snapshot->last_event_reason = g_mmrm.last_event_reason;
    snapshot->last_event_percent = g_mmrm.last_event_percent;
    snapshot->last_event_updated = g_mmrm.last_event_updated;
    snapshot->last_event_peak_milli_amps = g_mmrm.last_event_peak_milli_amps;
    snapshot->last_event_temp_deci_k = g_mmrm.last_event_temp_deci_k;
    snapshot->last_event_low_temp_deci_k = g_mmrm.last_event_low_temp_deci_k;
    snapshot->last_event_high_temp_deci_k = g_mmrm.last_event_high_temp_deci_k;
    snapshot->last_event_tick100ns = g_mmrm.last_event_tick100ns;
    KeReleaseSpinLock(&g_mmrm_lock, irql);
}

ULONG mmrm_query_updated_flag(VOID)
{
    mmrm_ensure_init();

    ULONG updated;
    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    updated = g_mmrm.updated;
    KeReleaseSpinLock(&g_mmrm_lock, irql);
    return updated;
}

VOID mmrm_clear_updated_flag(VOID)
{
    mmrm_ensure_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);
    g_mmrm.updated = 0;
    KeReleaseSpinLock(&g_mmrm_lock, irql);

    (VOID) registry_set_u_long(L"MmrmPolicyUpdated", 0);
}

NTSTATUS mmrm_get_history(_Out_writes_to_(capacity, *cnt) mmrm_event_t* entries,
                          _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!entries || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    mmrm_ensure_init();

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_mmrm_lock, &irql);

    ULONGLONG seq = g_mmrm.event_sequence;
    if (seq == 0) {
        KeReleaseSpinLock(&g_mmrm_lock, irql);
        return STATUS_SUCCESS;
    }

    ULONGLONG avail = (seq < MMRM_HISTORY_CAPACITY) ? seq : MMRM_HISTORY_CAPACITY;
    ULONGLONG take = (capacity < avail) ? capacity : avail;
    ULONGLONG skip = avail - take;
    ULONGLONG start_seq = (seq - avail) + skip + 1ull;

    for (ULONGLONG i = 0; i < take; i++) {
        ULONGLONG current_seq = start_seq + i;
        ULONGLONG idx = (current_seq - 1ull) % MMRM_HISTORY_CAPACITY;
        entries[i] = g_mmrm.history[idx];
    }

    *cnt = (ULONG)take;
    *dropped = (ULONG)((seq > MMRM_HISTORY_CAPACITY) ? (seq - MMRM_HISTORY_CAPACITY + skip) : skip);

    KeReleaseSpinLock(&g_mmrm_lock, irql);
    return STATUS_SUCCESS;
}
