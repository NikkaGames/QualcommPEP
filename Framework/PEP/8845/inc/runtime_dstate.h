#ifndef RUNTIME_DSTATE_SCOPE_H
#define RUNTIME_DSTATE_SCOPE_H
/* a device-independent runtime desc owner. No Windows PnP acceptance or hardware rights
 * are inferred from compilation: all actions must pass an independently acquired,
 * epoch-verified native provider lease. */

#include "aeob_idx.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct runtime_dstate_scope {
    program_idx_t idx;
    power_plan_t d0;
    power_plan_t d1;
    power_plan_t d2;
    power_plan_t d3;
    power_dev_t physical;
    uint32_t last_nonempty_target; /* only updated after verified nonempty votes. */
    uint8_t empty_f0;
    uint8_t valid;
    uint8_t quarantined;
} runtime_dstate_scope_t;

/* nonempty F/P programs, nested PSTATE_SET and unknown extra states MUST use separate
 * audited component/performance execution; they are never silently discarded. */

int runtime_dstate_compile(runtime_dstate_scope_t* scope, const void* blob, size_t bytes,
                           const char* path, runtime_desc_info_t* scratch);
power_result_t runtime_dstate_execute(runtime_dstate_scope_t* scope, uint32_t target,
                                      const power_backend_t* owner, power_execution_t* trace,
                                      power_scratch_t* scratch);
#ifdef __cplusplus
}
#endif
#endif
