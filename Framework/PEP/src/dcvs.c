#include "dcvs.h"
#include "tsens.h"
#include "soc_model.h"
#include "soc_transport.h"
#include "dcvs_rundown.h"
#include "trace.h"
#include "dcvs.tmh"

typedef struct dcvs_domain {
    ULONG domain_id;
    ULONG domain_members;
    ULONG coordination_type;
    ULONG worst_case_transition_latency;
    ULONG worst_case_transition_overhead;

    ULONG enable_perf_selection;
    ULONG enable_thermal_perf_selection;

    ULONG enable_boost;
    ULONG always_boost_perf_enabled;
    ULONG boost_freq_hz;

    ULONG max_freq_hz;
    ULONG min_freq_hz;
    ULONG nominal_freq_hz;
    ULONG lowest_nonlinear_freq_hz;

    ULONG perf_level_cnt;
    ULONG* perf_freq_hz;
} dcvs_domain_t;

typedef struct dcvs_cpu {
    ULONG domain_idx;
    ULONG current_perf_level;
    ULONG current_freq_hz;
    ULONG requested_perf_state_idx;
    ULONG min_perf;
    ULONG max_perf;
    ULONG desired_perf;
    ULONG epp;
    ULONGLONG enter_time100ns;
    volatile LONG64 residency100ns;
    volatile LONG64 transition_cnt;
} dcvs_cpu_t;

/* a second caller must not treat an in-progress init as success. */

static volatile LONG g_dcvs_init;
/* dispatch_LEVEL callbacks never wait for shutdown. */

static volatile LONG g_dcvs_users;
static volatile LONG g_dcvs_generation;

static __forceinline BOOLEAN dcvs_enter(VOID)
{
    return dcvs_rundown_enter(&g_dcvs_init, &g_dcvs_users, &g_dcvs_generation);
}

static __forceinline VOID dcvs_leave(VOID)
{
    dcvs_rundown_leave(&g_dcvs_users);
}

static BOOLEAN g_molokai_enabled;
static BOOLEAN g_molokai_enforce_android_floor;
static KSPIN_LOCK g_molokai_cpu_state_lock;
static dcvs_domain_t g_domain;
static dcvs_cpu_t* g_cpu;
static ULONG g_cpu_cnt;
static ULONG g_dcvs_thermal_hot_deci_c = 850;
static ULONG g_dcvs_thermal_critical_deci_c = 950;
static ULONG g_dcvs_thermal_cap_hot_percent = 70;
static ULONG g_dcvs_thermal_cap_critical_percent = 40;

static volatile LONG g_dcvs_trace_init;
static KSPIN_LOCK g_dcvs_trace_lock;
static dcvs_event_t g_dcvs_history[DCVS_HISTORY_CAPACITY];
static ULONG g_dcvs_history_head;
static ULONG g_dcvs_history_cnt;
static ULONG g_dcvs_history_dropped;
static ULONGLONG g_dcvs_event_sequence;
static ULONG g_dcvs_last_event_code;
static LONG g_dcvs_last_event_status;
static ULONG g_dcvs_last_event_cpu;
static ULONG g_dcvs_last_event_min_perf_state;
static ULONG g_dcvs_last_event_max_perf_state;
static ULONG g_dcvs_last_event_desired_perf_state;
static ULONG g_dcvs_last_event_applied_perf_level;
static ULONG g_dcvs_last_event_applied_freq_hz;
static ULONG g_dcvs_last_event_thermal_cap_percent;
static ULONG g_dcvs_last_event_epp;
static ULONG g_dcvs_last_event_aux0;
static ULONG g_dcvs_last_event_aux1;
static ULONGLONG g_dcvs_last_event_tick100ns;
static ULONG g_dcvs_policy_update_cnt;
static ULONGLONG g_dcvs_last_policy_tick100ns;

static __forceinline ULONGLONG dcvs_now100ns(VOID)
{
    return KeQueryInterruptTime();
}

static VOID dcvs_ensure_trace_init(VOID)
{
    if (InterlockedCompareExchange(&g_dcvs_trace_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_dcvs_trace_lock);
        RtlZeroMemory(g_dcvs_history, sizeof(g_dcvs_history));
        g_dcvs_history_head = 0;
        g_dcvs_history_cnt = 0;
        g_dcvs_history_dropped = 0;
        g_dcvs_event_sequence = 0;
        g_dcvs_last_event_code = 0;
        g_dcvs_last_event_status = STATUS_SUCCESS;
        g_dcvs_last_event_cpu = 0;
        g_dcvs_last_event_min_perf_state = 0;
        g_dcvs_last_event_max_perf_state = 0;
        g_dcvs_last_event_desired_perf_state = 0;
        g_dcvs_last_event_applied_perf_level = 0;
        g_dcvs_last_event_applied_freq_hz = 0;
        g_dcvs_last_event_thermal_cap_percent = 0;
        g_dcvs_last_event_epp = 0;
        g_dcvs_last_event_aux0 = 0;
        g_dcvs_last_event_aux1 = 0;
        g_dcvs_last_event_tick100ns = 0;
        g_dcvs_policy_update_cnt = 0;
        g_dcvs_last_policy_tick100ns = 0;
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_dcvs_trace_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_dcvs_trace_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static VOID dcvs_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG cpu,
                              _In_ ULONG min_perf_state, _In_ ULONG max_perf_state,
                              _In_ ULONG desired_perf_state, _In_ ULONG applied_perf_level,
                              _In_ ULONG applied_freq_hz, _In_ ULONG thermal_cap_percent,
                              _In_ ULONG epp, _In_ ULONG aux0, _In_ ULONG aux1)
{
    dcvs_ensure_trace_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_dcvs_trace_lock, &irql);

    ULONGLONG seq = ++g_dcvs_event_sequence;
    ULONG idx = g_dcvs_history_head;
    dcvs_event_t* e = &g_dcvs_history[idx];

    e->tick100ns = dcvs_now100ns();
    e->sequence = seq;
    e->event_code = event_code;
    e->status = status;
    e->cpu = cpu;
    e->min_perf_state = min_perf_state;
    e->max_perf_state = max_perf_state;
    e->desired_perf_state = desired_perf_state;
    e->applied_perf_level = applied_perf_level;
    e->applied_freq_hz = applied_freq_hz;
    e->thermal_cap_percent = thermal_cap_percent;
    e->epp = epp;
    e->aux0 = aux0;
    e->aux1 = aux1;

    g_dcvs_history_head = (idx + 1u) % DCVS_HISTORY_CAPACITY;
    if (g_dcvs_history_cnt < DCVS_HISTORY_CAPACITY) {
        g_dcvs_history_cnt++;
    } else {
        g_dcvs_history_dropped++;
    }

    g_dcvs_last_event_code = event_code;
    g_dcvs_last_event_status = status;
    g_dcvs_last_event_cpu = cpu;
    g_dcvs_last_event_min_perf_state = min_perf_state;
    g_dcvs_last_event_max_perf_state = max_perf_state;
    g_dcvs_last_event_desired_perf_state = desired_perf_state;
    g_dcvs_last_event_applied_perf_level = applied_perf_level;
    g_dcvs_last_event_applied_freq_hz = applied_freq_hz;
    g_dcvs_last_event_thermal_cap_percent = thermal_cap_percent;
    g_dcvs_last_event_epp = epp;
    g_dcvs_last_event_aux0 = aux0;
    g_dcvs_last_event_aux1 = aux1;
    g_dcvs_last_event_tick100ns = e->tick100ns;

    KeReleaseSpinLock(&g_dcvs_trace_lock, irql);
}

