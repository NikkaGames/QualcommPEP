#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "peplog.h"
#include "resource_cache.h"
#include "tsens.h"
#include "dispatch/park_types.h"
#include "dispatch/main_priv.h"
#include "dispatch/park_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

BOOLEAN pep_is_core_rotation_disabled(VOID);
ULONG pep_park_rank_cpu_cnt(VOID);
static __forceinline ULONG pep_park_tracked_cpu_cnt(VOID);
static __forceinline VOID pep_set_current_cpu_park_pref(_In_ ULONG cpu, _In_ UCHAR state);
static VOID pep_park_write_res(_In_ ULONG res_id, _In_ ULONG sub_type, _In_ ULONG value0,
                               _In_ ULONG value1, _In_ ULONG value2, _In_ ULONG value3);
static VOID pep_park_get_cluster_bounds(_In_ ULONG cpu_num, _Out_ PULONG start, _Out_ PULONG end);
ULONG pep_dcvs_query_park_info(_In_ pep_dcvs_park_queries_t query_type, _In_ ULONG cpu_num);
static VOID pep_park_log_additional_unpark_cores_cnt(_In_ ULONG remaining_cores);
VOID pep_park_ensure_trace_init(VOID);
static VOID pep_park_reset_trace(VOID);
ULONG pep_park_clamp_thermal_update_interval_ms(_In_ ULONG value);
LONG pep_park_clamp_thermal_hysteresis_deci_c(_In_ LONG value);
static VOID pep_park_record_event(_In_ ULONG event_code, _In_ LONG status, _In_ ULONG cpu,
                                  _In_ ULONG cpu_cnt, _In_ ULONG additional_unparked,
                                  _In_ ULONG remaining_unparked, _In_ ULONG evaluation_type,
                                  _In_ ULONGLONG evaluation_time, _In_ ULONG aux0, _In_ ULONG aux1);
NTSTATUS pep_park_refresh_thermal_ranking(_In_ BOOLEAN force, _In_ ULONG first_cpu,
                                          _In_ UCHAR evaluation_type,
                                          _In_ ULONGLONG evaluation_time);
static VOID pep_re_evaluate_cpu_core_park_pref_list(_In_ ULONG first_cpu,
                                                    _In_ UCHAR evaluation_type,
                                                    _In_ ULONGLONG evaluation_time);
static ULONG pep_select_unpark_core(_Inout_ pep_ppm_park_selection_v2_t* park_sel,
                                    _In_ BOOLEAN is_perf_check_req);
