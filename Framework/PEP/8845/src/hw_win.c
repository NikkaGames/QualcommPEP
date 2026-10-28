#include "clock_target.h"
/* the res owners and their published client state share one teardown epoch. */

#include "ufs_native_program.h"
#include "hw_backend_windows_wdf.h"
#include "hw_win.h"
#include "mmio_win.h"
#include "rsc_d0_map_win.h"
#include "gic_d0_map_win.h"
#include "gic_irq37_owned_win.h"
#include "oem_wdf_irq_create.h"
#include "oem_wdf_dpc_tracker_win.h"
#include "rpmh_irq_pipeline.h"
#include "rpmh_amc_drain.h"
#include "gcc.h"
#include "config.h"
#include "commit.h"
#include "bus.h"
#include "pmic_rail.h"
#include "platform_resources.h"
#include "rpmh_tcs.h"
#include "rpmh_oem_read_sequence.h"
#include "rpmh_write_fence.h"
#include "rpmh_busy_arbitration.h"
#include "rpmh_client_scheduler.h"
#include "rpmh_sync_deferred.h"
#include "rpmh_deferred_scheduler.h"
#include "rsc_completion.h"
#include "tlmm.h"
#include "io_target.h"
#include "power_delay.h"
#include "acpi.h"
#include "res_graph.h"
#include "npa_client.h"
#include "owner_attach_policy.h"
#include "pnp_resources.h"
#include "ufs_cxmx_owner.h"
#include "ufs_native_provision.h"
#include "required.h"
#include "scalar_required.h"
#include "group_vote.h"
#include "gdsc_native.h"
#include "arc_rail.h"
#include "domains.h"
#include "trace.h"
#include "hw_win.tmh"

NTSTATUS pep_publish_verified_molokai_graph(_Inout_ res_graph_t* graph,
                                            _Inout_ npa_registry_t* npa_clients);
NTSTATUS pep_unpublish_verified_molokai_graph(VOID);

#define RPMH_CLIENT_BUS 0u
#define RPMH_CLIENT_RAIL 1u
#define RPMH_CLIENT_READ 2u
#define RPMH_CLIENT_PROBE 3u
#define OWNER_TCS 0u /* amc slot this owner uses. */
#define RPMH_POLL_LIMIT 20000u
#define CLOCK_CLIENT_MAX 512u /* exhaustion refuses before mutation. */
#define CLOCK_TABLE_MAX 256u  /* 153 vendor-mapped GCC entries; fixed bounded nonpaged arrays. */
#define TLMM_PROBE_PIN 52u

/* each is a live hardware read; together they are what the production attestation means. */

#define PROBE_CMDDB 0x01u
#define PROBE_RSC_ID 0x02u /* hlos RSC identity and an idle owned TCS. */
#define PROBE_RPMH 0x04u
#define PROBE_GPLL0 0x08u
#define PROBE_CLOCK 0x10u
#define PROBE_TLMM 0x20u
#define PROBE_ALL 0x3Fu

typedef struct clock_client {
    UCHAR in_use;
    CHAR path[256];
    ULONG clock_idx;
    UCHAR enable;
    ULONG hz;
} clock_client_t;

typedef struct ufs_group_bridge ufs_group_bridge_t;

typedef struct owners {
    mmio_t mmio;
    /* D0 maps these windows independently of RPMh authorization. */
    rsc_d0_map_win_t d0_rsc;
    gic_d0_map_win_t d0_gic;
    gic_irq37_owned_win_t gic_irq37_owner;

    /* firmware-configured DRV2 lifetime; the fixed window is mapped independently of _CRS. */

    struct {
        WDFDEVICE dev;
        void* interrupt;
        uint64_t owner_epoch;
        uint64_t pnp_generation;
        uint8_t granted_amcs;
        uint8_t pre_enabled_amc_irq_mask;
        volatile LONG active;
    } os_rsc_lease;

    /* the WDF/ISR/DPC/AMC objects MUST outlive every callback, deferred TCS ticket and d0
     * mapping. Before this, no PEP0 owner retained a pipeline, DPC tracker or bridge at all;
     * callers could only bind external or temporary storage. */

    rpmh_irq_pipeline_t rpmh_irq_pipeline;
    oem_wdf_irq_bridge_t rpmh_irq_bridge;
    oem_wdf_dpc_tracker_win_t rpmh_dpc_tracker;
    oem_irq_mask_owner_t rpmh_irq_mask_owner;
    oem_irq_mask_ops_t rpmh_irq_mask_ops;
    /* they must remain alive across both the WDF IRQ and deferred worker. */

    rpmh_irq_src_t rpmh_irq_src;
    rpmh_amc_drain_t rpmh_amc_drain;
    rpmh_work_ops_t rpmh_owner_work_ops;
    UCHAR* cmd_db; /* private snapshot, driver lifetime. */
    SIZE_T cmd_db_bytes;
    /* these three paths may nest a PASSIVE KMDF interrupt wait lock or wait for an AMC
     * completion. KMUTEX serializes without raising IRQL; physical work still requires the
     * independently established native hardware owner. */

    KMUTEX lock;          /* owner operations and peer queries. */
    FAST_MUTEX tlmm_lock; /* tlmm-only, never nests the WDF IRQ wait lock. */
    KMUTEX rpmh_lock;     /* shared TCS/ISR/ACK serialization. */
    KMUTEX rpmh_scheduler_lock;
    KEVENT rpmh_completion_event;
    rpmh_sync_deferred_t rpmh_wait;
    rpmh_deferred_scheduler_t deferred_scheduler;
    gcc_io_t gcc_io;
    tlmm_io_t tlmm_io;
    rpmh_tcs_t rpmh;
    rpmh_scheduler_t rpmh_scheduler;
    rsc_completion_t rsc_completion;
    icb_bus_t bus;
    rail_owner_t rails;
    ufs_cxmx_owner_t ufs_rail_owner;
    arc_rail_t ufs_arc[2];
    uint32_t ufs_arc_last_address, ufs_arc_last_response;
    rpmh_tcs_result_t ufs_arc_last_result;
    fs_gdsc_driver_t ufs_domain_owner;
    ufs_native_scope_t ufs_native;
    group_vote_t* ufs_group_votes[2]; /* desc sets 1 and 2 */
    ufs_group_bridge_t* ufs_group_bridge;
    scalar_required_t* ufs_scalar_required;
    pofx_required_catalog_t* ufs_scalar_catalog;
    pofx_required_owner_t* ufs_scalar_owner;

    struct {
        char identity[256];
        uint32_t original_force_on;
        BOOLEAN active;
        BOOLEAN saved;
    } ufs_ice_config;

    BOOLEAN ufs_owner_uncertain;
    /* once RPMh completion is uncertain, no future d0 probe may clear the admission fence or
     * silently re-use software BCM/VRM vote snapshots. */

    rpmh_write_fence_t rpmh_write_fence;
    clock_client_t clock_clients[CLOCK_CLIENT_MAX];
    ULONG clock_programmed_hz[CLOCK_TABLE_MAX];
    UCHAR clock_uncertain[CLOCK_TABLE_MAX];
    /* sticky once a TLMM transaction may have touched hardware without a verified rollback. d0
     * revalidation must never convert an unknown pin state into an owner simply by reading a
     * stable value. */

    BOOLEAN tlmm_uncertain;
    ULONG probe;
    ULONG rsc_id, drv_config, gpll0_mode;
    /* reported coverage (never gating). */
    ULONG bcms_usable, bcms_total;
    ULONG rails_resolved, rails_total;
    ULONG routes_supported, routes_total;
    ULONG clocks_present, clocks_total;
    ULONGLONG clock_probe, tlmm_probe, rail_probe;
    BOOLEAN clocks_live, tlmm_live, rpmh_live;
} owners_t;

typedef struct hwio_win_ctx {
    WDFDEVICE dev;
    owners_t* owners;
    volatile LONG pmic_gpio_provider_live;
} hwio_win_ctx_t;

typedef struct win_backend_instance {
    win_backend_t core;
    hwio_win_ctx_t io;
} win_backend_instance_t;

/* one backend per PEP0 FDO; PnP serializes prepare/release/Surprise. */
static win_backend_instance_t* g_win_backend;
static volatile LONG g_win_backend_generation;
/* unverified IRQ cleanup pins the owner for the driver lifetime and forbids another
 * attach; never free its MMIO or lock. */

static owners_t* g_quarantined_rsc_owner;
/* never free until the owner has independently completed rundown; here quarantine for
 * driver lifetime and reject every subsequent attach. */

static win_backend_instance_t* g_quarantined_backend_instance;

/* published production objects (driver lifetime once published). */
static res_graph_t* g_published_graph;
static npa_registry_t* g_published_npa;
static npa_registry_t* g_quarantined_npa;
static WDFWAITLOCK g_published_graph_lock;
/* unpublished native clients can still hold graph callback pointers. */
static res_graph_t* g_quarantined_fs_graph;

/* process-lifetime PnP/peer/publication serialization. This KMUTEX is initialized in
 * DriverEntry BEFORE WdfDriverCreate can dispatch PnP. */

static KMUTEX g_win_lifecycle_lock;

VOID hw_backend_win_lifecycle_init(VOID)
{
    KeInitializeMutex(&g_win_lifecycle_lock, 0);
}

static int gcc_read(void* c, uint32_t off, uint32_t* v)
{
    return mmio_read32(&((owners_t*)c)->mmio.gcc, off, (ULONG*)v);
}

static int gcc_write(void* c, uint32_t off, uint32_t v)
{
    return mmio_write32(&((owners_t*)c)->mmio.gcc, off, v);
}

static void io_barrier(void* c)
{
    UNREFERENCED_PARAMETER(c);
    KeMemoryBarrier();
}

static void io_stall(void* c, uint32_t us)
{
    UNREFERENCED_PARAMETER(c);
    KeStallExecutionProcessor(us);
}

static int tlmm_read(void* c, uint32_t off, uint32_t* v)
{
    return mmio_read32(&((owners_t*)c)->mmio.tlmm, off, (ULONG*)v);
}

static int tlmm_write(void* c, uint32_t off, uint32_t v)
{
    return mmio_write32(&((owners_t*)c)->mmio.tlmm, off, v);
}

static int tlmm_lock_fn(void* c)
{
    if (KeGetCurrentIrql() > APC_LEVEL)
        return 0;
    ExAcquireFastMutex(&((owners_t*)c)->tlmm_lock);
    return 1;
}

static void tlmm_unlock_fn(void* c)
{
    ExReleaseFastMutex(&((owners_t*)c)->tlmm_lock);
}

static int tlmm_owns_pin(void* c, uint32_t pin)
{
    owners_t* o = (owners_t*)c;
    /* o->lock protects TLMM admission through rollback and retirement. Per-pin GPIO arbitration
     * still requires the qcgpio8480/ACPI ownership handoff. */

    return o && o->tlmm_live && !o->tlmm_uncertain && o->mmio.tlmm.base && pin < TLMM_PIN_CNT;
}

static int rpmh_read(void* c, uint32_t off, uint32_t* v)
{
    return mmio_read32(&((owners_t*)c)->mmio.rsc, off, (ULONG*)v);
}

static int rpmh_write(void* c, uint32_t off, uint32_t v)
{
    return mmio_write32(&((owners_t*)c)->mmio.rsc, off, v);
}

static void rpmh_spin(void* c)
{
    UNREFERENCED_PARAMETER(c);
    KeStallExecutionProcessor(1u);
}

static int rpmh_lock_fn(void* c)
{
    owners_t* o = (owners_t*)c;
    /* shared with the passive WDF ISR bridge and completion worker. A PASSIVE KMUTEX does not
     * elevate IRQL; no KMDF wait-lock is acquired while this command/ledger mutex is held. */

    return o && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           KeWaitForSingleObject(&o->rpmh_lock, Executive, KernelMode, FALSE, NULL) ==
               STATUS_SUCCESS;
}

static void rpmh_unlock_fn(void* c)
{
    (VOID) KeReleaseMutex(&((owners_t*)c)->rpmh_lock, FALSE);
}

/* the existing synchronous polling TCS writer is the temporary Windows execution adapter.
 * Each client submits through the bounded DRV2 scheduler, so BUSARB/PMIC/readback no
 * longer directly select a physical AMC slot. */

static rpmh_tcs_result_t rpmh_execute_sync_slot(void* ctx, uint8_t slot,
                                                const tcs_command_t* commands, uint32_t cnt,
                                                uint32_t* responses)
{
    owners_t* o = (owners_t*)ctx;
    if (!o || slot >= 3u)
        return TCS_INVALID;
    if (!(o->rpmh.owned_amc_mask & (1u << slot)))
        return TCS_NO_OWNERSHIP;
    /* the completion IRQ and per-command COMPLETED status still must be verified. Multi-
     * command legacy clients retain conservative full WAIT ordering until their res dependency
     * graph is reconstructed from firmware. */

    if (cnt == 1u)
        return rpmh_tcs_issue_batch_with_dependencies(&o->rpmh, slot, commands, cnt, 0u, responses);
    return rpmh_tcs_issue_batch_ex(&o->rpmh, slot, commands, cnt, responses);
}

/* the passive worker holds rpmh_scheduler_lock while capturing or retiring a ticket. It must not
 * acquire the outer operation mutex. */

static int rpmh_scheduler_enter(void* ctx)
{
    owners_t* o = ctx;
    /* the passive WDF AMC worker calls has_finished_amc/finish_one_amc while holding this
     * lock. Both paths acquire the KMDF interrupt wait lock; a FAST_MUTEX would elevate IRQL
     * and prevent any completion. */

    return o && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           KeWaitForSingleObject(&o->rpmh_scheduler_lock, Executive, KernelMode, FALSE, NULL) ==
               STATUS_SUCCESS;
}

static void rpmh_scheduler_leave(void* ctx)
{
    (VOID) KeReleaseMutex(&((owners_t*)ctx)->rpmh_scheduler_lock, FALSE);
}

static int rpmh_scheduler_try_enter(void* ctx)
{
    LARGE_INTEGER timeout;
    if (!ctx || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    timeout.QuadPart = 0;
    return KeWaitForSingleObject(&((owners_t*)ctx)->rpmh_scheduler_lock, Executive, KernelMode,
                                 FALSE, &timeout) == STATUS_SUCCESS;
}

static void rpmh_wait_reset(void* ctx)
{
    KeClearEvent(&((owners_t*)ctx)->rpmh_completion_event);
}

static void rpmh_wait_signal(void* ctx)
{
    (void)KeSetEvent(&((owners_t*)ctx)->rpmh_completion_event, IO_NO_INCREMENT, FALSE);
}

static void rpmh_observe_completion(void* ctx);

static int rpmh_wait_deadline(void* ctx, uint32_t ms)
{
    owners_t* o = ctx;
    LARGE_INTEGER interval;
    ULONGLONG started, deadline, now, remaining;
    ULONG waits = 0u;
    NTSTATUS status = STATUS_TIMEOUT;
    if (!o || !ms || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    started = KeQueryInterruptTime();
    deadline = started + (ULONGLONG)ms * 10000ULL;
    for (;;) {
        /* drain an already-pending completion before waiting on the system work queue. */
        rpmh_observe_completion(o);
        now = KeQueryInterruptTime();
        if (now >= deadline)
            break;
        remaining = deadline - now;
        interval.QuadPart = -(LONGLONG)(remaining < 100000ULL ? remaining : 100000ULL);
        status = KeWaitForSingleObject(&o->rpmh_completion_event, Executive, KernelMode, FALSE,
                                       &interval);
        ++waits;
        if (status != STATUS_TIMEOUT)
            break;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                "rpmh: wait ticket=%lu status=%08X waits=%lu elapsed-us=%llu",
                (ULONG)o->rpmh_wait.active_req, status, waits,
                (unsigned long long)((KeQueryInterruptTime() - started) / 10ULL));
    return status == STATUS_SUCCESS;
}

/* authorized owner must call this *after* scheduler promotion and before advertising RPMh
 * capabilities; the binding itself grants NOTHING. */

/* production callbacks that keep the ONE work callback context equal to the
 * backend-retained owner. mask/DPC/queue, WDF work and AMC drain all share this same
 * lifetime; a drain pointer alone cannot be used for physical GIC or WDF operations. */

static int os_rsc_lease_irq_proof(void*, uint64_t, void*, uint64_t);

static int rpmh_owner_mask_amc_irq(void* ctx)
{
    owners_t* o = ctx;
    uint64_t epoch;
    int ok;
    /* called with the transport mutex held. A passive ISR's GIC mask is owned by HAL; defer
     * only the controller sources granted to this PEP. */
    if (!o || !(epoch = o->os_rsc_lease.owner_epoch) ||
        !os_rsc_lease_irq_proof(o, epoch, o->os_rsc_lease.interrupt,
                                o->os_rsc_lease.pnp_generation))
        return 0;
    ok = rpmh_tcs_mask_owned_irq(&o->rpmh_irq_src, epoch);
    if (!ok)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: source mask failed epoch=%llX active=%u fault=%u",
                    (unsigned long long)epoch, (unsigned)o->rpmh_irq_src.irq_mask_active,
                    (unsigned)o->rpmh_irq_src.irq_mask_faulted);
    return ok;
}

static int rpmh_owner_unmask_amc_irq(void* ctx)
{
    owners_t* o = ctx;
    rpmh_irq_pipeline_t* p;
    rpmh_tcs_io_t* io;
    uint64_t epoch;
    int ok = 0;
    if (!o || !rpmh_scheduler_enter(o))
        return 0;
    epoch = o->os_rsc_lease.owner_epoch;
    p = &o->rpmh_irq_pipeline;
    io = &o->rpmh.io;
    if (io->lock && io->unlock && io->lock(io->ctx)) {
        if (os_rsc_lease_irq_proof(o, epoch, o->os_rsc_lease.interrupt,
                                   o->os_rsc_lease.pnp_generation) &&
            p->bound && !p->faulted && p->epoch == epoch && p->unmask_finalizing.active &&
            p->unmask_finalizing.epoch == epoch && !p->pending.active &&
            p->work.state == RPMH_WORK_COMPLETION_READY && p->work.irq_masked &&
            rpmh_irq_gate_outstanding(&p->gate) && !o->rpmh_scheduler.busy_mask &&
            !o->rpmh_scheduler.quarantined_mask)
            ok = rpmh_tcs_restore_owned_irq(&o->rpmh_irq_src, epoch);
        io->unlock(io->ctx);
    }
    rpmh_scheduler_leave(o);
    if (!ok)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: source restore failed epoch=%llX work=%u finalizing=%u active=%u "
                    "fault=%u owner=retained",
                    (unsigned long long)epoch, (unsigned)p->work.state,
                    (unsigned)p->unmask_finalizing.active,
                    (unsigned)o->rpmh_irq_src.irq_mask_active,
                    (unsigned)o->rpmh_irq_src.irq_mask_faulted);
    return ok;
}

static int rpmh_owner_queue_amc_dpc(void* ctx)
{
    owners_t* o = (owners_t*)ctx;
    return o && o->gic_irq37_owner.active &&
           o->rpmh_dpc_tracker.interrupt == o->gic_irq37_owner.wdf_interrupt &&
           oem_wdf_dpc_queue_win(&o->rpmh_dpc_tracker);
}

static int rpmh_owner_queue_amc_work(void* ctx)
{
    owners_t* o = (owners_t*)ctx;
    void* irq;
    uint64_t epoch;
    if (!o || !o->gic_irq37_owner.active || !(irq = o->gic_irq37_owner.wdf_interrupt) ||
        !(epoch = o->gic_irq37_owner.epoch))
        return 0;
    /* distinguishes transient WDF interrupt-lock contention from an actual AMC fault. The WDF
     * work item can service an immediately requeued ticket IN THE SAME CALLBACK, consuming all
     * 16 attempts while another PASSIVE ISR still holds the wait lock. */

    if (KeGetCurrentIrql() == PASSIVE_LEVEL &&
        o->rpmh_irq_pipeline.work.state == RPMH_WORK_WORKER_PENDING &&
        o->rpmh_irq_pipeline.work.lock_retry_cnt && o->rpmh_irq_pipeline.pending.active &&
        o->rpmh_irq_pipeline.pending.epoch == epoch) {
        LARGE_INTEGER interval;
        interval.QuadPart = -10000LL;
        if (!NT_SUCCESS(KeDelayExecutionThread(KernelMode, FALSE, &interval)))
            return 0;
        /* the ticket's generation must still match before any worker is queued. */

        if (!o->gic_irq37_owner.active || o->gic_irq37_owner.epoch != epoch ||
            o->gic_irq37_owner.wdf_interrupt != irq || !o->rpmh_irq_pipeline.pending.active ||
            o->rpmh_irq_pipeline.pending.epoch != epoch ||
            o->rpmh_irq_pipeline.work.state != RPMH_WORK_WORKER_PENDING)
            return 0;
    }
    return oem_wdf_queue_amc_work_win(irq);
}

static int rpmh_owner_has_finished_amc(void* ctx)
{
    owners_t* o = (owners_t*)ctx;
    return o ? rpmh_amc_drain_has_finished(&o->rpmh_amc_drain) : -1;
}

static int rpmh_owner_finish_one_amc(void* ctx)
{
    owners_t* o = (owners_t*)ctx;
    return o ? rpmh_amc_drain_finish_one(&o->rpmh_amc_drain) : 0;
}

static int rpmh_owner_enter_completion_lock(void* ctx)
{
    return rpmh_scheduler_enter(ctx);
}

static void rpmh_owner_leave_completion_lock(void* ctx)
{
    rpmh_scheduler_leave(ctx);
}

/* signal only after the physical RSC src enables are restored and the final immutable
 * pipeline ticket is retired. No IRP is completed here. */
static void rpmh_owner_irq_restored(void* ctx)
{
    owners_t* o = (owners_t*)ctx;
    if (o && rpmh_scheduler_enter(o)) {
        (void)rpmh_sync_deferred_finalized(&o->rpmh_wait, o->rpmh_irq_pipeline.epoch);
        rpmh_scheduler_leave(o);
    }
}

/* lock order: WDF passive ISR -> RPMh mutex. Submission and ACK share that mutex; never
 * acquire the WDF lock while holding it. */
static int rpmh_owner_src_enter(void* ctx)
{
    rpmh_irq_src_t* s = (rpmh_irq_src_t*)ctx;
    return s && s->dev && s->dev->io.lock && s->dev->io.unlock && s->dev->io.ctx &&
           s->dev->io.lock(s->dev->io.ctx);
}

static void rpmh_owner_src_leave(void* ctx)
{
    rpmh_irq_src_t* s = (rpmh_irq_src_t*)ctx;
    if (s && s->dev && s->dev->io.unlock && s->dev->io.ctx)
        s->dev->io.unlock(s->dev->io.ctx);
}

static void rpmh_observe_completion(void* ctx)
{
    owners_t* o = ctx;
    void* irq;
    uint64_t epoch;
    uint32_t req, i;
    int captured = 0;
    if (!o || !(irq = o->os_rsc_lease.interrupt) || !(epoch = o->os_rsc_lease.owner_epoch) ||
        !oem_wdf_enter_tcs_irq_lock_win(irq))
        return;
    /* the waiter holds neither of these inner locks. Always try the WDF passive lock first; an
     * ISR or the canonical worker can then progress. */
    if (!rpmh_scheduler_try_enter(o))
        goto unlock_irq;
    req = o->rpmh_wait.active_req;
    if (!req || o->rpmh_wait.faulted || o->rpmh_wait.timed_out_req ||
        o->rpmh_wait.owner_epoch != epoch || o->rpmh_scheduler.owner_epoch != epoch ||
        o->rpmh_irq_pipeline.pending.active || o->rpmh_irq_pipeline.unmask_finalizing.active ||
        o->rpmh_irq_pipeline.work.state != RPMH_WORK_READY ||
        !rpmh_owner_src_enter(&o->rpmh_irq_src))
        goto unlock_scheduler;
    for (i = 0u; i < RPMH_JOBS; ++i) {
        const rpmh_job_t* job = &o->rpmh_scheduler.jobs[i];
        uint32_t slot = job->slot;
        if (job->id != req || job->owner_epoch != epoch || job->state != RPMH_JOB_EXECUTING ||
            slot >= 3u || !(o->rpmh_scheduler.busy_mask & (1u << slot)))
            continue;
        if (o->rpmh.deferred_req_id[slot] == req && o->rsc_completion.amc[slot].req_id == req &&
            rpmh_tcs_read_owned_irq_status(&o->rpmh_irq_src, epoch) == 1)
            captured = rpmh_pipeline_capture_passive(&o->rpmh_irq_pipeline, epoch);
        break;
    }
    rpmh_owner_src_leave(&o->rpmh_irq_src);
unlock_scheduler:
    rpmh_scheduler_leave(o);
unlock_irq:
    oem_wdf_leave_tcs_irq_lock_win(irq);
    if (captured) {
        /* the caller is already PASSIVE and owns the exact lifetime pin. Run the canonical
         * drain/ACK/finalization directly so a saturated system work queue cannot strand its own
         * synchronous waiter. */
        (void)rpmh_pipeline_dispatch_worker(&o->rpmh_irq_pipeline, epoch);
    }
}

/* timeout review remains read-only under the canonical scheduler mutex. */
static int rpmh_timeout_review_pinned(void* ctx, uint64_t epoch, uint32_t ticket)
{
    owners_t* o = (owners_t*)ctx;
    rpmh_tcs_result_t outcome = TCS_INVALID;
    int state;
    if (!o || !epoch || !ticket || KeGetCurrentIrql() != PASSIVE_LEVEL || !rpmh_scheduler_enter(o))
        return 0;
    state = rpmh_sync_deferred_timeout_inspect(&o->rpmh_wait, epoch, ticket, &outcome);
    rpmh_scheduler_leave(o);
    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                "rpmh: timeout ticket=%lu epoch=%llX job=%d result=%d work=%u pending=%u "
                "finalizing=%u busy=%02X owner=retained",
                (ULONG)ticket, (unsigned long long)epoch, state, (int)outcome,
                (unsigned)o->rpmh_irq_pipeline.work.state,
                (unsigned)o->rpmh_irq_pipeline.pending.active,
                (unsigned)o->rpmh_irq_pipeline.unmask_finalizing.active,
                (unsigned)o->rpmh_scheduler.busy_mask);
    return state >= 1 && state <= 3;
}

static int rpmh_queue_timeout_pinned(void* ctx, uint64_t epoch, uint32_t ticket)
{
    owners_t* o = (owners_t*)ctx;
    void* irq;
    int accepted;
    if (!o || !epoch || !ticket || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    irq = o->gic_irq37_owner.wdf_interrupt;
    if (!irq || !o->gic_irq37_owner.active || o->gic_irq37_owner.epoch != epoch ||
        o->rpmh_wait.owner_epoch != epoch || o->rpmh_wait.active_req != ticket ||
        o->rpmh_wait.timed_out_req != ticket)
        return 0;
    accepted = oem_wdf_queue_timeout_review_win(irq, epoch, ticket, rpmh_timeout_review_pinned, o);
    if (!accepted)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: timeout work rejected ticket=%lu epoch=%llX owner=retained",
                    (ULONG)ticket, (unsigned long long)epoch);
    return accepted;
}

static int rpmh_bind_completion_wait(owners_t* o)
{
    rpmh_sync_wait_io_t io = {0};
    if (!o || !o->rpmh_scheduler.arm_deferred || !o->rpmh_scheduler.owner_epoch)
        return 0;
    io.ctx = o;
    io.enter = rpmh_scheduler_enter;
    io.leave_cb = rpmh_scheduler_leave;
    io.reset = rpmh_wait_reset;
    io.signal = rpmh_wait_signal;
    io.wait_ms = rpmh_wait_deadline;
    io.queue_timeout_review = rpmh_queue_timeout_pinned;
    io.defer_signal_until_irq_restored = 1u;
    return rpmh_sync_deferred_bind(&o->rpmh_wait, &o->rpmh_scheduler, o->rpmh_scheduler.owner_epoch,
                                   &io);
}

/* a deferred submission returning BUSY is never an on-wire result. */

typedef struct amc_retry_ctx {
    rpmh_sync_deferred_t* wait;
    uint32_t attempts;
} amc_retry_ctx_t;

/* a physical BUSY return is proven pre-dispatch by the sync-deferred owner. Do not retry
 * an issued or ambiguous operation. The previous code retried three times without giving
 * the passive WDF IRQ lock time to be released. */
