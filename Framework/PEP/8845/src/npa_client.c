#include "npa_client.h"
#include "molokai_catalog.h"
#include <string.h>

static int copy_key_name(char* out, size_t capacity, const char* from)
{
    size_t n = 0;
    if (!out || !from || !capacity)
        return 0;
    while (n < capacity && from[n])
        ++n;
    if (!n || n == capacity)
        return 0;
    memcpy(out, from, n + 1u);
    return 1;
}

/* provider callbacks may consume registry keys long after inventory. Reject malformed keys
 * BEFORE strcmp/_stricmp or passing them to an external provider. */

static int terminated(const char* name, size_t bytes)
{
    return name && bytes && memchr(name, 0, bytes) != NULL;
}

static int key_strings_terminated(const npa_key_t* key)
{
    return key && terminated(key->dev_path, sizeof(key->dev_path)) &&
           terminated(key->res, sizeof(key->res)) && terminated(key->slave, sizeof(key->slave));
}

/* compare NPA resource names without case. */

static int same_res(const char* a, const char* b)
{
    unsigned char x, y;
    if (!a || !b)
        return 0;
    do {
        x = (unsigned char)*a++;
        y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z')
            y = (unsigned char)(y + ('a' - 'A'));
        if (x != y)
            return 0;
    } while (x);
    return 1;
}

int npa_same_res_identity(const npa_key_t* a, const npa_key_t* b)
{
    return key_strings_terminated(a) && key_strings_terminated(b) && a->kind == b->kind &&
           a->client == b->client && a->icb_master_id == b->icb_master_id &&
           a->icb_slave_id == b->icb_slave_id && !strcmp(a->dev_path, b->dev_path) &&
           same_res(a->res, b->res) && same_res(a->slave, b->slave);
}

static int same_key(const npa_key_t* a, const npa_key_t* b)
{
    return a->d_state == b->d_state && npa_same_res_identity(a, b);
}

static int is_npa(power_action_kind_t kind)
{
    return kind == POWER_CLOCK || kind == POWER_ICB || kind == POWER_PMIC_REGULATOR;
}

/* bus client references contain two numeric endpoints; driver ID and client type need separate
 * binding. */

static int resolve_icb_endpoint(const char* symbol, int is_master, uint32_t* id)
{
    const icb_entry_t* entries = is_master ? icb_masters : icb_slaves;
    unsigned int cnt = is_master ? icb_master_cnt : icb_slave_cnt;
    unsigned int i, matched = 0;
    if (!symbol || !symbol[0] || !id)
        return 0;
    for (i = 0; i < cnt; ++i) {
        if (strcmp(entries[i].symbol, symbol) == 0) {
            if (++matched != 1u)
                return 0;
            *id = entries[i].id;
        }
    }
    return matched == 1u;
}

int npa_build_bus_client_reference(const npa_key_t* key, uint32_t out[2])
{
    uint32_t master = UINT32_MAX, slave = UINT32_MAX;
    if (!out)
        return 0;
    out[0] = 0u;
    out[1] = 0u;
    if (!key_strings_terminated(key) || key->kind != POWER_ICB ||
        !resolve_icb_endpoint(key->res, 1, &master) ||
        !resolve_icb_endpoint(key->slave, 0, &slave) || master != key->icb_master_id ||
        slave != key->icb_slave_id || !lookup_icb_route(master, slave))
        return 0;
    out[0] = master;
    out[1] = slave;
    return 1;
}

static int make_key(npa_key_t* key, const char* path, uint32_t state, const power_action_t* action)
{
    if (!key || !path || !action)
        return 0;
    memset(key, 0, sizeof(*key));
    if (!copy_key_name(key->dev_path, sizeof(key->dev_path), path))
        return 0;
    key->kind = action->kind;
    key->d_state = state;
    key->icb_master_id = UINT32_MAX;
    key->icb_slave_id = UINT32_MAX;
    if (action->kind == POWER_CLOCK) {
        if (action->u.clock.vote >= 32u)
            return 0;
        key->clock_mode_bits = 1u << action->u.clock.vote;
        return copy_key_name(key->res, sizeof(key->res), action->u.clock.name);
    }
    if (action->kind == POWER_PMIC_REGULATOR)
        return copy_key_name(key->res, sizeof(key->res), action->u.pmic_regulator.name);
    if (action->kind == POWER_ICB) {
        key->client = action->u.icb.client;
        if (!copy_key_name(key->res, sizeof(key->res), action->u.icb.master) ||
            !copy_key_name(key->slave, sizeof(key->slave), action->u.icb.slave) ||
            !resolve_icb_endpoint(key->res, 1, &key->icb_master_id) ||
            !resolve_icb_endpoint(key->slave, 0, &key->icb_slave_id) ||
            !lookup_icb_route(key->icb_master_id, key->icb_slave_id))
            return 0;
        return 1;
    }
    return 0;
}