VOID pep_park_init_state(VOID);
VOID pep_park_deinit_state(VOID);
VOID pep_park_selection_v2(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_park_mask(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

park_sel_data_t g_park_sel = {{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
                              {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
                              {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
                              {0, 0, 0, 0, 0, 0, 0, 0},
                              {0u, 0u},
                              0u,
                              0u,
                              0u,
                              0u,
                              {0ull, 0ull},
                              {86400u, 86400u},
                              0u,
                              0u,
                              0ull,
                              0u,
                              0u};
cpu_park_mask_t g_cpu_park_state;
static UCHAR g_cpu_current_action_setting[PARK_MAX_CPUS_TRACKED];
KSPIN_LOCK g_park_sel_trace_lock;
static volatile LONG g_park_sel_trace_init;
parksel_event_t g_park_sel_history[PARKSEL_HISTORY_CAPACITY];
ULONG g_park_sel_history_head;
ULONG g_park_sel_history_cnt;
ULONG g_park_sel_history_dropped;
ULONGLONG g_park_sel_event_sequence;
ULONG g_park_sel_last_event_code;
LONG g_park_sel_last_event_status;
ULONG g_park_sel_last_event_additional_unparked;
ULONG g_park_sel_last_event_remaining_unparked;
ULONGLONG g_park_sel_last_event_tick100ns;
ULONG g_park_sel_thermal_rank_enabled = 1u;
LONG g_park_sel_thermal_rank_hysteresis_deci_c = 15;
ULONG g_park_sel_thermal_rank_update_interval_ms = 1000u;
ULONG g_park_sel_thermal_sensor_base_idx = 0u;
volatile LONG g_park_sel_thermal_refresh_cnt;
volatile LONG g_park_sel_thermal_apply_cnt;
LONG g_park_sel_last_thermal_status = STATUS_NOT_SUPPORTED;
static ULONGLONG g_park_sel_last_thermal_refresh_time100ns;

extern __forceinline BOOLEAN pep_is_core_rotation_disabled(VOID)
{
    ULONG v = 0;
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"PARKSEL", L"EnableParkSelection", &v)))
        return (v == 0) ? TRUE : FALSE;

    return registry_query_u_long(L"DisableCoreRotation", 1) ? TRUE : FALSE;
}

extern __forceinline ULONG pep_park_rank_cpu_cnt(VOID)
{
    ULONG c = g_park_sel.active_cpu_cnt;
    if (c == 0 || c > PARK_MAX_RANK_CPUS)
        c = PARK_MAX_RANK_CPUS;
    return c;
}

static __forceinline ULONG pep_park_tracked_cpu_cnt(VOID)
{
    ULONG c = g_park_sel.active_cpu_cnt;
    if (c == 0 || c > PARK_MAX_CPUS_TRACKED)
        c = PARK_MAX_CPUS_TRACKED;
    return c;
}

static __forceinline VOID pep_set_current_cpu_park_pref(_In_ ULONG cpu, _In_ UCHAR state)
{
    ULONG tracked = pep_park_tracked_cpu_cnt();
    if (tracked == 0)
        tracked = 1;

    if (cpu < tracked && cpu < PARK_MAX_CPUS_TRACKED) {
        g_cpu_current_action_setting[cpu] = state;
        KeMemoryBarrier();
    }
}

static VOID pep_park_write_res(_In_ ULONG res_id, _In_ ULONG sub_type, _In_ ULONG value0,
                               _In_ ULONG value1, _In_ ULONG value2, _In_ ULONG value3)
{
    res_key_t k;
    res_value_t v;
    k.res_id = res_id;
    k.sub_type = sub_type;
    v.value0 = value0;
    v.value1 = value1;
    v.value2 = value2;
    v.value3 = value3;
    (VOID) res_set(k, v);
}

static VOID pep_park_get_cluster_bounds(_In_ ULONG cpu_num, _Out_ PULONG start, _Out_ PULONG end)
{
    ULONG rank_cnt = pep_park_rank_cpu_cnt();
    ULONG s = (cpu_num >= 4u) ? 4u : 0u;
    ULONG e = s + 4u;

    if (e > rank_cnt)
        e = rank_cnt;
    if (s > e)
        s = e;

    *start = s;
    *end = e;
}

ULONG pep_dcvs_query_park_info(_In_ pep_dcvs_park_queries_t query_type, _In_ ULONG cpu_num)
{
    ULONG tracked = pep_park_tracked_cpu_cnt();
    if (tracked == 0)
        return 0;

    if (cpu_num >= tracked)
        cpu_num %= tracked;

    ULONG cur_state = g_cpu_park_state.cpu_cur_park_state[cpu_num] ? 1u : 0u;
    ULONG prev_state = g_cpu_park_state.cpu_prev_park_state[cpu_num] ? 1u : 0u;

    switch (query_type) {
        case DCVS_CPU_PARK_STATE:
            return cur_state;

        case DCVS_CPU_STATE_CHANGE:
            return (cur_state != prev_state) ? 1u : 0u;

        case DCVS_CLUSTER_PARK_STATE:
            return (pep_dcvs_query_park_info(DCVS_CLUSTER_UNPARK_CNT, cpu_num) == 0u) ? 1u : 0u;

        case DCVS_CLUSTER_UNPARK_CNT: {
            ULONG s = 0;
            ULONG e = 0;
            ULONG unparked = 0;
            pep_park_get_cluster_bounds(cpu_num, &s, &e);
            for (ULONG i = s; i < e; i++) {
                if (!g_cpu_park_state.cpu_cur_park_state[i])
                    unparked++;
            }
            return unparked;
        }

        case DCVS_CLUSTER_STATE_CHANGE: {
            ULONG s = 0;
            ULONG e = 0;
            ULONG cur_unparked = 0;
            ULONG prev_unparked = 0;
            pep_park_get_cluster_bounds(cpu_num, &s, &e);
            for (ULONG i = s; i < e; i++) {
                if (!g_cpu_park_state.cpu_cur_park_state[i])
                    cur_unparked++;
                if (!g_cpu_park_state.cpu_prev_park_state[i])
                    prev_unparked++;
            }
            return (cur_unparked != prev_unparked) ? 1u : 0u;
        }

        default:
            return 0;
    }
}

static VOID pep_park_log_additional_unpark_cores_cnt(_In_ ULONG remaining_cores)
{
    pep_park_write_res(RES_PARK_MISC, 1u, g_park_sel.last_additional_unparked, remaining_cores,
                       g_park_sel.last_evaluation_type,
                       (ULONG)(g_park_sel.last_selection_evaluation_time & 0xFFFFFFFFu));
}

VOID pep_park_ensure_trace_init(VOID)
{
    if (InterlockedCompareExchange(&g_park_sel_trace_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_park_sel_trace_lock);
        RtlZeroMemory(g_park_sel_history, sizeof(g_park_sel_history));
        g_park_sel_history_head = 0;
        g_park_sel_history_cnt = 0;
        g_park_sel_history_dropped = 0;
        g_park_sel_event_sequence = 0;
        g_park_sel_last_event_code = 0;
        g_park_sel_last_event_status = STATUS_SUCCESS;
        g_park_sel_last_event_additional_unparked = 0;
        g_park_sel_last_event_remaining_unparked = 0;
        g_park_sel_last_event_tick100ns = 0;
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_park_sel_trace_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_park_sel_trace_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static VOID pep_park_reset_trace(VOID)
{
    pep_park_ensure_trace_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_park_sel_trace_lock, &irql);
    RtlZeroMemory(g_park_sel_history, sizeof(g_park_sel_history));
    g_park_sel_history_head = 0;
    g_park_sel_history_cnt = 0;
    g_park_sel_history_dropped = 0;
    g_park_sel_event_sequence = 0;
    g_park_sel_last_event_code = 0;
    g_park_sel_last_event_status = STATUS_SUCCESS;
    g_park_sel_last_event_additional_unparked = 0;
    g_park_sel_last_event_remaining_unparked = 0;
    g_park_sel_last_event_tick100ns = 0;
    KeReleaseSpinLock(&g_park_sel_trace_lock, irql);
}

extern __forceinline ULONG pep_park_clamp_thermal_update_interval_ms(_In_ ULONG value)
{
    if (value < 100u)
        return 100u;
    if (value > 600000u)
        return 600000u;
    return value;
}

extern __forceinline LONG pep_park_clamp_thermal_hysteresis_deci_c(_In_ LONG value)
{
    LONG v = value;
    if (v < 0)
        v = -v;
    if (v > 1000)
        v = 1000;
    return v;
}

static VOID pep_park_record_event(_In_ ULONG event_code, _In_ LONG status, _In_ ULONG cpu,
                                  _In_ ULONG cpu_cnt, _In_ ULONG additional_unparked,
                                  _In_ ULONG remaining_unparked, _In_ ULONG evaluation_type,
                                  _In_ ULONGLONG evaluation_time, _In_ ULONG aux0, _In_ ULONG aux1)
{
    pep_park_ensure_trace_init();

    parksel_event_t ev;
    RtlZeroMemory(&ev, sizeof(ev));
    ev.tick100ns = pep_now100ns();
    ev.event_code = event_code;
    ev.status = status;
    ev.cpu = cpu;
    ev.cpu_cnt = cpu_cnt;
    ev.additional_unparked = additional_unparked;
    ev.remaining_unparked = remaining_unparked;
    ev.evaluation_type = evaluation_type;
    ev.evaluation_time = evaluation_time;
    ev.cluster0_unparked = pep_dcvs_query_park_info(DCVS_CLUSTER_UNPARK_CNT, 0u);
    ev.cluster1_unparked = pep_dcvs_query_park_info(DCVS_CLUSTER_UNPARK_CNT, 4u);
    ev.aux0 = aux0;
    ev.aux1 = aux1;

    KIRQL irql;
    KeAcquireSpinLock(&g_park_sel_trace_lock, &irql);

    ev.sequence = ++g_park_sel_event_sequence;
    g_park_sel_history[g_park_sel_history_head] = ev;
    g_park_sel_history_head = (g_park_sel_history_head + 1u) % PARKSEL_HISTORY_CAPACITY;
    if (g_park_sel_history_cnt < PARKSEL_HISTORY_CAPACITY)
        g_park_sel_history_cnt++;
    else
        g_park_sel_history_dropped++;

    g_park_sel_last_event_code = ev.event_code;
    g_park_sel_last_event_status = ev.status;
    g_park_sel_last_event_additional_unparked = ev.additional_unparked;
    g_park_sel_last_event_remaining_unparked = ev.remaining_unparked;
    g_park_sel_last_event_tick100ns = ev.tick100ns;

    KeReleaseSpinLock(&g_park_sel_trace_lock, irql);
}

NTSTATUS pep_park_refresh_thermal_ranking(_In_ BOOLEAN force, _In_ ULONG first_cpu,
                                          _In_ UCHAR evaluation_type,
                                          _In_ ULONGLONG evaluation_time)
{
    ULONG rank_cnt = pep_park_rank_cpu_cnt();
    if (!g_park_sel_thermal_rank_enabled || rank_cnt <= 4u) {
        g_park_sel_last_thermal_status = STATUS_NOT_SUPPORTED;
        return STATUS_NOT_SUPPORTED;
    }

    ULONGLONG now = pep_now100ns();
    ULONGLONG interval100ns = (ULONGLONG)g_park_sel_thermal_rank_update_interval_ms * 10000ull;
    if (!force && g_park_sel_last_thermal_refresh_time100ns != 0 &&
        now - g_park_sel_last_thermal_refresh_time100ns < interval100ns) {
        return STATUS_RETRY;
    }

    tsens_snapshot_t ts;
    RtlZeroMemory(&ts, sizeof(ts));
    tsens_get_snapshot(&ts);

    LONG cpu_temp[PARK_MAX_RANK_CPUS];
    RtlZeroMemory(cpu_temp, sizeof(cpu_temp));
    ULONG sensor_cnt = ts.sensor_cnt;
    NTSTATUS status = (sensor_cnt > 0) ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;

    for (ULONG i = 0; i < rank_cnt && i < PARK_MAX_RANK_CPUS; i++) {
        LONG t = ts.aggregate_temp_deci_c;
        ULONG sensor = g_park_sel_thermal_sensor_base_idx + i;
        if (sensor < sensor_cnt && sensor < TSENS_MAX_SENSORS)
            t = ts.sensors[sensor].last_temp_deci_c;
        cpu_temp[i] = t;
        g_park_sel.cpu_temp[i] = t;
    }

    ULONG big_start = (rank_cnt > 4u) ? 4u : rank_cnt;
    ULONG big_cnt = (rank_cnt > big_start) ? (rank_cnt - big_start) : 0u;
    UCHAR sorted_cpu[PARK_MAX_RANK_CPUS];
    for (ULONG i = 0; i < big_cnt; i++)
        sorted_cpu[i] = (UCHAR)(big_start + i);

    LONG hyst = pep_park_clamp_thermal_hysteresis_deci_c(g_park_sel_thermal_rank_hysteresis_deci_c);
    for (ULONG i = 0; i < big_cnt; i++) {
        for (ULONG j = i + 1u; j < big_cnt; j++) {
            ULONG cpu_i = sorted_cpu[i];
            ULONG cpu_j = sorted_cpu[j];
            LONG delta = cpu_temp[cpu_i] - cpu_temp[cpu_j];
            LONG abs_delta = (delta < 0) ? -delta : delta;

            BOOLEAN swap = FALSE;
            if (abs_delta > hyst) {
                swap = (delta > 0) ? TRUE : FALSE;
            } else {
                swap = (g_park_sel.cpu_rank[cpu_i] > g_park_sel.cpu_rank[cpu_j]) ? TRUE : FALSE;
            }

            if (swap) {
                UCHAR tmp = sorted_cpu[i];
                sorted_cpu[i] = sorted_cpu[j];
                sorted_cpu[j] = tmp;
            }
        }
    }

    BOOLEAN changed = FALSE;
    for (ULONG i = 0; i < big_start; i++) {
        if (g_park_sel.cpu_rank[i] != (UCHAR)i)
            changed = TRUE;
        g_park_sel.cpu_rank[i] = (UCHAR)i;
    }
    for (ULONG i = 0; i < big_cnt; i++) {
        ULONG cpu = sorted_cpu[i];
        UCHAR new_rank = (UCHAR)(big_start + i);
        if (g_park_sel.cpu_rank[cpu] != new_rank)
            changed = TRUE;
        g_park_sel.cpu_rank[cpu] = new_rank;
    }

    g_park_sel_last_thermal_refresh_time100ns = now;
    (VOID) InterlockedIncrement(&g_park_sel_thermal_refresh_cnt);
    if (changed)
        (VOID) InterlockedIncrement(&g_park_sel_thermal_apply_cnt);
    g_park_sel_last_thermal_status = status;

    pep_park_write_res(RES_PARK_ROTATE, 2u, g_park_sel_thermal_rank_enabled, (ULONG)hyst,
                       (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_apply_cnt, 0, 0),
                       (ULONG)(now & 0xFFFFFFFFu));

    pep_park_record_event(PARK_SEL_EVENT_THERMAL_RANK, status, first_cpu, rank_cnt,
                          g_park_sel.last_additional_unparked, 0u, evaluation_type, evaluation_time,
                          (ULONG)InterlockedCompareExchange(&g_park_sel_thermal_refresh_cnt, 0, 0),
                          changed ? 1u : 0u);

    return status;
}

static VOID pep_re_evaluate_cpu_core_park_pref_list(_In_ ULONG first_cpu,
                                                    _In_ UCHAR evaluation_type,
                                                    _In_ ULONGLONG evaluation_time)
{
    ULONG rank_cnt = pep_park_rank_cpu_cnt();
    ULONG domain_id = (first_cpu >= 4u) ? 1u : 0u;
    if (domain_id > 1u)
        domain_id = 1u;

    g_park_sel.core_rotation_stat[domain_id] = CORE_ROTATION_NONE;

    if (rank_cnt <= 1u || pep_is_core_rotation_disabled())
        return;

    if (g_park_sel.last_evaluation_time[domain_id] == evaluation_time)
        return;

    g_park_sel.last_evaluation_time[domain_id] = evaluation_time;
    g_park_sel.core_rotation_stat[domain_id] = CORE_ROTATION_NO_RECOMMENDATION;

    if (domain_id == 1u && evaluation_type == 0u && rank_cnt > 5u) {
        ULONG threshold_sec = g_park_sel.timer_threshold_sec[1];
        if (threshold_sec == 0)
            threshold_sec = 86400u;

        ULONGLONG now = pep_now100ns();
        static ULONGLONG s_last_rotation_tick100ns = 0;
        ULONGLONG threshold100ns = (ULONGLONG)threshold_sec * 10000000ull;
        if (s_last_rotation_tick100ns == 0 || (now - s_last_rotation_tick100ns) > threshold100ns) {
            UCHAR tail = g_park_sel.cpu_rank[rank_cnt - 1u];
            for (ULONG i = rank_cnt - 1u; i > 4u; i--)
                g_park_sel.cpu_rank[i] = g_park_sel.cpu_rank[i - 1u];
            g_park_sel.cpu_rank[4u] = tail;

            s_last_rotation_tick100ns = now;
            g_park_sel.toggle_state ^= 1u;
            g_park_sel.core_rotation_stat[domain_id] = CORE_ROTATION_NEW_RECOMMENDATION;

            pep_park_write_res(RES_PARK_ROTATE, domain_id, g_park_sel.core_rotation_stat[domain_id],
                               g_park_sel.toggle_state, threshold_sec, (ULONG)(now >> 32));

            pep_park_record_event(PARK_SEL_EVENT_CORE_ROTATION, STATUS_SUCCESS, first_cpu, rank_cnt,
                                  g_park_sel.last_additional_unparked, 0u, evaluation_type,
                                  evaluation_time, domain_id, g_park_sel.toggle_state);
        }
    }
}

static ULONG pep_select_unpark_core(_Inout_ pep_ppm_park_selection_v2_t* park_sel,
                                    _In_ BOOLEAN is_perf_check_req)
{
    ULONG rank_cnt = pep_park_rank_cpu_cnt();
    if (!park_sel || !park_sel->processors || rank_cnt == 0 || park_sel->cnt == 0)
        return 0;

    ULONG cnt = park_sel->cnt;

    for (ULONG i = 0; i < rank_cnt; i++) {
        g_park_sel.last_pref[i] = g_park_sel.pref[i];
        g_park_sel.pref[i] = 0;
    }

    ULONG first_cpu = get_cpu_num_from_pep_handle(park_sel->processors[0].u.proc);
    (VOID) pep_park_refresh_thermal_ranking(FALSE, first_cpu, park_sel->evaluation_type,
                                            park_sel->evaluation_time);
    pep_re_evaluate_cpu_core_park_pref_list(first_cpu, park_sel->evaluation_type,
                                            park_sel->evaluation_time);

    for (ULONG i = 0; i < cnt; i++) {
        ULONG cpu = get_cpu_num_from_pep_handle(park_sel->processors[i].u.proc);
        if (cpu >= rank_cnt)
            continue;
        ULONG rank = g_park_sel.cpu_rank[cpu];
        if (rank < rank_cnt)
            g_park_sel.pref[rank] = 1;
    }

    ULONG remaining = park_sel->additional_unparked_processors;
    for (ULONG i = 0; i < rank_cnt && remaining > 0; i++) {
        if (g_park_sel.pref[i] == 1u) {
            g_park_sel.pref[i] = 2u;
            remaining--;
        }
    }

    for (ULONG i = 0; i < cnt; i++) {
        ULONG cpu = get_cpu_num_from_pep_handle(park_sel->processors[i].u.proc);
        ULONG tracked_cpu = cpu;
        ULONG tracked_cpu_cnt = pep_park_tracked_cpu_cnt();
        if (tracked_cpu_cnt == 0)
            tracked_cpu_cnt = 1;
        if (cpu >= rank_cnt)
            continue;

        ULONG rank = g_park_sel.cpu_rank[cpu];
        UCHAR pref = 0u;
        if (rank < rank_cnt)
            pref = g_park_sel.pref[rank];

        park_sel->processors[i].pep_preference = pref;
        if (is_perf_check_req) {
            if (pref == 1u)
                pep_set_current_cpu_park_pref(cpu, 1u);
            else if (pref == 2u)
                pep_set_current_cpu_park_pref(cpu, 4u);
        }

        if (tracked_cpu < tracked_cpu_cnt && tracked_cpu < PARK_MAX_CPUS_TRACKED)
            g_cpu_park_state.cpu_current_preference[tracked_cpu] = pref;

        pep_park_write_res(RES_PARK_PREF, tracked_cpu, pref, park_sel->evaluation_type,
                           park_sel->additional_unparked_processors, cnt);
    }

    g_park_sel.last_additional_unparked = park_sel->additional_unparked_processors;
    g_park_sel.last_evaluation_type = park_sel->evaluation_type;
    g_park_sel.last_selection_evaluation_time = park_sel->evaluation_time;

    if (is_perf_check_req)
        pep_park_log_additional_unpark_cores_cnt(remaining);

    pep_park_record_event(PARK_SEL_EVENT_SELECTION, STATUS_SUCCESS, first_cpu, cnt,
                          park_sel->additional_unparked_processors, remaining,
                          park_sel->evaluation_type, park_sel->evaluation_time, rank_cnt,
                          is_perf_check_req ? 1u : 0u);

    return remaining;
}

VOID pep_park_init_state(VOID)
{
    RtlZeroMemory(&g_cpu_park_state, sizeof(g_cpu_park_state));
    RtlZeroMemory(g_cpu_current_action_setting, sizeof(g_cpu_current_action_setting));
    pep_park_reset_trace();

    for (ULONG i = 0; i < PARK_MAX_RANK_CPUS; i++) {
        g_park_sel.last_pref[i] = 0;
        g_park_sel.pref[i] = 0;
        g_park_sel.cpu_rank[i] = (UCHAR)i;
        g_park_sel.cpu_temp[i] = 0;
    }
    g_park_sel.core_rotation_stat[0] = 0;
    g_park_sel.core_rotation_stat[1] = 0;

    ULONG active = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (active == 0)
        active = 1;
    if (active > PARK_MAX_RANK_CPUS)
        active = PARK_MAX_RANK_CPUS;
    g_park_sel.active_cpu_cnt = active;

    g_park_sel.pref_rail_id = 0x2331u;
    g_park_sel.timer_rail_id = g_park_sel.pref_rail_id + active;
    g_park_sel.num_unpark_rail_id = g_park_sel.timer_rail_id + 1u;
    g_park_sel.core_rotation_rail_id = g_park_sel.num_unpark_rail_id + 1u;
    g_park_sel.toggle_state = 0;
    g_park_sel.last_additional_unparked = 0;
    g_park_sel.last_evaluation_type = 0;
    g_park_sel.last_selection_evaluation_time = 0;
    g_park_sel.last_evaluation_time[0] = 0;
    g_park_sel.last_evaluation_time[1] = 0;

    ULONG timer_apc0 = g_park_sel.timer_threshold_sec[0];
    ULONG timer_apc1 = g_park_sel.timer_threshold_sec[1];
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"PARKSEL\\ReliabilityBased", L"Timer_APC0",
                                                    &timer_apc0)))
        g_park_sel.timer_threshold_sec[0] = timer_apc0;
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"PARKSEL\\ReliabilityBased", L"Timer_APC1",
                                                    &timer_apc1)))
        g_park_sel.timer_threshold_sec[1] = timer_apc1;
    g_park_sel.timer_threshold_sec[0] =
        registry_query_u_long(L"ParkSelTimerApc0Sec", g_park_sel.timer_threshold_sec[0]);
    g_park_sel.timer_threshold_sec[1] =
        registry_query_u_long(L"ParkSelTimerApc1Sec", g_park_sel.timer_threshold_sec[1]);

    g_park_sel_thermal_rank_enabled =
        registry_query_u_long(L"ParkSelThermalRankEnabled", 1u) ? 1u : 0u;
    g_park_sel_thermal_rank_hysteresis_deci_c = pep_park_clamp_thermal_hysteresis_deci_c(
        (LONG)registry_query_u_long(L"ParkSelThermalRankHysteresisDeciC", 15u));
    g_park_sel_thermal_rank_update_interval_ms = pep_park_clamp_thermal_update_interval_ms(
        registry_query_u_long(L"ParkSelThermalRankUpdateIntervalMs", 1000u));
    g_park_sel_thermal_sensor_base_idx = registry_query_u_long(L"ParkSelThermalSensorBase", 0u);
    if (g_park_sel_thermal_sensor_base_idx >= TSENS_MAX_SENSORS)
        g_park_sel_thermal_sensor_base_idx = 0u;
    g_park_sel_last_thermal_refresh_time100ns = 0;
    (VOID) InterlockedExchange(&g_park_sel_thermal_refresh_cnt, 0);
    (VOID) InterlockedExchange(&g_park_sel_thermal_apply_cnt, 0);
    g_park_sel_last_thermal_status = STATUS_NOT_SUPPORTED;

    ULONG pref_big_cpus = 0x7654u;
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"PARKSEL\\ParkingMaskInit", L"PrefBigCpus",
                                                    &pref_big_cpus))) {
        ULONG v = pref_big_cpus;
        for (ULONG i = 4; i < PARK_MAX_RANK_CPUS; i++) {
            g_park_sel.cpu_rank[i] = (UCHAR)(v & 0xFu);
            v >>= 4;
        }
    }

    for (ULONG cpu = 0; cpu < active; cpu++) {
        pep_park_write_res(RES_PARK_PREF, cpu, 0, 0, 0, 0);
        pep_park_write_res(RES_PARK_STATE, cpu, 0, 0, 0, 0);
    }
    pep_park_write_res(RES_PARK_MISC, 0u, g_park_sel.pref_rail_id, g_park_sel.timer_rail_id,
                       g_park_sel.num_unpark_rail_id, g_park_sel.core_rotation_rail_id);

    pep_park_record_event(PARK_SEL_EVENT_INIT, STATUS_SUCCESS, 0u, active, 0u, 0u, 0u, 0ull,
                          g_park_sel.pref_rail_id, g_park_sel.core_rotation_rail_id);

    (VOID) pep_park_refresh_thermal_ranking(TRUE, 0u, 0u, 0ull);
}

