/* icb_bus8845.c - /100 Molokai interconnect bandwidth owner, portable core (see inc/bus.h
 * for the arithmetic, the per-kind vote encoding and their src in the vendor ICB arbiter). */

#include "bus.h"
#include "molokai_catalog.h"
#include "cmddb.h"
#include "bcm.h"
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

static int str_eq(const char* a, const char* b)
{
    return a && b && strcmp(a, b) == 0;
}

static const icb_node_t* find_node(const icb_node_t* t, unsigned n, const char* sym)
{
    unsigned i;
    if (!sym)
        return 0;
    for (i = 0; i < n; ++i)
        if (str_eq(t[i].symbol, sym))
            return &t[i];
    return 0;
}

static const icb_node_t* node_by_id(const icb_node_t* t, unsigned n, uint32_t id)
{
    unsigned i;
    for (i = 0; i < n; ++i)
        if (t[i].id == id)
            return &t[i];
    return 0;
}

const icb_node_t* icb_find_master(const char* symbol)
{
    return find_node(QcPep8845IcbMasterNodes, QcPep8845IcbMasterNodeCount, symbol);
}

const icb_node_t* icb_find_slave(const char* symbol)
{
    return find_node(QcPep8845IcbSlaveNodes, QcPep8845IcbSlaveNodeCount, symbol);
}

static int node_votes_bcm(const icb_node_t* n, uint32_t bcm)
{
    uint32_t i;
    if (!n || n->bcm_first > QcPep8845IcbNodeBcmCount ||
        n->bcm_cnt > QcPep8845IcbNodeBcmCount - n->bcm_first)
        return 0;
    for (i = 0; i < n->bcm_cnt; ++i)
        if (QcPep8845IcbNodeBcms[n->bcm_first + i] == bcm)
            return 1;
    return 0;
}

static uint32_t route_nodes_by_id(uint32_t master_id, uint32_t slave_id, const icb_node_t** out,
                                  uint32_t cap)
{
    const icb_route_t* r = lookup_icb_route(master_id, slave_id);
    uint32_t i, n = 0;
    if (!r || !out || r->hop_cnt == 0u || r->hop_cnt > ICB_MAX_ROUTE_NODES / 2u)
        return 0;
    if (r->hop_offset > icb_hop_cnt || r->hop_cnt > icb_hop_cnt - r->hop_offset)
        return 0;
    for (i = 0; i < r->hop_cnt; ++i) {
        const icb_hop_t* h = &icb_hops[r->hop_offset + i];
        const icb_node_t* m =
            node_by_id(QcPep8845IcbMasterNodes, QcPep8845IcbMasterNodeCount, h->master_id);
        const icb_node_t* s =
            node_by_id(QcPep8845IcbSlaveNodes, QcPep8845IcbSlaveNodeCount, h->slave_id);
        if (!m || !s || n + 2u > cap)
            return 0;
        out[n++] = m;
        out[n++] = s;
    }
    /* the route must start at the requested master and end at the slave. */
    if (out[0]->id != master_id || out[n - 1u]->id != slave_id)
        return 0;
    return n;
}

uint32_t icb_route_nodes(const char* master_symbol, const char* slave_symbol,
                         const icb_node_t** nodes, uint32_t capacity)
{
    const icb_node_t* m = icb_find_master(master_symbol);
    const icb_node_t* s = icb_find_slave(slave_symbol);
    if (!m || !s || !nodes || capacity < 2u)
        return 0;
    return route_nodes_by_id(m->id, s->id, nodes, capacity);
}

uint32_t icb_bus_init(icb_bus_t* bus, const uint8_t* cmd_db, size_t cmd_db_bytes)
{
    unsigned i;
    uint32_t usable = 0;
    if (!bus)
        return 0;
    memset(bus, 0, sizeof(*bus));
    for (i = 0; i < QcPep8845IcbBcmNodeCount && bus->bcm_cnt < ICB_MAX_BCMS; ++i) {
        const icb_bcm_node_t* node = &QcPep8845IcbBcmNodes[i];
        icb_bcm_t* b = &bus->bcms[bus->bcm_cnt++];
        cmddb_entry_t e;
        bcm_aux_t aux;
        b->name = node->name;
        b->votable = (node->kind == BCM_KIND_BANDWIDTH || node->kind == BCM_KIND_VECTOR) ? 1u : 0u;
        if (!cmd_db || !cmd_db_lookup(cmd_db, cmd_db_bytes, node->name, &e))
            continue;
        b->address = e.address;
        b->address_known = 1u;

        if (bcm_decode_aux(e.aux_data, e.aux_length, &aux)) {
            b->unit = aux.bandwidth_unit;
            b->width = aux.port;
            b->clk_id = aux.clock_id;
            b->aux_known = 1u;
        }
        if (!b->votable || !b->aux_known)
            continue;
        if (node->kind == BCM_KIND_BANDWIDTH && (!b->unit || !b->width))
            continue;
        ++usable;
    }
    bus->initialized = 1u;
    return usable;
}

