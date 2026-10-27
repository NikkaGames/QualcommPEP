#ifndef OEM_AMC_COMPLETION_H
#define OEM_AMC_COMPLETION_H
/* not an owner, an MMIO mapper, or a production callback reg. every call is serialized
 * under the externally validated RPMh completion lock (as for ). */

#include <stdint.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum oem_amc_ack_state {
    AMC_ACK_IDLE = 0,
    AMC_ACK_OUTCOME_UNKNOWN = 1,
    AMC_ACK_CONFIRMED = 2
} oem_amc_ack_state_t;

typedef struct oem_amc_completion_ops {
    void* ctx;
    int (*read32)(void*, uint32_t offset, uint32_t*);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    /* this cannot be replaced by checking IRQ_STATUS alone. */

    int (*resolve_inflight)(void*, uint8_t slot, uint64_t* token);
    /* must finish the command queue before reusing the TCS; called exactly once after positive
     * ACK readback, never before the W1C operation. */

    int (*complete_inflight)(void*, uint8_t slot, uint64_t token);
} oem_amc_completion_ops_t;

typedef struct oem_amc_completion {
    oem_amc_completion_ops_t ops;
    /* the live owner must revoke only after WDF ISR/DPC/work-item rundown. */

    uint8_t external_grant_mask;
    uint8_t completion_handler_bound;
    uint8_t active_inflight_mask;
    uint8_t faulted;
    oem_amc_ack_state_t ack_state;
    uint8_t latched_slot;
    uint64_t latched_token;
    uint32_t finished;
} oem_amc_completion_t;

/* init never enables hardware or grants ownership. */
int oem_amc_init(oem_amc_completion_t*, const oem_amc_completion_ops_t*);
/* both calls must run under the same external completion serialization lock. */

int oem_amc_has_finished(void*);

int oem_amc_finish_one(void*);
#ifdef __cplusplus
}
#endif
#endif
