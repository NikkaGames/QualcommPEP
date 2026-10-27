#include "io_target.h"
#include "state_txn.h"
#include "gpio.h"
#include "provider_select.h"

static WDFWAITLOCK g_fwd_lock;
static BOOLEAN g_fwd_enabled;
static BOOLEAN g_fwd_guid_valid;
static GUID g_fwd_guid_list[8];
static ULONG g_fwd_guid_cnt;
static ULONG g_fwd_timeout_ms;
static BOOLEAN g_fwd_configured;

/* no generic GUID enumeration: only the exact PMIC GPIO provider is used. */

static const GUID g_pmic_gpio_interface = {
    0x5186fcea, 0x68fc, 0x420a, {0x94, 0x00, 0x60, 0x1a, 0x6d, 0x0c, 0xa9, 0x88}};

static NTSTATUS parse_guid_list(_Inout_ UNICODE_STRING* s, _Out_writes_(max_cnt) GUID* out_guids,
                                _In_ ULONG max_cnt, _Out_ PULONG cnt)
{
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;

    if (!s || !s->Buffer || s->Length == 0)
        return STATUS_NOT_FOUND;
    if (!out_guids || max_cnt == 0)
        return STATUS_INVALID_PARAMETER;

    PWCH buf = s->Buffer;
    USHORT len = s->Length / sizeof(WCHAR);
    ULONG n = 0;
    USHORT i = 0;

    while (i < len && n < max_cnt) {
        while (i < len && (buf[i] == L' ' || buf[i] == L'\t' || buf[i] == L';' || buf[i] == L','))
            i++;
        if (i >= len || !buf[i])
            break;

        USHORT start = i;
        while (i < len && buf[i] && buf[i] != L';' && buf[i] != L',')
            i++;
        USHORT end = i;
        while (end > start && (buf[end - 1] == L' ' || buf[end - 1] == L'\t'))
            end--;
        if (end <= start)
            continue;

        UNICODE_STRING token;
        token.Buffer = &buf[start];
        token.Length = (USHORT)((end - start) * sizeof(WCHAR));
        token.MaximumLength = token.Length;

        GUID g;
        NTSTATUS st = RtlGUIDFromString(&token, &g);
        if (NT_SUCCESS(st))
            out_guids[n++] = g;
    }

    *cnt = n;
    return (n > 0) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS load_guids(_Out_writes_(max_cnt) GUID* out_guids, _In_ ULONG max_cnt,
                           _Out_ PULONG cnt)
{
    if (!cnt)
        return STATUS_INVALID_PARAMETER;
    *cnt = 0;

    UNICODE_STRING s = {0};
    NTSTATUS st = registry_query_unicode_string_alloc(L"ForwardInterfaceGuid", &s, 256);
    if (!NT_SUCCESS(st) || !s.Buffer || s.Length == 0)
        return STATUS_NOT_FOUND;

    st = parse_guid_list(&s, out_guids, max_cnt, cnt);
    ExFreePool(s.Buffer);

    return st;
}

BOOLEAN forward_enabled(VOID)
{
    return g_fwd_enabled ? TRUE : FALSE;
}

/* a globally cached target cannot survive PnP stop/removal of that device. */

NTSTATUS forward_global_init(VOID)
{
    if (g_fwd_lock)
        return STATUS_SUCCESS;
    /* not lazily created by concurrent PnP callbacks. */

    return WdfWaitLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_fwd_lock);
}

NTSTATUS forward_init_from_dev(_In_ WDFDEVICE dev)
{
    UNREFERENCED_PARAMETER(dev);
    if (!g_fwd_lock)
        return STATUS_NOT_FOUND;

    WdfWaitLockAcquire(g_fwd_lock, NULL);

    if (!g_fwd_configured) {
        ULONG cnt = 0;
        GUID guids[RTL_NUMBER_OF(g_fwd_guid_list)];
        RtlZeroMemory(guids, sizeof(guids));
        g_fwd_enabled = (registry_query_u_long(L"ForwardIoctlsEnable", 0) != 0);
        g_fwd_timeout_ms = registry_query_u_long(L"ForwardTimeoutMs", 1000);
        if (g_fwd_timeout_ms == 0u)
            g_fwd_timeout_ms = 1000u;
        /* pmic GPIO has its own fixed, uniquely selected provider path. */

        if (g_fwd_enabled && NT_SUCCESS(load_guids(guids, RTL_NUMBER_OF(guids), &cnt)) &&
            cnt > 0u) {
            RtlCopyMemory(g_fwd_guid_list, guids, sizeof(guids));
            g_fwd_guid_cnt = cnt;
            g_fwd_guid_valid = TRUE;
        } else {
            g_fwd_guid_valid = FALSE;
            g_fwd_guid_cnt = 0u;
            g_fwd_enabled = FALSE;
            RtlZeroMemory(g_fwd_guid_list, sizeof(g_fwd_guid_list));
        }
        g_fwd_configured = TRUE;
    }
    WdfWaitLockRelease(g_fwd_lock);
    return STATUS_SUCCESS;
}

