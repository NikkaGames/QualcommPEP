#include "deep_standby.h"
#include "peplog.h"
#include "resource_cache.h"
#include "stats.h"

#define DS_SENTINEL_BATT 105u
#define DS_WLAN_IOCTL 0x224004u
#define DS_MODEM_IOCTL 0x8FF82004u

static const GUID guid_acdc_power_src = {
    0x5D3E9A59u, 0xE9D5u, 0x4B00u, {0xA6u, 0xBDu, 0xFFu, 0x34u, 0xFFu, 0x51u, 0x65u, 0x48u}};

static const GUID guid_battery_percentage_remaining = {
    0xA7AD8041u, 0xB45Au, 0x4CAEu, {0x87u, 0xA3u, 0xEEu, 0xCBu, 0xB4u, 0x68u, 0xA9u, 0xE1u}};

static const GUID guid_console_display_state = {
    0x6FE69556u, 0x704Au, 0x47A0u, {0x8Fu, 0x24u, 0xC2u, 0x8Du, 0x93u, 0x6Fu, 0xDAu, 0x47u}};

static const GUID guid_global_user_presence = {
    0x786E8A1Du, 0xB427u, 0x4344u, {0x92u, 0x07u, 0x09u, 0xE7u, 0x0Bu, 0xDCu, 0xBEu, 0xA9u}};

typedef enum ds_user_presence_status {
    USER_PRESENT_STATE = 0,
    USER_NOT_PRESENT_STATE = 1,
    USER_INACTIVE_STATE = 2,
    USER_UNKNOWN_STATE = 0xF,
} ds_user_presence_status_t;

typedef enum ds_power_src_status {
    AC_POWER_SRC = 0,
    DC_POWER_SRC = 1,
    HOT_POWER_SRC = 2,
    UNKNOWN_POWER_SRC = 0xF,
} ds_power_src_status_t;

typedef enum ds_screen_status {
    SCREEN_ON_STATE = 0,
    SCREEN_OFF_STATE = 1,
    SCREEN_DIMMED_STATE = 2,
    SCREEN_UNKNOWN_STATE = 0xF,
} ds_screen_status_t;

typedef enum ds_ms_on_dc_pwr_state {
    MS_ON_DC_PWR_OFF_STATE = 0,
    MS_ON_DC_PWR_ON_STATE = 1,
} ds_ms_on_dc_pwr_state_t;

typedef enum ds_state {
    DEEP_STANDBY_OFF_STATE = 0,
    DEEP_STANDBY_ON_STATE = 1,
} ds_state_t;

#pragma pack(push, 1)

typedef struct deep_standby_notify {
    ULONG version;
    ULONG deep_standby_state;
} deep_standby_notify_t;

typedef struct wlansleep_set_status_version_input_buf {
    ULONG version;
    ULONG wlan_enable;
} wlansleep_set_status_version_input_buf_t;

typedef struct deepstandby_requested_mode {
    ULONG deep_standby_mdm_req;
} deepstandby_requested_mode_t;

#pragma pack(pop)

typedef struct deep_standby_ctx {
    WDFDRIVER driver;
    WDFDEVICE dev;

    WDFWAITLOCK lock;
    WDFTIMER timer;

    PVOID screen_on_handle;
    PVOID user_presence_handle;
    PVOID power_src_handle;
    PVOID battery_percentage_handle;

    PCALLBACK_OBJECT callback_obj;
    deep_standby_notify_t* notify_st;

    WDFIOTARGET wlan_target;
    WDFIOTARGET modem_target;

    wlansleep_set_status_version_input_buf_t* wlan_set_status;
    deepstandby_requested_mode_t* modem_set_status;

    ULONG feature_enabled;
    ULONG callbacks_enabled;
    ULONG modem_enabled;
    ULONG wlan_enabled;
    ULONG max_batt_drain_pct;
    ULONG timeout_seconds;

    ds_power_src_status_t power_src;
    ds_screen_status_t screen_state;
    ds_user_presence_status_t user_state;

    ds_ms_on_dc_pwr_state_t ms_on_dc_pwr;
    ds_state_t deep_standby_state;

    ULONG timer_expired;
    ULONG batt_drain_exceeded_limit;

    ULONG batt_drain_since_tmr_start;
    ULONG tmr_starting_batt_pct;
    ULONG current_batt_pct;

    ULONGLONG tmr_start_time;
    ULONGLONG deep_standby_deadline;

    volatile LONG modem_req;

    ULONG evaluate_cnt;
    ULONG callback_cnt;
    ULONG state_transition_cnt;
    ULONG enter_cnt;
    ULONG exit_cnt;
    ULONG wlan_ioctl_errors;
    ULONG modem_ioctl_errors;
    ULONG last_wlan_ioctl_status;
    ULONG last_modem_ioctl_status;
    ULONG last_event_code;
    ULONG last_event_status;
    ULONG last_event_aux0;
    ULONG last_event_aux1;
    ULONG history_head;
    ULONG history_cnt;
    ULONG history_dropped;
    deep_standby_history_entry_t history[DEEP_STANDBY_HISTORY_CAPACITY];
    ULONGLONG last_evaluate_tick100ns;
    ULONGLONG last_transition_tick100ns;
} deep_standby_ctx_t;

