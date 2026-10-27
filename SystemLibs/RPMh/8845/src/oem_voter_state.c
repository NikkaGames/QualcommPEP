#include "oem_voter_state.h"
#include <stdint.h>
#include <string.h>

static oem_voter_res_t* res(oem_voter_state_t* v, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < v->res_cnt; ++i)
        if (v->resources[i].key == key)
            return &v->resources[i];
    return 0;
}

static oem_voter_client_t* client(oem_voter_state_t* v, uint64_t id, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < OEM_VOTER_CLIENTS; ++i)
        if (v->clients[i].active && v->clients[i].client == id && v->clients[i].key == key)
            return &v->clients[i];
    return 0;
}

static oem_voter_client_t* free_client(oem_voter_state_t* v)
{
    uint32_t i;
    for (i = 0; i < OEM_VOTER_CLIENTS; ++i)
        if (!v->clients[i].active)
            return &v->clients[i];
    return 0;
}

static int stable(oem_voter_state_t* v, uint32_t key, uint64_t expect)
{
    uint64_t a = 0, b = 0;
    return v->io.owns(v->io.ctx, key) == 1 && v->io.read_current(v->io.ctx, key, &a) == 1 &&
           v->io.read_current(v->io.ctx, key, &b) == 1 && a == b && a == expect &&
           v->io.owns(v->io.ctx, key) == 1;
}

int oem_voter_init(oem_voter_state_t* v, const oem_voter_io_t* ops)
{
    if (!v || !ops || !ops->owner_epoch || !ops->owns || !ops->read_current || !ops->ack_ticket)
        return 0;
    memset(v, 0, sizeof(*v));
    v->io = *ops;
    return 1;
}

int oem_voter_add(oem_voter_state_t* v, uint32_t key, uint32_t predecessor, uint8_t mode,
                  uint64_t baseline)
{
    oem_voter_res_t* r;
    if (!v || !v->io.owns || v->sealed || v->closed || v->faulted || !key || key == predecessor ||
        res(v, key) || v->res_cnt >= OEM_VOTER_RESOURCES || (predecessor && !res(v, predecessor)) ||
        (mode != OEM_VOTER_MAX && mode != OEM_VOTER_OR && mode != OEM_VOTER_SUM) ||
        (mode == OEM_VOTER_OR && baseline > 1u) || !stable(v, key, baseline))
        return 0;
    r = &v->resources[v->res_cnt++];
    r->key = key;
    r->depends_on = predecessor;
    r->mode = mode;
    r->base_vote = baseline;
    r->committed = baseline;
    return 1;
}

int oem_voter_seal(oem_voter_state_t* v)
{
    if (!v || v->sealed || v->closed || v->faulted || !v->res_cnt)
        return 0;
    v->sealed = 1;
    return 1;
}

static int aggregate(oem_voter_state_t* v, const oem_voter_res_t* r, uint64_t id, uint64_t value,
                     uint8_t remove, uint64_t* output)
{
    uint32_t i;
    uint64_t sum = r->base_vote;
    for (i = 0; i < OEM_VOTER_CLIENTS; ++i) {
        const oem_voter_client_t* c = &v->clients[i];
        uint64_t val;
        if (!c->active || c->key != r->key || c->client == id)
            continue;
        val = c->value;
        if (r->mode == OEM_VOTER_MAX) {
            if (val > sum)
                sum = val;
        } else if (r->mode == OEM_VOTER_SUM) {
            if (UINT64_MAX - sum < val)
                return 0;
            sum += val;
        } else if (val)
            sum = 1u;
    }
    if (!remove) {
        if (r->mode == OEM_VOTER_MAX) {
            if (value > sum)
                sum = value;
        } else if (r->mode == OEM_VOTER_SUM) {
            if (UINT64_MAX - sum < value)
                return 0;
            sum += value;
        } else if (value)
            sum = 1u;
    }
    *output = sum;
    return 1;
}

static void apply_client(oem_voter_state_t* v, const oem_voter_pending_t* p)
{
    oem_voter_client_t* c = client(v, p->client, p->key);
    if (p->remove) {
        if (c)
            memset(c, 0, sizeof(*c));
    } else {
        if (!c)
            c = free_client(v);
        if (c) {
            c->active = 1;
            c->client = p->client;
            c->key = p->key;
            c->value = p->value;
        }
    }
}

