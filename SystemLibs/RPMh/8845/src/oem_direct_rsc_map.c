#include "oem_direct_rsc_map.h"
#include <string.h>

static int owner_ok(const oem_direct_rsc_map_t* s)
{
    return s->io.owns && s->io.owns(s->io.ctx, s->owner_epoch) == 1 &&
           s->io.owns(s->io.ctx, s->owner_epoch) == 1;
}

int oem_direct_rsc_map_acquire(oem_direct_rsc_map_t* s, const oem_rsc_map_io_t* io, uint64_t epoch)
{
    void* mapped;
    if (!s || s->phase != OEM_RSC_MAP_COLD || s->mapped || !io || !io->owns || !io->map ||
        !io->unmap || !io->quiesced || !epoch)
        return 0;
    s->io = *io;
    s->owner_epoch = epoch;
    if (!owner_ok(s)) {
        s->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }
    mapped = io->map(io->ctx, OEM_RSC_DIRECT_BASE, OEM_RSC_DIRECT_BYTES);
    if (!mapped) {
        s->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }

    if (!owner_ok(s)) {
        io->unmap(io->ctx, mapped, OEM_RSC_DIRECT_BYTES);
        s->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }
    s->mapped = mapped;
    s->phase = OEM_RSC_MAP_LIVE;
    return 1;
}

void* oem_direct_rsc_map_borrow(oem_direct_rsc_map_t* s, uint64_t epoch)
{
    if (!s || s->phase != OEM_RSC_MAP_LIVE || !s->mapped || epoch != s->owner_epoch ||
        s->borrowers == UINT32_MAX)
        return 0;
    if (!owner_ok(s)) {
        s->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }
    ++s->borrowers;
    return s->mapped;
}

int oem_direct_rsc_map_return(oem_direct_rsc_map_t* s, uint64_t epoch, void* mapped)
{
    if (!s || !mapped || epoch != s->owner_epoch || mapped != s->mapped || !s->borrowers ||
        s->phase == OEM_RSC_MAP_RELEASED)
        return 0;
    --s->borrowers;
    return 1;
}

void oem_direct_rsc_map_close(oem_direct_rsc_map_t* s)
{
    if (s && (s->phase == OEM_RSC_MAP_LIVE || s->phase == OEM_RSC_MAP_FAULT))
        s->phase = OEM_RSC_MAP_CLOSING;
}

int oem_direct_rsc_map_release(oem_direct_rsc_map_t* s)
{
    if (!s || s->phase != OEM_RSC_MAP_CLOSING || s->borrowers || !s->io.quiesced ||
        s->io.quiesced(s->io.ctx, s->owner_epoch) != 1 ||
        s->io.quiesced(s->io.ctx, s->owner_epoch) != 1)
        return 0;
    if (s->mapped)
        s->io.unmap(s->io.ctx, s->mapped, OEM_RSC_DIRECT_BYTES);
    s->mapped = 0;
    s->phase = OEM_RSC_MAP_RELEASED;
    memset(&s->io, 0, sizeof(s->io));
    return 1;
}
