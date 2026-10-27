/* map DRV2 within the Windows D0 lifetime. The mapping grants no TCS submission authority. */

#include <ntddk.h>
#include "rsc_d0_map_win.h"

static uint32_t read_reg(volatile unsigned char* base, uint32_t offset)
{
    return (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(base + offset));
}

int rsc_d0_map_open_win(rsc_d0_map_win_t* map, uint64_t pnp_generation,
                        const rsc_identity_t* expected)
{
    PHYSICAL_ADDRESS physical;
    rsc_hlos_geometry_t geometry = {0};
    volatile unsigned char* base;
    uint32_t first_id, first_config;
    uint32_t irq_before, irq_after, enable_before, enable_after;
    uint32_t status_before[MOLOKAI_WIN_AMC_CNT];
    uint32_t status_after[MOLOKAI_WIN_AMC_CNT];
    uint32_t i;
    uint32_t amc_bits = (1u << MOLOKAI_WIN_AMC_CNT) - 1u;
    if (!map || map->base || map->pnp_generation || !pnp_generation || !expected ||
        !expected->identity_matched || expected->tcs_cnt < MOLOKAI_WIN_TCS_TOTAL ||
        expected->commands_per_tcs != TCS_BATCH_MAX || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    physical.QuadPart = (LONGLONG)HLOS_RSC_DRV2_PHYS;
    base = (volatile unsigned char*)MmMapIoSpaceEx(physical, RSC_DRV2_BYTES,
                                                   PAGE_READWRITE | PAGE_NOCACHE);
    if (!base)
        return 0;
    first_id = read_reg(base, RSC_ID_OFFSET);
    first_config = read_reg(base, RSC_PARENTCHILD_CONFIG_OFFSET);
    /* pending IRQs belong to their prior owner: never W1C them or infer a grant. */

    irq_before = read_reg(base, TCS_IRQ_STATUS);
    enable_before = read_reg(base, TCS_IRQ_ENABLE);
    for (i = 0; i < MOLOKAI_WIN_AMC_CNT; ++i)
        status_before[i] = read_reg(base, TCS_STATUS(i));
    irq_after = read_reg(base, TCS_IRQ_STATUS);
    enable_after = read_reg(base, TCS_IRQ_ENABLE);
    for (i = 0; i < MOLOKAI_WIN_AMC_CNT; ++i)
        status_after[i] = read_reg(base, TCS_STATUS(i));
    if (first_id != expected->rsc_id || first_config != expected->driver_config ||
        (first_id & RSC_ID_MASK) != RSC_ID_POR ||
        !rpmh_tcs_check_hlos_geometry(first_config, MOLOKAI_WIN_TCS_TOTAL, TCS_BATCH_MAX,
                                      &geometry) ||
        geometry.tcs_cnt != expected->tcs_cnt ||
        geometry.commands_per_tcs != expected->commands_per_tcs ||
        read_reg(base, RSC_ID_OFFSET) != first_id ||
        read_reg(base, RSC_PARENTCHILD_CONFIG_OFFSET) != first_config || irq_before != irq_after ||
        enable_before != enable_after || (irq_before & amc_bits) != 0u ||
        irq_before == UINT32_MAX || enable_before == UINT32_MAX) {
        MmUnmapIoSpace((PVOID)base, RSC_DRV2_BYTES);
        return 0;
    }
    for (i = 0; i < MOLOKAI_WIN_AMC_CNT; ++i) {
        if (status_before[i] != status_after[i] || status_before[i] == UINT32_MAX ||
            !(status_after[i] & TCS_STATUS_IDLE)) {
            MmUnmapIoSpace((PVOID)base, RSC_DRV2_BYTES);
            return 0;
        }
    }
    /* the caller holds the d0/owner lifetime lock while publishing it to the backend. Zero TCS
     * grants still prohibit all physical writes. */

    map->bytes = RSC_DRV2_BYTES;
    map->id = first_id;
    map->config = first_config;
    map->pnp_generation = pnp_generation;
    map->base = base;
    return 1;
}

int rsc_d0_map_close_win(rsc_d0_map_win_t* map, uint64_t pnp_generation, int quiesced)
{
    if (!map || !map->base || !map->pnp_generation || map->pnp_generation != pnp_generation ||
        !quiesced || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    MmUnmapIoSpace((PVOID)map->base, map->bytes);
    map->base = 0;
    map->bytes = 0;
    map->pnp_generation = 0;
    map->id = 0;
    map->config = 0;
    return 1;
}
