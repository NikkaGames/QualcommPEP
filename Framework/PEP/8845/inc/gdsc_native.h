#ifndef FOOTSWITCH_GDSC_NATIVE_H
#define FOOTSWITCH_GDSC_NATIVE_H
/* physical HAL for the src-owned FOOTSWITCH NPA nodes. */

#include <stdint.h>
#include "gcc.h"
#include "nodes.h"
#include "gdsc_phys.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GDSCR_HW_CONTROL 0x2u

/* both acquire and verify calls must transact the exact SOURCE-OWNED pair, including the
 * voltage floor; the mask is admission metadata, not evidence of physical provider
 * ownership. Hardware-control action 3 also needs a rail vote before the UFS GDSCR
 * HW_CONTROL bit is set. */

#define RAIL_VDD_CX 0x01u
#define RAIL_VDD_MM 0x08u
#define RAIL_VDD_MX 0x10u

typedef struct fs_gdsc_rail_provider {
    void* ctx;
    uint32_t rail_mask;
    int (*acquire_votes)(void*, const char*, uint64_t);
    int (*verify_votes)(void*, const char*, uint64_t, uint32_t);
    int (*release_votes)(void*, const char*, uint64_t);
} fs_gdsc_rail_provider_t;

typedef struct fs_gdsc_driver {
    gcc_io_t io;
    const gcc_clock_t* clock;
    gcc_clock_t local_clock;
    fs_gdsc_rail_provider_t rails;
    uint8_t requires_rails;
    uint8_t rails_held;
    void* owner_ctx;
    int (*owns)(void* owner_ctx, const char* domain, uint64_t epoch);
    uint64_t epoch;
    uint32_t domain_id;
    uint32_t effective_action;
    uint32_t effective_hw_action;
    char domain[FS_NAME_BYTES];
    fs_nodes_t npa;
    uint8_t bound;
    uint8_t quarantined;
} fs_gdsc_driver_t;

/* the supplied DomainId MUST equal the table-derived clock BSP ID (0x20000 |
 * aPowerDomains[] idx). HAL GDSCR physical address must match the Molokai GCC sequence. */

int fs_gdsc_init(fs_gdsc_driver_t* driver, const gcc_io_t* io, const char* domain,
                 uint32_t domain_id, void* owner_ctx, int (*owns)(void*, const char*, uint64_t),
                 uint64_t epoch);
/* they are NOT the original Qualcomm NPA runtime ABI. */

/* the window-only API cannot operate UFS or display domains without their rail dependencies. */

int fs_gdsc_init_window(fs_gdsc_driver_t* driver, const gcc_io_t* io, const char* domain,
                        uint32_t domain_id, uint64_t controller_physical, uint32_t controller_bytes,
                        void* owner_ctx, int (*owns)(void*, const char*, uint64_t), uint64_t epoch);
/* acquire and verify UFS CX/MX or display MM/MX before enabling the domain. Release rails only
 * after software and hardware-control requests are disabled; retain ownership after an uncertain
 * failure. */

int fs_gdsc_init_window_with_rails(fs_gdsc_driver_t* driver, const gcc_io_t* io, const char* domain,
                                   uint32_t domain_id, uint64_t controller_physical,
                                   uint32_t controller_bytes, void* owner_ctx,
                                   int (*owns)(void*, const char*, uint64_t), uint64_t epoch,
                                   const fs_gdsc_rail_provider_t* rails);
int fs_gdsc_provider(fs_gdsc_driver_t* driver, fs_factory_provider_t* provider);
int fs_gdsc_read(fs_gdsc_driver_t* driver, uint32_t family, uint32_t* action);
#ifdef __cplusplus
}
#endif
#endif
