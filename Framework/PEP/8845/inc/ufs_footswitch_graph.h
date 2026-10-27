#ifndef UFS_FOOTSWITCH_GRAPH_H
#define UFS_FOOTSWITCH_GRAPH_H
/* qualcomm FOOTSWITCH (kind 8) -> verified NPA-native client. This DOES NOT create a
 * client or authorize UFS DPM. */

#include "res_graph.h"
#include "fs_npa_client.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ufs_fs_graph {
    fs_res_t* res; /* independently bound owner. */
    int (*complete_normal)(void*, const char*, uint64_t);
    /* separate physical GDSCR/native-provider readback (not token cache). */
    int (*verify_physical)(void*, const char*, uint32_t, uint64_t);
    /* independent physical baseline of the domain before each transaction. required for
     * production; NPA client scalar 0 does NOT mean GDSC off. */

    int (*read_physical)(void*, const char*, uint64_t, uint32_t*);
    uint64_t owner_epoch;
    uint8_t bound;
} ufs_fs_graph_t;

/* must be invoked before GraphSeal, only with a live, independently bound
 * /fs/gcc_ufs_phy_gdsc/enable client. GraphBind will not replace a foreign provider. */

int ufs_fs_graph_bind(res_graph_t* graph, ufs_fs_graph_t* adapter, fs_res_t* res,
                      int (*complete_normal)(void*, const char*, uint64_t),
                      int (*verify_physical)(void*, const char*, uint32_t, uint64_t));
/* strict physical-readback variant used by the native factory publisher. */

int ufs_fs_graph_bind_verified(res_graph_t* graph, ufs_fs_graph_t* adapter, fs_res_t* res,
                               int (*complete_normal)(void*, const char*, uint64_t),
                               int (*verify_physical)(void*, const char*, uint32_t, uint64_t),
                               int (*read_physical)(void*, const char*, uint64_t, uint32_t*));
#ifdef __cplusplus
}
#endif
#endif