static rpmh_tcs_result_t rpmh_try_deferred_once(void* ctx, uint8_t client,
                                                const tcs_command_t* commands, uint32_t cnt,
                                                uint32_t* responses)
{
    amc_retry_ctx_t* retry = (amc_retry_ctx_t*)ctx;
    if (!retry || !retry->wait)
        return TCS_INVALID;
    if (retry->attempts++) {
        LARGE_INTEGER delay;
        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
            return TCS_NO_OWNERSHIP;
        delay.QuadPart = -20000LL; /* 2ms, bounded to two additional attempts */
        (void)KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    return rpmh_sync_deferred_execute(retry->wait, client, commands, cnt, responses, 2000u);
}

/* diagnostics run after completion or proven pre-dispatch cancellation. Hold the outer
 * owner mutex, then the transport lock; never modify RSC state here. */
static const char* rpmh_busy_at_issue(uint8_t reason)
{
    switch (reason) {
        case AMC_BUSY_TRANSPORT_LOCK:
            return "tcs-transport-lock";
        case AMC_BUSY_LEDGER_TICKET:
            return "ledger-ticket";
        case AMC_BUSY_INTERRUPT_LOCK:
            return "irq-lock-unclassified";
        case AMC_BUSY_IRQ_CTX:
            return "irq-context-or-irql";
        case AMC_BUSY_IRQ_FRAMEWORK:
            return "irq-framework-not-enabled";
        case AMC_BUSY_IRQ_OWNER:
            return "irq-owner-epoch-or-handle";
        case AMC_BUSY_IRQ_TRACKER:
            return "irq-dpc-tracker-not-active";
        case AMC_BUSY_IRQ_BRIDGE:
            return "irq-bridge-not-active";
        case AMC_BUSY_IRQ_TRY_CONTENDED:
            return "wdf-passive-try-lock-contended";
        case AMC_BUSY_IRQ_REVOKED:
            return "irq-owner-revoked-post-lock";
        case AMC_BUSY_HARDWARE_STATE:
            return "physical-tcs-or-irq";
        case AMC_BUSY_INHERITED_MASK:
            return "inherited-command-mask";
        default:
            return "no-issued-refusal-or-queued";
    }
}

static void rpmh_trace_pre_dispatch_busy(owners_t* o, uint64_t epoch)
{
    rpmh_tcs_io_t* io;
    uint32_t i, irq_en = 0u, irq_status = 0u;
    int locked;
    if (!trace_verbose_enabled(TRACE_RPMH))
        return;
    if (!o || !o->mmio.rsc.base || !o->rpmh_live || o->rpmh_scheduler.owner_epoch != epoch)
        return;
    io = &o->rpmh.io;
    if (!io->lock || !io->unlock || !io->read32)
        return;
    locked = io->lock(io->ctx);
    if (!locked) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: busy snapshot unavailable reason=owner-lock");
        return;
    }
    if (!io->read32(io->ctx, TCS_IRQ_ENABLE, &irq_en) ||
        !io->read32(io->ctx, TCS_IRQ_STATUS, &irq_status)) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: busy IRQ read failed epoch=%llX",
                    (unsigned long long)epoch);
        io->unlock(io->ctx);
        return;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                "rpmh: busy snapshot epoch=%llX grant=%02X active=%02X queued=%02X quarantine=%02X "
                "irq-enable=%08X irq-status=%08X",
                (unsigned long long)epoch, (unsigned)o->rpmh.external_grant_mask,
                (unsigned)o->rpmh_scheduler.execution_authorized,
                (unsigned)o->rpmh_scheduler.busy_mask, (unsigned)o->rpmh_scheduler.quarantined_mask,
                (unsigned)irq_en, (unsigned)irq_status);
    for (i = 0u; i < o->rpmh_scheduler.driver.active_cnt && i < 3u; ++i) {
        uint32_t status = 0u, control = 0u, enable = 0u, wait = 0u;
        uint32_t bit = 1u << i;
        const char* reason;
        if (io->read32(io->ctx, TCS_STATUS(i), &status) &&
            io->read32(io->ctx, TCS_CONTROL(i), &control) &&
            io->read32(io->ctx, TCS_ENABLE(i), &enable) &&
            io->read32(io->ctx, TCS_WAIT(i), &wait)) {
            /* snapshot-only classification. A concurrent ISR may change these values immediately;
             * this never authorizes a TCS write or ACK. */
            if (!(o->rpmh.external_grant_mask & bit) || !(o->rpmh.owned_amc_mask & bit) ||
                !(o->rsc_completion.granted_mask & bit))
                reason = "owner-grant-missing";
            else if (!(irq_en & bit))
                reason = "irq-disabled";
            else if ((o->rpmh.deferred_active_mask & bit) ||
                     o->rsc_completion.amc[i].state != RSC_AMC_VACANT)
                reason = "deferred-ticket-outstanding";
            else if (!(status & TCS_STATUS_IDLE))
                reason = "tcs-not-idle";
            else if (control & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER))
                reason = "tcs-control-busy";
            else if (irq_status & bit)
                reason = "irq-pending";
            else if ((enable || wait) && (!(o->rpmh.completed_amc_mask & bit) ||
                                          enable != o->rpmh.last_completed_command_mask[i] ||
                                          wait != o->rpmh.last_completed_dependency_mask[i]))
                reason = "inherited-command-unverified";
            else
                reason = "snapshot-eligible-check-lock-or-race";
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                        "rpmh: busy AMC=%lu issue=%s snapshot=%s status=%08X control=%08X "
                        "enable=%08X wait=%08X deferred=%02X completed=%02X ledger=%u",
                        (ULONG)i, rpmh_busy_at_issue(o->rpmh.last_arm_busy_reason[i]), reason,
                        (unsigned)status, (unsigned)control, (unsigned)enable, (unsigned)wait,
                        (unsigned)o->rpmh.deferred_active_mask,
                        (unsigned)o->rpmh.completed_amc_mask,
                        (unsigned)o->rsc_completion.amc[i].state);
        } else
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: busy AMC=%lu read failed",
                        (ULONG)i);
    }
    io->unlock(io->ctx);
}

static rpmh_tcs_result_t rpmh_submit_active(owners_t* o, uint8_t client,
                                            const tcs_command_t* commands, uint32_t cnt,
                                            uint32_t* responses)
{
    rpmh_tcs_result_t r;
    uint64_t epoch;
    if (!o || !commands || !cnt)
        return TCS_INVALID;
    epoch = o->rpmh_scheduler.owner_epoch;
    if (!rpmh_write_fence_live(&o->rpmh_write_fence, epoch))
        return TCS_NO_OWNERSHIP;
    if (o->rpmh_scheduler.arm_deferred) {
        uint32_t attempts = 0u, slot;
        amc_retry_ctx_t retry = {0};
        retry.wait = &o->rpmh_wait;
        for (slot = 0u; slot < 3u; ++slot)
            o->rpmh.last_arm_busy_reason[slot] = AMC_BUSY_NONE;
        if (o->rpmh_wait.scheduler != &o->rpmh_scheduler || o->rpmh_wait.owner_epoch != epoch)
            return TCS_NO_OWNERSHIP;
        /* rpmh_sync_deferred_execute: TCS BUSY is pre-dispatch (arm refused before any write, or
         * QUEUED ticket cancelled). */

        r = rpmh_try_owned_amcs(rpmh_try_deferred_once, &retry, o->rpmh_scheduler.driver.active_cnt,
                                client, commands, cnt, responses, &attempts);
        if (r == TCS_BUSY) {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                        "rpmh: submission busy client=%u attempts=%lu dispatched=0",
                        (unsigned)client, (ULONG)attempts);
            rpmh_trace_pre_dispatch_busy(o, epoch);
            return r;
        }
    } else {
        r = rpmh_scheduler_execute_sync(&o->rpmh_scheduler, client, commands, cnt, responses);
    }
    /* restoring an in-memory BCM/VRM vote is not hardware rollback. Keep this fence sticky
     * across Dx and read-only re-probes, including later successful RPMh READ attempts. */

    if (!rpmh_write_fence_record(&o->rpmh_write_fence, epoch, client, r) &&
        o->rpmh_write_fence.quarantined) {
        o->rpmh_live = FALSE;
        o->probe &= ~PROBE_RPMH;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_RPMH,
                    "rpmh: completion uncertain client=%u result=%u epoch=%llX votes=quarantined",
                    (unsigned)client, (unsigned)r, (unsigned long long)epoch);
    }
    return r;
}

/* every owner entry point takes this. A fast mutex needs APC_LEVEL or below; above it the
 * owner fails rather than blocking, which keeps the gate closed instead of deadlocking a
 * DPM callback. */

static int owners_lock(owners_t* o)
{
    /* do not elevate IRQL through this outer mutex. */

    return o && KeGetCurrentIrql() <= APC_LEVEL &&
           KeWaitForSingleObject(&o->lock, Executive, KernelMode, FALSE, NULL) == STATUS_SUCCESS;
}

static void owners_unlock(owners_t* o)
{
    (VOID) KeReleaseMutex(&o->lock, FALSE);
}

/* the PRE-lock rpmh_live/clock/TLMM checks are only a fast rejection. D0Quiesce must close
 * those gates under the SAME owner mutex that serializes a completed power vote. */

#define LIVE_RPMH 0x01u
#define LIVE_CLOCK 0x02u
#define LIVE_TLMM 0x04u

static int owners_lock_live(owners_t* o, unsigned required)
{
    if (!o || !required || !owners_lock(o))
        return 0;
    if (((required & LIVE_RPMH) && (!o->rpmh_live || o->rpmh_write_fence.quarantined)) ||
        ((required & LIVE_CLOCK) && !o->clocks_live) || ((required & LIVE_TLMM) && !o->tlmm_live)) {
        owners_unlock(o);
        return 0;
    }
    return 1;
}

static owners_t* owners_of(void* ctx)
{
    hwio_win_ctx_t* io = (hwio_win_ctx_t*)ctx;
    return io ? io->owners : NULL;
}

static int win_io_delay(void* ctx, uint32_t ms)
{
    UNREFERENCED_PARAMETER(ctx);
    return NT_SUCCESS(power_delay_ms(ms)) ? 1 : 0;
}

static int win_io_pmic_gpio_execution_ready(void* ctx)
{
    hwio_win_ctx_t* io = (hwio_win_ctx_t*)ctx;
    return io && InterlockedCompareExchange(&io->pmic_gpio_provider_live, 0, 0) == 1;
}

/* the PMIC provider can start after PrepareHardware. Require two matching IOCTL reads; cached
 * writes and callback addresses do not establish readiness. */

static BOOLEAN win_io_pmic_gpio_probe_provider(hwio_win_ctx_t* io)
{
    pm_gpio_state_query_t q;
    pm_gpio_state_raw_t first, second;
    BOOLEAN live;
    if (!io || !io->dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    q.pmic_number = 5u;
    q.gpio = 5u;
    q.write_configured_pins = 0u;
    RtlZeroMemory(&first, sizeof(first));
    RtlZeroMemory(&second, sizeof(second));
    live = (NT_SUCCESS(pmic_gpio_query_raw_state(io->dev, &q, &first)) &&
            NT_SUCCESS(pmic_gpio_query_raw_state(io->dev, &q, &second)) &&
            RtlCompareMemory(&first, &second, sizeof(first)) == sizeof(first));
    InterlockedExchange(&io->pmic_gpio_provider_live, live ? 1 : 0);
    return live;
}

static int win_io_pmic_gpio_cfg(void* ctx, const pm_gpio_digital_output_t* config)
{
    hwio_win_ctx_t* io = (hwio_win_ctx_t*)ctx;
    power_action_t action;
    NTSTATUS status;
    if (!io || !io->dev || !config || !win_io_pmic_gpio_execution_ready(ctx))
        return 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    RtlZeroMemory(&action, sizeof(action));
    action.kind = POWER_PMIC_GPIO;
    action.u.pmic_gpio.ioctl = PM_GPIO_CONFIG_DIGITAL_OUTPUT_IOCTL;
    action.u.pmic_gpio.fields[0] = config->pmic_number;
    action.u.pmic_gpio.fields[1] = config->gpio;
    action.u.pmic_gpio.fields[2] = config->out_buf_config;
    action.u.pmic_gpio.fields[3] = config->voltage_src;
    action.u.pmic_gpio.fields[4] = config->src;
    action.u.pmic_gpio.fields[5] = config->out_buf_strength;
    action.u.pmic_gpio.fields[6] = config->i_src_pulls;
    status = pmic_gpio_apply_action(io->dev, &action);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&io->pmic_gpio_provider_live, 0);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "pmic: GPIO %lu/%lu configure failed status=%08X", config->pmic_number,
                    config->gpio, status);
        return 0;
    }
    return 1;
}

static int win_io_pmic_gpio_query(void* ctx, const pm_gpio_state_query_t* query,
                                  pm_gpio_state_raw_t* raw)
{
    hwio_win_ctx_t* io = (hwio_win_ctx_t*)ctx;
    NTSTATUS status;
    if (!io || !io->dev || !query || !raw || !win_io_pmic_gpio_execution_ready(ctx))
        return 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    status = pmic_gpio_query_raw_state(io->dev, query, raw);
    if (!NT_SUCCESS(status))
        InterlockedExchange(&io->pmic_gpio_provider_live, 0);
    return NT_SUCCESS(status) ? 1 : 0;
}

/* tlmm owner. */

static int tlmm_read_pin(owners_t* o, uint32_t pin, tlmm_gpio_t* eff)
{
    uint32_t ctl, io;
    if (pin >= TLMM_PIN_CNT)
        return 0;
    if (!mmio_read32(&o->mmio.tlmm, TLMM_CTL(pin), (ULONG*)&ctl) ||
        !mmio_read32(&o->mmio.tlmm, TLMM_IO(pin), (ULONG*)&io))
        return 0;
    eff->pin = pin;
    eff->pull = ctl & 3u;
    eff->function = (ctl >> 2) & 0xFu;
    eff->drive = (ctl >> 6) & 7u;
    eff->direction = (ctl >> 9) & 1u;
    eff->state = (io >> 1) & 1u;
    return 1;
}

static int win_io_tlmm_cfg(void* ctx, const tlmm_gpio_t* config)
{
    owners_t* o = owners_of(ctx);
    tlmm_result_t r;
    if (!o || !config || !owners_lock_live(o, LIVE_TLMM))
        return 0;
    /* a previous uncertain write cannot be made safe by a new vote. */
    if (o->tlmm_uncertain) {
        owners_unlock(o);
        return 0;
    }
    r = tlmm_apply(&o->tlmm_io, config);
    if (r == TLMM_ROLLBACK_FAILED) {
        /* rollback was impossible or not verified after a hardware write. Quarantine the entire
         * TLMM controller, not only this graph path: a second ACPI client may otherwise
         * reconfigure the same pin. */

        o->tlmm_uncertain = TRUE;
        o->tlmm_live = FALSE;
        o->probe &= ~PROBE_TLMM;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_RESOURCE,
                    "tlmm: pin=%lu rollback uncertain writes=quarantined", config->pin);
    }
    owners_unlock(o);
    if (r != TLMM_OK)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "tlmm: pin=%lu configure failed result=%d",
                    config->pin, (int)r);
    return r == TLMM_OK;
}

static int win_io_tlmm_readback(void* ctx, uint32_t pin, tlmm_gpio_t* effective)
{
    owners_t* o = owners_of(ctx);
    int ok;
    if (!o || !effective || !owners_lock_live(o, LIVE_TLMM))
        return 0;
    if (o->tlmm_uncertain) {
        owners_unlock(o);
        return 0;
    }
    ok = tlmm_read_pin(o, pin, effective);
    owners_unlock(o);
    return ok;
}

/* report current physical ownership, not just the existence of callback entry points. This
 * is an admission hint; every actual operation still rechecks the live flag after
 * acquiring the same owner mutex. */

static int win_io_clock_execution_ready(void* ctx)
{
    owners_t* o = owners_of(ctx);
    int ready = 0;
    if (!o || !owners_lock(o))
        return 0;
    ready = o->clocks_live && o->mmio.gcc.base != NULL;
    owners_unlock(o);
    return ready;
}

static int win_io_tlmm_execution_ready(void* ctx)
{
    owners_t* o = owners_of(ctx);
    int ready = 0;
    if (!o || !owners_lock(o))
        return 0;
    ready = o->tlmm_live && !o->tlmm_uncertain && o->mmio.tlmm.base != NULL;
    owners_unlock(o);
    return ready;
}

/* clock owner. */

/* footswitch resources use NPA client-vote semantics, not the generic Windows CLOCK
 * delegation's raw GCC SW_COLLAPSE implementation. Decline this class across
 * known/apply/withdraw/HasVote/Readback (all share this idx function), until a separately
 * verified native NPA owner is bound. */

static LONG clock_idx(const char* name)
{
    const gcc_clock_t* c = gcc_find_req_clock(name);
    LONG i, idx;
    if (!c)
        return -1;
    idx = (LONG)(c - gcc_clocks);
    /* bsp aliases of one request bit share physical votes and uncertainty. Independent
     * name-indexed owners could otherwise disable each other. */
    for (i = 0; i < idx; ++i) {
        const gcc_clock_t* p = &gcc_clocks[i];
        if (p->kind == c->kind && p->cbcr_offset == c->cbcr_offset &&
            p->vote_offset == c->vote_offset && p->vote_mask == c->vote_mask &&
            (p->domain_idx == c->domain_idx || c->kind == GCC_KIND_REQ))
            return i;
    }
    return idx;
}

static clock_client_t* clock_client(owners_t* o, const char* path, ULONG ci, int create)
{
    ULONG i;
    clock_client_t* free_slot = NULL;
    for (i = 0; i < CLOCK_CLIENT_MAX; ++i) {
        clock_client_t* c = &o->clock_clients[i];
        if (!c->in_use) {
            if (!free_slot)
                free_slot = c;
            continue;
        }
        if (c->clock_idx == ci && strncmp(c->path, path, sizeof(c->path)) == 0)
            return c;
    }
    if (!create || !free_slot)
        return NULL;
    RtlZeroMemory(free_slot, sizeof(*free_slot));
    free_slot->in_use = 1u;
    free_slot->clock_idx = ci;
    (VOID) RtlStringCbCopyA(free_slot->path, sizeof(free_slot->path), path);
    return free_slot;
}

/* exactly the request aggregate held by this PEP's clients, not firmware's or another
 * driver's unknown clock reference count. The caller holds lock. */

static void clock_aggregate(owners_t* o, ULONG ci, int* enabled, ULONG* max_hz)
{
    ULONG i;
    *enabled = 0;
    *max_hz = 0;
    for (i = 0; i < CLOCK_CLIENT_MAX; ++i) {
        const clock_client_t* c = &o->clock_clients[i];
        int active;
        const char* lane;
        if (!c->in_use || c->clock_idx != ci)
            continue;
        active = c->enable != 0u;
        /* a desc component's rate sets are independent clients, but contribute only while that
         * component's F res client is on. Retaining a set's selected rate must not keep an idle
         * clock on. */
        lane = strrchr(c->path, '#');
        if (!active && c->hz && lane && lane[1] == 'S') {
            ULONG j;
            size_t prefix = (size_t)(lane - c->path);
            for (j = 0u; j < CLOCK_CLIENT_MAX; ++j) {
                const clock_client_t* parent = &o->clock_clients[j];
                if (parent->in_use && parent->clock_idx == ci && parent->enable &&
                    !strncmp(parent->path, c->path, prefix) &&
                    !strcmp(parent->path + prefix, "#F")) {
                    active = 1;
                    break;
                }
            }
        }
        if (!active)
            continue;
        *enabled = 1;
        if (c->hz > *max_hz)
            *max_hz = c->hz;
    }
}

/* when the old and new aggregates are identical and active, re-attest the retained
 * physical vote without writing the same CBCR. failed re-attestation rejects the new
 * client, and never claims to have written or rolled back hardware. */

static int clock_apply_aggregate(owners_t* o, ULONG ci, int previously_enabled, ULONG previous_hz)
{
    const gcc_clock_t* clk = &gcc_clocks[ci];
    ULONG max_hz = 0;
    int any_enabled = 0;
    gcc_commit_result_t result;
    gcc_commit_diag_t diag;
    if (o->clock_uncertain[ci])
        return 0;
    clock_aggregate(o, ci, &any_enabled, &max_hz);
    if (previously_enabled && any_enabled && previous_hz == max_hz) {
        /* previous state is a committed PEP client, not an assumed firmware vote. verify the held
         * clock in hardware twice. */

        result = gcc_verify_retained_vote(&o->gcc_io, clk, o->clock_programmed_hz[ci]);
        if (result != GCC_COMMIT_OK)
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK,
                        "clock: %s retained vote unverified new-client=rejected", clk->name);
        return result == GCC_COMMIT_OK;
    }
    result = gcc_commit_branch_vote_ex(&o->gcc_io, clk, max_hz, any_enabled,
                                       &o->clock_programmed_hz[ci], &diag);
    if (result != GCC_COMMIT_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK,
                    "clock: %s result=%d phase=%lu wrote=%lu polls=%lu target=%luHz enable=%d "
                    "pre=[%08lX/%08lX %u/%u] post=[%08lX/%08lX %u/%u]",
                    clk->name, (int)result, (ULONG)diag.stage, (ULONG)diag.write_started,
                    (ULONG)diag.post_poll_cnt, (ULONG)max_hz, any_enabled, (ULONG)diag.before1.cbcr,
                    (ULONG)diag.before2.cbcr, (unsigned)diag.before1.enabled,
                    (unsigned)diag.before2.enabled, (ULONG)diag.after1.cbcr,
                    (ULONG)diag.after2.cbcr, (unsigned)diag.after1.enabled,
                    (unsigned)diag.after2.enabled);
    }
    if (result == GCC_COMMIT_UNCERTAIN) {
        o->clock_uncertain[ci] = 1u;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_CLOCK, "clock: %s state uncertain votes=quarantined",
                    clk->name);
    } else if (result == GCC_COMMIT_DECLINED) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK, "clock: %s commit rejected before write",
                    clk->name);
    }
    return result == GCC_COMMIT_OK;
}

static int clock_apply_freq(owners_t* o, ULONG ci, ULONG hz)
{
    const gcc_clock_t* clk = &gcc_clocks[ci];
    gcc_commit_result_t result;
    gcc_commit_diag_t diag;
    ULONG active_hz;
    int enabled;
    clock_aggregate(o, ci, &enabled, &active_hz);
    if (!hz || o->clock_uncertain[ci])
        return 0;
    /* commit the active aggregate; a lower caller request is still valid while a different
     * client holds a higher minimum. */
    if (active_hz > hz)
        hz = active_hz;
    result = gcc_commit_freq_ex(&o->gcc_io, clk, hz, &o->clock_programmed_hz[ci], &diag);
    if (result == GCC_COMMIT_UNCERTAIN)
        o->clock_uncertain[ci] = 1u;
    if (result != GCC_COMMIT_OK)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK,
                    "clock: %s rate=%lu result=%d phase=%lu wrote=%lu", clk->name, hz, (int)result,
                    (ULONG)diag.stage, (ULONG)diag.write_started);
    return result == GCC_COMMIT_OK;
}

