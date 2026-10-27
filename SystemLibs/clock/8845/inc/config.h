#ifndef GCC_CONFIG_H
#define GCC_CONFIG_H
#include "gcc.h"
#ifdef __cplusplus
extern "C" {
#endif
/* firmware HALclkHWIO.h: GCC_CLK_CTL_REG base offset 0x10000, UFS_PHY_ICE_CORE_CBCR offset
 * 0x6707c => 0x7707c. */

#define UFS_ICE_CORE_CBCR 0x0007707Cu
#define UFS_ICE_FORCE_MEM_CORE 0x00004000u
#define UFS_ICE_CONFIG_FORCE_ON 18u

typedef enum gcc_config_result {
    GCC_CONFIG_OK = 0,
    GCC_CONFIG_DECLINED = 1, /* no hardware write attempted. */
    GCC_CONFIG_UNCERTAIN = 2
} gcc_config_result_t;

/* requires an externally established, exclusive GCC MMIO owner. Get reads the physical bit
 * twice, with no writes. */

int gcc_ufs_ice_mem_core_get(const gcc_io_t* io, const char* name, uint32_t* enabled);
gcc_config_result_t gcc_ufs_ice_mem_core_set(const gcc_io_t* io, const char* name,
                                             uint32_t enabled);
#ifdef __cplusplus
}
#endif
#endif