static deep_standby_ctx_t g_ds;

static ULONGLONG ds_now_interrupt_time100ns(VOID)
{
    return KeQueryInterruptTime();
}

static VOID ds_log(_In_ ULONG eid, _In_ ULONG code, _In_ NTSTATUS st, _In_ ULONG a0, _In_ ULONG a1,
                   _In_ ULONG a2, _In_ ULONG a3)
{
    pep_log_write(eid, code, st, a0, a1, a2, a3);
}

static VOID ds_record_event_locked(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG aux0,
                                   _In_ ULONG aux1)
{
    ULONG idx = g_ds.history_head % DEEP_STANDBY_HISTORY_CAPACITY;
    deep_standby_history_entry_t* e = &g_ds.history[idx];
    RtlZeroMemory(e, sizeof(*e));
    e->tick100ns = ds_now_interrupt_time100ns();
    e->event_code = event_code;
    e->status = (ULONG)status;
    e->prev_ms_on_dc_pwr = (ULONG)g_ds.ms_on_dc_pwr;
    e->curr_ms_on_dc_pwr = (ULONG)g_ds.ms_on_dc_pwr;
    e->prev_deep_standby_state = (ULONG)g_ds.deep_standby_state;
    e->curr_deep_standby_state = (ULONG)g_ds.deep_standby_state;
    e->aux0 = aux0;
    e->aux1 = aux1;

    if (event_code == DEEP_STANDBY_EVENT_MS_ON_DC_CHANGE ||
        event_code == DEEP_STANDBY_EVENT_EVALUATE) {
        e->prev_ms_on_dc_pwr = aux0;
        e->curr_ms_on_dc_pwr = aux1;
    }
    if (event_code == DEEP_STANDBY_EVENT_STATE_CHANGE) {
        e->prev_deep_standby_state = aux0;
        e->curr_deep_standby_state = aux1;
    }

    g_ds.history_head = (idx + 1) % DEEP_STANDBY_HISTORY_CAPACITY;
    if (g_ds.history_cnt < DEEP_STANDBY_HISTORY_CAPACITY) {
        g_ds.history_cnt++;
    } else {
        g_ds.history_dropped++;
    }

    g_ds.last_event_code = event_code;
    g_ds.last_event_status = (ULONG)status;
    g_ds.last_event_aux0 = aux0;
    g_ds.last_event_aux1 = aux1;

    stats_record_event(STATS_EVENT_DEEP_STANDBY_BASE + (event_code & 0xFFu), status, aux0, aux1,
                       (ULONG)g_ds.deep_standby_state, (ULONG)g_ds.ms_on_dc_pwr);
}

static VOID ds_publish_snapshot_locked(VOID)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x30445344u;
    k.sub_type = 0;
    v.value0 = (g_ds.feature_enabled ? 1u : 0u) | ((g_ds.callbacks_enabled ? 1u : 0u) << 1) |
               ((g_ds.modem_enabled ? 1u : 0u) << 2) | ((g_ds.wlan_enabled ? 1u : 0u) << 3);
    v.value1 = ((ULONG)g_ds.power_src & 0xFFu) | (((ULONG)g_ds.screen_state & 0xFFu) << 8) |
               (((ULONG)g_ds.user_state & 0xFFu) << 16) |
               (((ULONG)g_ds.ms_on_dc_pwr & 0xFFu) << 24);
    v.value2 = ((ULONG)g_ds.deep_standby_state & 0xFFu) | ((g_ds.timer_expired ? 1u : 0u) << 8) |
               ((g_ds.batt_drain_exceeded_limit ? 1u : 0u) << 16);
    v.value3 = (ULONG)InterlockedCompareExchange(&g_ds.modem_req, 0, 0);
    (VOID) res_set(k, v);

    k.res_id = 0x31445344u;
    k.sub_type = 0;
    v.value0 = g_ds.max_batt_drain_pct;
    v.value1 = g_ds.timeout_seconds;
    v.value2 = g_ds.batt_drain_since_tmr_start;
    v.value3 = g_ds.current_batt_pct;
    (VOID) res_set(k, v);

    k.res_id = 0x32445344u;
    k.sub_type = 0;
    v.value0 = g_ds.evaluate_cnt;
    v.value1 = g_ds.callback_cnt;
    v.value2 = g_ds.state_transition_cnt;
    v.value3 = (g_ds.enter_cnt & 0xFFFFu) | ((g_ds.exit_cnt & 0xFFFFu) << 16);
    (VOID) res_set(k, v);

    k.res_id = 0x33445344u;
    k.sub_type = 0;
    v.value0 = g_ds.wlan_ioctl_errors;
    v.value1 = g_ds.modem_ioctl_errors;
    v.value2 = g_ds.last_wlan_ioctl_status;
    v.value3 = g_ds.last_modem_ioctl_status;
    (VOID) res_set(k, v);

    k.res_id = 0x34445344u;
    k.sub_type = 0;
    v.value0 = g_ds.history_cnt;
    v.value1 = g_ds.history_dropped;
    v.value2 = g_ds.last_event_code;
    v.value3 = g_ds.last_event_status;
    (VOID) res_set(k, v);

    k.res_id = 0x35445344u;
    ULONG emit = (g_ds.history_cnt < 8u) ? g_ds.history_cnt : 8u;
    for (ULONG i = 0; i < emit; i++) {
        ULONG idx = (g_ds.history_head + DEEP_STANDBY_HISTORY_CAPACITY - 1u - i) %
                    DEEP_STANDBY_HISTORY_CAPACITY;
        const deep_standby_history_entry_t* e = &g_ds.history[idx];
        k.sub_type = i;
        v.value0 = e->event_code;
        v.value1 = e->status;
        v.value2 = ((e->prev_ms_on_dc_pwr & 0xFFu) | ((e->curr_ms_on_dc_pwr & 0xFFu) << 8) |
                    ((e->prev_deep_standby_state & 0xFFu) << 16) |
                    ((e->curr_deep_standby_state & 0xFFu) << 24));
        v.value3 = (ULONG)e->tick100ns;
        (VOID) res_set(k, v);
    }
}

