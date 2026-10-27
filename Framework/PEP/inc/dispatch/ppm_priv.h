#pragma once
#include "driver.h"
#include <pepfx.h>

VOID pep_query_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_feedback_counters(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_feedback_read(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_perf_caps(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_constraints(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_check_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_discrete_perf_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_domain_info(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_perf_set_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
