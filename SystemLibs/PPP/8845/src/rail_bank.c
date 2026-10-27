#include "rail_bank.h"
#include <string.h>

static int idx_of(uint32_t rail)
{
    if (rail == RAIL_LDO4_B)
        return 0;
    if (rail == RAIL_LDO15_B)
        return 1;
    return -1;
}

static int equal(rail_state_t a, rail_state_t b)
{
    return a.enabled == b.enabled && (!a.enabled || a.microvolts == b.microvolts);
}

static int valid(uint32_t rail, rail_state_t s)
{
    uint32_t min, max;
    if (s.enabled > 1u)
        return 0;
    if (!s.enabled)
        return s.microvolts == 0u;
    if (rail == RAIL_LDO4_B) {
        min = 1650000u;
        max = 1950000u;
    } else if (rail == RAIL_LDO15_B) {
        min = 1620000u;
        max = 2000000u;
    } else
        return 0;
    return s.microvolts >= min && s.microvolts <= max;
}

static int usable(const rail_bank_t* b)
{
    return b && b->provider.lock && b->provider.unlock && b->provider.owns &&
           b->provider.read_current && b->provider.apply;
}

static int read_verified(rail_bank_t* b, uint32_t rail, rail_state_t expected)
{
    rail_state_t a = {0}, c = {0};
    if (!b->provider.read_current(b->provider.ctx, rail, &a) ||
        !b->provider.read_current(b->provider.ctx, rail, &c))
        return 0;
    return valid(rail, a) && valid(rail, c) && equal(a, c) && equal(a, expected);
}

rail_result_t rail_adopt(rail_bank_t* b, uint32_t rail, rail_state_t baseline)
{
    rail_result_t result = RAIL_INVALID;
    rail_bank_entry_t* r;
    int idx = idx_of(rail);
    if (!usable(b) || idx < 0 || !valid(rail, baseline))
        return result;
    if (!b->provider.lock(b->provider.ctx))
        return RAIL_NO_OWNERSHIP;
    r = &b->rails[idx];
    if (r->adopted)
        goto done;
    if (!b->provider.owns(b->provider.ctx, rail)) {
        result = RAIL_NO_OWNERSHIP;
        goto done;
    }
    if (!read_verified(b, rail, baseline)) {
        result = RAIL_HARDWARE_FAILURE;
        goto done;
    }
    memset(r, 0, sizeof(*r));
    r->baseline = baseline;
    r->applied = baseline;
    r->adopted = 1;
    r->state_known = 1;
    result = RAIL_OK;
done:
    b->provider.unlock(b->provider.ctx);
    return result;
}

rail_result_t rail_vote(rail_bank_t* b, uint32_t rail, uint64_t client, rail_state_t desired)
{
    rail_result_t result = RAIL_INVALID;
    rail_bank_entry_t* r;
    rail_client_t *old = 0, *free_slot = 0, *slot;
    rail_state_t aggregate;
    unsigned i;
    int idx = idx_of(rail);
    if (!usable(b) || idx < 0 || !client || !valid(rail, desired))
        return result;
    if (!b->provider.lock(b->provider.ctx))
        return RAIL_NO_OWNERSHIP;
    r = &b->rails[idx];
    if (!r->adopted || !r->state_known) {
        result = RAIL_STATE_UNKNOWN;
        goto done;
    }
    if (!b->provider.owns(b->provider.ctx, rail)) {
        /* a bank that loses its authoritative provider cannot assume its last cached aggregate
         * still describes the physical rail. */

        r->state_known = 0;
        result = RAIL_NO_OWNERSHIP;
        goto done;
    }
    /* even a no-op client update must not succeed from a cached aggregate. Another
     * provider/boot owner may have changed the rail since adoption or a prior vote. */

    if (!read_verified(b, rail, r->applied)) {
        r->state_known = 0;
        result = RAIL_STATE_UNKNOWN;
        goto done;
    }
    for (i = 0; i < RAIL_MAX_CLIENTS; ++i) {
        slot = &r->clients[i];
        if (slot->in_use && slot->id == client) {
            old = slot;
            break;
        }
        if (!slot->in_use && !free_slot)
            free_slot = slot;
    }
    /* a d3 for an unknown client must not touch the rail. */
    if (!desired.enabled && !old)
        goto done;
    if (desired.enabled && !old && !free_slot) {
        result = RAIL_NO_CAPACITY;
        goto done;
    }
    aggregate = r->baseline;
    for (i = 0; i < RAIL_MAX_CLIENTS; ++i) {
        slot = &r->clients[i];
        if (!slot->in_use || slot == old || !slot->vote.enabled)
            continue;
        if (!aggregate.enabled || slot->vote.microvolts > aggregate.microvolts)
            aggregate = slot->vote;
    }
    if (desired.enabled && (!aggregate.enabled || desired.microvolts > aggregate.microvolts))
        aggregate = desired;
    if (!equal(aggregate, r->applied)) {
        /* never claim success from a TCS interrupt alone. The installed provider must make actual
         * rail state readable and stable. */

        if (!b->provider.apply(b->provider.ctx, rail, aggregate) ||
            !read_verified(b, rail, aggregate) || !b->provider.owns(b->provider.ctx, rail)) {
            if (!b->provider.owns(b->provider.ctx, rail) ||
                !b->provider.apply(b->provider.ctx, rail, r->applied) ||
                !read_verified(b, rail, r->applied)) {
                r->state_known = 0;
                result = RAIL_ROLLBACK_FAILED;
            } else
                result = RAIL_HARDWARE_FAILURE;
            goto done;
        }
    }
    if (desired.enabled) {
        if (!old)
            old = free_slot;
        old->in_use = 1;
        old->id = client;
        old->vote = desired;
    } else
        memset(old, 0, sizeof(*old));
    r->applied = aggregate;
    result = RAIL_OK;
done:
    b->provider.unlock(b->provider.ctx);
    return result;
}

/* single-client inspection for graph snapshot/verification. Caller must separately
 * serialize the graph transaction with all clients. */

rail_result_t rail_inspect_client(rail_bank_t* b, uint32_t rail, uint64_t client,
                                  rail_state_t* vote, uint8_t* found)
{
    rail_result_t result = RAIL_INVALID;
    rail_bank_entry_t* r;
    unsigned i;
    int idx = idx_of(rail);
    if (vote)
        memset(vote, 0, sizeof(*vote));
    if (found)
        *found = 0;
    if (!usable(b) || idx < 0 || !client || !vote || !found)
        return result;
    if (!b->provider.lock(b->provider.ctx))
        return RAIL_NO_OWNERSHIP;
    r = &b->rails[idx];
    if (!r->adopted || !r->state_known) {
        result = RAIL_STATE_UNKNOWN;
        goto done;
    }
    if (!b->provider.owns(b->provider.ctx, rail)) {
        r->state_known = 0;
        result = RAIL_NO_OWNERSHIP;
        goto done;
    }
    if (!read_verified(b, rail, r->applied)) {
        r->state_known = 0;
        result = RAIL_STATE_UNKNOWN;
        goto done;
    }
    for (i = 0; i < RAIL_MAX_CLIENTS; ++i) {
        if (r->clients[i].in_use && r->clients[i].id == client) {
            *vote = r->clients[i].vote;
            *found = 1;
            break;
        }
    }
    result = RAIL_OK;
done:
    b->provider.unlock(b->provider.ctx);
    return result;
}
