#include "nodes.h"
#include <string.h>

static size_t identifier_length(const char* s, size_t limit)
{
    size_t i;
    if (!s)
        return 0u;
    for (i = 0u; i < limit; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c)
            return i;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0u;
    }
    return 0u;
}

static int owned(const fs_nodes_t* n)
{
    return n && n->bound && !n->quarantined && n->io.owns &&
           n->io.owns(n->io.ctx, n->domain, n->domain_id, n->epoch);
}

static int valid_action(uint32_t family, uint32_t action)
{
    return family < 2u && (action == family * 2u + 1u || action == family * 2u + 2u);
}

int fs_nodes_verify(fs_nodes_t* n)
{
    uint32_t f, actual;
    if (!n || !n->bound || n->quarantined)
        return 0;
    if (!owned(n))
        goto lost;
    for (f = 0u; f < 2u; ++f) {
        if (!(n->family_mask & (1u << f)))
            continue;
        if (!n->io.read_action(n->io.ctx, n->domain, n->epoch, f, &actual) ||
            !valid_action(f, actual) || !owned(n) ||
            (n->nodes[f].has_physical_state && n->nodes[f].active_action != actual))
            goto lost;
    }
    return 1;
lost:
    n->quarantined = 1u;
    return 0;
}

static int family_of(const fs_nodes_t* n, const char* name)
{
    uint32_t f;
    size_t i;
    if (!n || !name)
        return -1;
    for (i = 0u; i < FS_FACTORY_NAME_BYTES && name[i]; ++i) {
    }
    if (i == FS_FACTORY_NAME_BYTES)
        return -1;
    for (f = 0u; f < 2u; ++f)
        if ((n->family_mask & (1u << f)) && !strcmp(name, n->nodes[f].name))
            return (int)f;
    return -1;
}

static fs_node_client_t* lookup(fs_nodes_t* n, const char* name, uint64_t token)
{
    uint32_t i;
    int family = family_of(n, name);
    if (!owned(n) || family < 0 || !token)
        return NULL;
    for (i = 0u; i < FS_NODE_CLIENTS; ++i) {
        fs_node_client_t* c = &n->clients[i];
        if (c->in_use && c->token == token && c->family == (uint8_t)family)
            return c;
    }
    return NULL;
}

static uint32_t minimum(const fs_nodes_t* n, const fs_node_client_t* changed, uint32_t pending)
{
    uint32_t i, result = 0u;
    for (i = 0u; i < FS_NODE_CLIENTS; ++i) {
        const fs_node_client_t* c = &n->clients[i];
        uint32_t value;
        if (!c->in_use || c->family != changed->family)
            continue;

        value = c == changed ? pending : c->work[c->idx];
        if (value && (!result || value < result))
            result = value;
    }
    return result;
}

static int provider_owns(void* ctx, const char* domain)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    return n && identifier_length(domain, sizeof(n->domain)) && !strcmp(domain, n->domain) &&
           owned(n);
}

static int provider_epoch(void* ctx, const char* domain, uint64_t* epoch)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    if (!epoch || !provider_owns(n, domain))
        return 0;
    *epoch = n->epoch;
    return 1;
}

static int provider_domain(void* ctx, const char* domain, uint32_t* id)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    if (!id || !provider_owns(n, domain))
        return 0;
    *id = n->domain_id;
    return 1;
}

static int provider_open(void* ctx, const char* res, const char* name, uint32_t type,
                         uint64_t* token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    int family = family_of(n, res);
    size_t length = identifier_length(name, FS_FACTORY_NAME_BYTES);
    uint32_t i;
    if (token)
        *token = 0u;
    if (!token || !owned(n) || n->closing || family < 0 || !length ||
        type != FS_NPA_REQUIRED_CLIENT_TYPE || n->next_token >= UINT64_MAX / 2u ||
        !fs_nodes_verify(n))
        return 0;
    for (i = 0u; i < FS_NODE_CLIENTS; ++i) {
        fs_node_client_t* c = &n->clients[i];
        if (c->in_use)
            continue;
        memset(c, 0, sizeof(*c));
        c->token = (++n->next_token << 1u) | (uint32_t)family;
        memcpy(c->name, name, length + 1u);
        c->family = (uint8_t)family;
        c->work[0] = c->work[1] = FS_CLIENT_INITIAL;
        c->in_use = 1u;
        *token = c->token;
        return 1;
    }
    return 0;
}

