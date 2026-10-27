/* pmic_rail8845.c - Molokai PMIC rail owner over RPMh VRM, portable core (see
 * inc/pmic_rail.h). */

#include "pmic_rail.h"
#include "cmddb.h"
#include <string.h>

static void copy_bounded(char* dst, size_t cap, const char* src)
{
    size_t i = 0;
    if (!dst || !cap)
        return;
    if (src)
        for (; i + 1u < cap && src[i]; ++i)
            dst[i] = src[i];
    dst[i] = 0;
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

int rail_cmd_db_name(const char* ppp_name, char out[RAIL_CMDDB_BYTES], uint32_t* type_arg)
{
    static const char prefix[] = "PPP_RESOURCE_ID_";
    const char* p;
    char kind;
    uint32_t type, number = 0, digits = 0, instance = 0;
    char pmic;
    size_t n = 0;
    if (out)
        out[0] = 0;
    if (type_arg)
        *type_arg = 0;
    if (!ppp_name || !out || strncmp(ppp_name, prefix, sizeof(prefix) - 1u) != 0)
        return 0;
    p = ppp_name + sizeof(prefix) - 1u;
    if (strncmp(p, "LDO", 3) == 0) {
        kind = 'L';
        p += 3;
        type = RAIL_TYPE_LDO;
    } else if (strncmp(p, "SMPS", 4) == 0) {
        kind = 'S';
        p += 4;
        type = RAIL_TYPE_SMPS;
    }

    else if (strncmp(p, "BUCK_BOOST", 10) == 0) {
        kind = 'B';
        p += 10;
        type = RAIL_TYPE_NONE;
    } else if (strncmp(p, "CLK", 3) == 0) {
        kind = 'C';
        p += 3;
        type = RAIL_TYPE_CLOCK;
    } else
        return 0;
    while (is_digit(*p) && digits < 3u) {
        number = number * 10u + (uint32_t)(*p - '0');
        ++p;
        ++digits;
    }
    if (!digits || number == 0u || *p != '_')
        return 0;
    ++p;
    if (!is_upper(*p))
        return 0;
    pmic = *p++;
    if (is_digit(*p)) {
        instance = (uint32_t)(*p - '0');
        ++p;
    }
    if (*p != 0)
        return 0;

    out[n++] = kind;
    if (number >= 100u)
        out[n++] = (char)('0' + number / 100u);
    if (number >= 10u)
        out[n++] = (char)('0' + (number / 10u) % 10u);
    out[n++] = (char)('0' + number % 10u);
    out[n++] = pmic;
    out[n++] = '_';
    out[n++] = 'E';
    out[n++] = (char)('0' + instance);
    out[n] = 0;
    if (type_arg)
        *type_arg = type;
    return 1;
}

int rail_decode_fields(const uint32_t fields[5], rail_vote_t* vote)
{
    if (!fields || !vote)
        return 0;
    memset(vote, 0, sizeof(*vote));

    if (fields[0] == RAIL_TYPE_LDO || fields[0] == RAIL_TYPE_SMPS) {
        if (fields[2] > 1u)
            return 0;
        if (fields[2] && fields[1] == 0u)
            return 0;
        if (fields[1] > 6000000u || fields[3] > 15u || fields[4] > 1000000u)
            return 0;
        vote->type = fields[0];
        vote->microvolts = fields[1];
        vote->enable = fields[2];
        vote->mode = fields[3];
        vote->headroom = fields[4];
        return 1;
    }
    if (fields[0] == RAIL_TYPE_CLOCK) {
        if (fields[1] > 1u || fields[2] || fields[3] || fields[4])
            return 0;
        vote->type = fields[0];
        vote->enable = fields[1];
        return 1;
    }
    return 0;
}

int rail_ensure(rail_owner_t* owner, const char* ppp_name)
{
    rail_t* r;
    char cmddb[RAIL_CMDDB_BYTES];
    uint32_t type;
    cmddb_entry_t e;
    int idx;
    if (!owner || !owner->initialized || !ppp_name || strlen(ppp_name) >= RAIL_NAME_BYTES)
        return -1;
    idx = rail_find(owner, ppp_name);
    if (idx >= 0)
        return idx;
    if (owner->rail_cnt >= RAIL_MAX)
        return -1;
    r = &owner->rails[owner->rail_cnt];
    memset(r, 0, sizeof(*r));
    copy_bounded(r->ppp, RAIL_NAME_BYTES, ppp_name);
    if (rail_cmd_db_name(ppp_name, cmddb, &type)) {
        memcpy(r->cmd_db_name, cmddb, sizeof(cmddb));
        r->type = type;
        if (owner->cmd_db && cmd_db_lookup(owner->cmd_db, owner->cmd_db_bytes, cmddb, &e)) {
            r->address = e.address;
            r->resolved = 1u;
        }
    }
    return (int)owner->rail_cnt++;
}

uint32_t rail_owner_init(rail_owner_t* owner, const uint8_t* cmd_db, size_t cmd_db_bytes,
                         const char* const* ppp_names, uint32_t cnt)
{
    uint32_t i, resolved = 0;
    if (!owner)
        return 0;
    memset(owner, 0, sizeof(*owner));
    owner->cmd_db = cmd_db;
    owner->cmd_db_bytes = cmd_db_bytes;
    owner->initialized = 1u;
    for (i = 0; i < cnt && ppp_names; ++i) {
        int idx = rail_ensure(owner, ppp_names[i]);
        if (idx >= 0 && owner->rails[idx].resolved)
            ++resolved;
    }
    return resolved;
}

int rail_find(const rail_owner_t* owner, const char* ppp_name)
{
    uint32_t i;
    if (!owner || !ppp_name)
        return -1;
    for (i = 0; i < owner->rail_cnt; ++i)
        if (strncmp(owner->rails[i].ppp, ppp_name, RAIL_NAME_BYTES) == 0)
            return (int)i;
    return -1;
}

int rail_supported(const rail_owner_t* owner, const char* ppp_name)
{
    int i = rail_find(owner, ppp_name);
    return i >= 0 && owner->initialized && owner->rails[i].resolved;
}

static rail_client_t* find_client(rail_owner_t* o, const char* path, uint32_t rail, int create)
{
    uint32_t i;
    rail_client_t* free_slot = 0;
    for (i = 0; i < RAIL_MAX_CLIENTS; ++i) {
        rail_client_t* c = &o->clients[i];
        if (!c->in_use) {
            if (!free_slot)
                free_slot = c;
            continue;
        }
        if (c->rail_idx == rail && strncmp(c->dev_path, path, RAIL_PATH_BYTES) == 0)
            return c;
    }
    if (!create || !free_slot)
        return 0;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = 1u;
    free_slot->rail_idx = rail;
    copy_bounded(free_slot->dev_path, RAIL_PATH_BYTES, path);
    return free_slot;
}

int rail_set_client_vote(rail_owner_t* owner, const char* dev_path, const char* ppp_name,
                         const rail_vote_t* vote)
{
    int idx;
    rail_client_t* c;
    if (!owner || !owner->initialized || !dev_path || !dev_path[0])
        return 0;
    idx = rail_find(owner, ppp_name);
    if (idx < 0 || !owner->rails[idx].resolved)
        return 0;
    if (!vote) {
        c = find_client(owner, dev_path, (uint32_t)idx, 0);
        if (c)
            memset(c, 0, sizeof(*c));
        return 1;
    }
    if (vote->type != owner->rails[idx].type)
        return 0;
    c = find_client(owner, dev_path, (uint32_t)idx, 1);
    if (!c)
        return 0;
    c->vote = *vote;
    return 1;
}

int rail_get_client_vote(const rail_owner_t* owner, const char* dev_path, const char* ppp_name,
                         rail_vote_t* vote)
{
    int idx;
    rail_client_t* c;
    if (vote)
        memset(vote, 0, sizeof(*vote));
    if (!owner || !dev_path)
        return 0;
    idx = rail_find(owner, ppp_name);
    if (idx < 0)
        return 0;
    c = find_client((rail_owner_t*)owner, dev_path, (uint32_t)idx, 0);
    if (!c)
        return 0;
    if (vote)
        *vote = c->vote;
    return 1;
}

int rail_aggregate(const rail_owner_t* owner, uint32_t rail_idx, rail_vote_t* aggregate)
{
    uint32_t i;
    if (!aggregate)
        return 0;
    memset(aggregate, 0, sizeof(*aggregate));
    if (!owner || rail_idx >= owner->rail_cnt)
        return 0;
    aggregate->type = owner->rails[rail_idx].type;
    for (i = 0; i < RAIL_MAX_CLIENTS; ++i) {
        const rail_client_t* c = &owner->clients[i];
        if (!c->in_use || c->rail_idx != rail_idx || !c->vote.enable)
            continue;
        aggregate->enable = 1u;
        if (c->vote.microvolts > aggregate->microvolts)
            aggregate->microvolts = c->vote.microvolts;
        if (c->vote.mode > aggregate->mode)
            aggregate->mode = c->vote.mode;
        if (c->vote.headroom > aggregate->headroom)
            aggregate->headroom = c->vote.headroom;
    }
    return 1;
}

int rail_program(const rail_owner_t* owner, uint32_t rail_idx, const rail_vote_t* aggregate,
                 tcs_command_t out[3], uint32_t* cnt)
{
    const rail_t* r;
    uint32_t n = 0;
    if (cnt)
        *cnt = 0;
    if (!owner || !aggregate || !out || !cnt || rail_idx >= owner->rail_cnt)
        return 0;
    r = &owner->rails[rail_idx];
    if (!r->resolved || aggregate->type != r->type)
        return 0;
    /* a rail kind with no observed vote opcode is never programmed. */
    if (r->type != RAIL_TYPE_LDO && r->type != RAIL_TYPE_SMPS && r->type != RAIL_TYPE_CLOCK)
        return 0;
    memset(out, 0, sizeof(tcs_command_t) * 3u);
    if (r->type == RAIL_TYPE_CLOCK) {
        out[n].address = r->address + VRM_ENABLE_OFFSET;
        out[n].data = aggregate->enable ? 1u : 0u;
        ++n;
    } else if (aggregate->enable) {
        if (aggregate->microvolts == 0u)
            return 0;

        /* match the native VRM batch: enable, voltage, mode. The complete command set is staged
         * before its single AMC trigger. */
        out[n].address = r->address + VRM_ENABLE_OFFSET;
        out[n].data = 1u;
        ++n;
        out[n].address = r->address + VRM_VOLTAGE_OFFSET;
        out[n].data = (aggregate->microvolts + 999u) / 1000u;
        ++n;
        out[n].address = r->address + VRM_MODE_OFFSET;
        out[n].data = aggregate->mode;
        ++n;
    } else {
        /* a disable vote never writes voltage 0: it only drops the enable. */
        out[n].address = r->address + VRM_ENABLE_OFFSET;
        out[n].data = 0u;
        ++n;
    }
    *cnt = n;
    return 1;
}

int rail_read_program(const rail_owner_t* owner, uint32_t rail_idx, tcs_command_t out[3],
                      uint32_t* cnt)
{
    const rail_t* r;
    if (cnt)
        *cnt = 0;
    if (!owner || !out || !cnt || rail_idx >= owner->rail_cnt)
        return 0;
    r = &owner->rails[rail_idx];
    if (!r->resolved)
        return 0;
    memset(out, 0, sizeof(tcs_command_t) * 3u);
    if (r->type == RAIL_TYPE_CLOCK) {
        out[0].address = r->address + VRM_ENABLE_OFFSET;
        out[0].read = 1u;
        *cnt = 1u;
        return 1;
    }
    out[0].address = r->address + VRM_VOLTAGE_OFFSET;
    out[0].read = 1u;
    out[1].address = r->address + VRM_ENABLE_OFFSET;
    out[1].read = 1u;
    out[2].address = r->address + VRM_MODE_OFFSET;
    out[2].read = 1u;
    *cnt = 3u;
    return 1;
}

uint64_t rail_pack_state(uint32_t millivolts, uint32_t enable, uint32_t mode)
{
    return (uint64_t)(millivolts & 0xFFFFFu) | ((uint64_t)(enable & 0xFu) << 20) |
           ((uint64_t)(mode & 0xFFu) << 24) | ((uint64_t)1u << 63);
}
