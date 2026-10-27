#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "aeob_idx.h"
#include "component_perf.h"
#include "dpm_verified.h"
#include "hw_backend.h"
#include "hw_backend_windows_wdf.h"
#include "trace.h"
#include "npa_client.h"
#include "peer_component.h"
#include "perf_desc.h"
#include "pofx_perf.h"
#include "res_graph.h"
#include "resource_cache.h"
#include "resource_ddi.h"
#include "runtime_component.h"
#include "runtime_component_win.h"
#include "runtime_dstate.h"
#include "state_owner.h"
#include "state_txn.h"
#include "dispatch/dstate_types.h"
#include "dispatch/node_types.h"
#include "dispatch/dstate_priv.h"
#include "dispatch/dump_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#include "dispatch/pstate_priv.h"
#include "dispatch/work_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#include "dstate.tmh"
#endif

ULONG_PTR pep_prepare_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_abandon_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_unregister_dev(_Inout_opt_ PVOID data);
static BOOLEAN pep_dpm_handle_registered(_In_opt_ pep_devnode_t* node);
BOOLEAN pep_dpm_begin_transition(_Inout_ pep_devnode_t* node,
                                 _In_ DEVICE_POWER_STATE desired_state);
static BOOLEAN pep_dpm_claim_completion(_Inout_ pep_devnode_t* node,
                                        _In_ DEVICE_POWER_STATE completed_state);
VOID pep_dpm_finish_transition(_Inout_ pep_devnode_t* node);
static BOOLEAN pep_forget_firmware_dpm_state(_In_ const UNICODE_STRING* dev_id);
static int pep_proof_invalidation_ready(void* ctx);
static BOOLEAN pep_invalidate_runtime_proof(_Inout_ pep_devnode_t* n, _In_ ULONG target);
static VOID pep_complete_dx_backend(_Inout_ pep_devnode_t* n,
                                    _In_ DEVICE_POWER_STATE effective_state,
                                    _In_ UCHAR system_transition);
VOID pep_dev_power_state_work(_Inout_ pep_dev_power_state_t* p);
static __forceinline BOOLEAN pep_dev_power_state_d0_transition(_Inout_ pep_dev_power_state_t* p,
                                                               _Inout_ pep_devnode_t* n);
static __forceinline BOOLEAN pep_dev_power_state_dx_transition(_Inout_ pep_dev_power_state_t* p,
                                                               _Inout_ pep_devnode_t* n);
static __forceinline VOID pep_handle_dev_power_system_transition(_Inout_ pep_dev_power_state_t* p,
                                                                 _Inout_ pep_devnode_t* n);
static VOID pep_dpm_record_notify(pep_devnode_t* n, const pep_dev_power_state_t* p);
ULONG_PTR pep_dev_power_state(_Inout_opt_ PVOID data);

/* independent bounded boot-time diagnostics, never ownership bits. Early callbacks before
 * d0 must not consume later exact-path trace budget. */
static volatile LONG g_pre_ready_traces;
static volatile LONG g_unmatched_traces;
static volatile LONG g_prepare_traces;

