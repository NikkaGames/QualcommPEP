#include "ufs_catalog.h"
#include <string.h>

static int valid_path(const char* p)
{
    static const char target[] = "\\_SB.UFS0";
    return p && memcmp(p, target, sizeof(target)) == 0;
}

int ufs_catalog_compile(ufs_catalog_t* catalog, const void* bpmd, size_t bytes)
{
    uint32_t state, set, f, i, mask;
    if (!catalog)
        return 0;
    memset(catalog, 0, sizeof(*catalog));
    if (!bpmd || !bytes)
        return 0;
    for (set = 0u; set < UFS_PSTATE_SETS; ++set) {
        for (state = 0u; state < UFS_STATES_PER_SET; ++state) {
            power_pstate_program_t* p = &catalog->p_states[set][state];
            if (power_compile_aeo_bp_state(bpmd, bytes, "\\_SB.UFS0", 0u, set, state, p) !=
                    POWER_OK ||
                !valid_path(p->resources.dev_path) || !p->resources.action_cnt || p->comp != 0u ||
                p->set != set || p->state != state)
                goto bad;
            for (i = 0u; i < p->resources.action_cnt; ++i) {
                power_action_kind_t k = p->resources.actions[i].kind;
                /* an action-kind match alone cannot authorize a provider; require the physical
                 * operation class from BPMD. */

                if (set == 0u   ? (k != POWER_FOOTSWITCH)
                    : set == 1u ? (k != POWER_CLOCK && k != POWER_PMIC_REGULATOR)
                                : (k != POWER_ICB))
                    goto bad;
            }
        }
    }
    for (f = 0u; f < UFS_FSTATES; ++f) {
        power_plan_t* p = &catalog->f_states[f];
        if (power_compile_aeo_bf_state(bpmd, bytes, "\\_SB.UFS0", 0u, f, p) != POWER_OK ||
            !valid_path(p->dev_path) || p->target_d_state != f || p->action_cnt != 3u)
            goto bad;
        mask = 0;
        for (i = 0u; i < p->action_cnt; ++i) {
            const power_action_t* a = &p->actions[i];
            uint32_t s = a->u.pstate_adjust.fields[0], target = a->u.pstate_adjust.fields[1];
            if (a->kind != POWER_PSTATE_ADJUST || s >= UFS_PSTATE_SETS || target != f ||
                (mask & (1u << s)))
                goto bad;
            mask |= 1u << s;
        }
        if (mask != 7u)
            goto bad;
    }
    catalog->compiled = 1u;
    return 1;
bad:
    memset(catalog, 0, sizeof(*catalog));
    return 0;
}

const power_pstate_program_t* ufs_catalog_get(const ufs_catalog_t* catalog, uint32_t comp,
                                              uint32_t set, uint32_t state)
{
    const power_pstate_program_t* p;
    if (!catalog || !catalog->compiled || comp != 0u || set >= UFS_PSTATE_SETS ||
        state >= UFS_STATES_PER_SET)
        return NULL;
    p = &catalog->p_states[set][state];
    if (p->comp != comp || p->set != set || p->state != state ||
        !valid_path(p->resources.dev_path) || !p->resources.action_cnt ||
        p->resources.target_d_state != state)
        return NULL;
    return p;
}

const power_plan_t* ufs_catalog_f_state(const ufs_catalog_t* catalog, uint32_t comp,
                                        uint32_t fstate)
{
    const power_plan_t* p;
    if (!catalog || !catalog->compiled || comp != 0u || fstate >= UFS_FSTATES)
        return NULL;
    p = &catalog->f_states[fstate];
    if (!valid_path(p->dev_path) || p->target_d_state != fstate || p->action_cnt != 3u)
        return NULL;
    return p;
}
