#include "rpmh_vote_bridge.h"
#include <stdint.h>
#include <string.h>

static rpmh_res_route_t* find(rpmh_vote_bridge_t* b, uint32_t id)
{
    uint32_t i;
    if (!b || !id)
        return 0;
    for (i = 0; i < b->route_cnt; ++i)
        if (b->routes[i].res_id == id)
            return &b->routes[i];
    return 0;
}

int rpmh_bridge_add(rpmh_vote_bridge_t* b, uint32_t id, uint32_t cmd_db_address)
{
    rpmh_res_route_t* r;
    int ok = 0;
    if (!b || !b->transport || !b->lock || !b->unlock || !id || (cmd_db_address & ~0x7ffffu) ||
        b->active_tcs >= 3u)
        return 0;
    if (!b->lock(b->lock_ctx))
        return 0;
    if (b->routes_sealed || b->route_cnt >= RPMH_MAX_ROUTES || find(b, id))
        goto done;
    r = &b->routes[b->route_cnt++];
    memset(r, 0, sizeof(*r));
    r->res_id = id;
    r->cmd_db_address = cmd_db_address;
    ok = 1;
done:
    b->unlock(b->lock_ctx);
    return ok;
}

int rpmh_bridge_seal(rpmh_vote_bridge_t* b)
{
    int ok = 0;
    if (!b || !b->transport || !b->lock || !b->unlock)
        return 0;
    if (!b->lock(b->lock_ctx))
        return 0;
    if (!b->routes_sealed && b->route_cnt && b->route_cnt <= RPMH_MAX_ROUTES &&
        b->active_tcs < 3u) {
        b->routes_sealed = 1;
        ok = 1;
    }
    b->unlock(b->lock_ctx);
    return ok;
}

int rpmh_bridge_add_by_name(rpmh_vote_bridge_t* b, uint32_t id, const uint8_t* database,
                            size_t bytes, const char* name)
{
    cmddb_entry_t entry;
    if (!cmd_db_lookup(database, bytes, name, &entry))
        return 0;
    return rpmh_bridge_add(b, id, entry.address);
}

static int supports(void* ctx, uint32_t id)
{
    rpmh_vote_bridge_t* b = (rpmh_vote_bridge_t*)ctx;
    return b && b->routes_sealed && b->transport && b->active_tcs < 3u &&
           (b->transport->owned_amc_mask & (1u << b->active_tcs)) && find(b, id) != 0;
}

static int apply(void* ctx, uint32_t id, uint64_t value)
{
    rpmh_vote_bridge_t* b = (rpmh_vote_bridge_t*)ctx;
    rpmh_res_route_t* r = find(b, id);
    if (!r || value > UINT32_MAX || !supports(ctx, id))
        return 0;
    r->has_ack = 0;
    if (rpmh_tcs_issue(b->transport, b->active_tcs, r->cmd_db_address, (uint32_t)value, 1u) !=
        TCS_OK)
        return 0;
    r->last_acked_value = (uint32_t)value;
    r->has_ack = 1;
    return 1;
}

static int verify(void* ctx, uint32_t id, uint64_t value)
{
    rpmh_res_route_t* r = find((rpmh_vote_bridge_t*)ctx, id);
    return r && r->has_ack && value <= UINT32_MAX && r->last_acked_value == (uint32_t)value;
}

static int lock(void* ctx)
{
    rpmh_vote_bridge_t* b = (rpmh_vote_bridge_t*)ctx;
    return b && b->lock && b->lock(b->lock_ctx);
}

static void unlock(void* ctx)
{
    rpmh_vote_bridge_t* b = (rpmh_vote_bridge_t*)ctx;
    if (b && b->unlock)
        b->unlock(b->lock_ctx);
}

vote_io_t rpmh_bridge_io(rpmh_vote_bridge_t* bridge)
{
    vote_io_t io;
    memset(&io, 0, sizeof(io));
    if (bridge && bridge->transport && bridge->lock && bridge->unlock &&
        /* a bank lock reused for transport would recursively acquire itself during apply and
         * deadlock in the Windows boot driver. This check catches identical callback/context
         * pairs; providers still must guarantee distinct lock instances and lock ordering. */

        !(bridge->lock == bridge->transport->io.lock &&
          bridge->lock_ctx == bridge->transport->io.ctx)) {
        io.ctx = bridge;
        io.lock = lock;
        io.unlock = unlock;
        io.supports = supports;
        io.apply = apply;
        io.verify = verify;
    }
    return io;
}
