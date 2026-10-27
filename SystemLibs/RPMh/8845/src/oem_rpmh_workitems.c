#include "oem_rpmh_workitems.h"
#include <stdint.h>
#include <string.h>

static int ops_valid(const oem_rpmh_work_ops_t* o)
{
    return o && o->create && o->enqueue && o->wait && o->destroy;
}

static int table_empty(const oem_rpmh_work_table_t* t)
{
    uint32_t i;
    if (!t || t->cnt || t->owner_epoch || t->published || t->faulted)
        return 0;
    for (i = 0; i <= OEM_RPMH_WORK_MAX_ID; ++i)
        if (t->present[i] || t->amc[i] || t->timeout[i])
            return 0;
    return 1;
}

static int requests_valid(const oem_rpmh_work_entry_t* r, uint32_t cnt)
{
    uint32_t i, j;
    if (!r || !cnt || cnt > OEM_RPMH_WORK_MAX_DRIVERS)
        return 0;
    for (i = 0; i < cnt; ++i) {
        if (r[i].driver_id > OEM_RPMH_WORK_MAX_ID)
            return 0;
        for (j = 0; j < i; ++j)
            if (r[j].driver_id == r[i].driver_id || r[j].hw_driver == r[i].hw_driver)
                return 0;
    }
    return 1;
}

int oem_rpmh_work_drain_destroy(oem_rpmh_work_table_t* t, int external_quiesced,
                                const oem_rpmh_work_ops_t* o)
{
    uint32_t i;
    if (!t || !external_quiesced || !ops_valid(o) || t->published)
        return 0;
    /* never destroy an object if its callback (or any sibling callback) might still be
     * running. */

    for (i = 0; i <= OEM_RPMH_WORK_MAX_ID; ++i) {
        if (t->amc[i] && !o->wait(o->ctx, t->amc[i])) {
            t->faulted = 1;
            return 0;
        }
        if (t->timeout[i] && !o->wait(o->ctx, t->timeout[i])) {
            t->faulted = 1;
            return 0;
        }
    }
    for (i = OEM_RPMH_WORK_MAX_ID + 1u; i > 0;) {
        --i;
        if (t->timeout[i]) {
            if (!o->destroy(o->ctx, t->timeout[i])) {
                t->faulted = 1;
                return 0;
            }
            t->timeout[i] = 0;
        }
        if (t->amc[i]) {
            if (!o->destroy(o->ctx, t->amc[i])) {
                t->faulted = 1;
                return 0;
            }
            t->amc[i] = 0;
        }
        t->present[i] = 0;
    }
    memset(t, 0, sizeof(*t));
    return 1;
}

int oem_rpmh_work_create(oem_rpmh_work_table_t* t, const oem_rpmh_work_entry_t* r, uint32_t cnt,
                         uint64_t epoch, oem_rpmh_handler_t amc, oem_rpmh_handler_t timeout,
                         const oem_rpmh_work_ops_t* o)
{
    uint32_t i;
    oem_rpmh_work_cfg_t cfg;
    if (!table_empty(t) || !requests_valid(r, cnt) || !epoch || !amc || !timeout || !ops_valid(o))
        return 0;
    t->owner_epoch = epoch;
    for (i = 0; i < cnt; ++i) {
        uint32_t d = r[i].driver_id;
        t->present[d] = 1;
        ++t->cnt;
        cfg.work_type = OEM_RPMH_WORK_CRITICAL;
        cfg.handler = amc;
        cfg.handler_param = (void*)(uintptr_t)r[i].hw_driver;
        if (!o->create(o->ctx, &cfg, &t->amc[d]) || !t->amc[d])
            goto fail;
        cfg.handler = timeout;
        if (!o->create(o->ctx, &cfg, &t->timeout[d]) || !t->timeout[d])
            goto fail;
    }
    t->published = 1;
    return 1;
fail:
    /* ownership never silently disappears. */

    t->faulted = 1;
    t->published = 0;
    (void)oem_rpmh_work_drain_destroy(t, 1, o);
    return 0;
}

void oem_rpmh_work_close(oem_rpmh_work_table_t* t)
{
    if (t)
        t->published = 0;
}

int oem_rpmh_work_queue_amc(oem_rpmh_work_table_t* t, uint32_t id, uint64_t epoch,
                            const oem_rpmh_work_ops_t* o)
{
    if (!t || !t->published || t->faulted || id > OEM_RPMH_WORK_MAX_ID || !t->present[id] ||
        !t->amc[id] || epoch != t->owner_epoch || !ops_valid(o))
        return 0;
    if (!o->enqueue(o->ctx, t->amc[id])) {
        t->faulted = 1;
        return 0;
    }
    return 1;
}

/* each controller has a timeout work item. Schedule only after its pinned deadline and retain
 * owner/work storage through rundown. */
int oem_rpmh_work_queue_timeout(oem_rpmh_work_table_t* t, uint32_t id, uint64_t epoch,
                                const oem_rpmh_work_ops_t* o)
{
    if (!t || !t->published || t->faulted || !epoch || id > OEM_RPMH_WORK_MAX_ID ||
        !t->present[id] || !t->timeout[id] || epoch != t->owner_epoch || !ops_valid(o))
        return 0;
    if (!o->enqueue(o->ctx, t->timeout[id])) {
        t->faulted = 1;
        return 0;
    }
    return 1;
}

int oem_rpmh_work_make_irq_req(oem_rpmh_work_table_t* t, uint32_t id, uint32_t intid,
                               const void* raw, const void* translated, oem_wdf_irq_req_t* out)
{
    oem_wdf_irq_req_t r = {0};
    if (!t || !out || !t->published || t->faulted || !t->owner_epoch || id > OEM_RPMH_WORK_MAX_ID ||
        !t->present[id] || !t->amc[id] || !t->timeout[id] || !raw || !translated || intid < 32 ||
        intid > 1019)
        return 0;
    r.driver_id = id;
    r.gic_intid = intid;
    r.owner_epoch = t->owner_epoch;
    r.amc_work_item = (void*)&t->amc[id];
    r.interrupt_raw = raw;
    r.interrupt_translated = translated;
    *out = r;
    return 1;
}
