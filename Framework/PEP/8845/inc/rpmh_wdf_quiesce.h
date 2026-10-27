#ifndef RPMH_WDF_QUIESCE_H
#define RPMH_WDF_QUIESCE_H
/* the exclusive controller owner serializes all model operations at PASSIVE_LEVEL. This model
 * provides no WDF registration, MMIO access or hardware grant. */

#include <stdint.h>
#include "rpmh_irq_pipeline.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum rpmh_wdf_quiesce_phase {
    RPMH_QUIESCE_IDLE = 0,
    RPMH_QUIESCE_CLOSED = 1,
    RPMH_QUIESCE_INTERRUPT_DISABLED = 2,
    RPMH_QUIESCE_ISR_QUIESCENT = 3,
    RPMH_QUIESCE_DPC_FLUSHED = 4,
    RPMH_QUIESCE_WORK_FLUSHED = 5,
    RPMH_QUIESCE_RELEASED = 6
} rpmh_wdf_quiesce_phase_t;

typedef enum rpmh_wdf_quiesce_step {
    RPMH_OBSERVE_INTERRUPT_DISABLED = 1,
    RPMH_OBSERVE_ISR_QUIESCENT = 2,
    RPMH_OBSERVE_DPC_FLUSHED = 3,
    RPMH_OBSERVE_WORK_FLUSHED = 4
} rpmh_wdf_quiesce_step_t;

typedef struct rpmh_wdf_quiesce {
    rpmh_irq_pipeline_t* pipeline;
    uint64_t epoch;
    uint64_t last_epoch;
    rpmh_wdf_quiesce_phase_t phase;
} rpmh_wdf_quiesce_t;

void rpmh_quiesce_init(rpmh_wdf_quiesce_t* q);
/* closing admission must precede any framework disable/synchronization. */
int rpmh_quiesce_begin(rpmh_wdf_quiesce_t* q, rpmh_irq_pipeline_t* p, uint64_t epoch);

int rpmh_quiesce_record(rpmh_wdf_quiesce_t* q, uint64_t epoch, rpmh_wdf_quiesce_step_t step);

/* refuses when out of order, an epoch mismatches, callbacks remain pending, completion was
 * quarantined, or pipeline is not closed/drained. */

/* a future verified WDF adapter supplies synchronous operations that report TRUE only when
 * the corresponding framework obligation has completed. This runner never registers an
 * interrupt, issues an MMIO transaction, or proves exclusive controller ownership. */

typedef struct rpmh_wdf_framework_ops {
    void* ctx;
    int (*disable_owned_interrupt)(void*);
    int (*sync_isr)(void*);
    int (*flush_queued_dpcs)(void*);
    int (*flush_deferred_work)(void*);
} rpmh_wdf_framework_ops_t;

/* returns 1 only when all steps are verified and work idle; returns 0 on missing callback,
 * failure, stale epoch or pending AMC work. Does not release/unmap hardware. */

int rpmh_quiesce_drive(rpmh_wdf_quiesce_t* q, uint64_t epoch, const rpmh_wdf_framework_ops_t* ops);

int rpmh_quiesce_release(rpmh_wdf_quiesce_t* q, uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
