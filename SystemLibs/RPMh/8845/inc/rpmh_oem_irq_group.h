#ifndef RPMH_OEM_IRQ_GROUP_H
#define RPMH_OEM_IRQ_GROUP_H
/* one separate ISR/DPC/deferred-work context per configured hardware driver. Neither this
 * API nor the single driver worker proves access to a controller, WDF IRQ, or TCS
 * resources. */

#include "rpmh_oem_work.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* capacity, NOT an assertion about the Molokai firmware's driver count. */
#define RPMH_OEM_MAX_DRIVERS 8u

typedef struct rpmh_oem_irq_entry {
    uint32_t driver_id;
    uint32_t gic_intid;
    uint64_t epoch;
    rpmh_oem_work_t work;
    uint8_t active;
} rpmh_oem_irq_entry_t;

typedef struct rpmh_oem_irq_group {
    rpmh_oem_irq_entry_t entries[RPMH_OEM_MAX_DRIVERS];
} rpmh_oem_irq_group_t;

/* these entries must be produced by an independently verified owner provider, never
 * invented from the PEP0 IRQ37 PnP res observation alone. */

typedef struct rpmh_oem_topology_entry {
    uint32_t driver_id;
    uint32_t gic_intid;
    const rpmh_work_ops_t* ops;
    uint64_t epoch;
} rpmh_oem_topology_entry_t;

/* the group must be empty. failure leaves the group byte-for-byte unchanged (no partial
 * ownership). */

int rpmh_irq_group_init_topology(rpmh_oem_irq_group_t* group,
                                 const rpmh_oem_topology_entry_t* entries, uint32_t cnt);

/* binds a *single, already verified* per-driver hardware grant. The supplied callbacks
 * must be specific to exactly that controller. */

int rpmh_irq_group_bind(rpmh_oem_irq_group_t* group, uint32_t driver_id, uint32_t gic_intid,
                        const rpmh_work_ops_t* ops, uint64_t epoch);
/* dispatch only through the matching interrupt-context identity AND epoch. */

int rpmh_irq_group_on_isr(rpmh_oem_irq_group_t* group, uint32_t driver_id, uint32_t gic_intid,
                          uint64_t epoch);
int rpmh_irq_group_on_dpc(rpmh_oem_irq_group_t* group, uint32_t driver_id, uint32_t gic_intid,
                          uint64_t epoch);
int rpmh_irq_group_on_worker(rpmh_oem_irq_group_t* group, uint32_t driver_id, uint32_t gic_intid,
                             uint64_t epoch);
/* fail-closed: refuses teardown while an interrupt is masked, active, quarantined or any
 * asynchronous work remains. The caller must first synchronize and drain WDF interrupt/DPC
 * and PEP work item callbacks. */

int rpmh_irq_group_revoke(rpmh_oem_irq_group_t* group, uint32_t driver_id, uint32_t gic_intid,
                          uint64_t epoch);
#ifdef __cplusplus
}
#endif
#endif
