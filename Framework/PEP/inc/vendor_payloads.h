#pragma once
#include "driver.h"

NTSTATUS build_pep_stats(_In_ p_dev_ctx_t ctx, _Inout_updates_bytes_(120) PVOID buf,
                         _In_ ULONG size);
NTSTATUS build_subsystem_status(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(320) PVOID buf,
                                _In_ ULONG size);
NTSTATUS build_xo_clock_status(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(3844) PVOID buf,
                               _In_ ULONG size);
NTSTATUS build_dev_info(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(15376) PVOID buf, _In_ ULONG size);
NTSTATUS build_pw_cal_rundown(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(51212) PVOID buf,
                              _In_ ULONG size);
NTSTATUS build_clock_debug_list(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(83984) PVOID buf,
                                _In_ ULONG size, _Out_ PULONG bytes_written);