ULONG_PTR pep_prepare_dev(_Inout_opt_ PVOID data)
{
    pep_prepare_dev_t* p = (pep_prepare_dev_t*)data;
    pep_devnode_t* n;
    char path[256];
    if (!p)
        return 0;
    p->dev_accepted = 0;
    /* PREPARE records OS ownership requests and the first failed admission check. It must not
     * bypass physical provider authorization. */

    if (!pep_is_molokai_dpm_execution_ready(p->dev_id)) {
        if (InterlockedIncrement(&g_pre_ready_traces) <= 16) {
            char early_id[128];
            KIRQL early_irql = KeGetCurrentIrql();
            BOOLEAN printable = (early_irql == PASSIVE_LEVEL) && ascii_dev_id(p->dev_id, early_id);
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                        "dpm: prepare rejected reason=backend-unavailable");
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                        "dpm: prepare path=%s IRQL=%lu accepted=0",
                        printable ? early_id : "<not-safe-to-format>", (ULONG)early_irql);
        }
        return 1;
    }
    /* runtime D0/D3 admission follows the signed descriptor and verified provider votes. */

    {
        char runtime_path[256];
        ULONG bytes = 0;
        if (runtime_acpi_path(p->dev_id, runtime_path) && strcmp(runtime_path, "\\_SB.UFS0") &&
            runtime_res_copy(runtime_path, NULL, 0, &bytes) == STATUS_BUFFER_TOO_SMALL &&
            bytes > 0u) {
            (VOID) pep_prepare_runtime_comp(p, runtime_path);
            if (p->dev_accepted)
                return 1;
            {
                pep_devnode_t* prior = pep_find_dev_node_by_id(p->dev_id);
                if (prior && prior->runtime_comp && prior->runtime_comp->model.retain)
                    return 1;
            }
            (VOID) pep_prepare_runtime_dstate(p, runtime_path);
            if (p->dev_accepted)
                return 1;

            (VOID) pep_prepare_reviewed_peer(p, runtime_path);
            if (p->dev_accepted)
                return 1;
        }
    }
    if (!pep_resolve_molokai_dev(p->dev_id, path)) {
        if (InterlockedIncrement(&g_unmatched_traces) <= 32) {
            if (p->dev_id && p->dev_id->Buffer && p->dev_id->Length <= p->dev_id->MaximumLength &&
                p->dev_id->Length <= 512u && !(p->dev_id->Length & 1u))
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                            "dpm: prepare path=%wZ rejected reason=identity-unmatched", p->dev_id);
            else
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER,
                            "dpm: prepare rejected reason=invalid-identity");
        }
        return 1;
    }
    if (!pep_ufs_comp_desc_matches_graph(path)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: prepare path=%s rejected reason=descriptor-generation", path);
        return 1;
    }
    if (!strcmp(path, "\\_SB.UFS0") &&
        !hw_backend_win_ufs_owner_ready(g_molokai_dpm_authoritative_graph,
                                        hw_backend_verified_serial())) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "ufs: prepare rejected reason=required-client-owner");
        return 1;
    }
    if (!g_molokai_npa_authoritative_clients ||
        !npa_dev_clients_ready_locked(g_molokai_npa_authoritative_clients, path)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: prepare path=%s rejected reason=clients-unavailable", path);
        return 1;
    }
    if (!graph_can_claim(g_molokai_dpm_authoritative_graph, path)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: prepare path=%s rejected reason=graph-claim", path);
        return 1;
    }
    if (InterlockedIncrement(&g_prepare_traces) <= 64)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "dpm: prepare path=%s target=D0", path);
    /* a failure here must not strand a device in d0 without an accepted PEP node. */

    n = pep_get_or_create_dev_node(p->dev_id);
    if (!n || n->reg_state == DEV_REGISTERED_MINI_PEP || n->reg_state == DEV_PREPARE_ACCEPTED)
        return 1;
    /* prepare d0, but only under the verified production gates above. A failed apply/rollback
     * must not advertise dev_accepted. */

    {
        power_result_t prepare_result = pep_run_molokai_lifecycle(p->dev_id, TRUE);
        if (prepare_result != POWER_OK) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: prepare path=%s D0 failed result=%d", path, (int)prepare_result);
            return 1;
        }
    }
    if (!strcmp(path, "\\_SB.UFS0") && !pep_prepare_native_ufs_scope(n, path)) {
        if (!n->native_owner) {
            /* no F/P request reached the provider: retire the known d0 graph vote instead of
             * stranding an unaccepted PREPARE. */
            power_result_t rollback = pep_run_molokai_lifecycle(p->dev_id, FALSE);
            if (rollback != POWER_OK)
                TraceEvents(TRACE_LEVEL_ERROR, TRACE_POWER,
                            "ufs: prepare rollback D3 failed result=%d", (int)rollback);
        } else {
            /* a request may have reached hardware. Preserve owner and resources until physically
             * verified f1/d3 retirement. */
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_POWER,
                        "ufs: prepare failed after F/P dispatch owner=retained");
        }
        return 1;
    }
    idle_acpi_free();
    n->reg_state = DEV_PREPARE_ACCEPTED;
    ExAcquireFastMutex(&n->d_state_change_f_mutex);
    n->d_state_change_in_progress = 0;
    n->d_state_completion_claimed = 0;
    n->d_state_pending_power_state = PowerDeviceUnspecified;
    KeSetEvent(&n->d_state_progress_event, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&n->d_state_change_f_mutex);
    pep_apply_prepare_states(n);
    /* prepare executed and verified the initial d0 sequence. record evidence before publishing
     * acceptance; a subsequent d0 notify must NOT issue the initial d0 votes a second time. */

    ExAcquireFastMutex(&n->d_state_change_f_mutex);
    dpm_verified_record(&n->dpm_backend_state, 0u, TRUE);
    ExReleaseFastMutex(&n->d_state_change_f_mutex);
    p->dev_accepted = 1;
    return 1;
}

