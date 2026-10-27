/* MmMapIoSpaceEx transport requires a pinned D0/IRQ/TCS lease and completed drain. */

#include <ntddk.h>
#include "oem_direct_rsc_map_win.h"

static int win_owns(void* ctx, uint64_t epoch)
{
    oem_rsc_win_map_t* w = (oem_rsc_win_map_t*)ctx;
    return w && w->lease && w->interrupt && w->epoch == epoch &&
           oem_d0_lease_can_use(w->lease, w->interrupt, epoch, D0_RIGHT_ALL);
}

static void* win_map(void* ctx, uint64_t phys, uint32_t bytes)
{
    PHYSICAL_ADDRESS pa;
    (void)ctx;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || phys != OEM_RSC_DIRECT_BASE ||
        bytes != OEM_RSC_DIRECT_BYTES)
        return NULL;
    pa.QuadPart = (LONGLONG)phys;
    return MmMapIoSpaceEx(pa, bytes, PAGE_READWRITE | PAGE_NOCACHE);
}

static void win_unmap(void* ctx, void* va, uint32_t bytes)
{
    (void)ctx;
    if (va && bytes == OEM_RSC_DIRECT_BYTES && KeGetCurrentIrql() == PASSIVE_LEVEL)
        MmUnmapIoSpace(va, bytes);
    /* caller contract: mapped window never released at elevated IRQL. */
}

static int win_quiesced(void* ctx, uint64_t epoch)
{
    oem_rsc_win_map_t* w = (oem_rsc_win_map_t*)ctx;
    return w && w->epoch == epoch && w->drain && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           w->drain(w->drain_ctx, epoch) == 1;
}

int oem_rsc_win_map(oem_rsc_win_map_t* w, oem_pnp_d0_lease_t* lease, void* irq, uint64_t epoch,
                    void* drain_ctx, int (*drain)(void*, uint64_t))
{
    oem_rsc_map_io_t io;
    if (!w || !lease || !irq || !epoch || !drain || w->state.phase != OEM_RSC_MAP_COLD ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    w->lease = lease;
    w->interrupt = irq;
    w->epoch = epoch;
    w->drain_ctx = drain_ctx;
    w->drain = drain;
    io.ctx = w;
    io.owns = win_owns;
    io.map = win_map;
    io.unmap = win_unmap;
    io.quiesced = win_quiesced;
    return oem_direct_rsc_map_acquire(&w->state, &io, epoch);
}

int oem_rsc_win_release(oem_rsc_win_map_t* w)
{
    if (!w || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    oem_direct_rsc_map_close(&w->state);
    return oem_direct_rsc_map_release(&w->state);
}
