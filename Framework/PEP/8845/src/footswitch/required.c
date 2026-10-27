#include "required.h"
#include <string.h>

static int name_ok(const char* name, size_t cap)
{
    size_t i;
    if (!name || !cap)
        return 0;
    for (i = 0u; i < cap; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c)
            return i != 0u;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return 0;
    }
    return 0;
}

static int epoch_ok(pofx_fs_required_t* s)
{
    uint64_t current = 0u;
    return s && s->bound && !s->quarantined && s->epoch && s->native.owns_domain &&
           s->native.read_owner_epoch && s->native.owns_domain(s->native.ctx, s->domain) &&
           s->native.read_owner_epoch(s->native.ctx, s->domain, &current) && current == s->epoch;
}

static int physical(pofx_fs_required_t* s, uint32_t* actual)
{
    return actual && epoch_ok(s) && s->read_physical &&
           s->read_physical(s->physical_ctx, s->domain, s->epoch, actual) &&
           (*actual == 1u || *actual == 2u) && epoch_ok(s);
}

static int owns(void* ctx, const pofx_required_action_t* a)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    uint32_t actual = 0u;
    return a && a->action.kind == POWER_FOOTSWITCH && a->program_class == PROGRAM_P_STATE &&
           a->action.u.footswitch.action >= 1u && a->action.u.footswitch.action <= 2u && s &&
           !strcmp(a->action.u.footswitch.name, s->domain) && physical(s, &actual);
}

static pofx_fs_required_slot_t* lookup(pofx_fs_required_t* s, const pofx_required_action_t* a,
                                       void* handle)
{
    uintptr_t cookie = (uintptr_t)handle;
    uint32_t i;
    if (!cookie || !a || !s || !epoch_ok(s))
        return NULL;
    for (i = 0u; i < POFX_FS_REQUIRED_SLOTS; ++i)
        if (s->slots[i].active && s->slots[i].cookie == cookie && s->slots[i].action == a &&
            !memcmp(&s->slots[i].snapshot, a, sizeof(*a)))
            return &s->slots[i];
    return NULL;
}

static int read_native(pofx_fs_required_t* s, pofx_fs_required_slot_t* slot, uint32_t* present,
                       uint32_t* scalar)
{
    uint32_t actual = 0u;
    return slot && present && scalar && s->native.read_client && epoch_ok(s) &&
           s->native.read_client(s->native.ctx, s->res, slot->native, present, scalar) &&
           *present <= 1u && (!*present || *scalar == slot->action->action.u.footswitch.action) &&
           physical(s, &actual);
}

static void* create(void* ctx, const pofx_required_action_t* a)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    pofx_fs_required_slot_t* slot = NULL;
    char client[FS_FACTORY_NAME_BYTES];
    uint32_t i, present = UINT32_MAX, scalar = UINT32_MAX;
    uintptr_t cookie;
    uint64_t token = 0u;
    if (!owns(s, a) || !s->native.open_idle_client || s->next_cookie == UINTPTR_MAX)
        return NULL;
    for (i = 0u; i < POFX_FS_REQUIRED_SLOTS; ++i)
        if (s->slots[i].active && s->slots[i].action == a)
            return NULL;
    for (i = 0u; i < POFX_FS_REQUIRED_SLOTS; ++i)
        if (!s->slots[i].active) {
            slot = &s->slots[i];
            break;
        }
    if (!slot)
        return NULL;
    /* client names are bounded identifiers, not ad hoc ACPI device paths. */
    memset(client, 0, sizeof(client));
    {
        const char prefix[] = "QcPep8845_FpFs_";
        size_t p = sizeof(prefix) - 1u;
        uint32_t v = i;
        memcpy(client, prefix, p);
        client[p++] = (char)('0' + ((v / 10u) % 10u));
        client[p++] = (char)('0' + (v % 10u));
        client[p] = 0;
    }
    if (!s->native.open_idle_client(s->native.ctx, s->res, client, FS_NPA_REQUIRED_CLIENT_TYPE,
                                    &token) ||
        !token)
        return NULL;
    cookie = ++s->next_cookie;
    slot->action = a;
    slot->snapshot = *a;
    slot->native = token;
    slot->cookie = cookie;
    slot->active = 1u;
    /* a client might have been opened even if its first read fails. Keep it addressable so the
     * required-client owner quarantines the entire scope. */
    if (!read_native(s, slot, &present, &scalar) || present || scalar) {
        s->quarantined = 1u;
        return (void*)cookie;
    }
    return (void*)cookie;
}