static ULONG clamp_u32(_In_ ULONG v, _In_ ULONG lo, _In_ ULONG hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static NTSTATUS dcvs_query_cluster_value(_In_ ULONG cluster_idx, _In_z_ PCWSTR value_name,
                                         _Out_ PULONG value)
{
    WCHAR sub[64];
    if (!value || !value_name)
        return STATUS_INVALID_PARAMETER;
    *value = 0;
    if (!NT_SUCCESS(RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"CPU_CLUSTER_%u",
                                        (unsigned int)cluster_idx)))
        return STATUS_INVALID_PARAMETER;
    return registry_query_u_long_in_sub_key(sub, value_name, value);
}

static BOOLEAN dcvs_query_cluster_value_with_fallback(_In_ ULONG preferred_cluster,
                                                      _In_z_ PCWSTR value_name, _Out_ PULONG value)
{
    if (!value || !value_name)
        return FALSE;
    *value = 0;

    if (NT_SUCCESS(dcvs_query_cluster_value(preferred_cluster, value_name, value)))
        return TRUE;
    if (preferred_cluster != 1 && NT_SUCCESS(dcvs_query_cluster_value(1, value_name, value)))
        return TRUE;
    if (preferred_cluster != 0 && NT_SUCCESS(dcvs_query_cluster_value(0, value_name, value)))
        return TRUE;
    return FALSE;
}

static ULONG dcvs_query_perf_level_freq_override(_In_ ULONG preferred_cluster, _In_ ULONG level,
                                                 _In_ ULONG default_hz)
{
    WCHAR sub[80];
    WCHAR name[64];
    ULONG v = 0;

    if (NT_SUCCESS(RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"CPU_CLUSTER_%u\\P%u",
                                       (unsigned int)preferred_cluster, (unsigned int)level)) &&
        NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Frequency", &v)) && v != 0) {
        return v;
    }
    if (preferred_cluster != 1 &&
        NT_SUCCESS(RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"CPU_CLUSTER_1\\P%u",
                                       (unsigned int)level)) &&
        NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Frequency", &v)) && v != 0) {
        return v;
    }
    if (preferred_cluster != 0 &&
        NT_SUCCESS(RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"CPU_CLUSTER_0\\P%u",
                                       (unsigned int)level)) &&
        NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Frequency", &v)) && v != 0) {
        return v;
    }

    if (NT_SUCCESS(RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"CpuPerfLevel%uFrequencyHz",
                                       (unsigned int)level))) {
        v = registry_query_u_long(name, 0);
        if (v != 0)
            return v;
    }

    return default_hz;
}

static ULONG compute_thermal_cap_percent(VOID)
{
    if (!g_domain.enable_thermal_perf_selection)
        return 100;

    ULONG t = tsens_get_temp_deci_c();
    ULONG hot = g_dcvs_thermal_hot_deci_c;
    ULONG crit = g_dcvs_thermal_critical_deci_c;
    ULONG cap_hot = g_dcvs_thermal_cap_hot_percent;
    ULONG cap_crit = g_dcvs_thermal_cap_critical_percent;

    if (crit <= hot)
        crit = hot + 50;

    cap_hot = clamp_u32(cap_hot, 1, 100);
    cap_crit = clamp_u32(cap_crit, 1, 100);

    if (t <= hot)
        return 100;
    if (t >= crit)
        return cap_crit;

    ULONG span = crit - hot;
    ULONG pos = t - hot;
    ULONG drop = 100 - cap_hot;
    ULONG drop2 = cap_hot - cap_crit;

    ULONG cap;
    if (pos * 2 < span) {
        cap = 100 - (drop * pos) / (span / 2);
        if (cap < cap_hot)
            cap = cap_hot;
    } else {
        ULONG pos2 = pos - (span / 2);
        cap = cap_hot - (drop2 * pos2) / (span - (span / 2));
        if (cap < cap_crit)
            cap = cap_crit;
    }

    cap = clamp_u32(cap, cap_crit, 100);
    return cap;
}

static VOID dcvs_free_domain(VOID)
{
    if (g_domain.perf_freq_hz) {
        ExFreePool(g_domain.perf_freq_hz);
        g_domain.perf_freq_hz = NULL;
    }
    g_domain.perf_level_cnt = 0;
}

