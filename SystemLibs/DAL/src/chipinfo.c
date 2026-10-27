#include "chipinfo.h"
#include "acpi.h"

static volatile LONG g_init;
static chipinfo_t g_info;

static NTSTATUS try_dsm_fill(_In_ WDFDEVICE dev, _Inout_ chipinfo_t* out)
{

    UNICODE_STRING guid_str = {0};
    NTSTATUS status = registry_query_unicode_string_alloc(L"DsmGuid", &guid_str, 64);
    if (!NT_SUCCESS(status))
        return STATUS_NOT_FOUND;

    GUID g;
    status = RtlGUIDFromString(&guid_str, &g);
    ExFreePool(guid_str.Buffer);

    if (!NT_SUCCESS(status))
        return STATUS_NOT_FOUND;

    ULONG rev = registry_query_u_long(L"DsmRevision", 0);
    ULONG f_id = registry_query_u_long(L"DsmFuncChipId", 0);
    ULONG f_fam = registry_query_u_long(L"DsmFuncChipFamily", 0);
    ULONG f_ver = registry_query_u_long(L"DsmFuncChipVersion", 0);
    ULONG f_sku = registry_query_u_long(L"DsmFuncChipSku", 0);

    ULONG v = 0;
    if (f_id) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_id, &v)))
            out->chip_id = v;
    }
    if (f_fam) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_fam, &v)))
            out->chip_family = v;
    }
    if (f_ver) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_ver, &v)))
            out->chip_version = v;
    }
    if (f_sku) {
        if (NT_SUCCESS(acpi_eval_dsm_ulong(dev, &g, rev, f_sku, &v)))
            out->chip_sku = v;
    }

    return STATUS_SUCCESS;
}

NTSTATUS chip_info_init_from_dev(_In_ WDFDEVICE dev)
{
    if (InterlockedCompareExchange(&g_init, 1, 0) != 0)
        return STATUS_SUCCESS;

    RtlZeroMemory(&g_info, sizeof(g_info));

    (VOID) try_dsm_fill(dev, &g_info);

    if (g_info.chip_id == 0)
        g_info.chip_id = registry_query_u_long(L"ChipId", 0);
    if (g_info.chip_family == 0)
        g_info.chip_family = registry_query_u_long(L"ChipFamily", 0);
    if (g_info.chip_version == 0)
        g_info.chip_version = registry_query_u_long(L"ChipVersion", 0);
    if (g_info.chip_sku == 0)
        g_info.chip_sku = registry_query_u_long(L"ChipSku", 0);

    return STATUS_SUCCESS;
}

chipinfo_t chip_info_get(VOID)
{
    return g_info;
}