static int validate(void* ctx, const pofx_required_action_t* a, void* h)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint32_t p = UINT32_MAX, v = UINT32_MAX;
    return slot && owns(s, a) && read_native(s, slot, &p, &v);
}

static int read_epoch(void* ctx, uint64_t* value)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    if (!value || !epoch_ok(s))
        return 0;
    *value = s->epoch;
    return 1;
}

static int read_client(void* ctx, const pofx_required_action_t* a, void* h, uint32_t* out)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint32_t p = UINT32_MAX, v = UINT32_MAX;
    if (!out || !slot || !read_native(s, slot, &p, &v))
        return 0;
    *out = p;
    return 1;
}

static int released(void* ctx, const pofx_required_action_t* a, void* h)
{
    uint32_t p = UINT32_MAX;
    return read_client(ctx, a, h, &p) && p == 0u;
}

static int destroy(void* ctx, const pofx_required_action_t* a, void* h)
{
    pofx_fs_required_t* s = (pofx_fs_required_t*)ctx;
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint64_t token;
    if (!slot || !released(ctx, a, h) || !s->native.close_idle_client || !s->native.client_gone)
        return 0;
    token = slot->native;
    if (!s->native.close_idle_client(s->native.ctx, s->res, token)) {
        s->quarantined = 1u;
        return 0;
    }
    if (!s->native.client_gone(s->native.ctx, s->res, token)) {
        s->quarantined = 1u;
        return 0;
    }
    memset(slot, 0, sizeof(*slot));
    return 1;
}

int po_fx_fs_required_bind(pofx_fs_required_t* s, const fs_factory_provider_t* n,
                           const char* domain, uint64_t epoch, void* pc,
                           int (*readphysical)(void*, const char*, uint64_t, uint32_t*))
{
    size_t length;
    uintptr_t sequence;
    uint64_t ep = 0u;
    if (!s || s->bound || s->quarantined || !n || !domain || !epoch || !readphysical ||
        !n->owns_domain || !n->read_owner_epoch || !n->open_idle_client || !n->read_client ||
        !n->issue_client || !n->can_complete_client || !n->complete_client ||
        !n->close_idle_client || !n->client_gone || !n->can_retire_off_client ||
        !n->retire_off_client || !n->owns_domain(n->ctx, domain) ||
        !n->read_owner_epoch(n->ctx, domain, &ep) || ep != epoch || !name_ok(domain, FS_NAME_BYTES))
        return 0;
    length = strlen(domain);
    if (length + sizeof("/fs//enable") > FS_FACTORY_NAME_BYTES)
        return 0;
    sequence = s->next_cookie;
    memset(s, 0, sizeof(*s));
    s->next_cookie = sequence;
    s->native = *n;
    s->epoch = epoch;
    s->read_physical = readphysical;
    s->physical_ctx = pc;
    memcpy(s->domain, domain, length + 1u);
    memcpy(s->res, "/fs/", 4u);
    memcpy(s->res + 4u, domain, length);
    memcpy(s->res + 4u + length, "/enable", 8u);
    s->bound = 1u;
    {
        uint32_t actual = 0u;
        if (!physical(s, &actual)) {
            sequence = s->next_cookie;
            memset(s, 0, sizeof(*s));
            s->next_cookie = sequence;
            return 0;
        }
    }
    return 1;
}

void po_fx_fs_required_provider(pofx_fs_required_t* s, pofx_required_provider_t* out)
{
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->ctx = s;
    out->owns = owns;
    out->create = create;
    out->validate = validate;
    out->read_owner_epoch = read_epoch;
    out->read_client = read_client;
    out->is_released = released;
    out->destroy = destroy;
}

