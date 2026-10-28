#include "clock_target.h"
#include "ufs_footswitch_graph.h"
#include <string.h>

static const char k_path[] = "\\_SB.UFS0";

static int supports_ufs_action(ufs_fs_graph_t* a, const char* path, uint32_t state,
                               const power_action_t* act)
{
    if (!a || !a->bound || !a->res || !a->owner_epoch || !path || strcmp(path, k_path) != 0 ||
        !act || act->kind != POWER_FOOTSWITCH ||
        strcmp(act->u.footswitch.name, clock_ufs_domain) != 0 || (state != 0u && state != 3u) ||
        act->u.footswitch.action != (state == 0u ? 1u : 2u) || !a->res->bound ||
        a->res->quarantined || !a->res->enable_client ||
        strcmp(a->res->name, clock_ufs_domain) != 0 || a->res->owner_epoch != a->owner_epoch)
        return 0;
    return fs_verify(a->res) == FS_OK;
}

static int supports(void* v, const char* p, uint32_t s, const power_action_t* a)
{
    return supports_ufs_action((ufs_fs_graph_t*)v, p, s, a);
}

static int snapshot(void* v, const char* p, uint32_t s, const power_action_t* a,
                    power_snapshot_t* out)
{
    ufs_fs_graph_t* g = v;
    if (!out || !supports(v, p, s, a))
        return 0;
    memset(out, 0, sizeof(*out));
    out->opaque[0] = g->res->normal_action;
    out->opaque[1] = g->owner_epoch;
    if (g->read_physical) {
        uint32_t raw = 0u;
        if (!g->read_physical(g->res->io.ctx, clock_ufs_domain, g->owner_epoch, &raw) ||
            (raw != 1u && raw != 2u))
            return 0;
        out->opaque[2] = raw;
    }
    return 1;
}

static int apply(void* v, const char* p, uint32_t s, const power_action_t* a)
{
    ufs_fs_graph_t* g = v;
    return supports(v, p, s, a) && fs_issue(g->res, a->u.footswitch.action) == FS_OK;
}

static int verify(void* v, const char* p, uint32_t s, const power_action_t* a)
{
    ufs_fs_graph_t* g = v;
    return supports(v, p, s, a) && g->res->normal_action == a->u.footswitch.action &&
           g->verify_physical &&
           g->verify_physical(g->res->io.ctx, clock_ufs_domain, a->u.footswitch.action,
                              g->owner_epoch);
}

static int restore(void* v, const char* p, uint32_t s, const power_action_t* a,
                   const power_snapshot_t* snap)
{
    ufs_fs_graph_t* g = v;
    uint32_t old;
    if (!snap || !supports(v, p, s, a) || snap->opaque[1] != g->owner_epoch || snap->opaque[0] > 2u)
        return 0;
    old = (uint32_t)snap->opaque[0];
    if (!old)
        return g->complete_normal && fs_complete(g->res, 1u, g->complete_normal) == FS_OK;
    return fs_issue(g->res, old) == FS_OK;
}

static int verify_restored(void* v, const char* p, uint32_t s, const power_action_t* a,
                           const power_snapshot_t* snap)
{
    ufs_fs_graph_t* g = v;
    if (!snap || !supports(v, p, s, a) || snap->opaque[1] != g->owner_epoch ||
        g->res->normal_action != snap->opaque[0] || !g->verify_physical)
        return 0;
    /* an NPA complete-request leaves the physical GDSC at its previous aggregate state. A
     * native client that is idle cannot imply a physically idle/off domain. */

    if (g->read_physical) {
        uint32_t actual = 0u;
        return (snap->opaque[2] == 1u || snap->opaque[2] == 2u) &&
               g->read_physical(g->res->io.ctx, clock_ufs_domain, g->owner_epoch, &actual) &&
               actual == snap->opaque[2];
    }
    return g->verify_physical(g->res->io.ctx, clock_ufs_domain, (uint32_t)snap->opaque[0],
                              g->owner_epoch);
}

static int verify_final(void* v, const char* p, uint32_t s, const power_sequence_t* sequence)
{
    ufs_fs_graph_t* g = v;
    uint32_t i, found = 0;
    if (!sequence || !p || strcmp(p, k_path) != 0 || sequence->action_cnt > POWER_MAX_ACTIONS)
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i)
        if (sequence->actions[i].kind == POWER_FOOTSWITCH) {
            if (!supports_ufs_action(g, p, s, &sequence->actions[i]))
                return 0;
            ++found;
        }
    return found == 1u && g->res->normal_action == (s == 0u ? 1u : 2u) && g->verify_physical &&
           g->verify_physical(g->res->io.ctx, clock_ufs_domain, s == 0u ? 1u : 2u, g->owner_epoch);
}

int ufs_fs_graph_bind_verified(res_graph_t* graph, ufs_fs_graph_t* adapter, fs_res_t* res,
                               int (*complete_normal)(void*, const char*, uint64_t),
                               int (*verify_physical)(void*, const char*, uint32_t, uint64_t),
                               int (*read_physical)(void*, const char*, uint64_t, uint32_t*))
{
    graph_provider_t p;
    ufs_fs_graph_t candidate;
    if (!graph || !adapter || !res || adapter->bound || !res->bound || res->quarantined ||
        !res->owner_epoch || !res->enable_client || strcmp(res->name, clock_ufs_domain) != 0 ||
        fs_verify(res) != FS_OK || !complete_normal || !verify_physical || graph->retired ||
        graph->quiescing || graph->providers_frozen)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.res = res;
    candidate.complete_normal = complete_normal;
    candidate.verify_physical = verify_physical;
    candidate.read_physical = read_physical;
    candidate.owner_epoch = res->owner_epoch;
    candidate.bound = 1u;
    memset(&p, 0, sizeof(p));
    p.ctx = adapter;
    p.supports = supports;
    p.snapshot = snapshot;
    p.apply = apply;
    p.verify = verify;
    p.restore = restore;
    p.verify_restored = verify_restored;
    p.verify_final = verify_final;
    /* D-state binding precedes AML graph construction; acquire the NPA client afterward. Post-seal
     * binding requires closed publication and the graph lock. */

    *adapter = candidate;
    if (!(graph->sealed ? graph_bind_prepublish_footswitch(graph, &p)
                        : graph_bind(graph, POWER_FOOTSWITCH, &p))) {
        memset(adapter, 0, sizeof(*adapter));
        return 0;
    }
    return 1;
}

int ufs_fs_graph_bind(res_graph_t* graph, ufs_fs_graph_t* adapter, fs_res_t* res,
                      int (*complete_normal)(void*, const char*, uint64_t),
                      int (*verify_physical)(void*, const char*, uint32_t, uint64_t))
{
    return ufs_fs_graph_bind_verified(graph, adapter, res, complete_normal, verify_physical, NULL);
}
