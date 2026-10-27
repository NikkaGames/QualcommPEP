#include "scalar.h"
#include <string.h>
#include <limits.h>

static int valid_scalar_name(const char* name)
{
    unsigned i;
    if (!name)
        return 0;
    for (i = 0; i < SCALAR_NAME_BYTES; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c)
            return i > 0u;
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
            c != '_' && c != '/' && c != '-')
            return 0;
        if (c == '/' && i > 0u && name[i - 1u] == '/')
            return 0;
    }
    return 0;
}

static int owns(scalar_res_t* r)
{
    /* treat revocation of the attestation as loss of res ownership, not as permission to
     * disable the clock on the next d3 transition. */

    uint64_t current = 0u;
    return r->io.owns_exclusive(r->io.ctx, r->name) &&
           (r->aggregate_mode == SCALAR_AGGREGATE_MAX ||
            (r->owner_epoch && r->io.read_owner_epoch &&
             r->io.read_owner_epoch(r->io.ctx, r->name, &current) && current == r->owner_epoch)) &&
           (!r->baseline_effective ||
            (r->io.attests_boot_baseline &&
             r->io.attests_boot_baseline(r->io.ctx, r->name, r->baseline_effective)));
}

static int read_state(scalar_res_t* r, uint32_t* state)
{
    return owns(r) && r->io.read_effective(r->io.ctx, r->name, state);
}

static int read_native_client(scalar_res_t* r, scalar_client_t* c, uint32_t expected_present,
                              uint32_t expected_req)
{
    uint32_t present = UINT32_MAX, req = UINT32_MAX;
    return owns(r) && c->native_token && r->io.read_native_client &&
           r->io.read_native_client(r->io.ctx, r->name, c->native_token, &present, &req) &&
           present == expected_present && (!present || req == expected_req) && owns(r);
}

/* verify every other published client before commit or compensation. A peer mismatch
 * forbids further writes even if the aggregate matches. */

static int native_peers_intact(scalar_res_t* r, const scalar_client_t* changed)
{
    uint32_t i;
    if (!r->native_transport)
        return 1;
    for (i = 0u; i < SCALAR_MAX_CLIENTS; i++) {
        scalar_client_t* c = &r->clients[i];
        if (c != changed && c->exists && !read_native_client(r, c, c->has_req ? 1u : 0u, c->req)) {
            r->quarantined = 1u;
            return 0;
        }
    }
    return 1;
}

static int native_req(scalar_res_t* r, scalar_client_t* c, uint32_t next, int has)
{
    if (!owns(r) || !c->native_token)
        return 0;
    if (has)
        return r->io.issue_native_client &&
               r->io.issue_native_client(r->io.ctx, r->name, c->native_token, next);
    return r->io.complete_native_client &&
           r->io.complete_native_client(r->io.ctx, r->name, c->native_token);
}

int scalar_verify_owned(scalar_res_t* r)
{
    uint32_t state = UINT32_MAX;
    if (!r || !r->active || r->quarantined)
        return 0;
    /* ownership, attestation, and effective state are one admission proof. A failed
     * independent readback or unexpected effective state indicates that the claimed exclusive
     * owner has lost control or its hardware request did not take effect. */

    if (!owns(r) || !r->io.read_effective(r->io.ctx, r->name, &state) || state != r->effective ||
        !owns(r)) {
        r->quarantined = 1u;
        return 0;
    }
    if (r->native_transport) {
        uint32_t i;
        for (i = 0u; i < SCALAR_MAX_CLIENTS; i++) {
            scalar_client_t* c = &r->clients[i];
            if (c->exists && !read_native_client(r, c, c->has_req ? 1u : 0u, c->req)) {
                /* a local vote cache that happens to match the aggregate cannot conceal a
                 * substituted/stale native client. */

                r->quarantined = 1u;
                return 0;
            }
        }
    }
    return 1;
}

