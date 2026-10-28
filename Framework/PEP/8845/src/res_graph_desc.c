/* the runtime ACPI -> provider graph bridge. This code only builds sequences; no DEVICE is
 * prepared and no GPIO, RPMh, ACPI power or PoFx vote is sent. */

#include "res_graph.h"
#include "power_desc.h"
#include "aeob_validate.h"
#include "clock_target.h"
#include <string.h>

typedef struct fstate_kind_audit {
    uint32_t seen;
    int unexpected;
} fstate_kind_audit_t;

static int inspect_fstate_action(const aeob_action_t* action, void* ctx)
{
    fstate_kind_audit_t* audit = (fstate_kind_audit_t*)ctx;
    ++audit->seen;
    /* opaque payloads stay non-executable until a matching provider is verified. */

    if (strcmp(action->kind, "PSTATE_ADJUST"))
        audit->unexpected = 1;
    return 1;
}

/* these remain READ-ONLY sequences: no provider owns UFS and PoFx admission stays denied. */

typedef struct ufs_pstate_audit {
    uint32_t set_id, state_id, calls;
    int unexpected;
} ufs_pstate_audit_t;

/* everything else must additionally pass the existing full component action grammar / res
 * catalog validation below. This validates an already evaluated aeo_b buf; it never
 * executes a GDSC vote. */