static int win_io_clock_known(void* ctx, const char* clock_name)
{
    owners_t* o = owners_of(ctx);
    LONG ci;
    int known;
    if (!o || !clock_name)
        return 0;
    ci = clock_idx(clock_name);
    if (ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    known = !o->clock_uncertain[ci];
    owners_unlock(o);
    return known;
}

static int win_io_clock_config_readback(void* ctx, const char* clock_name, uint32_t* force_on)
{
    owners_t* o = owners_of(ctx);
    LONG ci = clock_idx(clock_name);
    int ok;
    if (!o || !force_on || ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX ||
        !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    if (o->clock_uncertain[ci]) {
        owners_unlock(o);
        return 0;
    }
    ok = gcc_ufs_ice_mem_core_get(&o->gcc_io, clock_name, force_on);
    owners_unlock(o);
    return ok;
}

static int win_io_clock_config_write(void* ctx, const char* clock_name, uint32_t force_on)
{
    owners_t* o = owners_of(ctx);
    LONG ci = clock_idx(clock_name);
    gcc_config_result_t r;
    if (!o || ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    if (o->clock_uncertain[ci]) {
        owners_unlock(o);
        return 0;
    }
    r = gcc_ufs_ice_mem_core_set(&o->gcc_io, clock_name, force_on);
    if (r == GCC_CONFIG_UNCERTAIN) {
        o->clock_uncertain[ci] = 1u;
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_CLOCK,
                    "clock: %s configure uncertain votes=quarantined", clock_name);
    }
    owners_unlock(o);
    return r == GCC_CONFIG_OK;
}

static int win_io_clock_apply(void* ctx, const char* path, const power_action_t* action)
{
    owners_t* o = owners_of(ctx);
    clock_client_t* c;
    clock_client_t before;
    LONG ci;
    int ok, existed, previously_enabled;
    ULONG previous_hz;
    if (!o || !path || !action || action->kind != POWER_CLOCK)
        return 0;
    ci = clock_idx(action->u.clock.name);
    if (ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    if (o->clock_uncertain[ci]) {
        owners_unlock(o);
        return 0;
    }
    clock_aggregate(o, (ULONG)ci, &previously_enabled, &previous_hz);
    existed = clock_client(o, path, (ULONG)ci, 0) != NULL;
    c = clock_client(o, path, (ULONG)ci, 1);
    if (!c) {
        owners_unlock(o);
        return 0;
    }
    before = *c;
    switch (action->u.clock.vote) {
        case 1u:
            c->enable = 1u;
            break;
        case 2u:
            c->enable = 0u;
            c->hz = 0u;
            break;
        case 3u:
            c->hz = action->u.clock.extra[0];
            break;
        case 8u:
            c->enable = 1u;
            c->hz = action->u.clock.extra[0];
            break;
        default:
            if (!existed)
                RtlZeroMemory(c, sizeof(*c));
            owners_unlock(o);
            return 0;
    }
    ok = action->u.clock.vote == 3u
             ? clock_apply_freq(o, (ULONG)ci, action->u.clock.extra[0])
             : clock_apply_aggregate(o, (ULONG)ci, previously_enabled, previous_hz);
    if (!ok) {
        /* the uncertainty flag stops subsequent physical mutations. */

        if (existed)
            *c = before;
        else
            RtlZeroMemory(c, sizeof(*c));
    }
    owners_unlock(o);
    return ok;
}

static int win_io_clock_withdraw(void* ctx, const char* path, const char* clock_name)
{
    owners_t* o = owners_of(ctx);
    clock_client_t* c;
    clock_client_t before;
    LONG ci;
    int ok = 1, previously_enabled;
    ULONG previous_hz;
    if (!o || !path)
        return 0;
    ci = clock_idx(clock_name);
    if (ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    if (o->clock_uncertain[ci]) {
        owners_unlock(o);
        return 0;
    }
    clock_aggregate(o, (ULONG)ci, &previously_enabled, &previous_hz);
    c = clock_client(o, path, (ULONG)ci, 0);
    if (c) {
        before = *c;
        RtlZeroMemory(c, sizeof(*c));
        ok = clock_apply_aggregate(o, (ULONG)ci, previously_enabled, previous_hz);
        if (!ok)
            *c = before;
    }
    owners_unlock(o);
    return ok;
}

static int win_io_clock_has_vote(void* ctx, const char* path, const char* clock_name)
{
    owners_t* o = owners_of(ctx);
    clock_client_t* c;
    LONG ci;
    int has = 0;
    if (!o || !path)
        return 0;
    ci = clock_idx(clock_name);
    if (ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || !owners_lock(o))
        return 0;
    if (o->clock_uncertain[ci]) {
        owners_unlock(o);
        return 0;
    }
    c = clock_client(o, path, (ULONG)ci, 0);
    has = c && (c->enable || c->hz);
    owners_unlock(o);
    return has;
}

static int win_io_clock_readback(void* ctx, const char* clock_name, uint64_t* effective)
{
    owners_t* o = owners_of(ctx);
    const gcc_clock_t* clk = gcc_find_clock(clock_name);
    gcc_state_t st;
    int ok;
    if (!o || !clk || !effective || !owners_lock_live(o, LIVE_CLOCK))
        return 0;
    {
        LONG ci = clock_idx(clock_name);
        if (ci < 0 || (ULONG)ci >= CLOCK_TABLE_MAX || o->clock_uncertain[ci]) {
            owners_unlock(o);
            return 0;
        }
    }
    ok = gcc_readback(&o->gcc_io, clk, &st) == GCC_OK;
    owners_unlock(o);
    if (!ok)
        return 0;
    *effective = gcc_pack_state(&st);
    return 1;
}

/* bus owner. */

int hw_backend_win_clock_read(uint64_t serial, const char* name, uint64_t* out)
{
    hw_backend_t lease;
    win_backend_instance_t* instance;
    int ok = 0;
    if (!serial || !name || !out || !hw_backend_begin_bound(serial, &lease))
        return 0;
    instance = g_win_backend;
    if (instance)
        ok = win_io_clock_readback(&instance->io, name, out);
    hw_backend_end_bound();
    return ok;
}

int hw_backend_win_clock_dfs_catalog(uint64_t serial, const char* name, gcc_dfs_catalog_t* out)
{
    hw_backend_t lease;
    win_backend_instance_t* instance;
    const gcc_clock_t* clock = gcc_find_req_clock(name);
    owners_t* owner;
    int ok = 0;
    if (!serial || !clock || !out || !hw_backend_begin_bound(serial, &lease))
        return 0;
    instance = g_win_backend;
    owner = instance ? instance->io.owners : NULL;
    if (owner && owners_lock_live(owner, LIVE_CLOCK)) {
        ok = gcc_read_dfs_catalog(&owner->gcc_io, clock, out);
        owners_unlock(owner);
    }
    hw_backend_end_bound();
    return ok;
}

static int bus_commit_route(owners_t* o, const char* master, const char* slave)
{
    icb_command_t cmds[ICB_MAX_COMMANDS];
    tcs_command_t tcs[TCS_BATCH_MAX];
    uint32_t n = 0, i;
    rpmh_tcs_result_t r;
    if (!icb_route_commands(&o->bus, master, slave, cmds, ICB_MAX_COMMANDS, &n) || n == 0u ||
        n > TCS_BATCH_MAX)
        return 0;
    for (i = 0; i < n; ++i) {
        tcs[i].address = cmds[i].address;
        tcs[i].data = cmds[i].data;
        tcs[i].read = 0u;
    }
    r = rpmh_submit_active(o, RPMH_CLIENT_BUS, tcs, n, NULL);
    if (r != TCS_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "icb: %s->%s commands=%lu commit failed result=%d", master, slave, n, (int)r);
        return 0;
    }
    return 1;
}

/* never mistake their presence for an authorized controller. The owner must be physically
 * live, have a bound deferred scheduler, and possess a single active IRQ completion
 * consumer. */

/* pofx supports/capability queries also race d0 probe and exit. Never read a mutable RPMh
 * provider while the owner lock is held by a concurrent hardware probe. */

static int win_io_rpmh_execution_ready(void* ctx)
{
    owners_t* o = owners_of(ctx);
    int ready;
    if (!o || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    ready = rpmh_deferred_runtime_ready(&o->deferred_scheduler, &o->rpmh_wait);
    owners_unlock(o);
    return ready;
}

static int win_io_bus_route_known(void* ctx, const char* master, const char* slave)
{
    owners_t* o = owners_of(ctx);
    int supported;
    if (!o || !master || !slave || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    supported = icb_route_supported(&o->bus, master, slave);
    owners_unlock(o);
    return supported;
}

static int win_io_bus_vote(void* ctx, const char* path, const power_action_t* action)
{
    owners_t* o = owners_of(ctx);
    uint64_t old_ab = 0, old_ib = 0;
    int had, ok;
    if (!o || !path || !action || action->kind != POWER_ICB || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    had = icb_get_client_vote(&o->bus, path, action->u.icb.master, action->u.icb.slave, &old_ab,
                              &old_ib);
    ok = icb_set_client_vote(&o->bus, path, action->u.icb.master, action->u.icb.slave,
                             action->u.icb.average, action->u.icb.peak) &&
         bus_commit_route(o, action->u.icb.master, action->u.icb.slave);
    if (!ok)
        (void)icb_set_client_vote(&o->bus, path, action->u.icb.master, action->u.icb.slave,
                                  had ? old_ab : 0u, had ? old_ib : 0u);
    owners_unlock(o);
    return ok;
}

static int win_io_bus_withdraw(void* ctx, const char* path, const char* master, const char* slave)
{
    owners_t* o = owners_of(ctx);
    uint64_t old_ab = 0, old_ib = 0;
    int had, ok = 1;
    if (!o || !path || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    had = icb_get_client_vote(&o->bus, path, master, slave, &old_ab, &old_ib);
    if (had) {
        ok = icb_set_client_vote(&o->bus, path, master, slave, 0u, 0u) &&
             bus_commit_route(o, master, slave);
        if (!ok)
            (void)icb_set_client_vote(&o->bus, path, master, slave, old_ab, old_ib);
    }
    owners_unlock(o);
    return ok;
}

static int win_io_bus_has_vote(void* ctx, const char* path, const char* master, const char* slave)
{
    owners_t* o = owners_of(ctx);
    uint64_t ab = 0, ib = 0;
    int has;
    if (!o || !path || !owners_lock(o))
        return 0;
    /* the NPA destroy caller interprets !HasVote as proof it can release this client. After an
     * ambiguous TCS issue no such proof exists: a supposedly withdrawn vote may still be live
     * in AOP. */

    if (o->rpmh_write_fence.quarantined) {
        owners_unlock(o);
        return 1;
    }
    has = icb_get_client_vote(&o->bus, path, master, slave, &ab, &ib) && (ab || ib);
    owners_unlock(o);
    return has;
}

/* oem RpmhLib reads are individual AMCs, never multi-READ command sets. The existing live
 * transport owns physical completion and IRQ37 rundown. */
static rpmh_tcs_result_t rpmh_submit_single_read(void* ctx, uint8_t client,
                                                 const tcs_command_t* command, uint32_t cnt,
                                                 uint32_t* response)
{
    if (cnt != 1u || !command || !command->read || !response)
        return TCS_INVALID;
    return rpmh_submit_active((owners_t*)ctx, client, command, 1u, response);
}

/* independent readback: the AOP's current value of the route's BCMs. */
static int win_io_bus_readback(void* ctx, const char* master, const char* slave,
                               uint64_t* effective)
{
    owners_t* o = owners_of(ctx);
    icb_command_t cmds[ICB_MAX_COMMANDS];
    tcs_command_t tcs[TCS_BATCH_MAX];
    uint32_t resp[TCS_BATCH_MAX];
    uint32_t n = 0, i;
    uint64_t v = 0;
    rpmh_tcs_result_t r;
    if (!o || !effective || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    if (!icb_route_commands(&o->bus, master, slave, cmds, ICB_MAX_COMMANDS, &n) || n == 0u ||
        n > TCS_BATCH_MAX) {
        owners_unlock(o);
        return 0;
    }
    for (i = 0; i < n; ++i) {
        tcs[i].address = cmds[i].address;
        tcs[i].data = 0u;
        tcs[i].read = 1u;
    }
    /* boot.MXF rpmh_cmdq_send_read: a single READ per completed AMC. */
    r = rpmh_read_sequence(o, rpmh_submit_single_read, RPMH_CLIENT_READ, tcs, n, resp, NULL);
    owners_unlock(o);
    if (r != TCS_OK)
        return 0;
    for (i = 0; i < n && i < 4u; ++i)
        v ^= (uint64_t)resp[i] << (16u * i);
    *effective = v | ((uint64_t)n << 60);
    return 1;
}

static const char* win_io_bus_probe_route(void* ctx, uint32_t key, const char** slave)
{
    UNREFERENCED_PARAMETER(ctx);
    if (key >= platform_route_cnt || !slave)
        return NULL;
    *slave = platform_routes[key].slave;
    return platform_routes[key].master;
}

/* rail owner. */

static int rail_commit(owners_t* o, int idx)
{
    rail_vote_t agg;
    tcs_command_t cmds[3];
    uint32_t n = 0;
    rail_t* rail = &o->rails.rails[idx];
    rpmh_tcs_result_t r;
    if (!rail_aggregate(&o->rails, (uint32_t)idx, &agg))
        return 0;
    if (rail->issued && RtlCompareMemory(&agg, &rail->last_issued, sizeof(agg)) == sizeof(agg))
        return 1;
    if (!rail_program(&o->rails, (uint32_t)idx, &agg, cmds, &n) || n == 0u)
        return 0;
    r = rpmh_submit_active(o, RPMH_CLIENT_RAIL, cmds, n, NULL);
    if (r != TCS_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "pmic: rail=%s resource=%s program failed result=%d", rail->ppp,
                    rail->cmd_db_name, (int)r);
        return 0;
    }
    rail->issued = 1u;
    rail->last_issued = agg;
    return 1;
}

static int win_io_rail_known(void* ctx, const char* ppp_name)
{
    owners_t* o = owners_of(ctx);
    int idx, known;
    if (!o || !ppp_name || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    idx = rail_ensure(&o->rails, ppp_name);
    known = idx >= 0 && o->rails.rails[idx].resolved;
    owners_unlock(o);
    return known;
}

static int win_io_rail_vote(void* ctx, const char* path, const char* ppp_name,
                            const uint32_t fields[5])
{
    owners_t* o = owners_of(ctx);
    rail_vote_t vote, old;
    int idx, had, ok;
    if (!o || !path || !ppp_name || !fields || !rail_decode_fields(fields, &vote))
        return 0;
    if (!owners_lock_live(o, LIVE_RPMH))
        return 0;
    idx = rail_ensure(&o->rails, ppp_name);
    if (idx < 0 || !o->rails.rails[idx].resolved) {
        owners_unlock(o);
        return 0;
    }
    had = rail_get_client_vote(&o->rails, path, ppp_name, &old);
    ok = rail_set_client_vote(&o->rails, path, ppp_name, &vote) && rail_commit(o, idx);
    if (!ok)
        (void)rail_set_client_vote(&o->rails, path, ppp_name, had ? &old : NULL);
    owners_unlock(o);
    return ok;
}

static int win_io_rail_withdraw(void* ctx, const char* path, const char* ppp_name)
{
    owners_t* o = owners_of(ctx);
    rail_vote_t old;
    int idx, had, ok = 1;
    if (!o || !path || !ppp_name || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    idx = rail_find(&o->rails, ppp_name);
    had = idx >= 0 && rail_get_client_vote(&o->rails, path, ppp_name, &old);
    if (had) {
        ok = rail_set_client_vote(&o->rails, path, ppp_name, NULL) && rail_commit(o, idx);
        if (!ok)
            (void)rail_set_client_vote(&o->rails, path, ppp_name, &old);
    }
    owners_unlock(o);
    return ok;
}

static int win_io_rail_has_vote(void* ctx, const char* path, const char* ppp_name)
{
    owners_t* o = owners_of(ctx);
    rail_vote_t v;
    int has;
    if (!o || !path || !ppp_name || !owners_lock(o))
        return 0;
    /* sticky hardware uncertainty is NOT a verified no-vote state. */
    if (o->rpmh_write_fence.quarantined) {
        owners_unlock(o);
        return 1;
    }
    has = rail_get_client_vote(&o->rails, path, ppp_name, &v) && v.enable;
    owners_unlock(o);
    return has;
}

static int rail_read(owners_t* o, int idx, uint64_t* out)
{
    tcs_command_t cmds[3];
    rpmh_tcs_result_t submission;
    uint32_t resp[3] = {0, 0, 0};
    uint32_t n = 0;
    if (!rail_read_program(&o->rails, (uint32_t)idx, cmds, &n) || (n != 1u && n != 3u)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "pmic: read program unavailable rail=%d commands=%lu", idx, (ULONG)n);
        return 0;
    }
    /* a grouped VRM READ is not the OEM protocol: each register has its own request ID, idle
     * AMC, zero dependency mask and verified completion. If any step is ambiguous the live
     * transport still quarantines it; never report a partly observed PMIC rail state. */
    {
        uint32_t completed = 0u;
        submission = rpmh_read_sequence(o, rpmh_submit_single_read, RPMH_CLIENT_PROBE, cmds, n,
                                        resp, &completed);
        if (submission != TCS_OK)
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: rail read failed rail=%d completed=%lu total=%lu result=%d", idx,
                        (ULONG)completed, (ULONG)n, (int)submission);
    }
    if (submission != TCS_OK) {
        /* preserve the transport result. A timeout cannot establish rail state or authorize replay
         * of an uncertain AMC. */

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: rail read rejected rail=%d commands=%lu result=%d", idx, (ULONG)n,
                    (int)submission);
        return 0;
    }
    /* clock buffers have only an enable vote, at base+4. Reading their nonexistent
     * voltage/mode registers can leave an AMC uncompleted. */
    *out = n == 1u ? rail_pack_state(0u, resp[0], 0u) : rail_pack_state(resp[0], resp[1], resp[2]);
    return 1;
}

static int win_io_rail_readback(void* ctx, const char* ppp_name, uint64_t* effective)
{
    owners_t* o = owners_of(ctx);
    int idx, ok;
    if (!o || !ppp_name || !effective || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    idx = rail_ensure(&o->rails, ppp_name);
    ok = idx >= 0 && o->rails.rails[idx].resolved && rail_read(o, idx, effective);
    owners_unlock(o);
    return ok;
}

/* one OS owner epoch for RPMh VRM and GCC. No firmware vote is adopted. */
static int native_ufs_owns(void* ctx, const char* domain, uint64_t epoch)
{
    owners_t* o = (owners_t*)ctx;
    return o && domain && !strcmp(domain, clock_ufs_domain) && epoch &&
           o->os_rsc_lease.owner_epoch == epoch &&
           InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) == 1 && o->rpmh_live &&
           o->clocks_live && !o->rpmh_write_fence.quarantined && !o->ufs_owner_uncertain &&
           o->mmio.gcc.base && o->mmio.gcc.physical == GCC_WINDOW_PHYS;
}

static int native_ufs_epoch(void* ctx, uint64_t* epoch)
{
    owners_t* o = (owners_t*)ctx;
    if (!o || !epoch || !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch))
        return 0;
    *epoch = o->os_rsc_lease.owner_epoch;
    return 1;
}

static int native_arc_owns(void* ctx, uint64_t epoch)
{
    return native_ufs_owns(ctx, clock_ufs_domain, epoch);
}

static int native_arc_read(void* ctx, uint32_t address, uint32_t* response)
{
    owners_t* o = ctx;
    tcs_command_t command = {0};
    rpmh_tcs_result_t result;
    if (!o || !response || (address >> 16) != 3u || (address & 3u) ||
        !native_arc_owns(o, o->os_rsc_lease.owner_epoch))
        return 0;
    command.address = address;
    command.read = 1u;
    result = rpmh_submit_active(o, RPMH_CLIENT_PROBE, &command, 1u, response);
    o->ufs_arc_last_address = address;
    o->ufs_arc_last_response = *response;
    o->ufs_arc_last_result = result;
    return result == TCS_OK;
}

static int native_arc_write(void* ctx, uint32_t address, uint32_t corner)
{
    owners_t* o = ctx;
    tcs_command_t command = {0};
    rpmh_tcs_result_t result;
    if (!o || (address >> 16) != 3u || (address & 3u) ||
        !native_arc_owns(o, o->os_rsc_lease.owner_epoch))
        return 0;
    command.address = address;
    command.data = corner;
    result = rpmh_submit_active(o, RPMH_CLIENT_RAIL, &command, 1u, NULL);
    return result == TCS_OK ? 1 : (result == TCS_BUSY ? 0 : -1);
}

static arc_rail_t* native_ufs_arc(owners_t* o, const char* name)
{
    if (!o || !name)
        return NULL;
    if (!strcmp(name, UFS_CX_NAME))
        return &o->ufs_arc[0];
    if (!strcmp(name, UFS_MX_NAME))
        return &o->ufs_arc[1];
    return NULL;
}

static int native_ufs_resolve(void* ctx, const char* name, uint32_t floor)
{
    owners_t* o = ctx;
    arc_rail_t* rail = native_ufs_arc(o, name);
    arc_io_t io = {0};
    arc_bind_diag_t diag = {0};
    uint32_t corner;
    int ok = 0;
    if (!rail || !floor || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    io.ctx = o;
    io.owns = native_arc_owns;
    io.read = native_arc_read;
    io.write = native_arc_write;
    if (!rail->bound && !arc_bind_ex(rail, o->cmd_db, o->cmd_db_bytes, name, &io,
                                     o->os_rsc_lease.owner_epoch, &diag)) {
        TraceEvents(
            TRACE_LEVEL_WARNING, TRACE_POWER,
            "ufs: ARC=%s phase=%lu address=%05lX aux=%lu index=%lu count=%lu raw=%08lX/%08lX", name,
            diag.phase, diag.address, diag.aux_length, diag.level_idx, diag.level_cnt, diag.raw[0],
            diag.raw[1]);
        goto done;
    }
    if (o->ufs_arc[0].bound && o->ufs_arc[1].bound &&
        o->ufs_arc[0].address == o->ufs_arc[1].address)
        goto done;
    ok =
        rail->epoch == o->os_rsc_lease.owner_epoch && arc_map(rail, floor, &corner) && corner != 0u;
done:
    owners_unlock(o);
    return ok;
}

static int native_ufs_client(void* ctx, const char* client, const char* name, uint32_t* enabled,
                             uint32_t* level)
{
    owners_t* o = ctx;
    arc_rail_t* rail = native_ufs_arc(o, name);
    int ok;
    if (!rail || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    ok = arc_client_vote(rail, client, enabled, level);
    owners_unlock(o);
    return ok;
}

static int native_ufs_physical(void* ctx, const char* name, uint32_t* enabled, uint32_t* level)
{
    owners_t* o = ctx;
    arc_rail_t* rail = native_ufs_arc(o, name);
    int ok;
    if (!rail || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    ok = arc_read(rail, enabled, level);
    owners_unlock(o);
    return ok;
}

static int native_ufs_vote(void* ctx, const char* client, const char* name, uint32_t level,
                           uint32_t enabled)
{
    owners_t* o = ctx;
    arc_rail_t* rail = native_ufs_arc(o, name);
    int ok;
    if (!rail || enabled != 1u || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    ok = arc_vote(rail, client, level, enabled);
    if (rail->quarantined)
        o->ufs_owner_uncertain = TRUE;
    owners_unlock(o);
    return ok;
}

static int native_ufs_withdraw(void* ctx, const char* client, const char* name)
{
    owners_t* o = ctx;
    arc_rail_t* rail = native_ufs_arc(o, name);
    int ok;
    if (!rail || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    ok = arc_vote(rail, client, 0u, 0u);
    if (rail->quarantined)
        o->ufs_owner_uncertain = TRUE;
    owners_unlock(o);
    return ok;
}

static int native_ufs_gdsc_physical(void* ctx, const char* domain, uint64_t epoch, uint32_t* state)
{
    owners_t* o = (owners_t*)ctx;
    uint32_t a = 0, b = 0;
    if (!state || !native_ufs_owns(o, domain, epoch) || !gcc_read(o, 0x77004u, &a) ||
        !gcc_read(o, 0x77004u, &b) || a != b || !native_ufs_owns(o, domain, epoch))
        return 0;
    if (a & GDSCR_SW_COLLAPSE)
        *state = 2u;
    else if (a & GDSCR_PWR_ON)
        *state = 1u;
    else
        return 0;
    return 1;
}

/* both scalar P-state programs are executed through one graph lock and a checked backend
 * rundown lease; never borrow a device-path D-state vote. */
struct ufs_group_bridge {
    owners_t* owners;
    res_graph_t* graph;
    hw_backend_t leased;
    uint64_t owner_epoch;
    uint8_t lease;
};

static int native_group_bound(owners_t* o, uint64_t serial)
{
    ufs_group_bridge_t* b = o ? o->ufs_group_bridge : NULL;
    return b && b->owners == o && b->graph && serial &&
           b->graph->backend_binding_serial == serial && serial == hw_backend_verified_serial() &&
           native_ufs_owns(o, clock_ufs_domain, b->owner_epoch);
}

static int group_win_epoch(void* c, uint64_t* epoch)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return b && b->graph && native_ufs_epoch(b->owners, epoch) && *epoch == b->owner_epoch &&
           native_group_bound(b->owners, b->graph->backend_binding_serial);
}

static int group_win_lock(void* c)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    uint64_t epoch = 0u;
    if (!b || b->lease || !b->graph || !b->graph->acquire || !b->graph->release ||
        !group_win_epoch(c, &epoch) || !b->graph->acquire(b->graph->lock_ctx))
        return 0;
    if (!hw_backend_begin_bound(b->graph->backend_binding_serial, &b->leased)) {
        b->graph->release(b->graph->lock_ctx);
        return 0;
    }
    b->lease = 1u;
    return 1;
}

static void group_win_unlock(void* c)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    if (!b || !b->lease)
        return;
    b->lease = 0u;
    hw_backend_end_bound();
    RtlZeroMemory(&b->leased, sizeof(b->leased));
    b->graph->release(b->graph->lock_ctx);
}

static int group_win_supports(void* c, const char* id, uint32_t state, const power_action_t* a)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return b && b->lease && b->leased.supports && b->leased.supports(b->leased.ctx, id, state, a);
}

static int group_win_snapshot(void* c, const char* id, uint32_t state, const power_action_t* a,
                              power_snapshot_t* snap)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return group_win_supports(c, id, state, a) && b->leased.snapshot &&
           b->leased.snapshot(b->leased.ctx, id, state, a, snap);
}

static int group_win_apply(void* c, const char* id, uint32_t state, const power_action_t* a)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return group_win_supports(c, id, state, a) && b->leased.apply &&
           b->leased.apply(b->leased.ctx, id, state, a);
}

static int group_win_verify(void* c, const char* id, uint32_t state, const power_action_t* a)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return group_win_supports(c, id, state, a) && b->leased.verify &&
           b->leased.verify(b->leased.ctx, id, state, a);
}

static int group_win_restore(void* c, const char* id, uint32_t state, const power_action_t* a,
                             const power_snapshot_t* snap)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return b && b->lease && b->leased.restore &&
           b->leased.restore(b->leased.ctx, id, state, a, snap);
}

static int group_win_restored(void* c, const char* id, uint32_t state, const power_action_t* a,
                              const power_snapshot_t* snap)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return b && b->lease && b->leased.verify_restored &&
           b->leased.verify_restored(b->leased.ctx, id, state, a, snap);
}

static int group_win_final(void* c, const char* id, uint32_t state,
                           const power_sequence_t* sequence)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    return b && b->lease && b->leased.verify_final &&
           b->leased.verify_final(b->leased.ctx, id, state, sequence);
}

/* revoke the src-owned required client only after independent readback and explicit
 * transport confirmation of no outstanding clock/VRM/BCM vote. */
static int group_win_config(void* c, const char* name, uint32_t* value)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    hwio_win_ctx_t io = {0};
    if (!b || !b->lease || !name || !value)
        return 0;
    io.owners = b->owners;
    return win_io_clock_config_readback(&io, name, value);
}

static int group_win_released(void* c, const char* id)
{
    ufs_group_bridge_t* b = (ufs_group_bridge_t*)c;
    hwio_win_ctx_t io = {0};
    uint32_t i, set = UINT32_MAX;
    if (!b || !b->lease || !b->owners || !b->graph || !b->graph->ufs_catalog.compiled || !id ||
        !native_ufs_owns(b->owners, clock_ufs_domain, b->owner_epoch))
        return 0;
    io.owners = b->owners;
    for (i = 0u; i < 2u; ++i) {
        if (b->owners->ufs_group_votes[i] && !strcmp(b->owners->ufs_group_votes[i]->identity, id)) {
            set = i + 1u;
            break;
        }
    }
    if (set == UINT32_MAX)
        return 0;
    for (i = 0u; i < 2u; ++i) {
        uint32_t j;
        const power_pstate_program_t* p = ufs_catalog_get(&b->graph->ufs_catalog, 0u, set, i);
        if (!p)
            return 0;
        for (j = 0u; j < p->resources.action_cnt; ++j) {
            const power_action_t* a = &p->resources.actions[j];
            uint64_t physical = 0u;
            switch (a->kind) {
                case POWER_CLOCK:
                    if (!win_io_clock_readback(&io, a->u.clock.name, &physical) ||
                        win_io_clock_has_vote(&io, id, a->u.clock.name))
                        return 0;
                    break;
                case POWER_ICB:
                    if (!win_io_bus_readback(&io, a->u.icb.master, a->u.icb.slave, &physical) ||
                        win_io_bus_has_vote(&io, id, a->u.icb.master, a->u.icb.slave))
                        return 0;
                    break;
                case POWER_PMIC_REGULATOR:
                    if (!win_io_rail_readback(&io, a->u.pmic_regulator.name, &physical) ||
                        win_io_rail_has_vote(&io, id, a->u.pmic_regulator.name))
                        return 0;
                    break;
                default:
                    return 0;
            }
        }
    }
    {
        uint64_t live_epoch = 0u;
        return group_win_epoch(c, &live_epoch);
    }
}

static int native_ufs_group_open(owners_t* o, res_graph_t* graph, uint64_t epoch)
{
    group_vote_io_t io = {0};
    uint32_t i;
    ufs_group_bridge_t* b;
    if (!o || !graph || o->ufs_group_bridge || !epoch || !graph->ufs_catalog.compiled ||
        !graph->backend_binding_serial ||
        graph->backend_binding_serial != hw_backend_verified_serial() ||
        !native_ufs_owns(o, clock_ufs_domain, epoch))
        return 0;
    b = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*b), POOL_TAG);
    if (!b)
        return 0;
    RtlZeroMemory(b, sizeof(*b));
    b->owners = o;
    b->graph = graph;
    b->owner_epoch = epoch;
    o->ufs_group_bridge = b;
    io.ctx = b;
    io.lock = group_win_lock;
    io.unlock = group_win_unlock;
    io.read_epoch = group_win_epoch;
    io.supports = group_win_supports;
    io.snapshot = group_win_snapshot;
    io.apply = group_win_apply;
    io.verify = group_win_verify;
    io.restore = group_win_restore;
    io.verify_restored = group_win_restored;
    io.verify_final = group_win_final;
    io.verify_released = group_win_released;
    io.read_clock_config = group_win_config;
    for (i = 0u; i < 2u; ++i) {
        group_vote_t* scope = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scope), POOL_TAG);
        if (!scope)
            return 0;
        RtlZeroMemory(scope, sizeof(*scope));
        o->ufs_group_votes[i] = scope;
        if (!group_vote_bind(scope, &io, graph->ufs_catalog.p_states[i + 1u][0u].resources.dev_path,
                             0u, i + 1u, epoch))
            return 0;
    }
    return 1;
}

static int native_ufs_group_close(owners_t* o)
{
    uint32_t i;
    if (!o)
        return 0;
    if (!o->ufs_group_bridge)
        return !o->ufs_group_votes[0] && !o->ufs_group_votes[1];
    for (i = 0u; i < 2u; ++i) {
        if (o->ufs_group_votes[i]) {
            if (!group_vote_close(o->ufs_group_votes[i]))
                return 0;
            ExFreePool(o->ufs_group_votes[i]);
            o->ufs_group_votes[i] = NULL;
        }
    }
    ExFreePool(o->ufs_group_bridge);
    o->ufs_group_bridge = NULL;
    return 1;
}

/* the Windows PoFx caller must supply independently verified d0/f0; the backend cannot
 * infer these from a graph publication or a cached clock vote. */
static NTSTATUS hw_backend_win_ufs_scalar_transition_locked(_In_z_ const char* path,
                                                            _In_ ULONG comp, _In_ ULONG set,
                                                            _In_ ULONG state,
                                                            _In_ const power_dev_t* d0,
                                                            _In_ const power_dev_t* f0)
{
    win_backend_instance_t* inst;
    owners_t* o;
    const power_pstate_program_t* program;
    power_execution_t trace;
    power_result_t result;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !path || comp != 0u || set < 1u ||
        set >= UFS_PSTATE_SETS || state >= 2u || !d0 || !d0->state_known || d0->quarantined ||
        d0->current_d_state != 0u ||
        (f0 && (!f0->state_known || f0->quarantined || f0->current_d_state > 1u)) ||
        !hw_backend_ready())
        return STATUS_INVALID_DEVICE_STATE;
    inst = g_win_backend;
    o = inst ? inst->io.owners : NULL;
    if (!o || !o->ufs_group_bridge || !o->ufs_group_votes[set - 1u] ||
        o->ufs_group_bridge->owners != o || !o->ufs_group_bridge->graph ||
        !o->ufs_group_bridge->graph->sealed || o->ufs_group_bridge->graph->retired ||
        o->ufs_group_bridge->graph->quiescing ||
        !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch))
        goto done;
    program = ufs_catalog_get(&o->ufs_group_bridge->graph->ufs_catalog, comp, set, state);
    if (!program || strcmp(program->resources.dev_path, path)) {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    RtlZeroMemory(&trace, sizeof(trace));
    result = group_vote_transition(o->ufs_group_votes[set - 1u], program, &trace);
    if (result != POWER_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "ufs: P-state set=%lu state=%lu result=%d action=%lu applied=%lu restored=%lu",
                    set, state, (int)result, trace.failure_idx, trace.applied, trace.restored);
        status = STATUS_DEVICE_POWER_FAILURE;
    } else
        status = STATUS_SUCCESS;
done:
    return status;
}

NTSTATUS hw_backend_win_ufs_scalar_transition(_In_z_ const char* path, _In_ ULONG comp,
                                              _In_ ULONG set, _In_ ULONG state,
                                              _In_ const power_dev_t* d0,
                                              _In_ const power_dev_t* f0)
{
    if (!f0 || !f0->state_known || f0->quarantined || f0->current_d_state != 0u)
        return STATUS_INVALID_DEVICE_STATE;
    NTSTATUS result;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    result = hw_backend_win_ufs_scalar_transition_locked(path, comp, set, state, d0, f0);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

/* D-state and component transitions share the domain client under the graph lease. */
static NTSTATUS hw_backend_win_ufs_fs_transition_locked(_In_z_ const char* path, _In_ ULONG comp,
                                                        _In_ ULONG state,
                                                        _In_ const power_dev_t* d0,
                                                        _In_ const power_dev_t* f0)
{
    win_backend_instance_t* inst;
    owners_t* o;
    ufs_group_bridge_t* bridge;
    const power_pstate_program_t* program;
    fs_res_t* res;
    uint32_t actual;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !path || comp != 0u || state >= 2u || !d0 ||
        !d0->state_known || d0->quarantined || d0->current_d_state != 0u ||
        (f0 && (!f0->state_known || f0->quarantined || f0->current_d_state > 1u)) ||
        !hw_backend_ready())
        return STATUS_INVALID_DEVICE_STATE;
    inst = g_win_backend;
    o = inst ? inst->io.owners : NULL;
    bridge = o ? o->ufs_group_bridge : NULL;
    if (!bridge || !bridge->graph || !bridge->graph->sealed || bridge->graph->retired ||
        bridge->graph->quiescing || !native_group_bound(o, bridge->graph->backend_binding_serial))
        return status;
    program = ufs_catalog_get(&bridge->graph->ufs_catalog, comp, 0u, state);
    res = &o->ufs_native.factory.native;
    if (!program || strcmp(program->resources.dev_path, path) ||
        program->resources.action_cnt != 1u ||
        program->resources.actions[0].kind != POWER_FOOTSWITCH ||
        strcmp(program->resources.actions[0].u.footswitch.name, res->name))
        return STATUS_NOT_SUPPORTED;
    if (!group_win_lock(bridge))
        return status;
    if (fs_issue(res, program->resources.actions[0].u.footswitch.action) == FS_OK &&
        native_ufs_gdsc_physical(o, res->name, bridge->owner_epoch, &actual) &&
        actual == program->resources.actions[0].u.footswitch.action)
        status = STATUS_SUCCESS;
    else
        status = STATUS_DEVICE_POWER_FAILURE;
    group_win_unlock(bridge);
    return status;
}

NTSTATUS hw_backend_win_ufs_fs_transition(_In_z_ const char* path, _In_ ULONG comp,
                                          _In_ ULONG state, _In_ const power_dev_t* d0,
                                          _In_ const power_dev_t* f0)
{
    if (!f0 || !f0->state_known || f0->quarantined || f0->current_d_state != 0u)
        return STATUS_INVALID_DEVICE_STATE;
    NTSTATUS result;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    result = hw_backend_win_ufs_fs_transition_locked(path, comp, state, d0, f0);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

NTSTATUS hw_backend_win_ufs_p_state_transition(_In_z_ const char* path, _In_ ULONG comp,
                                               _In_ ULONG set, _In_ ULONG state,
                                               _In_ const power_dev_t* d0,
                                               _In_ const power_dev_t* f0)
{
    if (set == 0u)
        return hw_backend_win_ufs_fs_transition(path, comp, state, d0, f0);
    return hw_backend_win_ufs_scalar_transition(path, comp, set, state, d0, f0);
}

static int native_ufs_owner_attest(void* ctx, uint64_t epoch);

int hw_backend_win_ufs_owner_ready(const res_graph_t* graph, uint64_t epoch)
{
    win_backend_instance_t* inst;
    owners_t* o;
    int ready = 0;
    if (!graph || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL || !hw_backend_ready())
        return 0;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst ? inst->io.owners : NULL;
    if (o && o->ufs_group_bridge && o->ufs_group_bridge->graph == graph &&
        graph->native_ufs_owner_epoch == epoch && graph->backend_binding_serial == epoch)
        ready = native_ufs_owner_attest(o, epoch);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return ready;
}

/* execute a pinned descriptor program as one F/P transaction. Hold the lifecycle mutex across all
 * participating required clients and rollback. */
static int native_ufs_program_read_locked(owners_t* o, const power_sequence_t* sequence)
{
    ufs_native_selection_t selected[UFS_PSTATE_SETS];
    ufs_group_bridge_t* bridge;
    uint32_t cnt = 0u, i;
    if (!o || !sequence || !(bridge = o->ufs_group_bridge) || !bridge->graph ||
        !bridge->graph->sealed || bridge->graph->retired || bridge->graph->quiescing ||
        !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch) ||
        !ufs_native_select_program(&bridge->graph->ufs_catalog, sequence, selected, &cnt) ||
        !group_win_lock(bridge))
        return 0;
    for (i = 0; i < cnt; ++i) {
        uint32_t set = selected[i].set, state = selected[i].state;
        const power_pstate_program_t* program =
            ufs_catalog_get(&bridge->graph->ufs_catalog, 0u, set, state);
        if (!program)
            break;
        if (set == 0u) {
            fs_res_t* res = &o->ufs_native.factory.native;
            uint32_t physical;
            if (program->resources.action_cnt != 1u ||
                program->resources.actions[0].kind != POWER_FOOTSWITCH ||
                strcmp(program->resources.actions[0].u.footswitch.name, res->name) ||
                fs_verify(res) != FS_OK ||
                res->normal_action != program->resources.actions[0].u.footswitch.action ||
                !native_ufs_gdsc_physical(o, res->name, bridge->owner_epoch, &physical) ||
                physical != res->normal_action)
                break;
        } else {
            group_vote_t* vote = o->ufs_group_votes[set - 1u];
            if (!vote || !vote->bound || vote->quarantined || !vote->state_known ||
                vote->state != state || vote->epoch != o->os_rsc_lease.owner_epoch ||
                !group_vote_verify_held(vote, program))
                break;
        }
    }
    group_win_unlock(bridge);
    return i == cnt;
}

/* observe prior required-client identities before changing any res. Unknown or ambiguous
 * prior ownership cannot be safely rolled back. */
static int native_ufs_program_previous(owners_t* o, const ufs_native_selection_t* item,
                                       uint32_t* previous)
{
    if (!o || !item || !previous)
        return 0;
    if (item->set) {
        group_vote_t* vote = o->ufs_group_votes[item->set - 1u];
        if (!vote || !vote->bound || vote->quarantined ||
            vote->epoch != o->os_rsc_lease.owner_epoch)
            return 0;
        *previous = vote->state_known ? vote->state : UINT32_MAX;
        return 1;
    }
    {
        fs_res_t* res = &o->ufs_native.factory.native;
        if (fs_verify(res) != FS_OK || res->normal_action > FS_DISABLE)
            return 0;
        *previous = res->normal_action ? res->normal_action - 1u : UINT32_MAX;
        return 1;
    }
}

/* called from the desc-backed PoFx provider: a full, contiguous set program is submitted
 * under one Windows lifecycle lock. A partial commit is never reported successful;
 * unprovable rollback quarantines the owner. */