static NTSTATUS dcvs_build_perf_table(VOID)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(0);
        ULONG* frequencies =
            (ULONG*)ExAllocatePool2(POOL_FLAG_NON_PAGED, d->level_cnt * sizeof(ULONG), POOL_TAG);
        if (!frequencies)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(frequencies, d->freq_hz, d->level_cnt * sizeof(ULONG));
        g_domain.perf_freq_hz = frequencies;
        g_domain.perf_level_cnt = d->level_cnt;
        g_domain.domain_id = d->scmi_domain_id;
        g_domain.domain_members = d->cpu_cnt;
        g_domain.min_freq_hz = frequencies[0];
        g_domain.max_freq_hz = frequencies[d->non_boost_max_level];
        g_domain.nominal_freq_hz = g_domain.max_freq_hz;
        g_domain.lowest_nonlinear_freq_hz = frequencies[d->android_idle_floor_level];
        g_domain.enable_boost = 1;
        g_domain.boost_freq_hz = frequencies[d->level_cnt - 1];
        g_domain.enable_thermal_perf_selection =
            0; /* cpu voltage/thermal caps owned by PDP firmware. */
        return STATUS_SUCCESS;
    }

    ULONG max_hz = registry_query_u_long(L"CpuMaxFrequencyHz", 0);
    ULONG min_hz = registry_query_u_long(L"CpuMinFrequencyHz", 0);
    ULONG nom_hz = registry_query_u_long(L"CpuNominalFrequencyHz", 0);
    ULONG lnl_hz = registry_query_u_long(L"CpuLowestNonlinearFrequencyHz", 0);
    ULONG cluster_idx = registry_query_u_long(L"CpuClusterIndex", g_domain.domain_id);

    if (!max_hz)
        max_hz = 1000000;
    if (!min_hz)
        min_hz = max_hz / 4;
    if (!nom_hz)
        nom_hz = max_hz;
    if (!lnl_hz)
        lnl_hz = min_hz;

    ULONG perf_cnt = registry_query_u_long(L"CpuPerfLevelCount", 0);
    ULONG disc = registry_query_u_long(L"DiscretePerfStateCount", 0);
    if (!perf_cnt && disc)
        perf_cnt = disc;
    if (!perf_cnt)
        perf_cnt = 4;

    perf_cnt = clamp_u32(perf_cnt, 1, 64);

    ULONG boost = registry_query_u_long(L"CpuBoostFrequencyHz", 0);
    ULONG boost_enable = 0;
    ULONG boost_delta_hz = 0;
    ULONG always_boost = 0;

    if (!dcvs_query_cluster_value_with_fallback(cluster_idx, L"PerformanceBoost", &boost_enable))
        boost_enable = registry_query_u_long(L"PerformanceBoost", 0);
    if (!dcvs_query_cluster_value_with_fallback(cluster_idx, L"BoostDeltaHz", &boost_delta_hz))
        boost_delta_hz = registry_query_u_long(L"CpuBoostDeltaHz", 0);
    if (!dcvs_query_cluster_value_with_fallback(cluster_idx, L"AlwaysBoostPerfEnabled",
                                                &always_boost))
        always_boost = registry_query_u_long(L"AlwaysBoostPerfEnabled", 0);

    if (boost == 0 && boost_enable != 0 && boost_delta_hz != 0) {
        ULONGLONG sum = (ULONGLONG)max_hz + (ULONGLONG)boost_delta_hz;
        if (sum > 0xFFFFFFFFull)
            sum = 0xFFFFFFFFull;
        boost = (ULONG)sum;
    }

    g_domain.enable_boost = (boost_enable != 0 && boost > max_hz);
    g_domain.always_boost_perf_enabled = always_boost ? 1u : 0u;
    g_domain.boost_freq_hz = boost;

    ULONG extra = g_domain.enable_boost ? 1 : 0;
    ULONG total = perf_cnt + extra;

    ULONG* table = (ULONG*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(ULONG) * total, POOL_TAG);
    if (!table)
        return STATUS_INSUFFICIENT_RESOURCES;

    ULONG span = max_hz - min_hz;
    if (span == 0)
        span = 1;

    for (ULONG i = 0; i < perf_cnt; i++) {
        ULONG f;
        if (perf_cnt == 1)
            f = max_hz;
        else
            f = min_hz + (span * i) / (perf_cnt - 1);
        f = dcvs_query_perf_level_freq_override(cluster_idx, i, f);
        table[i] = f;
    }

    for (ULONG i = 1; i < perf_cnt; i++) {
        if (table[i] < table[i - 1])
            table[i] = table[i - 1];
    }

    if (g_domain.enable_boost)
        table[total - 1] = boost;

    if (perf_cnt != 0) {
        g_domain.min_freq_hz = table[0];
        g_domain.max_freq_hz = table[perf_cnt - 1];
    } else {
        g_domain.min_freq_hz = min_hz;
        g_domain.max_freq_hz = max_hz;
    }

    if (nom_hz < g_domain.min_freq_hz || nom_hz > g_domain.max_freq_hz)
        nom_hz = g_domain.max_freq_hz;
    if (lnl_hz < g_domain.min_freq_hz || lnl_hz > nom_hz)
        lnl_hz = g_domain.min_freq_hz;

    g_domain.nominal_freq_hz = nom_hz;
    g_domain.lowest_nonlinear_freq_hz = lnl_hz;
    g_domain.perf_level_cnt = total;
    g_domain.perf_freq_hz = table;

    return STATUS_SUCCESS;
}

