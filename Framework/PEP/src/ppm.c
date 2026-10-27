#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "chipinfo.h"
#include "dcvs.h"
#include "peplog.h"
#include "public.h"
#include "dispatch/ppm_types.h"
#include "dispatch/main_priv.h"
#include "dispatch/park_priv.h"
#include "dispatch/ppm_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

static __forceinline ULONGLONG pep_read_cpu_perf_counter64(VOID);
VOID pep_query_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_feedback_counters(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_feedback_read(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

VOID pep_query_perf_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_constraints(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_check_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_discrete_perf_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_domain_info(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_set_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

static volatile LONG64 g_perf_check_complete_cnt = 0;

static __forceinline ULONGLONG pep_read_cpu_perf_counter64(VOID)
{
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)

    return (ULONGLONG)_ReadStatusReg(ARM64_SYSREG(3, 3, 9, 13, 0));
#else
    return KeQueryInterruptTime();
#endif
}

VOID pep_query_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    pep_proc_query_caps_t* caps = (pep_proc_query_caps_t*)data;
    if (!caps)
        return;

    ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);
    if (pep_handle && cpu < RTL_NUMBER_OF(g_cpu_kernel_handle)) {
        PVOID kernel_handle = pep_extract_kernel_handle_from_pep_handle(pep_handle);
        g_cpu_pep_handle[cpu] = pep_handle;
        g_cpu_kernel_handle[cpu] = kernel_handle ? kernel_handle : pep_handle;
    }

    ULONG disc = dcvs_get_perf_level_cnt(cpu);

    UCHAR directed_p_state = 0;
    (VOID) chip_info_get();
    if ((g.config.EnableDirectedPStateFeatures & 0x1u) != 0) {
        NTSTATUS st = dcvs_init();
        directed_p_state = NT_SUCCESS(st) ? 1u : 0u;
    }

    caps->version = 2;
    caps->reserved = 0;
    caps->directed_p_state = directed_p_state;
    caps->core_rotation = (UCHAR)(pep_is_core_rotation_disabled() ? 0 : 1);
    caps->discrete_perf_state_cnt = (UCHAR)disc;
}

VOID pep_query_feedback_counters(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    __try {
        ULONG* d = (ULONG*)data;
        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        ULONG max_hz = dcvs_get_max_non_boost_freq_hz(cpu);
        if (!max_hz)
            max_hz = 1;

        ULONG max_perf = dcvs_get_cpu_normalized_performance(cpu, max_hz);

        d[1] |= 1u;
        d[1] = (d[1] & 0xFFFFFFF9u) | 2u;
        d[1] &= 0xFFFFFF87u;
        d[2] = max_hz;
        d[1] |= 0x80u;

        d[3] |= 1u;
        d[3] = (d[3] & 0xFFFFFFF9u) | 2u;
        d[3] = (d[3] & 0xFFFFFF87u) | 8u;
        d[4] = max_perf;
        d[3] |= 0x80u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_feedback_read(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef union pep_feedback_u {
        ULONGLONG instantaneous_value;
        ULONGLONG nominal_cnt;
    } pep_feedback_u_t;

    typedef struct pep_ppm_feedback_read {
        ULONG counter_idx;
        ULONG pad;
        pep_feedback_u_t u;
        ULONGLONG actual_cnt;
    } pep_ppm_feedback_read_t;

    __try {
        pep_ppm_feedback_read_t* r = (pep_ppm_feedback_read_t*)data;

        if (r->counter_idx >= 2)
            return;

        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        LARGE_INTEGER perf_freq;
        LARGE_INTEGER perf_ticks = KeQueryPerformanceCounter(&perf_freq);
        ULONGLONG ticks = (ULONGLONG)perf_ticks.QuadPart;
        ULONGLONG tick_freq = (ULONGLONG)perf_freq.QuadPart;
        ULONG max_hz = dcvs_get_max_non_boost_freq_hz(cpu);
        if (!max_hz)
            max_hz = 1;

        ULONGLONG nominal = 0;
        if (tick_freq) {
            nominal = (ticks / tick_freq) * (ULONGLONG)max_hz;
            nominal += ((ticks % tick_freq) * (ULONGLONG)max_hz) / tick_freq;
        }

        r->u.nominal_cnt = nominal;
        r->actual_cnt = pep_read_cpu_perf_counter64();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_query_perf_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef struct pep_ppm_query_perf_caps {
        ULONG highest_performance;
        ULONG nominal_performance;
        ULONG lowest_nonlinear_performance;
        ULONG lowest_performance;
        ULONG domain_id;
        ULONG domain_members;
    } pep_ppm_query_perf_caps_t;

    __try {
        pep_ppm_query_perf_caps_t* c = (pep_ppm_query_perf_caps_t*)data;
        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        ULONG high_hz = dcvs_get_max_freq_hz(cpu);
        ULONG nom_hz = dcvs_get_max_non_boost_freq_hz(cpu);
        ULONG low_hz = dcvs_get_min_freq_hz(cpu);

        c->highest_performance = dcvs_get_cpu_normalized_performance(cpu, high_hz);
        c->nominal_performance = dcvs_get_cpu_normalized_performance(cpu, nom_hz);
        c->lowest_nonlinear_performance = dcvs_get_cpu_normalized_performance(cpu, low_hz);
        c->lowest_performance = dcvs_get_cpu_normalized_performance(cpu, low_hz);
        c->domain_id = dcvs_get_domain_id_for_cpu(cpu);
        c->domain_members = dcvs_get_domain_members_for_cpu(cpu);

        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA6, STATUS_SUCCESS, cpu, c->highest_performance,
                      c->nominal_performance, c->lowest_performance);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_perf_constraints(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef struct pep_ppm_query_perf_constraints {
        ULONG guaranteed_performance_limit;
        ULONG limit_reasons;
    } pep_ppm_query_perf_constraints_t;

    __try {
        pep_ppm_query_perf_constraints_t* q = (pep_ppm_query_perf_constraints_t*)data;
        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        ULONG reasons = 0;
        ULONG max_hz = dcvs_get_max_freq_hz(cpu);
        ULONG limit_hz = max_hz;

        ULONG hibernate_hz = registry_query_u_long(L"HibernatePerfFrequencyHz", 0);
        BOOLEAN hibernate_active =
            (InterlockedCompareExchange(&g_hibernate_in_progress, 0, 0) != 0);
        if (hibernate_hz && hibernate_active) {
            limit_hz = hibernate_hz;
            reasons = 1;
        }

        q->guaranteed_performance_limit = dcvs_get_cpu_normalized_performance(cpu, limit_hz);
        if (hibernate_hz && hibernate_active) {

            q->guaranteed_performance_limit = hibernate_hz;
        }
        q->limit_reasons = reasons;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_perf_check_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(pep_handle);
    dcvs_perf_check_complete();

    KeMemoryBarrier();
    InterlockedIncrement64(&g_perf_check_complete_cnt);
    pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA7, STATUS_SUCCESS, (ULONG)g_perf_check_complete_cnt, 0,
                  0, 0);
}

VOID pep_query_discrete_perf_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef struct pep_proc_perf_state {
        ULONG performance;
        ULONG freq;
        ULONG reserved[4];
    } pep_proc_perf_state_t;

    typedef struct pep_ppm_query_discrete_perf_states {
        ULONG cnt;
        pep_proc_perf_state_t* states;
    } pep_ppm_query_discrete_perf_states_t;

    __try {
        pep_ppm_query_discrete_perf_states_t* q = (pep_ppm_query_discrete_perf_states_t*)data;
        if (!q->cnt || !q->states)
            return;

        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        ULONG advertised = dcvs_get_perf_level_cnt(cpu);
        ULONG to_fill = q->cnt < advertised ? q->cnt : advertised;
        for (ULONG i = 0; i < to_fill; i++) {
            ULONG hz = dcvs_get_perf_freq_hz_by_perf_state_idx(cpu, i);
            ULONG perf = dcvs_get_cpu_normalized_performance(cpu, hz);

            q->states[i].freq = (hz + 500000u) / 1000000u;
            q->states[i].performance = perf;
            RtlZeroMemory(q->states[i].reserved, sizeof(q->states[i].reserved));
        }

        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA3, STATUS_SUCCESS, cpu, q->cnt,
                      dcvs_get_perf_level_cnt(cpu), 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_query_domain_info(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef struct pep_ppm_query_domain_info {
        ULONG domain_id;
        UCHAR coordination_type;
        UCHAR idle_processors_discounted;
        UCHAR affinitize_perf_set;
        UCHAR scheduler_directed_transitions_supported;
        ULONG worst_case_transition_latency;
        ULONG worst_case_transition_overhead;
    } pep_ppm_query_domain_info_t;

    __try {
        pep_ppm_query_domain_info_t* q = (pep_ppm_query_domain_info_t*)data;

        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        q->domain_id = dcvs_get_domain_id_for_cpu(cpu);
        q->coordination_type = (UCHAR)(dcvs_get_domain_coordination_type() & 0xFF);
        q->idle_processors_discounted = 1;
        q->affinitize_perf_set = 1;
        if (q->coordination_type == 2)
            q->scheduler_directed_transitions_supported = 1;
        q->worst_case_transition_latency = dcvs_get_worst_case_transition_latency(cpu);
        q->worst_case_transition_overhead = dcvs_get_worst_case_transition_overhead(cpu);

        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA4, STATUS_SUCCESS, cpu, q->domain_id,
                      q->coordination_type, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_perf_set_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    if (!data)
        return;

    typedef struct pep_ppm_perf_set_state {
        UCHAR minimum_performance_state;
        UCHAR maximum_performance_state;
        UCHAR desired_performance_state;
        UCHAR energy_performance_preference;
    } pep_ppm_perf_set_state_t;

    __try {
        pep_ppm_perf_set_state_t* s = (pep_ppm_perf_set_state_t*)data;
        ULONG cpu = get_cpu_num_from_pep_handle(pep_handle);

        UCHAR min_state = s->minimum_performance_state;
        UCHAR max_state = s->maximum_performance_state;
        UCHAR desired_state = s->desired_performance_state;

        if (InterlockedCompareExchange(&g_hibernate_in_progress, 0, 0) != 0) {
            ULONG hibernate_hz = registry_query_u_long(L"HibernatePerfFrequencyHz", 0);
            if (hibernate_hz != 0) {
                ULONG levels = dcvs_get_perf_level_cnt(cpu);
                if (!levels)
                    levels = 1;

                ULONG selected_state = levels - 1;
                for (ULONG i = 0; i < levels; i++) {
                    ULONG hz = dcvs_get_perf_freq_hz_by_perf_state_idx(cpu, i);
                    if (hz <= hibernate_hz) {
                        selected_state = i;
                        break;
                    }
                }

                if (selected_state > 0xFFu)
                    selected_state = 0xFFu;

                desired_state = (UCHAR)selected_state;
                min_state = desired_state;
                max_state = desired_state;
            }
        }

        NTSTATUS perf_status = dcvs_perf_set_state_checked(cpu, min_state, max_state, desired_state,
                                                           s->energy_performance_preference);

        pep_log_write(PEPLOG_EVT_PROC_NOTIFY, 0xA5, perf_status, cpu,
                      ((ULONG)min_state << 16) | (ULONG)max_state,
                      ((ULONG)desired_state << 16) | (ULONG)s->energy_performance_preference, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}
