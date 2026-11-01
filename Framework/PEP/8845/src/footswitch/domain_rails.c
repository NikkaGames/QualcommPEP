#include "domain_rails.h"
#include <string.h>

static int live(const domain_rails_t* o)
{
    uint64_t epoch = 0;
    return o && o->bound && !o->quarantined && o->epoch &&
           o->io.owns_domain(o->io.ctx, o->spec->domain, o->epoch) &&
           o->io.owner_epoch(o->io.ctx, &epoch) && epoch == o->epoch;
}

static int query(const domain_rails_t* o, unsigned i, int want)
{
    uint32_t ce = 0, cv = 0, pe = 0, pv = 0;
    if (!live(o) || i >= 2 ||
        !o->io.read_client_vote(o->io.ctx, o->spec->client, o->spec->names[i], &ce, &cv) ||
        !o->io.read_physical(o->io.ctx, o->spec->names[i], &pe, &pv) || ce > 1u || pe > 1u)
        return 0;

    if (want)
        return ce == 1u && cv >= o->spec->floors[i] && pe == 1u && pv >= o->spec->floors[i];
    return ce == 0u;
}

static int verify_votes(void* ctx, const char* d, uint64_t epoch, uint32_t held)
{
    domain_rails_t* o = (domain_rails_t*)ctx;
    return o && o->spec && d && !strcmp(d, o->spec->domain) && epoch == o->epoch && held <= 1u &&
           live(o) && o->held == held && query(o, 0, held != 0u) && query(o, 1, held != 0u);
}

static int acquire(void* ctx, const char* d, uint64_t epoch)
{
    domain_rails_t* o = (domain_rails_t*)ctx;
    int first_changed = 0;
    if (!o || !o->spec || !d || strcmp(d, o->spec->domain) || epoch != o->epoch || !live(o))
        return 0;
    if (o->held)
        return verify_votes(o, d, epoch, 1);
    if (!verify_votes(o, d, epoch, 0))
        return 0;
    if (!o->io.vote(o->io.ctx, o->spec->client, o->spec->names[0], o->spec->floors[0], 1u)) {

        o->quarantined = 1u;
        return 0;
    }
    first_changed = 1;
    if (!query(o, 0, 1) || !live(o))
        goto rollback;
    if (!o->io.vote(o->io.ctx, o->spec->client, o->spec->names[1], o->spec->floors[1], 1u)) {
        o->quarantined = 1u;
        return 0;
    }
    if (!query(o, 1, 1) || !query(o, 0, 1) || !live(o))
        goto rollback;
    o->held = 1u;
    return 1;
rollback:

    if (!live(o) || !o->io.withdraw(o->io.ctx, o->spec->client, o->spec->names[1]) ||
        !query(o, 1, 0) ||
        (first_changed &&
         (!o->io.withdraw(o->io.ctx, o->spec->client, o->spec->names[0]) || !query(o, 0, 0))))
        o->quarantined = 1u;
    return 0;
}

static int release(void* ctx, const char* d, uint64_t epoch)
{
    domain_rails_t* o = (domain_rails_t*)ctx;
    if (!o || !o->spec || !d || strcmp(d, o->spec->domain) || epoch != o->epoch || !live(o))
        return 0;
    if (!o->held)
        return verify_votes(o, d, epoch, 0);
    if (!verify_votes(o, d, epoch, 1)) {
        o->quarantined = 1u;
        return 0;
    }

    if (!o->io.withdraw(o->io.ctx, o->spec->client, o->spec->names[1])) {
        o->quarantined = 1u;
        return 0;
    }
    if (!query(o, 1, 0)) {
        o->quarantined = 1u;
        return 0;
    }
    if (!o->io.withdraw(o->io.ctx, o->spec->client, o->spec->names[0])) {
        o->quarantined = 1u;
        return 0;
    }
    if (!query(o, 0, 0) || !query(o, 1, 0) || !live(o)) {
        o->quarantined = 1u;
        return 0;
    }
    o->held = 0u;
    return 1;
}

int domain_rails_bind(domain_rails_t* o, const domain_rail_spec_t* spec, const domain_rail_io_t* io,
                      uint64_t epoch)
{
    domain_rails_t candidate;
    if (!o || !spec || !spec->domain || !spec->client || !spec->names[0] || !spec->names[1] ||
        !spec->floors[0] || !spec->floors[1] || !spec->mask || !io || !epoch || o->bound ||
        o->quarantined || !io->owns_domain || !io->owner_epoch || !io->rail_resolved ||
        !io->read_client_vote || !io->read_physical || !io->vote || !io->withdraw)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.io = *io;
    candidate.spec = spec;
    candidate.epoch = epoch;
    candidate.bound = 1u;
    if (!live(&candidate) || !io->rail_resolved(io->ctx, spec->names[0], spec->floors[0]) ||
        !io->rail_resolved(io->ctx, spec->names[1], spec->floors[1]) || !query(&candidate, 0, 0) ||
        !query(&candidate, 1, 0))
        return 0;
    *o = candidate;
    return 1;
}

int domain_rails_provider(domain_rails_t* o, fs_gdsc_rail_provider_t* p)
{
    fs_gdsc_rail_provider_t next;
    if (!o || !p || !live(o) || o->held || !verify_votes(o, o->spec->domain, o->epoch, 0))
        return 0;
    memset(&next, 0, sizeof(next));
    next.ctx = o;
    next.rail_mask = o->spec->mask;
    next.acquire_votes = acquire;
    next.verify_votes = verify_votes;
    next.release_votes = release;
    *p = next;
    return 1;
}

int domain_rails_retire(domain_rails_t* o)
{
    if (!live(o) || o->held || !verify_votes(o, o->spec->domain, o->epoch, 0u))
        return 0;
    memset(o, 0, sizeof(*o));
    return 1;
}
