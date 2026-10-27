#ifndef GIC_IRQ37_OWNED_WIN_H
#define GIC_IRQ37_OWNED_WIN_H
/* physical INTID37 GIC distributor mask adapter. The mapping/PnP observation are NOT
 * grants. */

#include <stdint.h>
#include "gic_d0_map_win.h"
#include "oem_wdf_irq_mask_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GICD_ICENABLER1 0x184u
/* diagnostic stage; 0 only after verified success. */
#define GIC_WRITE_OK 0u
#define GIC_WRITE_BAD_INPUT 1u
#define GIC_WRITE_OWNER_PRE 2u
#define GIC_WRITE_READ_PRE 3u
#define GIC_WRITE_OWNER_POST 4u
#define GIC_WRITE_READ_POST 5u
#define GIC_WRITE_NO_COMMIT 6u

typedef struct gic_irq37_owned_win {
    gic_d0_map_win_t* map;
    uint64_t pnp_generation;
    uint64_t epoch;
    void* wdf_interrupt;
    void* proof_ctx;
    /* requires exclusive RSC/GICD/IRQ37 ownership. Address and PnP matches are insufficient; this
     * callback must not block. */

    int (*exclusive_owner)(void*, uint64_t, void*, uint64_t);
    volatile uint32_t faulted;
    volatile uint32_t active;
    /* last single physical W1 transition, for read-only KDNET diagnosis. */
    volatile uint32_t last_write_issued;
    volatile uint32_t last_write_proof_pairs;
    volatile int32_t last_write_target;
    volatile int32_t last_write_observed;
    volatile uint32_t last_write_attempt_id;
    volatile uint32_t last_write_failure_stage;
} gic_irq37_owned_win_t;

/* requires IRQ initially masked, independently verified owner, stable reads. */

int gic_irq37_prepare_mask_ops_win(gic_irq37_owned_win_t* binding, gic_d0_map_win_t* map,
                                   uint64_t pnp_generation, uint64_t epoch, void* interrupt,
                                   void* proof_ctx,
                                   int (*exclusive_owner)(void*, uint64_t, void*, uint64_t),
                                   oem_irq_mask_ops_t* ops);

/* the EXTERNAL proof must attest exclusive Windows ownership of DRV2, GICD and the PnP
 * IRQ, not just their geometry. Unreadable write outcome faults the binding and retains
 * the mapping until recovery; this API grants no RSC/TCS ownership and cannot be used to
 * bypass independent proof. */

int gic_irq37_acquire_masked_ops_win(gic_irq37_owned_win_t*, gic_d0_map_win_t*,
                                     uint64_t pnp_generation, uint64_t epoch, void* interrupt,
                                     void* proof_ctx,
                                     int (*exclusive_owner)(void*, uint64_t, void*, uint64_t),
                                     oem_irq_mask_ops_t* ops);

/* only after IRQ disable, original mask-owner verification, all WDF/AMC callbacks drained,
 * and independently proven masked hardware. on a fault preserve binding and MMIO lifetime;
 * never silently release uncertain IRQ. */

int gic_irq37_retire_mask_ops_win(gic_irq37_owned_win_t*, uint64_t epoch, void* interrupt,
                                  int owner_drained);
#ifdef __cplusplus
}
#endif
#endif
