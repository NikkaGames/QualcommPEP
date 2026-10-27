#include "po_fx.h"
#include "power_control.h"
#include "resource_cache.h"
#include "stats.h"
#include "xo_diag.h"
#include "pstate_payload.h"
#include "trace.h"
#include "po_fx.tmh"

static PO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK po_fx_active_condition;
static PO_FX_COMPONENT_IDLE_CONDITION_CALLBACK po_fx_idle_condition;

static const GUID guid_power_change_p_state = {
    872733356u, 36396u, 16868u, {132u, 88u, 49u, 88u, 229u, 173u, 68u, 43u}};
static const GUID guid_power_change_p_state_v2 = {
    2571285598u, 11412u, 16883u, {161u, 92u, 193u, 165u, 145u, 199u, 4u, 105u}};
static const GUID guid_clock_test = {
    3845751031u, 56204u, 19172u, {136u, 222u, 159u, 241u, 8u, 37u, 202u, 16u}};
static const GUID guid_power_set_selextsource = {
    3382942026u, 59180u, 20083u, {140u, 143u, 65u, 34u, 93u, 205u, 193u, 84u}};
static const GUID guid_log_command = {
    4052892314u, 54781u, 19497u, {167u, 46u, 171u, 201u, 242u, 248u, 163u, 185u}};
static const GUID guid_sdbus_pstate_reg = {
    452966471u, 47163u, 16536u, {188u, 13u, 41u, 53u, 192u, 94u, 198u, 180u}};
static const GUID guid_sdbus_pstate_req = {
    1479800911u, 33071u, 19654u, {139u, 175u, 25u, 130u, 189u, 195u, 170u, 226u}};
static const GUID guid_sdbus_pstate_mmc_info = {
    61598841u, 51386u, 16512u, {181u, 67u, 62u, 151u, 91u, 34u, 253u, 215u}};
static const GUID guid_xhci_sub_dx_state = {
    3234047663u, 2695u, 20289u, {165u, 29u, 33u, 189u, 226u, 235u, 191u, 179u}};

typedef struct pofx_pstate_req {
    ULONG comp;
    ULONG p_state_idx;
} pofx_pstate_req_t;

typedef struct pofx_pstate_res_hdr {
    ULONG version;
    ULONG comp_idx;
    ULONG p_state_req_type;
    ULONG reserved0;
    PVOID user_data;
} pofx_pstate_res_hdr_t;

typedef struct pofx_pstate_set_req {
    ULONG p_state_set_idx;
    ULONG p_state_idx;
} pofx_pstate_set_req_t;

typedef struct pofx_pstate_res_node_v2 {
    pofx_pstate_res_hdr_t hdr;
    pofx_pstate_set_req_t p_state_data[1];
} pofx_pstate_res_node_v2_t;

typedef enum pofx_power_code {
    PO_FX_POWER_DEFAULT = 0,
    PO_FX_POWER_CHANGE_P_STATE = 1,
    PO_FX_POWER_CLOCK_TEST = 2,
    PO_FX_POWER_SET_SEL_EXT_SRC = 3,
    PO_FX_POWER_LOG = 4,
    PO_FX_POWER_CHANGE_P_STATE_V2 = 5,
    PO_FX_POWER_SDBUS_PSTATE_REG = 7,
    PO_FX_POWER_SDBUS_PSTATE_REQ = 8,
    PO_FX_POWER_SDBUS_PSTATE_MMC_INFO = 9,
    PO_FX_POWER_GUID_XHCI_SUB_DX_STATE = 12,
} pofx_power_code_t;

