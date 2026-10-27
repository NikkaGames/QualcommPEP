#include "resolve.h"
#include "cmddb.h"
#include <string.h>

/* all accesses require external serialization; no hardware rights inferred. */
static const oem_cmddb_res_t* find_res(const oem_cmddb_resolver_t* r, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < r->resources_cnt; ++i)
        if (r->resources[i].key == key)
            return &r->resources[i];
    return 0;
}

static oem_cmddb_req_t* find_req(oem_cmddb_resolver_t* r, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < OEM_CMDDB_REQUESTS; ++i)
        if (r->requests[i].token && r->requests[i].req_id == id)
            return &r->requests[i];
    return 0;
}

int oem_cmd_db_resolver_init(oem_cmddb_resolver_t* r,
                             int (*voter)(void*, uint32_t, uint32_t, uint8_t), void* ctx)
{
    if (!r || !voter)
        return 0;
    memset(r, 0, sizeof(*r));
    r->voter_ready = voter;
    r->voter_ctx = ctx;
    return 1;
}

int oem_cmd_db_add_res(oem_cmddb_resolver_t* r, const uint8_t* db, size_t db_bytes,
                       const char* name, uint32_t key, uint32_t last_offset, uint8_t allow_read,
                       uint8_t allow_write)
{
    cmddb_entry_t e = {0};
    uint32_t i, lo, hi;
    if (!r || r->closed || r->sealed || r->faulted || !r->voter_ready || !key ||
        r->resources_cnt >= OEM_CMDDB_RESOURCES || (last_offset & 3u) || last_offset > 0xffcu ||
        allow_read > 1 || allow_write > 1 || (!allow_read && !allow_write) ||
        !cmd_db_lookup(db, db_bytes, name, &e) || (e.address & 3u) ||
        e.address > 0x7ffffu - last_offset ||
        ((e.address >> 16) & 7u) != ((e.address + last_offset) >> 16 & 7u))
        return 0;
    lo = e.address;
    hi = e.address + last_offset;
    for (i = 0; i < r->resources_cnt; ++i) {
        const oem_cmddb_res_t* v = &r->resources[i];
        if (v->key == key || (lo <= v->address + v->last_offset && v->address <= hi))
            return 0;
    }
    r->resources[r->resources_cnt].key = key;
    r->resources[r->resources_cnt].address = lo;
    r->resources[r->resources_cnt].last_offset = last_offset;
    r->resources[r->resources_cnt].allow_read = allow_read;
    r->resources[r->resources_cnt].allow_write = allow_write;
    ++r->resources_cnt;
    return 1;
}

int oem_cmd_db_seal(oem_cmddb_resolver_t* r)
{
    if (!r || r->closed || r->faulted || r->sealed || !r->resources_cnt || !r->voter_ready)
        return 0;
    r->sealed = 1;
    return 1;
}

int oem_cmd_db_reg(oem_cmddb_resolver_t* r, uint64_t token, uint32_t id, uint32_t key,
                   uint8_t is_read, const oem_cmddb_offset_cmd_t* cmds, uint32_t cnt)
{
    const oem_cmddb_res_t* res;
    oem_cmddb_req_t* req = 0;
    uint32_t i, reads = 0;
    if (!r || !r->sealed || r->closed || r->faulted || !r->voter_ready || !token || !id || !key ||
        is_read > 1 || !cmds || !cnt || cnt > OEM_CMDDB_CMDS || find_req(r, id))
        return 0;
    res = find_res(r, key);
    if (!res)
        return 0;
    for (i = 0; i < OEM_CMDDB_REQUESTS; ++i) {
        if (r->requests[i].token == token)
            return 0;
        if (!r->requests[i].token && !req)
            req = &r->requests[i];
    }
    if (!req)
        return 0;
    for (i = 0; i < cnt; ++i) {
        if (cmds[i].read > 1 || (cmds[i].offset & 3u) || cmds[i].offset > res->last_offset ||
            (cmds[i].read && (!res->allow_read || cmds[i].data)) ||
            (!cmds[i].read && !res->allow_write))
            return 0;
        reads += cmds[i].read;
    }
    if ((is_read && !reads) || (!is_read && reads))
        return 0;

    memset(req, 0, sizeof(*req));
    req->token = token;
    req->req_id = id;
    req->key = key;
    req->is_read = is_read;
    req->cnt = (uint8_t)cnt;
    for (i = 0; i < cnt; ++i) {
        req->commands[i].address = res->address + cmds[i].offset;
        req->commands[i].data = cmds[i].read ? 0u : cmds[i].data;
        req->commands[i].read = cmds[i].read;
    }
    return 1;
}

