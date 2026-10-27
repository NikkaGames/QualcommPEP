#ifndef RSC_IRQ_LATCHED_H
#define RSC_IRQ_LATCHED_H
/* exact RSC AMC capture and ACK. The caller supplies physical ownership and serialization;
 * a latch cannot be replayed against a later request. */

#include "rsc_irq_processor.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsc_irq_latch {
    uint32_t owner_epoch;
    uint32_t req_id;
    uint16_t expected_command_mask;
    uint16_t expected_dependency_mask;
    uint8_t slot;
    uint8_t valid;
    uint8_t ack_attempted; /* sticky W1C attempt; permits retry after unclear readback. */
} rsc_irq_latch_t;

/* read-only capture. Masking is an independent owner operation; capture cannot acknowledge
 * the hardware or confer a controller grant. */

rsc_irq_result_t rsc_irq_capture_pending(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                         uint32_t epoch, uint8_t slot, uint32_t req_id,
                                         uint16_t command_mask, rsc_irq_latch_t* latched);

/* rsc-local masking is accepted only through the same retained src, native owner and
 * ledger epoch that recorded the physical mask write. */
rsc_irq_result_t rsc_irq_capture_owned_src(const rpmh_irq_src_t* src, uint64_t owner_epoch,
                                           uint8_t slot, uint32_t req_id, uint16_t command_mask,
                                           rsc_irq_latch_t* latched);

rsc_irq_result_t rsc_irq_capture_ack(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                     uint32_t epoch, uint8_t slot, uint32_t req_id,
                                     uint16_t command_mask, rsc_irq_latch_t* latched);

/* deferred worker: validate the captured, NOT YET acknowledged request; check every
 * enabled command completion, W1C only the exact owned TCS bit, verify ACK by readback and
 * retire the immutable request. Never W1C an incomplete command. */

rsc_irq_result_t rsc_irq_finish_latched(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                        rsc_irq_latch_t* latched);

#ifdef __cplusplus
}
#endif
#endif