static NTSTATUS ds_open_io_target_by_name(_In_ WDFDEVICE dev, _In_ PCWSTR name,
                                          _Out_ WDFIOTARGET* out_target)
{
    if (!dev || !name || !out_target)
        return STATUS_INVALID_PARAMETER;

    *out_target = NULL;

    NTSTATUS status = WdfIoTargetCreate(dev, WDF_NO_OBJECT_ATTRIBUTES, out_target);
    if (!NT_SUCCESS(status))
        return status;

    UNICODE_STRING us;
    RtlInitUnicodeString(&us, name);

    WDF_IO_TARGET_OPEN_PARAMS open_params;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open_params, &us, 0x20000u);

    status = WdfIoTargetOpen(*out_target, &open_params);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(*out_target);
        *out_target = NULL;
        return status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS ds_send_ioctl_sync(_In_ WDFIOTARGET target, _In_ ULONG ioctl,
                                   _In_reads_bytes_(in_bytes) PVOID in_buf, _In_ size_t in_bytes)
{
    if (!target)
        return STATUS_INVALID_PARAMETER;

    WDFREQUEST req = NULL;
    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = target;

    NTSTATUS status = WdfRequestCreate(&attrs, target, &req);
    if (!NT_SUCCESS(status))
        return status;

    WDF_MEMORY_DESCRIPTOR mem;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&mem, in_buf, (ULONG)in_bytes);

    status = WdfIoTargetSendIoctlSynchronously(target, req, ioctl, &mem, NULL, NULL, NULL);

    WdfObjectDelete(req);
    return status;
}

static NTSTATUS ds_toggle_wlan_locked(_In_ BOOLEAN enable)
{
    if (!g_ds.wlan_enabled)
        return STATUS_SUCCESS;

    if (!g_ds.wlan_target) {
        NTSTATUS s = ds_open_io_target_by_name(g_ds.dev, L"\\??\\WlanSleepMgr", &g_ds.wlan_target);
        if (!NT_SUCCESS(s)) {
            g_ds.wlan_enabled = 0;
            g_ds.last_wlan_ioctl_status = (ULONG)s;
            g_ds.wlan_ioctl_errors++;
            ds_record_event_locked(DEEP_STANDBY_EVENT_WLAN_IOCTL, s, 0, 0);
            ds_log(0xD510, DS_WLAN_IOCTL, s, 0, 0, 0, 0);
            return s;
        }
    }

    if (!g_ds.wlan_set_status)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_ds.wlan_set_status, sizeof(*g_ds.wlan_set_status));
    g_ds.wlan_set_status->version = 0;
    g_ds.wlan_set_status->wlan_enable = enable ? 1u : 0u;

    NTSTATUS status = ds_send_ioctl_sync(g_ds.wlan_target, DS_WLAN_IOCTL, g_ds.wlan_set_status,
                                         sizeof(*g_ds.wlan_set_status));
    g_ds.last_wlan_ioctl_status = (ULONG)status;
    if (!NT_SUCCESS(status))
        g_ds.wlan_ioctl_errors++;
    ds_record_event_locked(DEEP_STANDBY_EVENT_WLAN_IOCTL, status, enable ? 1u : 0u,
                           g_ds.deep_standby_state);
    ds_log(0xD511, DS_WLAN_IOCTL, status, g_ds.deep_standby_state, enable, 0, 0);
    return status;
}

static NTSTATUS ds_toggle_modem_locked(_In_ ULONG mode)
{
    if (!g_ds.modem_enabled)
        return STATUS_SUCCESS;

    if (!g_ds.modem_target) {
        NTSTATUS s = ds_open_io_target_by_name(g_ds.dev, QCPEP_DOS_SYMLINK_GLOBAL_DEEPSTANDBY,
                                               &g_ds.modem_target);
        if (!NT_SUCCESS(s)) {
            g_ds.modem_enabled = 0;
            g_ds.last_modem_ioctl_status = (ULONG)s;
            g_ds.modem_ioctl_errors++;
            ds_record_event_locked(DEEP_STANDBY_EVENT_MODEM_IOCTL, s, 0, 0);
            ds_log(0xD520, DS_MODEM_IOCTL, s, 0, 0, 0, 0);
            return s;
        }
    }

    if (!g_ds.modem_set_status)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(g_ds.modem_set_status, sizeof(*g_ds.modem_set_status));
    g_ds.modem_set_status->deep_standby_mdm_req = mode;

    NTSTATUS status = ds_send_ioctl_sync(g_ds.modem_target, DS_MODEM_IOCTL, g_ds.modem_set_status,
                                         sizeof(*g_ds.modem_set_status));
    g_ds.last_modem_ioctl_status = (ULONG)status;
    if (!NT_SUCCESS(status))
        g_ds.modem_ioctl_errors++;
    ds_record_event_locked(DEEP_STANDBY_EVENT_MODEM_IOCTL, status, mode, g_ds.deep_standby_state);
    ds_log(0xD521, DS_MODEM_IOCTL, status, g_ds.deep_standby_state, mode, 0, 0);
    return status;
}

