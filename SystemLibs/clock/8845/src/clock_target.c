#include "clock_target.h"
#include "config.h"
#include "gcc.h"
#include <string.h>

const char clock_ufs_domain[] = "gcc_ufs_phy_gdsc";
const char clock_ufs_ice[] = "gcc_ufs_phy_ice_core_clk";
const char clock_ufs_enable[] = "/fs/gcc_ufs_phy_gdsc/enable";

static const clock_window_t windows[CLOCK_CTL_COUNT] = {
    {GCC_WINDOW_PHYS, 0u, GCC_WINDOW_BYTES},
    /* only the HLOS SMMU vote register is mapped here. */
    {UINT64_C(0x03D97000), 0x7000u, 4u},
};

const clock_window_t* clock_window_get(uint32_t controller)
{
    return controller < CLOCK_CTL_COUNT ? &windows[controller] : NULL;
}

int clock_window_offset(uint32_t controller, uint32_t offset, uint32_t* relative)
{
    const clock_window_t* window = clock_window_get(controller);
    if (!window || !relative || window->bytes < 4u || (offset & 3u) || offset < window->first ||
        offset - window->first > window->bytes - 4u)
        return 0;
    *relative = offset - window->first;
    return 1;
}

static const clock_config_rule_t config_rules[] = {
    {clock_ufs_ice, UFS_ICE_CONFIG_FORCE_ON, UFS_ICE_CORE_CBCR, UFS_ICE_FORCE_MEM_CORE},
};

static const char* const probe_clocks[] = {
    "gcc_qupv3_wrap3_core_clk",
    "gcc_qupv3_wrap3_core_2x_clk",
    "gcc_qupv3_wrap_3_m_ahb_clk",
    "gcc_qupv3_wrap_3_s_ahb_clk",
};

const clock_config_rule_t* clock_config_rule(const char* name, uint32_t selector)
{
    unsigned i;
    if (!name)
        return NULL;
    for (i = 0u; i < sizeof(config_rules) / sizeof(config_rules[0]); ++i)
        if (config_rules[i].selector == selector && !strcmp(config_rules[i].name, name))
            return &config_rules[i];
    return NULL;
}

const char* clock_probe_name(uint32_t index)
{
    return index < sizeof(probe_clocks) / sizeof(probe_clocks[0]) ? probe_clocks[index] : NULL;
}