power_result_t hw_backend_win_ufs_program_execute(const power_sequence_t* sequence, uint64_t epoch,
                                                  const power_dev_t* d0,
                                                  const power_dev_t* current_f,
                                                  power_execution_t* trace)
{
    win_backend_instance_t* inst;
    owners_t* o;
    ufs_native_selection_t selected[UFS_PSTATE_SETS];
    uint32_t old[UFS_PSTATE_SETS] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t cnt = 0u, i = 0u, changed = 0u;
    uint32_t changed_indices[UFS_PSTATE_SETS] = {0u};
    power_result_t result = POWER_UNSUPPORTED;
    if (trace) {
        RtlZeroMemory(trace, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
    }
    if (!sequence || !trace || !epoch || !d0 || !d0->state_known || d0->quarantined ||
        d0->current_d_state != 0u ||
        (current_f &&
         (!current_f->state_known || current_f->quarantined || current_f->current_d_state > 1u)) ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !hw_backend_ready())
        return POWER_UNSUPPORTED;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst ? inst->io.owners : NULL;
    if (!native_group_bound(o, epoch) ||
        !ufs_native_select_program(&o->ufs_group_bridge->graph->ufs_catalog, sequence, selected,
                                   &cnt) ||
        !cnt)
        goto done;
    /* a call without a prior component state is only initial f0, and must enumerate all three
     * desc sets. Never fabricate an f0 proof. */
    if (!current_f && (cnt != 3u || sequence->target_d_state != 0u))
        goto done;
    {
        uint32_t j;
        int found = 0;
        res_graph_t* graph = o->ufs_group_bridge->graph;
        for (j = 0u; j < graph->dev_cnt; ++j)
            if (!strcmp(graph->devices[j].path, sequence->dev_path) &&
                &graph->devices[j].state == d0 && graph->devices[j].prepared &&
                graph->devices[j].state.state_known && !graph->devices[j].state.quarantined &&
                graph->devices[j].state.current_d_state == 0u)
                found = 1;
        if (!found)
            goto done;
    }
    for (i = 0u; i < cnt; ++i)
        if (!native_ufs_program_previous(o, &selected[i], &old[i]))
            goto done;
    for (i = 0u; i < cnt; ++i) {
        NTSTATUS st;
        const power_pstate_program_t* one = ufs_catalog_get(
            &o->ufs_group_bridge->graph->ufs_catalog, 0u, selected[i].set, selected[i].state);
        if (!one) {
            result = POWER_INVALID_PROGRAM;
            break;
        }
        if (old[i] == selected[i].state) {
            /* duplicate P-state requests verify readback without replaying the ordered clock
             * program. */
            if (!native_ufs_program_read_locked(o, &one->resources)) {
                result = POWER_STATE_DRIFT;
                break;
            }
            continue;
        }
        if (selected[i].set == 0u)
            st = hw_backend_win_ufs_fs_transition_locked(sequence->dev_path, 0u, selected[i].state,
                                                         d0, current_f);
        else
            st = hw_backend_win_ufs_scalar_transition_locked(
                sequence->dev_path, 0u, selected[i].set, selected[i].state, d0, current_f);
        if (!NT_SUCCESS(st)) {
            result = POWER_ROLLBACK_FAILED;
            break;
        }
        changed_indices[changed++] = i;
    }
    if (i == cnt && native_ufs_program_read_locked(o, sequence)) {
        trace->applied = sequence->action_cnt;
        result = POWER_OK;
        goto done;
    }
    /* reverse all dispatched identities. An initially unowned set cannot be restored without
     * an explicit provider-verified no-vote operation. */
    result = POWER_ROLLBACK_FAILED;
    while (changed) {
        uint32_t j = changed_indices[--changed];
        NTSTATUS st;
        if (old[j] == UINT32_MAX)
            break;
        if (selected[j].set == 0u)
            st = hw_backend_win_ufs_fs_transition_locked(sequence->dev_path, 0u, old[j], d0,
                                                         current_f);
        else
            st = hw_backend_win_ufs_scalar_transition_locked(
                sequence->dev_path, 0u, selected[j].set, old[j], d0, current_f);
        if (!NT_SUCCESS(st))
            break;
        ++trace->restored;
    }
    if (result == POWER_ROLLBACK_FAILED)
        o->ufs_owner_uncertain = TRUE;
    trace->failure_idx = i;
done:
    trace->result = result;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

int hw_backend_win_ufs_program_verify(const power_sequence_t* sequence, uint64_t epoch)
{
    win_backend_instance_t* inst;
    owners_t* o;
    int result = 0;
    if (!sequence || !epoch || KeGetCurrentIrql() != PASSIVE_LEVEL || !hw_backend_ready())
        return 0;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst ? inst->io.owners : NULL;
    if (native_group_bound(o, epoch))
        result = native_ufs_program_read_locked(o, sequence);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

/* the device-path vote key cannot be shared between D- and F/P-states: the native
 * aggregators treat it as the client's identity. */
static int scalar_win_epoch(void* ctx, uint64_t* epoch)
{
    return native_ufs_epoch(ctx, epoch);
}

static int scalar_win_owns(void* ctx, const power_action_t* a)
{
    owners_t* o = (owners_t*)ctx;
    hwio_win_ctx_t io = {0};
    uint32_t force = 0u;
    if (!o || !a || o->ufs_owner_uncertain ||
        !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch))
        return 0;
    io.owners = o;
    switch (a->kind) {
        case POWER_CLOCK:
            if (a->u.clock.vote == 9u)
                return a->u.clock.extra_cnt == 1u &&
                       clock_config_rule(a->u.clock.name, a->u.clock.extra[0]) &&
                       win_io_clock_config_readback(&io, a->u.clock.name, &force);
            return (a->u.clock.vote == 1u || a->u.clock.vote == 2u || a->u.clock.vote == 3u ||
                    a->u.clock.vote == 8u) &&
                   win_io_clock_known(&io, a->u.clock.name);
        case POWER_ICB:
            return win_io_bus_route_known(&io, a->u.icb.master, a->u.icb.slave);
        case POWER_PMIC_REGULATOR:
            return win_io_rail_known(&io, a->u.pmic_regulator.name);
        default:
            return 0;
    }
}

static int scalar_win_verify(void* ctx, const power_action_t* a)
{
    owners_t* o = (owners_t*)ctx;
    hwio_win_ctx_t io = {0};
    uint64_t first = 0u, second = 0u;
    uint32_t f = 0u, g = 0u;
    if (!o || !a || o->ufs_owner_uncertain)
        return 0;
    io.owners = o;
    switch (a->kind) {
        case POWER_CLOCK:
            if (a->u.clock.vote == 9u)
                return win_io_clock_config_readback(&io, a->u.clock.name, &f) &&
                       win_io_clock_config_readback(&io, a->u.clock.name, &g) && f == g && f <= 1u;
            return win_io_clock_readback(&io, a->u.clock.name, &first) &&
                   win_io_clock_readback(&io, a->u.clock.name, &second) && first == second;
        case POWER_ICB:
            return win_io_bus_readback(&io, a->u.icb.master, a->u.icb.slave, &first) &&
                   win_io_bus_readback(&io, a->u.icb.master, a->u.icb.slave, &second) &&
                   first == second;
        case POWER_PMIC_REGULATOR:
            return win_io_rail_readback(&io, a->u.pmic_regulator.name, &first) &&
                   win_io_rail_readback(&io, a->u.pmic_regulator.name, &second) && first == second;
        default:
            return 0;
    }
}

static int scalar_win_read(void* ctx, const char* identity, const power_action_t* a,
                           uint32_t* outstanding)
{
    owners_t* o = (owners_t*)ctx;
    int found = 0;
    uint64_t ab = 0u, ib = 0u;
    rail_vote_t rv;
    clock_client_t* clock;
    LONG ci;
    if (!o || !identity || !a || !outstanding ||
        !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch) || !owners_lock(o))
        return 0;
    switch (a->kind) {
        case POWER_CLOCK:
            if (a->u.clock.vote == 9u)
                found = o->ufs_ice_config.active && !strcmp(o->ufs_ice_config.identity, identity);
            else {
                ci = clock_idx(a->u.clock.name);
                clock = ci < 0 ? NULL : clock_client(o, identity, (ULONG)ci, 0);
                found = clock && clock->in_use;
            }
            break;
        case POWER_ICB:
            found =
                icb_get_client_vote(&o->bus, identity, a->u.icb.master, a->u.icb.slave, &ab, &ib) &&
                (ab || ib);
            break;
        case POWER_PMIC_REGULATOR:
            found = rail_get_client_vote(&o->rails, identity, a->u.pmic_regulator.name, &rv) &&
                    rv.enable;
            break;
        default:
            owners_unlock(o);
            return 0;
    }
    owners_unlock(o);
    if (!scalar_win_verify(o, a))
        return 0;
    *outstanding = found ? 1u : 0u;
    return 1;
}

static int scalar_win_issue(void* ctx, const char* identity, const power_action_t* a)
{
    owners_t* o = (owners_t*)ctx;
    hwio_win_ctx_t io = {0};
    int ok = 0;
    uint32_t baseline = 0u, observed = 0u;
    if (!o || !identity || !scalar_win_owns(o, a))
        return 0;
    io.owners = o;
    switch (a->kind) {
        case POWER_CLOCK:
            if (a->u.clock.vote == 9u) {
                if (o->ufs_ice_config.saved ||
                    !win_io_clock_config_readback(&io, a->u.clock.name, &baseline))
                    return 0;
                /* preserve the original controller mode even on an ambiguous write. */
                o->ufs_ice_config.saved = TRUE;
                o->ufs_ice_config.original_force_on = baseline;
                (VOID) RtlStringCbCopyA(o->ufs_ice_config.identity,
                                        sizeof(o->ufs_ice_config.identity), identity);
                ok = win_io_clock_config_write(&io, a->u.clock.name, 1u) &&
                     win_io_clock_config_readback(&io, a->u.clock.name, &observed) &&
                     observed == 1u;
                if (ok)
                    o->ufs_ice_config.active = TRUE;
            } else
                ok = win_io_clock_apply(&io, identity, a);
            break;
        case POWER_ICB:
            ok = win_io_bus_vote(&io, identity, a);
            break;
        case POWER_PMIC_REGULATOR:
            ok = win_io_rail_vote(&io, identity, a->u.pmic_regulator.name,
                                  a->u.pmic_regulator.fields);
            break;
        default:
            return 0;
    }
    if (!ok || !scalar_win_verify(o, a)) {
        o->ufs_owner_uncertain = TRUE;
        return 0;
    }
    return 1;
}

static int scalar_win_withdraw(void* ctx, const char* identity, const power_action_t* a)
{
    owners_t* o = (owners_t*)ctx;
    hwio_win_ctx_t io = {0};
    uint32_t actual = UINT32_MAX;
    int ok = 0;
    if (!o || !identity || !a || o->ufs_owner_uncertain ||
        !native_ufs_owns(o, clock_ufs_domain, o->os_rsc_lease.owner_epoch))
        return 0;
    io.owners = o;
    switch (a->kind) {
        case POWER_CLOCK:
            if (a->u.clock.vote == 9u) {
                if (!o->ufs_ice_config.active || !o->ufs_ice_config.saved ||
                    strcmp(o->ufs_ice_config.identity, identity))
                    return 0;
                ok = win_io_clock_config_write(&io, a->u.clock.name,
                                               o->ufs_ice_config.original_force_on) &&
                     win_io_clock_config_readback(&io, a->u.clock.name, &actual) &&
                     actual == o->ufs_ice_config.original_force_on;
                if (ok)
                    RtlZeroMemory(&o->ufs_ice_config, sizeof(o->ufs_ice_config));
            } else
                ok = win_io_clock_withdraw(&io, identity, a->u.clock.name);
            break;
        case POWER_ICB:
            ok = win_io_bus_withdraw(&io, identity, a->u.icb.master, a->u.icb.slave);
            break;
        case POWER_PMIC_REGULATOR:
            ok = win_io_rail_withdraw(&io, identity, a->u.pmic_regulator.name);
            break;
        default:
            return 0;
    }
    if (!ok || !scalar_win_verify(o, a)) {
        o->ufs_owner_uncertain = TRUE;
        return 0;
    }
    return 1;
}

static int native_ufs_scalar_program_clients_close(owners_t* o, uint64_t epoch)
{
    if (!o)
        return 0;
    if (o->ufs_scalar_owner &&
        (o->ufs_scalar_owner->live || o->ufs_scalar_owner->bound ||
         o->ufs_scalar_owner->quarantined) &&
        !po_fx_required_close(o->ufs_scalar_owner, epoch))
        return 0;
    /* keep desc and owner allocations if unbind cannot prove that no physical required client
     * remains. */
    if (o->ufs_scalar_required) {
        if (o->ufs_scalar_required->bound && !scalar_required_unbind(o->ufs_scalar_required))
            return 0;
        if (o->ufs_scalar_required->quarantined)
            return 0;
    }
    if (o->ufs_ice_config.saved)
        return 0;
    if (o->ufs_scalar_owner) {
        ExFreePool(o->ufs_scalar_owner);
        o->ufs_scalar_owner = NULL;
    }
    if (o->ufs_scalar_catalog) {
        ExFreePool(o->ufs_scalar_catalog);
        o->ufs_scalar_catalog = NULL;
    }
    if (o->ufs_scalar_required) {
        ExFreePool(o->ufs_scalar_required);
        o->ufs_scalar_required = NULL;
    }
    return 1;
}

static int native_ufs_scalar_program_clients(owners_t* o, const res_graph_t* graph, uint64_t epoch)
{
    scalar_required_io_t io = {0};
    pofx_required_provider_t provider = {0};
    uint32_t set, state, i;
    pofx_required_catalog_t* c;
    if (!o || !graph || !graph->ufs_catalog.compiled || !epoch || o->ufs_scalar_required ||
        o->ufs_scalar_catalog || o->ufs_scalar_owner)
        return 0;
    o->ufs_scalar_required =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*o->ufs_scalar_required), POOL_TAG);
    if (!o->ufs_scalar_required)
        return 0;
    RtlZeroMemory(o->ufs_scalar_required, sizeof(*o->ufs_scalar_required));
    o->ufs_scalar_catalog =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*o->ufs_scalar_catalog), POOL_TAG);
    if (!o->ufs_scalar_catalog)
        return 0;
    RtlZeroMemory(o->ufs_scalar_catalog, sizeof(*o->ufs_scalar_catalog));
    o->ufs_scalar_owner =
        ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*o->ufs_scalar_owner), POOL_TAG);
    if (!o->ufs_scalar_owner)
        return 0;
    RtlZeroMemory(o->ufs_scalar_owner, sizeof(*o->ufs_scalar_owner));
    c = o->ufs_scalar_catalog;
    for (set = 0u; set < UFS_PSTATE_SETS; ++set)
        for (state = 0u; state < UFS_STATES_PER_SET; ++state) {
            const power_pstate_program_t* p = ufs_catalog_get(&graph->ufs_catalog, 0u, set, state);
            if (!p || p->resources.action_cnt > POWER_MAX_ACTIONS ||
                c->cnt > POFX_REQUIRED_MAX_ACTIONS - p->resources.action_cnt)
                return 0;
            for (i = 0u; i < p->resources.action_cnt; ++i) {
                pofx_required_action_t* a;
                if (p->resources.actions[i].kind == POWER_FOOTSWITCH)
                    continue;
                if (c->cnt >= POFX_REQUIRED_MAX_ACTIONS)
                    return 0;
                a = &c->actions[c->cnt++];
                a->action = p->resources.actions[i];
                a->program_class = (uint8_t)PROGRAM_P_STATE;
                a->comp = 0u;
                a->set = set;
                a->state = state;
            }
        }
    if (!c->cnt)
        return 0;
    c->bound = 1u;
    io.ctx = o;
    io.read_epoch = scalar_win_epoch;
    io.owns = scalar_win_owns;
    io.read_vote = scalar_win_read;
    io.issue = scalar_win_issue;
    io.withdraw = scalar_win_withdraw;
    io.verify_physical = scalar_win_verify;
    if (!scalar_required_bind(o->ufs_scalar_required, &io, epoch))
        return 0;
    scalar_required_provider(o->ufs_scalar_required, &provider);
    if (!po_fx_required_open(o->ufs_scalar_owner, c, &provider, epoch))
        return 0;
    return po_fx_required_ready(o->ufs_scalar_owner, epoch);
}

static int native_ufs_owner_attest(void* ctx, uint64_t epoch)
{
    owners_t* o = (owners_t*)ctx;
    uint64_t owner_epoch;
    if (!native_group_bound(o, epoch))
        return 0;
    owner_epoch = o->ufs_group_bridge->owner_epoch;
    return o->ufs_native.bound && o->ufs_rail_owner.bound && o->ufs_domain_owner.bound &&
           fs_factory_verify(&o->ufs_native.factory) == FS_OK && o->ufs_scalar_owner &&
           po_fx_required_ready(o->ufs_scalar_owner, owner_epoch) && o->ufs_group_votes[0] &&
           o->ufs_group_votes[1] && o->ufs_group_votes[0]->bound && o->ufs_group_votes[1]->bound &&
           o->ufs_group_votes[0]->epoch == owner_epoch &&
           o->ufs_group_votes[1]->epoch == owner_epoch && !o->ufs_owner_uncertain;
}

static int native_ufs_arcs_retire(owners_t* o)
{
    uint32_t i;
    int ok = 1;
    if (!o || o->ufs_rail_owner.bound || o->ufs_rail_owner.held || !owners_lock_live(o, LIVE_RPMH))
        return 0;
    for (i = 0u; i < 2u; ++i)
        if (o->ufs_arc[i].bound && !arc_retire(&o->ufs_arc[i])) {
            ok = 0;
            break;
        }
    owners_unlock(o);
    return ok;
}

static int native_ufs_graph_bind(owners_t* o, res_graph_t* graph)
{
    ufs_cxmx_io_t io = {0};
    fs_gdsc_rail_provider_t rails = {0};
    fs_factory_provider_t factory = {0};
    uint64_t epoch;
    uint32_t phase = 1u;
    if (!o || !graph || !graph->ufs_catalog.compiled || !native_ufs_epoch(o, &epoch) ||
        o->ufs_owner_uncertain || o->ufs_native.bound || o->ufs_rail_owner.bound ||
        o->ufs_domain_owner.bound)
        return 0;
    io.ctx = o;
    io.owns_domain = native_ufs_owns;
    io.owner_epoch = native_ufs_epoch;
    io.rail_resolved = native_ufs_resolve;
    io.read_client_vote = native_ufs_client;
    io.read_physical = native_ufs_physical;
    io.vote = native_ufs_vote;
    io.withdraw = native_ufs_withdraw;
    if (!ufs_cx_mx_bind(&o->ufs_rail_owner, &io, epoch) ||
        !ufs_cx_mx_rail_provider(&o->ufs_rail_owner, &rails))
        goto failed;
    phase = 2u;
    if (!fs_gdsc_init_window_with_rails(&o->ufs_domain_owner, &o->gcc_io, clock_ufs_domain,
                                        OEM_UFS_PHY_DOMAIN_ID, o->mmio.gcc.physical,
                                        o->mmio.gcc.bytes, o, native_ufs_owns, epoch, &rails) ||
        !fs_gdsc_provider(&o->ufs_domain_owner, &factory))
        goto failed;
    phase = 5u;
    if (!native_ufs_scalar_program_clients(o, graph, epoch))
        goto failed;
    phase = 6u;
    if (!native_ufs_group_open(o, graph, epoch))
        goto failed;
    phase = 7u;
    if (ufs_native_provision(&o->ufs_native, graph, &factory, "ufs0", o,
                             native_ufs_gdsc_physical) != FS_OK ||
        !graph_bind_native_ufs_owner(graph, graph->backend_binding_serial, o,
                                     native_ufs_owner_attest))
        goto failed;
    return 1;
failed:
    TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                "ufs: bind failed phase=%lu epoch=%llX ARC=%05lX raw=%08lX result=%d", phase,
                (unsigned long long)epoch, o->ufs_arc_last_address, o->ufs_arc_last_response,
                (int)o->ufs_arc_last_result);
    if (!native_ufs_group_close(o) || !native_ufs_scalar_program_clients_close(o, epoch)) {
        o->ufs_owner_uncertain = TRUE;
        return 0;
    }
    if (ufs_native_rollback_unpublished(&o->ufs_native, &o->ufs_domain_owner, &o->ufs_rail_owner) !=
            FS_OK ||
        !native_ufs_arcs_retire(o)) {
        o->ufs_owner_uncertain = TRUE;
        return 0;
    }
    return 0;
}

static const char* win_io_rail_probe_name(void* ctx, uint32_t key)
{
    UNREFERENCED_PARAMETER(ctx);
    return key < platform_rail_cnt ? platform_rail_names[key] : NULL;
}

/* the caller marshals the peer's strings and output array into its own kernel-safe storage
 * first (src/DAL/ddi.c): no structured exception may unwind through the owner lock. */

static NTSTATUS win_bcm_command_cnt_locked(_In_z_ const char* master, _In_z_ const char* slave,
                                           _Out_ ULONG* cnt)
{
    win_backend_instance_t* inst = g_win_backend;
    owners_t* o;
    icb_command_t cmds[ICB_MAX_COMMANDS];
    uint32_t n = 0;
    int ok;
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;
    if (!master || !slave)
        return STATUS_INVALID_PARAMETER;
    if (!inst || !(o = inst->io.owners))
        return STATUS_DEVICE_NOT_READY;
    /* live is protected by the owner mutex, not by an unlocked BOOLEAN sample in the caller's
     * PnP generation. The same lock is used by D0Quiesce to revoke every native delegate. */

    if (!owners_lock_live(o, LIVE_RPMH))
        return STATUS_DEVICE_NOT_READY;
    ok = icb_single_req_commands(&o->bus, master, slave, 1u, 1u, cmds, ICB_MAX_COMMANDS, &n);
    owners_unlock(o);
    if (!ok)
        return STATUS_NOT_FOUND;
    *cnt = n;
    return STATUS_SUCCESS;
}

static NTSTATUS win_bcm_commands_locked(_In_z_ const char* master, _In_z_ const char* slave,
                                        _In_ ULONGLONG ib, _In_ ULONGLONG ab,
                                        _Out_writes_to_(capacity, *cnt) icb_command_t* out,
                                        _In_ ULONG capacity, _Out_ ULONG* cnt)
{
    win_backend_instance_t* inst = g_win_backend;
    owners_t* o;
    /* validate route and count before touching the peer buf; sequence builder internals are not a
     * peer ABI guarantee. */

    icb_command_t staged[ICB_MAX_COMMANDS];
    uint32_t n = 0;
    int ok;
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;
    if (!master || !slave || !out || !capacity)
        return STATUS_INVALID_PARAMETER;
    if (!inst || !(o = inst->io.owners))
        return STATUS_DEVICE_NOT_READY;
    if (capacity > ICB_MAX_COMMANDS)
        capacity = ICB_MAX_COMMANDS;
    /* require the same native last-moment lock+live fence as actual BUSARB execution. */

    if (!owners_lock_live(o, LIVE_RPMH))
        return STATUS_DEVICE_NOT_READY;
    ok = icb_single_req_commands(&o->bus, master, slave, ab, ib, staged, capacity, &n);
    owners_unlock(o);
    if (ok && (n == 0u || n > capacity || n > ICB_MAX_COMMANDS))
        return STATUS_DATA_ERROR;
    if (!ok) {
        ULONG need = 0;
        if (NT_SUCCESS(win_bcm_command_cnt_locked(master, slave, &need)) && need > capacity)
            return STATUS_BUFFER_TOO_SMALL;
        return STATUS_NOT_FOUND;
    }
    /* the enclosing public lifecycle mutex keeps owner state alive throughout the copy. */

    RtlCopyMemory(out, staged, n * sizeof(staged[0]));
    *cnt = n;
    return STATUS_SUCCESS;
}

/* peer requests may arrive on unrelated WDF queues as the FDO releases hardware. pin the
 * backend owner until the command sequence is copied out. */

NTSTATUS hw_backend_win_bcm_command_cnt(_In_z_ const char* master, _In_z_ const char* slave,
                                        _Out_ ULONG* cnt)
{
    NTSTATUS st;
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    st = win_bcm_command_cnt_locked(master, slave, cnt);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return st;
}

NTSTATUS hw_backend_win_bcm_commands(_In_z_ const char* master, _In_z_ const char* slave,
                                     _In_ ULONGLONG ib, _In_ ULONGLONG ab,
                                     _Out_writes_to_(capacity, *cnt) icb_command_t* out,
                                     _In_ ULONG capacity, _Out_ ULONG* cnt)
{
    NTSTATUS st;
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    st = win_bcm_commands_locked(master, slave, ib, ab, out, capacity, cnt);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return st;
}

static VOID owners_report_coverage(owners_t* o)
{
    ULONG i;

    o->bcms_total = icb_bcm_node_cnt;
    o->rails_total = platform_rail_cnt;
    o->routes_total = platform_route_cnt;
    o->clocks_total = platform_clock_cnt;
    o->rails_resolved = 0;
    for (i = 0; i < o->rails.rail_cnt; ++i)
        if (o->rails.rails[i].resolved)
            ++o->rails_resolved;
    o->routes_supported = 0;
    for (i = 0; i < platform_route_cnt; ++i) {
        const char* missing =
            icb_route_missing_bcm(&o->bus, platform_routes[i].master, platform_routes[i].slave);
        if (!missing) {
            ++o->routes_supported;
            continue;
        }
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "icb: route=%s->%s unsupported BCM=%s",
                    platform_routes[i].master, platform_routes[i].slave, missing);
    }
    o->clocks_present = 0;
    for (i = 0; i < platform_clock_cnt; ++i)
        if (gcc_find_clock(platform_clock_names[i]))
            ++o->clocks_present;
        else
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CLOCK, "clock: %s absent from GCC table",
                        platform_clock_names[i]);
    for (i = 0; i < o->rails.rail_cnt; ++i)
        if (!o->rails.rails[i].resolved)
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE, "pmic: rail=%s resource=%s unresolved",
                        o->rails.rails[i].ppp,
                        o->rails.rails[i].cmd_db_name[0] ? o->rails.rails[i].cmd_db_name
                                                         : "(no name rule)");
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                "resources: coverage rails=%lu/%lu routes=%lu/%lu clocks=%lu/%lu BCMs=%lu/%lu",
                o->rails_resolved, o->rails_total, o->routes_supported, o->routes_total,
                o->clocks_present, o->clocks_total, o->bcms_usable, o->bcms_total);
}

static VOID owners_probe(owners_t* o)
{
    ULONG status = 0;
    rsc_hlos_geometry_t rsc_geometry;
    gcc_state_t s1, s2;
    tlmm_gpio_t p1, p2;
    const gcc_clock_t* probe;
    ULONG i;
    int rail_idx = -1;

    /* resume verification must not expose the prior RPMh readiness while the
     * new readback is in progress. The global PoFx admission lease is already closed by the d0
     * revalidator. */

    o->probe = 0;
    o->rpmh_live = FALSE;

    if (o->cmd_db) {
        if (!o->bus.initialized && !o->rails.initialized) {
            /* ICB/rail initialization clears client votes. Never repeat it after publication
             * without matching hardware withdrawals. */

            o->bcms_usable = icb_bus_init(&o->bus, o->cmd_db, o->cmd_db_bytes);
            (void)rail_owner_init(&o->rails, o->cmd_db, o->cmd_db_bytes, platform_rail_names,
                                  platform_rail_cnt);
        } else if (!o->bus.initialized || !o->rails.initialized || o->rails.cmd_db != o->cmd_db ||
                   o->rails.cmd_db_bytes != o->cmd_db_bytes) {
            /* mixed generations cannot be repaired by destructive init: retain all client votes and
             * fail the infrastructure probe. */

            TraceEvents(
                TRACE_LEVEL_ERROR, TRACE_RESOURCE,
                "resources: BCM/rail graph generation mismatch votes=retained attestation=denied");
            goto skip_catalog_probe;
        }
        for (i = 0; i < o->bus.bcm_cnt; ++i)
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                        "icb: BCM=%-5s address=%05lX unit=%lu width=%lu vcd=%lu kind=%lu%s",
                        o->bus.bcms[i].name, o->bus.bcms[i].address, o->bus.bcms[i].unit,
                        o->bus.bcms[i].width, o->bus.bcms[i].clk_id,
                        (i < icb_bcm_node_cnt) ? icb_bcm_nodes[i].kind : 0u,
                        o->bus.bcms[i].address_known ? "" : " (ABSENT)");
        for (i = 0; i < o->rails.rail_cnt; ++i)
            if (o->rails.rails[i].resolved)
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                            "pmic: rail=%s resource=%s address=%05lX", o->rails.rails[i].ppp,
                            o->rails.rails[i].cmd_db_name, o->rails.rails[i].address);
        for (i = 0; i < o->rails.rail_cnt && rail_idx < 0; ++i)
            if (o->rails.rails[i].resolved)
                rail_idx = (int)i;

        if (o->bcms_usable != 0u && rail_idx >= 0)
            o->probe |= PROBE_CMDDB;
        else
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "cmddb: parsed BCMs=%lu readable-rail=%s", o->bcms_usable,
                        rail_idx >= 0 ? "a" : "no");
    }
