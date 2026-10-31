#include "domains.h"
#include <string.h>

/* reference GDSCR addresses require validation against the mapped controller before use. */

const oem_power_domain_t oem_power_domains[OEM_POWER_DOMAIN_CNT] = {
    {"av1e_cc_core_gdsc", 0x20000u, UINT64_C(0x15224004)},
    {"cam_cc_bps_gdsc", 0x20001u, UINT64_C(0xADF0004)},
    {"cam_cc_ife_0_gdsc", 0x20002u, UINT64_C(0xADF1004)},
    {"cam_cc_ife_1_gdsc", 0x20003u, UINT64_C(0xADF2004)},
    {"cam_cc_ipe_0_gdsc", 0x20004u, UINT64_C(0xADF03BC)},
    {"cam_cc_titan_top_gdsc", 0x20005u, UINT64_C(0xADF3930)},
    {"disp_cc_mdss_core_gdsc", 0x20006u, UINT64_C(0xAF09000)},
    {"disp_cc_mdss_core_int2_gdsc", 0x20007u, UINT64_C(0xAF0B000)},
    {"eva_cc_mvs0_gdsc", 0x20008u, UINT64_C(0xABF8068)},
    {"eva_cc_mvs0c_gdsc", 0x20009u, UINT64_C(0xABF8034)},
    {"gcc_gpu_smmu_vote_gds", 0x2000Au, UINT64_C(0x17D03C)},
    {"gcc_hlos1_vote_aggre_noc_mmu_qtb1_gds", 0x2000Bu, UINT64_C(0x17D044)},
    {"gcc_hlos1_vote_aggre_noc_mmu_qtb2_gds", 0x2000Cu, UINT64_C(0x17D048)},
    {"gcc_hlos1_vote_aggre_noc_mmu_qtb3_gds", 0x2000Du, UINT64_C(0x18D080)},
    {"gcc_hlos1_vote_aggre_noc_mmu_qtb4_gds", 0x2000Eu, UINT64_C(0x18D084)},
    {"gcc_hlos1_vote_all_smmu_mmu_gds", 0x2000Fu, UINT64_C(0x17D064)},
    {"gcc_hlos1_vote_gpu_smmu_gds", 0x20010u, UINT64_C(0x17D03C)},
    {"gcc_hlos1_vote_lpass_qtb_gds", 0x20011u, UINT64_C(0x17D040)},
    {"gcc_hlos1_vote_mmnoc_mmu_qtb_hf01_gds", 0x20012u, UINT64_C(0x17D050)},
    {"gcc_hlos1_vote_mmnoc_mmu_qtb_hf23_gds", 0x20013u, UINT64_C(0x17D078)},
    {"gcc_hlos1_vote_mmnoc_mmu_qtb_sf_gds", 0x20014u, UINT64_C(0x17D054)},
    {"gcc_hlos1_vote_mmu_tcu_gds", 0x20015u, UINT64_C(0x17D068)},
    {"gcc_hlos1_vote_turing_mmu_qtb1_gds", 0x20016u, UINT64_C(0x18D060)},
    {"gcc_hlos1_vote_turing_mmu_qtb_gds", 0x20017u, UINT64_C(0x18D05C)},
    {"gcc_mmu_tcu_vote_gds", 0x20018u, UINT64_C(0x17D068)},
    {"gcc_pcie_0_tunnel_gdsc", 0x20019u, UINT64_C(0x1C8004)},
    {"gcc_pcie_1_tunnel_gdsc", 0x2001Au, UINT64_C(0x12E004)},
    {"gcc_pcie_2_tunnel_gdsc", 0x2001Bu, UINT64_C(0x1C0004)},
    {"gcc_pcie_3a_gdsc", 0x2001Cu, UINT64_C(0x1DC004)},
    {"gcc_pcie_3a_phy_gdsc", 0x2001Du, UINT64_C(0x16C004)},
    {"gcc_pcie_3b_gdsc", 0x2001Eu, UINT64_C(0x194004)},
    {"gcc_pcie_3b_phy_gdsc", 0x2001Fu, UINT64_C(0x175004)},
    {"gcc_pcie_4_gdsc", 0x20020u, UINT64_C(0x188004)},
    {"gcc_pcie_4_phy_gdsc", 0x20021u, UINT64_C(0x1D3004)},
    {"gcc_pcie_5_gdsc", 0x20022u, UINT64_C(0x1C3004)},
    {"gcc_pcie_5_phy_gdsc", 0x20023u, UINT64_C(0x1D2004)},
    {"gcc_pcie_6_gdsc", 0x20024u, UINT64_C(0x18A004)},
    {"gcc_pcie_6_phy_gdsc", 0x20025u, UINT64_C(0x1D4004)},
    {"gcc_ufs_phy_gdsc", 0x20026u, UINT64_C(0x177004)},
    {"gcc_usb20_prim_gdsc", 0x20027u, UINT64_C(0x1BC004)},
    {"gcc_usb30_mp_gdsc", 0x20028u, UINT64_C(0x19A010)},
    {"gcc_usb30_prim_gdsc", 0x20029u, UINT64_C(0x139004)},
    {"gcc_usb30_sec_gdsc", 0x2002Au, UINT64_C(0x1E2010)},
    {"gcc_usb30_tert_gdsc", 0x2002Bu, UINT64_C(0x1E1010)},
    {"gcc_usb3_mp_ss0_phy_gdsc", 0x2002Cu, UINT64_C(0x15400C)},
    {"gcc_usb3_mp_ss1_phy_gdsc", 0x2002Du, UINT64_C(0x15402C)},
    {"gcc_usb4_0_gdsc", 0x2002Eu, UINT64_C(0x12B008)},
    {"gcc_usb4_1_gdsc", 0x2002Fu, UINT64_C(0x12D008)},
    {"gcc_usb4_2_gdsc", 0x20030u, UINT64_C(0x1E0008)},
    {"gcc_usb_0_phy_gdsc", 0x20031u, UINT64_C(0x1DB024)},
    {"gcc_usb_1_phy_gdsc", 0x20032u, UINT64_C(0x12C024)},
    {"gcc_usb_2_phy_gdsc", 0x20033u, UINT64_C(0x150018)},
    {"gpu_cc_cx_gdsc", 0x20034u, UINT64_C(0x3DC5080)},
    {"gpu_cc_gpu_smmu_vote_gds", 0x20035u, UINT64_C(0x3D97004)},
    {"gx_clkctl_gx_gdsc", 0x20036u, UINT64_C(0x3D68024)},
    {"video_cc_mvs0_gdsc", 0x20037u, UINT64_C(0xAAF8068)},
    {"video_cc_mvs0c_gdsc", 0x20038u, UINT64_C(0xAAF8034)},
    {"video_cc_mvs1_gdsc", 0x20039u, UINT64_C(0xAAF80A4)},
};

int oem_get_power_domain(const char* name, oem_power_domain_t* out)
{
    uint32_t i;
    if (!name || !out || !name[0])
        return 0;
    for (i = 0u; i < OEM_POWER_DOMAIN_CNT; ++i) {
        if (!strcmp(name, oem_power_domains[i].name)) {
            *out = oem_power_domains[i];
            return 1;
        }
    }
    return 0;
}