static VOID ds_change_state_locked(VOID)
{
    ds_state_t prev = g_ds.deep_standby_state;

    g_ds.deep_standby_state = (g_ds.ms_on_dc_pwr == MS_ON_DC_PWR_ON_STATE &&
                               (g_ds.timer_expired || g_ds.batt_drain_exceeded_limit))
                                  ? DEEP_STANDBY_ON_STATE
                                  : DEEP_STANDBY_OFF_STATE;

    if (g_ds.timer_expired == 0)
        (VOID) WdfTimerStop(g_ds.timer, FALSE);

    if (g_ds.deep_standby_state == prev) {
        ds_record_event_locked(DEEP_STANDBY_EVENT_STATE_CHANGE, STATUS_SUCCESS, prev,
                               g_ds.deep_standby_state);
        ds_publish_snapshot_locked();
        return;
    }

    g_ds.state_transition_cnt++;
    g_ds.last_transition_tick100ns = ds_now_interrupt_time100ns();
    if (g_ds.deep_standby_state == DEEP_STANDBY_ON_STATE)
        g_ds.enter_cnt++;
    else
        g_ds.exit_cnt++;

    ds_record_event_locked(DEEP_STANDBY_EVENT_STATE_CHANGE, STATUS_SUCCESS, prev,
                           g_ds.deep_standby_state);

    if (g_ds.callbacks_enabled && g_ds.callback_obj && g_ds.notify_st) {
        g_ds.notify_st->deep_standby_state = (ULONG)g_ds.deep_standby_state;
        ExNotifyCallback(g_ds.callback_obj, g_ds.notify_st, NULL);
    }

    if (g_ds.wlan_enabled)
        (VOID) ds_toggle_wlan_locked(g_ds.deep_standby_state == DEEP_STANDBY_OFF_STATE);

    if (g_ds.modem_enabled) {
        ULONG req = (g_ds.deep_standby_state == DEEP_STANDBY_ON_STATE) ? 1u : 0u;
        InterlockedExchange(&g_ds.modem_req, (LONG)req);
        (VOID) registry_set_u_long(L"DeepStandbyModemReq", req);
        (VOID) ds_toggle_modem_locked(req);
    }

    if (g_ds.deep_standby_state == DEEP_STANDBY_ON_STATE)
        g_ds.deep_standby_deadline = ds_now_interrupt_time100ns();
    else
        g_ds.deep_standby_deadline = (ULONGLONG)-1;

    ds_publish_snapshot_locked();
    ds_log(0xD530, 0, STATUS_SUCCESS, prev, g_ds.deep_standby_state, g_ds.timer_expired,
           g_ds.batt_drain_exceeded_limit);
}

static VOID ds_evaluate_locked(VOID)
{
    if (!g_ds.feature_enabled)
        return;

    g_ds.evaluate_cnt++;
    g_ds.last_evaluate_tick100ns = ds_now_interrupt_time100ns();

    ds_ms_on_dc_pwr_state_t prev = g_ds.ms_on_dc_pwr;

    BOOLEAN ms_on_dc =
        (g_ds.screen_state == SCREEN_OFF_STATE) &&
        (g_ds.user_state == USER_NOT_PRESENT_STATE || g_ds.user_state == USER_INACTIVE_STATE) &&
        (g_ds.power_src == DC_POWER_SRC);

    g_ds.ms_on_dc_pwr = ms_on_dc ? MS_ON_DC_PWR_ON_STATE : MS_ON_DC_PWR_OFF_STATE;
    ds_record_event_locked(DEEP_STANDBY_EVENT_EVALUATE, STATUS_SUCCESS, prev, g_ds.ms_on_dc_pwr);

    if (g_ds.tmr_starting_batt_pct != DS_SENTINEL_BATT &&
        g_ds.tmr_starting_batt_pct >= g_ds.current_batt_pct &&
        g_ds.current_batt_pct != DS_SENTINEL_BATT)
        g_ds.batt_drain_since_tmr_start = g_ds.tmr_starting_batt_pct - g_ds.current_batt_pct;

    if (g_ds.ms_on_dc_pwr == prev) {
        if (g_ds.ms_on_dc_pwr == MS_ON_DC_PWR_ON_STATE &&
            g_ds.batt_drain_since_tmr_start >= g_ds.max_batt_drain_pct) {
            g_ds.batt_drain_exceeded_limit = 1;
            ds_change_state_locked();
        } else {
            ds_publish_snapshot_locked();
        }
        return;
    }

    ds_record_event_locked(DEEP_STANDBY_EVENT_MS_ON_DC_CHANGE, STATUS_SUCCESS, prev,
                           g_ds.ms_on_dc_pwr);
    if (g_ds.ms_on_dc_pwr == MS_ON_DC_PWR_ON_STATE) {
        g_ds.tmr_starting_batt_pct = g_ds.current_batt_pct;
        g_ds.tmr_start_time = ds_now_interrupt_time100ns();
        g_ds.batt_drain_since_tmr_start = 0;
        g_ds.batt_drain_exceeded_limit = 0;
        g_ds.timer_expired = 0;

        ULONGLONG abs_due = g_ds.tmr_start_time + (ULONGLONG)g_ds.timeout_seconds * 10000000ULL;
        g_ds.deep_standby_deadline = abs_due;

        (VOID) WdfTimerStart(g_ds.timer, (LONGLONG)abs_due);
        ds_record_event_locked(DEEP_STANDBY_EVENT_TIMER_START, STATUS_SUCCESS, g_ds.timeout_seconds,
                               g_ds.current_batt_pct);
        ds_publish_snapshot_locked();
        ds_log(0xD540, 0, STATUS_SUCCESS, prev, g_ds.ms_on_dc_pwr, g_ds.timeout_seconds, 0);
    } else {
        g_ds.tmr_starting_batt_pct = DS_SENTINEL_BATT;
        g_ds.tmr_start_time = (ULONGLONG)-1;
        g_ds.batt_drain_since_tmr_start = 0;
        g_ds.batt_drain_exceeded_limit = 0;
        g_ds.timer_expired = 0;
        ds_change_state_locked();
        ds_publish_snapshot_locked();
        ds_log(0xD541, 0, STATUS_SUCCESS, prev, g_ds.ms_on_dc_pwr, 0, 0);
    }
}

