#include "dispatch/ppm_priv.h"
#include "cpu_device.h"
#include "dcvs.h"
#include "soc_transport.h"

static ULONG frequency_mhz(ULONG frequency)
{
    return (ULONG)(((ULONGLONG)frequency + 500000u) / 1000000u);
}

BOOLEAN ppm_notify(PVOID handle, ULONG notify, PVOID data)
{
    ULONG cpu = cpu_device_index(handle);
    ULONG count;
    if (notify == PEP_NOTIFY_PPM_ENTER_SYSTEM_STATE) {
        molokai_transport_suspend(TRUE);
        return TRUE;
    }
    if (notify == PEP_NOTIFY_PPM_RESUME_FROM_SYSTEM_STATE) {
        molokai_transport_suspend(FALSE);
        return TRUE;
    }
    if (notify == PEP_NOTIFY_PPM_PERF_CHECK_COMPLETE)
        return dcvs_ready();
    if (notify == PEP_NOTIFY_PPM_QUERY_VETO_REASONS) {
        if (!data)
            return FALSE;
        ((PPEP_PPM_QUERY_VETO_REASONS)data)->VetoReasonCount = 0u;
        return TRUE;
    }
    if (!data || cpu == MAXULONG || !dcvs_ready())
        return FALSE;
    count = dcvs_get_perf_level_cnt(cpu);
    if (!count || count > MAXUCHAR)
        return FALSE;
    switch (notify) {
        case PEP_NOTIFY_PPM_QUERY_CAPABILITIES: {
            PPEP_PPM_QUERY_CAPABILITIES caps = data;
            RtlZeroMemory(caps, sizeof(*caps));
            caps->FeedbackCounterCount = 1u;
            caps->PerformanceStatesSupported = TRUE;
            caps->DiscretePerformanceStateCount = (UCHAR)count;
            return TRUE;
        }
        case PEP_NOTIFY_PPM_QUERY_FEEDBACK_COUNTERS: {
            PPEP_PPM_QUERY_FEEDBACK_COUNTERS query = data;
            ULONG frequency = frequency_mhz(dcvs_get_nominal_freq_hz(cpu));
            if (query->Count != 1u || !frequency)
                return FALSE;
            RtlZeroMemory(&query->Counters[0], sizeof(query->Counters[0]));
            query->Counters[0].Type = PROCESSOR_FEEDBACK_TYPE_INSTANTANEOUS;
            query->Counters[0].Counter = PROCESSOR_FEEDBACK_COUNTER_FREQUENCY;
            query->Counters[0].NominalRate = frequency;
            return TRUE;
        }
        case PEP_NOTIFY_PPM_FEEDBACK_READ: {
            PPEP_PPM_FEEDBACK_READ query = data;
            ULONG frequency;
            if (query->CounterIndex != 0u)
                return FALSE;
            frequency = dcvs_get_current_freq_hz(cpu);
            if (!frequency)
                return FALSE;
            query->InstantaneousValue = frequency_mhz(frequency);
            return TRUE;
        }
        case PEP_NOTIFY_PPM_QUERY_PERF_CAPABILITIES: {
            PPEP_PPM_QUERY_PERF_CAPABILITIES caps = data;
            caps->HighestPerformance =
                dcvs_get_cpu_normalized_performance(cpu, dcvs_get_max_freq_hz(cpu));
            caps->NominalPerformance =
                dcvs_get_cpu_normalized_performance(cpu, dcvs_get_nominal_freq_hz(cpu));
            caps->LowestNonlinearPerformance =
                dcvs_get_cpu_normalized_performance(cpu, dcvs_get_min_freq_hz(cpu));
            caps->LowestPerformance = caps->LowestNonlinearPerformance;
            caps->DomainId = dcvs_get_domain_id_for_cpu(cpu);
            caps->DomainMembers = dcvs_get_domain_members_for_cpu(cpu);
            return TRUE;
        }
        case PEP_NOTIFY_PPM_PERF_CONSTRAINTS: {
            PPEP_PPM_PERF_CONSTRAINTS constraints = data;
            constraints->GuaranteedPerformanceLimit =
                dcvs_get_cpu_normalized_performance(cpu, dcvs_get_nominal_freq_hz(cpu));
            constraints->LimitReasons = 0u;
            return TRUE;
        }
        case PEP_NOTIFY_PPM_QUERY_DISCRETE_PERF_STATES: {
            PPEP_PPM_QUERY_DISCRETE_PERF_STATES query = data;
            if (!query->States || query->Count != count)
                return FALSE;
            for (ULONG i = 0u; i < count; ++i) {
                ULONG frequency = dcvs_get_perf_freq_hz_by_perf_state_idx(cpu, i);
                RtlZeroMemory(&query->States[i], sizeof(query->States[i]));
                query->States[i].Frequency = frequency_mhz(frequency);
                query->States[i].Performance = dcvs_get_cpu_normalized_performance(cpu, frequency);
                if (!query->States[i].Performance ||
                    (i && query->States[i].Performance >= query->States[i - 1u].Performance))
                    return FALSE;
            }
            return TRUE;
        }
        case PEP_NOTIFY_PPM_QUERY_DOMAIN_INFO: {
            PPEP_PPM_QUERY_DOMAIN_INFO query = data;
            RtlZeroMemory(query, sizeof(*query));
            query->DomainId = dcvs_get_domain_id_for_cpu(cpu);
            query->CoordinationType = PROCESSOR_DOMAIN_COORDINATION_SW_ANY;
            query->WorstCaseTransitionLatency = dcvs_get_worst_case_transition_latency(cpu);
            return TRUE;
        }
        case PEP_NOTIFY_PPM_PERF_SET_STATE: {
            PPEP_PPM_PERF_SET_STATE request = data;
            if (request->DesiredPerformanceState == PEP_SET_STATE_AUTONOMOUS_MODE)
                return FALSE;
            return NT_SUCCESS(dcvs_perf_set_state_checked(
                cpu, request->MinimumPerformanceState, request->MaximumPerformanceState,
                request->DesiredPerformanceState, request->EnergyPerformancePreference));
        }
        case PEP_NOTIFY_PPM_QUERY_PERF_EPP_SUPPORT:
            ((PPEP_PPM_QUERY_PERF_EPP_SUPPORT)data)->EnergyPerformancePreferenceSupported = FALSE;
            return TRUE;
        case PEP_NOTIFY_PPM_QUERY_AUTONOMOUS_SUPPORT:
            ((PPEP_PPM_QUERY_PERF_AUTONOMOUS_SUPPORT)data)->AutonomousCapability =
                PROCESSOR_AUTONOMOUS_CAPABILITY_UNSUPPORTED;
            return TRUE;
        default:
            return FALSE;
    }
}
