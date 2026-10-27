#ifndef RUNTIME_PROGRAM_IDX_H
#define RUNTIME_PROGRAM_IDX_H
/* pin the descriptor generation and acquire provider leases before executing a compiled program. */

#include "state_txn.h"
#include "runtime_res.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum program_class {
    PROGRAM_D_STATE = 1,
    PROGRAM_F_STATE = 2,
    PROGRAM_P_STATE = 3
} program_class_t;

typedef struct program_idx {
    char path[256];
    uint64_t desc_fingerprint;
    size_t desc_bytes;
    uint32_t comp_mask;
    uint32_t f_state_mask[32];
    uint32_t p_state_set_mask[32];
    uint32_t p_state_mask[32][32];
    uint8_t has_d0;
    uint8_t has_d3;
    uint8_t d_state_mask;
    uint8_t valid;
} program_idx_t;

/* an unsupported device is never turned into a supported scope simply because it is
 * present in a structurally valid multi-device desc. */

int program_idx_describe(const void* desc, size_t bytes, const char* exact_path,
                         runtime_desc_info_t* scratch, program_idx_t* out);
/* pofx ordinals are dense; desc selectors may be sparse. */
int program_p_state_from_ordinal(const program_idx_t* idx, uint32_t comp, uint32_t set,
                                 uint32_t ordinal, uint32_t* state);
int program_p_state_to_ordinal(const program_idx_t* idx, uint32_t comp, uint32_t set,
                               uint32_t state, uint32_t* ordinal);
uint32_t program_p_state_cnt(const program_idx_t* idx, uint32_t comp, uint32_t set);
/* exactly one output kind must be non-NULL. */

power_result_t program_idx_compile(const program_idx_t* idx, const void* desc, size_t bytes,
                                   program_class_t class, uint32_t comp, uint32_t set,
                                   uint32_t state, power_plan_t* plan,
                                   power_pstate_program_t* p_state_program);
#ifdef __cplusplus
}
#endif
#endif