static VOID ds_timer_expiry(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (!g_ds.lock)
        return;

    WdfWaitLockAcquire(g_ds.lock, NULL);
    g_ds.timer_expired = 1;
    ds_record_event_locked(DEEP_STANDBY_EVENT_TIMER_EXPIRY, STATUS_SUCCESS, 1, 0);
    ds_change_state_locked();
    WdfWaitLockRelease(g_ds.lock);
}

static NTSTATUS ds_on_screen(_In_ const GUID* guid, _In_reads_bytes_opt_(value_length) PVOID value,
                             _In_ ULONG value_length, _In_opt_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(guid);
    UNREFERENCED_PARAMETER(ctx);

    if (!value || value_length < sizeof(ULONG))
        return STATUS_SUCCESS;

    ULONG v = *(ULONG*)value;

    ds_screen_status_t st = SCREEN_UNKNOWN_STATE;
    if (v == 0)
        st = SCREEN_ON_STATE;
    else if (v == 1)
        st = SCREEN_OFF_STATE;
    else if (v == 2)
        st = SCREEN_DIMMED_STATE;

    if (g_ds.lock) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        g_ds.callback_cnt++;
        g_ds.screen_state = st;
        ds_record_event_locked(DEEP_STANDBY_EVENT_CALLBACK_SCREEN, STATUS_SUCCESS, v, st);
        ds_evaluate_locked();
        WdfWaitLockRelease(g_ds.lock);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS ds_on_user_presence(_In_ const GUID* guid,
                                    _In_reads_bytes_opt_(value_length) PVOID value,
                                    _In_ ULONG value_length, _In_opt_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(guid);
    UNREFERENCED_PARAMETER(ctx);

    if (!value || value_length < sizeof(ULONG))
        return STATUS_SUCCESS;

    ULONG v = *(ULONG*)value;

    ds_user_presence_status_t st = USER_UNKNOWN_STATE;
    if (v == 0)
        st = USER_PRESENT_STATE;
    else if (v == 1)
        st = USER_NOT_PRESENT_STATE;
    else if (v == 2)
        st = USER_INACTIVE_STATE;

    if (g_ds.lock) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        g_ds.callback_cnt++;
        g_ds.user_state = st;
        ds_record_event_locked(DEEP_STANDBY_EVENT_CALLBACK_USER, STATUS_SUCCESS, v, st);
        ds_evaluate_locked();
        WdfWaitLockRelease(g_ds.lock);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS ds_on_power_src(_In_ const GUID* guid,
                                _In_reads_bytes_opt_(value_length) PVOID value,
                                _In_ ULONG value_length, _In_opt_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(guid);
    UNREFERENCED_PARAMETER(ctx);

    if (!value || value_length < sizeof(ULONG))
        return STATUS_SUCCESS;

    ULONG v = *(ULONG*)value;

    ds_power_src_status_t st = UNKNOWN_POWER_SRC;
    if (v == 0)
        st = AC_POWER_SRC;
    else if (v == 1)
        st = DC_POWER_SRC;
    else if (v == 2)
        st = HOT_POWER_SRC;

    if (g_ds.lock) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        g_ds.callback_cnt++;
        g_ds.power_src = st;
        ds_record_event_locked(DEEP_STANDBY_EVENT_CALLBACK_POWER, STATUS_SUCCESS, v, st);
        ds_evaluate_locked();
        WdfWaitLockRelease(g_ds.lock);
    }
    return STATUS_SUCCESS;
}

static NTSTATUS ds_on_battery_pct(_In_ const GUID* guid,
                                  _In_reads_bytes_opt_(value_length) PVOID value,
                                  _In_ ULONG value_length, _In_opt_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(guid);
    UNREFERENCED_PARAMETER(ctx);

    if (!value || value_length < sizeof(ULONG))
        return STATUS_SUCCESS;

    ULONG v = *(ULONG*)value;
    if (v > 100)
        v = DS_SENTINEL_BATT;

    if (g_ds.lock) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        g_ds.callback_cnt++;
        g_ds.current_batt_pct = v;
        ds_record_event_locked(DEEP_STANDBY_EVENT_CALLBACK_BATTERY, STATUS_SUCCESS, v,
                               g_ds.batt_drain_since_tmr_start);
        ds_evaluate_locked();
        WdfWaitLockRelease(g_ds.lock);
    }
    return STATUS_SUCCESS;
}

