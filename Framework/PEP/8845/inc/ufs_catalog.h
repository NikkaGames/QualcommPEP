#ifndef UFS_CATALOG_H
#define UFS_CATALOG_H
/* no PoFx admission, clock votes, controller permission or P-state completion is implied.
 * The native runtime must still bind a verified hardware backend. */

#include "state_txn.h"
#include "aeob_idx.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UFS_PSTATE_SETS 3u
#define UFS_STATES_PER_SET 2u
#define UFS_FSTATES 2u

typedef struct ufs_catalog {
    power_sequence_t f_states[UFS_FSTATES];
    power_pstate_program_t p_states[UFS_PSTATE_SETS][UFS_STATES_PER_SET];
    uint8_t compiled;
} ufs_catalog_t;

/* must be called on nonpaged, caller-owned memory, not the kernel stack. */

int ufs_catalog_compile(ufs_catalog_t* catalog, const void* bpmd, size_t bytes);
/* verify the pinned component submission describes exactly the same F/P programs as the
 * authoritative graph. Caller owns large NONPAGED scratch. */
int ufs_catalog_verify_pinned_desc(const ufs_catalog_t* authoritative, const void* desc,
                                   size_t bytes, runtime_desc_info_t* desc_scratch,
                                   ufs_catalog_t* catalog_scratch, program_idx_t* idx_scratch);

const power_pstate_program_t* ufs_catalog_get(const ufs_catalog_t* catalog, uint32_t comp,
                                              uint32_t set, uint32_t state);
const power_sequence_t* ufs_catalog_f_state(const ufs_catalog_t* catalog, uint32_t comp,
                                            uint32_t fstate);
#ifdef __cplusplus
}
#endif
#endif