static scalar_client_t* lookup(scalar_res_t* r, scalar_handle_t h)
{
    if (!r || !r->active || r->quarantined || h.slot >= SCALAR_MAX_CLIENTS)
        return NULL;
    if (!r->clients[h.slot].exists || r->clients[h.slot].generation != h.generation)
        return NULL;
    return &r->clients[h.slot];
}

static uint32_t aggregate(const scalar_res_t* r, const scalar_client_t* changed, uint32_t next,
                          int has)
{
    uint32_t i, effective = 0u;
    for (i = 0; i < SCALAR_MAX_CLIENTS; ++i) {
        const scalar_client_t* c = &r->clients[i];
        uint32_t value;
        int present;
        if (!c->exists)
            continue;
        present = (c == changed) ? has : c->has_req;
        if (!present)
            continue;
        value = (c == changed) ? next : c->req;
        if (r->aggregate_mode == SCALAR_AGGREGATE_CLOCK_ENABLE) {
            /* both 0x40 and 0x800 client banks contribute to min-nonzero. */

            if (!effective || value < effective)
                effective = value;
        } else if (value > effective)
            effective = value;
    }
    if (r->aggregate_mode == SCALAR_AGGREGATE_CLOCK_ENABLE)
        return effective && r->baseline_effective
                   ? (effective < r->baseline_effective ? effective : r->baseline_effective)
                   : (effective ? effective : r->baseline_effective);
    return effective > r->baseline_effective ? effective : r->baseline_effective;
}

static int native_clock_name(const char* name, uint32_t mode)
{
    static const char suffix[] = "/enable";
    static const char gdsc[] = "_gdsc";
    const char* base;
    const char* slash;
    size_t length;
    /* only frequency and /enable resources use this transport; memory and peripheral gates
     * have separate types. */

    if (!valid_scalar_name(name) || strncmp(name, "/clk/", 5) != 0)
        return 0;
    base = name + 5;
    slash = strchr(base, '/');
    length = slash ? (size_t)(slash - base) : strlen(base);
    if (!length || (length >= sizeof(gdsc) - 1u &&
                    memcmp(base + length - (sizeof(gdsc) - 1u), gdsc, sizeof(gdsc) - 1u) == 0))
        return 0;
    if (mode == SCALAR_AGGREGATE_CLOCK_FREQ)
        return slash == NULL;
    if (mode == SCALAR_AGGREGATE_CLOCK_ENABLE)
        return slash && strcmp(slash, suffix) == 0;
    return 0;
}

int scalar_attach_native_clock(scalar_res_t* r, const char* name, const scalar_io_t* io,
                               uint32_t mode)
{
    uint64_t initial_epoch = 0u;
    if (!native_clock_name(name, mode) || !io || !io->read_owner_epoch ||
        !io->read_owner_epoch(io->ctx, name, &initial_epoch) || !initial_epoch ||
        !scalar_attach_with_baseline(r, name, io, 0u))
        return 0;
    /* name/physical-value equality is not native NPA node identity: an owner can be replaced
     * during the attachment reads and reuse both. Publish the native mode only with a pinned,
     * unchanged owner epoch. */

    r->owner_epoch = initial_epoch;
    r->aggregate_mode = mode;
    if (!owns(r)) {
        r->quarantined = 1u;
        return 0;
    }
    return 1;
}

