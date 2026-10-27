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
    /* reference SC8280 uses the literal enum as the /clk/.../enable NPA scalar request (1
     * enable, 2 disable); the scalar core's owned physical aggregate instead uses 1 enabled
     * and 0 disabled. Never pass the raw 2 into the physical writer or raw 0 to a legacy NPA
     * client. */

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

static int scalar_name_ok(const char* value)
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

static int clock_key_ok(const npa_key_t* key)
{
    return key && key->kind == POWER_CLOCK && (key->d_state == 0u || key->d_state == 3u) &&
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
    if (!clock_key_ok(key) || !key->clock_mode_bits || (key->clock_mode_bits & ~CLOCK_SIMPLE_MODES))
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
    if (!b || !r || !scalar_name_ok(r->name) || r->baseline_effective > 1u || r->effective > 1u ||
        b->res_cnt >= SCALAR_CLOCK_MAX_NODES || b->client_cnt || !physical(r))
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
 * and NEVER dereference incoming. */

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
    if (!c || !clock_key_ok(key) || !owns(b, key))
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
    if (!b || !clock_key_ok(key) || !r || !physical(r) || b->next_cookie == UINTPTR_MAX ||
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

static int pair(void* ctx, const npa_key_t* k, void* h, const npa_key_t* peer, void* ph,
                const power_action_t* a)
{
    scalar_clock_client_t* c = client_for(ctx, h);
    scalar_clock_client_t* p = client_for(ctx, ph);
    clock_intent_t intent;
    if (!decode_simple_clock_intent(a, &intent) || !clock_key_ok(k) || !clock_key_ok(peer) ||
        k->d_state == peer->d_state || !equal_ci(k->res, peer->res) ||
        strcmp(k->dev_path, peer->dev_path) != 0 ||
        !(k->clock_mode_bits & (1u << a->u.clock.vote)) || !valid(ctx, k, h) ||
        !valid(ctx, peer, ph) || !c || !p || c == p || c->res != p->res)
        return 0;
    return 1;
}

static int read_pair(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                     power_snapshot_t* sn)
{
    scalar_clock_client_t *c = client_for(ctx, h), *p = client_for(ctx, ph);
    const scalar_client_t *a, *b;
    if (!sn || !valid(ctx, k, h) || !valid(ctx, pk, ph) || c->res != p->res || !physical(c->res))
        return 0;
    a = &c->res->clients[c->scalar.slot];
    b = &p->res->clients[p->scalar.slot];
    if (a->has_req && a->req != 1u)
        return 0;
    if (b->has_req && b->req != 1u)
        return 0;
    if (a->has_req && b->has_req)
        return 0;
    memset(sn, 0, sizeof(*sn));
    sn->opaque[0] = SNAP_MAGIC;
    sn->opaque[1] = ((uint64_t)c->scalar.generation << 32) | c->scalar.slot;
    sn->opaque[2] = ((uint64_t)p->scalar.generation << 32) | p->scalar.slot;
    sn->opaque[3] = a->req;
    sn->opaque[4] = b->req;
    sn->opaque[5] = (uint64_t)a->has_req | ((uint64_t)b->has_req << 1);
    sn->opaque[6] = c->res->effective;
    sn->opaque[7] = c->res->generation;
    return 1;
}

static int snap_ok(scalar_clock_client_t* c, scalar_clock_client_t* p, const power_snapshot_t* sn)
{
    return sn && sn->opaque[0] == SNAP_MAGIC &&
           sn->opaque[1] == (((uint64_t)c->scalar.generation << 32) | c->scalar.slot) &&
           sn->opaque[2] == (((uint64_t)p->scalar.generation << 32) | p->scalar.slot) &&
           sn->opaque[5] <= 2u && sn->opaque[3] <= 1u && sn->opaque[4] <= 1u &&
           sn->opaque[6] <= 1u && sn->opaque[7] == c->res->generation &&
           ((sn->opaque[5] & 1u) == 0u || sn->opaque[3] == 1u) &&
           ((sn->opaque[5] & 2u) == 0u || sn->opaque[4] == 1u);
}

static int pair_matches(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                        const power_action_t* a)
{
    scalar_clock_client_t *c = client_for(ctx, h), *p = client_for(ctx, ph);
    const scalar_client_t *cr, *pr;
    clock_intent_t intent;
    if (!decode_simple_clock_intent(a, &intent) || !pair(ctx, k, h, pk, ph, a) || !physical(c->res))
        return 0;
    cr = &c->res->clients[c->scalar.slot];
    pr = &p->res->clients[p->scalar.slot];
    if (pr->has_req)
        return 0;
    return intent.effective_enabled ? (cr->has_req && cr->req == 1u) : !cr->has_req;
}

static int issue_pair(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                      const power_action_t* a)
{
    scalar_clock_client_t *c = client_for(ctx, h), *p = client_for(ctx, ph);
    clock_intent_t intent;
    if (!decode_simple_clock_intent(a, &intent) || !pair(ctx, k, h, pk, ph, a))
        return 0;
    /* graph owns rollback for a partially applied second operation (including this action). */

    if (intent.effective_enabled && !scalar_issue(c->res, c->scalar, 1u))
        return 0;
    if (!scalar_complete(p->res, p->scalar))
        return 0;
    if (!intent.effective_enabled && !scalar_complete(c->res, c->scalar))
        return 0;
    return pair_matches(ctx, k, h, pk, ph, a);
}

static int restore_pair(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                        const power_snapshot_t* sn)
{
    scalar_clock_client_t *c = client_for(ctx, h), *p = client_for(ctx, ph);
    if (!valid(ctx, k, h) || !valid(ctx, pk, ph) || c->res != p->res || !snap_ok(c, p, sn))
        return 0;
    /* re-enable the original winning client before withdrawing any changed vote, then verify
     * the independent effective state after restoration. */

    if ((sn->opaque[5] & 1u) && !scalar_issue(c->res, c->scalar, 1u))
        return 0;
    if ((sn->opaque[5] & 2u) && !scalar_issue(p->res, p->scalar, 1u))
        return 0;
    if (!(sn->opaque[5] & 1u) && !scalar_complete(c->res, c->scalar))
        return 0;
    if (!(sn->opaque[5] & 2u) && !scalar_complete(p->res, p->scalar))
        return 0;
    return physical(c->res) && c->res->effective == sn->opaque[6];
}

static int verify_restored(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                           const power_snapshot_t* sn)
{
    scalar_clock_client_t *c = client_for(ctx, h), *p = client_for(ctx, ph);
    const scalar_client_t *cr, *pr;
    if (!valid(ctx, k, h) || !valid(ctx, pk, ph) || c->res != p->res || !snap_ok(c, p, sn))
        return 0;
    cr = &c->res->clients[c->scalar.slot];
    pr = &p->res->clients[p->scalar.slot];
    return physical(c->res) && c->res->effective == sn->opaque[6] &&
           cr->has_req == (sn->opaque[5] & 1u) && pr->has_req == ((sn->opaque[5] >> 1) & 1u) &&
           cr->req == sn->opaque[3] && pr->req == sn->opaque[4];
}

static int supports(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                    const power_action_t* a)
{
    return pair(ctx, k, h, pk, ph, a);
}

static int verify(void* ctx, const npa_key_t* k, void* h, const npa_key_t* pk, void* ph,
                  const power_action_t* a)
{
    return pair_matches(ctx, k, h, pk, ph, a);
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
    out->supports_pair = supports;
    out->read_pair = read_pair;
    out->issue_pair = issue_pair;
    out->verify_pair = verify;
    out->restore_pair = restore_pair;
    out->verify_restored_pair = verify_restored;
    out->verify_final_pair = verify;
}