skip_catalog_probe:

    if (mmio_read32(&o->mmio.rsc, RSC_ID_OFFSET, &o->rsc_id) &&
        mmio_read32(&o->mmio.rsc, RSC_PARENTCHILD_CONFIG_OFFSET, &o->drv_config) &&
        mmio_read32(&o->mmio.rsc, TCS_STATUS(OWNER_TCS), &status)) {
        /* tcs geometry does not grant AMC ownership; validate both geometry and the current owner.
         */

        if ((o->rsc_id & RSC_ID_MASK) == RSC_ID_POR && (status & TCS_STATUS_IDLE) &&
            rpmh_tcs_check_hlos_geometry(o->drv_config, (uint8_t)MOLOKAI_WIN_TCS_TOTAL,
                                         (uint8_t)TCS_BATCH_MAX, &rsc_geometry))
            o->probe |= PROBE_RSC_ID;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: DRV2 base=%llX id=%08lX expected=%06lX config=%08lX TCS=%u status=%08lX "
                    "count=%lu slots=%lu%s",
                    o->mmio.rsc.physical, o->rsc_id, (ULONG)RSC_ID_POR, o->drv_config, OWNER_TCS,
                    status, (o->drv_config >> 12) & 0x3Fu, (o->drv_config >> 27) & 0x1Fu,
                    (o->probe & PROBE_RSC_ID) ? "" : " (geometry/id/idle invalid, not admitted)");
    }

    /* the deferred owner submits the RPMh read probe. Do not call synchronous authorize on
     * arm_deferred; the probe does not modify grants. */

    if ((o->probe & (PROBE_CMDDB | PROBE_RSC_ID)) == (PROBE_CMDDB | PROBE_RSC_ID) &&
        rail_idx >= 0) {
        const int deferred_owner_ready =
            rpmh_deferred_probe_ready(&o->deferred_scheduler, &o->rpmh_wait);
        uint32_t irq_enable_before = 0u, irq_enable_after = 0u, irq_pending = 0u;
        const uint8_t granted = (uint8_t)(o->rpmh.owned_amc_mask & o->rpmh.external_grant_mask &
                                          o->rsc_completion.granted_mask);
        /* software owner flags alone cannot prove that DRV2 will physically raise an interrupt for
         * its admitted AMC slots. The completion path relies on TCS_IRQ_ENABLE: a missing bit
         * would strand an otherwise valid deferred read indefinitely. */

        const int physical_irq_ready =
            deferred_owner_ready && granted && o->rpmh.io.read32 &&
            o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_enable_before) &&
            o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &irq_pending) &&
            o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_enable_after) &&
            irq_enable_before == irq_enable_after && (irq_enable_after & granted) == granted &&
            !(irq_pending & granted) && irq_enable_after != UINT32_MAX && irq_pending != UINT32_MAX;
        if (!deferred_owner_ready) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: completion owner unavailable read=rejected");
        } else if (!physical_irq_ready) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: IRQ enable mismatch grants=%02X readback=%08lX/%08lX pending=%08lX "
                        "probe=rejected",
                        (ULONG)granted, (ULONG)irq_enable_before, (ULONG)irq_enable_after,
                        (ULONG)irq_pending);
        } else if (registry_query_u_long(L"MolokaiRpmhProbe", 1u) == 0u) {
            /* the default requires hardware readback before admitting a boot-capable provider. */

            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: reads disabled attestation=denied");
        } else if (rail_read(o, rail_idx, &o->rail_probe)) {
            o->probe |= PROBE_RPMH;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                        "pmic: rail=%s resource=%s voltage=%lumV enable=%lu mode=%lu",
                        o->rails.rails[rail_idx].ppp, o->rails.rails[rail_idx].cmd_db_name,
                        (ULONG)(o->rail_probe & 0xFFFFFu), (ULONG)((o->rail_probe >> 20) & 0xFu),
                        (ULONG)((o->rail_probe >> 24) & 0xFFu));
        } else {
            /* read-only timeout evidence from the canonical LIVE scheduler. This is under the RPMh
             * owner mutex and does not acknowledge, cancel or replay any vote. A late physical ACK
             * still cannot authorize a client-side DPM state after its waiter timed out. */
            if (o->rpmh_wait.timed_out_req && KeGetCurrentIrql() == PASSIVE_LEVEL &&
                rpmh_scheduler_enter(o)) {
                rpmh_tcs_result_t outcome = TCS_INVALID;
                uint32_t ticket = o->rpmh_wait.timed_out_req;
                int status = rpmh_sync_deferred_timeout_inspect(
                    &o->rpmh_wait, o->rpmh_wait.owner_epoch, ticket, &outcome);
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                            "rpmh: timeout ticket=%lu epoch=%llX status=%d late-ack=%u "
                            "verified-result=%d owner=retained",
                            (ULONG)ticket, (unsigned long long)o->rpmh_wait.owner_epoch, status,
                            (unsigned)o->rpmh_wait.late_completion_seen, (int)outcome);
                rpmh_scheduler_leave(o);
            }
            /* do not revoke a pending ticket or restore TCS registers here: that requires the
             * physical native IRQ owner to finish/drain. */

            /* no ticket is acknowledged, scheduler lock is acquired, or hardware state is altered.
             * These observations distinguish a timeout/faulted worker from a missing completion
             * without requiring the user to infer RPMh ownership manually in WinDbg. */

            {
                uint32_t pending = UINT32_MAX, enable = UINT32_MAX;
                int status_read = o->rpmh.io.read32 &&
                                  o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &pending) &&
                                  o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &enable);
                TraceEvents(
                    TRACE_LEVEL_ERROR, TRACE_RPMH,
                    "rpmh: read incomplete request=%lu fault=%u busy=%02X quarantine=%02X "
                    "work-fault=%u pending=%u irq-read=%u irq-status=%08lX irq-enable=%08lX "
                    "work=%u mask-phase=%lu mask-reason=%lu dpc-accepted=%ld dpc-finished=%ld "
                    "dpc-pending=%ld deferred=%02X",
                    (ULONG)o->rpmh_wait.active_req, (unsigned)o->rpmh_wait.faulted,
                    (unsigned)o->rpmh_scheduler.busy_mask,
                    (unsigned)o->rpmh_scheduler.quarantined_mask,
                    (unsigned)o->rpmh_irq_pipeline.faulted,
                    (unsigned)o->rpmh_irq_pipeline.pending.active, (unsigned)status_read,
                    (ULONG)pending, (ULONG)enable, (unsigned)o->rpmh_irq_pipeline.work.state,
                    (ULONG)o->rpmh_irq_mask_owner.phase, (ULONG)o->rpmh_irq_mask_owner.last_failure,
                    (LONG)o->rpmh_dpc_tracker.accepted, (LONG)o->rpmh_dpc_tracker.finished,
                    (LONG)o->rpmh_dpc_tracker.outstanding, (unsigned)o->rpmh.deferred_active_mask);
            }
        }
    }

    /* gpll0 lock and 5. a stable clock readback (the gate's sentinel). */
    if (mmio_read32(&o->mmio.gcc, gcc_gpll0_mode_offset, &o->gpll0_mode)) {
        if ((o->gpll0_mode & PLL_MODE_LOCK_DET) && (o->gpll0_mode & PLL_MODE_OUTCTRL))
            o->probe |= PROBE_GPLL0;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CLOCK, "clock: GCC base=%llX GPLL0=%08lX%s",
                    o->mmio.gcc.physical, o->gpll0_mode,
                    (o->probe & PROBE_GPLL0) ? " (locked)" : " (NOT locked)");
    }
    probe = gcc_find_clock(win_backend_probe_clock_name(0));
    if (probe && gcc_readback(&o->gcc_io, probe, &s1) == GCC_OK &&
        gcc_readback(&o->gcc_io, probe, &s2) == GCC_OK &&
        gcc_pack_state(&s1) == gcc_pack_state(&s2)) {
        o->clock_probe = gcc_pack_state(&s1);
        o->probe |= PROBE_CLOCK;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_CLOCK,
                    "clock: %s CBCR=%08lX vote=%08lX config=%08lX on=%u enabled=%u rate=%luHz",
                    probe->name, s1.cbcr, s1.vote_reg, s1.cfg_rcgr, s1.on, s1.enabled, s1.hz);
    } else {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK, "clock: sentinel readback failed");
    }

    /* 6. a stable TLMM readback of a DSDT pin. */
    if (!o->tlmm_uncertain && tlmm_read_pin(o, TLMM_PROBE_PIN, &p1) &&
        tlmm_read_pin(o, TLMM_PROBE_PIN, &p2) &&
        RtlCompareMemory(&p1, &p2, sizeof(p1)) == sizeof(p1)) {
        o->tlmm_probe = ((ULONGLONG)p1.function << 32) | (p1.pull << 8) | (p1.drive << 4) |
                        (p1.direction << 1) | p1.state;
        o->probe |= PROBE_TLMM;
        TraceEvents(
            TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
            "tlmm: tile0=%llX pin=%lu function=%lu direction=%lu pull=%lu drive=%lu output=%lu",
            o->mmio.tlmm.physical, p1.pin, p1.function, p1.direction, p1.pull, p1.drive, p1.state);
    } else {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "tlmm: pin=%lu readback failed",
                    TLMM_PROBE_PIN);
    }

    o->clocks_live = (o->probe & (PROBE_GPLL0 | PROBE_CLOCK)) == (PROBE_GPLL0 | PROBE_CLOCK);
    o->tlmm_live = !o->tlmm_uncertain && (o->probe & PROBE_TLMM) != 0;
    o->rpmh_live = !o->rpmh_write_fence.quarantined &&
                   (o->probe & (PROBE_CMDDB | PROBE_RSC_ID | PROBE_RPMH)) ==
                       (PROBE_CMDDB | PROBE_RSC_ID | PROBE_RPMH);
    owners_report_coverage(o);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                "resources: probes=%02lX/%02lX clocks=%u tlmm=%u rpmh=%u", o->probe,
                (ULONG)PROBE_ALL, o->clocks_live, o->tlmm_live, o->rpmh_live);
}

/* a controller VA cannot be unmapped while an AMC, ISR, scheduler or synchronous waiter
 * may still borrow it. This same check must remain mandatory after native owner wiring. */

static int rsc_d0_mapping_can_close(const owners_t* o)
{
    uint32_t i;
    if (!o || !o->rpmh_irq_pipeline.initialized || o->rpmh_irq_pipeline.bound ||
        o->rpmh_irq_pipeline.faulted || o->rpmh_irq_pipeline.pending.active ||
        o->rpmh_irq_pipeline.pending.epoch || o->rpmh_irq_pipeline.unmask_finalizing.active ||
        o->rpmh_irq_pipeline.unmask_finalizing.epoch || o->rpmh_dpc_tracker.phase ||
        o->rpmh_dpc_tracker.outstanding || o->rpmh_dpc_tracker.queuing ||
        o->rpmh_dpc_tracker.running || o->rpmh_irq_mask_owner.phase ||
        o->rpmh_irq_bridge.pipeline || o->rpmh_irq_bridge.epoch || o->rpmh_amc_drain.binding ||
        o->rpmh_amc_drain.epoch || o->rpmh_amc_drain.owner_epoch || o->rpmh_irq_src.owner_epoch ||
        o->rpmh_irq_src.ledger_epoch || o->gic_irq37_owner.active || o->gic_irq37_owner.faulted ||
        InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) != 0 || o->rpmh_live ||
        o->rpmh_write_fence.quarantined || o->rpmh.owned_amc_mask || o->rpmh.external_grant_mask ||
        o->rpmh.completion_handler_bound || o->rpmh.dpc_consumes_completion ||
        /* losing every software grant does NOT settle a physical TCS IRQ-enable cleanup debt. */

        o->rpmh.irq_enabled_by_us || o->rpmh.deferred_active_mask || o->rpmh.completed_amc_mask ||
        o->rsc_completion.irq_bound || o->rsc_completion.granted_mask ||
        o->deferred_scheduler.scheduler || o->rpmh_wait.scheduler || o->rpmh_wait.active_req ||
        o->rpmh_wait.faulted || o->rpmh_scheduler.busy_mask || o->rpmh_scheduler.quarantined_mask ||
        o->rpmh_scheduler.execution_authorized || o->rpmh_scheduler.owner_epoch)
        return 0;
    for (i = 0; i < RPMH_JOBS; ++i)
        if (o->rpmh_scheduler.jobs[i].state != RPMH_JOB_FREE)
            return 0;
    /* revocation retains aborted or awaiting-ACK tickets until their owner drains them. */

    for (i = 0; i < MOLOKAI_WIN_AMC_CNT; ++i)
        if (o->rsc_completion.amc[i].state != RSC_AMC_VACANT || o->rsc_completion.amc[i].req_id ||
            o->rsc_completion.amc[i].owner_epoch || o->rpmh.deferred_epoch[i] ||
            o->rpmh.deferred_req_id[i] || o->rpmh.deferred_command_cnt[i] ||
            o->rpmh.deferred_read_mask[i])
            return 0;
    return 1;
}

static VOID owners_destroy(owners_t* o)
{
    if (!o)
        return;
    /* the WDF/PnP owner has not yet supplied a complete worker/ISR teardown proof. Never
     * release bound storage. */

    if (ufs_native_retains_graph(&o->ufs_native) || o->ufs_group_bridge || o->ufs_group_votes[0] ||
        o->ufs_group_votes[1] || o->ufs_rail_owner.held || o->ufs_owner_uncertain ||
        o->rpmh_irq_pipeline.bound || o->rpmh_irq_pipeline.faulted ||
        o->rpmh_irq_pipeline.pending.active || o->rpmh_irq_pipeline.unmask_finalizing.active ||
        o->rpmh_dpc_tracker.phase || o->rpmh_dpc_tracker.outstanding ||
        o->rpmh_irq_mask_owner.phase || o->rpmh_irq_bridge.pipeline || o->rpmh_amc_drain.binding ||
        o->rpmh_amc_drain.owner_epoch || o->rpmh_irq_src.owner_epoch ||
        o->rpmh_irq_src.ledger_epoch || o->gic_irq37_owner.active || o->gic_irq37_owner.faulted ||
        InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) != 0 ||
        o->deferred_scheduler.scheduler || o->rpmh_wait.scheduler || o->rpmh_wait.active_req ||
        o->rpmh_scheduler.busy_mask) {
        g_quarantined_rsc_owner = o;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: worker rundown unverified controller=retained");
        return;
    }
    /* leave_cb the shared RSC as it was found: drop the AMC grant and the completion interrupt
     * bit this owner enabled. */

    rpmh_scheduler_revoke(&o->rpmh_scheduler);
    rsc_completion_revoke(&o->rsc_completion);
    {
        uint8_t pending = rpmh_tcs_restore(&o->rpmh);
        if (pending != 0u) {
            /* never release a controller map with outstanding IRQ debt OR without verified lock
             * synchronization. An unsynchronized restore can return 0x80 even with zero pending IRQ
             * bits: a concurrent producer may still be touching the mapping. */

            g_quarantined_rsc_owner = o;
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_RPMH,
                        "rpmh: teardown unverified mask=%02X unsynchronized=%u owner=retained "
                        "attach=blocked",
                        (ULONG)pending, (ULONG)((pending & TCS_CLEANUP_UNSYNCHRONIZED) != 0u));
            return;
        }
    }
    /* the mapping itself never changes GICD state. */

    if (o->d0_gic.base) {
        if (!rsc_d0_mapping_can_close(o) ||
            !gic_d0_close_win(&o->d0_gic, o->d0_gic.pnp_generation, 1)) {
            g_quarantined_rsc_owner = o;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: GIC mapping retained");
            return;
        }
    }
    if (o->d0_rsc.base) {
        if (!rsc_d0_mapping_can_close(o) ||
            !rsc_d0_map_close_win(&o->d0_rsc, o->d0_rsc.pnp_generation, 1)) {
            g_quarantined_rsc_owner = o;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: RSC mapping has pending users owner=retained");
            return;
        }
        /* mmio.rsc is an alias, not an independently owned allocation. */
        RtlZeroMemory(&o->mmio.rsc, sizeof(o->mmio.rsc));
    }
    mmio_unmap(&o->mmio);
    if (o->cmd_db)
        ExFreePool(o->cmd_db);
    ExFreePool(o);
}

/* it is intentionally independent from the legacy PEP0 RSC memory assignment and NEVER
 * serves as an exclusive RSC/IRQ/TCS hardware-owner predicate. Keep it separately testable
 * so stale WDF/PnP epochs cannot create a backend staging owner. */

static BOOLEAN can_stage_rpmh_clients(BOOLEAN candidate_valid, BOOLEAN unique_irq,
                                      ULONGLONG pnp_generation, ULONGLONG interrupt_generation,
                                      BOOLEAN interrupt_created)
{
    return candidate_valid && unique_irq && interrupt_created && pnp_generation != 0 &&
           pnp_generation == interrupt_generation;
}

static owners_t* owners_create(VOID)
{
    owners_t* o;
    NTSTATUS st;
    if (gcc_clock_cnt > CLOCK_TABLE_MAX || icb_bcm_node_cnt > ICB_MAX_BCMS) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: table capacity exceeded clocks=%lu BCMs=%lu", gcc_clock_cnt,
                    icb_bcm_node_cnt);
        return NULL;
    }
    o = (owners_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*o), POOL_TAG);
    if (!o)
        return NULL;
    RtlZeroMemory(o, sizeof(*o));
    KeInitializeMutex(&o->lock, 0);
    ExInitializeFastMutex(&o->tlmm_lock);
    KeInitializeMutex(&o->rpmh_lock, 0);
    KeInitializeMutex(&o->rpmh_scheduler_lock, 0);
    KeInitializeEvent(&o->rpmh_completion_event, NotificationEvent, FALSE);
    /* initialize an owner-retained pipeline BEFORE any PnP/d0 callback can publish a bridge.
     * It stays unbound, with no GIC/RSC/MMIO authority, until the same d0 owner establishes
     * physical rights and callbacks. */

    if (!rpmh_pipeline_init(&o->rpmh_irq_pipeline)) {
        ExFreePool(o);
        return NULL;
    }
    /* PEP0 has no dedicated RSC memory resource. Mapping remains blocked until ownership; software
     * scheduler/client and command-database setup may proceed independently. */

    st = mmio_map(&o->mmio);
    if (!NT_SUCCESS(st) && st != STATUS_NOT_SUPPORTED) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "mmio: staging failed status=%08X", st);
        ExFreePool(o);
        return NULL;
    }
    if (st == STATUS_NOT_SUPPORTED)
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RPMH,
                    "rpmh: scheduler initialized hardware-grants=0");

    st = mmio_snapshot_command_db(&o->cmd_db, &o->cmd_db_bytes);
    if (NT_SUCCESS(st))
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "cmddb: snapshot base=%llX bytes=%lu mapping=released", (ULONGLONG)CMDDB_PHYS,
                    (ULONG)o->cmd_db_bytes);
    else
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "cmddb: snapshot failed status=%08X", st);
    o->gcc_io.ctx = o;
    o->gcc_io.read32 = gcc_read;
    o->gcc_io.write32 = gcc_write;
    o->gcc_io.barrier = io_barrier;
    o->gcc_io.stall_microseconds = io_stall;
    o->tlmm_io.ctx = o;
    o->tlmm_io.lock = tlmm_lock_fn;
    o->tlmm_io.unlock = tlmm_unlock_fn;
    o->tlmm_io.owns_pin = tlmm_owns_pin;
    o->tlmm_io.read32 = tlmm_read;
    o->tlmm_io.write32 = tlmm_write;
    o->tlmm_io.barrier = io_barrier;
    o->rpmh.io.ctx = o;
    o->rpmh.io.read32 = rpmh_read;
    o->rpmh.io.write32 = rpmh_write;
    o->rpmh.io.barrier = io_barrier;
    o->rpmh.io.spin_hint = rpmh_spin;
    o->rpmh.io.lock = rpmh_lock_fn;
    o->rpmh.io.unlock = rpmh_unlock_fn;
    o->rpmh.owned_amc_mask = 0u;
    o->rpmh.external_grant_mask = 0u; /* no verified Windows owner. */
    o->rpmh.completion_handler_bound = 0u;
    o->rpmh.dpc_consumes_completion = 0u;
    rsc_completion_init(&o->rsc_completion);
    /* an unbound epoch intentionally makes every src read fail closed. */

    o->rpmh_irq_src.dev = &o->rpmh;
    o->rpmh_irq_src.ledger = &o->rsc_completion;
    /* all work callbacks share retained owner storage. Bind them only after WDF/GICD/RSC/TCS
     * ownership is established. */

    o->rpmh_owner_work_ops.ctx = o;
    o->rpmh_owner_work_ops.mask_owned_interrupt = rpmh_owner_mask_amc_irq;
    o->rpmh_owner_work_ops.queue_dpc = rpmh_owner_queue_amc_dpc;
    o->rpmh_owner_work_ops.queue_pep_work = rpmh_owner_queue_amc_work;
    o->rpmh_owner_work_ops.has_finished_amc = rpmh_owner_has_finished_amc;
    o->rpmh_owner_work_ops.finish_one_amc = rpmh_owner_finish_one_amc;
    o->rpmh_owner_work_ops.unmask_owned_interrupt = rpmh_owner_unmask_amc_irq;
    o->rpmh_owner_work_ops.enter_completion_lock = rpmh_owner_enter_completion_lock;
    o->rpmh_owner_work_ops.leave_completion_lock = rpmh_owner_leave_completion_lock;
    o->rpmh_irq_bridge.serialize_ctx = &o->rpmh_irq_src;
    o->rpmh_irq_bridge.read_owned_completion_pending = rpmh_tcs_read_owned_irq_status;
    o->rpmh_irq_bridge.enter = rpmh_owner_src_enter;
    o->rpmh_irq_bridge.leave_cb = rpmh_owner_src_leave;
    o->rpmh_irq_bridge.passive_drain = 1u;
    /* pipeline/epoch remain ZERO: no WDF ISR, GIC mask or AMC ownership. */
    o->rpmh.poll_limit = RPMH_POLL_LIMIT;
    {
        rpmh_driver_config_t drv = {MOLOKAI_WIN_HW_DRV,      0u,
                                    MOLOKAI_WIN_TCS_TOTAL,   MOLOKAI_WIN_AMC_CNT,
                                    MOLOKAI_WIN_SLEEP_START, MOLOKAI_WIN_WAKE_START,
                                    TCS_BATCH_MAX,           MOLOKAI_WIN_READ_AMC_MASK};
        uint8_t client;
        if (!rpmh_scheduler_init_result(&o->rpmh_scheduler, &drv, rpmh_execute_sync_slot, o) ||
            !rpmh_scheduler_add_client(&o->rpmh_scheduler, "BUSARB", 0u, &client) ||
            client != RPMH_CLIENT_BUS ||
            !rpmh_scheduler_add_client(&o->rpmh_scheduler, "PMICVREGVOTE", 0u, &client) ||
            client != RPMH_CLIENT_RAIL ||
            !rpmh_scheduler_add_client(&o->rpmh_scheduler, "READBACK", 0u, &client) ||
            client != RPMH_CLIENT_READ ||
            !rpmh_scheduler_add_client(&o->rpmh_scheduler, "PROBE", 0u, &client) ||
            client != RPMH_CLIENT_PROBE) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: scheduler initialization failed");
            owners_destroy(o);
            return NULL;
        }
    }
    owners_probe(o);
    return o;
}

static const char* delegation_name(hw_delegation_t d)
{
    switch (d) {
        case HW_DELEGATION_LIVE:
            return "LIVE";
        case HW_DELEGATION_REFUSED_BY_RULE:
            return "NATIVE-DESCRIPTOR-OWNER-REQUIRED";
        default:
            return "NOT-ESTABLISHED";
    }
}

static VOID trace_report(const win_backend_t* core)
{
    static const char* const kinds[8] = {"?",        "CLOCK",        "BUSARB", "TLMMGPIO",
                                         "PMICGPIO", "PMICVREGVOTE", "DELAY",  "PSTATE_ADJUST"};
    win_backend_report_t r;
    int k;
    win_backend_report(core, &r);
    for (k = (int)POWER_CLOCK; k <= (int)POWER_PSTATE_ADJUST; ++k)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE, "resources: provider=%s state=%s",
                    kinds[k], delegation_name(r.delegation[k]));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                "resources: live=%lu unavailable=%lu rejected=%lu clock-readback=%d attestation=%d "
                "admission=%d",
                r.live_kinds, r.not_established_kinds, r.refused_kinds, r.clock_readback_present,
                core->table.production_attestation == HW_ATTEST_MAGIC, hw_backend_ready());
}

/* a retained graph may contain provider callbacks into a detached backend; a quarantined
 * NPA registry may also reference its core even when graph publication failed. An RSC
 * owner may retain an active IRQ or transaction. */

static NTSTATUS win_owner_attach_fence(_In_ WDFDEVICE dev)
{
    owner_attach_decision_t decision = owner_attach_decision(
        (g_quarantined_rsc_owner != NULL || g_quarantined_backend_instance != NULL ||
         g_quarantined_npa != NULL),
        g_published_graph != NULL, g_win_backend != NULL && g_win_backend->io.dev != dev);
    switch (decision) {
        case OWNER_ATTACH_QUARANTINED:
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: attach rejected reason=owner-quarantined");
            return STATUS_DEVICE_HARDWARE_ERROR;
        case OWNER_ATTACH_RETAINED_GRAPH:
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: attach rejected reason=graph-retained");
            return STATUS_DEVICE_ALREADY_ATTACHED;
        case OWNER_ATTACH_FOREIGN_DEV:
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: attach rejected reason=another-owner");
            return STATUS_DEVICE_ALREADY_ATTACHED;
        default:
            return STATUS_SUCCESS;
    }
}

static VOID hw_backend_win_detach_locked(_In_ WDFDEVICE dev);

static NTSTATUS hw_backend_win_attach_locked(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst;
    owners_t* owners = NULL;
    hwio_t io;
    p_dev_ctx_t ctx;
    uint64_t epoch;
    int rc;
    NTSTATUS attach_fence;

    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    ctx = get_dev_ctx(dev);
    if (!ctx || ctx->is_control_dev)
        return STATUS_INVALID_PARAMETER;

    attach_fence = win_owner_attach_fence(dev);
    if (!NT_SUCCESS(attach_fence))
        return attach_fence;
    if (g_win_backend) {
        hw_backend_win_detach_locked(dev);
        /* never continue with a new instance over that failed teardown. */

        attach_fence = win_owner_attach_fence(dev);
        if (!NT_SUCCESS(attach_fence))
            return attach_fence;
        if (g_win_backend)
            return STATUS_DEVICE_BUSY;
    }

    inst = (win_backend_instance_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*inst), POOL_TAG);
    if (!inst)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(inst, sizeof(*inst));
    inst->io.dev = dev;

    /* paired PnP descriptors permit software scheduler staging, but grant no MMIO, TCS, GIC, ISR or
     * power-vote authority. */

    if (registry_query_u_long(L"MolokaiHwOwners", 1) == 1u) {
        ULONGLONG generation = ctx->rpmh_oem_pnp_observation.generation;
        if (can_stage_rpmh_clients(ctx->rpmh_oem_pnp_observation.valid,
                                   ctx->rsc_irq_snapshot.unique_candidate, generation,
                                   ctx->rpmh_observed_pnp_generation,
                                   ctx->rpmh_observed_wdf_interrupt != NULL)) {
            owners = owners_create();
            if (!owners)
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                            "rpmh: client staging failed providers=unavailable");
        } else {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                        "rpmh: client staging skipped reason=IRQ-generation-unavailable");
        }
    } else {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: client staging disabled by configuration");
    }
    inst->io.owners = owners;

    RtlZeroMemory(&io, sizeof(io));
    io.ctx = &inst->io;
    io.delay_ms = win_io_delay;
    /* reg immutable PMIC callbacks even if provider starts later. */

    io.pmic_gpio_cfg_digital_output = win_io_pmic_gpio_cfg;
    io.pmic_gpio_query_raw_state = win_io_pmic_gpio_query;
    io.pmic_gpio_execution_ready = win_io_pmic_gpio_execution_ready;
    if (win_io_pmic_gpio_probe_provider(&inst->io))
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "pmic: GPIO transport verified during prepare");
    else
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                    "pmic: GPIO transport unavailable during prepare");
    /* these delegates must remain in the registered immutable I/O table even when
     * PrepareHardware has no native GCC/TLMM owner. d0 revalidation cannot add a callback
     * after reg, and reusing a stale table would otherwise permanently reject CLOCK/TLMM DPM. */

    if (owners) {
        io.tlmm_execution_ready = win_io_tlmm_execution_ready;
        io.tlmm_cfg = win_io_tlmm_cfg;
        io.tlmm_readback = win_io_tlmm_readback;
        io.clock_execution_ready = win_io_clock_execution_ready;
        io.clock_apply = win_io_clock_apply;
        io.clock_withdraw = win_io_clock_withdraw;
        io.clock_readback = win_io_clock_readback;
        io.clock_known = win_io_clock_known;
        io.clock_has_vote = win_io_clock_has_vote;
        io.clock_config_readback = win_io_clock_config_readback;
        io.clock_config_write = win_io_clock_config_write;
    }
    /* the core checks rpmh_execution_ready dynamically before admitting any BUSARB or PMIC
     * regulator action, and each native method independently refuses hardware access while
     * rpmh_live is false. Never patch the registered table underneath a concurrent PoFx
     * reader. */

    if (owners) {
        io.rpmh_execution_ready = win_io_rpmh_execution_ready;
        io.bus_vote = win_io_bus_vote;
        io.bus_withdraw = win_io_bus_withdraw;
        io.bus_readback = win_io_bus_readback;
        io.bus_route_known = win_io_bus_route_known;
        io.bus_has_vote = win_io_bus_has_vote;
        io.bus_probe_route = win_io_bus_probe_route;
        io.pmic_regulator_vote = win_io_rail_vote;
        io.pmic_regulator_withdraw = win_io_rail_withdraw;
        io.pmic_regulator_readback = win_io_rail_readback;
        io.pmic_regulator_known = win_io_rail_known;
        io.pmic_regulator_has_vote = win_io_rail_has_vote;
        io.pmic_regulator_probe_name = win_io_rail_probe_name;
    }

    io.self_test_pmic_number = 5u;
    io.self_test_gpio = 5u;

    epoch =
        ((uint64_t)0x99u << 56) | (uint64_t)(ULONG)InterlockedIncrement(&g_win_backend_generation);

    rc = win_backend_init(&inst->core, &io, epoch);
    if (rc == 0 && owners && owners->probe == PROBE_ALL) {
        if (registry_query_u_long(L"MolokaiOwnersAttest", 1) == 1u) {
            /* every identity, readback and the end-to-end RPMh read passed on this device: the
             * backend is validated on hardware. */

            inst->core.table.production_attestation = HW_ATTEST_MAGIC;
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                        "resources: probes verified attestation=enabled");
        } else {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                        "resources: probes verified attestation=disabled-by-configuration");
        }
    } else if (rc == 0 && owners) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: probes incomplete observed=%02lX required=%02lX attestation=denied",
                    owners->probe, (ULONG)PROBE_ALL);
    }
    if (rc == 0)
        rc = win_backend_reg(&inst->core);
    if (rc != 0) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: registration rejected result=%d", rc);
        owners_destroy(owners);
        if (owners && g_quarantined_rsc_owner == owners) {

            g_quarantined_backend_instance = inst;
            return STATUS_UNSUCCESSFUL;
        }
        ExFreePool(inst);
        return STATUS_UNSUCCESSFUL;
    }
    g_win_backend = inst;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                "resources: owner registered epoch=%016llX interface=%lu", epoch,
                (ULONG)inst->core.table.version);
    /* after this the gate is a passive predicate that a PoFx callback may consult. */

    if (hw_backend_validate(epoch))
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "resources: hardware readback verified");
    else
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: hardware verification failed admission=closed");
    trace_report(&inst->core);
    return STATUS_SUCCESS;
}

/* the production owner handoff performs the missing in-place promotion itself. A prior
 * version required arm_deferred to have already been installed, although this backend
 * never installed it. */

