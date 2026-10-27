#include "scalar_required.h"
#include <string.h>

static int scalar_kind(const power_action_t* a)
{
    return a && (a->kind == POWER_CLOCK || a->kind == POWER_ICB || a->kind == POWER_PMIC_REGULATOR);
}

static int requires_program_transaction(const scalar_required_t* s, const pofx_required_action_t* a)
{
    uint32_t i;
    if (a->action.kind == POWER_ICB && !a->action.u.icb.average && !a->action.u.icb.peak)
        return 1;
    if (a->action.kind == POWER_PMIC_REGULATOR && !a->action.u.pmic_regulator.fields[1])
        return 1;
    if (a->action.kind != POWER_CLOCK)
        return 0;
    for (i = 0u; i < POFX_REQUIRED_MAX_ACTIONS; ++i) {
        const scalar_required_slot_t* existing = &s->slots[i];
        const pofx_required_action_t* other = existing->action;
        if (!existing->live || !other || other == a || other->program_class != a->program_class ||
            other->comp != a->comp || other->set != a->set || other->state != a->state ||
            other->action.kind != POWER_CLOCK)
            continue;
        /* enable, CONFIGURE and rate in one program are ordered operations on ONE native clock
         * client. Per-action identities would change the hardware aggregate; refuse until the
         * atomic program owner exists. */
        if (!strcmp(other->action.u.clock.name, a->action.u.clock.name))
            return 1;
    }
    return 0;
}

static int epoch_live(const scalar_required_t* s)
{
    uint64_t epoch = 0u;
    return s && s->bound && !s->quarantined && s->epoch && s->io.read_epoch &&
           s->io.read_epoch(s->io.ctx, &epoch) && epoch == s->epoch;
}

static scalar_required_slot_t* slot_lookup(scalar_required_t* s, const pofx_required_action_t* a,
                                           void* h)
{
    uintptr_t cookie = (uintptr_t)h;
    uint32_t i;
    if (!cookie || !a || !epoch_live(s))
        return NULL;
    for (i = 0; i < POFX_REQUIRED_MAX_ACTIONS; ++i) {
        scalar_required_slot_t* slot = &s->slots[i];
        if (slot->live && slot->cookie == cookie && slot->action == a &&
            !memcmp(&slot->snapshot, a, sizeof(*a)))
            return slot;
    }
    return NULL;
}

static int owns(void* ctx, const pofx_required_action_t* a)
{
    scalar_required_t* s = (scalar_required_t*)ctx;
    return scalar_kind(a ? &a->action : NULL) && epoch_live(s) && s->io.owns &&
           s->io.owns(s->io.ctx, &a->action) && s->io.verify_physical &&
           s->io.verify_physical(s->io.ctx, &a->action);
}

static int read_epoch(void* ctx, uint64_t* out)
{
    scalar_required_t* s = (scalar_required_t*)ctx;
    if (!out || !epoch_live(s))
        return 0;
    *out = s->epoch;
    return 1;
}

static int read_client(void* ctx, const pofx_required_action_t* a, void* h, uint32_t* out)
{
    scalar_required_t* s = (scalar_required_t*)ctx;
    scalar_required_slot_t* slot = slot_lookup(s, a, h);
    if (!slot || !out || !s->io.read_vote ||
        !s->io.read_vote(s->io.ctx, slot->identity, &a->action, out) || *out > 1u ||
        !s->io.verify_physical(s->io.ctx, &a->action) || !epoch_live(s))
        return 0;
    return 1;
}