int oem_cmd_db_res_ready(void* opaque, uint32_t key, uint32_t id, uint8_t is_read)
{
    oem_cmddb_resolver_t* r = opaque;
    oem_cmddb_req_t* req;
    int state;
    if (!r || !r->sealed || r->closed || r->faulted || !r->voter_ready || !key || !id ||
        is_read > 1)
        return -1;
    req = find_req(r, id);
    if (!req || req->key != key || req->is_read != is_read || req->claimed)
        return -1;
    req->ready_approved = 0;
    state = r->voter_ready(r->voter_ctx, key, id, is_read);
    if (state < 0) {
        r->faulted = 1;
        return -1;
    }
    if (state != 1)
        return 0;
    req->ready_approved = 1;
    return 1;
}

int oem_cmd_db_resolve_batch(void* opaque, uint64_t token, uint32_t id, uint32_t key,
                             uint8_t is_read, tcs_command_t* out, uint32_t* cnt)
{
    oem_cmddb_resolver_t* r = opaque;
    oem_cmddb_req_t* req;
    int state;
    if (!r || !r->sealed || r->closed || r->faulted || !r->voter_ready || !out || !cnt || !token ||
        !id || !key || is_read > 1)
        return 0;
    req = find_req(r, id);
    if (!req || req->token != token || req->key != key || req->is_read != is_read ||
        !req->ready_approved || req->claimed || *cnt < req->cnt)
        return 0;
    req->ready_approved = 0;
    state = r->voter_ready(r->voter_ctx, key, id, is_read);
    if (state < 0) {
        r->faulted = 1;
        return 0;
    }
    if (state != 1)
        return 0;
    req->claimed = 1;
    memcpy(out, req->commands, req->cnt * sizeof(*out));
    *cnt = req->cnt;
    return 1;
}

int oem_cmd_db_retire_after_tcs(oem_cmddb_resolver_t* r, oem_tcs_batch_t* batch, uint8_t slot,
                                uint64_t token)
{
    uint32_t i;
    if (!r || !batch || r->faulted || !r->sealed || !token || slot >= OEM_TCS_BATCH_SLOTS)
        return 0;
    for (i = 0; i < OEM_CMDDB_REQUESTS; ++i) {
        oem_cmddb_req_t* req = &r->requests[i];
        if (req->token != token)
            continue;
        if (!req->claimed || batch->slots[slot].token != token ||
            batch->slots[slot].req_id != req->req_id || batch->slots[slot].res_key != req->key)
            return 0;
        if (!oem_tcs_batch_retire(batch, slot, token)) {
            /* do not free a request after ambiguous TCS cleanup. */
            r->faulted = 1;
            return 0;
        }
        memset(req, 0, sizeof(*req));
        return 1;
    }
    return 0;
}

int oem_cmd_db_cancel_unclaimed(oem_cmddb_resolver_t* r, uint64_t token)
{
    uint32_t i;
    if (!r || r->faulted || !token)
        return 0;
    for (i = 0; i < OEM_CMDDB_REQUESTS; ++i) {
        oem_cmddb_req_t* req = &r->requests[i];
        if (req->token == token && !req->claimed) {
            memset(req, 0, sizeof(*req));
            return 1;
        }
    }
    return 0;
}

void oem_cmd_db_close(oem_cmddb_resolver_t* r)
{
    if (r)
        r->closed = 1;
}

int oem_cmd_db_can_release(const oem_cmddb_resolver_t* r)
{
    uint32_t i;
    if (!r || !r->closed || r->faulted)
        return 0;
    for (i = 0; i < OEM_CMDDB_REQUESTS; ++i)
        if (r->requests[i].token)
            return 0;
    return 1;
}