static int bcm_usable(const icb_bus_t* bus, uint32_t idx)
{
    const icb_bcm_node_t* node;
    const icb_bcm_t* b;
    if (idx >= bus->bcm_cnt || idx >= QcPep8845IcbBcmNodeCount)
        return 0;
    node = &QcPep8845IcbBcmNodes[idx];
    b = &bus->bcms[idx];
    /* every kind needs a physical VCD identity for COMMIT ordering. */
    if (!b->votable || !b->address_known || !b->aux_known)
        return 0;
    if (node->kind == BCM_KIND_BANDWIDTH && (!b->unit || !b->width))
        return 0;
    return 1;
}

static const char* route_missing(const icb_bus_t* bus, const char* ms, const char* ss)
{
    const icb_node_t* nodes[ICB_MAX_ROUTE_NODES];
    uint32_t n, i, j;
    if (!bus || !bus->initialized)
        return "(bus not initialized)";
    n = icb_route_nodes(ms, ss, nodes, ICB_MAX_ROUTE_NODES);
    if (!n)
        return "(no route)";
    for (i = 0; i < n; ++i) {
        const icb_node_t* ep = nodes[i];
        if (ep->bcm_first > QcPep8845IcbNodeBcmCount ||
            ep->bcm_cnt > QcPep8845IcbNodeBcmCount - ep->bcm_first)
            return "(bad topology)";
        for (j = 0; j < ep->bcm_cnt; ++j) {
            uint32_t idx = QcPep8845IcbNodeBcms[ep->bcm_first + j];
            if (!bcm_usable(bus, idx))
                return (idx < QcPep8845IcbBcmNodeCount) ? QcPep8845IcbBcmNodes[idx].name
                                                        : "(bad index)";
        }
    }
    return 0;
}

int icb_route_supported(const icb_bus_t* bus, const char* master_symbol, const char* slave_symbol)
{
    return route_missing(bus, master_symbol, slave_symbol) == 0;
}

const char* icb_route_missing_bcm(const icb_bus_t* bus, const char* master_symbol,
                                  const char* slave_symbol)
{
    return route_missing(bus, master_symbol, slave_symbol);
}

static icb_client_t* find_client(icb_bus_t* bus, const char* path, uint32_t m, uint32_t s,
                                 int create)
{
    uint32_t i;
    icb_client_t* free_slot = 0;
    for (i = 0; i < ICB_MAX_CLIENTS; ++i) {
        icb_client_t* c = &bus->clients[i];
        if (!c->in_use) {
            if (!free_slot)
                free_slot = c;
            continue;
        }
        if (c->master_id == m && c->slave_id == s &&
            strncmp(c->dev_path, path, ICB_PATH_BYTES) == 0)
            return c;
    }
    if (!create || !free_slot)
        return 0;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = 1u;
    free_slot->master_id = m;
    free_slot->slave_id = s;
    copy_bounded(free_slot->dev_path, ICB_PATH_BYTES, path);
    return free_slot;
}

int icb_set_client_vote(icb_bus_t* bus, const char* dev_path, const char* master_symbol,
                        const char* slave_symbol, uint64_t ab, uint64_t ib)
{
    const icb_node_t* m = icb_find_master(master_symbol);
    const icb_node_t* s = icb_find_slave(slave_symbol);
    icb_client_t* c;
    if (!bus || !bus->initialized || !dev_path || !dev_path[0] || !m || !s)
        return 0;
    if (!icb_route_supported(bus, master_symbol, slave_symbol))
        return 0;
    if (ab == 0u && ib == 0u) {
        c = find_client(bus, dev_path, m->id, s->id, 0);
        if (c)
            memset(c, 0, sizeof(*c));
        return 1;
    }
    c = find_client(bus, dev_path, m->id, s->id, 1);
    if (!c)
        return 0;
    c->ab = ab;
    c->ib = ib;
    return 1;
}

int icb_get_client_vote(const icb_bus_t* bus, const char* dev_path, const char* master_symbol,
                        const char* slave_symbol, uint64_t* ab, uint64_t* ib)
{
    const icb_node_t* m = icb_find_master(master_symbol);
    const icb_node_t* s = icb_find_slave(slave_symbol);
    icb_client_t* c;
    if (ab)
        *ab = 0u;
    if (ib)
        *ib = 0u;
    if (!bus || !dev_path || !m || !s)
        return 0;
    c = find_client((icb_bus_t*)bus, dev_path, m->id, s->id, 0);
    if (!c)
        return 0;
    if (ab)
        *ab = c->ab;
    if (ib)
        *ib = c->ib;
    return 1;
}