static void* create(void* ctx, const pofx_required_action_t* a)
{
    scalar_required_t* s = (scalar_required_t*)ctx;
    uint32_t i, present = UINT32_MAX;
    scalar_required_slot_t* slot = NULL;
    if (!owns(ctx, a) || s->serial == UINTPTR_MAX)
        return NULL;
    for (i = 0; i < POFX_REQUIRED_MAX_ACTIONS; ++i) {
        if (s->slots[i].live && s->slots[i].action == a)
            return NULL;
        if (!slot && !s->slots[i].live)
            slot = &s->slots[i];
    }
    if (!slot)
        return NULL;
    {
        const unsigned values[5] = {a->comp, (unsigned)a->program_class, a->set, a->state,
                                    (unsigned)(slot - s->slots)};
        const char labels[5] = {'C', 'K', 'S', 'T', 'I'};
        size_t pos = 0u;
        unsigned field;
        const char prefix[] = "PFX.";
        memcpy(slot->identity, prefix, sizeof(prefix) - 1u);
        pos = sizeof(prefix) - 1u;
        for (field = 0u; field < 5u; ++field) {
            char digits[11];
            unsigned n = values[field], cnt = 0u;
            if (pos + 13u >= sizeof(slot->identity))
                return NULL;
            slot->identity[pos++] = labels[field];
            do {
                digits[cnt++] = (char)('0' + n % 10u);
                n /= 10u;
            } while (n);
            while (cnt)
                slot->identity[pos++] = digits[--cnt];
            if (field != 4u)
                slot->identity[pos++] = '.';
        }
        slot->identity[pos] = 0;
    }
    slot->action = a;
    slot->snapshot = *a;
    slot->cookie = ++s->serial;
    slot->live = 1u;
    if (!read_client(ctx, a, (void*)slot->cookie, &present) || present) {
        s->quarantined = 1u;
    }
    return (void*)slot->cookie;
}

static int validate(void* ctx, const pofx_required_action_t* a, void* h)
{
    uint32_t present = UINT32_MAX;
    return slot_lookup((scalar_required_t*)ctx, a, h) != NULL && read_client(ctx, a, h, &present);
}

static int released(void* ctx, const pofx_required_action_t* a, void* h)
{
    uint32_t present = UINT32_MAX;
    return read_client(ctx, a, h, &present) && present == 0u;
}

static int destroy(void* ctx, const pofx_required_action_t* a, void* h)
{
    scalar_required_t* s = (scalar_required_t*)ctx;
    scalar_required_slot_t* slot = slot_lookup(s, a, h);
    if (!slot || !released(ctx, a, h))
        return 0;
    memset(slot, 0, sizeof(*slot));
    return 1;
}

int scalar_required_bind(scalar_required_t* s, const scalar_required_io_t* io, uint64_t epoch)
{
    uint64_t actual = 0u;
    uintptr_t sequence;
    if (!s || s->bound || s->quarantined || !io || !epoch || !io->read_epoch || !io->owns ||
        !io->read_vote || !io->issue || !io->withdraw || !io->verify_physical ||
        !io->read_epoch(io->ctx, &actual) || actual != epoch)
        return 0;
    sequence = s->serial;
    memset(s, 0, sizeof(*s));
    s->serial = sequence;
    s->io = *io;
    s->epoch = epoch;
    s->bound = 1u;
    return 1;
}

void scalar_required_provider(scalar_required_t* s, pofx_required_provider_t* out)
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

int scalar_required_issue(scalar_required_t* s, const pofx_required_action_t* a, void* h)
{
    scalar_required_slot_t* slot = slot_lookup(s, a, h);
    uint32_t before = UINT32_MAX, after = UINT32_MAX;
    if (!slot || !owns(s, a) || requires_program_transaction(s, a) ||
        !read_client(s, a, h, &before) || before)
        return 0;
    if (!s->io.issue(s->io.ctx, slot->identity, &a->action)) {
        s->quarantined = 1u;
        return 0;
    }
    if (!read_client(s, a, h, &after) || after != 1u) {
        s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int scalar_required_withdraw(scalar_required_t* s, const pofx_required_action_t* a, void* h)
{
    scalar_required_slot_t* slot = slot_lookup(s, a, h);
    uint32_t before = UINT32_MAX, after = UINT32_MAX;
    if (!slot || !read_client(s, a, h, &before))
        return 0;
    if (!before)
        return 1;
    if (!s->io.withdraw(s->io.ctx, slot->identity, &a->action)) {
        s->quarantined = 1u;
        return 0;
    }
    if (!read_client(s, a, h, &after) || after) {
        s->quarantined = 1u;
        return 0;
    }
    return 1;
}

int scalar_required_unbind(scalar_required_t* s)
{
    uintptr_t sequence;
    uint32_t i;
    if (!epoch_live(s))
        return 0;
    for (i = 0; i < POFX_REQUIRED_MAX_ACTIONS; ++i)
        if (s->slots[i].live)
            return 0;
    sequence = s->serial;
    memset(s, 0, sizeof(*s));
    s->serial = sequence;
    return 1;
}
