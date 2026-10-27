#ifndef OEM_WDF_IRQ_CREATE_H
#define OEM_WDF_IRQ_CREATE_H
/* a matched PnP desc NEVER establishes controller or GIC ownership. The caller must have
 * an independent, verified grant before invoking. cannot provide or infer such a grant. */

#include <stdint.h>
#include "oem_wdf_irq_bridge.h"
#include "oem_wdf_irq_mask_owner.h"
/* do not pull ntddk.h into this portable request/ABI header. */
typedef struct oem_wdf_dpc_tracker_win oem_wdf_dpc_tracker_win_t;
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_WDF_IRQ_MAX 8u

typedef struct oem_wdf_irq_req {
    uint32_t driver_id;
    uint32_t gic_intid;
    uint64_t owner_epoch;
    void* amc_work_item; /* wdf context offset 0x00. */
    const void* interrupt_raw;
    const void* interrupt_translated;
} oem_wdf_irq_req_t;

typedef struct oem_wdf_irq_ctx {
    void* amc_work_item; /* offset +0x00. */
    uint32_t gic_intid;  /* offset +0x08. */
    uint32_t driver_id;
    uint64_t owner_epoch;

    uint32_t passive_irq_configured;
    /* appended after the original two fields: external owner only. */
    oem_wdf_irq_bridge_t* bridge;
    volatile long bridge_enabled;
    /* wdfinterruptacquirelock must never run on an unenabled object. */

    volatile long framework_enabled;
    oem_wdf_dpc_tracker_win_t* dpc_tracker;
    oem_irq_mask_owner_t* irq_mask_owner;
    /* a future native owner must supply as its queue_pep_work callback. The existing worker
     * performs the one-and-only TCS completion. */

    void* completion_work_item;
    volatile long completion_pending;
    volatile long completion_queued;
    volatile long completion_faulted;
    volatile long completion_closing;
    /* per-transport-attempt diagnostic only; an ISR does not write it. */
    volatile long last_tcs_irq_lock_refusal;
    volatile long bridge_admission_fault_reported; /* one per immutable owner publication */
    volatile long tcs_isr_observed;
    /* independent OEM-style timeout work: the native ticket stays owned by the canonical
     * scheduler. This worker only inspects immutable evidence. */
    void* timeout_work_item;
    void* timeout_review_ctx;
    int (*timeout_review)(void*, uint64_t epoch, uint32_t req);
    uint64_t timeout_epoch;
    uint32_t timeout_req;
    volatile long timeout_queued;
    volatile long timeout_faulted;
} oem_wdf_irq_ctx_t;

typedef struct oem_wdf_irq_slot {
    void* handle;
    oem_wdf_irq_ctx_t expected;
} oem_wdf_irq_slot_t;

typedef struct oem_wdf_irq_bindings {
    uint32_t cnt;
    oem_wdf_irq_slot_t slots[OEM_WDF_IRQ_MAX];
} oem_wdf_irq_bindings_t;

typedef struct oem_wdf_irq_create_ops {
    void* ctx;
    /* backend must use the paired RAW+TRANSLATED descriptors when created during KMDF
     * EvtDevicePrepareHardware. It must NOT activate the ISR. */

    int (*create)(void* ctx, const oem_wdf_irq_req_t* req, void** out_handle,
                  oem_wdf_irq_ctx_t** out_ctx);
    /* delete only a newly created, never-activated interrupt object. */
    void (*destroy_unpublished)(void* ctx, void* handle);
} oem_wdf_irq_create_ops_t;

int oem_wdf_create_one_win(void* dev, const oem_wdf_irq_req_t* req, void** out_handle,
                           oem_wdf_irq_ctx_t** out_ctx);
/* result-bearing PnP destructor. Returns 1 ONLY after WDF worker flush and actual object
 * deletion; 0 leaves the handle and owner state intact. */

int oem_wdf_try_destroy_unpublished_win(void* dev, void* handle);
void oem_wdf_destroy_unpublished_win(void* dev, void* handle);
/* must only be attached by an exclusive controller owner under external WDF ISR/DPC
 * synchronization. */

/* publish the fully prepared native owner to an inert WDF IRQ exactly once, before KMDF
 * EvtInterruptEnable. The physical GIC mask, complete owner-bound AMC pipeline and DPC
 * tracker must already be verified. */

int oem_wdf_publish_native_owner_win(void* handle, uint64_t owner_epoch,
                                     oem_wdf_irq_bridge_t* bridge,
                                     oem_wdf_dpc_tracker_win_t* tracker,
                                     oem_irq_mask_owner_t* mask);