static __forceinline pofx_power_code_t po_fx_decode_power_code(_In_opt_ LPCGUID guid)
{
    if (!guid)
        return PO_FX_POWER_DEFAULT;

    if (IsEqualGUID(guid, &guid_power_change_p_state))
        return PO_FX_POWER_CHANGE_P_STATE;
    if (IsEqualGUID(guid, &guid_power_change_p_state_v2))
        return PO_FX_POWER_CHANGE_P_STATE_V2;
    if (IsEqualGUID(guid, &guid_clock_test))
        return PO_FX_POWER_CLOCK_TEST;
    if (IsEqualGUID(guid, &guid_power_set_selextsource))
        return PO_FX_POWER_SET_SEL_EXT_SRC;
    if (IsEqualGUID(guid, &guid_log_command))
        return PO_FX_POWER_LOG;
    if (IsEqualGUID(guid, &guid_sdbus_pstate_reg))
        return PO_FX_POWER_SDBUS_PSTATE_REG;
    if (IsEqualGUID(guid, &guid_sdbus_pstate_req))
        return PO_FX_POWER_SDBUS_PSTATE_REQ;
    if (IsEqualGUID(guid, &guid_sdbus_pstate_mmc_info))
        return PO_FX_POWER_SDBUS_PSTATE_MMC_INFO;
    if (IsEqualGUID(guid, &guid_xhci_sub_dx_state))
        return PO_FX_POWER_GUID_XHCI_SUB_DX_STATE;

    return PO_FX_POWER_DEFAULT;
}

static __forceinline VOID po_fx_trace_power_control(_In_opt_ LPCGUID guid,
                                                    _In_ pofx_power_code_t code,
                                                    _In_ NTSTATUS status, _In_ SIZE_T in_len,
                                                    _In_ SIZE_T out_len)
{
    etw_write_power_control(guid ? guid->Data1 : 0u, (ULONG)code, status, (ULONG)in_len,
                            (ULONG)out_len);
}

static NTSTATUS po_fx_write_compat_pstate_result(_In_ ULONG version_tag, _In_ NTSTATUS status,
                                                 _Out_writes_bytes_opt_(out_buf_size) PVOID out_buf,
                                                 _In_ SIZE_T out_buf_size,
                                                 _Out_ PSIZE_T bytes_returned)
{
    if (!bytes_returned)
        return STATUS_INVALID_PARAMETER;
    *bytes_returned = 0;

    if (!out_buf || out_buf_size == 0)
        return status;

    if (out_buf_size < sizeof(ULONG) * 3) {
        *bytes_returned = sizeof(ULONG) * 3;
        return STATUS_BUFFER_TOO_SMALL;
    }

    ULONG* out = (ULONG*)out_buf;
    out[0] = version_tag;
    out[1] = (ULONG)status;
    out[2] = 0;
    *bytes_returned = sizeof(ULONG) * 3;
    return status;
}

static NTSTATUS po_fx_write_compat_pstate_v2_result(_In_opt_ const pofx_pstate_res_node_v2_t* in,
                                                    _In_ NTSTATUS status,
                                                    _Out_writes_bytes_opt_(out_buf_size)
                                                        PVOID out_buf,
                                                    _In_ SIZE_T out_buf_size,
                                                    _Out_ PSIZE_T bytes_returned)
{
    if (!bytes_returned)
        return STATUS_INVALID_PARAMETER;
    *bytes_returned = 0;

    if (!out_buf || out_buf_size == 0)
        return status;

    if (out_buf_size < 24) {
        *bytes_returned = 24;
        return STATUS_BUFFER_TOO_SMALL;
    }

    ULONG* out = (ULONG*)out_buf;
    out[0] = 2;
    out[1] = in ? in->hdr.comp_idx : 0;
    out[2] = NT_SUCCESS(status) ? 0u : (ULONG)status;
    out[3] = NT_SUCCESS(status) ? 1u : 0u;
    *((ULONGLONG*)out + 2) = in ? (ULONGLONG)in->hdr.user_data : 0ull;
    *bytes_returned = 24;
    return status;
}

_Use_decl_annotations_ NTSTATUS evt_post_po_fx_reg(WDFDEVICE dev, POHANDLE po_handle)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (ctx)
        ctx->po_handle = po_handle;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, "pofx: registered handle=%p", po_handle);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_ VOID evt_pre_po_fx_unregister(WDFDEVICE dev, POHANDLE po_handle)
{
    UNREFERENCED_PARAMETER(po_handle);
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (ctx)
        ctx->po_handle = NULL;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, "pofx: unregister");
}

_Use_decl_annotations_ VOID po_fx_active_condition(PVOID ctx, ULONG comp)
{
    UNREFERENCED_PARAMETER(ctx);
    InterlockedIncrement(&g_stats.po_fx_comp_active);
    stats_record_event(STATS_EVENT_PO_FX_ACTIVE_CONDITION, STATUS_SUCCESS, comp, 0,
                       (ULONG)g_stats.po_fx_comp_active, 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "pofx: component=%lu active", comp);
}

