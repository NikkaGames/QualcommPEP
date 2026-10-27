#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "hw_backend.h"
#include "trace.h"
#include "peplog.h"
#include "pofx_perf.h"
#include "res_graph.h"
#include "resource_cache.h"
#include "runtime_component_win.h"
#include "runtime_dstate.h"
#include "state_owner.h"
#include "state_txn.h"
#include "ufs_catalog.h"
#include "ufs_pstate_graph_bridge.h"
#include "dispatch/fstate_types.h"
#include "dispatch/node_types.h"
#include "dispatch/work_types.h"
#include "dispatch/dump_priv.h"
#include "dispatch/fstate_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#include "dispatch/work_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#include "fstate.tmh"
#endif

static __forceinline BOOLEAN pep_idle_fields_valid(_In_opt_ const pep_notify_comp_idle_state_t* s,
                                                   _In_opt_ const pep_devnode_t* n);
static BOOLEAN bounded_acpi_path_equal(const char* a, const char* b);
static BOOLEAN pep_execute_verified_comp_f_state(_Inout_ pep_devnode_t* n, _In_ ULONG comp,
                                                 _In_ ULONG state);
VOID pep_notify_comp_idle_state_work(_Inout_ pep_notify_comp_idle_state_t* s,
                                     _In_opt_ pep_work_information_t* wi);
ULONG_PTR pep_notify_comp_idle_state(_Inout_opt_ PVOID data);
ULONG_PTR pep_dev_idle_constraints(_Inout_opt_ PVOID data);
ULONG_PTR pep_comp_idle_constraints(_Inout_opt_ PVOID data);

/* malformed PoFx idle notifications are not kernel invariants. The handler must also fail
 * admission if the device is no longer registered. */
static __forceinline BOOLEAN pep_idle_fields_valid(_In_opt_ const pep_notify_comp_idle_state_t* s,
                                                   _In_opt_ const pep_devnode_t* n)
{
    return s && n && s->comp < n->comp_cnt &&
           (s->driver_notified == 0u || s->driver_notified == 1u) &&
           ((s->driver_notified == 0u && s->idle_state == 0u) ||
            (s->driver_notified == 1u && s->idle_state != 0u));
}

/* checked fixed-buf path equality: never pass a malformed or missing terminator to strcmp
 * in a kernel PEP callback. */
static BOOLEAN bounded_acpi_path_equal(const char* a, const char* b)
{
    ULONG i;
    if (!a || !b || a[0] != '\\' || b[0] != '\\')
        return FALSE;
    for (i = 0; i < 256u; ++i) {
        if (a[i] != b[i])
            return FALSE;
        if (a[i] == 0u)
            return i >= 5u;
    }
    return FALSE;
}

/* the compiled ACPI F-state program requires provider snapshot/apply/verify/rollback before PoFx
 * completion. */