int oem_voter_begin(oem_voter_state_t* v, uint64_t id, uint32_t key, uint64_t value, uint8_t remove,
                    uint64_t* ticket, uint64_t* target)
{
    oem_voter_res_t* r;
    oem_voter_client_t* c;
    oem_voter_pending_t p = {0};
    uint64_t proposed;
    if (!v || !v->sealed || v->closed || v->faulted || v->pending.ticket || v->sleep_guard_token ||
        !ticket || !target || !id || !key || remove > 1)
        return 0;
    *ticket = 0;
    *target = 0;
    r = res(v, key);
    if (!r)
        return 0;
    /* an independent voter cannot authorize a child of an unowned rail. */

    if (oem_voter_ready(v, key, 1u, 0u) != 1)
        return 0;
    c = client(v, id, key);
    if ((remove && !c) || (!remove && !c && !free_client(v)) ||
        (!remove && r->mode == OEM_VOTER_OR && value > 1u))
        return 0;
    if (!aggregate(v, r, id, value, remove, &proposed))
        return 0;
    p.client = id;
    p.key = key;
    p.value = value;
    p.remove = remove;
    p.target = proposed;
    if (proposed == r->committed) {
        apply_client(v, &p);
        *target = proposed;
        return OEM_VOTER_NO_WRITE;
    }

    if (v->last_sequence == UINT32_MAX) {
        v->faulted = 1;
        return 0;
    }
    p.ticket = ((uint64_t)v->io.owner_epoch << 32) | (++v->last_sequence);
    v->pending = p;
    *ticket = p.ticket;
    *target = proposed;
    return 1;
}

int oem_voter_mark_issued(oem_voter_state_t* v, uint64_t ticket)
{
    if (!v || !ticket || v->faulted || v->pending.ticket != ticket || v->pending.issued)
        return 0;
    v->pending.issued = 1;
    return 1;
}

int oem_voter_finish(oem_voter_state_t* v, uint64_t ticket)
{
    oem_voter_res_t* r;
    if (!v || !ticket || v->faulted || v->pending.ticket != ticket || !v->pending.issued)
        return 0;
    r = res(v, v->pending.key);
    if (!r || v->io.ack_ticket(v->io.ctx, ticket, r->key, v->pending.target) != 1 ||
        !stable(v, r->key, v->pending.target)) {
        v->faulted = 1;
        return 0;
    }
    apply_client(v, &v->pending);
    r->committed = v->pending.target;
    memset(&v->pending, 0, sizeof(v->pending));
    return 1;
}

int oem_voter_cancel_unissued(oem_voter_state_t* v, uint64_t ticket)
{
    if (!v || !ticket || v->faulted || v->pending.ticket != ticket || v->pending.issued)
        return 0;
    memset(&v->pending, 0, sizeof(v->pending));
    return 1;
}

int oem_voter_ready(void* ctx, uint32_t key, uint32_t req_id, uint8_t is_read)
{
    oem_voter_state_t* v = ctx;
    oem_voter_res_t* r;
    uint32_t depth = 0;
    if (!v || !v->sealed || v->closed || v->faulted || !key || !req_id || is_read > 1)
        return -1;
    if (v->sleep_guard_token)
        return 0;
    r = res(v, key);
    if (!r)
        return -1;
    while (r && depth++ < OEM_VOTER_RESOURCES) {
        if (v->pending.ticket && v->pending.key == r->key)
            return 0;
        if (!stable(v, r->key, r->committed)) {
            v->faulted = 1;
            return -1;
        }
        r = r->depends_on ? res(v, r->depends_on) : 0;
    }
    if (r) {
        v->faulted = 1;
        return -1;
    }
    return 1;
}

int oem_voter_enter_sleep(oem_voter_state_t* v, uint64_t token)
{
    uint32_t i;
    if (!v || !v->sealed || v->closed || v->faulted || v->sleep_guard_token || v->pending.ticket ||
        !token || (uint32_t)(token >> 32) != v->io.owner_epoch)
        return 0;
    for (i = 0; i < v->res_cnt; ++i)
        if (oem_voter_ready(v, v->resources[i].key, 1u, 0u) != 1)
            return 0;
    v->sleep_guard_token = token;
    return 1;
}

int oem_voter_exit_sleep(oem_voter_state_t* v, uint64_t token)
{
    uint32_t i;
    if (!v || !token || v->faulted || v->sleep_guard_token != token || v->pending.ticket)
        return 0;
    /* the bridge must separately prove wake happened. */

    for (i = 0; i < v->res_cnt; ++i)
        if (!stable(v, v->resources[i].key, v->resources[i].committed)) {
            v->faulted = 1;
            return 0;
        }
    v->sleep_guard_token = 0;
    return 1;
}

int oem_voter_cancel_unstarted_sleep(oem_voter_state_t* v, uint64_t token)
{
    if (!v || !token || v->faulted || v->sleep_guard_token != token || v->pending.ticket)
        return 0;
    v->sleep_guard_token = 0;
    return 1;
}

void oem_voter_close(oem_voter_state_t* v)
{
    if (v)
        v->closed = 1;
}

int oem_voter_can_release(const oem_voter_state_t* v)
{
    return v && v->closed && !v->faulted && !v->pending.ticket && !v->sleep_guard_token;
}
