#ifndef PEER_COMP_H
#define PEER_COMP_H
/* admit only the validated single-component profile with no D/P programs and complete provider
 * snapshot/apply/readback/rollback. */

#include "state_txn.h"
#include "runtime_res.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct peer_comp {
    char path[256];
    power_plan_t f0;
    power_plan_t f1;
    power_dev_t state;
    uint64_t compiled_plan_fingerprint;
    uint8_t valid;
    uint8_t quarantined;
} peer_comp_t;

/* the desc info must be validated against the very same bytes, not obtained from a
 * different generation. */

int peer_comp_compile(peer_comp_t* scope, const void* aeo_b, size_t bytes, const char* exact_path,
                      runtime_desc_info_t* scratch);
power_result_t peer_comp_transition(peer_comp_t* scope, uint32_t f_state,
                                    const power_backend_t* verified_backend,
                                    power_execution_t* result, power_scratch_t* scratch);
#ifdef __cplusplus
}
#endif
#endif