int po_fx_fs_required_issue(pofx_fs_required_t* s, const pofx_required_action_t* a, void* h)
{
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint32_t p = 0u, v = 0u, actual = 0u;
    if (!slot || !owns(s, a) || !s->native.issue_client ||
        !s->native.issue_client(s->native.ctx, s->res, slot->native,
                                a->action.u.footswitch.action) ||
        !read_native(s, slot, &p, &v) || p != 1u || !physical(s, &actual) ||
        actual != a->action.u.footswitch.action) {
        if (slot)
            s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int po_fx_fs_required_handoff(pofx_fs_required_t* s, const pofx_required_action_t* previous,
                              void* previous_handle, const pofx_required_action_t* next,
                              void* next_handle)
{
    pofx_fs_required_slot_t *old_slot, *new_slot;
    uint32_t old_present = UINT32_MAX, new_present = UINT32_MAX;
    uint32_t old_value = UINT32_MAX, new_value = UINT32_MAX, actual = 0u;
    uint32_t from, to;
    int issued = 0;
    if (!s || !previous || !next || previous == next || !owns(s, previous) || !owns(s, next))
        return 0;
    old_slot = lookup(s, previous, previous_handle);
    new_slot = lookup(s, next, next_handle);
    if (!old_slot || !new_slot || old_slot == new_slot || !s->native.issue_client ||
        !s->native.can_complete_client || !s->native.complete_client)
        return 0;
    from = previous->action.u.footswitch.action;
    to = next->action.u.footswitch.action;
    if (!((from == 1u && to == 2u) || (from == 2u && to == 1u)) ||
        strcmp(previous->action.u.footswitch.name, next->action.u.footswitch.name) ||
        !read_native(s, old_slot, &old_present, &old_value) || old_present != 1u ||
        old_value != from || !read_native(s, new_slot, &new_present, &new_value) ||
        new_present != 0u || !physical(s, &actual) || actual != from)
        return 0;

    /* the src slot remains present until the destination is committed. A failed post-dispatch
     * readback is ambiguous: retain both clients. */
    issued = 1;
    if (!s->native.issue_client(s->native.ctx, s->res, new_slot->native, to) ||
        !read_native(s, new_slot, &new_present, &new_value) || new_present != 1u ||
        new_value != to || !read_native(s, old_slot, &old_present, &old_value) ||
        old_present != 1u || old_value != from ||
        !s->native.can_complete_client(s->native.ctx, s->res, old_slot->native) ||
        !s->native.complete_client(s->native.ctx, s->res, old_slot->native) ||
        !read_native(s, old_slot, &old_present, &old_value) || old_present != 0u ||
        !read_native(s, new_slot, &new_present, &new_value) || new_present != 1u ||
        new_value != to || !physical(s, &actual) || actual != to || !epoch_ok(s)) {
        if (issued)
            s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int po_fx_fs_required_complete(pofx_fs_required_t* s, const pofx_required_action_t* a, void* h)
{
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint32_t p = UINT32_MAX, v = UINT32_MAX;
    if (!slot || !s->native.can_complete_client || !s->native.complete_client ||
        !s->native.can_complete_client(s->native.ctx, s->res, slot->native))
        return 0;
    if (!s->native.complete_client(s->native.ctx, s->res, slot->native) ||
        !read_native(s, slot, &p, &v) || p) {
        s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int po_fx_fs_required_retire_off(pofx_fs_required_t* s, const pofx_required_action_t* a, void* h)
{
    pofx_fs_required_slot_t* slot = lookup(s, a, h);
    uint32_t actual = 0u, p = UINT32_MAX, v = UINT32_MAX;
    if (!slot || a->action.u.footswitch.action != 2u || !s->native.can_retire_off_client ||
        !s->native.retire_off_client || !physical(s, &actual) || actual != 2u ||
        !s->native.can_retire_off_client(s->native.ctx, s->res, slot->native))
        return 0;
    if (!s->native.retire_off_client(s->native.ctx, s->res, slot->native) ||
        !read_native(s, slot, &p, &v) || p) {
        s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int po_fx_fs_required_unbind(pofx_fs_required_t* s)
{
    uint32_t i;
    if (!s || !epoch_ok(s))
        return 0;
    for (i = 0u; i < POFX_FS_REQUIRED_SLOTS; ++i)
        if (s->slots[i].active)
            return 0;
    {
        uintptr_t sequence = s->next_cookie;
        memset(s, 0, sizeof(*s));
        s->next_cookie = sequence;
    }
    return 1;
}
