#include <ntddk.h>
#include <wdf.h>
#include "oem_wdf_dpc_tracker_win.h"

static LONG get(volatile LONG* x)
{
    return InterlockedCompareExchange(x, 0, 0);
}

static int matching(oem_wdf_dpc_tracker_win_t* t, void* h, uint64_t e)
{
    return t && h && t->interrupt == h && t->epoch == e && e != 0;
}

static int still(oem_wdf_dpc_tracker_win_t* t)
{
    return get(&t->outstanding) == 0 && get(&t->queuing) == 0 && get(&t->running) == 0 &&
           get(&t->accepted) == get(&t->finished);
}

void oem_wdf_dpc_quarantine_win(oem_wdf_dpc_tracker_win_t* t)
{
    if (t) {
        (void)InterlockedExchange(&t->flushed, 0);
        (void)InterlockedExchange(&t->phase, 3);
    }
}

int oem_wdf_dpc_tracker_init_win(oem_wdf_dpc_tracker_win_t* t, void* h, uint64_t epoch)
{
    if (!t || !h || !epoch || get(&t->phase))
        return 0;
    RtlZeroMemory(t, sizeof(*t));
    t->interrupt = h;
    t->epoch = epoch;
    (void)InterlockedExchange(&t->phase, 1);
    return 1;
}

int oem_wdf_dpc_queue_win(void* ctx)
{
    oem_wdf_dpc_tracker_win_t* t = ctx;
    BOOLEAN ok;
    if (!t || !t->interrupt || get(&t->phase) != 1)
        return 0;

    (void)InterlockedIncrement(&t->queuing);
    if (get(&t->phase) != 1) {
        (void)InterlockedDecrement(&t->queuing);
        return 0;
    }
    (void)InterlockedIncrement(&t->outstanding);
    (void)InterlockedIncrement(&t->accepted);
    ok = WdfInterruptQueueDpcForIsr((WDFINTERRUPT)t->interrupt);
    (void)InterlockedDecrement(&t->queuing);
    if (!ok) {
        /* outstanding includes our provisional +1; running callbacks cannot be used as evidence
         * that a queued callback still exists. Under the owner ISR synchronization, a separate
         * outstanding, not-yet-running ticket is sufficient to service this coalescing. */

        LONG queued_before = get(&t->outstanding) - get(&t->running) - 1;
        (void)InterlockedDecrement(&t->accepted);
        (void)InterlockedDecrement(&t->outstanding);
        if (queued_before > 0 && get(&t->phase) == 1)
            return 1;
        oem_wdf_dpc_quarantine_win(t);
        return 0;
    }
    return 1;
}

int oem_wdf_dpc_enter_win(oem_wdf_dpc_tracker_win_t* t, void* h)
{
    /* phase 3 forbids NEW queue admissions but a previously accepted DPC still has to run
     * (without dispatching power work) to retire its existing rundown reference. Otherwise
     * teardown is permanently pinned by a callback that KMDF actually delivered. */

    if (!t || !matching(t, h, t->epoch) || get(&t->phase) == 0 ||
        get(&t->outstanding) <= get(&t->running)) {
        oem_wdf_dpc_quarantine_win(t);
        return 0;
    }
    (void)InterlockedIncrement(&t->running);
    /* a second concurrent callback must have its own pre-reserved ticket. */

    if (get(&t->running) > get(&t->outstanding)) {
        (void)InterlockedDecrement(&t->running);
        oem_wdf_dpc_quarantine_win(t);
        return 0;
    }
    return 1;
}

int oem_wdf_dpc_leave_win(oem_wdf_dpc_tracker_win_t* t, void* h)
{
    if (!t || !matching(t, h, t->epoch) || get(&t->running) <= 0 || get(&t->outstanding) <= 0) {
        oem_wdf_dpc_quarantine_win(t);
        return 0;
    }
    (void)InterlockedIncrement(&t->finished);
    (void)InterlockedDecrement(&t->outstanding);
    (void)InterlockedDecrement(&t->running);
    return get(&t->phase) != 3;
}

int oem_wdf_dpc_close_win(oem_wdf_dpc_tracker_win_t* t, void* h, uint64_t epoch)
{
    return matching(t, h, epoch) && InterlockedCompareExchange(&t->phase, 2, 1) == 1;
}

int oem_wdf_dpc_flush_proof_win(oem_wdf_dpc_tracker_win_t* t, void* h, uint64_t epoch)
{
    if (!matching(t, h, epoch) || get(&t->phase) != 2 || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;

    KeFlushQueuedDpcs();
    if (get(&t->phase) != 2 || !still(t)) {
        oem_wdf_dpc_quarantine_win(t);
        return 0;
    }
    (void)InterlockedExchange(&t->flushed, 1);
    return 1;
}

int oem_wdf_dpc_reopen_win(oem_wdf_dpc_tracker_win_t* t, void* h, uint64_t epoch)
{
    if (matching(t, h, epoch) && still(t) && InterlockedCompareExchange(&t->phase, 1, 2) == 2) {
        (void)InterlockedExchange(&t->flushed, 0);
        return 1;
    }
    return 0;
}
