#include "npa_graph.h"
#include <string.h>

/* windows callback rundown must also retain both state clients and this provider context
 * throughout every transaction. A d0 or d3 vote is NOT complete while a vote from the
 * opposite-state client remains applied. */

static int paired(npa_graph_ctx_t* c, const char* path, uint32_t st, const power_action_t* action,
                  const npa_slot_t** current, const npa_slot_t** opposite)
{
    uint32_t i;
    const npa_slot_t* slot = NULL;
    if (current)
        *current = NULL;
    if (opposite)
        *opposite = NULL;
    if (!c || !c->registry || !action || !path || !current || !opposite ||
        action->kind != c->kind || (st != 0u && st != 3u) ||
        !npa_resolve_action(c->registry, path, st, action, &slot))
        return 0;
    for (i = 0; i < c->registry->cnt; ++i) {
        const npa_slot_t* peer = &c->registry->slots[i];
        if (peer->key.d_state == (st == 0u ? 3u : 0u) &&
            npa_same_res_identity(&peer->key, &slot->key)) {
            if (!peer->active || !peer->handle || !c->registry->provider.owns ||
                !c->registry->provider.validate ||
                !c->registry->provider.owns(c->registry->provider.ctx, &peer->key) ||
                !c->registry->provider.validate(c->registry->provider.ctx, &peer->key,
                                                peer->handle))
                return 0;
            *current = slot;
            *opposite = peer;
            return 1;
        }
    }
    return 0;
}

static int supports_pair(npa_graph_ctx_t* c, const char* path, uint32_t st, const power_action_t* a,
                         const npa_slot_t** current, const npa_slot_t** opposite)
{
    return paired(c, path, st, a, current, opposite) &&
           c->requests.supports_pair(c->requests.ctx, &(*current)->key, (*current)->handle,
                                     &(*opposite)->key, (*opposite)->handle, a);
}

static int supported(void* opaque, const char* path, uint32_t st, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    return supports_pair(c, path, st, a, &s, &peer);
}

static int snapshot(void* opaque, const char* path, uint32_t st, const power_action_t* a,
                    power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    if (!old || !supports_pair(c, path, st, a, &s, &peer))
        return 0;
    memset(old, 0, sizeof(*old));
    return c->requests.read_pair(c->requests.ctx, &s->key, s->handle, &peer->key, peer->handle,
                                 old);
}

static int apply(void* opaque, const char* path, uint32_t st, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    return supports_pair(c, path, st, a, &s, &peer) &&
           c->requests.issue_pair(c->requests.ctx, &s->key, s->handle, &peer->key, peer->handle, a);
}

static int verify(void* opaque, const char* path, uint32_t st, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    return supports_pair(c, path, st, a, &s, &peer) &&
           c->requests.verify_pair(c->requests.ctx, &s->key, s->handle, &peer->key, peer->handle,
                                   a);
}

static int restore(void* opaque, const char* path, uint32_t st, const power_action_t* a,
                   const power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    return old && paired(c, path, st, a, &s, &peer) &&
           c->requests.restore_pair(c->requests.ctx, &s->key, s->handle, &peer->key, peer->handle,
                                    old);
}

static int verify_restored(void* opaque, const char* path, uint32_t st, const power_action_t* a,
                           const power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    const npa_slot_t *s = NULL, *peer = NULL;
    return old && paired(c, path, st, a, &s, &peer) &&
           c->requests.verify_restored_pair(c->requests.ctx, &s->key, s->handle, &peer->key,
                                            peer->handle, old);
}

/* a plan may have been damaged after sealing; bound the path and action inventory before
 * strcmp or any provider readback. */

static int bounded_path(const char* path, size_t limit)
{
    size_t i;
    if (!path || !limit)
        return 0;
    for (i = 0u; i < limit; ++i)
        if (!path[i])
            return i != 0u;
    return 0;
}

static int verify_final(void* opaque, const char* path, uint32_t st, const power_plan_t* plan)
{
    npa_graph_ctx_t* c = opaque;
    uint32_t i, j;
    int found = 0;
    if (!c || !path || !plan || (st != 0u && st != 3u) || plan->target_d_state != st ||
        !plan->action_cnt || plan->action_cnt > POWER_MAX_ACTIONS ||
        !bounded_path(plan->dev_path, sizeof(plan->dev_path)) ||
        !bounded_path(path, sizeof(plan->dev_path)) || strcmp(plan->dev_path, path))
        return 0;
    /* final verification is per action selector; one successful vote must not validate a stale
     * selector on the same res. */

    for (i = 0; i < plan->action_cnt; ++i) {
        const power_action_t* a = &plan->actions[i];
        const npa_slot_t *s = NULL, *peer = NULL;
        int superseded = 0;
        if (a->kind != c->kind)
            continue;
        found = 1;
        if (!supports_pair(c, path, st, a, &s, &peer))
            return 0;
        for (j = i + 1u; j < plan->action_cnt; ++j) {
            const power_action_t* later = &plan->actions[j];
            const npa_slot_t *other = NULL, *other_peer = NULL;
            if (later->kind != c->kind)
                continue;
            if (!supports_pair(c, path, st, later, &other, &other_peer))
                return 0;
            if (other == s && (c->kind != POWER_CLOCK || later->u.clock.vote == a->u.clock.vote)) {
                superseded = 1;
                break;
            }
        }
        if (!superseded && !c->requests.verify_final_pair(c->requests.ctx, &s->key, s->handle,
                                                          &peer->key, peer->handle, a))
            return 0;
    }
    return found;
}

int npa_graph_make_provider(npa_graph_ctx_t* c, npa_registry_t* reg, power_action_kind_t kind,
                            const npa_req_ops_t* ops, graph_provider_t* provider)
{
    if (!c || !reg || !ops || !provider || reg->active || reg->quarantined ||
        (kind != POWER_CLOCK && kind != POWER_ICB && kind != POWER_PMIC_REGULATOR) ||
        !ops->supports_pair || !ops->read_pair || !ops->issue_pair || !ops->verify_pair ||
        !ops->restore_pair || !ops->verify_restored_pair || !ops->verify_final_pair)
        return 0;
    memset(c, 0, sizeof(*c));
    memset(provider, 0, sizeof(*provider));
    c->registry = reg;
    c->kind = kind;
    c->requests = *ops;
    provider->ctx = c;
    provider->supports = supported;
    provider->snapshot = snapshot;
    provider->apply = apply;
    provider->verify = verify;
    provider->restore = restore;
    provider->verify_restored = verify_restored;
    provider->verify_final = verify_final;
    return 1;
}
