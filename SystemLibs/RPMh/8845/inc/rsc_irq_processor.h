#ifndef RSC_IRQ_PROC_H
#define RSC_IRQ_PROC_H
/* read-status -> verify all TCS commands -> acknowledge exact AMC bit -> verify clear ->
 * retire exact request. It does not discover an IRQ, acquire DRV2, map MMIO, or configure
 * a WDF ISR. */

#include "rsc_completion.h"
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsc_irq_io {
    void* ctx;
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
} rsc_irq_io_t;

typedef enum rsc_irq_result {
    RSC_IRQ_INVALID = -1,
    RSC_IRQ_IO_ERROR = -2,
    RSC_IRQ_NOT_OWNED = -3,
    RSC_IRQ_NO_PENDING = 0,
    RSC_IRQ_COMMANDS_PENDING = 1,
    RSC_IRQ_ACK_PENDING = 2,
    RSC_IRQ_COMPLETED = 3,
    RSC_IRQ_DEFERRED = 4
} rsc_irq_result_t;

/* no hardware access at all unless epoch, bound handler, slot ownership, in-flight request
 * identity and IO callback provenance are all valid. */

rsc_irq_result_t rsc_irq_process(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                 uint32_t owner_epoch, uint8_t slot, uint32_t req_id,
                                 uint16_t expected_command_mask);

/* both masks must match the immutable ticket and committed hardware sequence. the owner serializes
 * ISR/worker access. */

rsc_irq_result_t rsc_irq_process_with_dependencies(rsc_completion_t* ledger, const rsc_irq_io_t* io,
                                                   uint32_t owner_epoch, uint8_t slot,
                                                   uint32_t req_id, uint16_t expected_command_mask,
                                                   uint16_t expected_dependency_mask);
#ifdef __cplusplus
}
#endif
#endif