static BOOLEAN pep_execute_verified_comp_f_state(_Inout_ pep_devnode_t* n, _In_ ULONG comp,
                                                 _In_ ULONG state)
{
    pep_comp_fstate_scope_t* scope;
    const power_sequence_t* sequence;
    power_scratch_t* scratch;
    power_execution_t trace;
    power_result_t result;
    power_sequence_t* expanded = NULL;
    ULONG i, nested = 0u;
    char path[256];
    if (!n || KeGetCurrentIrql() != PASSIVE_LEVEL || !n->current_f_state || comp >= n->comp_cnt ||
        state >= 32u)
        return FALSE;
    if (n->native_perf) {
        comp_state_work_t* work;
        power_result_t r = POWER_UNSUPPORTED;
        if (n->runtime_comp)
            return runtime_comp_win_f(n->runtime_comp, comp, state) == POWER_OK;
        if (!n->native_perf_backend_serial || !hw_backend_ready() ||
            n->native_perf_backend_serial != hw_backend_verified_serial() || !n->native_owner)
            return FALSE;
        work = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
        if (!work)
            return FALSE;
        RtlZeroMemory(&trace, sizeof(trace));
        if (pep_native_owner_lock(n->native_owner)) {
            r = po_fx_endpoint_execute_f_state(n->native_perf, n->native_perf->owner_epoch, comp,
                                               state, work, &trace);
            pep_native_owner_unlock(n->native_owner);
        }
        ExFreePool(work);
        if (r != POWER_OK)
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                        "fstate: F%lu component=%lu result=%d action=%lu restored=%lu", state, comp,
                        (int)r, trace.failure_idx, trace.restored);
        return r == POWER_OK;
    }
    if (n->runtime_dstate_scope) {
        return (comp == 0u && state == 0u && n->runtime_dstate_scope->program.empty_f0 &&
                pep_runtime_d_execute(n->runtime_dstate_scope, 0u) == POWER_OK);
    }
    if (n->peer_scope) {
        power_execution_t trace;
        power_result_t result;
        scratch = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
        if (!scratch)
            return FALSE;
        result = pep_run_peer_f_state(n->peer_scope, state, &trace, scratch);
        ExFreePool(scratch);
        return result == POWER_OK;
    }
    /* it has no writes, but must not be completed unless the native d0 provider votes remain
     * physically verified under the graph lock. */

    if (n->comp_cnt == 1u && comp == 0u && state == 0u &&
        pep_is_molokai_dpm_execution_ready(&n->instance_path) &&
        pep_resolve_molokai_dev(&n->instance_path, path) && g_molokai_dpm_authoritative_graph &&
        graph_verify_reviewed_ic_f0(g_molokai_dpm_authoritative_graph, path, comp, state))
        return TRUE;
    scope = n->f_state_scope;
    if (!scope || !scope->src_verified || scope->comp_cnt != n->comp_cnt ||
        scope->comp_cnt > 128u || scope->sequence_slots != scope->comp_cnt * 32u ||
        !scope->sequences || !scope->verified_states || !scope->backend ||
        /* even a direct (non-PSTATE_ADJUST) F-state requires independently verified device d0. A
         * registered PoFx handle or Windows current_d_state cannot substitute for proof that
         * physical power resources are live. */

        !scope->physical_dev_state || !scope->physical_dev_state->state_known ||
        scope->physical_dev_state->quarantined ||
        scope->physical_dev_state->current_d_state != 0u ||
        !pep_is_molokai_dpm_execution_ready(&n->instance_path) ||
        !pep_resolve_molokai_dev(&n->instance_path, path) ||
        !bounded_acpi_path_equal(scope->dev_path, path))
        return FALSE;
    sequence = scope->sequences[comp * 32u + state];
    if (!sequence || !bounded_acpi_path_equal(sequence->dev_path, path) ||
        sequence->target_d_state != state || sequence->action_cnt > POWER_MAX_ACTIONS)
        return FALSE;
    /* dispatch nested UFS BPMD f0/f1 through the existing authoritative graph and its atomic,
     * provider-verified PSTATE_SET executor. The ufs_graph_bridge is NOT populated until
     * native NPA owner + PoFx reg exist. */

    if (scope->ufs_graph_bridge) {
        ufs_pstate_graph_bridge_t* u = scope->ufs_graph_bridge;
        if (comp != 0u || state >= UFS_FSTATES || n->comp_cnt != 1u ||
            scope->sets_per_comp != UFS_PSTATE_SETS || !scope->p_state_set_states ||
            !bounded_acpi_path_equal(scope->dev_path, "\\_SB.UFS0") || !u->graph ||
            u->graph != g_molokai_dpm_authoritative_graph ||
            u->graph->backend_binding_serial != u->serial || !u->graph->ufs_catalog.compiled)
            return FALSE;
        scratch =
            (power_scratch_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
        if (!scratch)
            return FALSE;
        RtlZeroMemory(&trace, sizeof(trace));
        result = ufs_fstate_graph_execute(u, state, scope->physical_dev_state,
                                          &scope->verified_states[comp], scope->p_state_set_states,
                                          &trace, scratch);
        ExFreePool(scratch);
        if (result != POWER_OK) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER, "ufs: F%lu transaction failed result=%d",
                        state, (int)result);
            return FALSE;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "ufs: F%lu transaction verified", state);
        return TRUE;
    }
    /* zero-action F-states still require final provider verification. */

    /* never pass them to a generic backend, never call a nested transaction with a
     * nonrecursive graph lock, and never commit cached F-state if one of the three P-state
     * sets failed. */

    for (i = 0u; i < sequence->action_cnt; ++i)
        if (sequence->actions[i].kind == POWER_PSTATE_ADJUST)
            nested = 1u;
    if (nested &&
        (!scope->p_state_programs || !scope->p_state_program_cnt || !scope->p_state_set_states ||
         !scope->physical_dev_state || !scope->sets_per_comp || scope->sets_per_comp > 32u ||
         scope->p_state_program_cnt > 1024u))
        return FALSE;
    scratch = (power_scratch_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*scratch), POOL_TAG);
    if (!scratch)
        return FALSE;
    if (nested) {
        expanded =
            (power_sequence_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*expanded), POOL_TAG);
        if (!expanded) {
            ExFreePool(scratch);
            return FALSE;
        }
    }
    RtlZeroMemory(&trace, sizeof(trace));
    if (nested) {
        result = power_execute_comp_f_state(
            sequence, comp, scope->p_state_programs, scope->p_state_program_cnt,
            scope->physical_dev_state, &scope->verified_states[comp],
            &scope->p_state_set_states[comp * scope->sets_per_comp], scope->sets_per_comp,
            scope->backend, &trace, expanded, scratch);
    } else {
        result = power_execute_f_state(sequence, &scope->verified_states[comp], scope->backend,
                                       &trace, scratch);
    }
    if (expanded)
        ExFreePool(expanded);
    ExFreePool(scratch);
    if (result != POWER_OK) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER, "fstate: component=%lu F%lu failed result=%d",
                    comp, state, (int)result);
        return FALSE;
    }
    return TRUE;
}

