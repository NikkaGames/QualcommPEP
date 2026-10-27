#ifndef POWER_DISCOVERY_H
#define POWER_DISCOVERY_H
/* discover D-state resources from reviewed, read-only ACPI descriptor methods. */

#include "state_txn.h"
#ifdef __cplusplus
extern "C" {
#endif
#define DISCOVERY_MAX_METHODS 32u

typedef enum method_eval_result {
    METHOD_EVALUATED = 0,
    METHOD_NOT_PRESENT = 1,
    METHOD_EVALUATION_FAILED = 2
} method_eval_result_t;

typedef method_eval_result_t (*method_evaluate_t)(void* ctx, const char method[5], const void** buf,
                                                  size_t* bytes);
typedef void (*method_release_t)(void* ctx, const void* buf);

/* stnd can advertise methods from SSDTs: those must be reviewed before extending the
 * allowlist. */

int method_is_read_only_res(const char method[5]);

/* require one descriptor for this path/state and reject duplicate or malformed results.
 * release every returned buffer; discovery does not execute resource actions. */

power_result_t power_discover_dev_d_state(const char* const* method_names, uint32_t method_cnt,
                                          method_evaluate_t evaluate, method_release_t release,
                                          void* ctx, const char* dev_path, uint32_t target_d_state,
                                          power_sequence_t* sequence, char selected_method[5]);
#ifdef __cplusplus
}
#endif
#endif