NTSTATUS dcvs_init(VOID)
{
    LONG init_state = InterlockedCompareExchange(&g_dcvs_init, 1, 0);
    if (init_state == 2)
        return STATUS_SUCCESS;
    if (init_state != 0)
        return STATUS_DEVICE_NOT_READY;

    /* a late entrant from a previously stopped DCVS instance must not mistake the next ready=2
     * for the generation it initially observed. */

    InterlockedIncrement(&g_dcvs_generation);
    dcvs_ensure_trace_init();
    RtlZeroMemory(&g_domain, sizeof(g_domain));

    g_cpu_cnt = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (!g_cpu_cnt)
        g_cpu_cnt = 1;

    /* registry intent is not hardware ownership. rejects the unverified fixed-address firmware
     * transport even if an old INF or service key requests it. The existing software-table
     * fallback is not exposed as a physical Molokai processor performance provider. */

    ULONG dvfs_requested = registry_query_u_long(L"MolokaiDvfsEnable", 0);
    BOOLEAN cpu_owner_verified = molokai_transport_ownership_verified();
    g_molokai_enabled = (dvfs_requested == 1u && cpu_owner_verified);
    if (dvfs_requested == 1u && !cpu_owner_verified)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_PPM,
                    "ppm: DVFS requested owner=unverified writes=disabled");
    g_molokai_enforce_android_floor =
        (registry_query_u_long(L"MolokaiEnforceAndroidFloor", 0) == 1);
    if (g_molokai_enabled) {
        /* proc numbers must correspond to Molokai's two physical clusters. */
        if (g_cpu_cnt != MOLOKAI_CPU_CNT) {
            InterlockedExchange(&g_dcvs_init, 0);
            return STATUS_NOT_SUPPORTED;
        }
        KeInitializeSpinLock(&g_molokai_cpu_state_lock);
    }

    g_domain.domain_id = registry_query_u_long(L"PerfDomainId", 0);
    g_domain.domain_members = registry_query_u_long(L"PerfDomainMembers", 0);
    if (!g_domain.domain_members) {
        g_domain.domain_members = g_cpu_cnt ? g_cpu_cnt : 1;
    } else if (g_domain.domain_members > g_cpu_cnt) {

        ULONG mask = g_domain.domain_members;
        ULONG cnt = 0;
        while (mask) {
            cnt += (mask & 1u);
            mask >>= 1;
        }
        g_domain.domain_members = cnt ? cnt : (g_cpu_cnt ? g_cpu_cnt : 1);
    }

    {
        ULONG v = 0;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"DirectedPState",
                                                        L"DomainCoordinationType", &v))) {
            g_domain.coordination_type = v;
        } else {
            g_domain.coordination_type = registry_query_u_long(L"DomainCoordinationType", 2);
        }
    }
    g_domain.worst_case_transition_latency =
        registry_query_u_long(L"WorstCaseTransitionLatency", 0);
    g_domain.worst_case_transition_overhead =
        registry_query_u_long(L"WorstCaseTransitionOverhead", 0);

    g_domain.enable_perf_selection = registry_query_u_long(L"EnableCVSPerfSelection", 0);
    g_domain.enable_thermal_perf_selection =
        registry_query_u_long(L"EnableCVSThermalPerfSelection", 0);

    g_dcvs_thermal_hot_deci_c = registry_query_u_long(L"ThermalHotDeciC", 850);
    g_dcvs_thermal_critical_deci_c = registry_query_u_long(L"ThermalCriticalDeciC", 950);
    if (g_dcvs_thermal_critical_deci_c <= g_dcvs_thermal_hot_deci_c)
        g_dcvs_thermal_critical_deci_c = g_dcvs_thermal_hot_deci_c + 50;

    g_dcvs_thermal_cap_hot_percent =
        clamp_u32(registry_query_u_long(L"ThermalPerfCapPercentAtHot", 70), 1, 100);
    g_dcvs_thermal_cap_critical_percent =
        clamp_u32(registry_query_u_long(L"ThermalPerfCapPercentAtCritical", 40), 1, 100);
    if (g_dcvs_thermal_cap_critical_percent > g_dcvs_thermal_cap_hot_percent)
        g_dcvs_thermal_cap_critical_percent = g_dcvs_thermal_cap_hot_percent;

    NTSTATUS st = dcvs_build_perf_table();
    if (!NT_SUCCESS(st)) {
        dcvs_free_domain();
        InterlockedExchange(&g_dcvs_init, 0);
        return st;
    }

    /* fail reg if Molokai firmware transport cannot be mapped. Never advertise working DVFS
     * when writes are only software bookkeeping. */

    if (g_molokai_enabled) {
        st = molokai_transport_init();
        if (!NT_SUCCESS(st)) {
            dcvs_free_domain();
            InterlockedExchange(&g_dcvs_init, 0);
            return st;
        }
    }

    g_cpu =
        (dcvs_cpu_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(dcvs_cpu_t) * g_cpu_cnt, POOL_TAG);
    if (!g_cpu) {
        if (g_molokai_enabled)
            molokai_transport_deinit();
        dcvs_free_domain();
        InterlockedExchange(&g_dcvs_init, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(g_cpu, sizeof(dcvs_cpu_t) * g_cpu_cnt);

    ULONGLONG now = dcvs_now100ns();
    ULONG init_level = (g_domain.perf_level_cnt ? (g_domain.perf_level_cnt - 1) : 0);
    ULONG init_hz =
        g_domain.perf_level_cnt ? g_domain.perf_freq_hz[init_level] : g_domain.max_freq_hz;

    for (ULONG i = 0; i < g_cpu_cnt; i++) {
        const molokai_domain_model_t* d =
            g_molokai_enabled ? molokai_domain(molokai_domain_for_cpu(i)) : NULL;
        g_cpu[i].domain_idx = d ? d->scmi_domain_id : 0;
        /* initial physical level is unknown until Windows makes a request. */
        g_cpu[i].current_perf_level = d ? 0u : init_level;
        g_cpu[i].current_freq_hz = d ? 0u : init_hz;
        g_cpu[i].requested_perf_state_idx = 0;
        g_cpu[i].min_perf = 0;
        g_cpu[i].max_perf = 100;
        g_cpu[i].desired_perf = 0;
        g_cpu[i].epp = 0;
        g_cpu[i].enter_time100ns = now;
        g_cpu[i].residency100ns = 0;
        g_cpu[i].transition_cnt = 0;
    }

    /* publish only after all memory allocations and firmware mappings succeed. */
    InterlockedExchange(&g_dcvs_init, 2);

    dcvs_record_event(DCVS_EVENT_INIT, STATUS_SUCCESS, 0xFFFFFFFFu, 0, 0, 0, init_level, init_hz,
                      compute_thermal_cap_percent(), 0, g_cpu_cnt, g_domain.perf_level_cnt);

    return STATUS_SUCCESS;
}

VOID dcvs_deinit(VOID)
{
    /* a simple ready check before using g_cpu is not a lifetime guarantee; another thread may
     * free g_cpu between that check and the first dereference. */

    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !dcvs_rundown_begin_stop(&g_dcvs_init))
        return;

    /* avoid spinning at elevated IRQL while processor callbacks run. */

    while (!dcvs_rundown_empty(&g_dcvs_users)) {
        LARGE_INTEGER pause;
        pause.QuadPart = -10000LL;
        (VOID) KeDelayExecutionThread(KernelMode, FALSE, &pause);
    }

    dcvs_record_event(DCVS_EVENT_DEINIT, STATUS_SUCCESS, 0xFFFFFFFFu, 0, 0, 0, 0, 0,
                      compute_thermal_cap_percent(), 0, g_cpu_cnt, g_domain.perf_level_cnt);

    if (g_cpu) {
        ExFreePool(g_cpu);
        g_cpu = NULL;
    }
    g_cpu_cnt = 0;
    if (g_molokai_enabled)
        molokai_transport_deinit();
    dcvs_free_domain();
    g_molokai_enabled = FALSE;
    InterlockedExchange(&g_dcvs_init, 0);
}