ULONG_PTR pep_abandon_dev(_Inout_opt_ PVOID data)
{
    pep_abandon_dev_t* p = (pep_abandon_dev_t*)data;
    pep_devnode_t* n;
    if (!p)
        return 0;
    p->dev_accepted = 0;
    if (!p->dev_id || !p->dev_id->Buffer || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 1;
    n = pep_find_dev_node_by_id(p->dev_id);
    if (!n || (!n->native_owner && n->reg_state != DEV_PREPARE_ACCEPTED &&
               n->reg_state != DEV_UNREGISTERED))
        return 1;
    /* an unregistered native endpoint can still own f0/P-state/NPA votes. ABANDON cannot
     * discard them through the D-state-only graph path. */
    if (n->native_perf) {
        if (n->runtime_comp) {
            if (!runtime_comp_win_close(n->runtime_comp))
                return 1;
            n->native_perf = NULL;
            n->native_perf_backend_serial = 0u;
            pep_apply_abandon_states(n);
            n->reg_state = DEV_ABANDONED;
            p->dev_accepted = 1u;
            return 1;
        }
        if (!pep_native_ufs_retire(n, p->dev_id)) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: abandon retirement failed provider=retained clients=retained");
            return 1;
        }
        pep_apply_abandon_states(n);
        n->reg_state = DEV_ABANDONED;
        p->dev_accepted = 1;
        return 1;
    }
    if (n->runtime_dstate_scope) {
        if (pep_runtime_d_execute(n->runtime_dstate_scope, 3u) != POWER_OK)
            return 1;
    } else if (n->peer_scope) {
        power_scratch_t* scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
        power_execution_t trace;
        power_result_t result;
        if (!scratch)
            return 1;
        result = pep_run_peer_f_state(n->peer_scope, 1u, &trace, scratch);
        ExFreePool(scratch);
        if (result != POWER_OK)
            return 1;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, "dpm: F1 release verified");
    } else if (pep_run_molokai_lifecycle(p->dev_id, FALSE) != POWER_OK)
        return 1;
    pep_apply_abandon_states(n);
    n->reg_state = DEV_ABANDONED;
    p->dev_accepted = 1;
    return 1;
}

