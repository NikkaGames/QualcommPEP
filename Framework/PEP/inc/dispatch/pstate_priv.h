#pragma once
#include "driver.h"
#include <pepfx.h>
#include "component_perf.h"
#include "node_types.h"
#include "work_types.h"

VOID pep_handle_p_state_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                            _In_opt_ pep_work_t* w);
VOID pep_handle_p_state_v2_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                               _In_opt_ pep_work_t* w);
BOOLEAN pep_native_perf_ready(pep_devnode_t* n, ULONG comp);
BOOLEAN pep_native_perf_req(pep_devnode_t* n, pep_req_comp_perf_state_t* r);
ULONG_PTR pep_query_comp_perf_caps(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_set(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_set_name(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_states(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_comp_perf_states(_Inout_opt_ PVOID data);
ULONG_PTR pep_req_comp_perf_state(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_current_comp_perf_state(_Inout_opt_ PVOID data);
