#ifndef OEM_SLEEPWAKE_HANDOFF_H
#define OEM_SLEEPWAKE_HANDOFF_H
/* dormant sleep/wake host Handoff and rundown COORDINATOR. This API does not implement
 * firmware handoff, ACPI/PEP reg, ownership discovery, or an ISR-safe synchronization
 * primitive. */

#include "oem_sleepwake_program.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum oem_sleepwake_handoff_phase {
    HANDOFF_EMPTY = 0,
    HANDOFF_INITIALIZED = 1,
    HANDOFF_ARMED = 2,
    HANDOFF_PROGRAMMED = 3,
    HANDOFF_TRANSFERRED = 4,
    HANDOFF_RETIRED = 5,
    HANDOFF_CANCELLED = 6,
    HANDOFF_QUARANTINED = 7
} oem_sleepwake_handoff_phase_t;

typedef struct oem_sleepwake_handoff_ops {
    void* ctx;
    /* acquire must atomically close all new producers, wait for the existing /199 queues and
     * interrupt workers, then return a nonzero lease. callbacks may block and MUST NOT execute
     * holding an IRQ spinlock. */

    int (*fence_and_drain)(void*, uint32_t epoch, uint64_t* lease);
    /* re-attest the same immutable lease, closed producers and empty queues. Software flags
     * alone cannot attest hardware or platform ownership. */

    int (*fence_still_drained)(void*, uint32_t epoch, uint64_t lease);
    int (*release_fence)(void*, uint32_t epoch, uint64_t lease);
    /* independent assertion that NO TCS programming or firmware transfer started. */

    int (*never_started)(void*, uint32_t epoch, uint64_t ticket, uint64_t lease);
    /* may mutate firmware state. */

    int (*transfer_to_firmware)(void*, uint32_t epoch, uint64_t ticket, uint64_t lease,
                                uint8_t slot_mask);
    /* a verified return from firmware, distinct from resume proof and release attestation.
     * Never inferred from TCS readback. */

    int (*firmware_returned)(void*, uint32_t epoch, uint64_t ticket, uint64_t lease);
} oem_sleepwake_handoff_ops_t;

typedef struct oem_sleepwake_handoff {
    oem_sleepwake_bridge_t* bridge;
    oem_sleepwake_program_t program;
    oem_sleepwake_program_ops_t program_ops;
    oem_sleepwake_handoff_ops_t ops;
    uint32_t epoch;
    uint64_t ticket;
    uint64_t lease;
    oem_sleepwake_handoff_phase_t phase;
    uint8_t fence_held;
    uint8_t closed;
} oem_sleepwake_handoff_t;

int oem_handoff_init(oem_sleepwake_handoff_t*, oem_sleepwake_bridge_t*,
                     const oem_sleepwake_program_ops_t*, const oem_sleepwake_handoff_ops_t*);

int oem_handoff_begin(oem_sleepwake_handoff_t*);

int oem_handoff_attach_dependencies(oem_sleepwake_handoff_t*, const oem_sleepwake_dependencies_t*);
int oem_handoff_program(oem_sleepwake_handoff_t*);
/* no handoff call until ALL TCS programming has been independently verified. */
int oem_handoff_transfer(oem_sleepwake_handoff_t*);
/* independent firmware-return proof, resume proof, firmware-release proof, THEN reopen
 * producers. */

int oem_handoff_complete_resume(oem_sleepwake_handoff_t*);
/* allowed only before 's first possible write or firmware handoff. */
int oem_handoff_cancel_unstarted(oem_sleepwake_handoff_t*);
void oem_handoff_close(oem_sleepwake_handoff_t*);
int oem_handoff_can_release(const oem_sleepwake_handoff_t*);
#ifdef __cplusplus
}
#endif
#endif
