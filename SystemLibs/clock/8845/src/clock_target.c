#include "clock_target.h"
#include "config.h"
#include <string.h>

const char clock_ufs_domain[] = "gcc_ufs_phy_gdsc";
const char clock_ufs_ice[] = "gcc_ufs_phy_ice_core_clk";
const char clock_ufs_enable[] = "/fs/gcc_ufs_phy_gdsc/enable";

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