/* public admission/dispatch predicates must not trust the recorded Count alone. This check
 * has no provider callbacks and can be used while the graph lock is held. */

static int active_slots_consistent(const npa_registry_t* reg)
{
    uint32_t i;
    if (!reg || !reg->inventoried || !reg->active || reg->quarantined || !reg->owner_graph ||
        reg->cnt == 0u || reg->cnt > NPA_MAX_CLIENTS)
        return 0;
    for (i = 0u; i < NPA_MAX_CLIENTS; ++i) {
        const npa_slot_t* slot = &reg->slots[i];
        if (i < reg->cnt) {
            if (!slot->active || !slot->handle || !key_strings_terminated(&slot->key))
                return 0;
        } else if (slot->active || slot->handle) {
            return 0;
        }
    }
    return 1;
}

/* this is separate from active_slots_consistent because a successfully deactivated
 * registry must be allowed to reactivate with completely empty slots. */

static int all_slots_empty(const npa_registry_t* reg)
{
    uint32_t i;
    if (!reg)
        return 0;
    for (i = 0u; i < NPA_MAX_CLIENTS; ++i)
        if (reg->slots[i].active || reg->slots[i].handle)
            return 0;
    return 1;
}

int npa_inventory(npa_registry_t* reg, const res_graph_t* graph)
{
    uint32_t i, s, j, k;
    if (!reg || !graph || !graph->sealed || graph->retired || graph->quiescing || !graph->dev_cnt ||
        graph->dev_cnt > GRAPH_MAX_DEVICES || reg->inventoried || reg->active || reg->quarantined ||
        reg->cnt)
        return 0;

    if (!all_slots_empty(reg))
        goto fail;

    for (i = 0; i < graph->dev_cnt; ++i) {
        const graph_dev_t* d = &graph->devices[i];
        if (!d->has_d0 || !d->has_d3)
            goto fail;
        for (s = 0; s < 2u; ++s) {
            const power_sequence_t* p = s ? &d->d3 : &d->d0;
            if (!terminated(d->path, sizeof(d->path)) ||
                !terminated(p->dev_path, sizeof(p->dev_path)) ||
                p->target_d_state != (s ? 3u : 0u) || strcmp(p->dev_path, d->path) ||
                !p->action_cnt || p->action_cnt > POWER_MAX_ACTIONS)
                goto fail;
            for (j = 0; j < p->action_cnt; ++j) {
                npa_key_t key;
                const power_action_t* a = &p->actions[j];
                if (!is_npa(a->kind)) {
                    /* non-NPA operations have no corresponding NPA client handle. */

                    if (a->kind < POWER_TLMM_GPIO || a->kind > POWER_FOOTSWITCH)
                        goto fail;
                    ++reg->non_npa_operations;
                    continue;
                }
                ++reg->res_operations;
                if (!make_key(&key, d->path, p->target_d_state, a))
                    goto fail;
                for (k = 0; k < reg->cnt; ++k)
                    if (same_key(&reg->slots[k].key, &key))
                        break;
                if (k != reg->cnt) {
                    /* one bundle per device/state/name may need multiple actual Qualcomm
                     * enable/freq NPA subclients. */

                    reg->slots[k].key.clock_mode_bits |= key.clock_mode_bits;
                    continue;
                }
                if (reg->cnt == NPA_MAX_CLIENTS)
                    goto fail;
                reg->slots[reg->cnt++].key = key;
            }
        }
    }
    if (!reg->cnt)
        goto fail;
    /* each graph NPA identity needs an opposite-state peer for transactional release.
     * Otherwise require an independently verified release operation. */

    for (i = 0; i < reg->cnt; ++i) {
        const npa_key_t* a = &reg->slots[i].key;
        uint32_t matches = 0;
        for (k = 0; k < reg->cnt; ++k) {
            const npa_key_t* b = &reg->slots[k].key;
            if (a->d_state != b->d_state && npa_same_res_identity(a, b))
                ++matches;
        }
        if (matches != 1u)
            goto fail;
    }
    reg->owner_graph = graph;
    reg->inventoried = 1u;
    return 1;
fail:
    /* a partially filled registry is unusable until caller reinitializes it before any
     * provider has ever been attached. */

    reg->quarantined = 1u;
    return 0;
}

