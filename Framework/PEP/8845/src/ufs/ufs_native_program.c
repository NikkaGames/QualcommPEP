#include "ufs_native_program.h"
#include <string.h>

static int sequence_path(const char* p)
{
    const char target[] = "\\_SB.UFS0";
    return p && memcmp(p, target, sizeof(target)) == 0;
}

static int matches(const power_sequence_t* whole, uint32_t offset, const power_pstate_program_t* p)
{
    uint32_t i;
    if (!whole || !p || !p->resources.action_cnt ||
        p->resources.action_cnt > whole->action_cnt - offset)
        return 0;
    for (i = 0u; i < p->resources.action_cnt; ++i)
        if (memcmp(&whole->actions[offset + i], &p->resources.actions[i], sizeof(power_action_t)))
            return 0;
    return 1;
}

typedef struct search {
    const ufs_catalog_t* catalog;
    const power_sequence_t* program;
    ufs_native_selection_t work[UFS_PSTATE_SETS];
    ufs_native_selection_t found[UFS_PSTATE_SETS];
    uint32_t found_cnt, solutions;
} search_t;

static void descend(search_t* s, uint32_t offset, uint32_t used, uint32_t cnt)
{
    uint32_t set, state;
    if (s->solutions > 1u || cnt > UFS_PSTATE_SETS)
        return;
    if (offset == s->program->action_cnt) {
        uint32_t i;
        if (!cnt)
            return;
        if (s->program->target_d_state == 1u && cnt != UFS_PSTATE_SETS)
            return;
        ++s->solutions;
        s->found_cnt = cnt;
        for (i = 0u; i < cnt; ++i)
            s->found[i] = s->work[i];
        return;
    }
    for (set = 0u; set < UFS_PSTATE_SETS; ++set) {
        if (used & (1u << set))
            continue;
        for (state = 0u; state < UFS_STATES_PER_SET; ++state) {
            const power_pstate_program_t* p = ufs_catalog_get(s->catalog, 0u, set, state);
            if (s->program->target_d_state == 1u && state != 1u)
                continue;
            if (!matches(s->program, offset, p))
                continue;
            s->work[cnt].set = set;
            s->work[cnt].state = state;
            descend(s, offset + p->resources.action_cnt, used | (1u << set), cnt + 1u);
        }
    }
}

int ufs_native_select_program(const ufs_catalog_t* catalog, const power_sequence_t* program,
                              ufs_native_selection_t out[UFS_PSTATE_SETS], uint32_t* cnt)
{
    search_t search = {0};
    uint32_t i;
    if (cnt)
        *cnt = 0u;
    if (!catalog || !catalog->compiled || !program || !out || !cnt ||
        !sequence_path(program->dev_path) ||
        (program->target_d_state != 0u && program->target_d_state != 1u) || !program->action_cnt ||
        program->action_cnt > POWER_MAX_ACTIONS)
        return 0;
    search.catalog = catalog;
    search.program = program;
    descend(&search, 0u, 0u, 0u);
    if (search.solutions != 1u)
        return 0;
    for (i = 0u; i < search.found_cnt; ++i)
        out[i] = search.found[i];
    *cnt = search.found_cnt;
    return 1;
}
