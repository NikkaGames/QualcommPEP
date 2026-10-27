#ifndef RPMH_OEM_WORK_H
#define RPMH_OEM_WORK_H
/* no Windows binding or hardware grant is provided. Caller serializes callbacks with
 * teardown and must externally verify exclusive controller and IRQ ownership. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RPMH_WORK_DRAIN_BUDGET 64u
/* bound retries for one immutable deferred ACK ticket, never a new vote. */
#define RPMH_WORK_ACK_RETRY_MAX 8u
/* bound a temporary passive ISR-lock collision separately from W1C retries. Exhaustion
 * leaves the owned interrupt masked for explicit rundown. */

#define RPMH_WORK_IRQ_LOCK_RETRY_MAX 16u

typedef enum rpmh_work_state {
    RPMH_WORK_UNBOUND = 0,
    RPMH_WORK_READY = 1,
    RPMH_WORK_DPC_PENDING = 2,
    RPMH_WORK_WORKER_PENDING = 3,
    RPMH_WORK_QUARANTINED = 4,
    RPMH_WORK_WORKER_ACTIVE = 5,
    RPMH_WORK_COMPLETION_READY = 6
} rpmh_work_state_t;

typedef struct rpmh_work_ops {
    void* ctx;
    /* only an independently established, exclusive owner may use these. */
    int (*mask_owned_interrupt)(void*);
    int (*queue_dpc)(void*);
    int (*queue_pep_work)(void*);
    /* >0: finished AMC avail; 0: none; -2: ISR-lock unavailable before any hardware access;
     * -1: invalid owner or hardware read fault. */

    int (*has_finished_amc)(void*);
    /* 1: physical ACK + TCS cleanup; 2: same immutable ticket requires another deferred pass
     * (no reissue/unmask); 0: unsafe/fatal. */

    int (*finish_one_amc)(void*);
    int (*unmask_owned_interrupt)(void*);
    /* amc worker holds the RPMh controller completion lock across has_finished_amc /
     * finish_one_amc, and RELEASES it before unmask. Must be an actual verified controller
     * lock; no lockless/NULL fallback. */

    int (*enter_completion_lock)(void*);
    void (*leave_completion_lock)(void*);
} rpmh_work_ops_t;

typedef struct rpmh_oem_work {
    rpmh_work_ops_t ops;
    uint64_t owner_epoch;
    uint32_t finished;
    uint8_t ack_retry_cnt;
    uint8_t lock_retry_cnt; /* bounded lock contention, independent of ACK. */
    rpmh_work_state_t state;
    uint8_t irq_masked;
    /* pipeline owner opts into ordering: ACK -> retire ticket -> unmask. */

    uint8_t defer_unmask;

    uint8_t completion_lock_held;
    /* native owner optional finalization callback, not part of vendor WDF ops. */
    void (*on_owner_irq_restored)(void*);
    void* restored_ctx;
} rpmh_oem_work_t;

/* requires a WDF interrupt, verified completion lock and exclusive controller/AMC grant. PnP
 * candidates and static reference data are insufficient. */

int rpmh_work_bind(rpmh_oem_work_t*, const rpmh_work_ops_t*, uint64_t);
int rpmh_work_on_isr(rpmh_oem_work_t*, uint64_t);
int rpmh_work_on_dpc(rpmh_oem_work_t*, uint64_t);
int rpmh_work_on_worker(rpmh_oem_work_t*, uint64_t);
/* a blocked drain retains ownership and mappings. */

int rpmh_work_revoke(rpmh_oem_work_t*, uint64_t);
#ifdef __cplusplus
}
#endif
#endif