static uint16_t audit_u16(const uint8_t* b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static int audit_ufs_gdsc(const aeob_action_t* a, uint32_t state)
{
    const uint8_t* b;
    uint32_t len, off = 0u, pad, i;
    uint64_t selector = 0u;
    size_t target_bytes = strlen(clock_ufs_domain) + 1u;
    if (!a || !a->arguments || state > 1u)
        return 0;
    b = a->arguments;
    len = a->argument_bytes;
    if (len < 4u || audit_u16(b) != 1u)
        return 0;
    pad = audit_u16(b + 2u);
    if (pad != target_bytes || len - 4u < pad ||
        memcmp(b + 4u, clock_ufs_domain, target_bytes) != 0)
        return 0;
    off = 4u + (pad < 4u ? 4u : pad);
    if (off > len || len - off < 4u || audit_u16(b + off) != 0u)
        return 0;
    pad = audit_u16(b + off + 2u);
    off += 4u;
    if ((pad != 2u && pad != 4u && pad != 8u) || len - off < (pad < 4u ? 4u : pad) ||
        len - off != (pad < 4u ? 4u : pad))
        return 0;
    for (i = 0; i < pad; ++i)
        selector |= (uint64_t)b[off + i] << (8u * i);

    return selector == (state == 0u ? 1u : 2u);
}

static int audit_ufs_pstate_action(const aeob_action_t* a, void* ctx)
{
    ufs_pstate_audit_t* audit = (ufs_pstate_audit_t*)ctx;
    const char* kind = a->kind;
    ++audit->calls;
    if (audit->set_id == 0u) {
        if (strcmp(kind, "FOOTSWITCH") || !audit_ufs_gdsc(a, audit->state_id))
            audit->unexpected = 1;
    } else if (!((audit->set_id == 1u &&
                  (!strcmp(kind, "CLOCK") || !strcmp(kind, "PMICVREGVOTE"))) ||
                 (audit->set_id == 2u && !strcmp(kind, "BUSARB"))))
        audit->unexpected = 1;
    return 1;
}

static int resolve_pstate(const graph_desc_t* descriptors, uint32_t cnt, const char* path,
                          uint32_t set, uint32_t state, power_pstate_program_t* program)
{
    uint32_t comp, i, found = 0u;
    if (set >= 32u || state >= 32u)
        return 0;
    for (comp = 0u; comp < 32u; ++comp)
        for (i = 0u; i < cnt; ++i) {
            uint32_t actions = 0u;
            aeob_status_t status =
                aeo_b_visit_comp_p_state(descriptors[i].buf, descriptors[i].bytes, path, comp, set,
                                         state, NULL, NULL, &actions);
            if (status == AEO_B_NOT_FOUND)
                continue;
            if (status != AEO_B_OK || ++found != 1u ||
                power_compile_aeo_bp_state(descriptors[i].buf, descriptors[i].bytes, path, comp,
                                           set, state, program) != POWER_OK)
                return 0;
        }
    return found == 1u;
}

static int expand_dstate(const graph_desc_t* descriptors, uint32_t cnt, power_sequence_t* sequence,
                         power_sequence_t* expanded, power_pstate_program_t* program,
                         uint16_t clients[POWER_MAX_ACTIONS])
{
    uint32_t i, j, delay = 0u;
    memset(expanded, 0, sizeof(*expanded));
    memcpy(expanded->dev_path, sequence->dev_path, sizeof(expanded->dev_path));
    expanded->target_d_state = sequence->target_d_state;
    for (i = 0u; i < sequence->action_cnt; ++i) {
        const power_action_t* a = &sequence->actions[i];
        const power_action_t* actions = a;
        uint32_t actions_cnt = 1u;
        uint16_t client = 0u;
        if (a->kind == POWER_PSTATE_ADJUST) {
            if (!resolve_pstate(descriptors, cnt, sequence->dev_path, a->u.pstate_adjust.fields[0],
                                a->u.pstate_adjust.fields[1], program))
                return 0;
            actions = program->resources.actions;
            actions_cnt = program->resources.action_cnt;
            client = (uint16_t)(1u + program->comp * 32u + program->set);
        }
        if (actions_cnt > POWER_MAX_ACTIONS - expanded->action_cnt)
            return 0;
        for (j = 0u; j < actions_cnt; ++j) {
            if (actions[j].kind == POWER_PSTATE_ADJUST)
                return 0;
            if (actions[j].kind == POWER_DELAY) {
                uint32_t ms = actions[j].u.delay.ms;
                if (ms > 5000u || delay > 10000u - ms)
                    return 0;
                delay += ms;
            }
            clients[expanded->action_cnt] = client;
            expanded->actions[expanded->action_cnt++] = actions[j];
        }
    }
    *sequence = *expanded;
    return 1;
}

int graph_build_from_descriptors(res_graph_t* graph, const graph_desc_t* descriptors, uint32_t cnt,
                                 power_sequence_t* scratch, graph_build_workspace_t* workspace)
{
    uint32_t dev_cnt = 0, i, j, state;
    if (!graph || !descriptors || !scratch || !workspace || cnt == 0u ||
        cnt > DISCOVERY_MAX_METHODS || graph->sealed || graph->retired || graph->dev_cnt ||
        !graph->acquire || !graph->release)
        return 0;
    memset(workspace, 0, sizeof(*workspace));
    for (i = 0; i < cnt; ++i) {

        memset(workspace->candidates, 0, sizeof(workspace->candidates));
        uint32_t n = 0, k;
        if (descriptors[i].preload || !resource_method_valid(descriptors[i].method) ||
            !descriptors[i].buf || !descriptors[i].bytes)
            goto fail;
        for (j = 0; j < i; ++j)
            if (!strcmp(descriptors[j].method, descriptors[i].method))
                goto fail;
        if (aeo_b_list_dev_paths(descriptors[i].buf, descriptors[i].bytes,
                                 (char*)workspace->candidates, GRAPH_MAX_DEVICES, &n) != AEO_B_OK)
            goto fail;
        for (k = 0; k < n; ++k) {
            uint32_t prior;
            int duplicate = 0;
            for (prior = 0; prior < dev_cnt; ++prior)
                if (!strcmp(workspace->paths[prior], workspace->candidates[k])) {
                    duplicate = 1;
                    break;
                }
            if (duplicate)
                continue;
            if (dev_cnt >= GRAPH_MAX_DEVICES)
                goto fail;
            memcpy(workspace->paths[dev_cnt++], workspace->candidates[k], 256u);
        }
    }
    if (!dev_cnt)
        goto fail;
    for (i = 0; i < dev_cnt; ++i) {
        uint32_t mask = 0u;
        /* Component-only descriptions remain available through the runtime DDI. */
        for (j = 0u; j < cnt; ++j)
            for (state = 0u; state < POWER_DSTATE_COUNT; ++state) {
                uint32_t actions;
                aeob_status_t status =
                    aeo_b_visit_dev_d_state(descriptors[j].buf, descriptors[j].bytes,
                                            workspace->paths[i], state, NULL, NULL, &actions);
                if (status == AEO_B_OK)
                    mask |= 1u << state;
                else if (status != AEO_B_NOT_FOUND)
                    goto fail;
            }
        if ((mask & 9u) != 9u)
            continue;
        for (state = 0; state < POWER_DSTATE_COUNT; ++state) {
            int hits = 0;
            for (j = 0; j < cnt; ++j) {
                uint32_t actions = 0;
                aeob_status_t status =
                    aeo_b_visit_dev_d_state(descriptors[j].buf, descriptors[j].bytes,
                                            workspace->paths[i], state, NULL, NULL, &actions);
                if (status == AEO_B_NOT_FOUND)
                    continue;
                if (status != AEO_B_OK || ++hits != 1)
                    goto fail;
                if (power_compile_aeo_b(descriptors[j].buf, descriptors[j].bytes,
                                        workspace->paths[i], state, scratch) != POWER_OK ||
                    !graph_add_sequence(graph, scratch))
                    goto fail;
            }
            if ((state == 0u || state == 3u) && hits != 1)
                goto fail;
        }
    }
    /* a firmware-added COMPONENT on TSC1, SPK1, or any later SSDT res must not be silently
     * accepted as a D-state-only PEP device. */

    for (i = 0; i < graph->dev_cnt; ++i) {
        graph_dev_t* dev = &graph->devices[i];
        int is_ic = !strcmp(dev->path, "\\_SB.IC17") || !strcmp(dev->path, "\\_SB.IC18") ||
                    !strcmp(dev->path, "\\_SB.IC21");
        int is_ufs = !strcmp(dev->path, "\\_SB.UFS0");
        uint32_t required = is_ufs ? 2u : is_ic ? 1u : 0u;
        uint32_t seen_components = 0u, seen_fstates[32] = {0}, seen_psets[32] = {0};
        uint32_t crashdump_state = UINT32_MAX;
        uint32_t f;
        for (j = 0; j < cnt; ++j) {
            uint32_t comp_mask = 0, comp_f_states[32] = {0}, comp_psets[32] = {0},
                     crashdump = UINT32_MAX, k;
            aeob_status_t st =
                aeo_b_inventory_dev_components(descriptors[j].buf, descriptors[j].bytes, dev->path,
                                               &comp_mask, comp_f_states, comp_psets, &crashdump);
            if (st == AEO_B_NOT_FOUND)
                continue;
            if (st != AEO_B_OK || (seen_components & comp_mask))
                goto fail;
            seen_components |= comp_mask;
            if (crashdump != UINT32_MAX) {
                if (crashdump_state != UINT32_MAX)
                    goto fail;
                crashdump_state = crashdump;
            }
            for (k = 0; k < 32u; ++k) {
                seen_fstates[k] |= comp_f_states[k];
                seen_psets[k] |= comp_psets[k];
            }
        }
        dev->requires_comp_runtime =
            (uint8_t)(seen_components != 0u || crashdump_state != UINT32_MAX);
        if (required) {
            if (seen_components != 1u || seen_fstates[0] != (is_ufs ? 3u : 1u) ||
                seen_psets[0] != (is_ufs ? 7u : 0u) ||
                crashdump_state != (is_ufs ? 0u : UINT32_MAX))
                goto fail;

            if (is_ufs) {
                uint32_t set_id, pstate;
                for (set_id = 0u; set_id < 3u; ++set_id)
                    for (pstate = 0u; pstate < 3u; ++pstate) {
                        uint32_t found = 0u, actions = 0u;
                        ufs_pstate_audit_t audit = {0};
                        audit.set_id = set_id;
                        audit.state_id = pstate;
                        for (j = 0u; j < cnt; ++j) {
                            uint32_t n = 0u;
                            aeob_status_t st = aeo_b_visit_comp_p_state(
                                descriptors[j].buf, descriptors[j].bytes, dev->path, 0u, set_id,
                                pstate, audit_ufs_pstate_action, &audit, &n);
                            if (st == AEO_B_NOT_FOUND)
                                continue;
                            if (st != AEO_B_OK || ++found != 1u)
                                goto fail;
                            /* a kind-only audit previously admitted any same-named action with
                             * malformed arguments or an unknown physical res. */

                            {
                                aeob_validation_t validation;
                                if (aeo_b_validate_molokai_comp_p_state(
                                        descriptors[j].buf, descriptors[j].bytes, dev->path, 0u,
                                        set_id, pstate, &validation) != AEO_B_OK ||
                                    validation.failure != AEO_B_RES_NONE)
                                    goto fail;
                            }
                            actions = n;
                        }
                        if (pstate == 2u) {
                            if (found)
                                goto fail;
                        } else if (found != 1u || !actions || audit.unexpected ||
                                   audit.calls != actions)
                            goto fail;
                    }
            }
            for (f = 0; f < required; ++f) {
                uint32_t found = 0, actions = 0;
                fstate_kind_audit_t kind = {0};
                for (j = 0; j < cnt; ++j) {
                    uint32_t n = 0;
                    aeob_status_t st = aeo_b_visit_comp_f_state(
                        descriptors[j].buf, descriptors[j].bytes, dev->path, 0u, f,
                        is_ufs ? inspect_fstate_action : NULL, is_ufs ? (void*)&kind : NULL, &n);
                    if (st == AEO_B_NOT_FOUND)
                        continue;
                    if (st != AEO_B_OK || ++found != 1u)
                        goto fail;
                    if (is_ufs) {
                        aeob_validation_t validation;
                        if (aeo_b_validate_molokai_comp_f_state(descriptors[j].buf,
                                                                descriptors[j].bytes, dev->path, 0u,
                                                                f, &validation) != AEO_B_OK ||
                            validation.failure != AEO_B_RES_NONE)
                            goto fail;
                    }
                    actions = n;
                }
                if (found != 1u || actions != (is_ufs ? 3u : 0u) ||
                    (is_ufs && (kind.seen != actions || kind.unexpected)))
                    goto fail;
            }
            /* ic COMPONENT0 has exactly f0 with zero actions and no Pstate, f1 or crashdump. PoFx
             * f0 can therefore be checked against the actual d0 provider readbacks rather than
             * inventing a vote. */

            if (is_ic)
                dev->verified_ic_empty_f0 = 1u;
        }
    }
    /* retain the six UFS PSTATE_SET programs and two F-state programs. CPU DCVS tables cannot
     * replace them; execution requires a verified UFS provider. */

    for (i = 0u; i < graph->dev_cnt; ++i) {
        if (strcmp(graph->devices[i].path, "\\_SB.UFS0") != 0)
            continue;
        {
            uint32_t found = 0u;
            for (j = 0u; j < cnt; ++j) {
                if (strcmp(descriptors[j].method, "BPMD") != 0)
                    continue;
                if (++found != 1u || !ufs_catalog_compile(&graph->ufs_catalog, descriptors[j].buf,
                                                          descriptors[j].bytes))
                    goto fail;
            }
            if (found != 1u || !graph->ufs_catalog.compiled)
                goto fail;
        }
    }
    for (i = 0u; i < graph->dev_cnt; ++i)
        for (state = 0u; state < POWER_DSTATE_COUNT; ++state)
            if (graph_has_d_state(&graph->devices[i], state) &&
                !expand_dstate(descriptors, cnt, &graph->devices[i].d_states[state], scratch,
                               &workspace->program, graph->devices[i].state_clients[state]))
                goto fail;

    /* inventory is valid only after all references resolve. */

    graph->comp_inventory_verified = 1u;
    if (graph_seal(graph))
        return 1;
    graph->comp_inventory_verified = 0u;
    goto fail;
fail:

    memset(&graph->ufs_catalog, 0, sizeof(graph->ufs_catalog));
    graph->retired = 1u;
    return 0;
}