ULONG dcvs_get_cpu_cnt(VOID)
{
    return g_cpu_cnt ? g_cpu_cnt : 1;
}

ULONG dcvs_get_domain_id_for_cpu(_In_ ULONG cpu)
{
    if (g_molokai_enabled)
        return molokai_domain_for_cpu(cpu);
    return g_domain.domain_id;
}

ULONG dcvs_get_domain_members_for_cpu(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->cpu_cnt : 0u;
    }
    return g_domain.domain_members;
}

ULONG dcvs_get_domain_coordination_type(VOID)
{
    return g_domain.coordination_type;
}

ULONG dcvs_get_worst_case_transition_latency(_In_ ULONG cpu)
{
    UNREFERENCED_PARAMETER(cpu);
    return g_domain.worst_case_transition_latency;
}

ULONG dcvs_get_worst_case_transition_overhead(_In_ ULONG cpu)
{
    UNREFERENCED_PARAMETER(cpu);
    return g_domain.worst_case_transition_overhead;
}

ULONG dcvs_get_perf_level_cnt(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->level_cnt : 1u;
    }
    return g_domain.perf_level_cnt ? g_domain.perf_level_cnt : 1;
}

/* caller holds the DCVS rundown reference. */

static __forceinline ULONG dcvs_legacy_freq_held(_In_ ULONG perf_level)
{
    if (!g_domain.perf_freq_hz || !g_domain.perf_level_cnt)
        return g_domain.max_freq_hz;
    if (perf_level >= g_domain.perf_level_cnt)
        perf_level = g_domain.perf_level_cnt - 1u;
    return g_domain.perf_freq_hz[perf_level];
}

ULONG dcvs_get_perf_freq_hz_by_perf_level(_In_ ULONG cpu, _In_ ULONG perf_level)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        if (!d)
            return 0u;
        if (perf_level >= d->level_cnt)
            perf_level = d->level_cnt - 1u;
        return d->freq_hz[perf_level];
    }

    ULONG hz = 0;
    if (!dcvs_enter())
        return 0u;
    hz = dcvs_legacy_freq_held(perf_level);
    dcvs_leave();
    return hz;
}

ULONG dcvs_get_perf_freq_hz_by_perf_state_idx(_In_ ULONG cpu, _In_ ULONG perf_state_idx)
{
    ULONG levels = dcvs_get_perf_level_cnt(cpu);
    if (!levels)
        return dcvs_get_max_freq_hz(cpu);
    if (perf_state_idx >= levels)
        perf_state_idx = levels - 1;
    ULONG perf_level = (levels - 1) - perf_state_idx;
    return dcvs_get_perf_freq_hz_by_perf_level(cpu, perf_level);
}

ULONG dcvs_get_max_freq_hz(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->freq_hz[d->level_cnt - 1u] : 0u;
    }
    if (g_domain.enable_boost && g_domain.boost_freq_hz > g_domain.max_freq_hz)
        return g_domain.boost_freq_hz;
    return g_domain.max_freq_hz ? g_domain.max_freq_hz : 1;
}

ULONG dcvs_get_max_non_boost_freq_hz(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->freq_hz[d->non_boost_max_level] : 0u;
    }
    return g_domain.max_freq_hz ? g_domain.max_freq_hz : 1;
}

ULONG dcvs_get_min_freq_hz(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->freq_hz[0] : 0u;
    }
    return g_domain.min_freq_hz ? g_domain.min_freq_hz : 1;
}

ULONG dcvs_get_nominal_freq_hz(_In_ ULONG cpu)
{
    if (g_molokai_enabled)
        return dcvs_get_max_non_boost_freq_hz(cpu);
    return g_domain.nominal_freq_hz ? g_domain.nominal_freq_hz : dcvs_get_max_freq_hz(cpu);
}

ULONG dcvs_get_lowest_nonlinear_freq_hz(_In_ ULONG cpu)
{
    if (g_molokai_enabled) {
        const molokai_domain_model_t* d = molokai_domain(molokai_domain_for_cpu(cpu));
        return d ? d->freq_hz[d->android_idle_floor_level] : 0u;
    }
    return g_domain.lowest_nonlinear_freq_hz ? g_domain.lowest_nonlinear_freq_hz
                                             : dcvs_get_min_freq_hz(cpu);
}

ULONG dcvs_get_current_freq_hz(_In_ ULONG cpu)
{
    ULONG hz = 0;
    if (!dcvs_enter())
        return dcvs_get_max_freq_hz(cpu);
    if (g_cpu && cpu < g_cpu_cnt) {
        hz = g_cpu[cpu].current_freq_hz;
        if (!g_molokai_enabled && !hz)
            hz = dcvs_get_max_freq_hz(cpu);
    } else {
        hz = dcvs_get_max_freq_hz(cpu);
    }
    dcvs_leave();
    /* zero means the physical Molokai freq is not yet known. */
    return hz;
}

