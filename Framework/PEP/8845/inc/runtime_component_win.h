#pragma once
#include "driver.h"
#include "runtime_component.h"

typedef struct runtime_comp_win {
    KMUTEX mutex;
    runtime_comp_t model;
    res_graph_t* graph;
    uint64_t serial;
    UCHAR* desc;
    ULONG desc_bytes;
    comp_state_work_t* workspace;
    BOOLEAN provider_held;
    volatile LONG proof_lost;
} runtime_comp_win_t;

/* caller pins this object under device rundown; all public operations are serialized with
 * the same PASSIVE mutex before reading committed state. */
BOOLEAN runtime_comp_win_prepare(runtime_comp_win_t*, const void*, ULONG, const char*, res_graph_t*,
                                 uint64_t);
power_result_t runtime_comp_win_f(runtime_comp_win_t*, ULONG, ULONG);
power_result_t runtime_comp_win_p(runtime_comp_win_t*, ULONG, const uint32_t*, const uint32_t*,
                                  ULONG);
BOOLEAN runtime_comp_win_query(runtime_comp_win_t*, ULONG, ULONG, uint32_t*);
BOOLEAN runtime_comp_win_dx(runtime_comp_win_t*, ULONG);
BOOLEAN runtime_comp_win_close(runtime_comp_win_t*);
BOOLEAN runtime_comp_win_reopen(runtime_comp_win_t*, const void*, ULONG);
BOOLEAN runtime_comp_win_forget(runtime_comp_win_t*);
