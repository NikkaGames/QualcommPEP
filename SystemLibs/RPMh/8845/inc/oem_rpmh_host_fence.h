#ifndef OEM_RPMH_HOST_FENCE_H
#define OEM_RPMH_HOST_FENCE_H
/* serialize producer and IRQ rundown; the fence does not establish hardware ownership. */

#include "oem_amc_command_queue.h"
#include "oem_tcs_batch_async.h"
#include "rpmh_irq_pipeline.h"
#include "rpmh_irq_rundown.h"
#include "oem_sleepwake_handoff.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum oem_host_fence_phase {
    HOST_FENCE_EMPTY = 0,
    HOST_FENCE_OPEN = 1,
    HOST_FENCE_CLOSED = 2,
    HOST_FENCE_DRAINED = 3,
    HOST_FENCE_FAULT = 4
} oem_host_fence_phase_t;

typedef struct oem_host_fence_ops {
    void* ctx;
    /* use the completion lock; callbacks must not re-enter. */

    int (*enter_queue_lock)(void*);
    void (*leave_queue_lock)(void*);
    /* may block; no completion lock is held by this caller. */

    int (*drain_accepted)(void*);

    int (*disable_owned_interrupt)(void*);
    int (*sync_isr)(void*);
    int (*flush_dpcs)(void*);
    int (*flush_deferred_work)(void*);
    /* reenable only after independent firmware/resume/release proof. Must confirm original
     * exclusive owner before either WDF or MMIO use. */

    int (*exclusive_owner)(void*, uint32_t epoch);
    int (*enable_owned_interrupt)(void*);
} oem_host_fence_ops_t;

typedef struct oem_host_fence {
    rpmh_irq_gate_t admission;
    oem_amc_command_queue_t* queue;
    oem_tcs_batch_t* batch;
    rpmh_irq_pipeline_t* pipeline;
    oem_host_fence_ops_t ops;
    uint32_t owner_epoch;
    volatile int64_t gate_epoch;
    uint64_t lease;
    uint64_t lease_sequence;
    volatile int32_t phase;
    uint8_t interrupt_disabled;
} oem_host_fence_t;

int oem_host_fence_init(oem_host_fence_t*, oem_amc_command_queue_t*, oem_tcs_batch_t*,
                        rpmh_irq_pipeline_t*, uint32_t, const oem_host_fence_ops_t*);
/* must replace every public NEW-REQUEST admission, with no bypass. */

int oem_host_fence_enqueue(oem_host_fence_t*, uint32_t req_id, uint32_t res_key, uint8_t is_read,
                           uint64_t* token);

int oem_host_fence_and_drain(void*, uint32_t epoch, uint64_t* lease);
int oem_host_fence_still_drained(void*, uint32_t epoch, uint64_t lease);
int oem_host_fence_release(void*, uint32_t epoch, uint64_t lease);

/* stable lifetime: owner retains the binding until fully closes. the firmware callbacks
 * must be independently implemented/verified; this adapter only supplies context routing,
 * not firmware behavior/authority. */

typedef struct oem_host_fence_handoff_binding {
    oem_host_fence_t* fence;
    void* firmware_ctx;
    int (*never_started)(void*, uint32_t, uint64_t, uint64_t);
    int (*transfer_to_firmware)(void*, uint32_t, uint64_t, uint64_t, uint8_t);
    int (*firmware_returned)(void*, uint32_t, uint64_t, uint64_t);
} oem_host_fence_handoff_binding_t;

int oem_host_fence_bind_handoff(oem_host_fence_handoff_binding_t*, oem_sleepwake_handoff_ops_t*);
#ifdef __cplusplus
}
#endif
#endif
