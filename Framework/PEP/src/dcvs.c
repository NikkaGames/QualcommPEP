#include "dcvs.h"
#include "soc_model.h"
#include "soc_transport.h"
#include "cpu_device.h"
#include "dcvs_rundown.h"
#include "trace.h"
#include "dcvs.tmh"

static volatile LONG initialized;
static volatile LONG users;
static volatile LONG generation;
static KSPIN_LOCK requests_lock;
static dcvs_cpu_snapshot_t requests[MOLOKAI_CPU_CNT];

static BOOLEAN dcvs_enter(VOID)
{
    return dcvs_rundown_enter(&initialized, &users, &generation);
}

static VOID dcvs_leave(VOID)
{
    dcvs_rundown_leave(&users);
}

BOOLEAN dcvs_ready(VOID)
{
    return InterlockedCompareExchange(&initialized, 0, 0) == 2 && molokai_transport_ready();
}

NTSTATUS dcvs_init(VOID)
{
    NTSTATUS status;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (InterlockedCompareExchange(&initialized, 1, 0) != 0)
        return dcvs_ready() ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    InterlockedIncrement(&generation);
    KeInitializeSpinLock(&requests_lock);
    /* A disabled CPU provider does not affect device power management. */
    if (registry_query_u_long(L"CpuPerformanceEnable", 1u) != 1u) {
        InterlockedExchange(&initialized, 3);
        return STATUS_SUCCESS;
    }
    status = molokai_transport_init();
    if (NT_SUCCESS(status))
        status = cpu_device_init();
    if (!NT_SUCCESS(status)) {
        molokai_transport_deinit();
        InterlockedExchange(&initialized, 3);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_PPM, "ppm: CPU performance unavailable status=%08X",
                    status);
        return STATUS_SUCCESS;
    }
    for (ULONG cpu = 0u; cpu < MOLOKAI_CPU_CNT; ++cpu) {
        requests[cpu].cpu = cpu;
        requests[cpu].domain_idx = molokai_domain_for_cpu(cpu);
        requests[cpu].requested_perf_state_idx = MAXULONG;
    }
    InterlockedExchange(&initialized, 2);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PPM, "ppm: CPU performance initialized");
    return STATUS_SUCCESS;
}

VOID dcvs_deinit(VOID)
{
    LARGE_INTEGER delay;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !dcvs_rundown_begin_stop(&initialized))
        return;
    delay.QuadPart = -10000;
    while (!dcvs_rundown_empty(&users))
        (VOID) KeDelayExecutionThread(KernelMode, FALSE, &delay);
    molokai_transport_deinit();
}

ULONG dcvs_get_cpu_cnt(VOID)
{
    return dcvs_ready() ? MOLOKAI_CPU_CNT : 0u;
}

static const molokai_domain_model_t* model(ULONG cpu)
{
    return dcvs_ready() ? molokai_domain(molokai_domain_for_cpu(cpu)) : NULL;
}

ULONG dcvs_get_domain_id_for_cpu(ULONG cpu)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? d->scmi_domain_id : MAXULONG;
}

ULONG dcvs_get_domain_members_for_cpu(ULONG cpu)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? d->cpu_cnt : 0u;
}

ULONG dcvs_get_domain_coordination_type(VOID)
{
    return 1u; /* One request controls the complete firmware domain. */
}

ULONG dcvs_get_worst_case_transition_latency(ULONG cpu)
{
    return molokai_transport_latency(molokai_domain_for_cpu(cpu));
}

ULONG dcvs_get_worst_case_transition_overhead(ULONG cpu)
{
    UNREFERENCED_PARAMETER(cpu);
    return 0u;
}

ULONG dcvs_get_perf_level_cnt(ULONG cpu)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? d->level_cnt : 0u;
}

ULONG dcvs_get_perf_freq_hz_by_perf_level(ULONG cpu, ULONG level)
{
    const molokai_domain_model_t* d = model(cpu);
    return d && level < d->level_cnt ? d->freq_hz[level] : 0u;
}