int scalar_attach_native_clock_client_transport(scalar_res_t* r, const char* name,
                                                const scalar_io_t* io, uint32_t mode)
{
    uint64_t before = 0u, after = 0u;
    uint32_t state = UINT32_MAX, generation;
    /* the physical transport owns *individual* NPA clients. never accept an aggregate fallback
     * in a native-client-only res. */

    if (!r || !io || !native_clock_name(name, mode) || !io->owns_exclusive || !io->read_effective ||
        !io->allows_vote || !io->read_owner_epoch || !io->create_native_client ||
        !io->issue_native_client || !io->complete_native_client || !io->destroy_native_client ||
        !io->read_native_client || io->write_effective || r->active || r->quarantined ||
        r->generation == UINT32_MAX)
        return 0;
    /* no callbacks above are allowed to create a client or modify hardware. They must observe
     * the same independently owned and INITIALIZED NPA res generation before the res becomes
     * visible to callers. */

    if (!io->owns_exclusive(io->ctx, name) || !io->read_owner_epoch(io->ctx, name, &before) ||
        !before || !io->allows_vote(io->ctx, name, 0u) ||
        !io->read_effective(io->ctx, name, &state) || state != 0u ||
        !io->read_owner_epoch(io->ctx, name, &after) || after != before ||
        !io->owns_exclusive(io->ctx, name))
        return 0;
    generation = r->generation + 1u;
    memset(r, 0, sizeof(*r));
    r->generation = generation;
    memcpy(r->name, name, strlen(name) + 1u);
    r->io = *io;
    r->owner_epoch = before;
    r->aggregate_mode = mode;
    r->native_transport = 1u;
    r->active = 1u;
    /* post-publication verification closes a window where ownership changed between the final
     * external check and local state setup. A suspect node stays quarantined; it must not be
     * reattached blindly. */

    if (!owns(r) || !read_state(r, &state) || state != 0u || !owns(r)) {
        r->quarantined = 1u;
        return 0;
    }
    return 1;
}

int scalar_attach_with_baseline(scalar_res_t* r, const char* name, const scalar_io_t* io,
                                uint32_t baseline)
{
    uint32_t state = UINT32_MAX;
    if (!r || !io || !io->owns_exclusive || !io->write_effective || !io->read_effective ||
        !io->allows_vote || !valid_scalar_name(name) || r->active || r->quarantined ||
        r->generation == UINT32_MAX)
        return 0;
    /* nonzero initialization cannot be inferred just because a clock is on: a different
     * firmware or Windows consumer might still own the vote. */

    if (!io->owns_exclusive(io->ctx, name) || !io->allows_vote(io->ctx, name, baseline) ||
        (baseline &&
         (!io->attests_boot_baseline || !io->attests_boot_baseline(io->ctx, name, baseline))) ||
        !io->read_effective(io->ctx, name, &state) || state != baseline ||
        !io->owns_exclusive(io->ctx, name))
        return 0;
    {
        uint32_t epoch = r->generation + 1u;
        memset(r, 0, sizeof(*r));
        r->generation = epoch;
    }
    memcpy(r->name, name, strlen(name) + 1u);
    r->io = *io;
    r->baseline_effective = baseline;
    r->effective = baseline;
    r->active = 1u;
    return 1;
}

int scalar_attach(scalar_res_t* r, const char* name, const scalar_io_t* io)
{
    return scalar_attach_with_baseline(r, name, io, 0u);
}