VOID forward_prepare_for_hibernate(VOID)
{
    /* pnp teardown and hibernate cannot leave a stale cached provider handle. */
}

VOID forward_deinit(VOID)
{
    WDFWAITLOCK lock = g_fwd_lock;
    if (!lock)
        return;
    /* it is not a substitute for WDF request rundown. */

    WdfWaitLockAcquire(lock, NULL);
    g_fwd_enabled = FALSE;
    g_fwd_guid_valid = FALSE;
    g_fwd_guid_cnt = 0u;
    g_fwd_configured = FALSE;
    RtlZeroMemory(g_fwd_guid_list, sizeof(g_fwd_guid_list));
    g_fwd_timeout_ms = 0u;
    WdfWaitLockRelease(lock);
    g_fwd_lock = NULL;
    WdfObjectDelete(lock);
}

BOOLEAN forward_is_interesting_ioctl(_In_ ULONG ioctl)
{

    if ((ioctl & 0xFFFF0000u) == 0x80020000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80030000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x800B0000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80100000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80190000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x801C0000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80200000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80260000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80040000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80110000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x802D0000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80340000u)
        return TRUE;
    if ((ioctl & 0xFFFF0000u) == 0x80350000u)
        return TRUE;
    return FALSE;
}

static NTSTATUS send_to_guid(_In_ WDFDEVICE dev, _In_ const GUID* interface_guid,
                             _In_ BOOLEAN require_unique, _In_ ULONG ioctl, _In_opt_ PVOID in_buf,
                             _In_ ULONG in_len, _Out_opt_ PVOID out_buf, _In_ ULONG out_len,
                             _Out_opt_ PULONG bytes_returned)
{
    if (bytes_returned)
        *bytes_returned = 0;

    if (!interface_guid)
        return STATUS_INVALID_PARAMETER;

    PWSTR multi = NULL;
    NTSTATUS st = IoGetDeviceInterfaces((LPGUID)interface_guid, NULL, 0, &multi);
    if (!NT_SUCCESS(st) || !multi)
        return STATUS_NOT_FOUND;

    if (require_unique) {

        size_t first_units = wcslen(multi);
        if (first_units == 0u || first_units >= 32767u ||
            !is_unique_interface_multi_sz((const uint16_t*)multi, first_units + 2u)) {
            ExFreePool(multi);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }

    WDF_MEMORY_DESCRIPTOR in_desc;
    WDF_MEMORY_DESCRIPTOR out_desc;
    WDF_MEMORY_DESCRIPTOR* p_in = NULL;
    WDF_MEMORY_DESCRIPTOR* p_out = NULL;
    if (in_buf && in_len) {
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in_desc, in_buf, in_len);
        p_in = &in_desc;
    }
    if (out_buf && out_len) {
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out_desc, out_buf, out_len);
        p_out = &out_desc;
    }

    WDF_REQUEST_SEND_OPTIONS opt;
    WDF_REQUEST_SEND_OPTIONS_INIT(&opt, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    opt.Timeout = WDF_REL_TIMEOUT_IN_MS(g_fwd_timeout_ms ? g_fwd_timeout_ms : 1000);

    NTSTATUS best = STATUS_NOT_FOUND;
    PWSTR p_multi = multi;
    while (*p_multi) {
        UNICODE_STRING link;
        RtlInitUnicodeString(&link, p_multi);

        WDFIOTARGET tgt = NULL;
        st = WdfIoTargetCreate(dev, WDF_NO_OBJECT_ATTRIBUTES, &tgt);
        if (!NT_SUCCESS(st)) {
            best = st;
            break;
        }

        WDF_IO_TARGET_OPEN_PARAMS openp;
        WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openp, &link, GENERIC_READ | GENERIC_WRITE);
        openp.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
        openp.CreateDisposition = FILE_OPEN;
        openp.FileAttributes = FILE_ATTRIBUTE_NORMAL;

        st = WdfIoTargetOpen(tgt, &openp);
        if (NT_SUCCESS(st)) {
            ULONG_PTR received = 0;
            st = WdfIoTargetSendIoctlSynchronously(tgt, NULL, ioctl, p_in, p_out, &opt, &received);
            if (NT_SUCCESS(st) && received > MAXULONG)
                st = STATUS_INTEGER_OVERFLOW;
            if (NT_SUCCESS(st)) {
                if (bytes_returned)
                    *bytes_returned = (ULONG)received;
                WdfIoTargetClose(tgt);
                WdfObjectDelete(tgt);
                best = st;
                break;
            }
        }

        if (best == STATUS_NOT_FOUND || NT_SUCCESS(best))
            best = st;

        if (WdfIoTargetGetState(tgt) != WdfIoTargetClosed)
            WdfIoTargetClose(tgt);
        WdfObjectDelete(tgt);

        p_multi += wcslen(p_multi) + 1;
    }

    ExFreePool(multi);
    return best;
}