/* ab is summed and ib maximized over every request whose route crosses the endpoint
 * (ul_int.c update_sw_bandwidth_state). `only`, when set, replaces the recorded client
 * table with that single request. */

static void node_state(const icb_bus_t* bus, const icb_node_t* node, const icb_client_t* only,
                       uint64_t* ab, uint64_t* ib)
{
    const icb_node_t* nodes[ICB_MAX_ROUTE_NODES];
    uint32_t i, j, n;
    *ab = 0u;
    *ib = 0u;
    for (i = 0; i < ICB_MAX_CLIENTS; ++i) {
        const icb_client_t* c = only ? only : &bus->clients[i];
        if (!c->in_use || (c->ab == 0u && c->ib == 0u)) {
            if (only)
                break;
            continue;
        }
        n = route_nodes_by_id(c->master_id, c->slave_id, nodes, ICB_MAX_ROUTE_NODES);
        for (j = 0; j < n; ++j) {
            if (nodes[j] != node)
                continue;
            if (c->ib > *ib)
                *ib = c->ib;
            *ab = (*ab > UINT64_MAX - c->ab) ? UINT64_MAX : *ab + c->ab;
            break;
        }
        if (only)
            break;
    }
}

static uint64_t ceil_scale(uint64_t v, uint64_t mul, uint64_t div)
{
    uint64_t q, r, whole, frac;
    if (!div || !mul)
        return 0u;
    q = v / div;
    r = v % div;
    if (q > UINT64_MAX / mul)
        return UINT64_MAX;
    whole = q * mul;
    if (r > UINT64_MAX / mul)
        return UINT64_MAX;
    frac = (r * mul) / div;
    if ((r * mul) % div)
        ++frac;
    return whole > UINT64_MAX - frac ? UINT64_MAX : whole + frac;
}

uint32_t icb_encode_vote(uint64_t scaled_ib, uint64_t scaled_ab)
{
    uint32_t x = (uint32_t)(scaled_ab > BCM_VOTE_MASK ? BCM_VOTE_MASK : scaled_ab);
    uint32_t y = (uint32_t)(scaled_ib > BCM_VOTE_MASK ? BCM_VOTE_MASK : scaled_ib);
    uint32_t v = (x << BCM_VOTE_X_SHIFT) | y;
    if (v)
        v |= BCM_VALID;
    return v;
}

static int bcm_active(const icb_bus_t* bus, uint32_t idx, const icb_client_t* only)
{
    const icb_node_t* tables[2] = {QcPep8845IcbMasterNodes, QcPep8845IcbSlaveNodes};
    unsigned counts[2] = {QcPep8845IcbMasterNodeCount, QcPep8845IcbSlaveNodeCount};
    uint64_t ab, ib;
    unsigned t, i;
    for (t = 0; t < 2u; ++t)
        for (i = 0; i < counts[t]; ++i) {
            const icb_node_t* ep = &tables[t][i];
            if (!node_votes_bcm(ep, idx))
                continue;
            node_state(bus, ep, only, &ab, &ib);
            if (ab || ib)
                return 1;
        }
    return 0;
}

/* the 32-bit word for one BCM node, per its kind. */
static uint32_t bcm_vote(const icb_bus_t* bus, uint32_t idx, const icb_client_t* only)
{
    const icb_bcm_node_t* node = &QcPep8845IcbBcmNodes[idx];
    const icb_bcm_t* b = &bus->bcms[idx];
    const icb_node_t* tables[2] = {QcPep8845IcbMasterNodes, QcPep8845IcbSlaveNodes};
    unsigned counts[2] = {QcPep8845IcbMasterNodeCount, QcPep8845IcbSlaveNodeCount};
    uint64_t ib = 0u, ab = 0u, nab, nib, sab, sib, qib, qab;
    unsigned t, i;
    uint32_t val, r;

    if (node->kind == BCM_KIND_VECTOR) {

        val = (node->keep_alive || bcm_active(bus, idx, only)) ? node->output : 0u;
        if (node->relative_first <= QcPep8845IcbBcmRelativeCount &&
            node->relative_cnt <= QcPep8845IcbBcmRelativeCount - node->relative_first) {
            for (r = 0; r < node->relative_cnt; ++r) {
                uint32_t rel = QcPep8845IcbBcmRelatives[node->relative_first + r];
                if (rel < QcPep8845IcbBcmNodeCount &&
                    (QcPep8845IcbBcmNodes[rel].keep_alive || bcm_active(bus, rel, only)))
                    val |= QcPep8845IcbBcmNodes[rel].output;
            }
        }
        if (val)
            val |= BCM_VALID;
        return val;
    }
    if (node->kind != BCM_KIND_BANDWIDTH)
        return 0u;

    for (t = 0; t < 2u; ++t)
        for (i = 0; i < counts[t]; ++i) {
            const icb_node_t* ep = &tables[t][i];
            if (!node_votes_bcm(ep, idx) || ep->width_bytes == 0u)
                continue;
            node_state(bus, ep, only, &nab, &nib);
            if (nab == 0u && nib == 0u)
                continue;
            sib = ceil_scale(nib, b->width, ep->width_bytes);
            sab = ceil_scale(nab, b->width,
                             (uint64_t)ep->width_bytes * (ep->agg_ports ? ep->agg_ports : 1u));
            if (sib > ib)
                ib = sib;
            if (sab > ab)
                ab = sab;
        }
    if (!b->unit)
        return 0u;

    qib = (ib && ib < b->unit) ? 1u : ib / b->unit;
    qab = ab / b->unit + ((ab % b->unit) ? 1u : 0u);
    /* molokai's active keepalive is a 1/1 idle vote, not a clamp on each dimension of an
     * existing client vote. */
    if (node->keep_alive && qib == 0u && qab == 0u)
        qib = qab = 1u;
    return icb_encode_vote(qib, qab);
}