int scalar_create_typed(scalar_res_t* r, uint32_t client_type, scalar_handle_t* handle)
{
    uint32_t i, state = UINT32_MAX;
    if (handle)
        memset(handle, 0, sizeof(*handle));
    if (!r || !handle || !r->active || r->quarantined || r->generation == UINT32_MAX ||
        (client_type != SCALAR_CLIENT_REQUIRED &&
         (client_type != SCALAR_CLIENT_SUPPRESSIBLE || r->aggregate_mode == SCALAR_AGGREGATE_MAX)))
        return 0;
    if (!read_state(r, &state) || state != r->effective) {
        r->quarantined = 1u;
        return 0;
    }
    /* readback alone is insufficient: the provider might have been revoked during
     * read_effective. never publish a live client against an owner that has already
     * disappeared. */

    if (!owns(r) || (r->native_transport && !scalar_verify_owned(r))) {
        r->quarantined = 1u;
        return 0;
    }
    for (i = 0; i < SCALAR_MAX_CLIENTS; ++i) {
        scalar_client_t* c = &r->clients[i];
        if (!c->exists) {
            uint32_t generation = ++r->generation;
            memset(c, 0, sizeof(*c));
            c->exists = 1u;
            c->generation = generation;
            c->client_type = client_type;
            if (r->native_transport) {
                uint32_t present = UINT32_MAX, req = UINT32_MAX;
                uint64_t token = 0u;
                if (!owns(r) || !r->io.create_native_client ||
                    !r->io.create_native_client(r->io.ctx, r->name, client_type, &token) ||
                    !token) {

                    memset(c, 0, sizeof(*c));
                    r->quarantined = 1u;
                    return 0;
                }
                c->native_token = token;
                if (!owns(r)) {
                    r->quarantined = 1u;
                    return 0;
                }
                if (!r->io.read_native_client ||
                    !r->io.read_native_client(r->io.ctx, r->name, token, &present, &req) ||
                    present != 0u || !owns(r)) {
                    /* a client whose initial vote is unverified cannot be published; failing
                     * destruction quarantines the node. */

                    if (!owns(r) || !r->io.destroy_native_client ||
                        !r->io.destroy_native_client(r->io.ctx, r->name, token) || !owns(r) ||
                        !native_peers_intact(r, c) || !read_state(r, &state) ||
                        state != r->effective || !native_peers_intact(r, c) || !owns(r))
                        r->quarantined = 1u;
                    else
                        memset(c, 0, sizeof(*c));
                    return 0;
                }
                /* creating a new native client must not silently alter another client's outstanding
                 * vote. */

                if (!native_peers_intact(r, c) || !read_state(r, &state) || state != r->effective ||
                    !read_native_client(r, c, 0u, 0u) || !native_peers_intact(r, c) || !owns(r)) {
                    /* a created client may have changed the res state despite reporting "no vote".
                     * cannot prove that destroying it would restore other clients' state; retain
                     * the opaque handle for controlled rundown and quarantine the node. never
                     * publish it to DPM. */

                    r->quarantined = 1u;
                    return 0;
                }
            }
            handle->slot = i;
            handle->generation = generation;
            return 1;
        }
    }
    return 0;
}

int scalar_create(scalar_res_t* r, scalar_handle_t* handle)
{
    return scalar_create_typed(r, SCALAR_CLIENT_REQUIRED, handle);
}

static int transition(scalar_res_t* r, scalar_client_t* c, uint32_t next, int has)
{
    uint32_t actual = UINT32_MAX, wanted, previous;
    int force;
    if (!owns(r) || (r->native_transport && !scalar_verify_owned(r))) {
        r->quarantined = 1u;
        return 0;
    }
    /* native issue requests must never confuse zero (released) with an explicitly issued
     * OFF=2. the legacy generic scalar core still accepts a first zero request, as required by
     * its existing ABI. */

    if (has && r->aggregate_mode != SCALAR_AGGREGATE_MAX &&
        (!next || (r->aggregate_mode == SCALAR_AGGREGATE_CLOCK_ENABLE && next != 1u && next != 2u)))
        return 0;
    if (!r->io.allows_vote(r->io.ctx, r->name, has ? next : 0u))
        return 0;
    if (!read_state(r, &actual) || actual != r->effective) {
        r->quarantined = 1u;
        return 0;
    }
    previous = r->effective;
    wanted = aggregate(r, c, next, has);
    if (r->native_transport) {
        int old_has = c->has_req ? 1 : 0;
        uint32_t old_req = c->req;
        /* explicit OFF and client completion are distinct native operations. */

        if (!native_req(r, c, next, has) || !read_native_client(r, c, has ? 1u : 0u, next) ||
            !native_peers_intact(r, c) || !read_state(r, &actual) || actual != wanted ||
            !native_peers_intact(r, c) || !owns(r)) {
            /* a revoked resource cannot be written through a replacement owner. Restore this
             * client's prior vote only while ownership remains valid. */

            if (r->quarantined || !owns(r) || !native_peers_intact(r, c) ||
                !native_req(r, c, old_req, old_has) ||
                !read_native_client(r, c, old_has ? 1u : 0u, old_req) ||
                !native_peers_intact(r, c) || !read_state(r, &actual) || actual != previous ||
                !native_peers_intact(r, c) || !owns(r))
                r->quarantined = 1u;
            return 0;
        }
        c->req = has ? next : 0u;
        c->has_req = has ? 1u : 0u;
        r->effective = wanted;
        if (has)
            r->first_req_observed = 1u;
        return 1;
    }
    /* firmware npa_issue_sync_request_unconditional first-request rule: a request for zero
     * must not vanish just because old state was zero. */

    force = !r->first_req_observed || !c->has_req;
    if (force || wanted != previous) {
        if (!owns(r) || !r->io.write_effective(r->io.ctx, r->name, wanted) ||
            !read_state(r, &actual) || actual != wanted) {
            /* recover physical old value; a failed compensating action leaves res quarantined. */

            if (!owns(r) || !r->io.write_effective(r->io.ctx, r->name, previous) ||
                !read_state(r, &actual) || actual != previous || !owns(r))
                r->quarantined = 1u;
            return 0;
        }
    }
    /* final provider check is required even when the aggregate was unchanged, and even after a
     * successful hardware readback. If ownership vanished after a write, do not attempt a
     * compensating write against a controller that might belong to someone else. */

    if (!owns(r)) {
        r->quarantined = 1u;
        return 0;
    }
    c->req = has ? next : 0u;
    c->has_req = has ? 1u : 0u;
    r->effective = wanted;
    if (has)
        r->first_req_observed = 1u;
    return 1;
}

