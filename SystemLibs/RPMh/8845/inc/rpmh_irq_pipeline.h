#ifndef RPMH_IRQ_PIPELINE_H
#define RPMH_IRQ_PIPELINE_H
/* coordinate AMC completion with epoch-bound lifetime tickets; the provider owns IRQ and MMIO
 * operations. */

#include <stdint.h>
#include "rpmh_oem_work.h"
#include "rpmh_irq_rundown.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rpmh_irq_pipeline {
    rpmh_irq_gate_t gate;
    rpmh_oem_work_t work;
    rpmh_irq_ticket_t pending;

    rpmh_irq_ticket_t unmask_finalizing;
    uint64_t epoch;
    uint8_t initialized;
    uint8_t bound;
    uint8_t faulted;
} rpmh_irq_pipeline_t;

int rpmh_pipeline_init(rpmh_irq_pipeline_t* pipeline);

int rpmh_pipeline_bind_owner(rpmh_irq_pipeline_t* pipeline, const rpmh_work_ops_t* ops,
                             uint64_t owner_epoch);

int rpmh_pipeline_dispatch_isr(rpmh_irq_pipeline_t* pipeline, uint64_t owner_epoch);
int rpmh_pipeline_dispatch_dpc(rpmh_irq_pipeline_t* pipeline, uint64_t owner_epoch);
/* hold WDF, scheduler and transport locks in order and verify IRQ_STATUS for the executing request.
 * Capture its ticket, then release locks before worker dispatch. */
int rpmh_pipeline_capture_passive(rpmh_irq_pipeline_t*, uint64_t);
int rpmh_pipeline_dispatch_worker(rpmh_irq_pipeline_t* pipeline, uint64_t owner_epoch);

int rpmh_pipeline_close(rpmh_irq_pipeline_t* pipeline, uint64_t owner_epoch);
/* must also have disabled/synchronized WDF interrupts and flushed DPC and deferred
 * callbacks independently before entering release. */

int rpmh_pipeline_release(rpmh_irq_pipeline_t* pipeline, uint64_t owner_epoch);
#ifdef __cplusplus
}
#endif
#endif
