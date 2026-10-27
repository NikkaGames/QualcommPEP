#pragma once

#include <ntddk.h>

NTSTATUS power_ctrl_init(VOID);
VOID power_ctrl_deinit(VOID);

NTSTATUS power_ctrl_handle_select_external_src(_In_ ULONG dev_id, _In_opt_ const VOID* in_buf,
                                               _In_ size_t in_bytes,
                                               _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                               _In_ size_t out_bytes, _Out_ size_t* written);

NTSTATUS power_ctrl_handle_log_command(_In_opt_ const VOID* in_buf, _In_ size_t in_bytes,
                                       _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                       _In_ size_t out_bytes, _Out_ size_t* written);

NTSTATUS power_ctrl_write_simple_status(_In_ NTSTATUS status,
                                        _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                        _In_ size_t out_bytes, _Out_ size_t* written);