int scalar_issue(scalar_res_t* r, scalar_handle_t handle, uint32_t req)
{
    scalar_client_t* c = lookup(r, handle);
    if (!c)
        return 0;
    return transition(r, c, req, 1);
}

int scalar_complete(scalar_res_t* r, scalar_handle_t handle)
{
    scalar_client_t* c = lookup(r, handle);
    if (!c)
        return 0;
    if (!c->has_req) {
        uint32_t actual = UINT32_MAX;
        if (r->native_transport && !scalar_verify_owned(r))
            return 0;
        if (r->native_transport && !read_native_client(r, c, 0u, 0u)) {
            r->quarantined = 1u;
            return 0;
        }
        if (!read_state(r, &actual) || actual != r->effective || !owns(r)) {
            r->quarantined = 1u;
            return 0;
        }
        return 1;
    }
    return transition(r, c, 0u, 0);
}

int scalar_destroy(scalar_res_t* r, scalar_handle_t handle)
{
    scalar_client_t* c = lookup(r, handle);
    if (!c || (r->native_transport && !scalar_verify_owned(r)) || !scalar_complete(r, handle))
        return 0;
    if (r->native_transport) {
        uint32_t observed = UINT32_MAX;
        if (!owns(r) || !c->native_token || !r->io.destroy_native_client ||
            !native_peers_intact(r, c) ||
            !r->io.destroy_native_client(r->io.ctx, r->name, c->native_token) || !owns(r)) {
            r->quarantined = 1u;
            return 0;
        }
        /* destroy_native_client invalidates the remote token. cannot call ScalarVerifyOwned while
         * the now-destroyed client is published: that routine correctly expects every published
         * client to remain queryable. Instead independently verify the effective hardware state
         * and every OTHER client before clearing. */

        if (!native_peers_intact(r, c) || !read_state(r, &observed) || observed != r->effective ||
            !native_peers_intact(r, c) || !owns(r)) {
            r->quarantined = 1u;
            return 0;
        }
    }
    memset(c, 0, sizeof(*c));
    if (r->native_transport && !scalar_verify_owned(r))
        return 0;
    return 1;
}

int scalar_detach(scalar_res_t* r)
{
    uint32_t i, state = UINT32_MAX;
    if (!r || !r->active || r->quarantined)
        return 0;
    for (i = 0; i < SCALAR_MAX_CLIENTS; ++i)
        if (r->clients[i].exists)
            return 0;
    if (!read_state(r, &state) || state != r->baseline_effective ||
        r->effective != r->baseline_effective || !owns(r)) {
        r->quarantined = 1u;
        return 0;
    }
    {
        uint32_t epoch = r->generation;
        memset(r, 0, sizeof(*r));
        r->generation = epoch;
    }
    return 1;
}
