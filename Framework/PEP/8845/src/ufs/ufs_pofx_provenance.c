#include "ufs_catalog.h"
#include <string.h>

/* windows PREPARE may only pin the same BPMD generation from which the published physical
 * graph was compiled. A newly parsed BPMD with a matching ACPI path but different vote
 * values must not inherit the previous owner. */
int ufs_catalog_verify_pinned_desc(const ufs_catalog_t* authoritative, const void* desc,
                                   size_t bytes, runtime_desc_info_t* desc_scratch,
                                   ufs_catalog_t* candidate, program_idx_t* idx)
{
    uint32_t set, state, i;
    if (!authoritative || !authoritative->compiled || !desc || !desc_scratch || !candidate ||
        !idx || candidate == authoritative ||
        !program_idx_describe(desc, bytes, "\\_SB.UFS0", desc_scratch, idx) ||
        idx->comp_mask != 1u || idx->f_state_mask[0] != 3u || idx->p_state_set_mask[0] != 7u)
        return 0;
    for (set = 0u; set < UFS_PSTATE_SETS; ++set)
        if (idx->p_state_mask[0][set] != 3u)
            return 0;
    if (!ufs_catalog_compile(candidate, desc, bytes))
        return 0;
    for (state = 0u; state < UFS_FSTATES; ++state) {
        const power_sequence_t* a = &authoritative->f_states[state];
        const power_sequence_t* b = &candidate->f_states[state];
        if (a->action_cnt != b->action_cnt || a->target_d_state != b->target_d_state ||
            memcmp(a->dev_path, b->dev_path, sizeof(a->dev_path)))
            return 0;
        for (i = 0u; i < a->action_cnt; ++i)
            if (memcmp(&a->actions[i], &b->actions[i], sizeof(power_action_t)))
                return 0;
    }
    for (set = 0u; set < UFS_PSTATE_SETS; ++set)
        for (state = 0u; state < UFS_STATES_PER_SET; ++state) {
            const power_pstate_program_t* a = &authoritative->p_states[set][state];
            const power_pstate_program_t* b = &candidate->p_states[set][state];
            if (a->comp != b->comp || a->set != b->set || a->state != b->state ||
                a->resources.action_cnt != b->resources.action_cnt ||
                a->resources.target_d_state != b->resources.target_d_state ||
                memcmp(a->resources.dev_path, b->resources.dev_path, sizeof(a->resources.dev_path)))
                return 0;
            for (i = 0u; i < a->resources.action_cnt; ++i)
                if (memcmp(&a->resources.actions[i], &b->resources.actions[i],
                           sizeof(power_action_t)))
                    return 0;
        }
    return 1;
}
