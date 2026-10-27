#ifndef OEM_WDF_DPC_TRACKER_WIN_H
#define OEM_WDF_DPC_TRACKER_WIN_H
/* only the matched owner may bind this in nonpaged WDF/PnP-retained storage. ISR queue +
 * EvtInterruptDpc entry/exit MUST use these APIs exclusively. */

#include <stdint.h>
#include <ntddk.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct oem_wdf_dpc_tracker_win {
    void* interrupt;
    /* full owner generation: Windows backend uses 64-bit epochs with a nonzero high byte. */

    uint64_t epoch;
    volatile LONG phase;
    volatile LONG outstanding;
    volatile LONG queuing;
    volatile LONG running;
    volatile LONG accepted;
    volatile LONG finished;
    volatile LONG flushed;
} oem_wdf_dpc_tracker_win_t;

int oem_wdf_dpc_tracker_init_win(oem_wdf_dpc_tracker_win_t*, void*, uint64_t);
/* must be the sole QCPEP_RPMH_WORK_OPS.queue_dpc path for this interrupt. FALSE from
 * WdfInterruptQueueDpcForIsr means no new callback is queued; retire ONLY that queue
 * attempt and quarantine the owner. */

int oem_wdf_dpc_queue_win(void* tracker_ctx);
int oem_wdf_dpc_enter_win(oem_wdf_dpc_tracker_win_t*, void*);
int oem_wdf_dpc_leave_win(oem_wdf_dpc_tracker_win_t*, void*);
/* call only after: hardware mask independently VERIFIED, WDF interrupt disabled and
 * WdfInterruptSynchronize finished, no new software producer. */

int oem_wdf_dpc_close_win(oem_wdf_dpc_tracker_win_t*, void*, uint64_t);
int oem_wdf_dpc_flush_proof_win(oem_wdf_dpc_tracker_win_t*, void*, uint64_t);
/* reopen while hardware still masked, immediately before WDF enable. */
int oem_wdf_dpc_reopen_win(oem_wdf_dpc_tracker_win_t*, void*, uint64_t);
void oem_wdf_dpc_quarantine_win(oem_wdf_dpc_tracker_win_t*);
#ifdef __cplusplus
}
#endif
#endif
