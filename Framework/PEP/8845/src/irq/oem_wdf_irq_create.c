#include "oem_wdf_irq_create.h"
#include <string.h>

static int valid_requests(const oem_wdf_irq_req_t* r, uint32_t n)
{
    uint32_t i, j;
    if (!r || !n || n > OEM_WDF_IRQ_MAX)
        return 0;
    for (i = 0; i < n; ++i) {
        if (!r[i].owner_epoch || !r[i].amc_work_item || !r[i].interrupt_raw ||
            !r[i].interrupt_translated || r[i].gic_intid < 32 || r[i].gic_intid > 1019)
            return 0;
        for (j = 0; j < i; ++j) {
            if (r[j].driver_id == r[i].driver_id || r[j].gic_intid == r[i].gic_intid ||
                r[j].interrupt_raw == r[i].interrupt_raw ||
                r[j].interrupt_translated == r[i].interrupt_translated ||
                r[j].owner_epoch != r[i].owner_epoch)
                return 0;
        }
    }
    return 1;
}

int oem_wdf_create_all(const oem_wdf_irq_req_t* entries, uint32_t cnt,
                       const oem_wdf_irq_create_ops_t* ops, oem_wdf_irq_bindings_t* out)
{
    oem_wdf_irq_bindings_t staging;
    uint32_t i;
    if (!ops || !ops->create || !ops->destroy_unpublished || !out ||
        !valid_requests(entries, cnt) || out->cnt)
        return 0;
    memset(&staging, 0, sizeof(staging));
    for (i = 0; i < cnt; ++i) {
        void* handle = 0;
        oem_wdf_irq_ctx_t* ctx = 0;
        oem_wdf_irq_ctx_t expected;
        memset(&expected, 0, sizeof(expected));
        expected.amc_work_item = entries[i].amc_work_item;
        expected.gic_intid = entries[i].gic_intid;
        expected.driver_id = entries[i].driver_id;
        expected.owner_epoch = entries[i].owner_epoch;
        if (!ops->create(ops->ctx, &entries[i], &handle, &ctx) || !handle || !ctx) {

            if (handle)
                ops->destroy_unpublished(ops->ctx, handle);
            goto rollback;
        }

        ctx->amc_work_item = expected.amc_work_item;
        ctx->gic_intid = expected.gic_intid;
        ctx->driver_id = expected.driver_id;
        ctx->owner_epoch = expected.owner_epoch;
        staging.slots[i].handle = handle;
        staging.slots[i].expected = expected;
        staging.cnt++;
    }
    *out = staging;
    return 1;
rollback:
    while (staging.cnt) {
        --staging.cnt;
        ops->destroy_unpublished(ops->ctx, staging.slots[staging.cnt].handle);
    }
    return 0;
}