NTSTATUS hw_backend_win_bind_deferred_completion(_In_ WDFDEVICE dev, _In_ ULONGLONG owner_epoch,
                                                 _In_ ULONG ledger_epoch)
{
    owners_t* o;
    win_backend_instance_t* instance;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    const rpmh_driver_config_t firmware_geometry = {
        MOLOKAI_WIN_HW_DRV,      0u,
        MOLOKAI_WIN_TCS_TOTAL,   MOLOKAI_WIN_AMC_CNT,
        MOLOKAI_WIN_SLEEP_START, MOLOKAI_WIN_WAKE_START,
        TCS_BATCH_MAX,           MOLOKAI_WIN_READ_AMC_MASK};
    uint32_t i;
    int idle = 1;
    int provisional_irq_lock = 0;
    p_dev_ctx_t dev_ctx;
    if (!dev || !owner_epoch || !ledger_epoch || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    dev_ctx = get_dev_ctx(dev);
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    instance = g_win_backend;
    o = instance && instance->io.dev == dev ? instance->io.owners : NULL;
    /* a nonce, IRQ vector or PnP generation is not an adequate substitute for the backend
     * owner epoch. */

    /* reject a stale, still-published or callback-busy KMDF context BEFORE committing the
     * deferred scheduler/AMC drain. publication is the last step, but promoting the scheduler
     * while WDF cannot accept an owner irreversibly strands its clients. These checks are
     * extra prerequisites, not hardware grants. */

    if (o && instance->core.owner_epoch == owner_epoch && dev_ctx &&
        dev_ctx->rpmh_observed_wdf_interrupt &&
        oem_wdf_owner_publication_vacant_win((void*)dev_ctx->rpmh_observed_wdf_interrupt) &&
        o->rpmh_irq_pipeline.initialized && !o->rpmh_irq_pipeline.bound &&
        !o->rpmh_irq_pipeline.faulted && !o->rpmh_irq_pipeline.pending.active &&
        !o->rpmh_irq_pipeline.unmask_finalizing.active && !o->rpmh_dpc_tracker.phase &&
        !o->rpmh_dpc_tracker.outstanding && !o->rpmh_irq_mask_owner.phase &&
        !o->rpmh_amc_drain.binding && !o->rpmh_amc_drain.epoch && !o->rpmh_irq_src.owner_epoch &&
        rpmh_scheduler_enter(o)) {
        /* bind the published ISR owner before deferred submission. The RPMh mutex serializes
         * commands/ISR; this check does not acquire the WDF lock or establish GIC/geometry proof.
         */

        if (dev_ctx && !dev_ctx->is_control_dev && dev_ctx->rpmh_observed_wdf_interrupt &&
            o->gic_irq37_owner.active && !o->gic_irq37_owner.faulted &&
            o->gic_irq37_owner.epoch == owner_epoch &&
            o->gic_irq37_owner.wdf_interrupt == (void*)dev_ctx->rpmh_observed_wdf_interrupt &&
            o->rpmh_irq_mask_ops.ctx == &o->gic_irq37_owner &&
            o->rpmh_irq_mask_ops.exclusive_owner && o->rpmh_irq_mask_ops.read_masked &&
            o->rpmh_irq_mask_ops.write_masked &&
            o->rpmh_irq_mask_ops.exclusive_owner(o->rpmh_irq_mask_ops.ctx, owner_epoch,
                                                 (void*)dev_ctx->rpmh_observed_wdf_interrupt) ==
                1 &&
            !o->rpmh.io.enter_irq_lock && !o->rpmh.io.leave_irq_lock && !o->rpmh.io.irq_lock_ctx) {
            o->rpmh.io.enter_irq_lock = oem_wdf_check_tcs_command_owner_win;
            o->rpmh.io.leave_irq_lock = oem_wdf_leave_tcs_command_owner_win;
            o->rpmh.io.get_irq_lock_refusal = oem_wdf_read_last_tcs_lock_refusal_win;
            o->rpmh.io.irq_lock_ctx = (void*)dev_ctx->rpmh_observed_wdf_interrupt;
            provisional_irq_lock = 1;
        }
        for (i = 0u; i < RPMH_JOBS; ++i)
            if (o->rpmh_scheduler.jobs[i].state != RPMH_JOB_FREE)
                idle = 0;
        if (idle && !o->rpmh_scheduler.busy_mask && !o->rpmh_scheduler.execution_authorized &&
            !o->rpmh_scheduler.quarantined_mask && !o->rpmh_write_fence.quarantined &&
            !o->rpmh_write_fence.epoch && !o->deferred_scheduler.scheduler &&
            !o->rpmh_wait.scheduler && o->rpmh.completion_handler_bound &&
            o->rpmh.dpc_consumes_completion && o->rpmh.external_grant_mask &&
            o->rpmh.owned_amc_mask && o->rpmh.io.enter_irq_lock && o->rpmh.io.leave_irq_lock &&
            o->rpmh.io.irq_lock_ctx && dev_ctx &&
            o->rpmh.io.irq_lock_ctx == (void*)dev_ctx->rpmh_observed_wdf_interrupt &&
            o->rpmh.io.enter_irq_lock == oem_wdf_check_tcs_command_owner_win &&
            o->rpmh.io.leave_irq_lock == oem_wdf_leave_tcs_command_owner_win &&
            o->rpmh.io.get_irq_lock_refusal == oem_wdf_read_last_tcs_lock_refusal_win &&
            o->gic_irq37_owner.active && !o->gic_irq37_owner.faulted &&
            o->rpmh_irq_mask_ops.ctx == &o->gic_irq37_owner && o->rsc_completion.irq_bound &&
            o->rsc_completion.epoch == ledger_epoch && o->rsc_completion.granted_mask &&
            rpmh_deferred_scheduler_bind_owner(&o->deferred_scheduler, &o->rpmh_scheduler, &o->rpmh,
                                               &o->rsc_completion, &firmware_geometry,
                                               (uint64_t)owner_epoch, (uint32_t)ledger_epoch)) {
            /* promotion requires a retained AMC drain bound to this scheduler and ledger
             * generation. */

            if (rpmh_amc_drain_bind(&o->rpmh_amc_drain, &o->deferred_scheduler) &&
                rpmh_bind_completion_wait(o) &&
                /* bind the GICD owner while IRQ37 is masked and verify its physical state. */

                oem_irq_mask_bind(&o->rpmh_irq_mask_owner, &o->rpmh_irq_mask_ops,
                                  (void*)dev_ctx->rpmh_observed_wdf_interrupt, owner_epoch) &&
                oem_wdf_dpc_tracker_init_win(&o->rpmh_dpc_tracker,
                                             (void*)dev_ctx->rpmh_observed_wdf_interrupt,
                                             owner_epoch) &&
                rpmh_pipeline_bind_owner(&o->rpmh_irq_pipeline, &o->rpmh_owner_work_ops,
                                         owner_epoch)) {
                /* its epoch matches all physical and WDF owners. */
                o->rpmh_irq_pipeline.work.on_owner_irq_restored = rpmh_owner_irq_restored;
                o->rpmh_irq_pipeline.work.restored_ctx = o;

                o->rpmh_irq_bridge.pipeline = &o->rpmh_irq_pipeline;
                o->rpmh_irq_bridge.epoch = owner_epoch;
                if (oem_wdf_publish_native_owner_win(
                        (void*)dev_ctx->rpmh_observed_wdf_interrupt, owner_epoch,
                        &o->rpmh_irq_bridge, &o->rpmh_dpc_tracker, &o->rpmh_irq_mask_owner)) {

                    if (rpmh_write_fence_init(&o->rpmh_write_fence, (uint64_t)owner_epoch)) {
                        o->rpmh_irq_src.owner_epoch = (uint64_t)owner_epoch;
                        o->rpmh_irq_src.ledger_epoch = (uint32_t)ledger_epoch;
                        o->rpmh_amc_drain.irq_src = &o->rpmh_irq_src;
                        status = STATUS_SUCCESS;
                    }
                }
            }
            if (!NT_SUCCESS(status)) {
                /* never return to the polling path or free the scheduler/IRQ-owned storage after a
                 * partial publication. Refuse subsequent RPMh operations until a full native
                 * owner-rundown implementation exists. */

                rpmh_scheduler_revoke(&o->rpmh_scheduler);
                status = STATUS_DEVICE_HARDWARE_ERROR;
            }
        }
        /* binding the WDF TCS lock is an unpublished, provisional step. A failed pre-enable
         * scheduler admission (bad firmware geometry, outstanding job, missing ledger, etc.) must
         * not leave those three callbacks installed: a later retry explicitly needs the fields
         * vacant and would otherwise be permanently. */

        if (provisional_irq_lock && !NT_SUCCESS(status) && !o->deferred_scheduler.scheduler &&
            !o->rpmh_wait.scheduler && !o->rpmh_scheduler.execution_authorized &&
            !o->rpmh_irq_pipeline.bound && !o->rpmh_irq_bridge.epoch &&
            !o->rpmh_amc_drain.binding && !o->rpmh_dpc_tracker.phase &&
            !o->rpmh_irq_mask_owner.phase && !o->rpmh_irq_src.owner_epoch &&
            o->rpmh.io.enter_irq_lock == oem_wdf_check_tcs_command_owner_win &&
            o->rpmh.io.leave_irq_lock == oem_wdf_leave_tcs_command_owner_win &&
            o->rpmh.io.get_irq_lock_refusal == oem_wdf_read_last_tcs_lock_refusal_win &&
            o->rpmh.io.irq_lock_ctx == (void*)dev_ctx->rpmh_observed_wdf_interrupt) {
            o->rpmh.io.enter_irq_lock = NULL;
            o->rpmh.io.leave_irq_lock = NULL;
            o->rpmh.io.irq_lock_ctx = NULL;
            o->rpmh.io.get_irq_lock_refusal = NULL;
        }
        rpmh_scheduler_leave(o);
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

/* the previously missing physically effective, independently authorized AMC-slot
 * preparation transaction. supplied a native WDF publication, but no production entry
 * point could prepare physical TCS IRQ-enable state and bind the matching immutable
 * ledger. The owner acquiring exclusive DRV2 + IRQ37. */

NTSTATUS hw_backend_win_prepare_granted_amcs(WDFDEVICE dev, ULONGLONG pnp_generation,
                                             ULONGLONG owner_epoch, ULONG ledger_epoch,
                                             UCHAR granted_amcs, void* proof_ctx,
                                             int (*exclusive_amc_owner)(void*, uint64_t, uint64_t,
                                                                        void*, uint8_t))
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    rpmh_pnp_resources_t sequence = {0};
    NTSTATUS result = STATUS_DEVICE_NOT_READY;
    ULONG slot;
    UCHAR cleanup_debt = 0u;
    ULONG irq_before = 0, irq_after = 0, irq_pending = 0;
    if (!dev || !pnp_generation || !owner_epoch || !ledger_epoch || !granted_amcs ||
        (granted_amcs & (UCHAR)~MOLOKAI_WIN_AMC_MASK) || !exclusive_amc_owner ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    ctx = o ? get_dev_ctx(dev) : NULL;
    /* match the PnP/D0 generation and authorized GICD owner before writing RSC IRQ registers. */

    if (!o || !ctx || ctx->is_control_dev || g_quarantined_rsc_owner ||
        g_quarantined_backend_instance || inst->core.owner_epoch != owner_epoch ||
        ctx->rpmh_observed_pnp_generation != pnp_generation ||
        ctx->rpmh_identity_pnp_generation != pnp_generation || !ctx->rpmh_observed_wdf_interrupt ||
        !rpmh_oem_pnp_observation_read(&ctx->rpmh_oem_pnp_observation, pnp_generation, &sequence) ||
        !rpmh_has_exclusive_irq37_resource(&sequence, pnp_generation) || !o->d0_rsc.base ||
        o->d0_rsc.pnp_generation != pnp_generation || !o->d0_gic.base ||
        o->d0_gic.pnp_generation != pnp_generation || o->mmio.rsc.base != o->d0_rsc.base ||
        !o->gic_irq37_owner.active || o->gic_irq37_owner.faulted ||
        o->gic_irq37_owner.epoch != owner_epoch ||
        o->gic_irq37_owner.wdf_interrupt != (void*)ctx->rpmh_observed_wdf_interrupt ||
        o->rpmh_irq_mask_ops.ctx != &o->gic_irq37_owner || !o->rpmh_irq_mask_ops.exclusive_owner ||
        !o->rpmh_irq_mask_ops.exclusive_owner(o->rpmh_irq_mask_ops.ctx, owner_epoch,
                                              (void*)ctx->rpmh_observed_wdf_interrupt) ||
        !exclusive_amc_owner(proof_ctx, owner_epoch, pnp_generation,
                             (void*)ctx->rpmh_observed_wdf_interrupt, granted_amcs) ||
        !o->rpmh_irq_mask_ops.read_masked || !o->rpmh_irq_mask_ops.write_masked ||
        !o->rpmh.io.read32 || !o->rpmh.io.write32 || !o->rpmh.io.lock || !o->rpmh.io.unlock ||
        o->rpmh.owned_amc_mask || o->rpmh.external_grant_mask || o->rpmh.irq_enabled_by_us ||
        o->rpmh.completion_handler_bound || o->rpmh.dpc_consumes_completion ||
        o->rpmh.deferred_active_mask || o->rsc_completion.irq_bound ||
        o->rsc_completion.granted_mask || o->deferred_scheduler.scheduler ||
        o->rpmh_wait.scheduler || o->rpmh_scheduler.execution_authorized ||
        o->rpmh_scheduler.busy_mask || o->rpmh_scheduler.quarantined_mask ||
        !oem_wdf_completion_idle_win((void*)ctx->rpmh_observed_wdf_interrupt))
        goto out;
    /* a bare GIC ownership flag is not enough. */

    {
        int masked = -1;
        if (!o->rpmh_irq_mask_ops.read_masked(o->rpmh_irq_mask_ops.ctx, &masked) || masked != 1)
            goto out;
    }
    /* physical DRV2 identity and all requested AMC slots must still be idle and unprogrammed.
     * TcsPrepare repeats these checks under its own RSC lock before the first write,
     * preventing a stale snapshot from authorizing a different transaction. */

    if (!o->rpmh.io.read32(o->rpmh.io.ctx, RSC_ID_OFFSET, &irq_before) ||
        irq_before != o->d0_rsc.id ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, RSC_PARENTCHILD_CONFIG_OFFSET, &irq_after) ||
        irq_after != o->d0_rsc.config ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &irq_pending) || irq_pending != 0u ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_before) ||
        irq_before == UINT32_MAX ||
        (irq_before & granted_amcs) != o->os_rsc_lease.pre_enabled_amc_irq_mask ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_after) || irq_after != irq_before ||
        o->rpmh.pre_enabled_irq_mask)
        goto out;
    if (!rsc_completion_bind(&o->rsc_completion, (uint32_t)ledger_epoch, granted_amcs, 1u))
        goto out;
    /* prepare permissions are provisional. Submit only after ISR/ledger publication and a completed
     * D0 readback. */

    o->rpmh.pre_enabled_irq_mask = o->os_rsc_lease.pre_enabled_amc_irq_mask;
    o->rpmh.external_grant_mask = granted_amcs;
    o->rpmh.completion_handler_bound = 1u;
    o->rpmh.dpc_consumes_completion = 1u;
    for (slot = 0u; slot < MOLOKAI_WIN_AMC_CNT; ++slot) {
        if (!(granted_amcs & (1u << slot)))
            continue;
        /* recheck the independent owner before every possible write; the provider must retain its
         * exclusive lease across the call. */

        if (!exclusive_amc_owner(proof_ctx, owner_epoch, pnp_generation,
                                 (void*)ctx->rpmh_observed_wdf_interrupt, granted_amcs))
            goto rollback;
        if (rpmh_tcs_prepare(&o->rpmh, (uint8_t)slot) != TCS_OK)
            goto rollback;
    }
    /* verify actual TCS IRQ enable bits after all controller writes, rather than accepting a
     * bookkeeping-only grant. */

    if (!o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_before) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &irq_pending) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_after) || irq_before != irq_after ||
        (irq_after & granted_amcs) != granted_amcs || (irq_pending & granted_amcs) ||
        (o->rpmh.owned_amc_mask & granted_amcs) != granted_amcs ||
        !exclusive_amc_owner(proof_ctx, owner_epoch, pnp_generation,
                             (void*)ctx->rpmh_observed_wdf_interrupt, granted_amcs))
        goto rollback;
    result = STATUS_SUCCESS;
    goto out;
rollback:
    cleanup_debt = rpmh_tcs_restore(&o->rpmh);
    rsc_completion_revoke(&o->rsc_completion);
    if (cleanup_debt) {
        /* readback did not prove every touched physical bit restored. */
        g_quarantined_rsc_owner = o;
        result = STATUS_DEVICE_BUSY;
    } else
        result = STATUS_DEVICE_HARDWARE_ERROR;
out:
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

/* counterpart: a failed pre-enable WDF publication must not strand recoverable TCS
 * IRQ-enable state. No ISR or active ticket is allowed, and the controller's independently
 * granted AMC ownership is kept through the physical restore. */

NTSTATUS hw_backend_win_abort_unpublished_granted_amcs(
    WDFDEVICE dev, ULONGLONG pnp_generation, ULONGLONG owner_epoch, ULONG ledger_epoch,
    UCHAR granted_amcs, void* proof_ctx,
    int (*exclusive_amc_owner)(void*, uint64_t, uint64_t, void*, uint8_t))
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    NTSTATUS result = STATUS_DEVICE_BUSY;
    UCHAR debt;
    ULONG slot;
    if (!dev || !pnp_generation || !owner_epoch || !ledger_epoch || !granted_amcs ||
        !exclusive_amc_owner || (granted_amcs & (UCHAR)~MOLOKAI_WIN_AMC_MASK) ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    ctx = o ? get_dev_ctx(dev) : NULL;
    if (!o || !ctx || ctx->is_control_dev || g_quarantined_rsc_owner ||
        inst->core.owner_epoch != owner_epoch || !ctx->rpmh_observed_wdf_interrupt ||
        ctx->rpmh_observed_pnp_generation != pnp_generation ||
        ctx->rpmh_identity_pnp_generation != pnp_generation ||
        o->d0_rsc.pnp_generation != pnp_generation || o->d0_gic.pnp_generation != pnp_generation ||
        !o->gic_irq37_owner.active || o->gic_irq37_owner.faulted ||
        o->gic_irq37_owner.epoch != owner_epoch ||
        o->gic_irq37_owner.wdf_interrupt != (void*)ctx->rpmh_observed_wdf_interrupt ||
        o->rpmh_irq_mask_ops.ctx != &o->gic_irq37_owner || !o->rpmh_irq_mask_ops.exclusive_owner ||
        !o->rpmh_irq_mask_ops.exclusive_owner(o->rpmh_irq_mask_ops.ctx, owner_epoch,
                                              (void*)ctx->rpmh_observed_wdf_interrupt) ||
        !exclusive_amc_owner(proof_ctx, owner_epoch, pnp_generation,
                             (void*)ctx->rpmh_observed_wdf_interrupt, granted_amcs) ||
        !oem_wdf_owner_publication_vacant_win((void*)ctx->rpmh_observed_wdf_interrupt) ||
        !o->d0_rsc.base || !o->d0_gic.base || o->rsc_completion.epoch != ledger_epoch ||
        o->rsc_completion.granted_mask != granted_amcs || !o->rsc_completion.irq_bound ||
        o->rpmh.external_grant_mask != granted_amcs || !o->rpmh.completion_handler_bound ||
        !o->rpmh.dpc_consumes_completion || o->deferred_scheduler.scheduler ||
        o->rpmh_wait.scheduler || o->rpmh_scheduler.execution_authorized ||
        o->rpmh_scheduler.busy_mask || o->rpmh_scheduler.quarantined_mask ||
        o->rpmh.deferred_active_mask || o->rpmh_irq_pipeline.bound)
        goto out;
    for (slot = 0u; slot < MOLOKAI_WIN_AMC_CNT; ++slot)
        if (o->rsc_completion.amc[slot].state != RSC_AMC_VACANT)
            goto out;
    /* a firmware/hardware completion arriving before publication is NOT an idle software
     * ledger. Neither discard its IRQ nor restore the owner from a stale read. */

    {
        int masked = -1;
        ULONG before = 0, after = 0;
        if (!o->rpmh_irq_mask_ops.read_masked ||
            !o->rpmh_irq_mask_ops.read_masked(o->rpmh_irq_mask_ops.ctx, &masked) || masked != 1 ||
            !o->rpmh.io.read32 || !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &before) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &after) || before != after ||
            (after & granted_amcs) || before == UINT32_MAX || after == UINT32_MAX) {
            g_quarantined_rsc_owner = o;
            goto out;
        }
    }
    /* the only MMIO writes in are the existing owned TCS restore operations. That core never
     * disables bits it did not enable and verifies every attempted physical cleanup. */

    debt = rpmh_tcs_restore(&o->rpmh);
    rsc_completion_revoke(&o->rsc_completion);
    if (debt) {
        g_quarantined_rsc_owner = o;
        result = STATUS_DEVICE_BUSY;
    } else
        result = STATUS_SUCCESS;
out:
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

/* the PEP0 D0 callback must first validate its PnP/IRQ37 generation. Mapping the controller does
 * not authorize submission. */

/* the *actual* physical GICD INTID37 mask provider for the existing WDF
 * EvtInterruptEnable/Disable mask owner. This operation never publishes RPMh rights. */

/* firmware-assigned HLOS DRV2 (not PEP0._CRS MMIO) combined with Windows-assigned
 * device-exclusive IRQ37, live controller geometry, stable physical readbacks, and the
 * pinned d0 generation. The lease is scoped to the one PEP0 WDF FDO and is invalidated at
 * verified retirement. */

static int os_rsc_lease_irq_proof(void* ctx_arg, uint64_t epoch, void* irq, uint64_t generation)
{
    owners_t* o = (owners_t*)ctx_arg;
    p_dev_ctx_t ctx;
    if (!o || InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) != 1 || !epoch || !irq ||
        !generation || o->os_rsc_lease.owner_epoch != epoch ||
        o->os_rsc_lease.pnp_generation != generation || o->os_rsc_lease.interrupt != irq ||
        !o->os_rsc_lease.dev || !o->d0_rsc.base || o->d0_rsc.pnp_generation != generation ||
        !o->d0_gic.base || o->d0_gic.pnp_generation != generation ||
        o->d0_rsc.bytes != RSC_DRV2_BYTES || o->mmio.rsc.base != o->d0_rsc.base ||
        o->mmio.rsc.bytes != RSC_DRV2_BYTES || o->mmio.rsc.physical != HLOS_RSC_DRV2_PHYS)
        return 0;
    ctx = get_dev_ctx(o->os_rsc_lease.dev);
    return ctx && !ctx->is_control_dev &&
           /* pnp census must be valid and have no conflicting range, but firmware DRV2 need not be
            * described as a PEP0 _CRS memory range. */

           ctx->rsc_drv2_assignment_observed == 1 &&
           ctx->rsc_drv2_assignment_status == STATUS_SUCCESS &&
           ctx->rpmh_observed_pnp_generation == generation &&
           ctx->rpmh_identity_pnp_generation == generation &&
           ctx->rpmh_observed_wdf_interrupt == (WDFINTERRUPT)irq;
}

static int os_rsc_lease_amc_proof(void* ctx, uint64_t epoch, uint64_t generation, void* irq,
                                  uint8_t slots)
{
    owners_t* o = (owners_t*)ctx;
    return o && slots == o->os_rsc_lease.granted_amcs && slots == MOLOKAI_WIN_AMC_MASK &&
           os_rsc_lease_irq_proof(ctx, epoch, irq, generation);
}

/* reconstruct the original OEM's firmware configured fixed RSC direct-map model, not an
 * invented _CRS memory lease. The exclusive IRQ37 PnP/WDF res, mapping address/size,
 * physical controller geometry, idle slots and d0 epoch are independently checked. */

/* read the actual RPMh src: a WDF callback and a GIC enable bit by themselves cannot
 * authorize it. This executes only from the already connected KMDF interrupt callback with
 * the PEP's exclusive live RSC lease and physical MMIO mapping. */

static int os_rsc_irq_src_quiescent(void* ctx, uint64_t epoch, void* irq)
{
    gic_irq37_owned_win_t* g = (gic_irq37_owned_win_t*)ctx;
    owners_t* o = g ? (owners_t*)g->proof_ctx : NULL;
    uint32_t pending_a = UINT32_MAX, pending_b = UINT32_MAX, slot;
    if (!g || !o || !g->active || g->faulted ||
        !os_rsc_lease_irq_proof(o, epoch, irq, g->pnp_generation) || !o->rpmh.io.read32 ||
        o->rpmh_scheduler.busy_mask || o->rpmh_scheduler.quarantined_mask ||
        o->rpmh_irq_pipeline.pending.active || o->rpmh_wait.active_req ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &pending_a) || pending_a != 0u)
        return 0;
    for (slot = 0; slot < MOLOKAI_WIN_AMC_CNT; ++slot) {
        uint32_t status_a = UINT32_MAX, status_b = UINT32_MAX;
        uint32_t ctrl_a = UINT32_MAX, ctrl_b = UINT32_MAX;
        uint32_t cmd_a = UINT32_MAX, cmd_b = UINT32_MAX;
        uint32_t wait_a = UINT32_MAX, wait_b = UINT32_MAX;
        if (!o->rpmh.io.read32(o->rpmh.io.ctx, TCS_STATUS(slot), &status_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_CONTROL(slot), &ctrl_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_ENABLE(slot), &cmd_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_WAIT(slot), &wait_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_STATUS(slot), &status_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_CONTROL(slot), &ctrl_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_ENABLE(slot), &cmd_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_WAIT(slot), &wait_b) || status_a != status_b ||
            ctrl_a != ctrl_b || cmd_a != cmd_b || wait_a != wait_b ||
            !(status_a & TCS_STATUS_IDLE) ||
            (ctrl_a & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || cmd_a || wait_a)
            return 0;
    }
    return o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &pending_b) && pending_b == 0u &&
           os_rsc_lease_irq_proof(o, epoch, irq, g->pnp_generation);
}

/* terminal IRQ37 owner proof, distinct from d0 shutdown's fully idle src test. The
 * synchronous waiter is intentionally still active here; releasing it before GIC
 * restoration would recreate 's race. */
static int os_rsc_irq_completion_drained(void* ctx, uint64_t epoch, void* irq)
{
    gic_irq37_owned_win_t* g = (gic_irq37_owned_win_t*)ctx;
    owners_t* o = g ? (owners_t*)g->proof_ctx : NULL;
    uint32_t a = UINT32_MAX, b = UINT32_MAX, slot;
    rpmh_irq_pipeline_t* p;
    if (!g || !o || !g->active || g->faulted || !epoch ||
        !os_rsc_lease_irq_proof(o, epoch, irq, g->pnp_generation))
        return 0;
    p = &o->rpmh_irq_pipeline;
    if (!p->bound || p->epoch != epoch || p->faulted || !p->unmask_finalizing.active ||
        p->unmask_finalizing.epoch != epoch || p->pending.active ||
        p->work.state != RPMH_WORK_COMPLETION_READY || !p->work.irq_masked ||
        !rpmh_irq_gate_outstanding(&p->gate) || o->rpmh_scheduler.busy_mask ||
        o->rpmh_scheduler.quarantined_mask || o->rpmh.deferred_active_mask || !o->rpmh.io.read32 ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &a) || a)
        return 0;
    for (slot = 0; slot < MOLOKAI_WIN_AMC_CNT; ++slot) {
        uint32_t status = UINT32_MAX, ctrl = UINT32_MAX, en = UINT32_MAX, wait = UINT32_MAX;
        uint32_t bit = 1u << slot;
        if (o->rsc_completion.amc[slot].state != RSC_AMC_VACANT || o->rpmh.deferred_req_id[slot] ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_STATUS(slot), &status) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_CONTROL(slot), &ctrl) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_ENABLE(slot), &en) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_WAIT(slot), &wait) ||
            !(status & TCS_STATUS_IDLE) ||
            (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) ||
            ((en || wait) && (!(o->rpmh.completed_amc_mask & bit) ||
                              en != o->rpmh.last_completed_command_mask[slot] ||
                              wait != o->rpmh.last_completed_dependency_mask[slot])))
            return 0;
    }
    return o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &b) && b == 0u &&
           os_rsc_lease_irq_proof(o, epoch, irq, g->pnp_generation);
}