VOID pep_notify_comp_idle_state_work(_Inout_ pep_notify_comp_idle_state_t* s,
                                     _In_opt_ pep_work_information_t* wi)
{
    pep_devnode_t* n;
    if (!s)
        return;
    s->completed = 0;
    n = (pep_devnode_t*)s->dev_handle;
    if (!pep_idle_fields_valid(s, n)) {
        /* return a completion without changing cached F-state or programming hardware. */

        pep_log_write(PEPLOG_EVT_IDLE, 0x80u, STATUS_INVALID_PARAMETER, 0u, s->comp,
                      s->driver_notified, s->idle_state);
        if (wi) {
            wi->work_type = PepWorkCompleteIdleState;
            wi->u1.active_complete.dev_handle = s->dev_handle;
            wi->u1.active_complete.comp = s->comp;
        }
        return;
    }
    /* never mutate current_f_state or report completion before the entire physical F-state
     * program has committed and verified. */

    if (!pep_execute_verified_comp_f_state(n, s->comp, s->idle_state)) {
        pep_log_write(PEPLOG_EVT_IDLE, 0x80u, STATUS_NOT_SUPPORTED, 0u, s->comp, s->driver_notified,
                      s->idle_state);
        return;
    }
    n->current_f_state[s->comp] = s->idle_state;

    {
        res_key_t k;
        k.res_id = 0x41545346u;
        k.sub_type = ((n->dev_id & 0xFFFFu) << 16) | (s->comp & 0xFFFFu);
        ULONGLONG t = KeQueryInterruptTime();
        res_value_t v;
        v.value0 = s->idle_state;
        v.value1 = s->driver_notified ? 1u : 0u;
        v.value2 = (ULONG)t;
        v.value3 = (ULONG)(t >> 32);
        (VOID) res_set(k, v);
    }

    pep_log_write(PEPLOG_EVT_IDLE, 0x80, STATUS_SUCCESS, (ULONG)(ULONG_PTR)n, s->comp,
                  s->driver_notified ? 1u : 0u, s->idle_state);
    if (g_enable_pep_ocd_test && pep_ocd_timer_armed()) {
        pep_f_state_ocd_test(n);
        if (g_enable_pep_ocd_test > 1)
            pep_display_ocd_test(n);
    }
    if (wi) {
        wi->work_type = PepWorkCompleteIdleState;
        wi->u1.active_complete.dev_handle = n->kernel_handle;
        wi->u1.active_complete.comp = s->comp;
    } else
        s->completed = 1;
}

/* a synchronous component F-state notify is a hardware transaction, just like d0/Dx:
 * acquire a node rundown claim BEFORE field validation or execution. The wait lock in
 * SyncDpmBegin requires PASSIVE_LEVEL and protects against interpreting a stale arbitrary
 * dev_handle as a live node. */
