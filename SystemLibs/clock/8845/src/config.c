#include "config.h"
#include "clock_target.h"
#include <string.h>

static const clock_config_rule_t* valid(const gcc_io_t* io, const char* name)
{
    const gcc_clock_t* clk;
    const clock_config_rule_t* rule = clock_config_rule(name, UFS_ICE_CONFIG_FORCE_ON);
    if (!io || !io->read32 || !io->write32 || !io->barrier || !rule)
        return 0;
    clk = gcc_find_req_clock(name);
    return clk && clk->kind == GCC_KIND_BRANCH && clk->cbcr_offset == rule->offset ? rule : NULL;
}

int gcc_ufs_ice_mem_core_get(const gcc_io_t* io, const char* name, uint32_t* enabled)
{
    uint32_t a, b;
    const clock_config_rule_t* rule = valid(io, name);
    if (!enabled || !rule || !io->read32(io->ctx, rule->offset, &a) ||
        !io->read32(io->ctx, rule->offset, &b) || ((a ^ b) & rule->mask))
        return 0;
    *enabled = (b & rule->mask) ? 1u : 0u;
    return 1;
}

gcc_config_result_t gcc_ufs_ice_mem_core_set(const gcc_io_t* io, const char* name, uint32_t enabled)
{
    uint32_t old, confirm, target, after1, after2;
    const clock_config_rule_t* rule = valid(io, name);
    if (enabled > 1u || !rule || !io->read32(io->ctx, rule->offset, &old) ||
        !io->read32(io->ctx, rule->offset, &confirm) || ((old ^ confirm) & rule->mask))
        return GCC_CONFIG_DECLINED;
    target = enabled ? (confirm | rule->mask) : (confirm & ~rule->mask);
    if ((confirm & rule->mask) == (target & rule->mask))
        return GCC_CONFIG_OK;
    /* a failed write can have reached hardware; quarantine the clock rather than claiming rollback.
     */

    if (!io->write32(io->ctx, rule->offset, target))
        return GCC_CONFIG_UNCERTAIN;
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, rule->offset, &after1) ||
        !io->read32(io->ctx, rule->offset, &after2) || ((after1 ^ after2) & rule->mask) ||
        ((after2 ^ target) & rule->mask))
        return GCC_CONFIG_UNCERTAIN;
    return GCC_CONFIG_OK;
}
