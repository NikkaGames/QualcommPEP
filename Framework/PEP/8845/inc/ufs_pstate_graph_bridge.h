#ifndef UFS_PSTATE_GRAPH_BRIDGE_H
#define UFS_PSTATE_GRAPH_BRIDGE_H
/* UFS PSTATE_SET uses the bound graph providers. Its owner lock must differ from the graph
 * nonrecursive lock. */

#include "res_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ufs_pstate_req {
    uint32_t set;
    uint32_t state;
} ufs_pstate_req_t;

typedef struct ufs_pstate_graph_bridge {
    res_graph_t* graph;
    void* owner_lock_ctx;
    int (*owner_acquire)(void*);
    void (*owner_release)(void*);
    uint64_t serial;
    power_sequence_t* combined;
    power_sequence_t* final;
    uint8_t action_states[POWER_MAX_ACTIONS];
    uint32_t expected_actions;
    uint8_t executing;
} ufs_pstate_graph_bridge_t;

/* no default provider or automatic authorization: binding checks current backend
 * publication, but every execute additionally checks live graph and exact provider
 * callbacks. native FOOTSWITCH must be bound before Seal. */

int ufs_pstate_graph_bridge_bind(ufs_pstate_graph_bridge_t* bridge, res_graph_t* graph,
                                 power_sequence_t* combined, power_sequence_t* final,
                                 void* owner_ctx, int (*owner_acquire)(void*),
                                 void (*owner_release)(void*));

/* execute the exact BPMD f0/f1 PSTATE_ADJUST trio as ONE owner-locked operation. The
 * component cache advances only on full verified success. */

power_result_t ufs_fstate_graph_execute(ufs_pstate_graph_bridge_t* bridge, uint32_t target_f_state,
                                        const power_dev_t* dev, power_dev_t* comp,
                                        power_dev_t* per_set, power_execution_t* trace,
                                        power_scratch_t* scratch);

/* applies ALL requested UFS P-state sets via atomic executor. The caller's verified d0/f0
 * and per-set state are not modified on failure. */

power_result_t ufs_pstate_graph_execute(ufs_pstate_graph_bridge_t* bridge,
                                        const ufs_pstate_req_t* requests, uint32_t cnt,
                                        const power_dev_t* dev, const power_dev_t* comp,
                                        power_dev_t* per_set, power_execution_t* trace,
                                        power_scratch_t* scratch);
#ifdef __cplusplus
}
#endif
#endif
