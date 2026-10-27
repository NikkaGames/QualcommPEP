/* firmware BOOT.MXF places the HLOS DRV2 at 0x18920000 / 0x10000. This probe DOES NOT
 * assume a PnP IRQ grants AMC or MMIO write ownership. */

#include <ntddk.h>
#include "oem_rsc_identity_win.h"

static uint32_t probe_read(volatile unsigned char* base, uint32_t offset)
{
    return (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(base + offset));
}

int rsc_read_only_identify_hlos_win(rsc_identity_t* out)
{
    rsc_identity_t local = {0};
    rsc_hlos_geometry_t geom = {0};
    PHYSICAL_ADDRESS phys;
    volatile unsigned char* regs;
    uint32_t first_id, first_config;
    uint32_t i;
    int valid = 0;
    if (!out)
        return 0;

    *out = local;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    phys.QuadPart = (LONGLONG)HLOS_RSC_DRV2_PHYS;
    regs =
        (volatile unsigned char*)MmMapIoSpaceEx(phys, RSC_DRV2_BYTES, PAGE_READONLY | PAGE_NOCACHE);
    if (!regs)
        return 0;
    first_id = probe_read(regs, RSC_ID_OFFSET);
    first_config = probe_read(regs, RSC_PARENTCHILD_CONFIG_OFFSET);
    if ((first_id & RSC_ID_MASK) != RSC_ID_POR ||
        !rpmh_tcs_check_hlos_geometry(first_config, MOLOKAI_WIN_TCS_TOTAL, TCS_BATCH_MAX, &geom))
        goto unmap;
    local.irq_enable = probe_read(regs, TCS_IRQ_ENABLE);
    local.irq_status = probe_read(regs, TCS_IRQ_STATUS);
    for (i = 0; i < MOLOKAI_WIN_AMC_CNT; i++)
        local.amc_status[i] = probe_read(regs, TCS_STATUS(i));
    /* a second or changing controller must never produce an accepted record. */

    if (probe_read(regs, RSC_ID_OFFSET) != first_id ||
        probe_read(regs, RSC_PARENTCHILD_CONFIG_OFFSET) != first_config)
        goto unmap;
    local.rsc_id = first_id;
    local.driver_config = first_config;
    local.tcs_cnt = geom.tcs_cnt;
    local.commands_per_tcs = geom.commands_per_tcs;
    local.identity_matched = 1;
    valid = 1;
unmap:
    MmUnmapIoSpace((PVOID)regs, RSC_DRV2_BYTES);
    if (valid)
        *out = local;
    return valid;
}
