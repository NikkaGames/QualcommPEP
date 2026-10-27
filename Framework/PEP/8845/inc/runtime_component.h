#ifndef RUNTIME_COMP_H
#define RUNTIME_COMP_H
#include "pofx_perf.h"
#include "hw_backend.h"

typedef struct runtime_comp {
    comp_state_owner_t owner;
    pofx_perf_catalog_t catalog;
    pofx_perf_endpoint_t endpoint;
    power_dev_t dev_envelope;
    power_backend_t adapter, inner;
    hw_backend_t provider;
    const void* desc;
    size_t bytes;
    uint64_t epoch;
    void* ctx;
    int (*enter)(void*, hw_backend_t*);
    void (*leave_cb)(void*);
    int (*live)(void*, uint64_t);
    comp_state_work_t* work;
    power_plan_t expected, batch, group;
    const power_plan_t* executing;
    uint8_t lane[POWER_MAX_ACTIONS];
    uint32_t comp, components;
    uint8_t locked, bound, retain, dev_in_d0, proof_invalidated;
} runtime_comp_t;

/* storage and pinned desc are caller-owned NONPAGED memory. The caller serializes every
 * entry, including queries and cache publication. */
int runtime_comp_bind(runtime_comp_t*, const void*, size_t, const char*, uint64_t, void*,
                      int (*)(void*, hw_backend_t*), void (*)(void*), int (*)(void*, uint64_t),
                      runtime_desc_info_t*, comp_state_work_t*);
power_result_t runtime_comp_f(runtime_comp_t*, uint32_t, uint32_t, comp_state_work_t*,
                              power_execution_t*);
power_result_t runtime_comp_p(runtime_comp_t*, uint32_t, const uint32_t*, const uint32_t*, uint32_t,
                              comp_state_work_t*, power_execution_t*);
int runtime_comp_query(runtime_comp_t*, uint32_t, uint32_t, comp_state_work_t*, uint32_t*);
int runtime_comp_dx(runtime_comp_t*, uint32_t, comp_state_work_t*);
int runtime_comp_close(runtime_comp_t*, comp_state_work_t*);
void runtime_comp_forget(runtime_comp_t*);
#endif