NTSTATUS hw_backend_win_d0_acquire_firmware_configured_rpmh(_In_ WDFDEVICE dev,
                                                            _In_ ULONGLONG pnp_generation)
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    rpmh_pnp_resources_t sequence = {0};
    oem_irq_mask_ops_t ops = {0};
    NTSTATUS result = STATUS_NOT_SUPPORTED;
    uint64_t epoch = 0;
    ULONG ledger_epoch = 0;
    ULONG id_a = 0, id_b = 0, config_a = 0, config_b = 0, irq_a = 0, irq_b = 0;
    ULONG irq_enable_a = 0, irq_enable_b = 0;
    ULONG slot;
    if (!dev || !pnp_generation || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    ctx = o ? get_dev_ctx(dev) : NULL;
    /* stock PEP0 intentionally has no DRV2 MMIO desc. The observed PnP census is still
     * mandatory to reject malformed/conflicting MMIO allocations; absence of that desc is
     * normal behavior. */

    if (!o || !ctx || ctx->is_control_dev || ctx->rsc_drv2_assignment_observed != 1 ||
        ctx->rsc_drv2_assignment_status != STATUS_SUCCESS)
        goto unlock;
    result = STATUS_DEVICE_NOT_READY;
    if (g_quarantined_rsc_owner || g_quarantined_backend_instance || !inst->core.owner_epoch ||
        InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) != 0 ||
        ctx->rpmh_identity_pnp_generation != pnp_generation ||
        ctx->rpmh_observed_pnp_generation != pnp_generation || !ctx->rpmh_observed_wdf_interrupt ||
        !rpmh_oem_pnp_observation_read(&ctx->rpmh_oem_pnp_observation, pnp_generation, &sequence) ||
        !rpmh_has_exclusive_irq37_resource(&sequence, pnp_generation) || !o->d0_rsc.base ||
        o->d0_rsc.pnp_generation != pnp_generation || !o->d0_gic.base ||
        o->d0_gic.pnp_generation != pnp_generation || o->d0_rsc.bytes != RSC_DRV2_BYTES ||
        o->mmio.rsc.base != o->d0_rsc.base || o->mmio.rsc.bytes != RSC_DRV2_BYTES ||
        o->mmio.rsc.physical != HLOS_RSC_DRV2_PHYS || !ctx->rpmh_rsc_identity.identity_matched ||
        ctx->rpmh_rsc_identity.rsc_id != o->d0_rsc.id ||
        ctx->rpmh_rsc_identity.driver_config != o->d0_rsc.config ||
        ctx->rpmh_rsc_identity.tcs_cnt != MOLOKAI_WIN_TCS_TOTAL ||
        ctx->rpmh_rsc_identity.commands_per_tcs != TCS_BATCH_MAX ||
        !oem_wdf_owner_publication_vacant_win((void*)ctx->rpmh_observed_wdf_interrupt) ||
        !o->rpmh.io.read32 || !o->rpmh.io.write32)
        goto unlock;
    /* a build-specific marker at the exact native preflight entry lets KDNET distinguish this
     * executed src from a retained WDK object. Do not infer any hardware permission from this
     * diagnostic. */

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                "rpmh: DRV2 preflight generation=%llu pending=0 AMC=vacant",
                (unsigned long long)pnp_generation);

    if (!o->rpmh.io.read32(o->rpmh.io.ctx, RSC_ID_OFFSET, &id_a) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, RSC_PARENTCHILD_CONFIG_OFFSET, &config_a) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &irq_a) || id_a != o->d0_rsc.id ||
        config_a != o->d0_rsc.config || irq_a != 0u) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: identity preflight failed id=%08lX expected=%08lX config=%08lX "
                    "expected=%08lX pending=%08lX",
                    id_a, o->d0_rsc.id, config_a, o->d0_rsc.config, irq_a);
        goto unlock;
    }
    /* tcs_update_mode() preserves/updates TCS IRQ enable during HLOS initialization. A set
     * IRQ_ENABLE bit by itself is not a running AMC: accept it ONLY as a stable inherited
     * register baseline, after independently proving empty IRQ_STATUS, idle/unprogrammed AMC
     * slots, and the exclusive PnP IRQ37 owner. */

    if (!o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_enable_a) ||
        irq_enable_a == UINT32_MAX) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: IRQ enable read failed value=%08lX",
                    irq_enable_a);
        goto unlock;
    }
    for (slot = 0; slot < MOLOKAI_WIN_AMC_CNT; ++slot) {
        ULONG status_a = 0, status_b = 0, ctrl_a = 0, ctrl_b = 0;
        ULONG cmds_a = 0, cmds_b = 0, wait_a = 0, wait_b = 0;
        if (!o->rpmh.io.read32(o->rpmh.io.ctx, TCS_STATUS(slot), &status_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_CONTROL(slot), &ctrl_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_ENABLE(slot), &cmds_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_WAIT(slot), &wait_a) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_STATUS(slot), &status_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_CONTROL(slot), &ctrl_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_ENABLE(slot), &cmds_b) ||
            !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_WAIT(slot), &wait_b) || status_a != status_b ||
            ctrl_a != ctrl_b || cmds_a != cmds_b || wait_a != wait_b || status_a == UINT32_MAX ||
            ctrl_a == UINT32_MAX || !(status_a & TCS_STATUS_IDLE) ||
            (ctrl_a & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || cmds_a || wait_a) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: AMC=%lu occupied status=%08lX control=%08lX enable=%08lX wait=%08lX",
                        (ULONG)slot, status_a, ctrl_a, cmds_a, wait_a);
            goto unlock;
        }
    }
    if (!o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_ENABLE, &irq_enable_b) ||
        irq_enable_a != irq_enable_b || irq_enable_b == UINT32_MAX) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: IRQ enable changed during preflight before=%08lX after=%08lX",
                    irq_enable_a, irq_enable_b);
        goto unlock;
    }
    if (!o->rpmh.io.read32(o->rpmh.io.ctx, RSC_ID_OFFSET, &id_b) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, RSC_PARENTCHILD_CONFIG_OFFSET, &config_b) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &irq_b) || id_b != id_a ||
        config_b != config_a || irq_b != 0u)
        goto unlock;
    epoch = inst->core.owner_epoch;
    /* rsc ledger epochs are 32-bit and nonzero, distinct from the 64-bit backend identity. The
     * backend owner counter is monotonically allocated for this boot and is already unique
     * across new attaches. */

    ledger_epoch = (ULONG)(epoch & 0xFFFFFFFFu);
    /* a fresh d0 owner reuses its 64-bit backend epoch, but the AMC completion ledger
     * explicitly REJECTS an epoch <= its last owner. Allocate a strictly increasing 32-bit
     * ledger generation on every successful rebind. */

    if (ledger_epoch <= o->rsc_completion.epoch) {
        if (o->rsc_completion.epoch == UINT32_MAX)
            goto unlock;
        ledger_epoch = o->rsc_completion.epoch + 1u;
    }
    if (!ledger_epoch)
        goto unlock;
    o->os_rsc_lease.dev = dev;
    o->os_rsc_lease.interrupt = (void*)ctx->rpmh_observed_wdf_interrupt;
    o->os_rsc_lease.owner_epoch = epoch;
    o->os_rsc_lease.pnp_generation = pnp_generation;
    o->os_rsc_lease.granted_amcs = MOLOKAI_WIN_AMC_MASK;
    o->os_rsc_lease.pre_enabled_amc_irq_mask = (uint8_t)(irq_enable_b & MOLOKAI_WIN_AMC_MASK);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: IRQ baseline enable=%08lX inherited=%02X",
                irq_enable_b, (ULONG)o->os_rsc_lease.pre_enabled_amc_irq_mask);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: AMC baseline pending=0 slots=vacant");
    InterlockedExchange(&o->os_rsc_lease.active, 1);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    /* existing hardware-enforcing transactions perform all GICD/TCS writes. */

    result = hw_backend_win_prepare_gic_irq37_mask(dev, pnp_generation, epoch,
                                                   (void*)ctx->rpmh_observed_wdf_interrupt, o,
                                                   os_rsc_lease_irq_proof, &ops);
    if (!NT_SUCCESS(result)) {
        /* an exclusive-owner precondition can refuse the GIC acquisition BEFORE any GICD mask
         * write. The GIC provider leaves Active/faulted clear ONLY when it has not tried a
         * physical mask transition. */

        (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
        if (g_win_backend == inst && inst->io.owners == o && o->os_rsc_lease.dev == dev &&
            o->os_rsc_lease.owner_epoch == epoch &&
            o->os_rsc_lease.pnp_generation == pnp_generation &&
            InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) == 1 &&
            !o->gic_irq37_owner.active && !o->gic_irq37_owner.faulted && !o->rpmh.owned_amc_mask &&
            !o->rpmh.external_grant_mask && !o->rpmh.irq_enabled_by_us &&
            !o->rpmh.completion_handler_bound && !o->rpmh.dpc_consumes_completion &&
            !o->rsc_completion.irq_bound && !o->deferred_scheduler.scheduler &&
            !o->rpmh_irq_pipeline.bound) {
            InterlockedExchange(&o->os_rsc_lease.active, 0);
            RtlZeroMemory(&o->os_rsc_lease, sizeof(o->os_rsc_lease));
            (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: GIC admission rejected status=%08X dispatched=0 lease=retired",
                        result);
            return STATUS_DEVICE_NOT_READY;
        }
        (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_RPMH,
                    "rpmh: GIC acquisition uncertain status=%08X owner=retained", result);
    }
    if (NT_SUCCESS(result)) {
        result =
            hw_backend_win_prepare_granted_amcs(dev, pnp_generation, epoch, ledger_epoch,
                                                MOLOKAI_WIN_AMC_MASK, o, os_rsc_lease_amc_proof);
        if (!NT_SUCCESS(result))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: AMC prepare rejected status=%08X owner=retained", result);
        if (NT_SUCCESS(result)) {
            result = hw_backend_win_bind_deferred_completion(dev, epoch, ledger_epoch);
            if (!NT_SUCCESS(result))
                TraceEvents(
                    TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: deferred binding rejected status=%08X rollback=unpublished-owner",
                    result);
            if (NT_SUCCESS(result)) {
                TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RPMH,
                            "rpmh: DRV2 owner published epoch=%llu", (unsigned long long)epoch);
                return STATUS_SUCCESS;
            }
            /* the abort API refuses partially published WDF/worker state. Treat any failure as
             * uncertain, never issue a fabricated ACK. */

            if (!NT_SUCCESS(hw_backend_win_abort_unpublished_granted_amcs(
                    dev, pnp_generation, epoch, ledger_epoch, MOLOKAI_WIN_AMC_MASK, o,
                    os_rsc_lease_amc_proof)))
                result = STATUS_DEVICE_BUSY;
        }
    }
    /* retain the lease + MMIO mappings: a failed D0Entry must not blindly clear a hardware
     * owner. */

    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    if (g_win_backend == inst && inst->io.owners == o)
        g_quarantined_rsc_owner = o;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return STATUS_DEVICE_BUSY;
unlock:
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

NTSTATUS
hw_backend_win_prepare_gic_irq37_mask(WDFDEVICE dev, ULONGLONG pnp_generation,
                                      ULONGLONG owner_epoch, void* wdf_interrupt, void* proof_ctx,
                                      int (*exclusive_owner)(void*, uint64_t, void*, uint64_t),
                                      oem_irq_mask_ops_t* ops)
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    rpmh_pnp_resources_t sequence = {0};
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    if (!dev || !pnp_generation || !owner_epoch || !wdf_interrupt || !exclusive_owner || !ops ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    ctx = o ? get_dev_ctx(dev) : NULL;
    /* hardware mask writes cannot be justified by a unique vector that Windows might share
     * with another device. Require the *current* PnP IRQ37 assignment to be device-exclusive,
     * and the exact WDF handle and d0 RSC identity to match it. */

    if (o && ctx && !ctx->is_control_dev &&
        ctx->rpmh_observed_wdf_interrupt == (WDFINTERRUPT)wdf_interrupt &&
        ctx->rpmh_observed_pnp_generation == pnp_generation &&
        ctx->rpmh_identity_pnp_generation == pnp_generation &&
        rpmh_oem_pnp_observation_read(&ctx->rpmh_oem_pnp_observation, pnp_generation, &sequence) &&
        rpmh_has_exclusive_irq37_resource(&sequence, pnp_generation) &&
        inst->core.owner_epoch == owner_epoch && o->d0_rsc.base &&
        o->d0_rsc.pnp_generation == pnp_generation && o->d0_gic.base &&
        o->d0_gic.pnp_generation == pnp_generation && !o->gic_irq37_owner.active &&
        !o->gic_irq37_owner.faulted &&
        gic_irq37_acquire_masked_ops_win(&o->gic_irq37_owner, &o->d0_gic, pnp_generation,
                                         owner_epoch, wdf_interrupt, proof_ctx, exclusive_owner,
                                         ops)) {
        /* this independently validates the live RPMh src whenever the framework has already
         * applied the requested physical GIC edge. Its context is the GIC owner containing the
         * native lease proof. */

        ops->src_quiescent = os_rsc_irq_src_quiescent;
        ops->completion_drained = os_rsc_irq_completion_drained;
        /* keep the authenticated physical mask operations in the SAME backend lifetime as GICD,
         * the WDF interrupt and AMC scheduler. Borrowed caller output storage cannot be used for
         * d0 publish. */

        o->rpmh_irq_mask_ops = *ops;
        status = STATUS_SUCCESS;
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

NTSTATUS hw_backend_win_retire_gic_irq37_mask(WDFDEVICE dev, ULONGLONG owner_epoch,
                                              void* wdf_interrupt, int owner_drained)
{
    win_backend_instance_t* inst;
    owners_t* o;
    NTSTATUS status = STATUS_DEVICE_BUSY;
    if (!dev || !owner_epoch || !wdf_interrupt || !owner_drained ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    if (o && inst->core.owner_epoch == owner_epoch &&
        gic_irq37_retire_mask_ops_win(&o->gic_irq37_owner, owner_epoch, wdf_interrupt,
                                      owner_drained))
        status = STATUS_SUCCESS;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

NTSTATUS hw_backend_win_d0_map_rsc(_In_ WDFDEVICE dev, _In_ ULONGLONG pnp_generation,
                                   _In_ const rsc_identity_t* identity)
{
    win_backend_instance_t* inst;
    owners_t* o;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    p_dev_ctx_t ctx;
    if (!dev || !identity || !pnp_generation || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    ctx = get_dev_ctx(dev);
    if (!ctx || ctx->is_control_dev || !ctx->rpmh_observed_wdf_interrupt ||
        ctx->rpmh_observed_pnp_generation != pnp_generation ||
        ctx->rpmh_identity_pnp_generation != pnp_generation ||
        ctx->rpmh_oem_pnp_observation.generation != pnp_generation ||
        !ctx->rpmh_oem_pnp_observation.valid || !ctx->rsc_irq_snapshot.unique_candidate ||
        ctx->rpmh_wdf_lifecycle.phase != RPMH_WDF_D0_OBSERVED ||
        identity != &ctx->rpmh_rsc_identity || !ctx->rpmh_rsc_identity.identity_matched)
        return STATUS_DEVICE_NOT_READY;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    if (o && !o->mmio.rsc.base && !o->d0_rsc.base && rsc_d0_mapping_can_close(o) &&
        rsc_d0_map_open_win(&o->d0_rsc, pnp_generation, identity)) {
        o->mmio.rsc.base = o->d0_rsc.base;
        o->mmio.rsc.bytes = o->d0_rsc.bytes;
        o->mmio.rsc.physical = HLOS_RSC_DRV2_PHYS;
        /* PrepareHardware had no RSC mapping. Observe identity and idle state in D0 without
         * changing grants. */

        {
            ULONG first = 0, second = 0;
            o->probe &= ~PROBE_RSC_ID;
            if (mmio_read32(&o->mmio.rsc, TCS_STATUS(OWNER_TCS), &first) &&
                mmio_read32(&o->mmio.rsc, TCS_STATUS(OWNER_TCS), &second) && first == second &&
                (first & TCS_STATUS_IDLE)) {
                o->rsc_id = o->d0_rsc.id;
                o->drv_config = o->d0_rsc.config;
                o->probe |= PROBE_RSC_ID;
            }
        }

        if ((o->probe & PROBE_RSC_ID) && gic_d0_observe_win(&o->d0_gic, pnp_generation)) {
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                        "rpmh: IRQ37 GICD typer=%08lX id=%08lX enabled=%u observation-only=1",
                        (ULONG)o->d0_gic.typer, (ULONG)o->d0_gic.iidr,
                        (unsigned)o->d0_gic.irq37_enabled);
            status = STATUS_SUCCESS;
        } else {
            /* rollback a partial mapping only while RSC is idle; never report a partially prepared
             * transport as successful. */

            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: controller preflight unavailable transport=rollback");
            status = STATUS_DEVICE_NOT_READY;
            if (!rsc_d0_mapping_can_close(o) ||
                !rsc_d0_map_close_win(&o->d0_rsc, pnp_generation, 1)) {
                g_quarantined_rsc_owner = o;
                status = STATUS_DEVICE_BUSY;
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                            "rpmh: transport rollback unverified mapping=retained");
            } else {
                RtlZeroMemory(&o->mmio.rsc, sizeof(o->mmio.rsc));
                o->rsc_id = 0;
                o->drv_config = 0;
                o->probe &= ~PROBE_RSC_ID;
            }
        }
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

/* verify after KMDF enables this IRQ37 generation and all physical providers are
 * installed. PrepareHardware has neither d0 maps nor completion proof. */

NTSTATUS hw_backend_win_d0_revalidate(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    ULONGLONG generation;
    rpmh_pnp_resources_t exclusive_resources = {0};
    NTSTATUS result = STATUS_DEVICE_NOT_READY;
    int reg;
    int d0_admission_closed = 0;
    int probe_ready = 0;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    ctx = inst && inst->io.dev == dev ? get_dev_ctx(dev) : NULL;
    o = inst ? inst->io.owners : NULL;
    generation = ctx ? ctx->rpmh_identity_pnp_generation : 0;
    /* revoke graph admission BEFORE reading any new owner/interrupt/mapping state, including
     * the fast-failure path. No stale PoFx lease may execute against hardware after a new d0
     * check has rejected its provenance. */

    if (!inst || inst->io.dev != dev || !inst->core.registered ||
        !hw_backend_quiesce_d0(inst->core.owner_epoch))
        goto out;
    d0_admission_closed = 1;
    InterlockedExchange(&inst->io.pmic_gpio_provider_live, 0);
    if (o) {
        /* a callback that queued before d0 revalidation must not continue a physical vote while
         * the new generation is examined. */

        if (!owners_lock(o))
            goto out;
        o->rpmh_live = FALSE;
        o->clocks_live = FALSE;
        o->tlmm_live = FALSE;
        o->probe = 0u;
        owners_unlock(o);
    }
    /* a WDF handle and mapped MMIO are necessary, never sufficient. The authentic owner must
     * have armed all the already-existing hardware gates; checking these fields does not
     * itself grant controller access. */

    if (!ctx || ctx->is_control_dev || !inst->core.registered || !o || !generation ||
        !rpmh_oem_pnp_observation_read(&ctx->rpmh_oem_pnp_observation, generation,
                                       &exclusive_resources) ||
        !rpmh_has_exclusive_irq37_resource(&exclusive_resources, generation) ||
        generation != ctx->rpmh_observed_pnp_generation ||
        ctx->rpmh_oem_pnp_observation.generation != generation ||
        !ctx->rpmh_observed_wdf_interrupt ||
        /* the separate direct GCC/TLMM d0 mapper must only run under the already-published and
         * exact native DRV2/IRQ37 lease. */

        InterlockedCompareExchange(&o->os_rsc_lease.active, 0, 0) != 1 ||
        o->os_rsc_lease.dev != dev || o->os_rsc_lease.pnp_generation != generation ||
        o->os_rsc_lease.owner_epoch != inst->core.owner_epoch ||
        o->os_rsc_lease.interrupt != (void*)ctx->rpmh_observed_wdf_interrupt ||
        o->os_rsc_lease.granted_amcs != MOLOKAI_WIN_AMC_MASK || !o->d0_rsc.base ||
        o->d0_rsc.pnp_generation != generation || !o->d0_gic.base ||
        o->d0_gic.pnp_generation != generation || o->mmio.rsc.base != o->d0_rsc.base ||
        !oem_wdf_d0_owner_ready_win((void*)ctx->rpmh_observed_wdf_interrupt,
                                    inst->core.owner_epoch) ||
        /* a d0 owner cannot borrow a stack/local pipeline from another module: the physical MMIO
         * and KMDF callbacks must reference the exact nonpaged structures pinned by THIS backend
         * lifetime. */

        !oem_wdf_d0_owner_matches_win((void*)ctx->rpmh_observed_wdf_interrupt,
                                      inst->core.owner_epoch, &o->rpmh_irq_bridge,
                                      &o->rpmh_dpc_tracker, &o->rpmh_irq_mask_owner) ||
        /* the attached ISR and worker must consume the actual, retained RSC IRQ src and ACK ledger
         * for THIS backend, not foreign contexts or a fabricated always-pending callback. */

        o->rpmh_irq_src.dev != &o->rpmh || o->rpmh_irq_src.ledger != &o->rsc_completion ||
        o->rpmh_irq_src.owner_epoch != inst->core.owner_epoch ||
        o->rpmh_irq_src.ledger_epoch != o->rsc_completion.epoch ||
        o->rpmh_irq_bridge.serialize_ctx != &o->rpmh_irq_src ||
        o->rpmh_irq_bridge.read_owned_completion_pending != rpmh_tcs_read_owned_irq_status ||
        o->rpmh_amc_drain.binding != &o->deferred_scheduler ||
        o->rpmh_amc_drain.irq_src != &o->rpmh_irq_src ||
        o->rpmh_amc_drain.owner_epoch != inst->core.owner_epoch ||
        o->rpmh_amc_drain.epoch != o->rsc_completion.epoch || o->rpmh_owner_work_ops.ctx != o ||
        o->rpmh_irq_bridge.enter != rpmh_owner_src_enter ||
        o->rpmh_irq_bridge.leave_cb != rpmh_owner_src_leave ||
        o->rpmh_irq_bridge.passive_drain != 1u || o->rpmh_irq_pipeline.work.ops.ctx != o ||
        o->rpmh_irq_pipeline.work.ops.mask_owned_interrupt != rpmh_owner_mask_amc_irq ||
        o->rpmh_irq_pipeline.work.ops.unmask_owned_interrupt != rpmh_owner_unmask_amc_irq ||
        o->rpmh_irq_pipeline.work.ops.queue_dpc != rpmh_owner_queue_amc_dpc ||
        o->rpmh_irq_pipeline.work.ops.queue_pep_work != rpmh_owner_queue_amc_work ||
        o->rpmh_irq_pipeline.work.ops.has_finished_amc != rpmh_owner_has_finished_amc ||
        o->rpmh_irq_pipeline.work.ops.finish_one_amc != rpmh_owner_finish_one_amc ||
        o->rpmh_irq_pipeline.work.ops.enter_completion_lock != rpmh_owner_enter_completion_lock ||
        o->rpmh_irq_pipeline.work.ops.leave_completion_lock != rpmh_owner_leave_completion_lock ||
        o->rpmh_irq_bridge.pipeline != &o->rpmh_irq_pipeline || !o->rpmh_irq_pipeline.bound ||
        o->rpmh_irq_pipeline.faulted || o->rpmh_irq_pipeline.epoch != inst->core.owner_epoch ||
        !o->gic_irq37_owner.active || o->gic_irq37_owner.faulted ||
        o->gic_irq37_owner.wdf_interrupt != (void*)ctx->rpmh_observed_wdf_interrupt ||
        o->gic_irq37_owner.epoch != inst->core.owner_epoch || !o->rpmh.completion_handler_bound ||
        !o->rpmh.dpc_consumes_completion || !o->rpmh.external_grant_mask ||
        !o->rpmh.owned_amc_mask || !o->rsc_completion.irq_bound ||
        !o->rsc_completion.granted_mask || !o->deferred_scheduler.scheduler ||
        !o->rpmh_wait.scheduler || !o->rpmh.io.enter_irq_lock || !o->rpmh.io.leave_irq_lock ||
        o->rpmh.io.irq_lock_ctx != (void*)ctx->rpmh_observed_wdf_interrupt)
        goto out;

    /* D0 re-entry resets probe evidence, rechecks the command database and hardware, and requires
     * PROBE_ALL before admission. */

    /* serialize this with every native callback that also holds o->lock; otherwise
     * probe/resume and rail votes race. release before backend validation:
     * self_test/probe_readback enter those same native clock callbacks and must NOT
     * recursively take o->lock. */

    if (!owners_lock(o))
        goto out;
    /* clock_InitBases / TLMM_DeviceInit independently map peripheral registers after d0
     * hardware ownership has been established. These reads do not authorize a write:
     * owners_probe still requires physical GPLL0, clock, pin and deferred RPMh attestations. */

    if (!(o->mmio.gcc.base && o->mmio.tlmm.base) &&
        !NT_SUCCESS(mmio_d0_map_peripherals(&o->mmio))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: GCC/TLMM mapping or identity failed admission=closed");
        owners_unlock(o);
        goto out;
    }
    owners_probe(o);
    /* the native readiness callback acquires o->lock. ALREADY own that lock here while
     * probing; inspect the underlying scheduler contract directly to avoid a recursive
     * FAST_MUTEX acquisition. */

    probe_ready = (o->probe == PROBE_ALL && o->rpmh_live && !o->rpmh_write_fence.quarantined &&
                   rpmh_write_fence_live(&o->rpmh_write_fence, o->rpmh_scheduler.owner_epoch) &&
                   rpmh_deferred_runtime_ready(&o->deferred_scheduler, &o->rpmh_wait));
    owners_unlock(o);
    if (!probe_ready) {

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: revalidation failed probes=%02lX required=%02lX rpmh=%u gcc=%u "
                    "tlmm=%u wait-fault=%u scheduler-fault=%02X work-fault=%u",
                    (ULONG)o->probe, (ULONG)PROBE_ALL, (unsigned)o->rpmh_live,
                    (unsigned)o->clocks_live, (unsigned)o->tlmm_live,
                    (unsigned)o->rpmh_wait.faulted, (unsigned)o->rpmh_scheduler.quarantined_mask,
                    (unsigned)o->rpmh_irq_pipeline.faulted);
        goto out;
    }
    if (registry_query_u_long(L"MolokaiOwnersAttest", 1u) != 1u) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: probes verified attestation=disabled admission=closed");
        goto out;
    }
    /* promote an initially unverified PrepareHardware table only ONCE, after actual d0
     * hardware verification, to preserve the serial used by published NPA clients. A retained
     * verified table on resume is revalidated in place. */

    if (inst->core.table.production_attestation != HW_ATTEST_MAGIC) {
        inst->core.table.production_attestation = HW_ATTEST_MAGIC;
        reg = win_backend_reg(&inst->core);
        if (reg != 0) {
            inst->core.table.production_attestation = 0;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: backend registration rejected result=%d", reg);
            goto out;
        }
    }
    if (hw_backend_validate(inst->core.owner_epoch)) {
        /* d0 owner has been proven. */

        if (win_io_pmic_gpio_probe_provider(&inst->io))
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                        "pmic: GPIO provider available after D0");
        else
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "pmic: GPIO provider unavailable after D0");
        trace_report(&inst->core);
        result = STATUS_SUCCESS;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "resources: D0 revalidation complete admission=open");
    }
out:
    /* a later attestation/reg/readback failure must revoke ALL native delegates, not only the
     * PoFx graph lease. */

    if (!NT_SUCCESS(result) && d0_admission_closed && o && inst && inst->io.dev == dev &&
        owners_lock(o)) {
        o->rpmh_live = FALSE;
        o->clocks_live = FALSE;
        o->tlmm_live = FALSE;
        o->probe = 0u;
        owners_unlock(o);
    }
    if (!NT_SUCCESS(result))
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                    "resources: D0 owner or hardware probes unavailable admission=closed");
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

/* an unavailable hardware owner is never 'cleaned up' by clearing a pointer; failed
 * rundown retains the entire mapping. */

/* first close the backend's PoFx leases so future device power callbacks cannot borrow
 * RSC/GICD while PnP tears them down. */

/* native completion must be idle BEFORE KMDF invokes EvtInterruptDisable. Once that
 * callback returns, the PASSIVE WDF interrupt lock cannot be reacquired by the AMC worker. */

static BOOLEAN native_pre_disable_amcs_idle(owners_t* o, WDFDEVICE dev, uint64_t epoch)
{
    p_dev_ctx_t ctx;
    void* irq;
    BOOLEAN idle = FALSE;
    ULONG i;
    uint32_t status_a = UINT32_MAX, status_b = UINT32_MAX;
    if (!o || !dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;

    if (!o->gic_irq37_owner.active)
        return !o->rpmh_irq_pipeline.bound && !o->deferred_scheduler.scheduler &&
               !o->rpmh.external_grant_mask && !o->rpmh_irq_src.owner_epoch &&
               !o->rpmh_irq_src.irq_mask_active && !o->rpmh_irq_src.irq_mask_faulted &&
               !o->rpmh_irq_src.masked_amcs && !o->rpmh_irq_pipeline.pending.active &&
               !o->rpmh_irq_pipeline.unmask_finalizing.active;

    ctx = get_dev_ctx(dev);
    irq = ctx ? (void*)ctx->rpmh_observed_wdf_interrupt : NULL;
    if (!epoch || !irq || ctx->is_control_dev || o->gic_irq37_owner.faulted ||
        o->gic_irq37_owner.epoch != epoch || o->gic_irq37_owner.wdf_interrupt != irq ||
        !o->rpmh_irq_pipeline.bound || o->rpmh_irq_pipeline.epoch != epoch ||
        !oem_wdf_d0_owner_ready_win(irq, epoch))
        return FALSE;
    /* honor OEM lock order WDF passive ISR -> RPMh KMUTEX. The reverse order caused repeated
     * shutdown refusal under ISR contention. WdfInterruptTryToAcquireLock is nonblocking; if
     * occupied, do not release the controller owner or manufacture an idle snapshot. */
    if (!oem_wdf_enter_tcs_irq_lock_win(irq))
        return FALSE;
    if (KeWaitForSingleObject(&o->rpmh_scheduler_lock, Executive, KernelMode, FALSE, NULL) !=
        STATUS_SUCCESS) {
        oem_wdf_leave_tcs_irq_lock_win(irq);
        return FALSE;
    }
    idle = !o->rpmh_scheduler.busy_mask && !o->rpmh_scheduler.quarantined_mask &&
           !o->rpmh_wait.active_req && !o->rpmh_wait.faulted && !o->rpmh.deferred_active_mask &&
           !o->rpmh.completed_amc_mask && !o->rpmh_irq_pipeline.faulted &&
           !o->rpmh_irq_pipeline.pending.active && !o->rpmh_irq_pipeline.unmask_finalizing.active &&
           o->rpmh_irq_pipeline.work.state == RPMH_WORK_READY &&
           !o->rpmh_irq_pipeline.work.irq_masked && !o->rpmh_irq_src.irq_mask_active &&
           !o->rpmh_irq_src.irq_mask_faulted && !o->rpmh_irq_src.masked_amcs &&
           !o->rpmh_irq_pipeline.work.completion_lock_held &&
           !o->rpmh_irq_pipeline.work.defer_unmask &&
           !rpmh_irq_gate_outstanding(&o->rpmh_irq_pipeline.gate) &&
           o->rpmh_amc_drain.selected == 0xFFu;
    for (i = 0; idle && i < RPMH_JOBS; ++i)
        if (o->rpmh_scheduler.jobs[i].state != RPMH_JOB_FREE || o->rpmh_scheduler.jobs[i].id)
            idle = FALSE;
    for (i = 0; idle && i < MOLOKAI_WIN_AMC_CNT; ++i)
        if (o->rsc_completion.amc[i].state != RSC_AMC_VACANT || o->rsc_completion.amc[i].req_id ||
            o->rsc_completion.amc[i].owner_epoch || o->rpmh_amc_drain.latched[i].valid)
            idle = FALSE;
    /* both WDF passive ISR lock and RPMh scheduler mutex remain held through the
     * software/physical idle proof. The WDF lock was acquired FIRST to match the ISR's WDF ->
     * RPMh ordering. An interrupt cannot interleave between the snapshot, GIC mask commit, and
     * the admission pause. */

    if (!idle) {
        (VOID) KeReleaseMutex(&o->rpmh_scheduler_lock, FALSE);
        oem_wdf_leave_tcs_irq_lock_win(irq);
        return FALSE;
    }
    /* these atomic WDF counters and pending flags are checked INSIDE both domains, after the
     * interrupt-lock acquisition has serialized with any ISR that was already active during
     * the scheduler snapshot. */

    idle = oem_wdf_completion_idle_win(irq) &&
           InterlockedCompareExchange(&o->rpmh_dpc_tracker.phase, 0, 0) == 1 &&
           !InterlockedCompareExchange(&o->rpmh_dpc_tracker.outstanding, 0, 0) &&
           !InterlockedCompareExchange(&o->rpmh_dpc_tracker.queuing, 0, 0) &&
           !InterlockedCompareExchange(&o->rpmh_dpc_tracker.running, 0, 0) &&
           InterlockedCompareExchange(&o->rpmh_dpc_tracker.accepted, 0, 0) ==
               InterlockedCompareExchange(&o->rpmh_dpc_tracker.finished, 0, 0) &&
           !o->rpmh_irq_pipeline.pending.active && !o->rpmh_irq_pipeline.unmask_finalizing.active &&
           !rpmh_irq_gate_outstanding(&o->rpmh_irq_pipeline.gate);
    if (idle) {
        idle = o->rpmh.io.read32 && o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &status_a) &&
               o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &status_b) && status_a == 0u &&
               status_b == 0u;
    }
    /* mask GIC INTID37 while BOTH the KMDF passive ISR lock and native RPMh mutex are held. Do
     * not infer exclusive ownership from an idle register. */

    if (idle) {
        idle = o->rpmh_irq_mask_owner.interrupt == irq && o->rpmh_irq_mask_owner.epoch == epoch &&
               o->rpmh_irq_mask_ops.ctx == &o->gic_irq37_owner &&
               oem_irq_mask_transition(&o->rpmh_irq_mask_owner, irq, epoch, 1);
    }
    /* write the GIC only after an independently verified state change is required. If a completion
     * entered before masking, retain the masked line and reject PnP progression. */

    if (idle) {
        status_a = UINT32_MAX;
        status_b = UINT32_MAX;
        idle = oem_wdf_completion_idle_win(irq) &&
               !InterlockedCompareExchange(&o->rpmh_dpc_tracker.outstanding, 0, 0) &&
               !InterlockedCompareExchange(&o->rpmh_dpc_tracker.queuing, 0, 0) &&
               !InterlockedCompareExchange(&o->rpmh_dpc_tracker.running, 0, 0) &&
               InterlockedCompareExchange(&o->rpmh_dpc_tracker.accepted, 0, 0) ==
                   InterlockedCompareExchange(&o->rpmh_dpc_tracker.finished, 0, 0) &&
               !o->rpmh_irq_pipeline.pending.active &&
               !o->rpmh_irq_pipeline.unmask_finalizing.active &&
               !rpmh_irq_gate_outstanding(&o->rpmh_irq_pipeline.gate) && o->rpmh.io.read32 &&
               o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &status_a) &&
               o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &status_b) && status_a == 0u &&
               status_b == 0u && oem_irq_mask_read_proof(&o->rpmh_irq_mask_owner, irq, epoch, 1);
    }
    /* close live ACTIVE admissions at the exact pre-disable boundary, while both interrupt and
     * command locks are held, AFTER physical GIC masking and zero-pending evidence. No
     * in-flight ticket is dropped. */
    if (idle && o->deferred_scheduler.scheduler == &o->rpmh_scheduler) {
        idle = rpmh_scheduler_pause_deferred(&o->rpmh_scheduler, epoch) != 0;
        if (idle)
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                        "rpmh: queue paused IRQ37=masked epoch=%llu", (unsigned long long)epoch);
    }
    (VOID) KeReleaseMutex(&o->rpmh_scheduler_lock, FALSE);
    oem_wdf_leave_tcs_irq_lock_win(irq);
    return idle;
}

