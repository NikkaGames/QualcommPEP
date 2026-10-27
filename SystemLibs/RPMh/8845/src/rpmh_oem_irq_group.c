#include "rpmh_oem_irq_group.h"
#include <string.h>

static rpmh_oem_irq_entry_t* find_exact(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq,
                                        uint64_t epoch)
{
    uint32_t i;
    if (!group || !irq || !epoch)
        return 0;
    for (i = 0; i < RPMH_OEM_MAX_DRIVERS; ++i) {
        rpmh_oem_irq_entry_t* e = &group->entries[i];
        if (e->active && e->driver_id == id && e->gic_intid == irq && e->epoch == epoch &&
            e->work.owner_epoch == epoch)
            return e;
    }
    return 0;
}

int rpmh_irq_group_bind(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq,
                        const rpmh_work_ops_t* ops, uint64_t epoch)
{
    rpmh_oem_irq_entry_t* empty = 0;
    uint32_t i;
    if (!group || !ops || !irq || !epoch)
        return 0;
    for (i = 0; i < RPMH_OEM_MAX_DRIVERS; ++i) {
        rpmh_oem_irq_entry_t* e = &group->entries[i];
        if (!e->active) {
            if (!empty)
                empty = e;
        } else if (e->driver_id == id || e->gic_intid == irq) {
            /* duplicate WDF context or IRQ assignment: never choose an arbitrary active owner when
             * dispatching a shared src. */

            return 0;
        }
    }
    if (!empty)
        return 0;
    if (!rpmh_work_bind(&empty->work, ops, epoch))
        return 0;
    empty->driver_id = id;
    empty->gic_intid = irq;
    empty->epoch = epoch;
    empty->active = 1;
    return 1;
}

int rpmh_irq_group_on_isr(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq, uint64_t epoch)
{
    rpmh_oem_irq_entry_t* e = find_exact(group, id, irq, epoch);
    return e ? rpmh_work_on_isr(&e->work, epoch) : 0;
}

int rpmh_irq_group_on_dpc(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq, uint64_t epoch)
{
    rpmh_oem_irq_entry_t* e = find_exact(group, id, irq, epoch);
    return e ? rpmh_work_on_dpc(&e->work, epoch) : 0;
}

int rpmh_irq_group_on_worker(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq, uint64_t epoch)
{
    rpmh_oem_irq_entry_t* e = find_exact(group, id, irq, epoch);
    return e ? rpmh_work_on_worker(&e->work, epoch) : 0;
}

int rpmh_irq_group_revoke(rpmh_oem_irq_group_t* group, uint32_t id, uint32_t irq, uint64_t epoch)
{
    rpmh_oem_irq_entry_t* e = find_exact(group, id, irq, epoch);
    if (!e || !rpmh_work_revoke(&e->work, epoch))
        return 0;
    memset(e, 0, sizeof(*e));
    return 1;
}

/* recover the initialization TABLE semantics without hardcoding the number of Molokai
 * drivers or publishing partially constructed ownership. */

int rpmh_irq_group_init_topology(rpmh_oem_irq_group_t* group,
                                 const rpmh_oem_topology_entry_t* entries, uint32_t cnt)
{
    rpmh_oem_irq_group_t candidate = {0};
    uint32_t i;
    if (!group || !entries || !cnt || cnt > RPMH_OEM_MAX_DRIVERS)
        return 0;
    for (i = 0; i < RPMH_OEM_MAX_DRIVERS; ++i)
        if (group->entries[i].active)
            return 0;
    for (i = 0; i < cnt; ++i) {
        const rpmh_oem_topology_entry_t* e = &entries[i];
        if (!e->ops ||
            !rpmh_irq_group_bind(&candidate, e->driver_id, e->gic_intid, e->ops, e->epoch))
            return 0;
    }
    memcpy(group, &candidate, sizeof(candidate));
    return 1;
}
