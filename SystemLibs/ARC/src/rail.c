#include "arc_rail.h"
#include <string.h>

static int live(const arc_rail_t* r)
{
    return r && r->bound && !r->quarantined && r->epoch && r->io.owns &&
           r->io.owns(r->io.ctx, r->epoch);
}

static int physical(arc_rail_t* r, uint32_t* corner)
{
    uint32_t a = 0, b = 0;
    if (!live(r) || !corner || !r->io.read(r->io.ctx, r->address, &a) ||
        !r->io.read(r->io.ctx, r->address, &b) || a >= r->level_cnt || b >= r->level_cnt ||
        !live(r))
        return 0;
    /* other ARC consumers can change the aggregate between completed reads. both samples must
     * support a requested floor; equality is not a controller protocol requirement. */
    *corner = a < b ? a : b;
    return 1;
}

int arc_map(const arc_rail_t* r, uint32_t level, uint32_t* corner)
{
    uint32_t i;
    if (!r || !corner || !r->bound || r->quarantined || r->level_cnt < 2u ||
        r->level_cnt > ARC_MAX_LEVELS)
        return 0;
    for (i = 0u; i < r->level_cnt; ++i)
        if (r->levels[i] >= level) {
            *corner = i;
            return 1;
        }
    return 0;
}

int arc_bind_ex(arc_rail_t* r, const uint8_t* db, size_t bytes, const char* name,
                const arc_io_t* io, uint64_t epoch, arc_bind_diag_t* diag)
{
    cmddb_entry_t entry;
    uint16_t levels[ARC_MAX_LEVELS] = {0};
    uint32_t i, cnt, corner, second, level_cnt = 0;
    int padding = 0;
    size_t n = 0;
    if (diag) {
        memset(diag, 0, sizeof(*diag));
        diag->phase = 1u;
    }
    if (!r || r->bound || r->quarantined || !io || !io->owns || !io->read || !io->write || !epoch ||
        !name)
        return 0;
    if (diag)
        diag->phase = 2u;
    if (!cmd_db_lookup(db, bytes, name, &entry))
        return 0;
    if (diag) {
        diag->phase = 3u;
        diag->address = entry.address;
        diag->aux_length = entry.aux_length;
    }
    if ((entry.address >> 16) != 3u || (entry.address & 3u) || !entry.aux_data ||
        entry.aux_length < 4u || (entry.aux_length & 1u) || entry.aux_length > 2u * ARC_MAX_LEVELS)
        return 0;
    while (n < 9u && name[n])
        ++n;
    if (!n || n > 8u)
        return 0;
    cnt = entry.aux_length / 2u;
    if (diag)
        diag->phase = 4u;
    for (i = 0u; i < cnt; ++i) {
        uint16_t level = (uint16_t)((uint16_t)entry.aux_data[i * 2u] |
                                    ((uint16_t)entry.aux_data[i * 2u + 1u] << 8));
        if (diag)
            diag->level_idx = i;
        if (!i && level)
            return 0;
        if (i && !level)
            padding = 1;
        else {
            if (padding || (i && level <= levels[i - 1u]))
                return 0;
            levels[i] = level;
            level_cnt = i + 1u;
        }
    }
    if (level_cnt < 2u)
        return 0;
    memset(r, 0, sizeof(*r));
    memcpy(r->name, name, n + 1u);
    memcpy(r->levels, levels, sizeof(levels));
    r->level_cnt = level_cnt;
    r->address = entry.address;
    r->io = *io;
    r->epoch = epoch;
    r->bound = 1u;
    if (diag) {
        diag->level_cnt = level_cnt;
        diag->phase = 5u;
    }
    if (!live(r) || !io->read(io->ctx, r->address, &corner))
        goto no_read;
    if (diag) {
        diag->raw[0] = corner;
        diag->phase = 6u;
    }
    if (!io->read(io->ctx, r->address, &second))
        goto no_read;
    if (diag) {
        diag->raw[1] = second;
        diag->phase = 7u;
    }
    if (corner >= level_cnt || second >= level_cnt || !live(r))
        goto no_read;
    /* preserve the higher inherited requirement when the aggregate moves during binding.
     * Neither observation authorizes a vote or an ACK. */
    r->boot_floor = corner > second ? corner : second;
    if (diag)
        diag->phase = 0u;
    return 1;
no_read:
    memset(r, 0, sizeof(*r));
    return 0;
}

