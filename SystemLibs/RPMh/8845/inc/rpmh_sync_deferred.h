#ifndef RPMH_SYNC_DEFERRED_H
#define RPMH_SYNC_DEFERRED_H
/* IRQ-backed completion adapter for BUSARB/PMIC/readback clients. It neither polls TCS MMIO nor
 * cancels issued votes. */

#include "rpmh_client_scheduler.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rpmh_sync_wait_io {
    void* ctx;
    int (*enter)(void*); /* owner scheduler mutex; not DIRQL. */
    void (*leave_cb)(void*);
    void (*reset)(void*);               /* manual-reset event; lock held. */
    void (*signal)(void*);              /* completion event; lock held. */
    int (*wait_ms)(void*, uint32_t ms); /* lock not held. */
    /* optional native WDF producer, invoked once after an issued timeout has been pinned and
     * after leave_cb. Does not own the original request. */
    int (*queue_timeout_review)(void*, uint64_t epoch, uint32_t req);
    /* native IRQ worker must restore IRQ37 + retire the pin before waking waiter. */
    uint8_t defer_signal_until_irq_restored;
} rpmh_sync_wait_io_t;

typedef struct rpmh_sync_deferred {
    rpmh_scheduler_t* scheduler;
    rpmh_sync_wait_io_t io;
    uint64_t owner_epoch;
    uint32_t active_req; /* 0 if no PASSIVE client waiting. */
    uint8_t faulted;
    /* issued timeout pins its original request for late IRQ/ACK retirement. No timeout
     * callback is permitted to clear or replay the physical vote. */
    uint32_t timed_out_req;
    uint8_t late_completion_seen;
    uint32_t awaiting_irq_restore;
} rpmh_sync_deferred_t;

int rpmh_sync_deferred_bind(rpmh_sync_deferred_t* b, rpmh_scheduler_t* scheduler,
                            uint64_t owner_epoch, const rpmh_sync_wait_io_t* io);
/* does not revoke authorization, complete hardware, or mutate the waiter. */

/* read-only, owner-mutex-required review of a timed-out original ticket. 0
 * invalid/unknown, 1 still physically in flight, 2 ACK verified success, 3 completion
 * worker verified failure. NONE authorizes release or replay. */
int rpmh_sync_deferred_timeout_inspect(const rpmh_sync_deferred_t* waiter, uint64_t owner_epoch,
                                       uint32_t req_id, rpmh_tcs_result_t* verified_result);
/* called by the true passive AMC owner only after verified GIC unmask and the final
 * IRQ-gate ticket retirement. caller holds the waiter mutex. */
int rpmh_sync_deferred_finalized(rpmh_sync_deferred_t* waiter, uint64_t epoch);
int rpmh_sync_deferred_can_retire(const rpmh_sync_deferred_t* waiter,
                                  const rpmh_scheduler_t* scheduler, uint64_t owner_epoch);
rpmh_tcs_result_t rpmh_sync_deferred_execute(rpmh_sync_deferred_t* b, uint8_t client,
                                             const tcs_command_t* commands, uint32_t cnt,
                                             uint32_t* responses, uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
