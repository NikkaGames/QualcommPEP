#include "etw.h"
#include <TraceLoggingProvider.h>

TRACELOGGING_DEFINE_PROVIDER(g_provider, "qcpep",
                             (0xa64d8f5b, 0x6d53, 0x4f4b, 0x9c, 0x53, 0xeb, 0xf2, 0xcf, 0xa9, 0xcc,
                              0x6c));

static volatile LONG g_provider_enabled;

NTSTATUS etw_reg(VOID)
{
    if (InterlockedCompareExchange(&g_provider_enabled, 1, 0) != 0)
        return STATUS_SUCCESS;

    NTSTATUS status = TraceLoggingRegister(g_provider);
    if (!NT_SUCCESS(status))
        InterlockedExchange(&g_provider_enabled, 0);
    return status;
}

VOID etw_unregister(VOID)
{
    if (InterlockedCompareExchange(&g_provider_enabled, 0, 1) == 1)
        TraceLoggingUnregister(g_provider);
}

VOID etw_write_ioctl(_In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG in_len, _In_ ULONG out_len)
{
    if (!g_provider_enabled)
        return;

    TraceLoggingWrite(g_provider, "Ioctl", TraceLoggingUInt32(code, "Code"),
                      TraceLoggingInt32(status, "Status"), TraceLoggingUInt32(in_len, "InLen"),
                      TraceLoggingUInt32(out_len, "OutLen"));
}

VOID etw_write_power_control(_In_ ULONG guid_data1, _In_ ULONG code_type, _In_ NTSTATUS status,
                             _In_ ULONG in_len, _In_ ULONG out_len)
{
    if (!g_provider_enabled)
        return;

    TraceLoggingWrite(g_provider, "PowerControl", TraceLoggingUInt32(guid_data1, "GuidData1"),
                      TraceLoggingUInt32(code_type, "CodeType"),
                      TraceLoggingInt32(status, "Status"), TraceLoggingUInt32(in_len, "InLen"),
                      TraceLoggingUInt32(out_len, "OutLen"));
}