static int close_all(npa_registry_t* reg)
{
    uint32_t i;
    int ok = 1;
    /* a half-torn-down registry cannot be treated as empty just because Count changed. */

    if (!reg || reg->cnt > NPA_MAX_CLIENTS) {
        if (reg)
            reg->quarantined = 1u;
        return 0;
    }
    for (i = reg->cnt; i > 0u; --i) {
        npa_slot_t* slot = &reg->slots[i - 1u];
        /* a handle with a clear Active bit (or Active without a handle) is an uncertain provider
         * lifetime, NOT proof of destruction. Do not call a provider with an invalid handle or
         * silently skip it. */

        if (!!slot->active != !!slot->handle) {
            ok = 0;
            continue;
        }
        if (!slot->active)
            continue;
        /* never pass an unterminated name into the provider's comparison, logging or destruction
         * logic. */

        if (!key_strings_terminated(&slot->key)) {
            ok = 0;
            continue;
        }
        /* destroying an active NPA client does not guarantee the previous hardware request was
         * retracted. A request owner must independently read back this client's RELEASED state
         * first. */

        if (!reg->provider.is_released ||
            !reg->provider.is_released(reg->provider.ctx, &slot->key, slot->handle) ||
            !reg->provider.destroy ||
            !reg->provider.destroy(reg->provider.ctx, &slot->key, slot->handle)) {
            ok = 0;
            continue;
        }
        slot->handle = NULL;
        slot->active = 0;
    }

    for (i = 0u; i < NPA_MAX_CLIENTS; ++i)
        if (reg->slots[i].active || reg->slots[i].handle)
            ok = 0;
    if (!ok)
        reg->quarantined = 1u;
    return ok;
}

int npa_activate(npa_registry_t* reg, const npa_callbacks_t* provider)
{
    uint32_t i;
    const res_graph_t* graph;
    int ok = 0;
    if (!reg || !provider || !provider->owns || !provider->create || !provider->validate ||
        !provider->is_released || !provider->destroy || !reg->inventoried || !reg->cnt ||
        reg->cnt > NPA_MAX_CLIENTS || reg->active || reg->quarantined || !reg->owner_graph)
        return 0;
    graph = reg->owner_graph;
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return 0;
    /* activation must serialize with graph retirement/drain, DPM transitions and NPA
     * deactivation. Otherwise client creation can succeed after shutdown has already begun and
     * publish stale handles. */

    if (!graph->sealed || graph->retired || graph->quiescing || !reg->inventoried || reg->active ||
        reg->quarantined || reg->owner_graph != graph || graph->dev_cnt == 0u ||
        graph->dev_cnt > GRAPH_MAX_DEVICES || reg->cnt == 0u || reg->cnt > NPA_MAX_CLIENTS ||
        !all_slots_empty(reg))
        goto done;
    for (i = 0; i < graph->dev_cnt; ++i) {
        const graph_dev_t* dev = &graph->devices[i];
        if (dev->prepared || dev->state.quarantined ||
            (dev->state.state_known && dev->state.current_d_state != 3u))
            goto done;
    }
    reg->provider = *provider;
    /* all resources must be owned before ANY client is created. */
    for (i = 0; i < reg->cnt; ++i)
        if (!key_strings_terminated(&reg->slots[i].key) ||
            !provider->owns(provider->ctx, &reg->slots[i].key))
            goto done;
    for (i = 0; i < reg->cnt; ++i) {
        npa_slot_t* slot = &reg->slots[i];
        void* handle = provider->create(provider->ctx, &slot->key);
        if (!handle)
            goto failed;
        slot->handle = handle;
        slot->active = 1u;
        if (!provider->owns(provider->ctx, &slot->key) ||
            !provider->validate(provider->ctx, &slot->key, handle))
            goto failed;
    }
    reg->active = 1u;
    ok = 1;
    goto done;
failed:
    (void)close_all(reg);
done:
    graph->release(graph->lock_ctx);
    return ok;
}

/* explicit retention is stricter than the sticky quarantined bit. */

int npa_needs_retention(const npa_registry_t* reg)
{
    uint32_t i;
    if (!reg)
        return 0;
    if (reg->quarantined || reg->active || reg->cnt > NPA_MAX_CLIENTS)
        return 1;
    /* a damaged or partially updated Count must not conceal a live handle in the fixed-size
     * registry. */

    for (i = 0u; i < NPA_MAX_CLIENTS; ++i)
        if (reg->slots[i].active || reg->slots[i].handle)
            return 1;
    return 0;
}