ULONG_PTR pep_notify_comp_idle_state(_Inout_opt_ PVOID data)
{
    pep_notify_comp_idle_state_t* s = (pep_notify_comp_idle_state_t*)data;
    pep_devnode_t* n = NULL;
    BOOLEAN admitted = FALSE;
    ULONG_PTR result = 0;
    if (!s)
        return 0;
    __try {
        s->completed = 0;
        /* no device-list wait lock, no node dereference, and no work item at DISPATCH_LEVEL. */

        if (KeGetCurrentIrql() == PASSIVE_LEVEL) {
            n = (pep_devnode_t*)s->dev_handle;
            if (pep_sync_dpm_begin(n)) {
                admitted = TRUE;
                if (pep_idle_fields_valid(s, n)) {
                    pep_notify_comp_idle_state_work(s, NULL);
                    result = s->completed ? 1u : 0u;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* the rundown claim must still retire after an exception. */

        result = 0;
    }
    if (admitted)
        pep_sync_dpm_end(n);
    return result;
}

ULONG_PTR pep_dev_idle_constraints(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;

    __try {
        ULONG_PTR* q = (ULONG_PTR*)data;

        pep_devnode_t* n = (pep_devnode_t*)q[0];
        if (!n || !q[1])
            return 0;

        ULONG platform_cnt = *(ULONG*)((PUCHAR)data + 16);
        if (platform_cnt == 0 || platform_cnt > 0x1000)
            return 1;

        ULONG coordinated_cnt = 0;
        pep_query_platform_states_acpi_lpi_pep(&coordinated_cnt, NULL);
        if (platform_cnt != coordinated_cnt)
            return 1;

        if (n->reg_state != DEV_REGISTERED_MINI_PEP)
            return 0;

        ULONG* out = (ULONG*)q[1];

        ULONG drips_idx = g_drips_coordinated_mode_idx;
        if (drips_idx == 0xFFFFFFFFu || drips_idx > platform_cnt)
            drips_idx = 0;

        ULONG shallow_d_state = 1;
        ULONG min_d_state = shallow_d_state;
        if (n->current_d_state >= PowerDeviceD0 && n->current_d_state < PowerDeviceMaximum)
            min_d_state = (ULONG)n->current_d_state;

        for (ULONG i = 0; i < platform_cnt; i++)
            out[i] = (i < drips_idx) ? shallow_d_state : min_d_state;

        pep_log_write(PEPLOG_EVT_IDLE, 0x70, STATUS_SUCCESS, platform_cnt, shallow_d_state,
                      min_d_state, drips_idx);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_comp_idle_constraints(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;

    __try {

        ULONG_PTR* q = (ULONG_PTR*)data;

        pep_devnode_t* n = (pep_devnode_t*)q[0];
        if (!n || !q[2])
            return 0;

        if (n->reg_state != DEV_REGISTERED_MINI_PEP)
            return 0;

        ULONG comp = *(ULONG*)((PUCHAR)data + 8);
        if (comp >= n->comp_cnt)
            return 0;

        ULONG platform_cnt = *(ULONG*)((PUCHAR)data + 24);
        if (platform_cnt == 0 || platform_cnt > 0x1000)
            return 1;

        ULONG coordinated_cnt = 0;
        pep_query_platform_states_acpi_lpi_pep(&coordinated_cnt, NULL);
        if (platform_cnt != coordinated_cnt)
            return 1;

        ULONG* out = (ULONG*)q[2];

        ULONG drips_idx = g_drips_coordinated_mode_idx;
        if (drips_idx == 0xFFFFFFFFu || drips_idx > platform_cnt)
            drips_idx = 0;

        ULONG shallow_f_state = 0;
        ULONG min_f_state = shallow_f_state;
        if (n->current_f_state)
            min_f_state = n->current_f_state[comp];

        for (ULONG i = 0; i < platform_cnt; i++)
            out[i] = (i < drips_idx) ? shallow_f_state : min_f_state;

        pep_log_write(PEPLOG_EVT_IDLE, 0x71, STATUS_SUCCESS, platform_cnt, comp, min_f_state,
                      drips_idx);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
