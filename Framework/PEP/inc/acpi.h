#pragma once
#include "driver.h"
#include <acpiioct.h>
#include "state_txn.h"
#include "power_desc.h"

NTSTATUS acpi_eval_method_no_args_ulong(_In_ WDFDEVICE dev, _In_ ULONG method_name,
                                        _Out_ PULONG value);
NTSTATUS acpi_eval_method_no_args_string(_In_ WDFDEVICE dev, _In_ ULONG method_name,
                                         _Out_ UNICODE_STRING* out, _In_ USHORT max_chars);

NTSTATUS resolve_port_id(_In_ WDFDEVICE dev, _Out_ PULONG port_id);

NTSTATUS acpi_eval_dsm_ulong(_In_ WDFDEVICE dev, _In_ const GUID* guid, _In_ ULONG revision,
                             _In_ ULONG function_idx, _Out_ PULONG value);

NTSTATUS acpi_eval_dsm_ulong_with_arg_ulong(_In_ WDFDEVICE dev, _In_ const GUID* guid,
                                            _In_ ULONG revision, _In_ ULONG function_idx,
                                            _In_ ULONG arg_value, _Out_ PULONG value);

NTSTATUS acpi_eval_dsm_package_ulongs(_In_ WDFDEVICE dev, _In_ const GUID* guid,
                                      _In_ ULONG revision, _In_ ULONG function_idx,
                                      _In_reads_opt_(arg_cnt) const ULONG* args, _In_ ULONG arg_cnt,
                                      _Out_writes_to_(value_capacity, *value_cnt) PULONG values,
                                      _In_ ULONG value_capacity, _Out_ PULONG value_cnt);

NTSTATUS acpi_molokai_prepare_lpi(_In_ WDFDEVICE dev);

/* only discovers method names; it NEVER initiates a d0/d3 vote. */
#define MOLOKAI_STND_METHOD_MAX 32u
NTSTATUS acpi_query_stnd_method_names(_In_ WDFDEVICE dev,
                                      _Out_writes_bytes_(capacity * 5) CHAR* names,
                                      _In_ ULONG capacity, _Out_ PULONG name_cnt);

/* returns STATUS_SUCCESS only when every d0/d3 operation is validated; performs NO actual
 * hardware votes or power transition. dev must be the WDF device bound to PEP0's ACPI PDO. */

NTSTATUS acpi_compile_molokai_lpmx(_In_ WDFDEVICE pep_dev, _In_z_ const char* dev_path,
                                   _In_ ULONG d_state, _Out_ power_plan_t* plan);

/* never takes hardware ownership or executes votes. The caller must supply nonpaged plan
 * storage; this is not a PoFx callback. */

NTSTATUS acpi_compile_molokai_dev_power(_In_ WDFDEVICE pep_dev, _In_z_ const char* dev_path,
                                        _In_ ULONG target_d_state, _Out_ power_plan_t* plan,
                                        _Out_writes_(5) CHAR selected_method[5]);

struct res_graph;

/* pep_dev must be the WDF device attached to the PEP0 ACPI PDO; this does not discover a
 * PnP device ownership ID and does not run power transitions. graph must already be
 * initialized and provider callbacks bound before calling. */

NTSTATUS acpi_populate_molokai_res_graph(_In_ WDFDEVICE pep_dev, _Inout_ struct res_graph* graph,
                                         _Out_ power_plan_t* plan_scratch);
