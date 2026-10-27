#ifndef OEM_GIC_DISPATCH_H
#define OEM_GIC_DISPATCH_H
/* no GICD mapping or controller ownership is provided by this module; the caller supplies
 * a verified, owned window. It MUST NOT be used to mask any IRQ until the platform
 * establishes the Windows interrupt-controller integration and DRV2 exclusive rights. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct oem_gic_io {
    void* ctx;
    /* offset relative to the mapped GICD distributor base. Return 1 only after a successful,
     * properly ordered 32-bit MMIO write. */

    int (*write32)(void* ctx, uint32_t offset, uint32_t value);
    void (*barrier)(void* ctx);
} oem_gic_io_t;

typedef enum oem_gic_result {
    OEM_GIC_OK = 0,
    OEM_GIC_INVALID = 1,
    OEM_GIC_IO_FAILURE = 2
} oem_gic_result_t;

#define OEM_GICD_ICENABLER 0x180u
#define OEM_GICD_ISENABLER 0x100u

oem_gic_result_t oem_gic_mask(const oem_gic_io_t* io, uint32_t gic_intid);
oem_gic_result_t oem_gic_unmask(const oem_gic_io_t* io, uint32_t gic_intid);
#ifdef __cplusplus
}
#endif
#endif