VOID pep_park_deinit_state(VOID)
{
    pep_park_record_event(PARK_SEL_EVENT_DEINIT, STATUS_SUCCESS, 0u, g_park_sel.active_cpu_cnt,
                          g_park_sel.last_additional_unparked, 0u, g_park_sel.last_evaluation_type,
                          g_park_sel.last_selection_evaluation_time, 0u, 0u);

    RtlZeroMemory(&g_cpu_park_state, sizeof(g_cpu_park_state));
    RtlZeroMemory(g_cpu_current_action_setting, sizeof(g_cpu_current_action_setting));
    RtlZeroMemory(&g_park_sel, sizeof(g_park_sel));
    g_park_sel_thermal_rank_enabled = 0u;
    g_park_sel_thermal_rank_hysteresis_deci_c = 15;
    g_park_sel_thermal_rank_update_interval_ms = 1000u;
    g_park_sel_thermal_sensor_base_idx = 0u;
    g_park_sel_last_thermal_refresh_time100ns = 0;
    (VOID) InterlockedExchange(&g_park_sel_thermal_refresh_cnt, 0);
    (VOID) InterlockedExchange(&g_park_sel_thermal_apply_cnt, 0);
    g_park_sel_last_thermal_status = STATUS_NOT_SUPPORTED;
}

VOID pep_park_selection_v2(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    __try {
        pep_ppm_park_selection_v2_t* p = (pep_ppm_park_selection_v2_t*)data;
        if (!p->cnt || !p->processors)
            return;
        BOOLEAN is_perf_check_req = (p->evaluation_type == 0u) ? TRUE : FALSE;
        ULONG remaining = pep_select_unpark_core(p, is_perf_check_req);
        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA1, STATUS_SUCCESS, p->cnt,
                      (ULONG)p->additional_unparked_processors, (ULONG)p->evaluation_type,
                      remaining);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_park_mask(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    __try {
        pep_ppm_park_mask_t* m = (pep_ppm_park_mask_t*)data;
        if (!m->cnt || !m->processors)
            return;

        for (ULONG i = 0; i < m->cnt; i++) {
            ULONG cpu = get_cpu_num_from_pep_handle(m->processors[i].u.proc);
            if (cpu < PARK_MAX_CPUS_TRACKED)
                g_cpu_park_state.cpu_cur_park_state[cpu] = m->processors[i].parked ? 1u : 0u;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}
