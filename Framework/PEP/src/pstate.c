#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "aeob_idx.h"
#include "component_perf.h"
#include "hw_backend.h"
#include "init_gate.h"
#include "trace.h"
#include "perf_clock_win.h"
#include "perf_desc.h"
#include "pofx_perf.h"
#include "resource_cache.h"
#include "runtime_component.h"
#include "runtime_component_win.h"
#include "state_owner.h"
#include "state_txn.h"
#include "dispatch/node_types.h"
#include "dispatch/pstate_types.h"
#include "dispatch/work_types.h"
#include "dispatch/dump_priv.h"
#include "dispatch/main_priv.h"
#include "dispatch/pstate_priv.h"
#include "dispatch/work_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#include "pstate.tmh"
#endif

static BOOLEAN pep_validate_comp_p_state_batch(_In_ PVOID* perf_ctx, _In_ ULONG comp,
                                               _In_reads_(cnt)
                                                   const pep_comp_perf_state_req_t* requests,
                                               _In_ ULONG cnt);
static BOOLEAN pep_native_perf_validate_batch(pep_devnode_t* n, ULONG comp,
                                              const pep_comp_perf_state_req_t* requests, ULONG cnt);
static BOOLEAN pep_comp_p_state_req_completed(_In_ PVOID* perf_ctx, _In_ ULONG comp,
                                              _In_reads_(cnt)
                                                  const pep_comp_perf_state_req_t* requests,
                                              _In_ ULONG cnt,
                                              _In_ const pep_req_comp_perf_state_t* result);
VOID pep_handle_p_state_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                            _In_opt_ pep_work_t* w);
VOID pep_handle_p_state_v2_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                               _In_opt_ pep_work_t* w);
static BOOLEAN pep_native_perf_catalog_ready(pep_devnode_t* n, ULONG comp);
BOOLEAN pep_native_perf_ready(pep_devnode_t* n, ULONG comp);
BOOLEAN pep_native_perf_req(pep_devnode_t* n, pep_req_comp_perf_state_t* r);
static BOOLEAN pep_native_perf_query_set(pep_devnode_t* n, pep_query_comp_perf_set_t* q);
static BOOLEAN pep_native_perf_query_current(pep_devnode_t* n,
                                             pep_query_current_comp_perf_state_t* q);