int oem_wdf_attach_bridge_win(void* handle, oem_wdf_irq_bridge_t* bridge);
void oem_wdf_disable_bridge_win(void* handle);
/* tracker installation is owner-only before bridge publication. The pipeline queue_dpc
 * hook MUST exclusively call the tracked queue wrapper. */

int oem_wdf_attach_dpc_tracker_win(void* handle, oem_wdf_dpc_tracker_win_t* tracker);
/* invoked by a future OWNER's queued PEP AMC completion callback, not by the WDF DPC
 * itself. Must run after DPC and with work-item lifetime held. */

int oem_wdf_run_amc_worker_win(void* handle);
/* <= DISPATCH_LEVEL only; never called from ISR/DIRQL. An attached native owner wires this
 * to the pipeline's queue_pep_work callback. */

int oem_wdf_queue_amc_work_win(void* handle);
/* passive only. Caller holds a verified live owner but no scheduler mutex. on a proven
 * issued timeout, queue an epoch-bound, read-only WDF worker. Review returns 1 only for a
 * valid ticket observation, NOT vote success. */
int oem_wdf_queue_timeout_review_win(void* handle, uint64_t epoch, uint32_t req,
                                     int (*review)(void*, uint64_t, uint32_t), void* ctx);
/* does not remove an active owner's outstanding ticket; refuses unsafe pending teardown. */

int oem_wdf_completion_idle_win(void* handle);
/* before first WDF owner publication, prove there is no ISR/DPC/worker closure or
 * framework interrupt enable. used only for rollback of an AMC prepare that has NOT
 * published the native pipeline to KMDF. */

int oem_wdf_owner_publication_vacant_win(void* handle);

int oem_wdf_check_tcs_command_owner_win(void* handle);
void oem_wdf_leave_tcs_command_owner_win(void* handle);
int oem_wdf_framework_connected_win(void* handle);
int oem_wdf_framework_disconnected_win(void* handle);
/* read-only observation after the framework's physical IRQ connection. Checks the bound
 * bridge, full owner epoch and actual physical GICD enable readback. */

int oem_wdf_d0_owner_ready_win(void* handle, uint64_t owner_epoch);
/* require the owner's WDF context to reference the EXACT backend-retained nonpaged bridge,
 * DPC tracker and physical mask owner, not a stack-local or foreign-epoch object. */

int oem_wdf_d0_owner_matches_win(void* handle, uint64_t owner_epoch,
                                 const oem_wdf_irq_bridge_t* expected_bridge,
                                 const oem_wdf_dpc_tracker_win_t* expected_tracker,
                                 const oem_irq_mask_owner_t* expected_mask);
/* only after WDF disable, ISR/DPC flush, actual masked readback and completion ledger
 * drain. Detaches a retired owner before PnP deletes the interrupt; refuses a
 * bound/inflight worker without faking drain. */

int oem_wdf_retire_owner_win(void* handle, uint64_t owner_epoch);
/* after framework disable, DPC flush, physical mask verification and pipeline release, flush
 * passive work before detaching pointers. Restore a vacant context only after that drain. */

int oem_wdf_finalize_published_owner_win(void* handle, uint64_t owner_epoch);
/* drain workers after EvtInterruptDisable and DPC close/flush. Run outside the backend lifecycle
 * mutex. */

int oem_wdf_drain_published_work_win(void* handle, uint64_t owner_epoch);
/* read-only exact published-owner / framework-disabled / masked-IRQ proof. */
int oem_wdf_published_owner_stopped_win(void* handle, uint64_t owner_epoch);

/* io.irq_lock_ctx must be the SAME WDFINTERRUPT used by its WDF ISR. A failed try is not
 * permission to reissue previously programmed TCS commands or release an issued ticket. */

/* refusal values match AMC busy-IRQ statuses after the transport translates them. A zero
 * indicates no refusal; this is NOT a lock or an ownership grant. */
uint32_t oem_wdf_read_last_tcs_lock_refusal_win(void* interrupt);
int oem_wdf_enter_tcs_irq_lock_win(void* interrupt);
void oem_wdf_leave_tcs_irq_lock_win(void* interrupt);
/* authorized owner must attach while WDF interrupt callbacks are stopped. This NEVER
 * grants rights or sets an register address. */

int oem_wdf_attach_irq_mask_owner_win(void* handle, oem_irq_mask_owner_t* owner);

/* caller serializes with all WDF/PnP callbacks. */

int oem_wdf_create_all(const oem_wdf_irq_req_t* entries, uint32_t cnt,
                       const oem_wdf_irq_create_ops_t* ops, oem_wdf_irq_bindings_t* out);
#ifdef __cplusplus
}
#endif
#endif
