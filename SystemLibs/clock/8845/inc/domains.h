#ifndef OEM_CLOCK_DOMAINS_H
#define OEM_CLOCK_DOMAINS_H
/* native HAL IDs are distinct from register offsets and table indices.
 * resolving an ID does not establish execution ownership. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_GDSC_TYPE 0x00020000u
#define OEM_POWER_DOMAIN_CNT 58u
#define OEM_UFS_PHY_DOMAIN_ID 0x00020026u

typedef struct oem_power_domain {
    const char* name;
    uint32_t id;
    uint64_t hal_gdscr_address;
} oem_power_domain_t;

extern const oem_power_domain_t oem_power_domains[OEM_POWER_DOMAIN_CNT];

int oem_get_power_domain(const char* name, oem_power_domain_t* out);
#ifdef __cplusplus
}
#endif
#endif
