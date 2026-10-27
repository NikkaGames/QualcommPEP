#ifndef OEM_WDF_IRQ_BRIDGE_H
#define OEM_WDF_IRQ_BRIDGE_H
/* caller supplies the physical owner, callback serialization and rundown. Passive ISRs can
 * drain inline; DIRQL callers require DPC/worker dispatch. */

#include "rpmh_irq_pipeline.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct oem_wdf_irq_bridge {
    rpmh_irq_pipeline_t* pipeline;
    uint64_t epoch;
    void* serialize_ctx;
    /* 1 = IRQ_STATUS contains an active owned AMC bit; 0 = this driver has no pending
     * completion; -1 = read/ownership proof failed. The provider must check an actual RSC
     * IRQ_STATUS readback AND the immutable active/owner masks for this epoch. */

    int (*read_owned_completion_pending)(void* ctx, uint64_t owner_epoch);
    /* enter must be safe at the invoking IRQL, including ISR DIRQL; return false without
     * blocking if serialization is unavailable. */

    int (*enter)(void*);
    void (*leave_cb)(void*);
    /* set only for a KMDF PassiveHandling ISR. Its canonical completion drain runs after
     * releasing the transport lock, with no DPC hop. */
    uint8_t passive_drain;
} oem_wdf_irq_bridge_t;

typedef enum oem_irq_claim {
    OEM_IRQ_UNCLAIMED = 0,
    OEM_IRQ_CLAIMED = 1,
    OEM_IRQ_QUARANTINED = 2
} oem_irq_claim_t;

/* this only validates an already-bound simulated owner; it does not grant access to MMIO,
 * DRV2, TCS, GICD, or the completion lock. */

int oem_irq_bridge_valid(const oem_wdf_irq_bridge_t* bridge);
oem_irq_claim_t oem_irq_bridge_isr(oem_wdf_irq_bridge_t* bridge);
/* call ONLY from a KMDF EvtInterruptIsr where WDF already owns the IRQ synchronization
 * lock. */

oem_irq_claim_t oem_irq_bridge_wdf_locked_isr(oem_wdf_irq_bridge_t* bridge);
int oem_irq_bridge_dpc(oem_wdf_irq_bridge_t* bridge);

int oem_irq_bridge_worker(oem_wdf_irq_bridge_t* bridge);
#ifdef __cplusplus
}
#endif
#endif
