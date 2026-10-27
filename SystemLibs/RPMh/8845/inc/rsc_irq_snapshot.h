#ifndef RSC_IRQ_SNAPSHOT_H
#define RSC_IRQ_SNAPSHOT_H
/* pnp-scoped, read-only IRQ res observation. NOT an RSC ownership grant. */

#include <stdint.h>
#include "rsc_irq_assignment.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsc_irq_snapshot {
    uint64_t generation;
    rsc_irq_report_t report;
    uint8_t prepared;
    uint8_t unique_candidate;
} rsc_irq_snapshot_t;

void rsc_irq_snapshot_init(rsc_irq_snapshot_t* s);

void rsc_irq_snapshot_capture(rsc_irq_snapshot_t* s, rsc_irq_report_t report);

void rsc_irq_snapshot_revoke(rsc_irq_snapshot_t* s);
/* return 1 only for a unique PnP candidate from THIS hardware lifecycle. This never
 * authorizes WdfInterruptCreate, GIC/RSC MMIO, AMC or DVFS. */

int rsc_irq_snapshot_read(const rsc_irq_snapshot_t* s, uint64_t generation, rsc_irq_report_t* out);
#ifdef __cplusplus
}
#endif
#endif