static int provider_read(void* ctx, const char* res, uint64_t token, uint32_t* present,
                         uint32_t* scalar)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    if (!c || !present || !scalar || !fs_nodes_verify(n))
        return 0;
    *present = c->has_req;
    *scalar = c->has_req ? c->work[c->idx] : 0u;
    return 1;
}

static int transition(fs_nodes_t* n, fs_node_client_t* c, uint32_t pending, int present)
{
    fs_node_t* node = &n->nodes[c->family];
    uint32_t aggregate, action, actual;
    if (!fs_nodes_verify(n))
        return 0;
    aggregate = minimum(n, c, pending);
    action = aggregate > node->maximum ? node->maximum : aggregate;
    /* the OEM dispatcher faults on zero. Refuse before touching a client or HAL; retaining the
     * previous physical action would invent a completion. */
    if (!valid_action(c->family, action))
        return 0;
    c->work[c->idx ^ 1u] = pending;
    if ((!node->has_physical_state || node->active_action != action || node->force_driver) &&
        !n->io.apply_action(n->io.ctx, n->domain, n->epoch, action))
        goto lost;
    if (!owned(n) || !n->io.read_action(n->io.ctx, n->domain, n->epoch, c->family, &actual) ||
        actual != action || !owned(n))
        goto lost;
    /* the other family shares the domain and rail dependency lifetime. */
    {
        uint32_t other = c->family ^ 1u;
        if ((n->family_mask & (1u << other)) &&
            (!n->io.read_action(n->io.ctx, n->domain, n->epoch, other, &actual) ||
             !valid_action(other, actual) || !owned(n) ||
             (n->nodes[other].has_physical_state && actual != n->nodes[other].active_action)))
            goto lost;
    }
    c->idx ^= 1u;
    c->has_req = (uint8_t)present;
    node->aggregate = aggregate;
    node->active_action = action;
    node->has_physical_state = 1u;
    node->force_driver = present ? 0u : 1u;
    return 1;
lost:
    /* keep the pending buf and old active idx for owner recovery. */
    n->quarantined = 1u;
    return 0;
}

static int provider_issue(void* ctx, const char* res, uint64_t token, uint32_t action)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    if (!c || !valid_action(c->family, action) ||
        (n->closing && action != n->nodes[c->family].maximum))
        return 0;
    if (c->has_req && c->work[c->idx] == action && !n->nodes[c->family].force_driver)
        return fs_nodes_verify(n);
    return transition(n, c, action, 1);
}

static int provider_can_complete(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    if (!c || !fs_nodes_verify(n))
        return 0;
    return !c->has_req || minimum(n, c, 0u) != 0u;
}

static int provider_complete(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    if (!c || !fs_nodes_verify(n))
        return 0;
    if (!c->has_req)
        return 1;
    return transition(n, c, 0u, 0);
}

static int provider_can_retire_off(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    uint32_t action = 0u;
    if (!c || !n->closing || !c->has_req || c->work[c->idx] != n->nodes[c->family].maximum ||
        !n->nodes[c->family].has_physical_state ||
        n->nodes[c->family].active_action != n->nodes[c->family].maximum ||
        minimum(n, c, 0u) != 0u || !fs_nodes_verify(n) ||
        !n->io.read_action(n->io.ctx, n->domain, n->epoch, c->family, &action) ||
        action != n->nodes[c->family].maximum || !owned(n))
        return 0;
    return 1;
}

static int provider_retire_off(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c;
    fs_node_t* node;
    if (!provider_can_retire_off(ctx, res, token))
        return 0;
    c = lookup(n, res, token);
    if (!c)
        return 0;
    node = &n->nodes[c->family];
    /* no MMIO write here. The last client already issued OFF, whose owner and effective
     * physical readback are checked before withdrawing its record. */
    c->work[c->idx ^ 1u] = 0u;
    c->idx ^= 1u;
    c->has_req = 0u;
    node->aggregate = 0u;
    node->force_driver = 1u;
    return 1;
}

