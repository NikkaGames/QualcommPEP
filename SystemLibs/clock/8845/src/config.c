#include "config.h"
#include <string.h>

static int valid(const gcc_io_t* io, const char* name)
{
    const gcc_clock_t* clk;
    if (!io || !io->read32 || !io->write32 || !io->barrier || !name ||
        strcmp(name, "gcc_ufs_phy_ice_core_clk") != 0)
        return 0;
    clk = gcc_find_req_clock(name);
    return clk && clk->kind == GCC_KIND_BRANCH && clk->cbcr_offset == UFS_ICE_CORE_CBCR;
}

int gcc_ufs_ice_mem_core_get(const gcc_io_t* io, const char* name, uint32_t* enabled)
{
    uint32_t a, b;
    if (!enabled || !valid(io, name) || !io->read32(io->ctx, UFS_ICE_CORE_CBCR, &a) ||
        !io->read32(io->ctx, UFS_ICE_CORE_CBCR, &b) || ((a ^ b) & UFS_ICE_FORCE_MEM_CORE))
        return 0;
    *enabled = (b & UFS_ICE_FORCE_MEM_CORE) ? 1u : 0u;
    return 1;
}

gcc_config_result_t gcc_ufs_ice_mem_core_set(const gcc_io_t* io, const char* name, uint32_t enabled)
{
    uint32_t old, confirm, target, after1, after2;
    if (enabled > 1u || !valid(io, name) || !io->read32(io->ctx, UFS_ICE_CORE_CBCR, &old) ||
        !io->read32(io->ctx, UFS_ICE_CORE_CBCR, &confirm) ||
        ((old ^ confirm) & UFS_ICE_FORCE_MEM_CORE))
        return GCC_CONFIG_DECLINED;
    target = enabled ? (confirm | UFS_ICE_FORCE_MEM_CORE) : (confirm & ~UFS_ICE_FORCE_MEM_CORE);
    if ((confirm & UFS_ICE_FORCE_MEM_CORE) == (target & UFS_ICE_FORCE_MEM_CORE))
        return GCC_CONFIG_OK;
    /* a failed write can have reached hardware; quarantine the clock rather than claiming rollback.
     */

    if (!io->write32(io->ctx, UFS_ICE_CORE_CBCR, target))
        return GCC_CONFIG_UNCERTAIN;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, UFS_ICE_CORE_CBCR, &after1) ||
        !io->read32(io->ctx, UFS_ICE_CORE_CBCR, &after2) ||
        ((after1 ^ after2) & UFS_ICE_FORCE_MEM_CORE) ||
        ((after2 ^ target) & UFS_ICE_FORCE_MEM_CORE))
        return GCC_CONFIG_UNCERTAIN;
    return GCC_CONFIG_OK;
}
