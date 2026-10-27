#ifndef OEM_RPMH_WORKITEMS_H
#define OEM_RPMH_WORKITEMS_H
/* parent owner serializes create/queue/drain against PnP. */

#include <stdint.h>
#include "oem_wdf_irq_create.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_RPMH_WORK_MAX_ID 16u
#define OEM_RPMH_WORK_MAX_DRIVERS 8u
#define OEM_RPMH_WORK_CRITICAL 1u

typedef void (*oem_rpmh_handler_t)(void*);

typedef struct oem_rpmh_work_cfg {
    uint32_t work_type;
    oem_rpmh_handler_t handler;
    void* handler_param;
} oem_rpmh_work_cfg_t;

typedef struct oem_rpmh_work_ops {
    void* ctx;
    int (*create)(void*, const oem_rpmh_work_cfg_t*, void**);
    int (*enqueue)(void*, void*);
    /* wait must not return success until the callback and queued work are done. */

    int (*wait)(void*, void*);
    int (*destroy)(void*, void*);
} oem_rpmh_work_ops_t;

typedef struct oem_rpmh_work_entry {
    uint32_t driver_id;
    uint32_t hw_driver;
} oem_rpmh_work_entry_t;

typedef struct oem_rpmh_work_table {

    void* amc[OEM_RPMH_WORK_MAX_ID + 1u];
    void* timeout[OEM_RPMH_WORK_MAX_ID + 1u];
    uint8_t present[OEM_RPMH_WORK_MAX_ID + 1u];
    uint32_t cnt;
    uint64_t owner_epoch;
    uint8_t published;
    uint8_t faulted;
} oem_rpmh_work_table_t;

/* if rollback itself fails, the handles are retained in faulted state for a later passive
 * cleanup. */

int oem_rpmh_work_create(oem_rpmh_work_table_t*, const oem_rpmh_work_entry_t*, uint32_t, uint64_t,
                         oem_rpmh_handler_t, oem_rpmh_handler_t, const oem_rpmh_work_ops_t*);
/* caller owns serialization, hardware quiescence, and worker synchronization. */

void oem_rpmh_work_close(oem_rpmh_work_table_t*);
int oem_rpmh_work_queue_timeout(oem_rpmh_work_table_t*, uint32_t driver_id, uint64_t owner_epoch,
                                const oem_rpmh_work_ops_t*);
int oem_rpmh_work_queue_amc(oem_rpmh_work_table_t*, uint32_t, uint64_t, const oem_rpmh_work_ops_t*);

int oem_rpmh_work_drain_destroy(oem_rpmh_work_table_t*, int external_quiesced,
                                const oem_rpmh_work_ops_t*);
/* builds the exact pointer-to-slot used by the original DPC; no PnP, GIC or RPMh access is
 * enabled by this helper. */

int oem_rpmh_work_make_irq_req(oem_rpmh_work_table_t*, uint32_t, uint32_t, const void*, const void*,
                               oem_wdf_irq_req_t*);
#ifdef __cplusplus
}
#endif
#endif