NTSTATUS deep_standby_init(_In_ WDFDRIVER driver)
{
    RtlZeroMemory(&g_ds, sizeof(g_ds));
    g_ds.driver = driver;

    g_ds.feature_enabled = registry_query_u_long(L"DeepStandbyFeatureEnabled", 0);
    g_ds.callbacks_enabled = registry_query_u_long(L"DeepStandbyCallbacksEnabled", 1);
    g_ds.modem_enabled = registry_query_u_long(L"DeepStandbyModemEnabled", 1);
    g_ds.wlan_enabled = registry_query_u_long(L"DeepStandbyWlanEnabled", 1);
    g_ds.max_batt_drain_pct = registry_query_u_long(L"DeepStandbyMaxBattDrainPercentage", 3);
    g_ds.timeout_seconds = registry_query_u_long(L"DeepStandbyTimeoutSeconds", 14400);

    g_ds.power_src = UNKNOWN_POWER_SRC;
    g_ds.screen_state = SCREEN_UNKNOWN_STATE;
    g_ds.user_state = USER_UNKNOWN_STATE;
    g_ds.ms_on_dc_pwr = MS_ON_DC_PWR_OFF_STATE;
    g_ds.deep_standby_state = DEEP_STANDBY_OFF_STATE;

    g_ds.tmr_starting_batt_pct = DS_SENTINEL_BATT;
    g_ds.current_batt_pct = DS_SENTINEL_BATT;
    g_ds.tmr_start_time = (ULONGLONG)-1;
    g_ds.deep_standby_deadline = (ULONGLONG)-1;
    g_ds.last_wlan_ioctl_status = STATUS_SUCCESS;
    g_ds.last_modem_ioctl_status = STATUS_SUCCESS;

    g_ds.modem_req = (LONG)registry_query_u_long(L"DeepStandbyModemReq", 0);
    ds_record_event_locked(DEEP_STANDBY_EVENT_INIT, STATUS_SUCCESS, g_ds.feature_enabled,
                           g_ds.modem_req);

    return STATUS_SUCCESS;
}

static VOID ds_unregister_power_callbacks(VOID)
{
    if (g_ds.screen_on_handle) {
        PoUnregisterPowerSettingCallback(g_ds.screen_on_handle);
        g_ds.screen_on_handle = NULL;
    }
    if (g_ds.user_presence_handle) {
        PoUnregisterPowerSettingCallback(g_ds.user_presence_handle);
        g_ds.user_presence_handle = NULL;
    }
    if (g_ds.power_src_handle) {
        PoUnregisterPowerSettingCallback(g_ds.power_src_handle);
        g_ds.power_src_handle = NULL;
    }
    if (g_ds.battery_percentage_handle) {
        PoUnregisterPowerSettingCallback(g_ds.battery_percentage_handle);
        g_ds.battery_percentage_handle = NULL;
    }
}

VOID deep_standby_detach_dev(VOID)
{
    if (g_ds.lock)
        WdfWaitLockAcquire(g_ds.lock, NULL);

    ds_record_event_locked(DEEP_STANDBY_EVENT_DETACH, STATUS_SUCCESS, 0, 0);
    ds_publish_snapshot_locked();

    ds_unregister_power_callbacks();

    if (g_ds.timer)
        (VOID) WdfTimerStop(g_ds.timer, TRUE);

    if (g_ds.wlan_target) {
        WdfObjectDelete(g_ds.wlan_target);
        g_ds.wlan_target = NULL;
    }
    if (g_ds.modem_target) {
        WdfObjectDelete(g_ds.modem_target);
        g_ds.modem_target = NULL;
    }

    if (g_ds.wlan_set_status) {
        ExFreePoolWithTag(g_ds.wlan_set_status, POOL_TAG);
        g_ds.wlan_set_status = NULL;
    }
    if (g_ds.modem_set_status) {
        ExFreePoolWithTag(g_ds.modem_set_status, POOL_TAG);
        g_ds.modem_set_status = NULL;
    }

    if (g_ds.notify_st) {
        ExFreePoolWithTag(g_ds.notify_st, POOL_TAG);
        g_ds.notify_st = NULL;
    }

    if (g_ds.callback_obj) {
        ObDereferenceObject(g_ds.callback_obj);
        g_ds.callback_obj = NULL;
    }

    g_ds.dev = NULL;

    if (g_ds.lock)
        WdfWaitLockRelease(g_ds.lock);
}