ULONG dcvs_get_perf_freq_hz_by_perf_state_idx(ULONG cpu, ULONG index)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? molokai_freq_from_win_state(d->scmi_domain_id, index) : 0u;
}

ULONG dcvs_get_max_freq_hz(ULONG cpu)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? d->freq_hz[d->level_cnt - 1u] : 0u;
}

ULONG dcvs_get_max_non_boost_freq_hz(ULONG cpu)
{
    const molokai_domain_model_t* d = model(cpu);
    return d ? d->freq_hz[d->non_boost_max_level] : 0u;
}

ULONG dcvs_get_min_freq_hz(ULONG cpu)
{
    return dcvs_get_perf_freq_hz_by_perf_level(cpu, 0u);
}

ULONG dcvs_get_nominal_freq_hz(ULONG cpu)
{
    return dcvs_get_max_non_boost_freq_hz(cpu);
}

ULONG dcvs_get_lowest_nonlinear_freq_hz(ULONG cpu)
{
    return dcvs_get_min_freq_hz(cpu);
}

ULONG dcvs_get_current_perf_level(ULONG cpu)
{
    ULONG level = MAXULONG;
    if (!dcvs_enter())
        return level;
    if (cpu < MOLOKAI_CPU_CNT)
        (VOID) molokai_transport_get_level(molokai_domain_for_cpu(cpu), &level);
    dcvs_leave();
    return level;
}

ULONG dcvs_get_current_freq_hz(ULONG cpu)
{
    return dcvs_get_perf_freq_hz_by_perf_level(cpu, dcvs_get_current_perf_level(cpu));
}

ULONG dcvs_get_cpu_normalized_performance(ULONG cpu, ULONG frequency)
{
    return dcvs_ready() ? molokai_normalized_performance(molokai_domain_for_cpu(cpu), frequency)
                        : 0u;
}

NTSTATUS dcvs_perf_set_state_checked(ULONG cpu, ULONG minimum, ULONG maximum, ULONG desired,
                                     ULONG preference)
{
    const scmi_perf_domain_t* d;
    ULONG level, domain;
    NTSTATUS status;
    KIRQL irql;
    if (KeGetCurrentIrql() > DISPATCH_LEVEL || !dcvs_enter())
        return STATUS_DEVICE_NOT_READY;
    domain = molokai_domain_for_cpu(cpu);
    d = molokai_transport_domain(domain);
    if (cpu >= MOLOKAI_CPU_CNT || !scmi_perf_select(d, minimum, maximum, desired, &level)) {
        dcvs_leave();
        return STATUS_INVALID_PARAMETER;
    }
    KeAcquireSpinLock(&requests_lock, &irql);
    status = molokai_transport_set_level(domain, level);
    if (NT_SUCCESS(status)) {
        for (ULONG i = 0u; i < MOLOKAI_CPU_CNT; ++i) {
            dcvs_cpu_snapshot_t* request = &requests[i];
            if (molokai_domain_for_cpu(i) != domain)
                continue;
            request->requested_perf_state_idx = desired;
            request->min_perf = minimum;
            request->max_perf = maximum;
            request->desired_perf = desired;
            request->epp = preference;
        }
    }
    KeReleaseSpinLock(&requests_lock, irql);
    dcvs_leave();
    return status;
}

VOID dcvs_perf_set_state(ULONG cpu, ULONG minimum, ULONG maximum, ULONG desired, ULONG preference)
{
    (VOID) dcvs_perf_set_state_checked(cpu, minimum, maximum, desired, preference);
}

VOID dcvs_reset_feedback_counters(VOID)
{
    /* No synthetic frequency feedback counters are exposed. */
}

