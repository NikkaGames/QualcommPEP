#include "npa_graph.h"
#include <string.h>

/* Callback rundown retains the registry and every client in a resource group. */
static int request_group(npa_graph_ctx_t* c, const char* path, uint32_t state,
                         const power_action_t* action, npa_req_group_t* group)
{
    const npa_slot_t* current = NULL;
    uint32_t i;
    if (!group)
        return 0;
    memset(group, 0, sizeof(*group));
    if (!c || !c->registry || !action || !path || state >= POWER_DSTATE_COUNT ||
        action->kind != c->kind || !npa_resolve_action(c->registry, path, state, action, &current))
        return 0;
    group->target = state;
    for (i = 0u; i < c->registry->cnt; ++i) {
        const npa_slot_t* slot = &c->registry->slots[i];
        uint32_t target = slot->key.d_state;
        if (!npa_same_res_identity(&slot->key, &current->key))
            continue;
        if (target >= POWER_DSTATE_COUNT || group->states[target] || !slot->active ||
            !slot->handle || !c->registry->provider.owns || !c->registry->provider.validate ||
            !c->registry->provider.owns(c->registry->provider.ctx, &slot->key) ||
            !c->registry->provider.validate(c->registry->provider.ctx, &slot->key, slot->handle))
            return 0;
        group->states[target] = slot;
    }
    return group->states[state] == current;
}

static int supported(void* opaque, const char* path, uint32_t state, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    return request_group(c, path, state, a, &group) &&
           c->requests.supports(c->requests.ctx, &group, a);
}

static int snapshot(void* opaque, const char* path, uint32_t state, const power_action_t* a,
                    power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    if (!old || !request_group(c, path, state, a, &group) ||
        !c->requests.supports(c->requests.ctx, &group, a))
        return 0;
    memset(old, 0, sizeof(*old));
    return c->requests.read(c->requests.ctx, &group, old);
}

static int apply(void* opaque, const char* path, uint32_t state, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    return request_group(c, path, state, a, &group) &&
           c->requests.supports(c->requests.ctx, &group, a) &&
           c->requests.issue(c->requests.ctx, &group, a);
}

static int verify(void* opaque, const char* path, uint32_t state, const power_action_t* a)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    return request_group(c, path, state, a, &group) &&
           c->requests.supports(c->requests.ctx, &group, a) &&
           c->requests.verify(c->requests.ctx, &group, a);
}

static int restore(void* opaque, const char* path, uint32_t state, const power_action_t* a,
                   const power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    return old && request_group(c, path, state, a, &group) &&
           c->requests.restore(c->requests.ctx, &group, old);
}

static int verify_restored(void* opaque, const char* path, uint32_t state, const power_action_t* a,
                           const power_snapshot_t* old)
{
    npa_graph_ctx_t* c = opaque;
    npa_req_group_t group;
    return old && request_group(c, path, state, a, &group) &&
           c->requests.verify_restored(c->requests.ctx, &group, old);
}

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

static int verify_final(void* opaque, const char* path, uint32_t state,
                        const power_sequence_t* sequence)
{
    npa_graph_ctx_t* c = opaque;
    uint32_t i, j;
    int found = 0;
    if (!c || !path || !sequence || state >= POWER_DSTATE_COUNT ||
        sequence->target_d_state != state || !sequence->action_cnt ||
        sequence->action_cnt > POWER_MAX_ACTIONS ||
        !bounded_path(sequence->dev_path, sizeof(sequence->dev_path)) ||
        !bounded_path(path, sizeof(sequence->dev_path)) || strcmp(sequence->dev_path, path))
        return 0;
    for (i = 0u; i < sequence->action_cnt; ++i) {
        const power_action_t* a = &sequence->actions[i];
        npa_req_group_t group;
        int superseded = 0;
        if (a->kind != c->kind)
            continue;
        found = 1;
        if (!request_group(c, path, state, a, &group) ||
            !c->requests.supports(c->requests.ctx, &group, a))
            return 0;
        for (j = i + 1u; j < sequence->action_cnt; ++j) {
            const power_action_t* later = &sequence->actions[j];
            npa_req_group_t other;
            if (later->kind != c->kind)
                continue;
            if (!request_group(c, path, state, later, &other) ||
                !c->requests.supports(c->requests.ctx, &other, later))
                return 0;
            if (other.states[state] == group.states[state] &&
                (c->kind != POWER_CLOCK || later->u.clock.vote == a->u.clock.vote)) {
                superseded = 1;
                break;
            }
        }
        if (!superseded && !c->requests.verify_final(c->requests.ctx, &group, a))
            return 0;
    }
    return found;
}

int npa_graph_make_provider(npa_graph_ctx_t* c, npa_registry_t* reg, power_action_kind_t kind,
                            const npa_req_ops_t* ops, graph_provider_t* provider)
{
    if (!c || !reg || !ops || !provider || reg->active || reg->quarantined ||
        (kind != POWER_CLOCK && kind != POWER_ICB && kind != POWER_PMIC_REGULATOR) ||
        !ops->supports || !ops->read || !ops->issue || !ops->verify || !ops->restore ||
        !ops->verify_restored || !ops->verify_final)
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
