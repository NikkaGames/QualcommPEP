#ifndef UFS_NATIVE_PROVISION_H
#define UFS_NATIVE_PROVISION_H
/* connect required-client acquisition to the sealed UFS graph; the provider must construct the
 * native clients. */

#include "factory.h"
#include "ufs_footswitch_graph.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ufs_native_scope {
    /* factory MUST be first: the FS adapter's native IO Context is factory. Its callbacks
     * recover this enclosing, owner-retained scope. */

    fs_factory_t factory;
    ufs_fs_graph_t adapter;
    void* physical_ctx;
    int (*read_physical)(void*, const char* domain, uint64_t epoch, uint32_t* action);
    res_graph_t* graph;
    uint64_t epoch;
    uint8_t bound;
    uint8_t quarantined;
} ufs_native_scope_t;

/* permits this ONLY after seal and before provider freeze. The factory opens a verified
 * idle NPA_CLIENT_REQUIRED (/enable) client; no vote is issued during provisioning. */

fs_result_t ufs_native_provision(ufs_native_scope_t* scope, res_graph_t* graph,
                                 const fs_factory_provider_t* native_owner, const char* client_name,
                                 void* physical_ctx,
                                 int (*read_physical)(void*, const char*, uint64_t, uint32_t*));

/* bound graph callback storage must never be destroyed by a scope-local function without
 * full PoFx rundown and provider removal (not currently implemented). */

/* graph storage is pinned if native callbacks may still reach it. */
int ufs_native_retains_graph(const ufs_native_scope_t* scope);

fs_result_t ufs_native_discard_unbound(ufs_native_scope_t* scope);

/* external PoFx/graph admission must already be closed and callbacks drained. The GCC and
 * rail owners remain live throughout this operation. */
struct fs_gdsc_driver;
struct ufs_cxmx_owner;
/* roll back a provision that never published a native graph provider or issued votes.
 * Neither an active client nor ambiguous physical state is disposable. */
fs_result_t ufs_native_rollback_unpublished(ufs_native_scope_t* scope,
                                            struct fs_gdsc_driver* domain,
                                            struct ufs_cxmx_owner* rails);
fs_result_t ufs_native_retire_verified(ufs_native_scope_t* scope, struct fs_gdsc_driver* domain,
                                       struct ufs_cxmx_owner* rails);

#ifdef __cplusplus
}
#endif
#endif
