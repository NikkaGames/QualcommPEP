#pragma once
#include <ntddk.h>

NTSTATUS etw_reg(VOID);
VOID etw_unregister(VOID);

VOID etw_write_ioctl(_In_ ULONG code, _In_ NTSTATUS status, _In_ ULONG in_len, _In_ ULONG out_len);
VOID etw_write_power_control(_In_ ULONG guid_data1, _In_ ULONG code_type, _In_ NTSTATUS status,
                             _In_ ULONG in_len, _In_ ULONG out_len);
