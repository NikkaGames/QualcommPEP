#ifndef COMP_STATE_OWNER_H
#define COMP_STATE_OWNER_H
#include "aeob_idx.h"
#ifdef __cplusplus
extern "C" {
#endif

/* the caller pins the desc and all buffers for the owner's lifetime. */

typedef struct comp_state_owner {
    program_idx_t idx;
    power_dev_t comp[32];
    power_dev_t sets[32][32];
    uint64_t owner_epoch;
    uint32_t comp_mask;
    uint8_t bound;
    uint8_t quarantined;
} comp_state_owner_t;

/* no giant transient stack allocations in PoFx. */
typedef struct comp_state_work {
    power_sequence_t f_state;
    power_sequence_t expanded;
    power_pstate_program_t p_states[32];
    const power_pstate_program_t* program_refs[32];
    uint32_t po_fx_selectors[32];
    power_scratch_t scratch;
} comp_state_work_t;

int comp_owner_bind(comp_state_owner_t* owner, const program_idx_t* idx, uint64_t epoch);

power_result_t
comp_owner_execute_f_state(comp_state_owner_t* owner, const void* pinned_desc, size_t desc_bytes,
                           uint32_t comp, uint32_t f_state, const power_dev_t* physical_d0,
                           const power_backend_t* verified_provider, uint64_t current_owner_epoch,
                           comp_state_work_t* nonpaged_work, power_execution_t* trace);

/* pofx P-state requests are distinct from an F-state's nested adjustments. */
power_result_t
comp_owner_execute_p_states(comp_state_owner_t* owner, const void* pinned_desc, size_t desc_bytes,
                            uint32_t comp, const uint32_t* sets, const uint32_t* states,
                            uint32_t cnt, const power_dev_t* physical_d0,
                            const power_backend_t* verified_provider, uint64_t current_owner_epoch,
                            comp_state_work_t* nonpaged_work, power_execution_t* trace);

/* pofx state indices are ordinal, not desc selectors. */
power_result_t comp_owner_execute_po_fx_p_states(
    comp_state_owner_t* owner, const void* pinned_desc, size_t desc_bytes, uint32_t comp,
    const uint32_t* sets, const uint32_t* ordinals, uint32_t cnt, const power_dev_t* physical_d0,
    const power_backend_t* verified_provider, uint64_t current_owner_epoch,
    comp_state_work_t* nonpaged_work, power_execution_t* trace);

#ifdef __cplusplus
}
#endif
#endif
