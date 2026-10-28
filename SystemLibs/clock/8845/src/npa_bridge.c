#include "npa_bridge.h"
#include <string.h>
#include <stdint.h>

#define CLOCK_SIMPLE_MODES ((1u << 1) | (1u << 2))
#define SNAP_MAGIC UINT64_C(0x53434c4b50414952)

int decode_simple_clock_intent(const power_action_t* a, clock_intent_t* out)
{
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!a || a->kind != POWER_CLOCK || a->u.clock.extra_cnt != 0u)
        return 0;
    /* translate NPA enable/disable values 1/2 to physical values 1/0 at the transport
     * boundary. */

    if (a->u.clock.vote == 1u || a->u.clock.vote == 2u) {
        out->legacy_npa_action = a->u.clock.vote;
        out->effective_enabled = (a->u.clock.vote == 1u) ? 1u : 0u;
        return 1;
    }
    return 0;
}

/* validate bounded, caller-supplied res identities before any native provider callback or
 * comparison. Keys are fixed-size arrays and may be damaged after inventory; strcmp must
 * not walk into adjacent struct fields. */

static int bounded_nonempty(const char* value, size_t capacity)
{
    size_t i;
    if (!value || !capacity)
        return 0;
    for (i = 0; i < capacity; ++i)
        if (!value[i])
            return i != 0;
    return 0;
}

static int valid_scalar_resource_name(const char* value)
{
    size_t i, length;
    if (!bounded_nonempty(value, SCALAR_NAME_BYTES) || strncmp(value, "/clk/", 5) != 0)
        return 0;
    length = strlen(value);
    if (length <= 12 || strcmp(value + length - 7, "/enable") != 0)
        return 0;
    for (i = 5; i < length - 7; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return 0;
    }
    return 1;
}

