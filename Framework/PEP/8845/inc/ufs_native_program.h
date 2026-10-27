#ifndef UFS_NATIVE_PROGRAM_H
#define UFS_NATIVE_PROGRAM_H
#include "ufs_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ufs_native_selection {
    uint32_t set;
    uint32_t state;
} ufs_native_selection_t;

/* a power sequence has no trusted set-id metadata after F-state expansion. Recover only an
 * UNAMBIGUOUS sequence of exact, desc-owned P-state programs. */
int ufs_native_select_program(const ufs_catalog_t* catalog, const power_sequence_t* program,
                              ufs_native_selection_t out[UFS_PSTATE_SETS], uint32_t* cnt);
#ifdef __cplusplus
}
#endif
#endif