NTSTATUS hw_backend_win_d0_quiesce(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst;
    NTSTATUS status = STATUS_SUCCESS;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!NT_SUCCESS(pep_native_perf_quiesce_gate()))
        return STATUS_DEVICE_BUSY;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    if (inst && inst->io.dev == dev && inst->core.registered) {
        /* group clients need a live transport for final readback. drain graph admissions, then
         * retire those clients before revoking provider leases. */
        if (inst->io.owners && inst->io.owners->ufs_group_bridge &&
            (!g_published_graph || !graph_begin_drain(g_published_graph) ||
             !native_ufs_group_close(inst->io.owners))) {
            g_quarantined_rsc_owner = inst->io.owners;
            (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
            return STATUS_DEVICE_BUSY;
        }
        if (!hw_backend_quiesce_d0(inst->core.owner_epoch)) {
            status = STATUS_DEVICE_BUSY;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: callback rundown busy controller=retained");
        } else if (inst->io.owners) {
            owners_t* o = inst->io.owners;
            /* last-client completion requires live RPMh/GCC and CX/MX transport. Retire before
             * revoking any of the physical owner gates. */
            if (ufs_native_retains_graph(&o->ufs_native) &&
                (!g_published_graph || o->ufs_native.graph != g_published_graph ||
                 !graph_begin_drain(g_published_graph) || !native_ufs_group_close(o) ||
                 !native_ufs_scalar_program_clients_close(o, o->os_rsc_lease.owner_epoch) ||
                 ufs_native_retire_verified(&o->ufs_native, &o->ufs_domain_owner,
                                            &o->ufs_rail_owner) != FS_OK ||
                 !native_ufs_arcs_retire(o))) {
                g_quarantined_rsc_owner = o;
                status = STATUS_DEVICE_BUSY;
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                            "fs: shutdown readback unverified epoch=%llX owner=retained",
                            (unsigned long long)o->os_rsc_lease.owner_epoch);
            } else if (!owners_lock(o)) {
                status = STATUS_DEVICE_BUSY;
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                            "rpmh: shutdown owner lock unavailable mappings=retained");
            } else {
                if (ufs_native_retains_graph(&o->ufs_native) || o->ufs_scalar_required ||
                    o->ufs_scalar_catalog || o->ufs_scalar_owner || o->ufs_group_bridge ||
                    o->ufs_group_votes[0] || o->ufs_group_votes[1] || o->ufs_ice_config.saved ||
                    o->ufs_rail_owner.held || o->ufs_owner_uncertain) {
                    g_quarantined_rsc_owner = o;
                    status = STATUS_DEVICE_BUSY;
                    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                                "fs: quiesce blocked epoch=%llX client=retained",
                                (unsigned long long)o->os_rsc_lease.owner_epoch);
                } else if (!native_pre_disable_amcs_idle(o, dev, inst->core.owner_epoch)) {
                    status = STATUS_DEVICE_BUSY;
                    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                "rpmh: shutdown completion pending IRQ-disable=rejected");
                } else {
                    o->rpmh_live = FALSE;
                    o->clocks_live = FALSE;
                    o->tlmm_live = FALSE;
                    o->probe = 0u;
                    InterlockedExchange(&inst->io.pmic_gpio_provider_live, 0);
                }
                owners_unlock(o);
            }
        }
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

/* D0Exit retires the published WDF/AMC completion chain established and verified during D0. */

NTSTATUS hw_backend_win_d0_retire_published_owner(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst;
    owners_t* o;
    p_dev_ctx_t ctx;
    ULONGLONG epoch;
    ULONG ledger_epoch;
    void* irq;
    ULONG before = 0, after = 0, slot;
    int masked = -1;
    NTSTATUS result = STATUS_DEVICE_BUSY;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    ctx = o ? get_dev_ctx(dev) : NULL;
    TraceEvents(
        TRACE_LEVEL_WARNING, TRACE_RPMH,
        "rpmh: retirement preflight backend=%u owner=%u GIC-active=%u GIC-fault=%u quarantine=%u "
        "IRQ=%p pipeline=%u deferred=%u",
        (unsigned)(inst != NULL), (unsigned)(o != NULL), (unsigned)(o && o->gic_irq37_owner.active),
        (unsigned)(o && o->gic_irq37_owner.faulted), (unsigned)(g_quarantined_rsc_owner != NULL),
        ctx ? (PVOID)ctx->rpmh_observed_wdf_interrupt : NULL,
        (unsigned)(o && o->rpmh_irq_pipeline.bound),
        (unsigned)(o && o->deferred_scheduler.scheduler != NULL));
    /* normal pre-activation boots have no native controller owner. */

    if (!o || !o->gic_irq37_owner.active) {
        if (o && (g_quarantined_rsc_owner == o || o->deferred_scheduler.scheduler ||
                  o->rpmh.external_grant_mask || o->rpmh_irq_pipeline.bound ||
                  o->rpmh_irq_src.owner_epoch))
            g_quarantined_rsc_owner = o;
        else
            result = STATUS_SUCCESS;
        goto unlock_first;
    }
    if (!ctx || ctx->is_control_dev || g_quarantined_rsc_owner ||
        !ctx->rpmh_observed_wdf_interrupt ||
        o->gic_irq37_owner.wdf_interrupt != (void*)ctx->rpmh_observed_wdf_interrupt ||
        o->gic_irq37_owner.epoch != inst->core.owner_epoch || !o->deferred_scheduler.scheduler ||
        o->deferred_scheduler.epoch != inst->core.owner_epoch ||
        o->deferred_scheduler.ledger_epoch != o->rsc_completion.epoch ||
        !o->rpmh_irq_pipeline.bound || o->rpmh_irq_pipeline.epoch != inst->core.owner_epoch ||
        !o->d0_rsc.base || !o->d0_gic.base) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: retirement rejected owner-epoch=%llu GIC-epoch=%llu active=%u "
                    "scheduler=%u pipeline=%u RSC=%u GIC=%u",
                    (unsigned long long)(inst ? inst->core.owner_epoch : 0),
                    (unsigned long long)(o ? o->gic_irq37_owner.epoch : 0),
                    (unsigned)(o && o->gic_irq37_owner.active),
                    (unsigned)(o && o->deferred_scheduler.scheduler != NULL),
                    (unsigned)(o && o->rpmh_irq_pipeline.bound),
                    (unsigned)(o && o->d0_rsc.base != NULL),
                    (unsigned)(o && o->d0_gic.base != NULL));
        goto unlock_first;
    }
    irq = (void*)ctx->rpmh_observed_wdf_interrupt;
    epoch = inst->core.owner_epoch;
    ledger_epoch = o->deferred_scheduler.ledger_epoch;
    /* log the exact mask owner/readback state that prevents safe retirement. */

    {
        int stopped = oem_wdf_published_owner_stopped_win(irq, epoch);
        int physical_read = o->rpmh_irq_mask_ops.read_masked &&
                            o->rpmh_irq_mask_ops.read_masked(o->rpmh_irq_mask_ops.ctx, &masked);
        if (!stopped || !physical_read || masked != 1) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: retirement WDF-stopped=%d GIC-read=%d masked=%d phase=%lu fault=%d "
                        "DPC-phase=%ld mappings=retained",
                        stopped, physical_read, masked, (ULONG)o->rpmh_irq_mask_owner.phase,
                        (int)o->gic_irq37_owner.faulted,
                        InterlockedCompareExchange(&o->rpmh_dpc_tracker.phase, 0, 0));
            goto unlock_first;
        }
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);

    /* a WDF ISR cannot enqueue after framework disable. */

    if (!oem_wdf_dpc_close_win(&o->rpmh_dpc_tracker, irq, epoch) && o->rpmh_dpc_tracker.phase != 2)
        goto fail_unlocked;
    if (!oem_wdf_dpc_flush_proof_win(&o->rpmh_dpc_tracker, irq, epoch) ||
        !oem_wdf_drain_published_work_win(irq, epoch))
        goto fail_unlocked;

    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    if (!inst || inst->io.dev != dev || inst->io.owners != o || inst->core.owner_epoch != epoch ||
        g_quarantined_rsc_owner || !oem_wdf_published_owner_stopped_win(irq, epoch) ||
        !o->rpmh.io.read32 || !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &before) ||
        !o->rpmh.io.read32(o->rpmh.io.ctx, TCS_IRQ_STATUS, &after) || before != after ||
        before == UINT32_MAX || (after & o->rpmh.external_grant_mask) ||
        o->rpmh_write_fence.quarantined || !o->rpmh_scheduler.admission_paused ||
        !rpmh_scheduler_deferred_drained(&o->rpmh_scheduler, epoch) ||
        o->rpmh_scheduler.busy_mask || o->rpmh_scheduler.quarantined_mask ||
        o->rpmh_wait.active_req || o->rpmh_wait.faulted || o->rpmh.deferred_active_mask ||
        o->rpmh.completed_amc_mask || o->rpmh_irq_src.irq_mask_active ||
        o->rpmh_irq_src.irq_mask_faulted || o->rpmh_irq_src.masked_amcs ||
        o->rpmh_irq_pipeline.pending.active || o->rpmh_irq_pipeline.pending.epoch ||
        o->rpmh_irq_pipeline.unmask_finalizing.active ||
        o->rpmh_irq_pipeline.unmask_finalizing.epoch || o->rpmh_irq_pipeline.faulted ||
        o->rpmh_amc_drain.selected != 0xFFu)
        goto fail_locked;
    for (slot = 0u; slot < RPMH_JOBS; ++slot)
        if (o->rpmh_scheduler.jobs[slot].state != RPMH_JOB_FREE || o->rpmh_scheduler.jobs[slot].id)
            goto fail_locked;
    for (slot = 0u; slot < MOLOKAI_WIN_AMC_CNT; ++slot)
        if (o->rsc_completion.amc[slot].state != RSC_AMC_VACANT ||
            o->rsc_completion.amc[slot].req_id || o->rsc_completion.amc[slot].owner_epoch ||
            o->rpmh_amc_drain.latched[slot].valid)
            goto fail_locked;

    /* after exact WDF+DPC+worker drain, close the pipeline's admission gate and release its
     * lifetime tickets. failure pins BOTH physical maps. */

    if (!rpmh_pipeline_close(&o->rpmh_irq_pipeline, epoch) ||
        !rpmh_pipeline_release(&o->rpmh_irq_pipeline, epoch))
        goto fail_locked;
    oem_wdf_disable_bridge_win(irq);
    rpmh_scheduler_revoke(&o->rpmh_scheduler);
    if (rpmh_tcs_restore(&o->rpmh) != 0u)
        goto fail_locked;
    rsc_completion_revoke(&o->rsc_completion);

    if (!rpmh_deferred_scheduler_retire_owner(&o->deferred_scheduler, &o->rpmh_wait, epoch,
                                              ledger_epoch))
        goto fail_locked;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);

    /* do not hold lifecycle locks across WdfWorkItemFlush. Keep the GIC owner active through WDF
     * detach and verified physical release. */

    if (!oem_wdf_finalize_published_owner_win(irq, epoch) ||
        !NT_SUCCESS(hw_backend_win_retire_gic_irq37_mask(dev, epoch, irq, 1)))
        goto fail_unlocked;

    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    if (!g_win_backend || g_win_backend != inst || inst->io.dev != dev || inst->io.owners != o ||
        o->gic_irq37_owner.active || !oem_wdf_owner_publication_vacant_win(irq))
        goto fail_locked;
    /* wdf worker+DPC flush, GIC masked readback and pipeline release prove that none may be
     * borrowed. */

    RtlZeroMemory(&o->rpmh_dpc_tracker, sizeof(o->rpmh_dpc_tracker));
    RtlZeroMemory(&o->rpmh_irq_mask_owner, sizeof(o->rpmh_irq_mask_owner));
    RtlZeroMemory(&o->rpmh_irq_mask_ops, sizeof(o->rpmh_irq_mask_ops));
    RtlZeroMemory(&o->rpmh_amc_drain, sizeof(o->rpmh_amc_drain));
    o->rpmh_irq_bridge.pipeline = NULL;
    o->rpmh_irq_bridge.epoch = 0;
    o->rpmh_irq_src.owner_epoch = 0;
    o->rpmh_irq_src.ledger_epoch = 0;
    o->rpmh_irq_src.masked_amcs = 0;
    o->rpmh_irq_src.irq_mask_active = 0;
    o->rpmh_irq_src.irq_mask_faulted = 0;
    o->rpmh.io.enter_irq_lock = NULL;
    o->rpmh.io.leave_irq_lock = NULL;
    o->rpmh.io.irq_lock_ctx = NULL;
    o->rpmh.io.get_irq_lock_refusal = NULL;
    /* after verified WDF/GIC/AMC retirement, this exact d0 owner no longer holds the OS res
     * lease. */

    if (o->os_rsc_lease.active && o->os_rsc_lease.owner_epoch == epoch &&
        o->os_rsc_lease.interrupt == irq) {
        InterlockedExchange(&o->os_rsc_lease.active, 0);
        RtlZeroMemory(&o->os_rsc_lease, sizeof(o->os_rsc_lease));
    }
    /* only the fully drained native owner can clear a clean epoch. */

    if (!rpmh_write_fence_retire(&o->rpmh_write_fence, epoch))
        goto fail_locked;
    o->rpmh_live = FALSE;
    o->probe &= ~(PROBE_RPMH | PROBE_RSC_ID);
    result = STATUS_SUCCESS;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
fail_locked:
    g_quarantined_rsc_owner = o;
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return STATUS_DEVICE_BUSY;
fail_unlocked:
    /* an unretired IRQ or outstanding work must never permit PnP to unmap the RSC/GICD
     * controller or destroy the retained WDF callbacks. */

    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    g_quarantined_rsc_owner = o;
unlock_first:
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return result;
}

NTSTATUS hw_backend_win_d0_unmap_rsc(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst;
    owners_t* o;
    NTSTATUS status = STATUS_SUCCESS;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    inst = g_win_backend;
    o = inst && inst->io.dev == dev ? inst->io.owners : NULL;
    if (o &&
        (ufs_native_retains_graph(&o->ufs_native) || o->ufs_group_bridge || o->ufs_group_votes[0] ||
         o->ufs_group_votes[1] || o->ufs_rail_owner.held || o->ufs_owner_uncertain)) {
        g_quarantined_rsc_owner = o;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "ufs: unmap rejected epoch=%llX client=%llX uncertain=%u",
                    (unsigned long long)o->os_rsc_lease.owner_epoch,
                    (unsigned long long)o->ufs_native.factory.normal_handle,
                    (unsigned)o->ufs_owner_uncertain);
        (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
        return STATUS_DEVICE_BUSY;
    }
    /* the RSC/GICD d0 transport is ONE native lifetime. Check BOTH live maps and their
     * matching PnP generation BEFORE unmapping either, not only before each unmap
     * independently. */

    if (o && (o->d0_rsc.base || o->d0_gic.base) &&
        (g_quarantined_rsc_owner == o || !rsc_d0_mapping_can_close(o) ||
         (o->d0_gic.base && (!o->d0_rsc.base || !o->d0_gic.pnp_generation ||
                             o->d0_gic.pnp_generation != o->d0_rsc.pnp_generation)) ||
         (o->d0_rsc.base && (!o->d0_rsc.pnp_generation || o->mmio.rsc.base != o->d0_rsc.base ||
                             o->mmio.rsc.bytes != o->d0_rsc.bytes)))) {
        g_quarantined_rsc_owner = o;
        status = STATUS_DEVICE_BUSY;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: epoch or rundown mismatch RSC/GIC=retained");
    }
    if (o && o->d0_gic.base && status == STATUS_SUCCESS &&
        !gic_d0_close_win(&o->d0_gic, o->d0_gic.pnp_generation, 1)) {
        g_quarantined_rsc_owner = o;
        status = STATUS_DEVICE_BUSY;
    }
    /* only after joint preflight and successful GICD rundown can the RSC transport be
     * released. Neither branch claims power-vote ownership. */

    if (o && o->d0_rsc.base && status == STATUS_SUCCESS) {
        if (!rsc_d0_map_close_win(&o->d0_rsc, o->d0_rsc.pnp_generation, 1)) {
            g_quarantined_rsc_owner = o;
            status = STATUS_DEVICE_BUSY;
        } else {
            RtlZeroMemory(&o->mmio.rsc, sizeof(o->mmio.rsc));
            o->rsc_id = 0;
            o->drv_config = 0;
            o->probe &= ~PROBE_RSC_ID;
        }
    }
    /* release GCC/TLMM only once the native RSC+GIC owner has been retired, the PoFx admission
     * gate closed, and every physical delegate serialized with o->lock. Never unmap on
     * uncertain teardown. */

    if (o && status == STATUS_SUCCESS && (o->mmio.gcc.base || o->mmio.tlmm.base)) {
        if (!owners_lock(o)) {
            status = STATUS_DEVICE_BUSY;
        } else {
            o->clocks_live = FALSE;
            o->tlmm_live = FALSE;
            o->probe &= ~(PROBE_GPLL0 | PROBE_CLOCK | PROBE_TLMM);
            mmio_d0_unmap_peripherals(&o->mmio);
            owners_unlock(o);
        }
    }
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

NTSTATUS hw_backend_win_attach(_In_ WDFDEVICE dev)
{
    NTSTATUS status;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    status = hw_backend_win_attach_locked(dev);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}

static VOID hw_backend_win_detach_locked(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst = g_win_backend;
    if (!inst)
        return;
    /* a different FDO must never tear down the shared backend's owner, graph, or MMIO
     * lifetime. */

    if (!dev || inst->io.dev != dev) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: detach ignored reason=not-owner");
        return;
    }
    if (!win_backend_unregister(&inst->core)) {
        /* close admission, but never free a backend while a delegated op still holds its lease.
         * retain the entire owner/graph lifetime. */

        g_quarantined_backend_instance = inst;
        g_win_backend = NULL;
        /* a running provider may need the lifecycle mutex. Keep graph storage pinned and admission
         * closed; never take its WDFWAITLOCK under that mutex. */

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: graph callback rundown pending backend=retained attach=blocked");
        return;
    }
    g_win_backend = NULL;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                "resources: backend unregistered admission=%d", hw_backend_ready());
    if (g_quarantined_fs_graph) {
        g_quarantined_backend_instance = inst;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "fs: unpublished client retained backend=pinned");
        return;
    }
    if (g_quarantined_npa) {
        /* failed publication may leave active NPA handles in the retained registry. Its provider
         * callbacks borrow &inst->core, whose IO context and owner allocations must remain valid
         * for an eventual externally synchronized release/audit. */

        g_quarantined_backend_instance = inst;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "npa: unpublished clients quarantined attach=blocked");
        return;
    }
    if (g_published_graph) {
        /* the published graph's providers point into this instance and its owners hold the
         * register mappings and locks, and PoFx callbacks are not run down by anything here.
         * withdraw the graph from DPM and RETAIN the storage for the driver's lifetime: a
         * deliberate leak is the only safe outcome, exactly as. */

        (VOID) pep_unpublish_verified_molokai_graph();
        (VOID) graph_retire(g_published_graph);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: published graph retained reason=provider-rundown-unavailable");
        return;
    }
    {
        owners_t* o = inst->io.owners;
        owners_destroy(o);
        if (o && g_quarantined_rsc_owner == o) {

            g_quarantined_backend_instance = inst;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: owner quarantined backend=retained");
            return;
        }
    }
    ExFreePool(inst);
}

VOID hw_backend_win_detach(_In_ WDFDEVICE dev)
{
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: detach rejected reason=IRQL-or-owner");
        return;
    }
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);
    hw_backend_win_detach_locked(dev);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
}

static int graph_acquire(void* ctx)
{
    if (!ctx || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    WdfWaitLockAcquire((WDFWAITLOCK)ctx, NULL);
    return 1;
}

static void graph_release(void* ctx)
{
    if (ctx)
        WdfWaitLockRelease((WDFWAITLOCK)ctx);
}

/* does not install a hardware provider, NPA client, or PEP ownership; does not send
 * MMIO/RPMh/clock votes. Explicitly opt-in (default OFF): even apparently read-only ACPI
 * AML is firmware-controlled and must be reviewed on a particular device first. */

static NTSTATUS hw_backend_audit_scopes_read_only(_In_ WDFDEVICE dev)
{
    res_graph_t* graph = NULL;
    power_sequence_t* sequence = NULL;
    WDFWAITLOCK lock = NULL;
    NTSTATUS status;
    ULONG i;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    graph = (res_graph_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*graph), POOL_TAG);
    sequence = (power_sequence_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*sequence), POOL_TAG);
    if (!graph || !sequence) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &lock);
    if (!NT_SUCCESS(status))
        goto cleanup;
    /* the graph is inventory-only; every provider callback remains NULL and no D-state can be
     * claimed. */

    graph_init(graph, lock, graph_acquire, graph_release);
    status = acpi_populate_molokai_res_graph(dev, graph, sequence);
    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ACPI, "acpi: inventory scopes=%lu ownership=0",
                    graph->dev_cnt);
        for (i = 0; i < graph->dev_cnt; ++i) {
            const graph_dev_t* d = &graph->devices[i];
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ACPI,
                        "acpi: scope=%s D0=%u D3=%u components=%u ownership=0", d->path,
                        (ULONG)d->has_d0, (ULONG)d->has_d3, (ULONG)d->requires_comp_runtime);
        }
    } else {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                    "acpi: descriptor inventory failed status=%08X", status);
    }
cleanup:
    if (graph && lock)
        (VOID) graph_retire(graph);
    if (lock)
        WdfObjectDelete(lock);
    if (sequence)
        ExFreePool(sequence);
    if (graph) {
        graph_free_sources(graph);
        ExFreePool(graph);
    }
    return status;
}

/* only the current owner FDO may publish the graph; a control device or foreign FDO cannot adopt
 * it. */

static NTSTATUS win_publication_caller(_In_ WDFDEVICE dev)
{
    win_backend_instance_t* inst = g_win_backend;
    p_dev_ctx_t ctx;
    if (!dev || !inst || inst->io.dev != dev)
        return STATUS_DEVICE_NOT_READY;
    ctx = get_dev_ctx(dev);
    if (!ctx || ctx->is_control_dev)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static NTSTATUS hw_backend_win_try_publish_locked(_In_ WDFDEVICE dev)
{
    res_graph_t* graph = NULL;
    npa_registry_t* npa = NULL;
    power_sequence_t* sequence = NULL;
    WDFWAITLOCK lock = NULL;
    npa_callbacks_t callbacks;
    NTSTATUS status;
    BOOLEAN npa_may_hold_clients = FALSE;
    BOOLEAN graph_initialized = FALSE;

    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    /* the lifecycle mutex prevents owner replacement during this check. */

    status = win_publication_caller(dev);
    if (!NT_SUCCESS(status))
        return status;
    /* a physically ambiguous RPMh completion is sticky for this native owner lifetime. An
     * already-published graph or a stale registered callback table must not make TryPublish
     * claim a live DPM owner. */

    if (g_win_backend->io.owners) {
        owners_t* o = g_win_backend->io.owners;
        BOOLEAN uncertain;
        /* publication holds the lifecycle mutex; read the sticky flag under the SAME native-owner
         * mutex that the RPMh submitter uses. An in-flight vote cannot race the admission
         * decision. */

        if (!owners_lock(o))
            return STATUS_DEVICE_NOT_READY;
        uncertain = o->rpmh_write_fence.quarantined ? TRUE : FALSE;
        owners_unlock(o);
        if (uncertain) {
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_RPMH,
                        "rpmh: dispatched state uncertain graph-admission=closed");
            return STATUS_DEVICE_HARDWARE_ERROR;
        }
    }
    if (g_published_graph) {
        /* the graph is intentionally retained across Dx, but its already-installed PoFx provider
         * is unusable while the d0 hardware gate is closed. Publication must not report a
         * successful live graph just because a pointer from a previous d0 still exists. */

        if (!hw_backend_ready())
            return STATUS_DEVICE_NOT_READY;
        if (!g_published_graph->backend_binding_serial ||
            g_published_graph->backend_binding_serial != hw_backend_verified_serial())
            return STATUS_DEVICE_HARDWARE_ERROR;
        return STATUS_SUCCESS;
    }
    if (g_quarantined_npa || g_quarantined_fs_graph)
        return STATUS_DEVICE_HARDWARE_ERROR;
    if (!g_win_backend || !g_win_backend->core.registered)
        return STATUS_DEVICE_NOT_READY;

    if (!hw_backend_ready()) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                    "resources: execution admission closed DPM=rejected");
        /* never convert its data into a published graph or a PEP device acceptance. */

        if (registry_query_u_long(L"MolokaiReadOnlyScopeCensus", 0) == 1u)
            (VOID) hw_backend_audit_scopes_read_only(dev);
        return STATUS_DEVICE_NOT_READY;
    }

    /* all five native delegates must have independently live transports when claiming DPM
     * ownership. */

    if (!win_backend_five_delegations_live(&g_win_backend->core)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: runtime providers unavailable DPM=rejected");
        trace_report(&g_win_backend->core);
        return STATUS_DEVICE_NOT_READY;
    }
    graph = (res_graph_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*graph), POOL_TAG);
    npa = (npa_registry_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*npa), POOL_TAG);
    sequence = (power_sequence_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*sequence), POOL_TAG);
    if (!graph || !npa || !sequence) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    status = WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &lock);
    if (!NT_SUCCESS(status))
        goto fail;

    /* order matters: bind BEFORE build, because the ACPI builder seals the graph atomically
     * and a sealed graph accepts no provider. */

    graph_init(graph, lock, graph_acquire, graph_release);
    graph_initialized = TRUE;
    if (hw_backend_bind_graph(graph) != 0) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "resources: graph binding rejected");
        status = STATUS_DEVICE_NOT_READY;
        goto fail;
    }
    status = acpi_populate_molokai_res_graph(dev, graph, sequence);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI, "acpi: graph build failed status=%08X",
                    status);
        goto fail;
    }
    if (graph->ufs_catalog.compiled) {
        owners_t* owner = g_win_backend->io.owners;
        if (!native_ufs_graph_bind(owner, graph)) {
            if (owner && owner->ufs_owner_uncertain) {
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_POWER, "ufs: owner state uncertain epoch=%llX",
                            (unsigned long long)owner->os_rsc_lease.owner_epoch);
                status = STATUS_DEVICE_HARDWARE_ERROR;
                goto fail;
            }
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "ufs: required-client owner unavailable epoch=%llX",
                        owner ? (unsigned long long)owner->os_rsc_lease.owner_epoch : 0ull);
        } else {
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER,
                        "ufs: required client bound epoch=%llX",
                        (unsigned long long)owner->os_rsc_lease.owner_epoch);
        }
    }
    if (!npa_inventory(npa, graph)) {
        status = STATUS_DATA_ERROR;
        goto fail;
    }
    win_backend_npa_callbacks(&g_win_backend->core, &callbacks);
    npa_may_hold_clients = TRUE;
    if (!npa_activate(npa, &callbacks)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "npa: client activation rejected created=%lu rejected=%lu",
                    g_win_backend->core.diag.npa_created, g_win_backend->core.diag.npa_refused);
        status = STATUS_DEVICE_NOT_READY;
        goto fail;
    }
    status = pep_publish_verified_molokai_graph(graph, npa);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: publication rejected status=%08X", status);
        if (!npa_release(npa))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "npa: client release incomplete registry=quarantined");
        goto fail;
    }
    g_published_graph = graph;
    g_published_npa = npa;
    g_published_graph_lock = lock;
    ExFreePool(sequence);
    if (graph->ufs_catalog.compiled) {
        /* a compiled AEOB table is SOURCE evidence, not a DPM ownership grant or execution result.
         * UFS still needs native PoFx V2 and a crashdump-safe set-0/1/2 physical provider. */

        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                    "ufs: F/P catalog present performance-owner=unbound");
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                "resources: graph published devices=%lu clients=%lu", graph->dev_cnt, npa->cnt);
    return STATUS_SUCCESS;

fail: {
    owners_t* owner = g_win_backend ? g_win_backend->io.owners : NULL;
    BOOLEAN fs_retained = owner && (ufs_native_retains_graph(&owner->ufs_native) ||
                                    owner->ufs_rail_owner.held || owner->ufs_owner_uncertain);
    BOOLEAN npa_retained = npa && npa_may_hold_clients && npa_needs_retention(npa);

    if (fs_retained) {
        /* failed publication cannot release storage referenced by native clients. */
        g_quarantined_fs_graph = graph;
        g_quarantined_rsc_owner = owner;
        g_quarantined_backend_instance = g_win_backend;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "fs: unpublished owner retained epoch=%llX",
                    (unsigned long long)owner->os_rsc_lease.owner_epoch);
    }
    if (npa_retained) {
        npa->quarantined = 1u;
        g_quarantined_npa = npa;
        npa = NULL;
        g_quarantined_backend_instance = g_win_backend;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "npa: unpublished handles retained");
    }
    if (graph_initialized)
        (VOID) graph_retire(graph);
    if (lock && !fs_retained && !npa_retained)
        WdfObjectDelete(lock);
    if (sequence)
        ExFreePool(sequence);
    if (npa)
        ExFreePool(npa);
    if (graph && !fs_retained && !npa_retained) {
        graph_free_sources(graph);
        ExFreePool(graph);
    }
}
    return status;
}

NTSTATUS hw_backend_win_try_publish(_In_ WDFDEVICE dev)
{
    NTSTATUS status;
    if (!dev || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    (VOID) KeWaitForSingleObject(&g_win_lifecycle_lock, Executive, KernelMode, FALSE, NULL);

    status = hw_backend_win_try_publish_locked(dev);
    (VOID) KeReleaseMutex(&g_win_lifecycle_lock, FALSE);
    return status;
}