ULONG_PTR pep_reg_dev(_Inout_opt_ PVOID data)
{
    pep_reg_dev_v2_t* p = (pep_reg_dev_v2_t*)data;
    pep_devnode_t* n = NULL;
    ULONG* new_p = NULL;
    ULONG* new_f = NULL;
    PVOID new_perf_ctx = NULL;
    ULONG cc;
    BOOLEAN reviewed_without_p_states = FALSE;

    if (!p)
        return 0;
    /* no existing node means no device ownership. */

    p->dev_handle = NULL;
    p->dev_accepted = DEV_NOT_ACCEPTED;
    if (!p->dev_id || !p->dev_id->Buffer || !p->dev_id->Length || !p->reg || !g_pep_dev_list_lock ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 1;

    cc = p->reg->comp_cnt;

    if (cc > 128u)
        return 1;
    /* allocated component arrays do not establish F/P support; native NPA/component state programs
     * must be verified. */

    /* prepare's earlier acceptance must not survive revoked graph/NPA publication. Do these
     * independent lock acquisitions BEFORE the WDF list lock (the graph and provider locks
     * must never nest under it). */

    {
        char path[256];
        BOOLEAN peer_ok = FALSE, ic_ok = FALSE, dstate_ok = FALSE, runtime_ok = FALSE;
        BOOLEAN native_perf_ok = FALSE;
        pep_devnode_t* runtime_node = pep_find_dev_node_by_id(p->dev_id);
        if (runtime_node && runtime_node->runtime_dstate_scope) {
            runtime_dstate_native_t* r = runtime_node->runtime_dstate_scope;
            runtime_ok = r->program.valid && !r->program.quarantined &&
                         r->serial == hw_backend_verified_serial() &&
                         r->program.physical.state_known &&
                         r->program.physical.current_d_state == 0u &&
                         ((cc == 0u && r->program.idx.comp_mask == 0u) ||
                          (cc == 1u && r->program.idx.comp_mask == 1u && r->program.empty_f0));
        }
        if (cc == 1u) {
            pep_devnode_t* peer_node = pep_find_dev_node_by_id(p->dev_id);
            peer_ok = peer_node && peer_node->peer_scope &&
                      peer_node->peer_scope->serial == hw_backend_verified_serial() &&
                      peer_node->peer_scope->comp.state.state_known &&
                      peer_node->peer_scope->comp.state.current_d_state == 0u &&
                      !peer_node->peer_scope->comp.quarantined;
        }
        {
            pep_devnode_t* provider_node = pep_find_dev_node_by_id(p->dev_id);
            if (provider_node && provider_node->native_perf &&
                provider_node->native_perf->catalog &&
                provider_node->native_perf->catalog->components == cc) {
                native_perf_ok = TRUE;
                for (ULONG comp = 0; comp < cc; ++comp)
                    if (provider_node->runtime_comp
                            ? (!provider_node->runtime_comp->model.owner.comp[comp].state_known ||
                               provider_node->runtime_comp->model.owner.quarantined ||
                               !provider_node->runtime_comp->model.dev_in_d0)
                            : !pep_native_perf_ready(provider_node, comp))
                        native_perf_ok = FALSE;
            }
        }
        if (!peer_ok && !runtime_ok && !native_perf_ok) {
            if (!pep_is_molokai_dpm_execution_ready(p->dev_id) ||
                !pep_resolve_molokai_dev(p->dev_id, path) || !g_molokai_dpm_authoritative_graph ||
                !g_molokai_npa_authoritative_clients ||
                !npa_dev_clients_ready_locked(g_molokai_npa_authoritative_clients, path))
                return 1;
            /* the only verified local component contract has an empty f0, no PSTATE_SET. */

            dstate_ok = graph_reg_dstate_only(g_molokai_dpm_authoritative_graph, path, cc);
            if (!dstate_ok && cc == 1u)
                ic_ok = graph_reg_reviewed_ic_comp(g_molokai_dpm_authoritative_graph, path, cc);
            if (!dstate_ok && !ic_ok)
                return 1;
        }
        /* a retained native endpoint takes precedence even after retirement. Never downgrade an
         * unverified F/P owner to the graph-only profile. */
        if (runtime_node && runtime_node->native_perf && !native_perf_ok)
            return 1;
        reviewed_without_p_states = (peer_ok || ic_ok || runtime_ok || native_perf_ok);
    }

    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    n = pep_find_dev_node_locked(p->dev_id);
    if (!n || n->reg_state != DEV_PREPARE_ACCEPTED)
        goto done;

    if (cc) {
        const SIZE_T bytes = (SIZE_T)cc * sizeof(ULONG);
        new_p = (ULONG*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
        if (!new_p)
            goto done;
        RtlZeroMemory(new_p, bytes);
        new_f = (ULONG*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
        if (!new_f)
            goto done;
        RtlZeroMemory(new_f, bytes);
        /* never initialize the generic CompPerf implementation here: that path derives its set
         * count/frequencies from CPU DCVS and could advertise wholly fabricated device P-states
         * when EnableComponentPerf is set. f0/f1 remain independently handled by the verified
         * component idle-state executor. */

        if (!reviewed_without_p_states || (!n->native_perf && cc != 1u))
            goto done;
    }

    /* the previous unregister must finish its admitted workers before a new reg can reopen
     * this device's deferred execution gate. */

    if (n->deferred_unregistering || n->deferred_workers != 0)
        goto done;
    /* the list lock also protects n against teardown here. */

    if (n->comp_perf_ctx)
        comp_perf_deinit_for_dev(&n->comp_perf_ctx);
    if (n->current_p_state)
        ExFreePool(n->current_p_state);
    if (n->current_f_state)
        ExFreePool(n->current_f_state);
    n->current_p_state = new_p;
    n->current_f_state = new_f;
    n->comp_perf_ctx = new_perf_ctx;
    n->comp_cnt = cc;
    n->kernel_handle = p->kernel_handle;
    n->reg_state = DEV_REGISTERED_MINI_PEP;
    {
        KIRQL irq;
        KeAcquireSpinLock(&n->deferred_work_lock, &irq);
        ++n->deferred_generation;
        if (n->deferred_generation == 0)
            ++n->deferred_generation;
        n->deferred_closed = FALSE;
        KeReleaseSpinLock(&n->deferred_work_lock, irq);
    }
    new_p = NULL;
    new_f = NULL;
    new_perf_ctx = NULL;
    p->dev_handle = n;
    p->dev_accepted = DEV_ACCEPTED;
done:
    WdfWaitLockRelease(g_pep_dev_list_lock);
    if (new_perf_ctx)
        comp_perf_deinit_for_dev(&new_perf_ctx);
    if (new_p)
        ExFreePool(new_p);
    if (new_f)
        ExFreePool(new_f);
    return 1;
}

ULONG_PTR pep_unregister_dev(_Inout_opt_ PVOID data)
{
    pep_unregister_dev_t* p = (pep_unregister_dev_t*)data;
    if (!p || !p->dev_handle)
        return 0;

    pep_devnode_t* n = (pep_devnode_t*)p->dev_handle;
    if (!g_pep_dev_list_lock)
        return 0;

    BOOLEAN found = FALSE;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (pep_devnode_t* it = g_pep_dev_list_head; it; it = it->next) {
        if (it == n) {
            found = TRUE;
            break;
        }
    }

    if (found && n->reg_state == DEV_UNREGISTERED) {
        /* a second UNREGISTER must not reopen the gate of an earlier callback that is still
         * waiting for deferred workers to retire. */

        WdfWaitLockRelease(g_pep_dev_list_lock);
        return 1;
    }
    if (found && n->reg_state != DEV_REGISTERED_MINI_PEP) {
        WdfWaitLockRelease(g_pep_dev_list_lock);
        return 0;
    }
    if (found) {
        KIRQL irq;
        KeAcquireSpinLock(&n->deferred_work_lock, &irq);
        n->deferred_closed = TRUE;
        n->deferred_unregistering = TRUE;
        ++n->deferred_generation;
        if (n->deferred_generation == 0)
            ++n->deferred_generation;
        KeReleaseSpinLock(&n->deferred_work_lock, irq);
        ExAcquireFastMutex(&n->d_state_change_f_mutex);
        n->reg_state = DEV_UNREGISTERED;
        n->d_state_change_in_progress = 0;
        n->d_state_completion_claimed = 0;
        n->d_state_pending_power_state = PowerDeviceUnspecified;
        KeSetEvent(&n->d_state_progress_event, IO_NO_INCREMENT, FALSE);
        ExReleaseFastMutex(&n->d_state_change_f_mutex);
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);

    if (!found)
        return 0;

    /* never wait while holding g_pep_dev_list_lock: a deferred worker may need that lock to
     * finish. Every admitted worker retires before this UNREGISTER notify returns to PoFx. */

    KeWaitForSingleObject(&n->deferred_work_drained, Executive, KernelMode, FALSE, NULL);
    if (n->native_perf) {
        /* all deferred AND synchronous callbacks have been drained. Keep provider and pinned BPMD
         * alive for verified ABANDON f1/d3. */
        if (n->native_owner)
            po_fx_producer_close(&n->native_owner->producer);
        else
            po_fx_endpoint_retire(n->native_perf);
        n->native_perf_backend_serial = 0u;
    }
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    {
        KIRQL irq;
        KeAcquireSpinLock(&n->deferred_work_lock, &irq);
        n->deferred_unregistering = FALSE;
        KeReleaseSpinLock(&n->deferred_work_lock, irq);
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return 1;
}

/* dpm callbacks must not dereference an unregistered/stale PoFx handle. These nodes are
 * retained while PoFx may still reference them, but their reg status must be checked under
 * the device-list lock. */
static BOOLEAN pep_dpm_handle_registered(_In_opt_ pep_devnode_t* node)
{
    BOOLEAN found = FALSE;
    if (!node || !g_pep_dev_list_lock || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    WdfWaitLockAcquire(g_pep_dev_list_lock, NULL);
    for (pep_devnode_t* it = g_pep_dev_list_head; it; it = it->next) {
        if (it == node) {
            found = (it->reg_state == DEV_REGISTERED_MINI_PEP);
            break;
        }
    }
    WdfWaitLockRelease(g_pep_dev_list_lock);
    return found;
}

/* the two-phase notify spans callbacks, so its completion must remain free to acquire the
 * node mutex while a new transition waits. */
BOOLEAN pep_dpm_begin_transition(_Inout_ pep_devnode_t* node, _In_ DEVICE_POWER_STATE desired_state)
{
    LARGE_INTEGER timeout;
    ULONGLONG deadline, now;
    if (!node || KeGetCurrentIrql() != PASSIVE_LEVEL || desired_state <= PowerDeviceUnspecified ||
        desired_state >= PowerDeviceMaximum)
        return FALSE;
    deadline = KeQueryInterruptTime() + 80000000ULL;
    for (;;) {
        ExAcquireFastMutex(&node->d_state_change_f_mutex);
        if (node->reg_state != DEV_REGISTERED_MINI_PEP) {
            ExReleaseFastMutex(&node->d_state_change_f_mutex);
            return FALSE;
        }
        if (!node->d_state_change_in_progress) {
            node->d_state_change_in_progress = 1u;
            node->d_state_pending_power_state = desired_state;
            node->d_state_completion_claimed = 0u;
            KeClearEvent(&node->d_state_progress_event);
            ExReleaseFastMutex(&node->d_state_change_f_mutex);
            return TRUE;
        }
        ExReleaseFastMutex(&node->d_state_change_f_mutex);
        now = KeQueryInterruptTime();
        if (now >= deadline)
            return FALSE;
        timeout.QuadPart = -(LONGLONG)(deadline - now);
        if (KeWaitForSingleObject(&node->d_state_progress_event, Executive, KernelMode, FALSE,
                                  &timeout) != STATUS_SUCCESS)
            return FALSE;
    }
}

/* pofx may deliver delayed or duplicate complete notifications. Never let a different
 * D-state completion clear an unrelated transition, or run a d3 provider a second time. */
static BOOLEAN pep_dpm_claim_completion(_Inout_ pep_devnode_t* node,
                                        _In_ DEVICE_POWER_STATE completed_state)
{
    BOOLEAN claimed = FALSE;
    if (!node || KeGetCurrentIrql() != PASSIVE_LEVEL || completed_state <= PowerDeviceUnspecified ||
        completed_state >= PowerDeviceMaximum)
        return FALSE;
    ExAcquireFastMutex(&node->d_state_change_f_mutex);
    if (node->reg_state == DEV_REGISTERED_MINI_PEP && node->d_state_change_in_progress &&
        !node->d_state_completion_claimed && node->d_state_pending_power_state == completed_state) {
        node->d_state_completion_claimed = 1u;
        claimed = TRUE;
    }
    ExReleaseFastMutex(&node->d_state_change_f_mutex);
    return claimed;
}

VOID pep_dpm_finish_transition(_Inout_ pep_devnode_t* node)
{
    if (!node || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    ExAcquireFastMutex(&node->d_state_change_f_mutex);
    node->d_state_change_in_progress = 0;
    node->d_state_completion_claimed = 0;
    node->d_state_pending_power_state = PowerDeviceUnspecified;
    KeSetEvent(&node->d_state_progress_event, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&node->d_state_change_f_mutex);
}

/* invalidate graph state under its transaction lock so resume replays or verifies owned resources.
 */
static BOOLEAN pep_forget_firmware_dpm_state(_In_ const UNICODE_STRING* dev_id)
{
    char path[256];
    if (!pep_resolve_molokai_dev(dev_id, path))
        return FALSE;
    return graph_forget_firmware_state(g_molokai_dpm_authoritative_graph, path) ? TRUE : FALSE;
}

static int pep_proof_invalidation_ready(void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
}

static BOOLEAN pep_invalidate_runtime_proof(_Inout_ pep_devnode_t* n, _In_ ULONG target)
{
    volatile LONG* slot;
    if (!n || target > 3u)
        return FALSE;
    if (n->runtime_comp)
        return runtime_comp_win_forget(n->runtime_comp);
    if (n->runtime_dstate_scope)
        slot = &n->runtime_dstate_scope->target_d_state;
    else if (n->peer_scope)
        slot = &n->peer_scope->target_f_state;
    else if (n->native_owner) {
        BOOLEAN forgotten;
        ULONG c, set;
        if (!pep_native_owner_lock(n->native_owner)) {
            n->native_owner->producer.owner.quarantined = 1u;
            return FALSE;
        }
        forgotten = pep_forget_firmware_dpm_state(&n->instance_path);
        for (c = 0u; c < 32u; ++c) {
            n->native_owner->producer.owner.comp[c].state_known = 0u;
            for (set = 0u; set < 32u; ++set)
                n->native_owner->producer.owner.sets[c][set].state_known = 0u;
        }
        pep_native_owner_unlock(n->native_owner);
        return forgotten;
    } else
        return pep_forget_firmware_dpm_state(&n->instance_path);
    if (!pep_claim_res_transaction(slot, (LONG)target, NULL, pep_proof_invalidation_ready)) {
        /* firmware may already have changed power. The sticky flag is not cleared by an in-flight
         * commit, unlike its state_known field. */
        if (n->runtime_dstate_scope)
            n->runtime_dstate_scope->program.quarantined = 1u;
        else
            n->peer_scope->comp.quarantined = 1u;
        return FALSE;
    }
    if (n->runtime_dstate_scope)
        n->runtime_dstate_scope->program.physical.state_known = 0u;
    else
        n->peer_scope->comp.state.state_known = 0u;
    InterlockedExchange(slot, -1);
    return TRUE;
}

static VOID pep_complete_dx_backend(_Inout_ pep_devnode_t* n,
                                    _In_ DEVICE_POWER_STATE effective_state,
                                    _In_ UCHAR system_transition)
{
    power_result_t d3;
    BOOLEAN forgotten;
    if (!n || effective_state <= PowerDeviceD0 || effective_state >= PowerDeviceMaximum ||
        (n->reg_state != DEV_REGISTERED_MINI_PEP && n->reg_state != DEV_PREPARE_ACCEPTED))
        return;
    if (system_transition) {
        /* no d3 vote: firmware owns its system-transition sequencing. This graph operation only
         * invalidates cached proof, not hardware. */

        forgotten = pep_invalidate_runtime_proof(n, (ULONG)effective_state - (ULONG)PowerDeviceD0);
        ExAcquireFastMutex(&n->d_state_change_f_mutex);
        dpm_verified_reset(&n->dpm_backend_state);
        ExReleaseFastMutex(&n->d_state_change_f_mutex);
        if (!forgotten)
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: system transition graph proof retained resume-verification=required");
        return;
    }
    {
        /* a d1/d2 callback must never dispatch d3 or claim its physical proof. */

        const ULONG target = (ULONG)effective_state - (ULONG)PowerDeviceD0;
        d3 = pep_run_node_d_state(n, target);
        ExAcquireFastMutex(&n->d_state_change_f_mutex);
        dpm_verified_record(&n->dpm_backend_state, target,
                            d3 == POWER_OK && n->reg_state == DEV_REGISTERED_MINI_PEP);
        ExReleaseFastMutex(&n->d_state_change_f_mutex);
        if (d3 != POWER_OK && !(InterlockedOr(&n->dpm_failure_reports, 2) & 2))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: path=%wZ D%lu backend failed result=%d", &n->instance_path, target,
                        (int)d3);
    }
}

VOID pep_dev_power_state_work(_Inout_ pep_dev_power_state_t* p)
{
    if (!p || !p->dev_handle || p->power_state <= PowerDeviceUnspecified ||
        p->power_state >= PowerDeviceMaximum ||
        !pep_dpm_handle_registered((pep_devnode_t*)p->dev_handle)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: completion rejected reason=invalid-handle");
        return;
    }

    /* pep_DEVICE_POWER_STATE.complete is INPUT from PoFx, not a completion acknowledgement
     * written by this driver. */

    if (!p->complete)
        return;

    pep_devnode_t* n = (pep_devnode_t*)p->dev_handle;
    if (!pep_dpm_claim_completion(n, p->power_state)) {
        if (!(InterlockedOr(&n->dpm_failure_reports, 4) & 4))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: path=%wZ unmatched D%lu completion ignored", &n->instance_path,
                        (ULONG)p->power_state - (ULONG)PowerDeviceD0);
        return;
    }
    if (n) {
        DEVICE_POWER_STATE effective_state = pep_apply_usb_sub_dx_state_hint(n, p->power_state);
        ExAcquireFastMutex(&n->d_state_change_f_mutex);
        n->current_d_state = effective_state;
        ExReleaseFastMutex(&n->d_state_change_f_mutex);
        /* this stores the PoFx-reported state, not a hardware-success flag. d0 completion without
         * a verified d0 pre-notify must never make this device appear powered to our backend. */

        if (effective_state == PowerDeviceD0 && !dpm_verified_is(&n->dpm_backend_state, 0u) &&
            (n->reg_state == DEV_REGISTERED_MINI_PEP || n->reg_state == DEV_PREPARE_ACCEPTED))
            TraceEvents(TRACE_LEVEL_ERROR, TRACE_POWER,
                        "dpm: OS D0 completion without verified backend vote");
        n->in_system_transition = (UCHAR)(p->system_transition ? 1 : 0);

        res_key_t k;
        k.res_id = 0x41545344u;
        k.sub_type = (n->dev_id & 0xFFFFu);

        res_value_t v;
        v.value0 = (ULONG)effective_state;
        v.value1 = (ULONG)p->system_transition;
        v.value2 = 0;
        v.value3 = 0;
        (VOID) res_set(k, v);

        pep_complete_dx_backend(n, effective_state, p->system_transition);

        pep_dpm_finish_transition(n);

        if (g_enable_pep_ocd_test && pep_ocd_timer_armed()) {
            pep_d_state_ocd_test(n);
            if (g_enable_pep_ocd_test > 1)
                pep_display_ocd_test(n);
        }
    }

    /* do not write p->complete: PoFx owns that input field. */
}

/* this only declines our callback: Windows does not provide an error NTSTATUS channel for
 * these notifications, and FALSE must not be interpreted as a guaranteed OS power-IRP
 * veto. */
static __forceinline BOOLEAN pep_dev_power_state_d0_transition(_Inout_ pep_dev_power_state_t* p,
                                                               _Inout_ pep_devnode_t* n)
{
    if (!p || !n || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    if (p->complete) {
        pep_dev_power_state_work(p);
        return TRUE;
    }
    if (!pep_dpm_begin_transition(n, p->power_state)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER, "dpm: D0 admission timed out dispatched=0");
        return FALSE;
    }
    power_result_t d0 = pep_run_node_d_state(n, 0u);
    ExAcquireFastMutex(&n->d_state_change_f_mutex);
    dpm_verified_record(&n->dpm_backend_state, 0u,
                        d0 == POWER_OK && n->reg_state == DEV_REGISTERED_MINI_PEP);
    ExReleaseFastMutex(&n->d_state_change_f_mutex);
    if (d0 != POWER_OK) {
        if (!(InterlockedOr(&n->dpm_failure_reports, 1) & 1))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "dpm: path=%wZ D0 pre-notification failed result=%d", &n->instance_path,
                        (int)d0);
        pep_dpm_finish_transition(n);
        return FALSE;
    }
    return TRUE;
}

static __forceinline BOOLEAN pep_dev_power_state_dx_transition(_Inout_ pep_dev_power_state_t* p,
                                                               _Inout_ pep_devnode_t* n)
{
    if (!p || !n || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    if (p->complete) {
        pep_dev_power_state_work(p);
        return TRUE;
    }
    /* d1/d2 must be independently present and compiled for this runtime owner. System
     * transitions remain firmware-owned and invalidate proof later. */

    if (!p->system_transition && p->power_state != PowerDeviceD3 &&
        (p->power_state != PowerDeviceD1 && p->power_state != PowerDeviceD2 ||
         !n->runtime_dstate_scope ||
         !(n->runtime_dstate_scope->program.idx.d_state_mask &
           (uint8_t)(1u << ((ULONG)p->power_state - (ULONG)PowerDeviceD0))))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: intermediate state absent from descriptor");
        return FALSE;
    }
    if (!pep_dpm_begin_transition(n, p->power_state)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER, "dpm: Dx admission timed out claim=0");
        return FALSE;
    }
    return TRUE;
}

static __forceinline VOID pep_handle_dev_power_system_transition(_Inout_ pep_dev_power_state_t* p,
                                                                 _Inout_ pep_devnode_t* n)
{
    if (!p || !n)
        pep_bug_check(18u, p ? (PVOID)n : (PVOID)p);
    if (!p->system_transition)
        return;

    if (p->power_state == PowerDeviceD0 && !p->complete) {
        if (InterlockedCompareExchange(&g_hibernate_in_progress, 0, 0) != 0)
            pep_on_hibernate_resume();
        return;
    }

    if (p->power_state > PowerDeviceD0 && p->power_state < PowerDeviceMaximum && p->complete) {
        if (InterlockedCompareExchange(&g_hibernate_in_progress, 0, 0) == 0)
            (VOID) pep_on_hibernate_enter();
    }
}

static VOID pep_dpm_record_notify(pep_devnode_t* n, const pep_dev_power_state_t* p)
{
    ULONG target = (ULONG)p->power_state - (ULONG)PowerDeviceD0;
    LONG cnt;
    if (target > 3u)
        return;
    cnt = InterlockedIncrement(p->complete ? &n->d_state_complete_notifications[target]
                                           : &n->d_state_pre_notifications[target]);

    if (cnt == 1)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "dpm: path=%wZ D%lu phase=%s system=%u",
                    &n->instance_path, target, p->complete ? "complete" : "pre",
                    (unsigned)p->system_transition);
}

ULONG_PTR pep_dev_power_state(_Inout_opt_ PVOID data)
{
    pep_dev_power_state_t* p = (pep_dev_power_state_t*)data;
    pep_devnode_t* n;
    ULONG_PTR result = 0;
    if (!p || !p->dev_handle || p->power_state <= PowerDeviceUnspecified ||
        p->power_state >= PowerDeviceMaximum || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    n = (pep_devnode_t*)p->dev_handle;
    if (!pep_sync_dpm_begin(n)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "dpm: notification rejected reason=stale-handle");
        return 0;
    }
    /* from this point, unregister must wait for the synchronous callback. */

    pep_dpm_record_notify(n, p);
    pep_handle_dev_power_system_transition(p, n);
    /* the transaction engine re-verifies an unchanged state under its res lock without
     * replaying its sequence. Node cache alone cannot bypass a concurrent clock request, revoked
     * epoch or quarantine. */
    if (p->power_state == PowerDeviceD0)
        result = pep_dev_power_state_d0_transition(p, n) ? 1u : 0u;
    else
        result = pep_dev_power_state_dx_transition(p, n) ? 1u : 0u;
    pep_sync_dpm_end(n);
    return result;
}
