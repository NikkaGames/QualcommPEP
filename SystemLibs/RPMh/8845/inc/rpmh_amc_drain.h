#ifndef RPMH_AMC_DRAIN_H
#define RPMH_AMC_DRAIN_H
/* these callbacks perform RSC IRQ capture and W1C/cleanup on the EXACT previously
 * triggered TCS ticket. They do not attach an IRQ or confer an MMIO/GIC/DRV2 grant. */

#include "rpmh_deferred_scheduler.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rpmh_amc_drain {
    rpmh_deferred_scheduler_t* binding;
    rsc_irq_latch_t latched[RSC_AMCS];
    uint32_t epoch;       /* 32-bit RSC completion ticket generation. */
    uint64_t owner_epoch; /* full native Windows scheduler owner epoch. */
    uint8_t selected;
    rpmh_irq_src_t* irq_src;
} rpmh_amc_drain_t;

int rpmh_amc_drain_bind(rpmh_amc_drain_t*, rpmh_deferred_scheduler_t*);
/* direct QCPEP_RPMH_WORK_OPS callbacks for an independently-bound owner. */

int rpmh_amc_drain_has_finished(void*);
int rpmh_amc_drain_finish_one(void*);
#ifdef __cplusplus
}
#endif
#endif