static int valid_clock_key(const npa_key_t* key)
{
    return key && key->kind == POWER_CLOCK && key->d_state < POWER_DSTATE_COUNT &&
           bounded_nonempty(key->dev_path, sizeof(key->dev_path)) &&
           bounded_nonempty(key->res, sizeof(key->res));
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int equal_ci(const char* a, const char* b)
{
    size_t i;
    if (!a || !b)
        return 0;
    for (i = 0; i < SCALAR_NAME_BYTES; ++i) {
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
        if (!a[i])
            return 1;
    }
    return 0;
}

static int physical(scalar_res_t* r)
{
    /* unlike a bare owns_exclusive+readback check, this also validates continued boot-baseline
     * attestation and quarantines drift. */

    return scalar_verify_owned(r);
}

static int clock_name(const npa_key_t* key, char out[SCALAR_NAME_BYTES])
{
    static const char prefix[] = "/clk/", suffix[] = "/enable";
    size_t len, i;
    if (!valid_clock_key(key) || !key->clock_mode_bits ||
        (key->clock_mode_bits & ~CLOCK_SIMPLE_MODES))
        return 0;
    for (len = 0; len < NPA_NAME_BYTES && key->res[len]; ++len) {
        unsigned char c = (unsigned char)key->res[len];
        if (!(c >= 'A' && c <= 'Z') && !(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') &&
            c != '_')
            return 0;
    }
    if (!len || len >= NPA_NAME_BYTES ||
        len + sizeof(prefix) - 1u + sizeof(suffix) > SCALAR_NAME_BYTES)
        return 0;
    memcpy(out, prefix, sizeof(prefix) - 1u);
    for (i = 0; i < len; ++i)
        out[sizeof(prefix) - 1u + i] = key->res[i];
    memcpy(out + sizeof(prefix) - 1u + len, suffix, sizeof(suffix));
    return 1;
}

static scalar_res_t* res_for(scalar_clock_bridge_t* b, const npa_key_t* k)
{
    char name[SCALAR_NAME_BYTES];
    uint32_t i;
    if (!b || !clock_name(k, name))
        return NULL;
    for (i = 0; i < b->res_cnt; ++i)
        if (b->resources[i] && equal_ci(b->resources[i]->name, name))
            return b->resources[i];
    return NULL;
}

int scalar_clock_add_res(scalar_clock_bridge_t* b, scalar_res_t* r)
{
    uint32_t i;
    if (!b || !r || !valid_scalar_resource_name(r->name) || r->baseline_effective > 1u ||
        r->effective > 1u || b->res_cnt >= SCALAR_CLOCK_MAX_NODES || b->client_cnt || !physical(r))
        return 0;
    for (i = 0; i < b->res_cnt; ++i)
        if (b->resources[i] == r || equal_ci(b->resources[i]->name, r->name))
            return 0;
    b->resources[b->res_cnt++] = r;
    return 1;
}

static int owns(void* ctx, const npa_key_t* key)
{
    return physical(res_for((scalar_clock_bridge_t*)ctx, key));
}

/* client slots can be recycled across graph activations, but an address cannot serve as an
 * opaque handle: a stale pointer to the recycled slot would be accepted for a new client
 * with the same state/key (ABA). Use a strictly increasing, nonzero pointer-sized cookie
 * and never dereference incoming. */

static scalar_clock_client_t* client_for(scalar_clock_bridge_t* b, void* handle)
{
    uintptr_t cookie = (uintptr_t)handle;
    uint32_t i;
    if (!b || !cookie || b->client_cnt > SCALAR_CLOCK_MAX_HANDLES)
        return NULL;
    for (i = 0; i < b->client_cnt; ++i)
        if (b->clients[i].active && b->clients[i].cookie == cookie)
            return &b->clients[i];
    return NULL;
}

static int valid(void* ctx, const npa_key_t* key, void* handle)
{
    scalar_clock_bridge_t* b = ctx;
    scalar_clock_client_t* c = client_for(b, handle);
    scalar_res_t* r;
    if (!c || !valid_clock_key(key) || !owns(b, key))
        return 0;
    r = res_for(b, key);
    return r && r == c->res && c->key.d_state == key->d_state &&
           c->key.clock_mode_bits == key->clock_mode_bits && npa_same_res_identity(&c->key, key) &&
           c->scalar.slot < SCALAR_MAX_CLIENTS && r->clients[c->scalar.slot].exists &&
           r->clients[c->scalar.slot].generation == c->scalar.generation;
}

static void* create(void* ctx, const npa_key_t* key)
{
    scalar_clock_bridge_t* b = ctx;
    scalar_clock_client_t* c;
    scalar_res_t* r = res_for(b, key);
    scalar_handle_t h;
    uint32_t i;
    if (!b || !valid_clock_key(key) || !r || !physical(r) || b->next_cookie == UINTPTR_MAX ||
        b->client_cnt > SCALAR_CLOCK_MAX_HANDLES)
        return NULL;
    for (i = 0; i < b->client_cnt && b->clients[i].active; ++i) {
    }
    if (i == SCALAR_CLOCK_MAX_HANDLES)
        return NULL;
    if (!scalar_create(r, &h))
        return NULL;
    if (i == b->client_cnt)
        ++b->client_cnt;
    c = &b->clients[i];
    memset(c, 0, sizeof(*c));
    c->res = r;
    c->scalar = h;
    c->key = *key;
    c->active = 1u;
    c->cookie = ++b->next_cookie;
    return (void*)(uintptr_t)c->cookie;
}

static int released(void* ctx, const npa_key_t* key, void* handle)
{
    scalar_clock_client_t* c;
    scalar_client_t* scalar;
    if (!valid(ctx, key, handle))
        return 0;
    c = client_for(ctx, handle);
    scalar = &c->res->clients[c->scalar.slot];
    return !scalar->has_req;
}

static int destroy(void* ctx, const npa_key_t* key, void* handle)
{
    scalar_clock_client_t* c;
    if (!released(ctx, key, handle))
        return 0;
    c = client_for(ctx, handle);
    if (!c || !scalar_destroy(c->res, c->scalar))
        return 0;
    c->active = 0u;
    c->cookie = 0u;
    return 1;
}

static int group_clients(scalar_clock_bridge_t* bridge, const npa_req_group_t* group,
                         scalar_clock_client_t* clients[POWER_DSTATE_COUNT])
{
    const npa_slot_t* target;
    scalar_res_t* resource = NULL;
    uint32_t i;
    if (!bridge || !group || group->target >= POWER_DSTATE_COUNT ||
        !(target = group->states[group->target]))
        return 0;
    memset(clients, 0, POWER_DSTATE_COUNT * sizeof(*clients));
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i) {
        const npa_slot_t* slot = group->states[i];
        scalar_clock_client_t* client;
        if (!slot)
            continue;
        if (slot->key.d_state != i || !npa_same_res_identity(&slot->key, &target->key) ||
            !valid(bridge, &slot->key, slot->handle) ||
            !(client = client_for(bridge, slot->handle)) || (resource && client->res != resource))
            return 0;
        resource = client->res;
        clients[i] = client;
    }
    return clients[group->target] != NULL;
}

static int supports(void* ctx, const npa_req_group_t* group, const power_action_t* action)
{
    scalar_clock_client_t* clients[POWER_DSTATE_COUNT];
    clock_intent_t intent;
    return decode_simple_clock_intent(action, &intent) && group_clients(ctx, group, clients) &&
           equal_ci(group->states[group->target]->key.res, action->u.clock.name) &&
           (group->states[group->target]->key.clock_mode_bits & (1u << action->u.clock.vote));
}

static int read_group(void* ctx, const npa_req_group_t* group, power_snapshot_t* snapshot)
{
    scalar_clock_client_t* clients[POWER_DSTATE_COUNT];
    scalar_res_t* resource;
    uint32_t i, requests = 0u;
    if (!snapshot || !group_clients(ctx, group, clients))
        return 0;
    resource = clients[group->target]->res;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->opaque[0] = SNAP_MAGIC;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i) {
        const scalar_client_t* scalar;
        if (!clients[i])
            continue;
        scalar = &resource->clients[clients[i]->scalar.slot];
        if ((scalar->has_req && scalar->req != 1u) || (!scalar->has_req && scalar->req))
            return 0;
        snapshot->opaque[i + 1u] =
            ((uint64_t)clients[i]->scalar.generation << 32) | clients[i]->scalar.slot;
        if (scalar->has_req) {
            snapshot->opaque[5] |= UINT64_C(1) << i;
            ++requests;
        }
    }
    if (requests > 1u)
        return 0;
    snapshot->opaque[6] = resource->effective;
    snapshot->opaque[7] = resource->generation;
    return 1;
}

