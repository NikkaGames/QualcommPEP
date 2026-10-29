#include "8845_acpi_resources.h"
#include "molokai_catalog.h"

/* dts identity is optional and must not be assumed to be a vote address. */

static int eq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

const ppp_res_t ppp_catalog[] = {
    {"PPP_RESOURCE_ID_BUCK_BOOST1_B", 0, PPP_BUCK_BOOST, 0u, 1u},
    {"PPP_RESOURCE_ID_BUCK_BOOST2_B", 0, PPP_BUCK_BOOST, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK1_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK2_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK3_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK4_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK6_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_CLK8_A", 0, PPP_PMIC_CLOCK, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO10_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO11_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO12_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO13_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO14_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO15_B", "pm_humu_l15", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO16_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO17_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO1_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO1_D", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO1_F", "pm8550ve_f_l1", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO1_I0", "pm8550ve_i_l1", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO1_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO1_M", "pm8010_m_l1", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO2_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO2_D", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO2_F", "pm8550ve_f_l2", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO2_G", "pm8550ve_g_l2", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO2_I0", "pm8550ve_i_l2", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO2_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO2_M", "pm8010_m_l2", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO3_D", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO3_F", "pm8550ve_f_l3", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO3_I0", "pm8550ve_i_l3", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO3_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO3_M", "pm8010_m_l3", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO4_B", "pm_humu_l4", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO4_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO4_M", "pm8010_m_l4", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO5_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO5_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO5_M", "pm8010_m_l5", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO6_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO6_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO6_M", "pm8010_m_l6", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO7_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO7_K", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO7_M", "pm8010_m_l7", PPP_LDO, 1u, 1u},
    {"PPP_RESOURCE_ID_LDO8_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_LDO9_B", 0, PPP_LDO, 0u, 1u},
    {"PPP_RESOURCE_ID_SMPS1_D", "pm8550ve_d_s1", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS2_D", "pm8550ve_d_s2", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS3_D", "pm8550ve_d_s3", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS3_G", "pm8550ve_g_s3", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS4_D", "pm8550ve_d_s4", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS4_F", "pm8550ve_f_s4", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS4_G", "pm8550ve_g_s4", PPP_SMPS, 1u, 1u},
    {"PPP_RESOURCE_ID_SMPS5_F", "pm8550ve_f_s5", PPP_SMPS, 1u, 1u},
};
const unsigned int ppp_name_cnt = (unsigned int)(sizeof(ppp_catalog) / sizeof(ppp_catalog[0]));

const ppp_res_t* lookup_ppp_name(const char* n)
{
    unsigned int i;
    for (i = 0; i < ppp_name_cnt; ++i)
        if (eq(n, ppp_catalog[i].name))
            return &ppp_catalog[i];
    return 0;
}

const gdsc_res_t gdscs[] = {
    {"gcc_ufs_phy_gdsc", 0x0B76D452u, 0x67004u, GDSC_RAIL_CX | GDSC_RAIL_MX}};
const unsigned int gdsc_cnt = (unsigned int)(sizeof(gdscs) / sizeof(gdscs[0]));
/* reviewed ARC client resource paths; provenance is recorded in the source map. */

const npa_res_t npa_resources[] = {
    {"/arc/client/rail_cx", 1u, "cx.lvl"},     {"/arc/client/rail_gx", 1u, "gfx.lvl"},
    {"/arc/client/rail_lpicx", 1u, "lcx.lvl"}, {"/arc/client/rail_lpimx", 1u, "lmx.lvl"},
    {"/arc/client/rail_mmcx", 1u, "mmcx.lvl"}, {"/arc/client/rail_mx", 1u, "mx.lvl"},
    {"/arc/client/rail_mxa", 1u, 0},           {"/arc/client/rail_mxc", 1u, "mxc.lvl"},
    {"/arc/client/rail_nsp", 1u, "nsp.lvl"},   {"/arc/client/rail_xo", 1u, "xo.lvl"},
};
const unsigned int npa_resource_cnt =
    (unsigned int)(sizeof(npa_resources) / sizeof(npa_resources[0]));

const npa_res_t* lookup_npa_res(const char* p)
{
    unsigned int i;
    for (i = 0; i < npa_resource_cnt; ++i)
        if (eq(p, npa_resources[i].path))
            return &npa_resources[i];
    return 0;
}

const gdsc_res_t* lookup_gdsc(const char* n)
{
    unsigned int i;
    for (i = 0; i < gdsc_cnt; ++i)
        if (eq(n, gdscs[i].name))
            return &gdscs[i];
    return 0;
}

int is_unverified_clock_alias(const char* name)
{
    return eq(name, "gcc_disp_xo_clk");
}

acpi_res_type_t classify_acpi_res(const char* name)
{
    if (!name)
        return ACPI_RES_UNKNOWN;
    if (lookup_ppp_name(name))
        return ACPI_RES_PPP;
    if (lookup_gdsc(name))
        return ACPI_RES_GDSC;
    if (lookup_clock(name))
        return ACPI_RES_CLOCK;
    if (is_unverified_clock_alias(name))
        return ACPI_RES_UNVERIFIED_ALIAS;
    return ACPI_RES_UNKNOWN;
}