_Use_decl_annotations_ VOID po_fx_idle_condition(PVOID ctx_arg, ULONG comp)
{
    WDFDEVICE dev = (WDFDEVICE)ctx_arg;
    p_dev_ctx_t ctx = get_dev_ctx(dev);

    InterlockedIncrement(&g_stats.po_fx_comp_idle);
    stats_record_event(STATS_EVENT_PO_FX_IDLE_CONDITION, STATUS_SUCCESS, comp,
                       ctx ? ctx->port_id : 0, (ULONG)g_stats.po_fx_comp_idle, 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_POWER, "pofx: component=%lu idle", comp);

    if (ctx && ctx->po_handle)
        PoFxCompleteIdleCondition(ctx->po_handle, comp);
}

_Use_decl_annotations_ NTSTATUS power_control(WDFDEVICE dev, LPCGUID power_control_code,
                                              PVOID in_buf, SIZE_T in_buf_size, PVOID out_buf,
                                              SIZE_T out_buf_size, PSIZE_T bytes_returned)
{
    if (!bytes_returned)
        return STATUS_INVALID_PARAMETER;
    *bytes_returned = 0;

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    BOOLEAN is_passive = (KeGetCurrentIrql() == PASSIVE_LEVEL);
    pofx_power_code_t code = po_fx_decode_power_code(power_control_code);

    switch (code) {
        case PO_FX_POWER_CHANGE_P_STATE: {
            uint32_t comp = 0u, state = 0u;
            /* do not tell PoFx the hardware changed when it did not. */

            NTSTATUS status = po_fx_p_state_validate_v1(in_buf, in_buf_size, &comp, &state)
                                  ? STATUS_NOT_SUPPORTED
                                  : STATUS_INVALID_PARAMETER;
            UNREFERENCED_PARAMETER(comp);
            UNREFERENCED_PARAMETER(state);

            status =
                po_fx_write_compat_pstate_result(1u, status, out_buf, out_buf_size, bytes_returned);
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_CHANGE_P_STATE_V2: {
            uint32_t comp = 0u, state = 0u, cnt = 0u;
            int valid = po_fx_p_state_validate_v2(in_buf, in_buf_size, &comp, &state, &cnt);
            /* no native NPA/clock/PMIC transaction is attached to this legacy WDF control handler,
             * even if its payload is valid. */

            NTSTATUS status = valid ? STATUS_NOT_SUPPORTED : STATUS_INVALID_PARAMETER;
            pofx_pstate_res_node_v2_t local = {0};
            const pofx_pstate_res_node_v2_t* in = NULL;
            if (valid) {

                RtlCopyMemory(&local, in_buf, sizeof(local));
                in = &local;
            }
            UNREFERENCED_PARAMETER(comp);
            UNREFERENCED_PARAMETER(state);
            UNREFERENCED_PARAMETER(cnt);

            status = po_fx_write_compat_pstate_v2_result(in, status, out_buf, out_buf_size,
                                                         bytes_returned);
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_CLOCK_TEST: {
            NTSTATUS status;
            LONG req = 0;
            /* never execute that path above PASSIVE_LEVEL and never acknowledge an omitted command
             * as completed work. */

            if (!is_passive)
                status = STATUS_INVALID_DEVICE_STATE;
            else if (!in_buf || in_buf_size < sizeof(req))
                status = STATUS_INVALID_PARAMETER;
            else {
                RtlCopyMemory(&req, in_buf, sizeof(req));
                xo_test_handle_req(req);
                status = STATUS_SUCCESS;
            }
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_SET_SEL_EXT_SRC: {
            ULONG dev_id = ctx ? ctx->port_id : 0;
            /* this operation must run at PASSIVE_LEVEL. */

            NTSTATUS status =
                !is_passive
                    ? power_ctrl_write_simple_status(STATUS_INVALID_DEVICE_STATE, out_buf,
                                                     out_buf_size, bytes_returned)
                    : power_ctrl_handle_select_external_src(dev_id, in_buf, in_buf_size, out_buf,
                                                            out_buf_size, bytes_returned);
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_LOG: {
            /* the log-command worker can allocate or access pageable storage; elevated IRQL is not
             * a successful no-op. */

            NTSTATUS status =
                !is_passive ? power_ctrl_write_simple_status(STATUS_INVALID_DEVICE_STATE, out_buf,
                                                             out_buf_size, bytes_returned)
                            : power_ctrl_handle_log_command(in_buf, in_buf_size, out_buf,
                                                            out_buf_size, bytes_returned);
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_SDBUS_PSTATE_REG:
        case PO_FX_POWER_SDBUS_PSTATE_REQ:
        case PO_FX_POWER_SDBUS_PSTATE_MMC_INFO: {
            /* none of these operations has a hardware-backed SD clock/performance executor. Do not
             * advertise performance states or acknowledge transitions until an authenticated
             * hardware provider exists. */

            NTSTATUS status = STATUS_NOT_SUPPORTED;
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        case PO_FX_POWER_GUID_XHCI_SUB_DX_STATE: {
            NTSTATUS status = STATUS_SUCCESS;
            if (!in_buf || in_buf_size < sizeof(ULONG)) {
                status = STATUS_INVALID_PARAMETER;
            } else {
                ULONG s = *(volatile ULONG*)in_buf;
                if (s != 1 && s != 2) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    res_key_t k;
                    k.res_id = 0x49434858u;
                    k.sub_type = ctx ? ctx->port_id : 0;
                    res_value_t v;
                    ULONGLONG t = KeQueryInterruptTime();
                    v.value0 = s;
                    v.value1 = (ULONG)t;
                    v.value2 = (ULONG)(t >> 32);
                    v.value3 = 0;
                    (VOID) res_set(k, v);
                }
            }
            status = power_ctrl_write_simple_status(status, out_buf, out_buf_size, bytes_returned);
            po_fx_trace_power_control(power_control_code, code, status, in_buf_size, out_buf_size);
            return status;
        }

        default:
            break;
    }

    /* unknown GUIDs must not be acknowledged as completed hardware work. */
    po_fx_trace_power_control(power_control_code, code, STATUS_NOT_SUPPORTED, in_buf_size,
                              out_buf_size);
    return STATUS_NOT_SUPPORTED;
}

/* this FDO provides PoFx power controls but has no verified Fx executor. Keep the reg
 * latch through PnP restart: WdfDeviceWdmAssignPowerFrameworkSettings may be called ONCE
 * per WDFDEVICE. */

NTSTATUS po_fx_init(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!ctx || ctx->is_control_dev)
        return STATUS_SUCCESS;

    LONG was = InterlockedCompareExchange(&ctx->po_fx_settings_state, 1, 0);
    if (was == 2)
        return STATUS_SUCCESS;
    if (was != 0)
        return STATUS_DEVICE_BUSY; /* never race a second assignment. */

    WDF_POWER_FRAMEWORK_SETTINGS s;
    WDF_POWER_FRAMEWORK_SETTINGS_INIT(&s);
    s.EvtDeviceWdmPostPoFxRegisterDevice = evt_post_po_fx_reg;
    s.EvtDeviceWdmPrePoFxUnregisterDevice = evt_pre_po_fx_unregister;
    s.Component = NULL;
    s.ComponentActiveConditionCallback = po_fx_active_condition;
    s.ComponentIdleConditionCallback = po_fx_idle_condition;
    s.ComponentIdleStateCallback = NULL;
    s.PowerControlCallback = power_control;
    s.PoFxDeviceContext = (PVOID)dev;

    NTSTATUS status = WdfDeviceWdmAssignPowerFrameworkSettings(dev, &s);
    if (!NT_SUCCESS(status)) {
        InterlockedExchange(&ctx->po_fx_settings_state, 0);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_POWER, "pofx: F0-only settings failed status=%08X",
                    status);
        return status;
    }
    InterlockedExchange(&ctx->po_fx_settings_state, 2);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_POWER, "pofx: F0-only settings assigned");
    return STATUS_SUCCESS;
}

VOID po_fx_deinitialize(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!ctx)
        return;

    ctx->po_handle = NULL;
    /* wdf assignment persists through hardware release and power cycling. */
}
