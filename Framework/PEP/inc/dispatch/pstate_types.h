#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef struct sdbus_pstate {
    ULONG performance;
    ULONG freq;
    ULONG nominal_power;
    ULONG max_utilization_threshold;
    ULONG min_utilization_threshold;
} sdbus_pstate_t;

typedef struct sdbus_pstate_reg {
    ULONGLONG sampling_interval;
    ULONG perf_state_cnt;
    sdbus_pstate_t* perf_states;
} sdbus_pstate_reg_t;

typedef struct pep_pstate_req {
    ULONG comp;
    ULONG p_state_idx;
} pep_pstate_req_t;

typedef struct pep_pstate_res_hdr {
    ULONG version;
    ULONG comp_idx;
    ULONG p_state_req_type;
    ULONG reserved0;
    PVOID user_data;
} pep_pstate_res_hdr_t;

typedef struct pep_pstate_set_req {
    ULONG p_state_set_idx;
    ULONG p_state_idx;
} pep_pstate_set_req_t;

typedef struct pep_pstate_res_node_v2 {
    pep_pstate_res_hdr_t hdr;
    pep_pstate_set_req_t p_state_data[1];
} pep_pstate_res_node_v2_t;