NTSTATUS pmic_gpio_send(_In_ WDFDEVICE dev, _In_ ULONG ioctl, _In_opt_ PVOID in_buf,
                        _In_ ULONG in_len, _Out_opt_ PVOID out_buf, _In_ ULONG out_len,
                        _Out_opt_ PULONG bytes_returned)
{
    if (bytes_returned)
        *bytes_returned = 0;
    if (!dev || !in_buf || !out_buf)
        return STATUS_INVALID_PARAMETER;

    /* validate the complete wire ABI before opening a foreign driver. This route must never
     * accept a generic PMIC, GPIO or PEP command. */

    ULONG expected_input = 12u;
    ULONG expected_output = 4u;
    switch (ioctl) {
        case IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT:
            expected_input = 20u;
            break;
        case IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT:
            expected_input = 28u;
            break;
        case IOCTL_PM_GPIO_GET_GPIO_STATE:
            expected_output = 8u;
            break;
        case IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE:
        case IOCTL_QCPEP_GPIO_PMIC_SET_PULL:
        case IOCTL_QCPEP_GPIO_PMIC_SET_DRIVE:
        case IOCTL_PM_GPIO_SET_GPIO_OUTPUT:
            break;
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (in_len != expected_input || out_len != expected_output)
        return STATUS_INVALID_BUFFER_SIZE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    /* if it is absent, propagate failure; never declare an AML d0/d3 vote complete without the
     * provider's own result. */

    return send_to_guid(dev, &g_pmic_gpio_interface, TRUE, ioctl, in_buf, in_len, out_buf, out_len,
                        bytes_returned);
}

NTSTATUS pmic_gpio_query_raw_state(_In_ WDFDEVICE dev, _In_ const pm_gpio_state_query_t* query,
                                   _Out_ pm_gpio_state_raw_t* raw_state)
{
    UCHAR req[PM_GPIO_GET_GPIO_STATE_INPUT_BYTES];
    UCHAR response[PM_GPIO_GET_GPIO_STATE_OUTPUT_BYTES];
    ULONG returned = 0;
    NTSTATUS status;

    if (!raw_state)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(raw_state, sizeof(*raw_state));
    if (!dev || !query)
        return STATUS_INVALID_PARAMETER;
    if (!pm_gpio_encode_state_query(query, req, sizeof(req)))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(response, sizeof(response));
    status = pmic_gpio_send(dev, IOCTL_PM_GPIO_GET_GPIO_STATE, req, sizeof(req), response,
                            sizeof(response), &returned);
    if (!NT_SUCCESS(status))
        return status;
    if (returned != sizeof(response))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (!pm_gpio_decode_raw_state(response, returned, raw_state))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS pmic_gpio_apply_plan_action(_In_ WDFDEVICE dev, _In_ const power_action_t* action)
{
    UCHAR input[PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES];
    ULONG output = 0;
    ULONG received = 0;
    pm_gpio_digital_output_t req;
    NTSTATUS status;

    if (!action || action->kind != POWER_PMIC_GPIO ||
        action->u.pmic_gpio.ioctl != IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT)
        return STATUS_INVALID_DEVICE_REQUEST;

    req.pmic_number = action->u.pmic_gpio.fields[0];
    req.gpio = action->u.pmic_gpio.fields[1];
    req.out_buf_config = action->u.pmic_gpio.fields[2];
    req.voltage_src = action->u.pmic_gpio.fields[3];
    req.src = action->u.pmic_gpio.fields[4];
    req.out_buf_strength = action->u.pmic_gpio.fields[5];
    req.i_src_pulls = action->u.pmic_gpio.fields[6];

    if (!pm_gpio_encode_digital_output(&req, input, sizeof(input)))
        return STATUS_INVALID_PARAMETER;

    status = pmic_gpio_send(dev, action->u.pmic_gpio.ioctl, input, sizeof(input), &output,
                            sizeof(output), &received);
    if (!NT_SUCCESS(status))
        return status;
    /* the native PMIC configuration IOCTL completes through NTSTATUS with no output payload.
     * providers returning a result word must still report zero. */
    if (received == 0u)
        return STATUS_SUCCESS;
    if (received != sizeof(output) || output != 0u)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    return STATUS_SUCCESS;
}

NTSTATUS forward_send_guid(_In_ WDFDEVICE dev, _In_ const GUID* interface_guid, _In_ ULONG ioctl,
                           _In_opt_ PVOID in_buf, _In_ ULONG in_len, _Out_opt_ PVOID out_buf,
                           _In_ ULONG out_len, _Out_opt_ PULONG bytes_returned)
{
    BOOLEAN enabled;
    if (bytes_returned)
        *bytes_returned = 0;
    if (!g_fwd_lock)
        return STATUS_NOT_FOUND;
    WdfWaitLockAcquire(g_fwd_lock, NULL);
    enabled = g_fwd_enabled && g_fwd_configured;
    WdfWaitLockRelease(g_fwd_lock);
    if (!enabled)
        return STATUS_NOT_FOUND;
    /* do not hold the forwarding lock while invoking a foreign WDF driver: its completion path
     * can call back into this PEP. Routing is immutable after initialization; WDF request
     * rundown governs unload. */

    return send_to_guid(dev, interface_guid, TRUE, ioctl, in_buf, in_len, out_buf, out_len,
                        bytes_returned);
}

NTSTATUS forward_send(_In_ WDFDEVICE dev, _In_ ULONG ioctl, _In_opt_ PVOID in_buf,
                      _In_ ULONG in_len, _Out_opt_ PVOID out_buf, _In_ ULONG out_len,
                      _Out_opt_ PULONG bytes_returned)
{
    NTSTATUS status = STATUS_NOT_FOUND;
    GUID guids[RTL_NUMBER_OF(g_fwd_guid_list)];
    ULONG cnt = 0, i;
    if (bytes_returned)
        *bytes_returned = 0;
    if (!g_fwd_lock)
        return STATUS_NOT_FOUND;

    WdfWaitLockAcquire(g_fwd_lock, NULL);
    if (g_fwd_configured && g_fwd_enabled && g_fwd_guid_valid && g_fwd_guid_cnt > 0u &&
        g_fwd_guid_cnt <= RTL_NUMBER_OF(g_fwd_guid_list)) {
        cnt = g_fwd_guid_cnt;
        RtlCopyMemory(guids, g_fwd_guid_list, cnt * sizeof(GUID));
    }
    WdfWaitLockRelease(g_fwd_lock);

    /* a synchronous external IOCTL must never run under a driver-global forwarding mutex.
     * snapshot immutable explicit routing once, then release the lock before foreign
     * PnP/provider code can re-enter. */

    for (i = 0; i < cnt; ++i) {
        /* reject ambiguous provider instances, rather than choosing one. */

        status = send_to_guid(dev, &guids[i], TRUE, ioctl, in_buf, in_len, out_buf, out_len,
                              bytes_returned);
        if (status != STATUS_NOT_FOUND)
            break;
    }
    return status;
}