static int route_commands(const icb_bus_t* bus, const char* ms, const char* ss,
                          const icb_client_t* only, icb_command_t* out, uint32_t cap, uint32_t* cnt)
{
    const icb_node_t* nodes[ICB_MAX_ROUTE_NODES];
    uint32_t list[ICB_MAX_BCMS];
    uint32_t n, i, j, k, used = 0;
    if (cnt)
        *cnt = 0;
    if (!bus || !bus->initialized || !out || !cnt || !cap)
        return 0;
    if (route_missing(bus, ms, ss) != 0)
        return 0;
    n = icb_route_nodes(ms, ss, nodes, ICB_MAX_ROUTE_NODES);
    if (!n)
        return 0;

    for (i = 0; i < n; ++i) {
        const icb_node_t* ep = nodes[i];
        for (j = 0; j < ep->bcm_cnt; ++j) {
            uint32_t idx = QcPep8845IcbNodeBcms[ep->bcm_first + j];
            int dup = 0;
            for (k = 0; k < used; ++k)
                if (list[k] == idx || bus->bcms[list[k]].address == bus->bcms[idx].address) {
                    dup = 1;
                    break;
                }
            if (dup)
                continue;
            if (used >= ICB_MAX_BCMS)
                return 0;
            list[used++] = idx;
        }
    }
    if (!used || used > cap)
        return 0;

    for (i = 1; i < used; ++i) {
        uint32_t key = list[i];
        j = i;
        while (j > 0 && (bus->bcms[list[j - 1]].clk_id > bus->bcms[key].clk_id ||
                         (bus->bcms[list[j - 1]].clk_id == bus->bcms[key].clk_id &&
                          strcmp(bus->bcms[list[j - 1]].name, bus->bcms[key].name) > 0))) {
            list[j] = list[j - 1];
            --j;
        }
        list[j] = key;
    }
    for (i = 0; i < used; ++i) {
        out[i].address = bus->bcms[list[i]].address;
        out[i].data = bcm_vote(bus, list[i], only);
        out[i].completion = 1u;
        if (i + 1u == used || bus->bcms[list[i + 1]].clk_id != bus->bcms[list[i]].clk_id)
            out[i].data |= BCM_COMMIT;
    }
    *cnt = used;
    return 1;
}

int icb_route_commands(const icb_bus_t* bus, const char* master_symbol, const char* slave_symbol,
                       icb_command_t* out, uint32_t capacity, uint32_t* cnt)
{
    return route_commands(bus, master_symbol, slave_symbol, 0, out, capacity, cnt);
}

int icb_single_req_commands(const icb_bus_t* bus, const char* master_symbol,
                            const char* slave_symbol, uint64_t ab, uint64_t ib, icb_command_t* out,
                            uint32_t capacity, uint32_t* cnt)
{
    icb_client_t only;
    const icb_node_t* m = icb_find_master(master_symbol);
    const icb_node_t* s = icb_find_slave(slave_symbol);
    if (!m || !s) {
        if (cnt)
            *cnt = 0;
        return 0;
    }
    memset(&only, 0, sizeof(only));
    only.in_use = 1u;
    only.master_id = m->id;
    only.slave_id = s->id;
    only.ab = ab;
    only.ib = ib;
    return route_commands(bus, master_symbol, slave_symbol, &only, out, capacity, cnt);
}
