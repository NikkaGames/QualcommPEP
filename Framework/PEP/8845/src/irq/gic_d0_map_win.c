/* mapping physical pages is not permission to mutate GICD or RSC. Do not substitute PnP
 * IRQ37 for RPMh TCS grants. */

#include <ntddk.h>
#include "gic_d0_map_win.h"

static uint32_t gic_read(volatile unsigned char* base, uint32_t offset)
{
    return (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(base + offset));
}

int gic_d0_observe_win(gic_d0_map_win_t* map, uint64_t pnp_generation)
{
    PHYSICAL_ADDRESS pa;
    volatile unsigned char* base;
    uint32_t type0, type1, iidr0, iidr1, irq0, irq1;
    if (!map || map->base || map->pnp_generation || !pnp_generation ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    pa.QuadPart = (LONGLONG)GICD_PHYS;
    base = (volatile unsigned char*)MmMapIoSpaceEx(pa, GICD_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (!base)
        return 0;
    type0 = gic_read(base, GICD_TYPER);
    iidr0 = gic_read(base, GICD_IIDR);
    irq0 = gic_read(base, GICD_ISENABLER1);
    type1 = gic_read(base, GICD_TYPER);
    iidr1 = gic_read(base, GICD_IIDR);
    irq1 = gic_read(base, GICD_ISENABLER1);
    /* readback may race HAL's mask changes; refuse inconsistent snapshots. */

    if (type0 != type1 || iidr0 != iidr1 || type0 == UINT32_MAX || iidr0 == UINT32_MAX ||
        (type0 & 0x1fu) < 1u || ((irq0 ^ irq1) & GICD_INTID37_BIT) != 0u) {
        MmUnmapIoSpace((PVOID)base, GICD_BYTES);
        return 0;
    }
    map->bytes = GICD_BYTES;
    map->pnp_generation = pnp_generation;
    map->typer = type0;
    map->iidr = iidr0;
    map->irq37_enabled = (irq1 & GICD_INTID37_BIT) != 0u;
    map->base = base;
    return 1;
}

int gic_d0_close_win(gic_d0_map_win_t* map, uint64_t pnp_generation, int quiesced)
{
    if (!map || !map->base || !pnp_generation || map->pnp_generation != pnp_generation ||
        !quiesced || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    MmUnmapIoSpace((PVOID)map->base, map->bytes);
    map->base = 0;
    map->bytes = 0;
    map->pnp_generation = 0;
    map->typer = 0;
    map->iidr = 0;
    map->irq37_enabled = 0;
    return 1;
}
