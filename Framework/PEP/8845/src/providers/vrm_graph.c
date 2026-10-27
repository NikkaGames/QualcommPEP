#include "vrm_graph.h"
#include "vrm.h"
#include "vrm_acpi.h"
#include <string.h>

/* these are stable client identities scoped to the exact reviewed ACPI instance paths.
 * They are not firmware NPA client handles. */

static int map_action(const char* path, uint32_t dstate, const power_action_t* action,
                      uint32_t* rail, uint64_t* client, rail_state_t* wanted)
{
    vrm_program_t program;
    if (!rail || !client || !wanted || !vrm_prepare_known_dstate(path, action, dstate, &program) ||
        !program.res)
        return 0;
    memset(wanted, 0, sizeof(*wanted));
    if (!strcmp(path, "\\_SB.TSC1") && !strcmp(program.res->command_db_name, "L4B_E0")) {
        *rail = RAIL_LDO4_B;
        *client = 0x54534331u;
    } else if (!strcmp(path, "\\_SB.SPK1") && !strcmp(program.res->command_db_name, "L15B_E0")) {
        *rail = RAIL_LDO15_B;
        *client = 0x53504b31u;
    } else
        return 0;
    if (dstate == 0u) {
        wanted->enabled = 1u;
        wanted->microvolts = action->u.pmic_regulator.fields[1];
    }
    return 1;
}

static int inspect(void* opaque, const char* path, uint32_t st, const power_action_t* action,
                   rail_state_t* current, uint8_t* found, rail_state_t* wanted, uint32_t* rail,
                   uint64_t* client)
{
    vrm_graph_ctx_t* c = (vrm_graph_ctx_t*)opaque;
    return c && c->bank && map_action(path, st, action, rail, client, wanted) &&
           rail_inspect_client(c->bank, *rail, *client, current, found) == RAIL_OK;
}

static int supports(void* ctx, const char* path, uint32_t st, const power_action_t* action)
{
    rail_state_t cur, wanted;
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    return inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client);
}

static int snapshot(void* ctx, const char* path, uint32_t st, const power_action_t* action,
                    power_snapshot_t* snap)
{
    rail_state_t cur, wanted;
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    if (!snap || !inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client))
        return 0;
    memset(snap, 0, sizeof(*snap));
    snap->opaque[0] = found;
    snap->opaque[1] = cur.enabled;
    snap->opaque[2] = cur.microvolts;
    snap->opaque[3] = client;
    snap->opaque[4] = rail;
    return 1;
}

static int apply(void* ctx, const char* path, uint32_t st, const power_action_t* action)
{
    vrm_graph_ctx_t* c = (vrm_graph_ctx_t*)ctx;
    rail_state_t cur, wanted;
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    return inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client) &&
           /* a d3 for a client with no prior vote is not a completed release. */
           (st != 3u || found) && rail_vote(c->bank, rail, client, wanted) == RAIL_OK;
}

static int verify(void* ctx, const char* path, uint32_t st, const power_action_t* action)
{
    rail_state_t cur, wanted;
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    if (!inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client))
        return 0;
    return st == 3u ? !found : found && cur.enabled && cur.microvolts == wanted.microvolts;
}

static int restore(void* ctx, const char* path, uint32_t st, const power_action_t* action,
                   const power_snapshot_t* snap)
{
    vrm_graph_ctx_t* c = (vrm_graph_ctx_t*)ctx;
    rail_state_t cur, wanted, previous = {0};
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    if (!snap || (snap->opaque[0] != 0u && snap->opaque[0] != 1u) || snap->opaque[1] > 1u ||
        snap->opaque[2] > UINT32_MAX ||
        !inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client) ||
        snap->opaque[3] != client || snap->opaque[4] != rail)
        return 0;
    if (snap->opaque[0]) {
        previous.enabled = (uint8_t)snap->opaque[1];
        previous.microvolts = (uint32_t)snap->opaque[2];
        if (!previous.enabled)
            return 0;
    }
    if (!found && !snap->opaque[0])
        return 1;
    return rail_vote(c->bank, rail, client, previous) == RAIL_OK;
}

static int verify_restored(void* ctx, const char* path, uint32_t st, const power_action_t* action,
                           const power_snapshot_t* snap)
{
    rail_state_t cur, wanted;
    uint8_t found;
    uint32_t rail;
    uint64_t client;
    if (!snap || !inspect(ctx, path, st, action, &cur, &found, &wanted, &rail, &client) ||
        snap->opaque[3] != client || snap->opaque[4] != rail)
        return 0;
    return !snap->opaque[0]
               ? !found
               : found && cur.enabled == snap->opaque[1] && cur.microvolts == snap->opaque[2];
}

static int verify_state(void* ctx, const char* path, uint32_t st, const power_sequence_t* sequence)
{
    uint32_t i;
    int had = 0;
    if (!sequence || st != sequence->target_d_state || strcmp(path, sequence->dev_path))
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i) {
        if (sequence->actions[i].kind == POWER_PMIC_REGULATOR) {
            had = 1;
            /* only the exact currently supported actions are accepted, and verify checks the last
             * logical client vote, not intermediate hardware settings shared with boot firmware. */

            if (!verify(ctx, path, st, &sequence->actions[i]))
                return 0;
        }
    }
    return had;
}

int graph_bind_known_vrm(res_graph_t* graph, vrm_graph_ctx_t* ctx)
{
    graph_provider_t p = {0};
    if (!graph || !ctx || !ctx->bank)
        return 0;
    p.ctx = ctx;
    p.supports = supports;
    p.snapshot = snapshot;
    p.apply = apply;
    p.verify = verify;
    p.restore = restore;
    p.verify_restored = verify_restored;
    p.verify_final = verify_state;
    return graph_bind(graph, POWER_PMIC_REGULATOR, &p);
}