static int snapshot_matches(scalar_clock_client_t* clients[POWER_DSTATE_COUNT],
                            const npa_req_group_t* group, const power_snapshot_t* snapshot)
{
    uint32_t i;
    if (!snapshot || snapshot->opaque[0] != SNAP_MAGIC || snapshot->opaque[5] > 0x0fu ||
        (snapshot->opaque[5] && (snapshot->opaque[5] & (snapshot->opaque[5] - 1u))) ||
        snapshot->opaque[6] > 1u || snapshot->opaque[7] != clients[group->target]->res->generation)
        return 0;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i) {
        if (!clients[i]) {
            if (snapshot->opaque[i + 1u] || (snapshot->opaque[5] & (UINT64_C(1) << i)))
                return 0;
        } else if (snapshot->opaque[i + 1u] !=
                   (((uint64_t)clients[i]->scalar.generation << 32) | clients[i]->scalar.slot))
            return 0;
    }
    return 1;
}

static int group_matches(void* ctx, const npa_req_group_t* group, const power_action_t* action)
{
    scalar_clock_client_t* clients[POWER_DSTATE_COUNT];
    clock_intent_t intent;
    uint32_t i;
    if (!supports(ctx, group, action) || !decode_simple_clock_intent(action, &intent) ||
        !group_clients(ctx, group, clients))
        return 0;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i) {
        const scalar_client_t* scalar;
        int issued = i == group->target && intent.effective_enabled;
        if (!clients[i])
            continue;
        scalar = &clients[i]->res->clients[clients[i]->scalar.slot];
        if (issued ? (!scalar->has_req || scalar->req != 1u) : (scalar->has_req || scalar->req))
            return 0;
    }
    return 1;
}

static int issue_group(void* ctx, const npa_req_group_t* group, const power_action_t* action)
{
    scalar_clock_client_t* clients[POWER_DSTATE_COUNT];
    scalar_clock_client_t* target;
    clock_intent_t intent;
    uint32_t i;
    if (!supports(ctx, group, action) || !decode_simple_clock_intent(action, &intent) ||
        !group_clients(ctx, group, clients))
        return 0;
    target = clients[group->target];
    /* Acquire the incoming vote before releasing any previous state's request. */
    if (intent.effective_enabled && !scalar_issue(target->res, target->scalar, 1u))
        return 0;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i)
        if (clients[i] && (i != group->target || !intent.effective_enabled) &&
            !scalar_complete(clients[i]->res, clients[i]->scalar))
            return 0;
    return group_matches(ctx, group, action);
}

static int restore_group(void* ctx, const npa_req_group_t* group, const power_snapshot_t* snapshot)
{
    scalar_clock_client_t* clients[POWER_DSTATE_COUNT];
    scalar_res_t* resource;
    uint32_t i;
    if (!group_clients(ctx, group, clients) || !snapshot_matches(clients, group, snapshot))
        return 0;
    resource = clients[group->target]->res;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i)
        if (clients[i] && (snapshot->opaque[5] & (UINT64_C(1) << i)) &&
            !scalar_issue(resource, clients[i]->scalar, 1u))
            return 0;
    for (i = 0u; i < POWER_DSTATE_COUNT; ++i)
        if (clients[i] && !(snapshot->opaque[5] & (UINT64_C(1) << i)) &&
            !scalar_complete(resource, clients[i]->scalar))
            return 0;
    return physical(resource) && resource->effective == snapshot->opaque[6];
}

static int verify_restored(void* ctx, const npa_req_group_t* group,
                           const power_snapshot_t* snapshot)
{
    power_snapshot_t current;
    return snapshot && read_group(ctx, group, &current) &&
           !memcmp(snapshot, &current, sizeof(current));
}

void scalar_clock_lifecycle(scalar_clock_bridge_t* b, npa_callbacks_t* out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!b)
        return;
    out->ctx = b;
    out->owns = owns;
    out->create = create;
    out->validate = valid;
    out->is_released = released;
    out->destroy = destroy;
}

void scalar_clock_requests(scalar_clock_bridge_t* b, npa_req_ops_t* out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!b)
        return;
    out->ctx = b;
    out->supports = supports;
    out->read = read_group;
    out->issue = issue_group;
    out->verify = group_matches;
    out->restore = restore_group;
    out->verify_restored = verify_restored;
    out->verify_final = group_matches;
}