VOID dcvs_get_snapshot(dcvs_snapshot_t* snapshot)
{
    KIRQL irql;
    if (!snapshot)
        return;
    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 1u;
    if (!dcvs_enter())
        return;
    snapshot->initialized = dcvs_ready();
    snapshot->cpu_cnt = MOLOKAI_CPU_CNT;
    snapshot->domain_id = dcvs_get_domain_id_for_cpu(0u);
    snapshot->domain_members = dcvs_get_domain_members_for_cpu(0u);
    snapshot->coordination_type = dcvs_get_domain_coordination_type();
    snapshot->worst_case_transition_latency = dcvs_get_worst_case_transition_latency(0u);
    snapshot->enable_perf_selection = dcvs_ready();
    snapshot->max_freq_hz = dcvs_get_max_freq_hz(0u);
    snapshot->min_freq_hz = dcvs_get_min_freq_hz(0u);
    snapshot->nominal_freq_hz = dcvs_get_nominal_freq_hz(0u);
    snapshot->lowest_nonlinear_freq_hz = dcvs_get_lowest_nonlinear_freq_hz(0u);
    snapshot->boost_freq_hz = snapshot->max_freq_hz;
    snapshot->enable_boost = snapshot->max_freq_hz > snapshot->nominal_freq_hz;
    snapshot->perf_table_cnt = dcvs_get_perf_level_cnt(0u);
    snapshot->cpu_entry_cnt = MOLOKAI_CPU_CNT;
    for (ULONG i = 0u; i < snapshot->perf_table_cnt; ++i)
        snapshot->perf_table_hz[i] = dcvs_get_perf_freq_hz_by_perf_level(0u, i);
    KeAcquireSpinLock(&requests_lock, &irql);
    RtlCopyMemory(snapshot->cpus, requests, sizeof(requests));
    KeReleaseSpinLock(&requests_lock, irql);
    for (ULONG i = 0u; i < MOLOKAI_CPU_CNT; ++i) {
        ULONG level = MAXULONG;
        (VOID) molokai_transport_get_level(molokai_domain_for_cpu(i), &level);
        snapshot->cpus[i].current_perf_level = level;
        snapshot->cpus[i].current_freq_hz = dcvs_get_perf_freq_hz_by_perf_level(i, level);
        snapshot->cpus[i].normalized_performance =
            dcvs_get_cpu_normalized_performance(i, snapshot->cpus[i].current_freq_hz);
    }
    dcvs_leave();
}

NTSTATUS dcvs_get_history(dcvs_event_t* events, ULONG capacity, ULONG* count, ULONG* dropped)
{
    if (!events || !capacity || !count || !dropped)
        return STATUS_INVALID_PARAMETER;
    *count = 0u;
    *dropped = 0u;
    return STATUS_SUCCESS;
}

NTSTATUS dcvs_get_perf_table(ULONG* frequencies, ULONG capacity, ULONG* count)
{
    ULONG available;
    if (!frequencies || !capacity || !count)
        return STATUS_INVALID_PARAMETER;
    *count = 0u;
    if (!dcvs_enter())
        return STATUS_DEVICE_NOT_READY;
    available = dcvs_get_perf_level_cnt(0u);
    *count = capacity < available ? capacity : available;
    for (ULONG i = 0u; i < *count; ++i)
        frequencies[i] = dcvs_get_perf_freq_hz_by_perf_level(0u, i);
    dcvs_leave();
    return STATUS_SUCCESS;
}

NTSTATUS dcvs_set_policy(ULONG flags, ULONG selection, ULONG thermal_selection, ULONG hot,
                         ULONG critical, ULONG hot_cap, ULONG critical_cap)
{
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(selection);
    UNREFERENCED_PARAMETER(thermal_selection);
    UNREFERENCED_PARAMETER(hot);
    UNREFERENCED_PARAMETER(critical);
    UNREFERENCED_PARAMETER(hot_cap);
    UNREFERENCED_PARAMETER(critical_cap);
    /* Voltage sequencing and thermal limits remain firmware responsibilities. */
    return STATUS_NOT_SUPPORTED;
}