ULONG dcvs_get_current_perf_level(_In_ ULONG cpu)
{
    ULONG level = 0;
    if (!dcvs_enter())
        return 0u;
    if (g_cpu && cpu < g_cpu_cnt)
        level = g_cpu[cpu].current_perf_level;
    dcvs_leave();
    return level;
}

ULONG dcvs_get_cpu_normalized_performance(_In_ ULONG cpu, _In_ ULONG freq_hz)
{
    if (g_molokai_enabled)
        return molokai_normalized_performance(molokai_domain_for_cpu(cpu), freq_hz);

    ULONG scale_permille = registry_query_u_long(L"CpuPerfNormalizationPermille", 1000);
    if (!scale_permille)
        scale_permille = 1000;

    ULONGLONG perf = ((ULONGLONG)freq_hz * (ULONGLONG)scale_permille) / 1000ULL;
    if (perf > 0xFFFFFFFFull)
        perf = 0xFFFFFFFFull;

    return (ULONG)perf;
}

NTSTATUS dcvs_perf_set_state_checked(_In_ ULONG cpu, _In_ ULONG minimum_performance_state,
                                     _In_ ULONG maximum_performance_state,
                                     _In_ ULONG desired_performance_state,
                                     _In_ ULONG energy_performance_preference)
{
    if (!dcvs_enter())
        return STATUS_DEVICE_NOT_READY;
    if (!g_cpu || cpu >= g_cpu_cnt) {
        dcvs_leave();
        return STATUS_DEVICE_NOT_READY;
    }

    if (g_molokai_enabled) {
        const ULONG domain = molokai_domain_for_cpu(cpu);
        const molokai_domain_model_t* d = molokai_domain(domain);
        ULONG state, level, hz;
        NTSTATUS status;
        KIRQL irql;
        ULONGLONG now;
        ULONG previous_level, previous_hz;
        if (!d) {
            dcvs_leave();
            return STATUS_INVALID_PARAMETER;
        }
        if (KeGetCurrentIrql() > DISPATCH_LEVEL) {
            dcvs_leave();
            return STATUS_INVALID_DEVICE_STATE;
        }
        state = desired_performance_state < d->level_cnt ? desired_performance_state
                                                         : d->level_cnt - 1u;
        level = molokai_level_from_win_state(domain, state, g_molokai_enforce_android_floor);
        hz = d->freq_hz[level];
        KeAcquireSpinLock(&g_molokai_cpu_state_lock, &irql);
        previous_level = g_cpu[cpu].current_perf_level;
        previous_hz = g_cpu[cpu].current_freq_hz;
        status = molokai_transport_set_level(domain, level);
        if (NT_SUCCESS(status)) {
            now = dcvs_now100ns();
            /* all processors in this SCMI domain share one hardware clock. */
            for (ULONG i = d->first_cpu; i < d->first_cpu + d->cpu_cnt; ++i) {
                dcvs_cpu_t* c = &g_cpu[i];
                if (c->current_freq_hz != hz)
                    InterlockedIncrement64(&c->transition_cnt);
                if (now > c->enter_time100ns)
                    InterlockedAdd64(&c->residency100ns, (LONG64)(now - c->enter_time100ns));
                c->current_perf_level = level;
                c->current_freq_hz = hz;
                c->enter_time100ns = now;
            }
            g_cpu[cpu].requested_perf_state_idx = state;
            g_cpu[cpu].min_perf = minimum_performance_state;
            g_cpu[cpu].max_perf = maximum_performance_state;
            g_cpu[cpu].desired_perf = state;
            g_cpu[cpu].epp = energy_performance_preference;
        }
        KeReleaseSpinLock(&g_molokai_cpu_state_lock, irql);
        dcvs_record_event(DCVS_EVENT_PERF_SET, status, cpu, minimum_performance_state,
                          maximum_performance_state, state, level,
                          NT_SUCCESS(status) ? hz : previous_hz, 100, energy_performance_preference,
                          previous_level, previous_hz);
        dcvs_leave();
        return status;
    }

    ULONGLONG now = dcvs_now100ns();

    dcvs_cpu_t* c = &g_cpu[cpu];

    ULONGLONG dt = 0;
    if (now > c->enter_time100ns)
        dt = now - c->enter_time100ns;

    InterlockedAdd64(&c->residency100ns, (LONG64)dt);

    ULONG levels = g_domain.perf_level_cnt ? g_domain.perf_level_cnt : 1;
    if (!levels)
        levels = 1;

    ULONG min_state = clamp_u32(minimum_performance_state, 0, levels - 1);
    ULONG max_state = clamp_u32(maximum_performance_state, 0, levels - 1);
    ULONG des_state = clamp_u32(desired_performance_state, 0, levels - 1);

    ULONG desired_level = (levels - 1) - des_state;
    if (g_domain.enable_boost && g_domain.always_boost_perf_enabled)
        desired_level = levels - 1;

    ULONG target_hz = dcvs_legacy_freq_held(desired_level);

    ULONG cap = compute_thermal_cap_percent();
    ULONG cap_hz = (ULONG)(((ULONGLONG)dcvs_get_max_freq_hz(cpu) * (ULONGLONG)cap) / 100ULL);
    if (target_hz > cap_hz)
        target_hz = cap_hz;

    if (!target_hz)
        target_hz = dcvs_get_min_freq_hz(cpu);

    ULONG prev_hz = c->current_freq_hz;
    ULONG prev_level = c->current_perf_level;

    c->requested_perf_state_idx = des_state;
    c->min_perf = min_state;
    c->max_perf = max_state;
    c->desired_perf = des_state;
    c->epp = energy_performance_preference;

    if (c->current_freq_hz != target_hz)
        InterlockedIncrement64(&c->transition_cnt);

    c->current_perf_level = desired_level;
    c->current_freq_hz = target_hz;
    c->enter_time100ns = now;

    dcvs_record_event(DCVS_EVENT_PERF_SET, STATUS_SUCCESS, cpu, min_state, max_state, des_state,
                      desired_level, target_hz, cap, energy_performance_preference, prev_level,
                      prev_hz);
    dcvs_leave();
    return STATUS_SUCCESS;
}

