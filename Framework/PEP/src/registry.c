#include "driver.h"

static WDFKEY g_params_key;

NTSTATUS registry_init(_In_ WDFDRIVER driver)
{
    g_params_key = NULL;
    return WdfDriverOpenParametersRegistryKey(driver, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES,
                                              &g_params_key);
}

VOID registry_deinit(VOID)
{
    if (g_params_key) {
        WdfRegistryClose(g_params_key);
        g_params_key = NULL;
    }
}

ULONG registry_query_u_long(_In_z_ PCWSTR name, _In_ ULONG default_value)
{
    ULONG v = default_value;
    if (g_params_key) {
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, name);
        (VOID) WdfRegistryQueryULong(g_params_key, &us, &v);
    }
    return v;
}

NTSTATUS registry_read_config(_Out_ QCPEP_CONFIG* cfg)
{
    if (!cfg)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(cfg, sizeof(*cfg));

    cfg->BranchTrackEnabled = registry_query_u_long(L"BranchTrackEnabled", 0);

    {
        ULONG v = 0;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"DirectedPState",
                                                        L"EnableDirectedPStateFeatures", &v)))
            cfg->EnableDirectedPStateFeatures = v;
        else
            cfg->EnableDirectedPStateFeatures =
                registry_query_u_long(L"EnableDirectedPStateFeatures", 1);
    }

    {
        ULONG v = 0;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(L"PARKSEL", L"EnableParkSelection", &v)))
            cfg->EnableParkSelection = v;
        else
            cfg->EnableParkSelection = registry_query_u_long(L"EnableParkSelection", 1);
    }
    cfg->EnablePEPOCDTest = registry_query_u_long(L"EnablePEPOCDTest", 0);
    cfg->EnablePEPOCDTestLowPerf = registry_query_u_long(L"EnablePEPOCDTestLowPerf", 0);
    cfg->PepBcl_Notification_Level = registry_query_u_long(L"PepBcl_Notification_Level", 0);

    return STATUS_SUCCESS;
}

NTSTATUS registry_query_unicode_string_alloc(_In_z_ PCWSTR name, _Out_ UNICODE_STRING* out,
                                             _In_ USHORT max_chars)
{
    if (!out)
        return STATUS_INVALID_PARAMETER;

    out->Length = 0;
    out->MaximumLength = (USHORT)(max_chars * sizeof(WCHAR));
    out->Buffer = NULL;

    if (!g_params_key)
        return STATUS_DEVICE_NOT_READY;

    UNICODE_STRING value_name;
    RtlInitUnicodeString(&value_name, name);

    USHORT required = 0;
    NTSTATUS status = WdfRegistryQueryUnicodeString(g_params_key, &value_name, &required, NULL);
    if (status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(status))
        return status;

    if (required == 0)
        return STATUS_NOT_FOUND;

    out->Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED, out->MaximumLength, POOL_TAG);
    if (!out->Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(out->Buffer, out->MaximumLength);

    UNICODE_STRING tmp;
    tmp.Buffer = out->Buffer;
    tmp.Length = 0;
    tmp.MaximumLength = out->MaximumLength;

    USHORT bytes = tmp.MaximumLength;
    status = WdfRegistryQueryUnicodeString(g_params_key, &value_name, &bytes, &tmp);
    if (!NT_SUCCESS(status)) {
        ExFreePool(out->Buffer);
        out->Buffer = NULL;
        out->Length = 0;
        out->MaximumLength = 0;
        return status;
    }

    if (tmp.Length == 0 && tmp.Buffer)
        tmp.Length = (USHORT)(wcslen(tmp.Buffer) * sizeof(WCHAR));

    out->Length = tmp.Length;
    return STATUS_SUCCESS;
}

static NTSTATUS registry_open_sub_key_path(_In_z_ PCWSTR sub_key_path, _Out_ WDFKEY* out_key)
{
    if (!out_key)
        return STATUS_INVALID_PARAMETER;
    *out_key = NULL;
    if (!g_params_key)
        return STATUS_DEVICE_NOT_READY;
    if (!sub_key_path || !*sub_key_path)
        return STATUS_INVALID_PARAMETER;

    WDFKEY cur = g_params_key;
    WDFKEY next = NULL;

    const WCHAR* p = sub_key_path;
    WCHAR token[260];
    while (*p) {
        while (*p == L'\\')
            ++p;
        if (!*p)
            break;
        size_t n = 0;
        while (p[n] && p[n] != L'\\' && n < (sizeof(token) / sizeof(token[0]) - 1)) {
            token[n] = p[n];
            ++n;
        }
        token[n] = 0;
        p += n;
        UNICODE_STRING us;
        RtlInitUnicodeString(&us, token);

        NTSTATUS st = WdfRegistryOpenKey(cur, &us, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES, &next);
        if (cur != g_params_key)
            WdfRegistryClose(cur);
        cur = NULL;

        if (!NT_SUCCESS(st))
            return st;

        cur = next;
        next = NULL;
    }

    if (cur == g_params_key)
        return STATUS_INVALID_PARAMETER;

    *out_key = cur;
    return STATUS_SUCCESS;
}

NTSTATUS registry_query_u_long_in_sub_key(_In_z_ PCWSTR sub_key_path, _In_z_ PCWSTR value_name,
                                          _Out_ PULONG value)
{
    if (!value)
        return STATUS_INVALID_PARAMETER;
    *value = 0;

    if (!g_params_key)
        return STATUS_DEVICE_NOT_READY;

    WDFKEY key = NULL;
    NTSTATUS st = registry_open_sub_key_path(sub_key_path, &key);
    if (!NT_SUCCESS(st))
        return st;

    UNICODE_STRING vn;
    RtlInitUnicodeString(&vn, value_name);
    ULONG v = 0;
    st = WdfRegistryQueryULong(key, &vn, &v);
    WdfRegistryClose(key);

    if (!NT_SUCCESS(st))
        return st;

    *value = v;
    return STATUS_SUCCESS;
}

NTSTATUS registry_set_u_long(_In_ PCWSTR name, _In_ ULONG value)
{
    if (!g.driver)
        return STATUS_DEVICE_NOT_READY;

    WDFKEY h_key = NULL;
    NTSTATUS status = WdfDriverOpenParametersRegistryKey(g.driver, KEY_SET_VALUE,
                                                         WDF_NO_OBJECT_ATTRIBUTES, &h_key);
    if (!NT_SUCCESS(status))
        return status;

    UNICODE_STRING n;
    RtlInitUnicodeString(&n, name);
    status = WdfRegistryAssignULong(h_key, &n, value);
    WdfRegistryClose(h_key);
    return status;
}
