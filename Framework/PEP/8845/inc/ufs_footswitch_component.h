#ifndef UFS_FOOTSWITCH_COMP_H
#define UFS_FOOTSWITCH_COMP_H
/* ufs component-0 PSTATE_SET-0 aeo_b -> native NPA vote bridge. The reviewed aeo_b is only
 * data, NOT ownership evidence. */

#include "registry.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ufs_fs_comp {
    fs_registry_t* registry;
    uint64_t ticket;
    uint32_t last_verified_state;
    uint32_t owner_state_set;
    uint32_t owner_state_idx;
    uint8_t bound;
    uint8_t quarantined;
} ufs_fs_comp_t;

/* checks BOTH descriptors before calling the provider even once. */

fs_result_t ufs_fs_open(ufs_fs_comp_t* scope, const void* reviewed_aeo_b, size_t bytes,
                        fs_registry_t* registry, uint32_t owner_state_set, uint32_t owner_state_idx,
                        const char* owner_client_name, const fs_factory_provider_t* provider);

/* one exact native NPA scalar request, followed by per-client readback. Caller must not
 * mistake an accepted vote for physical GDSC power status. */

fs_result_t ufs_fs_set_p_state(ufs_fs_comp_t* scope, uint32_t state);
fs_result_t ufs_fs_verify(ufs_fs_comp_t* scope);
/* close succeeds only if the native client has actually become idle; DISABLE scalar 2 does
 * NOT release the native NPA request. */

/* when PSTATE1 was physically verified, a capable native provider can complete (terminate)
 * scalar 2 before closing the last client. scalar 1 is never automatically disabled on
 * teardown. */

fs_result_t ufs_fs_close(ufs_fs_comp_t* scope);
#ifdef __cplusplus
}
#endif
#endif
