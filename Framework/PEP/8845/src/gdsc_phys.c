#include "gdsc_phys.h"
#include "gcc.h"
#include "domains.h"
#include <string.h>
static const molokai_gdsc_location_t verified[] = {
    {"gcc_ufs_phy_gdsc", OEM_UFS_PHY_DOMAIN_ID, GCC_WINDOW_PHYS, GCC_WINDOW_BYTES, 0x77004u,
     UINT64_C(0x00177004)},
    {"disp_cc_mdss_core_gdsc", 0x00020006u, DISP_CC_PHYS, DISP_CC_BYTES, DISP_CORE_GDSCR_OFFSET,
     DISP_CORE_GDSCR_PHYS}};

int molokai_gdsc_location(const char* domain, molokai_gdsc_location_t* out)
{
    unsigned i;
    oem_power_domain_t oem;
    if (!domain || !*domain || !out)
        return 0;
    for (i = 0u; i < sizeof(verified) / sizeof(verified[0]); ++i) {
        const molokai_gdsc_location_t* p = &verified[i];
        if (strcmp(domain, p->domain))
            continue;
        if (!oem_get_power_domain(domain, &oem) || oem.id != p->legacy8480_clock_id ||
            !p->controller_bytes || p->gdscr_offset > p->controller_bytes - 4u ||
            p->gdscr_physical != p->controller_physical + (uint64_t)p->gdscr_offset)
            return 0;
        /* signed SC8480 HAL addresses are not a Molokai physical map. */

        *out = *p;
        return 1;
    }
    return 0;
}
