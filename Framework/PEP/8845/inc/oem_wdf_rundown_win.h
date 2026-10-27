#ifndef OEM_WDF_RUNDOWN_WIN_H
#define OEM_WDF_RUNDOWN_WIN_H
/* this adapter does not obtain an owner, establish PnP lifetime, connect the interrupt's
 * EvtInterruptDpc, or execute a firmware handoff. The owner MUST independently attest
 * hardware mask/unmask and all WDF-managed IRQ DPC completion. */

#include "oem_rpmh_host_fence.h"
#include "oem_wdf_work_provider.h"
#include "oem_wdf_dpc_tracker_win.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct oem_wdf_rundown_proofs {
    void* ctx;
    int (*exclusive_owner)(void*, uint32_t);
    int (*hardware_irq_masked)(void*);
    int (*hardware_irq_enabled)(void*);
    /* wdfinterrupt EvtInterruptDpc is NOT a WDFDPC: independent callback must prove all
     * queued, running and re-queue-capable IRQ DPCs drained. */

    int (*interrupt_dpcs_drained)(void*);
    /* must prove new callbacks cannot enqueue work during/after flushing. */
    int (*work_enqueue_stopped)(void*);
} oem_wdf_rundown_proofs_t;

typedef struct oem_wdf_rundown_win {
    void* interrupt; /* already connected/owned WDFINTERRUPT; PnP holds lifetime. */
    oem_rpmh_work_table_t* works;
    oem_rpmh_work_ops_t work_ops;
    oem_host_fence_ops_t base; /* queue lock/drain from owner. */
    oem_wdf_dpc_tracker_win_t* dpc_tracker;
    oem_wdf_rundown_proofs_t proofs;
    uint32_t epoch;
    unsigned char disabled, isr_synced, dpcs_drained, work_flushed, faulted;
} oem_wdf_rundown_win_t;

int oem_wdf_rundown_init_win(oem_wdf_rundown_win_t*, void* interrupt, oem_rpmh_work_table_t*,
                             const oem_rpmh_work_ops_t*, uint32_t, const oem_host_fence_ops_t*,
                             const oem_wdf_rundown_proofs_t*);
int oem_wdf_rundown_attach_dpc_tracker_win(oem_wdf_rundown_win_t*, oem_wdf_dpc_tracker_win_t*);
int oem_wdf_rundown_bind_host_ops_win(oem_wdf_rundown_win_t*, oem_host_fence_ops_t*);
#ifdef __cplusplus
}
#endif
#endif
