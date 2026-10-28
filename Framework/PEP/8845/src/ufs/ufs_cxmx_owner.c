#include "clock_target.h"
#include "ufs_cxmx_owner.h"
#include <string.h>

static const char* const names[2] = {UFS_CX_NAME, UFS_MX_NAME};
static const uint32_t floors[2] = {UFS_CX_LEVEL, UFS_MX_LEVEL};

static int live(const ufs_cxmx_owner_t* o)
{
    uint64_t epoch = 0;
    return o && o->bound && !o->quarantined && o->epoch &&
           o->io.owns_domain(o->io.ctx, clock_ufs_domain, o->epoch) &&
           o->io.owner_epoch(o->io.ctx, &epoch) && epoch == o->epoch;
}

static int query(const ufs_cxmx_owner_t* o, unsigned i, int want)
{
    uint32_t ce = 0, cv = 0, pe = 0, pv = 0;
    if (!live(o) || i >= 2 ||
        !o->io.read_client_vote(o->io.ctx, UFS_RAIL_CLIENT, names[i], &ce, &cv) ||
        !o->io.read_physical(o->io.ctx, names[i], &pe, &pv) || ce > 1u || pe > 1u)
        return 0;
    /* do NOT infer PEP client ownership from the physical aggregate: a firmware/client vote
     * can hold the rail on after PEP withdrawal. */

    if (want)
        return ce == 1u && cv >= floors[i] && pe == 1u && pv >= floors[i];
    return ce == 0u;
}

static int verify_votes(void* ctx, const char* d, uint64_t epoch, uint32_t held)
{
    ufs_cxmx_owner_t* o = (ufs_cxmx_owner_t*)ctx;
    return d && !strcmp(d, clock_ufs_domain) && o && epoch == o->epoch && held <= 1u && live(o) &&
           o->held == held && query(o, 0, held != 0u) && query(o, 1, held != 0u);
}

static int acquire(void* ctx, const char* d, uint64_t epoch)
{
    ufs_cxmx_owner_t* o = (ufs_cxmx_owner_t*)ctx;
    int first_changed = 0;
    if (!d || strcmp(d, clock_ufs_domain) || !o || epoch != o->epoch || !live(o))
        return 0;
    if (o->held)
        return verify_votes(o, d, epoch, 1);
    if (!verify_votes(o, d, epoch, 0))
        return 0;
    if (!o->io.vote(o->io.ctx, UFS_RAIL_CLIENT, names[0], floors[0], 1u)) {

        o->quarantined = 1u;
        return 0;
    }
    first_changed = 1;
    if (!query(o, 0, 1) || !live(o))
        goto rollback;
    if (!o->io.vote(o->io.ctx, UFS_RAIL_CLIENT, names[1], floors[1], 1u)) {
        o->quarantined = 1u;
        return 0;
    }
    if (!query(o, 1, 1) || !query(o, 0, 1) || !live(o))
        goto rollback;
    o->held = 1u;
    return 1;
rollback:
    /* the second request may already have reached hardware: withdraw it and both first/second
     * client requests only if every physical and client verification remains live. Any
     * ambiguity is sticky. */

    if (!live(o) || !o->io.withdraw(o->io.ctx, UFS_RAIL_CLIENT, names[1]) || !query(o, 1, 0) ||
        (first_changed &&
         (!o->io.withdraw(o->io.ctx, UFS_RAIL_CLIENT, names[0]) || !query(o, 0, 0))))
        o->quarantined = 1u;
    return 0;
}

static int release(void* ctx, const char* d, uint64_t epoch)
{
    ufs_cxmx_owner_t* o = (ufs_cxmx_owner_t*)ctx;
    if (!o || !d || strcmp(d, clock_ufs_domain) || epoch != o->epoch || !live(o))
        return 0;
    if (!o->held)
        return verify_votes(o, d, epoch, 0);
    if (!verify_votes(o, d, epoch, 1)) {
        o->quarantined = 1u;
        return 0;
    }
    /* never let VDD_CX/MX drop until the GDSCR executor's own independently verified
     * SW_COLLAPSE and HW_CONTROL sequencing has finished. */

    if (!o->io.withdraw(o->io.ctx, UFS_RAIL_CLIENT, names[1])) {
        o->quarantined = 1u;
        return 0;
    }
    if (!query(o, 1, 0)) {
        o->quarantined = 1u;
        return 0;
    }
    if (!o->io.withdraw(o->io.ctx, UFS_RAIL_CLIENT, names[0])) {
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

int ufs_cx_mx_bind(ufs_cxmx_owner_t* o, const ufs_cxmx_io_t* io, uint64_t epoch)
{
    ufs_cxmx_owner_t candidate;
    if (!o || !io || !epoch || o->bound || o->quarantined || !io->owns_domain || !io->owner_epoch ||
        !io->rail_resolved || !io->read_client_vote || !io->read_physical || !io->vote ||
        !io->withdraw)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.io = *io;
    candidate.epoch = epoch;
    candidate.bound = 1u;
    if (!live(&candidate) || !io->rail_resolved(io->ctx, names[0], floors[0]) ||
        !io->rail_resolved(io->ctx, names[1], floors[1]) || !query(&candidate, 0, 0) ||
        !query(&candidate, 1, 0))
        return 0;
    *o = candidate;
    return 1;
}

int ufs_cx_mx_rail_provider(ufs_cxmx_owner_t* o, fs_gdsc_rail_provider_t* p)
{
    fs_gdsc_rail_provider_t next;
    if (!o || !p || !live(o) || o->held || !verify_votes(o, clock_ufs_domain, o->epoch, 0))
        return 0;
    memset(&next, 0, sizeof(next));
    next.ctx = o;
    next.rail_mask = RAIL_VDD_CX | RAIL_VDD_MX;
    next.acquire_votes = acquire;
    next.verify_votes = verify_votes;
    next.release_votes = release;
    *p = next;
    return 1;
}

int ufs_cx_mx_retire(ufs_cxmx_owner_t* o)
{
    if (!o || !live(o) || o->held || !verify_votes(o, clock_ufs_domain, o->epoch, 0))
        return 0;
    memset(o, 0, sizeof(*o));
    return 1;
}