static int provider_close(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    fs_node_client_t* c = lookup(n, res, token);
    if (!c || c->has_req || !fs_nodes_verify(n))
        return 0;
    /* never-issued records own no vote. This is explicit local teardown, not a call to the OEM
     * destroy routine's implicit terminate path. */
    memset(c, 0, sizeof(*c));
    return 1;
}

static int provider_gone(void* ctx, const char* res, uint64_t token)
{
    fs_nodes_t* n = (fs_nodes_t*)ctx;
    uint32_t i;
    int family = family_of(n, res);
    if (!owned(n) || family < 0 || !(token >> 1u) || (token >> 1u) > n->next_token ||
        (token & 1u) != (uint32_t)family || !fs_nodes_verify(n))
        return 0;
    for (i = 0u; i < FS_NODE_CLIENTS; ++i)
        if (n->clients[i].in_use && n->clients[i].token == token)
            return 0;
    return 1;
}

int fs_nodes_bind(fs_nodes_t* n, const char* domain, uint32_t id, uint32_t families,
                  const fs_node_io_t* io, uint64_t epoch)
{
    const char* suffix[2] = {"/enable", "/hwctrlenable"};
    uint64_t sequence;
    uint32_t f, actual;
    size_t length = identifier_length(domain, FS_NAME_BYTES);
    if (!n || n->bound || n->quarantined || !length || !id || !epoch || !families ||
        (families & ~3u) || !io || !io->owns || !io->read_action || !io->apply_action ||
        !io->owns(io->ctx, domain, id, epoch))
        return 0;
    for (f = 0u; f < 2u; ++f) {
        if (!(families & (1u << f)))
            continue;
        if (4u + length + strlen(suffix[f]) >= FS_FACTORY_NAME_BYTES ||
            !io->read_action(io->ctx, domain, epoch, f, &actual) || !valid_action(f, actual) ||
            !io->owns(io->ctx, domain, id, epoch))
            return 0;
    }
    sequence = n->next_token;
    memset(n, 0, sizeof(*n));
    n->next_token = sequence;
    n->io = *io;
    n->epoch = epoch;
    n->domain_id = id;
    n->family_mask = families;
    memcpy(n->domain, domain, length + 1u);
    for (f = 0u; f < 2u; ++f) {
        fs_node_t* node = &n->nodes[f];
        if (!(families & (1u << f)))
            continue;
        memcpy(node->name, "/fs/", 4u);
        memcpy(node->name + 4u, domain, length);
        memcpy(node->name + 4u + length, suffix[f], strlen(suffix[f]) + 1u);
        node->maximum = f * 2u + 2u;
        node->active_action = FS_NODE_INITIAL;
    }
    n->bound = 1u;
    return fs_nodes_verify(n);
}

int fs_nodes_provider(fs_nodes_t* n, fs_factory_provider_t* p)
{
    if (!p || !fs_nodes_verify(n))
        return 0;
    memset(p, 0, sizeof(*p));
    p->ctx = n;
    p->owns_domain = provider_owns;
    p->read_owner_epoch = provider_epoch;
    p->resolve_domain_id = provider_domain;
    p->open_idle_client = provider_open;
    p->read_client = provider_read;
    p->issue_client = provider_issue;
    p->can_complete_client = provider_can_complete;
    p->complete_client = provider_complete;
    p->close_idle_client = provider_close;
    p->client_gone = provider_gone;
    p->can_retire_off_client = provider_can_retire_off;
    p->retire_off_client = provider_retire_off;
    return 1;
}

int fs_nodes_begin_shutdown(fs_nodes_t* n, uint64_t epoch)
{
    if (!n || !epoch || !n->bound || n->epoch != epoch || !fs_nodes_verify(n))
        return 0;
    n->closing = 1u;
    return 1;
}

int fs_nodes_retire(fs_nodes_t* n)
{
    uint32_t i;
    uint64_t sequence;
    if (!n || !n->closing || !fs_nodes_verify(n))
        return 0;
    for (i = 0u; i < FS_NODE_CLIENTS; ++i)
        if (n->clients[i].in_use)
            return 0;
    for (i = 0u; i < 2u; ++i)
        if (n->nodes[i].has_physical_state && n->nodes[i].active_action != n->nodes[i].maximum)
            return 0;
    sequence = n->next_token;
    memset(n, 0, sizeof(*n));
    n->next_token = sequence;
    return 1;
}
