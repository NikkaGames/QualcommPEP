#include "vote.h"
#include <stdint.h>
#include <string.h>

vote_result_t vote_add_res(vote_bank_t* b, uint32_t id, vote_aggregation_t mode, uint64_t initial)
{
    uint32_t i;
    vote_res_t* r;
    vote_result_t result = VOTE_BAD_REQ;
    if (!b || !id || mode < VOTE_MAX || mode > VOTE_BOOLEAN_OR ||
        (mode == VOTE_BOOLEAN_OR && initial > 1u) || !b->io.lock || !b->io.unlock || !b->io.apply ||
        !b->io.verify || !b->io.supports)
        return result;
    /* registration races with reg AND client changes. in particular, ResourceCount, duplicate
     * IDs and the ownership check belong INSIDE the common bank lock, not before it. */

    if (!b->io.lock(b->io.ctx))
        return VOTE_UNSUPPORTED;
    if (b->res_cnt >= MAX_VOTE_RESOURCES)
        goto done;
    for (i = 0; i < b->res_cnt; ++i)
        if (b->resources[i].id == id)
            goto done;
    if (!b->io.supports(b->io.ctx, id)) {
        result = VOTE_UNSUPPORTED;
        goto done;
    }

    /* reserve a slot before doing the first hardware write. */

    r = &b->resources[b->res_cnt++];
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->aggregation = mode;
    r->base_vote = initial;
    if (!b->io.apply(b->io.ctx, id, initial) || !b->io.verify(b->io.ctx, id, initial)) {
        result = VOTE_HARDWARE_FAILURE;
        goto done;
    }
    r->applied = initial;
    r->known = 1;
    result = VOTE_OK;
done:
    b->io.unlock(b->io.ctx);
    return result;
}

vote_result_t vote_adopt_res(vote_bank_t* b, uint32_t id, vote_aggregation_t mode,
                             uint64_t expected_current)
{
    uint32_t i;
    uint64_t read1 = 0, read2 = 0;
    vote_result_t result = VOTE_BAD_REQ;
    vote_res_t* r;
    if (!b || !id || mode < VOTE_MAX || mode > VOTE_BOOLEAN_OR ||
        (mode == VOTE_BOOLEAN_OR && expected_current > 1u) || !b->io.lock || !b->io.unlock ||
        !b->io.supports || !b->io.apply || !b->io.verify || !b->io.owns || !b->io.read_current)
        return result;
    if (!b->io.lock(b->io.ctx))
        return VOTE_UNSUPPORTED;
    if (b->res_cnt >= MAX_VOTE_RESOURCES) {
        result = VOTE_NO_CAPACITY;
        goto done;
    }
    for (i = 0; i < b->res_cnt; ++i)
        if (b->resources[i].id == id)
            goto done;
    if (!b->io.supports(b->io.ctx, id) || !b->io.owns(b->io.ctx, id)) {
        result = VOTE_UNSUPPORTED;
        goto done;
    }
    /* a stale cached value is not evidence of a hardware baseline. Do not register a res or
     * change the voltage when readback is uncertain. */

    if (!b->io.read_current(b->io.ctx, id, &read1) || !b->io.read_current(b->io.ctx, id, &read2) ||
        read1 != read2 || read1 != expected_current) {
        result = VOTE_HARDWARE_FAILURE;
        goto done;
    }
    r = &b->resources[b->res_cnt++];
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->aggregation = mode;
    r->applied = expected_current;
    r->base_vote = expected_current;
    r->known = 1;
    r->requires_readback = 1;
    result = VOTE_OK;
done:
    b->io.unlock(b->io.ctx);
    return result;
}

