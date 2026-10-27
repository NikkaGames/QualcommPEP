#ifndef OEM_DIRECT_RSC_MAP_H
#define OEM_DIRECT_RSC_MAP_H
/* a transport/lifetime primitive, NOT hardware ownership. calls must be serialized by a
 * PASSIVE_LEVEL, nonrecursive host lock; the owner callback must be independent of the PnP
 * census, mapping or register readback. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_RSC_DIRECT_BASE UINT64_C(0x18920000)
#define OEM_RSC_DIRECT_BYTES 0x10000u
#define OEM_RSC_MAP_COLD 0u
#define OEM_RSC_MAP_LIVE 1u
#define OEM_RSC_MAP_FAULT 2u
#define OEM_RSC_MAP_CLOSING 3u
#define OEM_RSC_MAP_RELEASED 4u

typedef struct oem_rsc_map_io {
    void* ctx;
    /* authoritative and non-cached DRV2/TCS/IRQ lease. Return 1 only if the caller holds all
     * three rights through the given controller epoch. */

    int (*owns)(void*, uint64_t);
    void* (*map)(void*, uint64_t, uint32_t);
    void (*unmap)(void*, void*, uint32_t);
    /* independent producer/ISR/DPC/TCS rundown; not inferred from PnP. */
    int (*quiesced)(void*, uint64_t);
} oem_rsc_map_io_t;

typedef struct oem_direct_rsc_map {
    uint32_t phase;
    uint32_t borrowers;
    uint64_t owner_epoch;
    void* mapped;
    oem_rsc_map_io_t io;
} oem_direct_rsc_map_t;

/* must be initially zero. No implicit successful ownership or late activation; a
 * failed/ambiguous acquisition latches FAULT. */

int oem_direct_rsc_map_acquire(oem_direct_rsc_map_t*, const oem_rsc_map_io_t*,
                               uint64_t owner_epoch);
/* the caller must hold the external lifecycle lock and must independently revalidate owner
 * before every register transaction. An opaque pointer is returned for transport
 * integration, not authority. */

void* oem_direct_rsc_map_borrow(oem_direct_rsc_map_t*, uint64_t owner_epoch);
int oem_direct_rsc_map_return(oem_direct_rsc_map_t*, uint64_t owner_epoch, void* mapped);
void oem_direct_rsc_map_close(oem_direct_rsc_map_t*);
/* may unmap after verified drain even if the owner lease was revoked. MUST be called at
 * PASSIVE_LEVEL only; failure keeps the map quarantined. */

int oem_direct_rsc_map_release(oem_direct_rsc_map_t*);
#ifdef __cplusplus
}
#endif
#endif