/* preserve legacy src ABI while exposing actual PDP submission status. */
VOID dcvs_perf_set_state(_In_ ULONG cpu, _In_ ULONG minimum_performance_state,
                         _In_ ULONG maximum_performance_state, _In_ ULONG desired_performance_state,
                         _In_ ULONG energy_performance_preference)
{
    (VOID) dcvs_perf_set_state_checked(cpu, minimum_performance_state, maximum_performance_state,
                                       desired_performance_state, energy_performance_preference);
}

VOID dcvs_perf_check_complete(VOID)
{
    KeMemoryBarrier();
}

VOID dcvs_reset_feedback_counters(VOID)
{
    if (!dcvs_enter())
        return;
    if (!g_cpu || !g_cpu_cnt) {
        dcvs_leave();
        return;
    }

    ULONGLONG now = dcvs_now100ns();

    for (ULONG i = 0; i < g_cpu_cnt; i++) {
        g_cpu[i].enter_time100ns = now;
        InterlockedExchange64(&g_cpu[i].residency100ns, 0);
        InterlockedExchange64(&g_cpu[i].transition_cnt, 0);
    }

    dcvs_record_event(DCVS_EVENT_RESET_FEEDBACK, STATUS_SUCCESS, 0xFFFFFFFFu, 0, 0, 0, 0, 0,
                      compute_thermal_cap_percent(), 0, g_cpu_cnt, 0);
    dcvs_leave();
}

VOID dcvs_get_snapshot(_Out_ dcvs_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 1;
    if (!dcvs_enter())
        return;
    snapshot->initialized = (InterlockedCompareExchange(&g_dcvs_init, 0, 0) == 2) ? 1u : 0u;
    snapshot->cpu_cnt = g_cpu_cnt;
    snapshot->domain_id = g_domain.domain_id;
    snapshot->domain_members = g_domain.domain_members;
    snapshot->coordination_type = g_domain.coordination_type;
    snapshot->worst_case_transition_latency = g_domain.worst_case_transition_latency;
    snapshot->worst_case_transition_overhead = g_domain.worst_case_transition_overhead;
    snapshot->enable_perf_selection = g_domain.enable_perf_selection;
    snapshot->enable_thermal_perf_selection = g_domain.enable_thermal_perf_selection;
    snapshot->enable_boost = g_domain.enable_boost;
    snapshot->boost_freq_hz = g_domain.boost_freq_hz;
    snapshot->max_freq_hz = g_domain.max_freq_hz;
    snapshot->min_freq_hz = g_domain.min_freq_hz;
    snapshot->nominal_freq_hz = g_domain.nominal_freq_hz;
    snapshot->lowest_nonlinear_freq_hz = g_domain.lowest_nonlinear_freq_hz;
    snapshot->thermal_hot_deci_c = g_dcvs_thermal_hot_deci_c;
    snapshot->thermal_critical_deci_c = g_dcvs_thermal_critical_deci_c;
    snapshot->thermal_cap_hot_percent = g_dcvs_thermal_cap_hot_percent;
    snapshot->thermal_cap_critical_percent = g_dcvs_thermal_cap_critical_percent;
    snapshot->current_thermal_cap_percent = compute_thermal_cap_percent();

    ULONG perf_cnt = g_domain.perf_level_cnt;
    if (perf_cnt > DCVS_PERF_CAPACITY)
        perf_cnt = DCVS_PERF_CAPACITY;
    snapshot->perf_table_cnt = perf_cnt;
    for (ULONG i = 0; i < perf_cnt; i++)
        snapshot->perf_table_hz[i] = g_domain.perf_freq_hz ? g_domain.perf_freq_hz[i] : 0;

    ULONG cpu_cnt = g_cpu_cnt;
    if (cpu_cnt > DCVS_CPU_CAPACITY)
        cpu_cnt = DCVS_CPU_CAPACITY;
    snapshot->cpu_entry_cnt = cpu_cnt;

    ULONGLONG now = dcvs_now100ns();
    for (ULONG i = 0; i < cpu_cnt; i++) {
        dcvs_cpu_t* c = &g_cpu[i];
        snapshot->cpus[i].cpu = i;
        snapshot->cpus[i].domain_idx = c->domain_idx;
        snapshot->cpus[i].current_perf_level = c->current_perf_level;
        snapshot->cpus[i].current_freq_hz = c->current_freq_hz;
        snapshot->cpus[i].requested_perf_state_idx = c->requested_perf_state_idx;
        snapshot->cpus[i].min_perf = c->min_perf;
        snapshot->cpus[i].max_perf = c->max_perf;
        snapshot->cpus[i].desired_perf = c->desired_perf;
        snapshot->cpus[i].epp = c->epp;
        snapshot->cpus[i].enter_time100ns = c->enter_time100ns;
        snapshot->cpus[i].transition_cnt = (ULONGLONG)InterlockedAdd64(&c->transition_cnt, 0);

        ULONGLONG residency = (ULONGLONG)InterlockedAdd64(&c->residency100ns, 0);
        if (now > c->enter_time100ns)
            residency += (now - c->enter_time100ns);
        snapshot->cpus[i].residency100ns = residency;
        snapshot->cpus[i].normalized_performance =
            dcvs_get_cpu_normalized_performance(i, c->current_freq_hz);
    }

    dcvs_ensure_trace_init();
    KIRQL irql;
    KeAcquireSpinLock(&g_dcvs_trace_lock, &irql);
    snapshot->policy_update_cnt = g_dcvs_policy_update_cnt;
    snapshot->last_policy_tick100ns = g_dcvs_last_policy_tick100ns;
    snapshot->event_sequence = g_dcvs_event_sequence;
    snapshot->history_cnt = g_dcvs_history_cnt;
    snapshot->history_dropped = g_dcvs_history_dropped;
    snapshot->last_event_code = g_dcvs_last_event_code;
    snapshot->last_event_status = g_dcvs_last_event_status;
    snapshot->last_event_cpu = g_dcvs_last_event_cpu;
    snapshot->last_event_min_perf_state = g_dcvs_last_event_min_perf_state;
    snapshot->last_event_max_perf_state = g_dcvs_last_event_max_perf_state;
    snapshot->last_event_desired_perf_state = g_dcvs_last_event_desired_perf_state;
    snapshot->last_event_applied_perf_level = g_dcvs_last_event_applied_perf_level;
    snapshot->last_event_applied_freq_hz = g_dcvs_last_event_applied_freq_hz;
    snapshot->last_event_thermal_cap_percent = g_dcvs_last_event_thermal_cap_percent;
    snapshot->last_event_epp = g_dcvs_last_event_epp;
    snapshot->last_event_aux0 = g_dcvs_last_event_aux0;
    snapshot->last_event_aux1 = g_dcvs_last_event_aux1;
    snapshot->last_event_tick100ns = g_dcvs_last_event_tick100ns;
    KeReleaseSpinLock(&g_dcvs_trace_lock, irql);
    dcvs_leave();
}