VOID deep_standby_deinit(VOID)
{
    deep_standby_detach_dev();
    if (g_ds.lock) {
        WdfObjectDelete(g_ds.lock);
        g_ds.lock = NULL;
    }
    RtlZeroMemory(&g_ds, sizeof(g_ds));
}

static NTSTATUS ds_create_callback_object_if_needed(VOID)
{
    if (!g_ds.callbacks_enabled)
        return STATUS_SUCCESS;

    if (g_ds.callback_obj)
        return STATUS_SUCCESS;

    UNICODE_STRING name;
    RtlInitUnicodeString(&name, L"\\Callback\\QcDeepStandby");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_KERNEL_HANDLE, NULL, NULL);

    NTSTATUS status = ExCreateCallback(&g_ds.callback_obj, &oa, TRUE, TRUE);
    if (!NT_SUCCESS(status))
        return status;

    if (!g_ds.notify_st) {
        g_ds.notify_st = (deep_standby_notify_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(deep_standby_notify_t), POOL_TAG);
        if (!g_ds.notify_st)
            return STATUS_INSUFFICIENT_RESOURCES;

        g_ds.notify_st->version = 1;
        g_ds.notify_st->deep_standby_state = 0;
    }

    return STATUS_SUCCESS;
}

NTSTATUS deep_standby_attach_dev(_In_ WDFDEVICE dev)
{
    if (!dev)
        return STATUS_INVALID_PARAMETER;

    if (g_ds.dev)
        return g_ds.dev == dev ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;

    /* an earlier WDF creation failure must not make a subsequent attach incorrectly report
     * success. */

    if (!g_ds.lock) {
        WDF_OBJECT_ATTRIBUTES a;
        WDF_OBJECT_ATTRIBUTES_INIT(&a);
        a.ParentObject = dev;
        /* the lock is acquired only at PASSIVE_LEVEL by our callers. */

        NTSTATUS status = WdfWaitLockCreate(&a, &g_ds.lock);
        if (!NT_SUCCESS(status)) {
            ds_log(0xD5E1u, 0, status, 0, 0, 0, 0);
            g_ds.feature_enabled = 0;
            return status;
        }
    }

    if (!g_ds.timer) {
        WDF_TIMER_CONFIG tc;
        WDF_TIMER_CONFIG_INIT(&tc, ds_timer_expiry);
        tc.TolerableDelay = 300000;

        WDF_OBJECT_ATTRIBUTES a;
        WDF_OBJECT_ATTRIBUTES_INIT(&a);
        a.ParentObject = dev;
        a.ExecutionLevel = WdfExecutionLevelPassive;
        a.SynchronizationScope = WdfSynchronizationScopeNone;

        tc.AutomaticSerialization = FALSE;
        NTSTATUS status = WdfTimerCreate(&tc, &a, &g_ds.timer);
        if (!NT_SUCCESS(status)) {
            ds_log(0xD5E2u, 0, status, 0, 0, 0, 0);
            g_ds.feature_enabled = 0;
            return status;
        }
    }

    if (!g_ds.wlan_set_status)
        g_ds.wlan_set_status = (wlansleep_set_status_version_input_buf_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(wlansleep_set_status_version_input_buf_t), POOL_TAG);
    if (!g_ds.modem_set_status)
        g_ds.modem_set_status = (deepstandby_requested_mode_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(deepstandby_requested_mode_t), POOL_TAG);

    if ((g_ds.wlan_enabled && !g_ds.wlan_set_status) ||
        (g_ds.modem_enabled && !g_ds.modem_set_status))
        return STATUS_INSUFFICIENT_RESOURCES;

    (VOID) ds_create_callback_object_if_needed();
    g_ds.dev = dev;

    PDEVICE_OBJECT pdo = WdfDeviceWdmGetDeviceObject(dev);
    if (pdo) {
        (VOID) PoRegisterPowerSettingCallback(pdo, &guid_console_display_state, ds_on_screen, NULL,
                                              &g_ds.screen_on_handle);
        (VOID) PoRegisterPowerSettingCallback(pdo, &guid_global_user_presence, ds_on_user_presence,
                                              NULL, &g_ds.user_presence_handle);
        (VOID) PoRegisterPowerSettingCallback(pdo, &guid_acdc_power_src, ds_on_power_src, NULL,
                                              &g_ds.power_src_handle);
        (VOID) PoRegisterPowerSettingCallback(pdo, &guid_battery_percentage_remaining,
                                              ds_on_battery_pct, NULL,
                                              &g_ds.battery_percentage_handle);
    }

    ds_log(0xD500, 0, STATUS_SUCCESS, g_ds.feature_enabled, g_ds.callbacks_enabled,
           g_ds.modem_enabled, g_ds.wlan_enabled);

    if (g_ds.lock) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        ds_record_event_locked(DEEP_STANDBY_EVENT_ATTACH, STATUS_SUCCESS, g_ds.feature_enabled,
                               g_ds.deep_standby_state);
        ds_publish_snapshot_locked();
        WdfWaitLockRelease(g_ds.lock);
    }

    return STATUS_SUCCESS;
}

