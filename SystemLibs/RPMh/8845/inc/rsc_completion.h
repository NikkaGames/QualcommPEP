#ifndef RSC_COMPLETION_H
#define RSC_COMPLETION_H
/* pure per-driver RSC AMC completion bookkeeping, no hardware access. BOOT.MXF RpmhLib
 * rpmh_tcs.c: get_finished_amc / tcs_clean_up / tcs_finish_active_amc associate a finished
 * TCS with its request/client. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RSC_AMCS 3u
#define RSC_AMC_MASK 0x07u

typedef enum rsc_amc_state {
    RSC_AMC_VACANT = 0,
    RSC_AMC_IN_FLIGHT = 1,
    RSC_AMC_SIGNALED = 2,
    RSC_AMC_ABORTED = 3,
    RSC_AMC_AWAITING_ACK = 4
} rsc_amc_state_t;

typedef struct rsc_amc_track {
    uint32_t req_id;
    uint32_t owner_epoch;
    uint8_t state;
    uint8_t reserved;
    uint16_t expected_command_mask;
    uint16_t expected_dependency_mask;
} rsc_amc_track_t;

typedef struct rsc_completion {
    uint32_t epoch;
    uint8_t granted_mask;
    uint8_t irq_bound;
    uint8_t reserved[2];
    rsc_amc_track_t amc[RSC_AMCS];
    uint32_t unexpected_irq_cnt;
    uint32_t stale_irq_cnt;
} rsc_completion_t;

void rsc_completion_init(rsc_completion_t* p);
/* called only by a verified Windows owner/IRQ binding. */
int rsc_completion_bind(rsc_completion_t* p, uint32_t epoch, uint8_t independently_granted_amcs,
                        uint8_t irq_bound);
/* bookkeeping submissions cannot establish hardware completion. */

int rsc_completion_submit_batch(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                                uint16_t expected_command_mask, uint8_t stale_status_cleared);
/* rpmhlib has separate cmd_en_bmsk and dependency_bmsk: record both as immutable
 * owner-issued transaction metadata. No MMIO or right is granted. */

int rsc_completion_submit_batch_with_dependencies(rsc_completion_t* p, uint32_t epoch, uint8_t slot,
                                                  uint32_t req_id, uint16_t command_mask,
                                                  uint16_t dependency_mask,
                                                  uint8_t stale_status_cleared);
/* legacy mask-less ledger submission; incompatible with hardware completion. */
int rsc_completion_submit(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                          uint8_t stale_status_cleared);
/* this call makes no hardware modifications and must never be used as proof of ownership. */

uint8_t rsc_completion_observe_irq(rsc_completion_t* p, uint32_t epoch, uint32_t irq_status);
/* separately report proven hardware acknowledgement and command completion. caller is
 * responsible for checking all command statuses and clearing the TCS completion interrupt
 * with appropriate barriers on the verified owner. */

int rsc_completion_confirm_ack(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t req_id,
                               uint8_t irq_acknowledged, uint8_t commands_complete);
int rsc_completion_take(rsc_completion_t* p, uint32_t epoch, uint8_t slot, uint32_t* req_id,
                        uint8_t* aborted);
/* software state is not atomic: the owner must synchronize submit, IRQ observation,
 * confirmation, take and revoke with ISR/DPC and teardown. Hardware teardown must be
 * synchronized with ISR/DPC before this call. */

void rsc_completion_revoke(rsc_completion_t* p);
#ifdef __cplusplus
}
#endif
#endif
