#ifndef OEM_RSC_READBACK_H
#define OEM_RSC_READBACK_H
/* not hardware-ownership detection or a dispatch grant. The host holds the SAME
 * PASSIVE_LEVEL nonrecursive lifecycle lock used for map, d0 lease, quiescence, borrow and
 * release operations. */

#include "oem_direct_rsc_map.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_RSC_STATUS_OFFSET 0x0cu
#define OEM_RSC_CONFIG_OFFSET 0xd8u
#define OEM_RSC_SNAPSHOT_VERSION 1u

typedef struct oem_rsc_read_io {
    void* ctx;
    /* never use a cached shadow, guest-emulated owner proof or request-side value. */

    int (*read32)(void* ctx, volatile void* base, uint32_t offset, uint32_t* value);
} oem_rsc_read_io_t;

typedef struct oem_rsc_snapshot {
    uint32_t version;
    uint32_t raw_status0_c;
    uint32_t raw_config_d8;
    uint32_t oem_status_top5;
    uint32_t oem_status_bits12_to17;
    uint64_t owner_epoch;
} oem_rsc_snapshot_t;

/* double physical reads of both registers; owner independently checked before/after reads;
 * fails closed with zero output and quarantines drift. No register writes, MMIO ownership
 * grants or implicit dispatch activation. */

int oem_rsc_readback(oem_direct_rsc_map_t* mapping, uint64_t epoch, const oem_rsc_read_io_t* io,
                     oem_rsc_snapshot_t* out);
#ifdef __cplusplus
}
#endif
#endif