VOID deep_standby_set_modem_req(_In_ ULONG req)
{
    InterlockedExchange(&g_ds.modem_req, (LONG)req);
    (VOID) registry_set_u_long(L"DeepStandbyModemReq", req);

    if (g_ds.lock && g_ds.dev && g_ds.modem_enabled) {
        WdfWaitLockAcquire(g_ds.lock, NULL);
        ds_record_event_locked(DEEP_STANDBY_EVENT_MANUAL_MODEM_REQ, STATUS_SUCCESS, req,
                               g_ds.deep_standby_state);
        (VOID) ds_toggle_modem_locked(req);
        ds_publish_snapshot_locked();
        WdfWaitLockRelease(g_ds.lock);
    }
}

ULONG deep_standby_get_modem_req(VOID)
{
    return (ULONG)InterlockedCompareExchange(&g_ds.modem_req, 0, 0);
}

NTSTATUS deep_standby_get_snapshot(_Out_ deep_standby_snapshot_t* snapshot)
{
    if (!snapshot)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 2;

    if (g_ds.lock)
        WdfWaitLockAcquire(g_ds.lock, NULL);

    snapshot->feature_enabled = g_ds.feature_enabled;
    snapshot->callbacks_enabled = g_ds.callbacks_enabled;
    snapshot->modem_enabled = g_ds.modem_enabled;
    snapshot->wlan_enabled = g_ds.wlan_enabled;
    snapshot->power_src = (ULONG)g_ds.power_src;
    snapshot->screen_state = (ULONG)g_ds.screen_state;
    snapshot->user_state = (ULONG)g_ds.user_state;
    snapshot->ms_on_dc_pwr = (ULONG)g_ds.ms_on_dc_pwr;
    snapshot->deep_standby_state = (ULONG)g_ds.deep_standby_state;
    snapshot->timer_expired = g_ds.timer_expired;
    snapshot->batt_drain_exceeded_limit = g_ds.batt_drain_exceeded_limit;
    snapshot->batt_drain_since_tmr_start = g_ds.batt_drain_since_tmr_start;
    snapshot->tmr_starting_batt_pct = g_ds.tmr_starting_batt_pct;
    snapshot->current_batt_pct = g_ds.current_batt_pct;
    snapshot->max_batt_drain_pct = g_ds.max_batt_drain_pct;
    snapshot->timeout_seconds = g_ds.timeout_seconds;
    snapshot->modem_req = (ULONG)InterlockedCompareExchange(&g_ds.modem_req, 0, 0);
    snapshot->evaluate_cnt = g_ds.evaluate_cnt;
    snapshot->callback_cnt = g_ds.callback_cnt;
    snapshot->state_transition_cnt = g_ds.state_transition_cnt;
    snapshot->enter_cnt = g_ds.enter_cnt;
    snapshot->exit_cnt = g_ds.exit_cnt;
    snapshot->wlan_ioctl_errors = g_ds.wlan_ioctl_errors;
    snapshot->modem_ioctl_errors = g_ds.modem_ioctl_errors;
    snapshot->last_wlan_ioctl_status = g_ds.last_wlan_ioctl_status;
    snapshot->last_modem_ioctl_status = g_ds.last_modem_ioctl_status;
    snapshot->history_cnt = g_ds.history_cnt;
    snapshot->history_dropped = g_ds.history_dropped;
    snapshot->last_event_code = g_ds.last_event_code;
    snapshot->last_event_status = g_ds.last_event_status;
    snapshot->tmr_start_time100ns = g_ds.tmr_start_time;
    snapshot->deep_standby_deadline100ns = g_ds.deep_standby_deadline;
    snapshot->last_evaluate_tick100ns = g_ds.last_evaluate_tick100ns;
    snapshot->last_transition_tick100ns = g_ds.last_transition_tick100ns;

    if (g_ds.lock)
        WdfWaitLockRelease(g_ds.lock);

    return STATUS_SUCCESS;
}

NTSTATUS deep_standby_get_history(_Out_writes_to_(capacity, *cnt)
                                      deep_standby_history_entry_t* entries,
                                  _In_ ULONG capacity, _Out_ PULONG cnt, _Out_opt_ PULONG dropped)
{
    if (!entries || capacity == 0 || !cnt)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    if (dropped)
        *dropped = 0;

    if (g_ds.lock)
        WdfWaitLockAcquire(g_ds.lock, NULL);

    ULONG avail = g_ds.history_cnt;
    ULONG copy_cnt = (avail < capacity) ? avail : capacity;
    ULONG start = (g_ds.history_head + DEEP_STANDBY_HISTORY_CAPACITY - copy_cnt) %
                  DEEP_STANDBY_HISTORY_CAPACITY;

    for (ULONG i = 0; i < copy_cnt; i++) {
        ULONG idx = (start + i) % DEEP_STANDBY_HISTORY_CAPACITY;
        entries[i] = g_ds.history[idx];
    }

    *cnt = copy_cnt;
    if (dropped) {
        ULONG skipped = (avail > copy_cnt) ? (avail - copy_cnt) : 0;
        *dropped = g_ds.history_dropped + skipped;
    }

    if (g_ds.lock)
        WdfWaitLockRelease(g_ds.lock);

    return STATUS_SUCCESS;
}
