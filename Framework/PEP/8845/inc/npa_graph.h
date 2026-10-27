#ifndef NPA_GRAPH_PROVIDER_H
#define NPA_GRAPH_PROVIDER_H
/* no public Qualcomm NPA ABI is inferred here. provider must implement the precise
 * CLOCK/BUSARB/PMICVREGVOTE request protocol and confirm every vote. */

#include "npa_client.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct npa_req_ops {
    void* ctx;
    /* every request must be a reversible d0/d3 CLIENT PAIR transfer. A vote on a separate d3
     * handle does not release the d0 client's vote. */

    int (*supports_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                         const power_action_t*);
    int (*read_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*, power_snapshot_t*);
    /* SC8480 npa_issue_scalar_request at RVA 0x231BA8 returns void. Establish owner, completion and
     * effective readback independently before reporting success. */

    int (*issue_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                      const power_action_t*);
    int (*verify_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                       const power_action_t*);
    int (*restore_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                        const power_snapshot_t*);
    int (*verify_restored_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                                const power_snapshot_t*);
    int (*verify_final_pair)(void*, const npa_key_t*, void*, const npa_key_t*, void*,
                             const power_action_t*);
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
