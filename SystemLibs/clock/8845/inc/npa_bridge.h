#ifndef NPA_SCALAR_CLOCK_BRIDGE_H
#define NPA_SCALAR_CLOCK_BRIDGE_H
/* the caller supplies owned physical resources; this adapter neither maps MMIO nor grants
 * ownership. */

#include "scalar.h"
#include "npa_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SCALAR_CLOCK_MAX_NODES 64u
#define SCALAR_CLOCK_MAX_HANDLES NPA_MAX_CLIENTS

typedef struct scalar_clock_client {
    scalar_res_t* res;
    scalar_handle_t scalar;
    /* binding is exact to the immutable device/state/client identity. Two D-state clients may
     * vote on the same physical clock, but their handles cannot be used interchangeably by the
     * graph/provider. */

    npa_key_t key;
    uint8_t active;
    /* unique opaque identity for this activation, never a dereferenceable pointer. Reusing a
     * client slot cannot resurrect a stale handle. */

    uintptr_t cookie;
} scalar_clock_client_t;

typedef struct scalar_clock_bridge {
    scalar_res_t* resources[SCALAR_CLOCK_MAX_NODES];
    scalar_clock_client_t clients[SCALAR_CLOCK_MAX_HANDLES];
    uint32_t res_cnt;
    uint32_t client_cnt;
    uintptr_t next_cookie;
} scalar_clock_bridge_t;

/* NPA enable values 1/2 differ from physical enabled/disabled values 1/0. */

typedef struct clock_intent {
    uint32_t legacy_npa_action;
    uint32_t effective_enabled; /* scalar hardware target, 1/0. */
} clock_intent_t;

int decode_simple_clock_intent(const power_action_t*, clock_intent_t*);

/* resources must already be attached to the scalar core using physically verified
 * callbacks. No implicit attach, MMIO mapping, or default votes. */

int scalar_clock_add_res(scalar_clock_bridge_t*, scalar_res_t*);
void scalar_clock_lifecycle(scalar_clock_bridge_t*, npa_callbacks_t*);
void scalar_clock_requests(scalar_clock_bridge_t*, npa_req_ops_t*);
#ifdef __cplusplus
}
#endif
#endif
