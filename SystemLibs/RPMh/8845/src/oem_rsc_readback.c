#include "oem_rsc_readback.h"
#include <string.h>

int oem_rsc_readback(oem_direct_rsc_map_t* s, uint64_t epoch, const oem_rsc_read_io_t* io,
                     oem_rsc_snapshot_t* out)
{
    volatile void* base;
    uint32_t status1 = 0, status2 = 0, config1 = 0, config2 = 0;
    int good = 0;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!s || !out || !io || !io->read32 || !epoch || s->phase != OEM_RSC_MAP_LIVE ||
        epoch != s->owner_epoch || !s->io.owns)
        return 0;
    base = (volatile void*)oem_direct_rsc_map_borrow(s, epoch);
    if (!base)
        return 0;
    /* one external lock protects this entire operation, including the authoritative proof and
     * map borrower accounting. */

    if (s->io.owns(s->io.ctx, epoch) != 1 ||
        io->read32(io->ctx, base, OEM_RSC_STATUS_OFFSET, &status1) != 1 ||
        io->read32(io->ctx, base, OEM_RSC_STATUS_OFFSET, &status2) != 1 || status1 != status2 ||
        s->io.owns(s->io.ctx, epoch) != 1 ||
        io->read32(io->ctx, base, OEM_RSC_CONFIG_OFFSET, &config1) != 1 ||
        io->read32(io->ctx, base, OEM_RSC_CONFIG_OFFSET, &config2) != 1 || config1 != config2 ||
        s->io.owns(s->io.ctx, epoch) != 1 || status1 == 0 || status1 == UINT32_MAX ||
        config1 == UINT32_MAX) {
        good = 0;
    } else {
        good = 1;
    }

    if (!oem_direct_rsc_map_return(s, epoch, (void*)base))
        good = 0;
    if (!good) {
        s->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }
    out->version = OEM_RSC_SNAPSHOT_VERSION;
    out->raw_status0_c = status1;
    out->raw_config_d8 = config1;
    out->oem_status_top5 = status1 >> 27;
    out->oem_status_bits12_to17 = (status1 >> 12) & 0x3fu;
    out->owner_epoch = epoch;
    return 1;
}
