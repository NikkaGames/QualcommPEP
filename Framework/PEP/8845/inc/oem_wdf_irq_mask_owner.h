#ifndef OEM_WDF_IRQ_MASK_OWNER_H
#define OEM_WDF_IRQ_MASK_OWNER_H
/* nonpaged, DIRQL-safe *external owner* contract for the WDFINTERRUPT enable/disable
 * callbacks. Not hardware ownership, does not discover a GICD/RSC register and cannot set
 * hardware rights. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_IRQ_MASK_UNBOUND 0u
#define OEM_IRQ_MASK_OFF 1u
#define OEM_IRQ_MASK_ON 2u
#define OEM_IRQ_MASK_FAULT 3u

/* forensic reason for a refused physical mask transition (not a grant). */
#define OEM_MASK_FAIL_NONE 0u
#define OEM_MASK_FAIL_INPUT 1u
#define OEM_MASK_FAIL_STATE 2u
#define OEM_MASK_FAIL_OWNER 3u
#define OEM_MASK_FAIL_PRE_READ 4u
#define OEM_MASK_FAIL_WRITE 5u
#define OEM_MASK_FAIL_POST_READ 6u
#define OEM_MASK_FAIL_SRC_BUSY 7u

typedef struct oem_irq_mask_ops {
    void* ctx;
    /* all functions are NONPAGED, nonblocking and safe under the WDF interrupt lock at DIRQL
     * (or the framework's passive IRQ lock). exclusive_owner MUST mean a runtime DRV2+GIC
     * owner, not PnP only. */

    int (*exclusive_owner)(void*, uint64_t epoch, void* interrupt);
    int (*read_masked)(void*, int* masked); /* authoritative 0/1 hardware state. */
    int (*write_masked)(void*, int masked);

    int (*src_quiescent)(void*, uint64_t epoch, void* interrupt);
    /* only the terminal worker with a pinned completion token may call this. Unlike shutdown
     * quiescence, a synchronous waiter may still be active. */
    int (*completion_drained)(void*, uint64_t epoch, void* interrupt);
} oem_irq_mask_ops_t;

typedef struct oem_irq_mask_owner {
    oem_irq_mask_ops_t io;
    void* interrupt;
    uint64_t epoch;
    uint32_t phase;
    uint32_t last_failure;
    uint32_t framework_observed_transition;
    uint32_t verified_completed_unmask;
    uint32_t last_finalize_step;
    int32_t last_finalize_read_a;
    int32_t last_finalize_read_b;
} oem_irq_mask_owner_t;

/* called by PnP owner while WDF callbacks are blocked, before d0. Initially requires TWO
 * independent reads proving hardware masked. */

int oem_irq_mask_bind(oem_irq_mask_owner_t*, const oem_irq_mask_ops_t*, void* interrupt,
                      uint64_t epoch);
/* invoked only from framework EvtInterruptEnable/Disable, under its lock. A missing
 * provider means ENABLE REFUSED; disable of an unbound never- enabled object is a no-op. */

int oem_irq_mask_transition(oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch, int masked);
/* call only from the matching KMDF enable/disable callback with its interrupt lock held. This
 * records framework state and performs no hardware write. */

/* terminal worker only: physical AMCs are retired and the final gate token is pinned. If
 * HAL already restored the GIC enable bit, adopt it without issuing a duplicate W1S. Never
 * accept this state based solely on GIC readback. */
int oem_irq_mask_finalize_completed(oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch);

int oem_irq_mask_wdf_transition(oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch, int masked);
/* isr-only mask operation: caller is running under the actual bound WDF IRQ lock and has
 * JUST verified its outstanding DRV2 IRQ_STATUS bit and exact in-flight AMC. A
 * framework-masked GIC is already safe to keep masked, but is never a command
 * completion/acknowledgement. */

int oem_irq_mask_verified_isr_mask(oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch,
                                   int verified_owned_pending);

int oem_irq_mask_observe_framework(oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch,
                                   int masked);
int oem_irq_mask_read_proof(const oem_irq_mask_owner_t*, void* interrupt, uint64_t epoch,
                            int masked);
void oem_irq_mask_quarantine(oem_irq_mask_owner_t*);
#ifdef __cplusplus
}
#endif
#endif