ULONG_PTR pep_query_comp_perf_caps(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_set(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_set_name(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_comp_perf_states(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_comp_perf_states(_Inout_opt_ PVOID data);
ULONG_PTR pep_req_comp_perf_state(_Inout_opt_ PVOID data);
ULONG_PTR pep_query_current_comp_perf_state(_Inout_opt_ PVOID data);

/* never clamp UFS or other peripherals to the CPU0 DCVS table. This is admission-only; it
 * does not establish a physical provider. */
static BOOLEAN pep_validate_comp_p_state_batch(_In_ PVOID* perf_ctx, _In_ ULONG comp,
                                               _In_reads_(cnt)
                                                   const pep_comp_perf_state_req_t* requests,
                                               _In_ ULONG cnt)
{
    ULONG seen = 0u;
    ULONG set_cnt;
    ULONG i;
    if (!perf_ctx || !*perf_ctx || !requests || !cnt || cnt > 32u)
        return FALSE;
    set_cnt = comp_perf_get_set_cnt(perf_ctx, comp);
    if (!set_cnt || set_cnt > 32u || cnt > set_cnt)
        return FALSE;
    for (i = 0u; i < cnt; ++i) {
        pep_query_comp_perf_set_t q;
        ULONG set = requests[i].set;
        ULONG mask;
        if (set >= set_cnt)
            return FALSE;
        mask = 1u << set;
        if (seen & mask)
            return FALSE;
        seen |= mask;
        RtlZeroMemory(&q, sizeof(q));
        q.dev_handle = NULL;
        q.comp = comp;
        q.set = set;
        if (!comp_perf_query_set(perf_ctx, comp, &q) || q.type != PEP_PERF_STATE_TYPE_DISCRETE ||
            q.u.discrete.cnt == 0u || requests[i].u.state_idx >= q.u.discrete.cnt)
            return FALSE;
    }
    return TRUE;
}

/* a transport acknowledgment is NOT a physical success assertion. The component provider
 * must eventually supply independently authenticated hardware readback, owner-generation
 * proof and rollback before this bridge may be admitted for production. */
static BOOLEAN pep_native_perf_validate_batch(pep_devnode_t* n, ULONG comp,
                                              const pep_comp_perf_state_req_t* requests, ULONG cnt)
{
    ULONG i;
    uint32_t selector, state, seen = 0u;
    if (!n || !requests || !cnt || cnt > 32u || !pep_native_perf_ready(n, comp) ||
        cnt > n->native_perf->catalog->set_cnt[comp])
        return FALSE;
    for (i = 0u; i < cnt; ++i) {
        if (requests[i].set >= 32u || (seen & (1u << requests[i].set)) ||
            !po_fx_catalog_resolve_state(n->native_perf->catalog, comp, requests[i].set,
                                         requests[i].u.state_idx, &selector, &state))
            return FALSE;
        seen |= 1u << requests[i].set;
    }
    return TRUE;
}

static BOOLEAN pep_comp_p_state_req_completed(_In_ PVOID* perf_ctx, _In_ ULONG comp,
                                              _In_reads_(cnt)
                                                  const pep_comp_perf_state_req_t* requests,
                                              _In_ ULONG cnt,
                                              _In_ const pep_req_comp_perf_state_t* result)
{
    ULONG i;
    if (!result || !result->completed || !result->succeeded ||
        !comp_perf_hardware_backend_ready() ||
        !pep_validate_comp_p_state_batch(perf_ctx, comp, requests, cnt))
        return FALSE;
    for (i = 0u; i < cnt; ++i) {
        pep_query_current_comp_perf_state_t q;
        RtlZeroMemory(&q, sizeof(q));
        q.dev_handle = NULL;
        q.comp = comp;
        q.set = requests[i].set;
        if (!comp_perf_query_current_state(perf_ctx, &q) ||
            q.u.state_idx != requests[i].u.state_idx)
            return FALSE;
    }
    return TRUE;
}

VOID pep_handle_p_state_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                            _In_opt_ pep_work_t* w)
{
    NTSTATUS st = STATUS_SUCCESS;
    BOOLEAN held = FALSE;
    pep_pstate_req_t* req = (pep_pstate_req_t*)r->in_buf;
    ULONG comp = 0;
    ULONG sel = 0;

    if (!req || r->in_buf_size < sizeof(*req))
        st = STATUS_INVALID_PARAMETER;
    else {
        comp = req->comp;
        sel = req->p_state_idx;
        if (!n || comp >= n->comp_cnt)
            st = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(st)) {
        held = pep_sync_dpm_begin(n);
        if (!held)
            st = STATUS_INVALID_DEVICE_STATE;
    }

    if (NT_SUCCESS(st) && !n->clock_perf && !n->native_perf && !comp_perf_hardware_backend_ready())
        st = STATUS_NOT_SUPPORTED;

    if (NT_SUCCESS(st)) {
        pep_comp_perf_state_req_t pr0;
        pep_req_comp_perf_state_t pr;
        pr0.set = 0u;
        pr0.u.state_idx = sel;
        /* v1 cannot express more than one performance set. */

        if (!n->clock_perf &&
            (n->native_perf
                 ? (pep_native_perf_ready(n, comp) &&
                    n->native_perf->catalog->set_cnt[comp] == 1u &&
                    pep_native_perf_validate_batch(n, comp, &pr0, 1u)) == FALSE
                 : (comp_perf_get_set_cnt(&n->comp_perf_ctx, comp) != 1u ||
                    !pep_validate_comp_p_state_batch(&n->comp_perf_ctx, comp, &pr0, 1u))))
            st = STATUS_INVALID_PARAMETER;
        else {
            RtlZeroMemory(&pr, sizeof(pr));
            pr.dev_handle = n;
            pr.comp = comp;
            pr.perf_requests_cnt = 1u;
            pr.perf_requests = &pr0;
            if (n->clock_perf ? (!clock_perf_handle(n->clock_perf, 33u, &pr) || !pr.completed ||
                                 !pr.succeeded)
                : n->native_perf
                    ? !pep_native_perf_req(n, &pr)
                    : (!comp_perf_req_state(&n->comp_perf_ctx, &pr) ||
                       !pep_comp_p_state_req_completed(&n->comp_perf_ctx, comp, &pr0, 1u, &pr)))
                st = STATUS_NOT_SUPPORTED;
            else if (n->current_p_state)
                n->current_p_state[comp] = sel;
        }
    }

    if (NT_SUCCESS(st) && n) {
        res_key_t k;
        k.res_id = RESID_PSTATE;
        k.sub_type = ((n->dev_id & 0xFFFFu) << 16) | (comp & 0xFFFFu);

        res_value_t v;
        v.value0 = sel;
        v.value1 = 0;
        v.value2 = 0;
        v.value3 = 0;
        (VOID) res_set(k, v);
        pep_p_state_ocd_test(n, comp, sel);
        if (g_enable_pep_ocd_test > 1)
            pep_display_ocd_test(n);
    }

    if (w && w->work_information) {
        pep_state_result_t* res = (pep_state_result_t*)pep_alloc_result(sizeof(pep_state_result_t));
        if (!res) {
            if (held)
                pep_sync_dpm_end(n);
            return;
        }
        res->pep_status = 2;
        res->status = st;
        res->idx = sel;

        w->work_information->work_type = PepWorkRequestPowerControl;
        w->work_information->u1.power_control.dev_handle = n ? n->kernel_handle : NULL;
        w->work_information->u1.power_control.power_control_code = r->power_control_code;
        w->work_information->u1.power_control.req_ctx = res;
        w->work_information->u1.power_control.in_buf = r->in_buf;
        w->work_information->u1.power_control.in_buf_size = r->in_buf_size;
        w->work_information->u1.power_control.out_buf = res;
        w->work_information->u1.power_control.out_buf_size = sizeof(pep_state_result_t);
        if (held)
            pep_sync_dpm_end(n);
        return;
    }

    if (r->out_buf && r->out_buf_size >= 12) {
        ULONG* out = (ULONG*)r->out_buf;
        out[0] = 3;
        out[1] = st;
        out[2] = sel;
        r->bytes_returned = 12;
    }
    r->status = st;
    if (held)
        pep_sync_dpm_end(n);
}

VOID pep_handle_p_state_v2_req(_In_ pep_devnode_t* n, _Inout_ pep_power_control_req_t* r,
                               _In_opt_ pep_work_t* w)
{
    NTSTATUS st = STATUS_SUCCESS;
    BOOLEAN held = FALSE;
    pep_pstate_res_node_v2_t* in = (pep_pstate_res_node_v2_t*)r->in_buf;

    ULONG comp = 0;
    ULONG cnt = 0;

    if (!in || r->in_buf_size < sizeof(pep_pstate_res_hdr_t) + sizeof(pep_pstate_set_req_t))
        st = STATUS_INVALID_PARAMETER;
    else {
        comp = in->hdr.comp_idx;
        if (!n || comp >= n->comp_cnt)
            st = STATUS_INVALID_PARAMETER;
        else {
            ULONGLONG avail = r->in_buf_size - sizeof(pep_pstate_res_hdr_t);
            /* reject before narrowing the 64-bit buf length. Otherwise a crafted oversized buf
             * could wrap Count to a small value. */

            if (avail == 0u || avail > 32u * sizeof(pep_pstate_set_req_t) ||
                (avail % sizeof(pep_pstate_set_req_t)) != 0u)
                st = STATUS_INVALID_PARAMETER;
            else
                cnt = (ULONG)(avail / sizeof(pep_pstate_set_req_t));
        }
    }

    ULONG sel0 = 0;
    ULONG set0 = 0;

    if (NT_SUCCESS(st)) {
        held = pep_sync_dpm_begin(n);
        if (!held)
            st = STATUS_INVALID_DEVICE_STATE;
    }

    if (NT_SUCCESS(st) && !n->clock_perf && !n->native_perf && !comp_perf_hardware_backend_ready())
        st = STATUS_NOT_SUPPORTED;

    if (NT_SUCCESS(st)) {
        pep_comp_perf_state_req_t reqs[32];
        pep_req_comp_perf_state_t pr;
        ULONG i;
        for (i = 0u; i < cnt; ++i) {
            reqs[i].set = in->p_state_data[i].p_state_set_idx;
            reqs[i].u.state_idx = in->p_state_data[i].p_state_idx;
        }

        if (!n->clock_perf &&
            (n->native_perf ? !pep_native_perf_validate_batch(n, comp, reqs, cnt)
                            : !pep_validate_comp_p_state_batch(&n->comp_perf_ctx, comp, reqs, cnt)))
            st = STATUS_INVALID_PARAMETER;
        else {
            sel0 = reqs[0].u.state_idx;
            set0 = reqs[0].set;
            RtlZeroMemory(&pr, sizeof(pr));
            pr.dev_handle = n;
            pr.comp = comp;
            pr.perf_requests_cnt = cnt;
            pr.perf_requests = reqs;
            if (n->clock_perf ? (!clock_perf_handle(n->clock_perf, 33u, &pr) || !pr.completed ||
                                 !pr.succeeded)
                : n->native_perf
                    ? !pep_native_perf_req(n, &pr)
                    : (!comp_perf_req_state(&n->comp_perf_ctx, &pr) ||
                       !pep_comp_p_state_req_completed(&n->comp_perf_ctx, comp, reqs, cnt, &pr)))
                st = STATUS_NOT_SUPPORTED;
            else if (cnt == 1u && set0 == 0u && n->current_p_state)
                /* this legacy scalar cache cannot describe multiple sets. */
                n->current_p_state[comp] = sel0;
        }
    }

    if (NT_SUCCESS(st) && n && cnt == 1u && set0 == 0u) {
        res_key_t k;
        k.res_id = RESID_PSTATE;
        k.sub_type = ((n->dev_id & 0xFFFFu) << 16) | (comp & 0xFFFFu);

        res_value_t v;
        v.value0 = sel0;
        v.value1 = set0;
        v.value2 = in ? in->hdr.p_state_req_type : 0;
        v.value3 = in ? in->hdr.version : 0;
        (VOID) res_set(k, v);
        pep_p_state_ocd_test(n, comp, sel0);
        if (g_enable_pep_ocd_test > 1)
            pep_display_ocd_test(n);
    }

    if (w && w->work_information) {
        pep_state_result_hdr_t* res =
            (pep_state_result_hdr_t*)pep_alloc_result(sizeof(pep_state_result_hdr_t));
        if (!res) {
            if (held)
                pep_sync_dpm_end(n);
            return;
        }

        res->version = in ? in->hdr.version : 0;
        res->comp_idx = comp;
        res->status = st;
        res->pep_status = 2;
        res->user_data = in ? in->hdr.user_data : NULL;

        w->work_information->work_type = PepWorkRequestPowerControl;
        w->work_information->u1.power_control.dev_handle = n ? n->kernel_handle : NULL;
        w->work_information->u1.power_control.power_control_code = r->power_control_code;
        w->work_information->u1.power_control.req_ctx = res;
        w->work_information->u1.power_control.in_buf = r->in_buf;
        w->work_information->u1.power_control.in_buf_size = r->in_buf_size;
        w->work_information->u1.power_control.out_buf = res;
        w->work_information->u1.power_control.out_buf_size = sizeof(pep_state_result_hdr_t);
        if (held)
            pep_sync_dpm_end(n);
        return;
    }

    if (r->out_buf && r->out_buf_size >= 24) {
        ULONG* out = (ULONG*)r->out_buf;
        out[0] = 2;
        out[1] = comp;
        out[2] = st;
        out[3] = 3;
        *((ULONGLONG*)out + 2) = (ULONGLONG)(in ? in->hdr.user_data : NULL);
        r->bytes_returned = 24;
    }
    r->status = st;
    if (held)
        pep_sync_dpm_end(n);
}

static BOOLEAN pep_native_perf_catalog_ready(pep_devnode_t* n, ULONG comp)
{
    return n && n->native_perf && n->native_perf_backend_serial && hw_backend_ready() &&
           n->native_perf_backend_serial == hw_backend_verified_serial() &&
           po_fx_endpoint_catalog_ready(n->native_perf, n->native_perf->owner_epoch, comp);
}

BOOLEAN pep_native_perf_ready(pep_devnode_t* n, ULONG comp)
{
    return n && n->native_perf && n->native_perf_backend_serial &&
           (!n->runtime_comp || n->runtime_comp->model.dev_in_d0) && hw_backend_ready() &&
           n->native_perf_backend_serial == hw_backend_verified_serial() &&
           po_fx_endpoint_ready(n->native_perf, n->native_perf->owner_epoch, comp);
}

BOOLEAN pep_native_perf_req(pep_devnode_t* n, pep_req_comp_perf_state_t* r)
{
    comp_state_work_t* work;
    power_execution_t trace;
    uint32_t sets[32], states[32], i;
    power_result_t result;
    if (!r || !r->perf_requests || !r->perf_requests_cnt || r->perf_requests_cnt > 32u ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || !n || (!n->native_owner && !n->runtime_comp) ||
        !n->native_perf)
        return FALSE;
    for (i = 0; i < r->perf_requests_cnt; ++i) {
        sets[i] = r->perf_requests[i].set;
        states[i] = r->perf_requests[i].u.state_idx;
    }
    if (n->runtime_comp)
        return runtime_comp_win_p(n->runtime_comp, r->comp, sets, states, r->perf_requests_cnt) ==
               POWER_OK;
    work = (comp_state_work_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
    if (!work)
        return FALSE;
    RtlZeroMemory(&trace, sizeof(trace));
    result = POWER_UNSUPPORTED;
    /* the nested backend lock covers hardware; this outer recursive owner lock also covers
     * preconditions and the per-set commit after it returns. */
    if (pep_native_owner_lock(n->native_owner)) {
        if (pep_native_perf_ready(n, r->comp))
            result = po_fx_endpoint_execute(n->native_perf, n->native_perf->owner_epoch, r->comp,
                                            sets, states, r->perf_requests_cnt, work, &trace);
        pep_native_owner_unlock(n->native_owner);
    }
    ExFreePool(work);
    if (result != POWER_OK)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER,
                    "pstate: set=%lu state=%lu result=%d action=%lu applied=%lu restored=%lu",
                    r->comp, r->perf_requests_cnt, (int)result, trace.failure_idx, trace.applied,
                    trace.restored);
    return result == POWER_OK;
}

static BOOLEAN pep_native_perf_query_set(pep_devnode_t* n, pep_query_comp_perf_set_t* q)
{
    uint32_t selector, cnt;
    if (!q || !pep_native_perf_catalog_ready(n, q->comp) ||
        !po_fx_catalog_resolve_set(n->native_perf->catalog, q->comp, q->set, &selector, &cnt))
        return FALSE;
    q->flags = 0u;
    q->unit = PEP_PERF_STATE_UNIT_OTHER;
    q->type = PEP_PERF_STATE_TYPE_DISCRETE;
    q->u.discrete.cnt = cnt;
    return TRUE;
}

static BOOLEAN pep_native_perf_query_current(pep_devnode_t* n,
                                             pep_query_current_comp_perf_state_t* q)
{
    comp_state_work_t* work;
    uint32_t ordinal = 0u;
    BOOLEAN result;
    if (!q || !pep_native_perf_catalog_ready(n, q->comp))
        return FALSE;
    if (n->runtime_comp) {
        result = runtime_comp_win_query(n->runtime_comp, q->comp, q->set, &ordinal);
        if (result)
            q->u.state_idx = ordinal;
        return result;
    }
    work = (comp_state_work_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*work), POOL_TAG);
    if (!work)
        return FALSE;
    result = po_fx_endpoint_query_current(n->native_perf, n->native_perf->owner_epoch, q->comp,
                                          q->set, work, &ordinal) != 0;
    ExFreePool(work);
    if (result)
        q->u.state_idx = ordinal;
    return result;
}

ULONG_PTR pep_query_comp_perf_caps(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_query_comp_perf_caps_t* q = (pep_query_comp_perf_caps_t*)data;
            n = (pep_devnode_t*)q->dev_handle;
            q->set_cnt = 0;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 28u, data);
            if (!n || q->comp >= n->comp_cnt) {
                return 0;
            }
            q->set_cnt = n->native_perf ? (pep_native_perf_catalog_ready(n, q->comp)
                                               ? n->native_perf->catalog->set_cnt[q->comp]
                                               : 0u)
                                        : comp_perf_get_set_cnt(&n->comp_perf_ctx, q->comp);
            return (q->set_cnt != 0) ? 1 : 0;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_query_comp_perf_set(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_query_comp_perf_set_t* q = (pep_query_comp_perf_set_t*)data;
            n = (pep_devnode_t*)q->dev_handle;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 29u, data);
            if (!n || q->comp >= n->comp_cnt)
                return 0;
            return n->native_perf ? (pep_native_perf_query_set(n, q) ? 1 : 0)
                                  : (comp_perf_query_set(&n->comp_perf_ctx, q->comp, q) ? 1 : 0);
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_query_comp_perf_set_name(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_query_comp_perf_set_name_t* q = (pep_query_comp_perf_set_name_t*)data;
            n = (pep_devnode_t*)q->dev_handle;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 30u, data);
            if (!n || q->comp >= n->comp_cnt)
                return 0;
            if (n->native_perf) {
                uint32_t selector, cnt;
                WCHAR name[40];
                NTSTATUS status;
                USHORT bytes;
                if (!pep_native_perf_catalog_ready(n, q->comp) ||
                    !po_fx_catalog_resolve_set(n->native_perf->catalog, q->comp, q->set, &selector,
                                               &cnt))
                    return 0;
                status = RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"PSTATE_SET_%lu",
                                             (ULONG)selector);
                if (!NT_SUCCESS(status))
                    return 0;
                {
                    size_t chars = 0;
                    if (!NT_SUCCESS(RtlStringCchLengthW(name, RTL_NUMBER_OF(name), &chars)))
                        return 0;
                    bytes = (USHORT)((chars + 1u) * sizeof(WCHAR));
                }
                if (!q->name || q->name_size < bytes) {
                    q->name_size = bytes;
                    return q->name ? 0 : 1;
                }
                RtlCopyMemory(q->name, name, bytes);
                q->name_size = bytes;
                return 1;
            }
            return comp_perf_query_set_name(&n->comp_perf_ctx, q) ? 1 : 0;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_query_comp_perf_states(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_query_comp_perf_states_t* q = (pep_query_comp_perf_states_t*)data;
            n = (pep_devnode_t*)q->dev_handle;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 31u, data);
            if (!n || q->comp >= n->comp_cnt)
                return 0;
            if (n->native_perf) {
                uint32_t selector, cnt, state;
                ULONG i;
                if (!q->states || !pep_native_perf_catalog_ready(n, q->comp) ||
                    !po_fx_catalog_resolve_set(n->native_perf->catalog, q->comp, q->set, &selector,
                                               &cnt))
                    return 0;
                for (i = 0; i < cnt; ++i) {
                    if (!program_p_state_from_ordinal(&n->native_perf->owner->idx, q->comp,
                                                      selector, i, &state))
                        return 0;
                    q->states[i].value = state;
                    q->states[i].ctx = (PVOID)(ULONG_PTR)state;
                }
                return 1;
            }
            return comp_perf_query_states(&n->comp_perf_ctx, q) ? 1 : 0;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_reg_comp_perf_states(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_reg_comp_perf_states_t* r = (pep_reg_comp_perf_states_t*)data;
            n = (pep_devnode_t*)r->dev_handle;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 32u, data);
            if (!n || r->comp >= n->comp_cnt)
                return 0;
            if (!r->perf_state_info)
                return 0;
            if (n->native_perf) {
                uint32_t set_cnt, s, set_selector, states_cnt, state_selector;
                pep_query_current_comp_perf_state_t current;
                if (!pep_native_perf_catalog_ready(n, r->comp))
                    return 0;
                set_cnt = n->native_perf->catalog->set_cnt[r->comp];
                if (!set_cnt || set_cnt != r->perf_state_info->set_cnt)
                    return 0;
                for (s = 0; s < set_cnt; ++s) {
                    const pep_comp_perf_set_t* input = &r->perf_state_info->perf_state_sets[s];
                    if (!po_fx_catalog_resolve_set(n->native_perf->catalog, r->comp, s,
                                                   &set_selector, &states_cnt) ||
                        input->type != PEP_PERF_STATE_TYPE_DISCRETE ||
                        input->u.discrete.cnt != states_cnt || !input->u.discrete.states)
                        return 0;
                    for (uint32_t i = 0; i < states_cnt; ++i) {
                        if (!po_fx_catalog_resolve_state(n->native_perf->catalog, r->comp, s, i,
                                                         NULL, &state_selector) ||
                            input->u.discrete.states[i].value != state_selector)
                            return 0;
                    }
                    RtlZeroMemory(&current, sizeof(current));
                    current.comp = r->comp;
                    current.set = s;
                    if (!pep_native_perf_query_current(n, &current))
                        return 0;
                }
                return 1;
            }
            if (!comp_perf_hardware_backend_ready())
                return 0;
            ULONG existing_set_cnt = comp_perf_get_set_cnt(&n->comp_perf_ctx, r->comp);
            if (existing_set_cnt == 0 || r->perf_state_info->set_cnt != existing_set_cnt)
                return 0;

            for (ULONG set_idx = 0; set_idx < existing_set_cnt; ++set_idx) {
                pep_query_comp_perf_set_t q;
                RtlZeroMemory(&q, sizeof(q));
                q.dev_handle = r->dev_handle;
                q.comp = r->comp;
                q.set = set_idx;
                if (!comp_perf_query_set(&n->comp_perf_ctx, r->comp, &q))
                    return 0;

                pep_comp_perf_set_t* in = &r->perf_state_info->perf_state_sets[set_idx];
                if (in->type != q.type)
                    return 0;

                if (in->type == PEP_PERF_STATE_TYPE_DISCRETE) {
                    if (!in->u.discrete.states || in->u.discrete.cnt == 0 ||
                        in->u.discrete.cnt != q.u.discrete.cnt) {
                        return 0;
                    }
                } else if (in->type == PEP_PERF_STATE_TYPE_RANGE) {
                    if (in->u.range.maximum < in->u.range.minimum)
                        return 0;
                } else {
                    return 0;
                }
            }

            /* propagate failure to PoFx rather than crashing the kernel. */

            if (!comp_perf_reg_states(&n->comp_perf_ctx, r))
                return 0;
            return 1;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_req_comp_perf_state(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_req_comp_perf_state_t* r = (pep_req_comp_perf_state_t*)data;
            n = (pep_devnode_t*)r->dev_handle;
            r->completed = 1;
            r->succeeded = 0;
            if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
                if (!n || !r->perf_requests || !r->perf_requests_cnt ||
                    r->perf_requests_cnt > PEP_MAX_QUEUED_PERF_REQUESTS ||
                    !init_gate_ready(&g_pep_work_init))
                    return 1;
                r->completed = 0;
                if (!pep_enqueue_work(PEP_WORKPAYLOAD_REQUESTPERFSTATE, r, FALSE))
                    r->completed = 1;
                return 1;
            }
            if (!pep_sync_dpm_begin(n))
                return 1;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 33u, data);
            /* unsupported PoFx requests are completed as failed, not treated as a
             * firmware-programming success or fatal input corruption. */

            if (!n || r->comp >= n->comp_cnt || !r->perf_requests || !r->perf_requests_cnt ||
                r->perf_requests_cnt >= 0xFFFFu ||
                (!n->native_perf && !comp_perf_hardware_backend_ready()))
                return 1;

            if (n->native_perf)
                r->succeeded = pep_native_perf_req(n, r) ? 1 : 0;
            else
                (VOID) comp_perf_req_state(&n->comp_perf_ctx, r);
            return 1;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

ULONG_PTR pep_query_current_comp_perf_state(_Inout_opt_ PVOID data)
{
    if (!data)
        return 0;
    BOOLEAN held = FALSE;
    pep_devnode_t* n = NULL;
    __try {
        __try {
            pep_query_current_comp_perf_state_t* q = (pep_query_current_comp_perf_state_t*)data;
            n = (pep_devnode_t*)q->dev_handle;
            if (!pep_sync_dpm_begin(n))
                return 0;
            held = TRUE;
            if (n->clock_perf)
                return clock_perf_handle(n->clock_perf, 34u, data);
            /* an unregistered component or withdrawn backend makes this query unsupported; neither
             * condition is a kernel invariant failure. */

            if (!n || q->comp >= n->comp_cnt)
                return 0;
            if (n->native_perf)
                return pep_native_perf_query_current(n, q) ? 1 : 0;
            if (!comp_perf_hardware_backend_ready() || !n->comp_perf_ctx ||
                !comp_perf_query_current_state(&n->comp_perf_ctx, q))
                return 0;
            return 1;
        } __finally {
            if (held)
                pep_sync_dpm_end(n);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