vote_result_t vote_change(vote_bank_t* b, uint64_t client, uint32_t id, uint64_t value, int remove)
{
    vote_result_t result = VOTE_BAD_REQ;
    vote_res_t* r = 0;
    vote_client_t *slot = 0, *free_slot = 0;
    uint32_t i;
    uint64_t aggregate;
    if (!b || !client || !id || (remove != 0 && remove != 1) || !b->io.lock || !b->io.unlock ||
        !b->io.apply || !b->io.verify || !b->io.supports)
        return result;
    if (!b->io.lock(b->io.ctx))
        return VOTE_UNSUPPORTED;
    for (i = 0; i < b->res_cnt; ++i)
        if (b->resources[i].id == id) {
            r = &b->resources[i];
            break;
        }
    if (!r)
        goto done;
    if (!r->known) {
        result = VOTE_STATE_UNKNOWN;
        goto done;
    }

    if (!remove && r->aggregation == VOTE_BOOLEAN_OR && value > 1u)
        goto done;
    if (!b->io.supports(b->io.ctx, id)) {
        result = VOTE_UNSUPPORTED;
        goto done;
    }
    /* before every client operation, including no-op votes, require exclusive ownership and
     * two matching authoritative provider readbacks of the last applied aggregate. */

    if (r->requires_readback) {
        uint64_t a = 0, c = 0;
        if (!b->io.owns || !b->io.read_current || !b->io.owns(b->io.ctx, id)) {
            r->known = 0;
            result = VOTE_UNSUPPORTED;
            goto done;
        }
        if (!b->io.read_current(b->io.ctx, id, &a) || !b->io.read_current(b->io.ctx, id, &c) ||
            a != c || a != r->applied) {
            r->known = 0;
            result = VOTE_STATE_UNKNOWN;
            goto done;
        }
    }
    for (i = 0; i < MAX_VOTE_CLIENTS; ++i) {
        vote_client_t* c = &b->clients[i];
        if (!c->in_use) {
            if (!free_slot)
                free_slot = c;
            continue;
        }
        if (c->id == id && c->client == client) {
            slot = c;
            break;
        }
    }
    /* an unknown client owns no vote. */

    if (remove && !slot)
        goto done;
    if (!slot && !remove && !free_slot) {
        result = VOTE_NO_CAPACITY;
        goto done;
    }

    /* preserve the initial owner's confirmed vote as an implicit client. this prevents the
     * first/newest explicit client from accidentally canceling a boot-time baseline vote. */

    aggregate = r->base_vote;
    for (i = 0; i < MAX_VOTE_CLIENTS; ++i) {
        const vote_client_t* c = &b->clients[i];
        uint64_t v;
        if (!c->in_use || c->id != id || c == slot)
            continue;
        v = c->value;
        if (r->aggregation == VOTE_MAX) {
            if (v > aggregate)
                aggregate = v;
        } else if (r->aggregation == VOTE_SUM) {
            if (UINT64_MAX - aggregate < v) {
                result = VOTE_OVERFLOW;
                goto done;
            }
            aggregate += v;
        } else if (v)
            aggregate = 1u;
    }
    if (!remove) {
        if (r->aggregation == VOTE_MAX) {
            if (value > aggregate)
                aggregate = value;
        } else if (r->aggregation == VOTE_SUM) {
            if (UINT64_MAX - aggregate < value) {
                result = VOTE_OVERFLOW;
                goto done;
            }
            aggregate += value;
        } else if (value)
            aggregate = 1u;
    }
    if (aggregate != r->applied) {
        int success =
            b->io.apply(b->io.ctx, id, aggregate) && b->io.verify(b->io.ctx, id, aggregate);
        if (success && r->requires_readback) {
            uint64_t a = 0, c = 0;
            success = b->io.owns(b->io.ctx, id) && b->io.read_current(b->io.ctx, id, &a) &&
                      b->io.read_current(b->io.ctx, id, &c) && a == c && a == aggregate;
        }
        if (!success) {
            /* a provider losing ownership during apply must not try to restore the rail, even when
             * the TCS response was successful. */

            if (r->requires_readback && !b->io.owns(b->io.ctx, id)) {
                r->known = 0;
                result = VOTE_ROLLBACK_FAILED;
                goto done;
            }
            /* apply might have partially succeeded: only a verified restore can leave this res
             * usable. otherwise quarantine it. */

            success =
                b->io.apply(b->io.ctx, id, r->applied) && b->io.verify(b->io.ctx, id, r->applied);
            if (success && r->requires_readback) {
                uint64_t a = 0, c = 0;
                success = b->io.owns(b->io.ctx, id) && b->io.read_current(b->io.ctx, id, &a) &&
                          b->io.read_current(b->io.ctx, id, &c) && a == c && a == r->applied;
            }
            if (!success) {
                r->known = 0;
                result = VOTE_ROLLBACK_FAILED;
            } else
                result = VOTE_HARDWARE_FAILURE;
            goto done;
        }
    }
    if (remove) {
        memset(slot, 0, sizeof(*slot));
    } else {
        if (!slot)
            slot = free_slot;
        slot->client = client;
        slot->id = id;
        slot->value = value;
        slot->in_use = 1;
    }
    r->applied = aggregate;
    result = VOTE_OK;
done:
    b->io.unlock(b->io.ctx);
    return result;
}
