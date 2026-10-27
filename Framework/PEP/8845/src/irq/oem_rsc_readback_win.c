#include <ntddk.h>
#include <stdint.h>
#include <string.h>
#include "oem_rsc_readback_win.h"

static int win_read32(void* ctx, volatile void* base, uint32_t offset, uint32_t* value)
{
    oem_rsc_win_map_t* w = (oem_rsc_win_map_t*)ctx;
    if (!w || !base || !value || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        (offset != OEM_RSC_STATUS_OFFSET && offset != OEM_RSC_CONFIG_OFFSET) ||
        (offset & 3u) != 0 || offset > OEM_RSC_DIRECT_BYTES - sizeof(uint32_t))
        return 0;
    /* read_REGISTER_ULONG supplies appropriate MMIO access ordering; never use ordinary
     * pointer dereference for a physical register. */

    *value =
        (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)((volatile unsigned char*)base + offset));
    return 1;
}

int oem_rsc_win_readback(oem_rsc_win_map_t* w, oem_rsc_snapshot_t* out)
{
    oem_rsc_read_io_t io;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!w || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    io.ctx = w;
    io.read32 = win_read32;
    return oem_rsc_readback(&w->state, w->epoch, &io, out);
}