int arc_bind(arc_rail_t* r, const uint8_t* db, size_t bytes, const char* name, const arc_io_t* io,
             uint64_t epoch)
{
    return arc_bind_ex(r, db, bytes, name, io, epoch, NULL);
}

int arc_read(arc_rail_t* r, uint32_t* enabled, uint32_t* level)
{
    uint32_t corner;
    if (!enabled || !level || !physical(r, &corner))
        return 0;
    *enabled = corner != 0u;
    *level = r->levels[corner];
    return 1;
}

static int client_idx(const arc_rail_t* r, const char* identity)
{
    uint32_t i;
    size_t n = 0;
    if (!identity)
        return -2;
    while (n < 256u && identity[n])
        ++n;
    if (!n || n >= 256u)
        return -2;
    for (i = 0u; i < ARC_CLIENTS; ++i)
        if (r->clients[i].used && !strcmp(r->clients[i].identity, identity))
            return (int)i;
    return -1;
}

int arc_client_vote(const arc_rail_t* r, const char* client, uint32_t* enabled, uint32_t* level)
{
    int idx;
    uint32_t corner;
    if (!live(r) || !enabled || !level || (idx = client_idx(r, client)) < -1)
        return 0;
    corner = idx < 0 ? 0u : r->clients[idx].corner;
    if (corner >= r->level_cnt)
        return 0;
    *enabled = corner != 0u;
    *level = r->levels[corner];
    return 1;
}

int arc_vote(arc_rail_t* r, const char* client, uint32_t level, uint32_t enabled)
{
    uint32_t desired = 0, target, i, observed;
    int idx, result;
    if (!live(r) || enabled > 1u || (idx = client_idx(r, client)) < -1 ||
        !arc_map(r, enabled ? level : 0u, &desired) || (enabled && !desired))
        return 0;
    if (idx < 0) {
        if (!enabled)
            return 1;
        for (i = 0u; i < ARC_CLIENTS; ++i)
            if (!r->clients[i].used) {
                idx = (int)i;
                break;
            }
        if (idx < 0)
            return 0;
    }
    target = r->boot_floor;
    for (i = 0u; i < ARC_CLIENTS; ++i) {
        uint32_t vote =
            (i == (uint32_t)idx) ? desired : (r->clients[i].used ? r->clients[i].corner : 0u);
        if (vote >= r->level_cnt)
            return 0;
        if (vote > target)
            target = vote;
    }
    /* the exact transport ACK and independent ARC status, not this client's bookkeeping or a
     * firmware aggregate, establish a successful vote. */
    result = r->io.write(r->io.ctx, r->address, target);
    if (result != 1) {
        if (result < 0)
            r->quarantined = 1u;
        return 0;
    }
    r->floor_published = 1u;
    if (!physical(r, &observed) || observed < target) {
        r->quarantined = 1u;
        return 0;
    }
    if (!desired) {
        memset(&r->clients[idx], 0, sizeof(r->clients[idx]));
        return 1;
    }
    if (!r->clients[idx].used) {
        memcpy(r->clients[idx].identity, client, strlen(client) + 1u);
        r->clients[idx].used = 1u;
    }
    r->clients[idx].corner = desired;
    return 1;
}

int arc_retire(arc_rail_t* r)
{
    uint32_t i, corner;
    if (!live(r))
        return 0;
    for (i = 0u; i < ARC_CLIENTS; ++i)
        if (r->clients[i].used && r->clients[i].corner)
            return 0;
    if (r->floor_published && (!physical(r, &corner) || corner < r->boot_floor))
        return 0;
    memset(r, 0, sizeof(*r));
    return 1;
}