int npa_release(npa_registry_t* reg)
{
    const res_graph_t* graph;
    uint32_t i;
    int okay = 1;
    if (!reg || !reg->inventoried || !reg->provider.destroy || !reg->provider.is_released ||
        !reg->owner_graph)
        return 0;
    graph = reg->owner_graph;
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return 0;
    /* a device may still own NPA votes even if a provider currently sees zero on one handle.
     * Never release the registry while PREPARE/d0 is active, hardware state is quarantined, or
     * an emergency retirement occurred without a verified drain. */

    if (graph->retired && !graph->quiescing)
        okay = 0;
    /* invalid count metadata is a teardown fault, not permission to walk beyond either array
     * or to forget outstanding hardware-owned clients. */

    if (reg->cnt > NPA_MAX_CLIENTS || graph->dev_cnt > GRAPH_MAX_DEVICES) {
        reg->quarantined = 1u;
        okay = 0;
    }
    for (i = 0; okay && i < graph->dev_cnt; ++i) {
        const graph_dev_t* d = &graph->devices[i];
        if (d->prepared || d->state.quarantined ||
            (d->state.state_known && d->state.current_d_state != 3u))
            okay = 0;
    }
    if (okay) {
        okay = close_all(reg);
        /* keep deactivation under the same graph lock: a new DPM query cannot observe all handles
         * freed with a stale Active bit. */

        if (okay)
            reg->active = 0;
    }
    graph->release(graph->lock_ctx);
    if (!okay)
        return 0;
    /* quarantine is sticky: a failed teardown needs an external audit, even if a later attempt
     * seems to release the lingering handle. */

    return !reg->quarantined;
}

int npa_dev_clients_ready(const npa_registry_t* reg, const char* path)
{
    uint32_t i;
    int found = 0;
    if (!path || !active_slots_consistent(reg) || !reg->provider.validate || !reg->provider.owns)
        return 0;
    for (i = 0; i < reg->cnt; ++i) {
        const npa_slot_t* slot = &reg->slots[i];
        if (strcmp(slot->key.dev_path, path))
            continue;
        found = 1;
        if (!slot->active || !slot->handle || !reg->provider.owns(reg->provider.ctx, &slot->key) ||
            !reg->provider.validate(reg->provider.ctx, &slot->key, slot->handle))
            return 0;
    }
    return found;
}

int npa_resolve_action(const npa_registry_t* reg, const char* path, uint32_t state,
                       const power_action_t* action, const npa_slot_t** result)
{
    npa_key_t key;
    uint32_t i;
    if (result)
        *result = NULL;
    if (!reg || !result || !path || !action || (state != 0u && state != 3u) ||
        !active_slots_consistent(reg) || !reg->provider.validate || !reg->provider.owns ||
        !make_key(&key, path, state, action))
        return 0;
    for (i = 0; i < reg->cnt; ++i) {
        const npa_slot_t* slot = &reg->slots[i];
        if (!same_key(&slot->key, &key) ||
            (key.kind == POWER_CLOCK &&
             (slot->key.clock_mode_bits & key.clock_mode_bits) != key.clock_mode_bits))
            continue;
        if (!slot->active || !slot->handle || !reg->provider.owns(reg->provider.ctx, &slot->key) ||
            !reg->provider.validate(reg->provider.ctx, &slot->key, slot->handle))
            return 0;
        *result = slot;
        return 1;
    }
    return 0;
}

/* holding the graph lock keeps successful checks from racing NPA release/destroy. The
 * caller must still own an external rundown reference to graph memory. */

int npa_dev_clients_ready_held(const npa_registry_t* reg, const res_graph_t* graph,
                               const char* path)
{
    /* caller has acquired graph lock and holds external graph rundown. The read-only provider
     * validate callback must not take the lock. */

    return reg && graph && path && reg->owner_graph == graph && graph->sealed && !graph->retired &&
           !graph->quiescing && npa_dev_clients_ready(reg, path);
}

int npa_dev_clients_ready_locked(const npa_registry_t* reg, const char* path)
{
    const res_graph_t* graph;
    int ready;
    if (!reg || !path || !reg->owner_graph)
        return 0;
    graph = reg->owner_graph;
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return 0;
    ready = npa_dev_clients_ready_held(reg, graph, path);
    graph->release(graph->lock_ctx);
    return ready;
}