NTSTATUS dcvs_get_history(_Out_writes_to_(capacity, *cnt) dcvs_event_t* events, _In_ ULONG capacity,
                          _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    dcvs_ensure_trace_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_dcvs_trace_lock, &irql);

    ULONG avail = g_dcvs_history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;
    ULONG start =
        (g_dcvs_history_head + DCVS_HISTORY_CAPACITY - avail + skip) % DCVS_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % DCVS_HISTORY_CAPACITY;
        events[i] = g_dcvs_history[idx];
    }

    *cnt = take;
    *dropped = g_dcvs_history_dropped + skip;

    KeReleaseSpinLock(&g_dcvs_trace_lock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS dcvs_get_perf_table(_Out_writes_to_(capacity, *cnt) ULONG* frequencies_hz,
                             _In_ ULONG capacity, _Out_ ULONG* cnt)
{
    if (!frequencies_hz || !cnt || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    if (!dcvs_enter())
        return STATUS_DEVICE_NOT_READY;
    ULONG avail = g_domain.perf_level_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    for (ULONG i = 0; i < take; i++)
        frequencies_hz[i] = g_domain.perf_freq_hz ? g_domain.perf_freq_hz[i] : 0;
    *cnt = take;
    dcvs_leave();
    return STATUS_SUCCESS;
}

NTSTATUS dcvs_set_policy(_In_ ULONG flags, _In_ ULONG enable_perf_selection,
                         _In_ ULONG enable_thermal_perf_selection, _In_ ULONG thermal_hot_deci_c,
                         _In_ ULONG thermal_critical_deci_c, _In_ ULONG thermal_cap_hot_percent,
                         _In_ ULONG thermal_cap_critical_percent)
{

    const ULONG persist = (flags & 0x10u) ? 1u : 0u;

    if (flags & 0x1u) {
        g_domain.enable_perf_selection = enable_perf_selection ? 1u : 0u;
        if (persist)
            (VOID) registry_set_u_long(L"EnableCVSPerfSelection", g_domain.enable_perf_selection);
    }

    if (flags & 0x2u) {
        g_domain.enable_thermal_perf_selection = enable_thermal_perf_selection ? 1u : 0u;
        if (persist)
            (VOID) registry_set_u_long(L"EnableCVSThermalPerfSelection",
                                       g_domain.enable_thermal_perf_selection);
    }

    if (flags & 0x4u) {
        ULONG hot = thermal_hot_deci_c;
        ULONG critical = thermal_critical_deci_c;
        if (critical <= hot)
            critical = hot + 50;

        g_dcvs_thermal_hot_deci_c = hot;
        g_dcvs_thermal_critical_deci_c = critical;

        if (persist) {
            (VOID) registry_set_u_long(L"ThermalHotDeciC", g_dcvs_thermal_hot_deci_c);
            (VOID) registry_set_u_long(L"ThermalCriticalDeciC", g_dcvs_thermal_critical_deci_c);
        }
    }

    if (flags & 0x8u) {
        ULONG cap_hot = clamp_u32(thermal_cap_hot_percent, 1, 100);
        ULONG cap_critical = clamp_u32(thermal_cap_critical_percent, 1, 100);
        if (cap_critical > cap_hot)
            cap_critical = cap_hot;

        g_dcvs_thermal_cap_hot_percent = cap_hot;
        g_dcvs_thermal_cap_critical_percent = cap_critical;

        if (persist) {
            (VOID)
                registry_set_u_long(L"ThermalPerfCapPercentAtHot", g_dcvs_thermal_cap_hot_percent);
            (VOID) registry_set_u_long(L"ThermalPerfCapPercentAtCritical",
                                       g_dcvs_thermal_cap_critical_percent);
        }
    }

    if (flags & 0x20u)
        dcvs_reset_feedback_counters();

    ULONGLONG now = dcvs_now100ns();
    dcvs_ensure_trace_init();
    KIRQL irql;
    KeAcquireSpinLock(&g_dcvs_trace_lock, &irql);
    g_dcvs_policy_update_cnt++;
    g_dcvs_last_policy_tick100ns = now;
    KeReleaseSpinLock(&g_dcvs_trace_lock, irql);

    dcvs_record_event(DCVS_EVENT_POLICY_UPDATE, STATUS_SUCCESS, 0xFFFFFFFFu,
                      g_domain.enable_perf_selection, g_domain.enable_thermal_perf_selection, flags,
                      0, 0, compute_thermal_cap_percent(), 0, g_dcvs_thermal_hot_deci_c,
                      g_dcvs_thermal_critical_deci_c);

    return STATUS_SUCCESS;
}
