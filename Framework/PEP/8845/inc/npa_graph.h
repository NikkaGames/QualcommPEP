#ifndef NPA_GRAPH_PROVIDER_H
#define NPA_GRAPH_PROVIDER_H
/* no public Qualcomm NPA ABI is inferred here. provider must implement the precise
 * CLOCK/BUSARB/PMICVREGVOTE request protocol and confirm every vote. */

#include "npa_client.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct npa_req_group {
    const npa_slot_t* states[POWER_DSTATE_COUNT];
    uint32_t target;
} npa_req_group_t;

typedef struct npa_req_ops {
    void* ctx;
    /* A state change transfers this resource's request across all declared clients.
     * Snapshot and rollback cover the entire group under the graph lock. */
    int (*supports)(void*, const npa_req_group_t*, const power_action_t*);
    int (*read)(void*, const npa_req_group_t*, power_snapshot_t*);
    int (*issue)(void*, const npa_req_group_t*, const power_action_t*);
    int (*verify)(void*, const npa_req_group_t*, const power_action_t*);
    int (*restore)(void*, const npa_req_group_t*, const power_snapshot_t*);
    int (*verify_restored)(void*, const npa_req_group_t*, const power_snapshot_t*);
    int (*verify_final)(void*, const npa_req_group_t*, const power_action_t*);
} npa_req_ops_t;

typedef struct npa_graph_ctx {
    npa_registry_t* registry;
    power_action_kind_t kind;
    npa_req_ops_t requests;
} npa_graph_ctx_t;

/* returns a complete GraphBind provider for one family. Does not install callbacks or
 * acquire device ownership. */

int npa_graph_make_provider(npa_graph_ctx_t*, npa_registry_t*, power_action_kind_t,
                            const npa_req_ops_t*, graph_provider_t*);
#ifdef __cplusplus
}
#endif
#endif
