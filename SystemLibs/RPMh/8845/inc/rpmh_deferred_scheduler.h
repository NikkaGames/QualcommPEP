#ifndef RPMH_DEFERRED_SCHEDULER_H
#define RPMH_DEFERRED_SCHEDULER_H
/* production binding between the software RPMh client scheduler and the
 * TCS/immutable-IRQ-ticket issuer. the owner supplies externally synchronized ISR/DPC and
 * PnP lifetime; grants no RSC rights. */

#include "rpmh_client_scheduler.h"
#include "rsc_irq_latched.h"
#include "rpmh_sync_deferred.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rpmh_deferred_scheduler {
    rpmh_scheduler_t* scheduler;
    rpmh_tcs_t* tcs;
    rsc_completion_t* ledger;
    uint64_t epoch;        /* full Windows/WDF owner identity; never truncated. */
    uint32_t ledger_epoch; /* independently bound 32-bit RSC ticket generation. */
    /* a retired d0 must not erase its clients or strand a second d0 by leaving arm_deferred
     * pinned to a dead epoch. */

    rpmh_execute_t previous_execute;
    rpmh_execute_result_t previous_execute_result;
    void* previous_execute_ctx;
} rpmh_deferred_scheduler_t;

/* preconditions are independently supplied hardware/IRQ rights; bind never calls
 * RscCompletionBind or modifies grants, masks, or registers. */

int rpmh_deferred_scheduler_bind(rpmh_deferred_scheduler_t* binding, rpmh_scheduler_t* scheduler,
                                 rpmh_tcs_t* tcs, rsc_completion_t* ledger,
                                 const rpmh_driver_config_t* geometry,
                                 uint32_t independently_established_owner_epoch);
/* owner epoch and ledger generation are separate: Windows epochs may use bit 63; ledger/TCS
 * generations remain 32-bit. */

int rpmh_deferred_scheduler_bind_owner(rpmh_deferred_scheduler_t* binding,
                                       rpmh_scheduler_t* scheduler, rpmh_tcs_t* tcs,
                                       rsc_completion_t* ledger,
                                       const rpmh_driver_config_t* geometry,
                                       uint64_t independently_established_win_owner_epoch,
                                       uint32_t independently_established_ledger_epoch);
/* owner-only *post-hardware-rundown* d0 transition. the caller must already stop PoFx
 * requests, synchronize WDF ISR/DPC/worker, retire exact AMC ACKs, and restore physically
 * owned TCS IRQ enable bits. */

int rpmh_deferred_scheduler_retire_owner(rpmh_deferred_scheduler_t* binding,
                                         rpmh_sync_deferred_t* waiter, uint64_t owner_epoch,
                                         uint32_t ledger_epoch);

/* read-only admission predicate used by the actual Windows d0 hardware probe. A scheduler
 * promoted in place must not be reauthorized using the incompatible synchronous
 * authorize/prepare path. */

int rpmh_deferred_probe_ready(const rpmh_deferred_scheduler_t* binding,
                              const rpmh_sync_deferred_t* waiter);

int rpmh_deferred_runtime_ready(const rpmh_deferred_scheduler_t* binding,
                                const rpmh_sync_deferred_t* waiter);

/* one WDF worker must own the exact ISR-latched ticket. the hardware response is published
 * only after ACK and AMC cleanup succeed. */

rpmh_tcs_result_t rpmh_deferred_scheduler_finish(rpmh_deferred_scheduler_t* binding,
                                                 rsc_irq_latch_t* latched);
#ifdef __cplusplus
}
#endif
#endif
