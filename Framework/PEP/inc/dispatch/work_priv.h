#pragma once
#include "driver.h"
#include <pepfx.h>
#include "node_types.h"
#include "work_types.h"

BOOLEAN pep_work_init_if_needed(VOID);
BOOLEAN pep_power_ctrl_ctx_init_if_needed(VOID);
pep_work_power_control_ctx_t* pep_alloc_power_control_ctx(VOID);
VOID pep_free_power_control_ctx(_In_opt_ pep_work_power_control_ctx_t* ctx);
BOOLEAN pep_sync_dpm_begin(_In_opt_ pep_devnode_t* candidate);
VOID pep_sync_dpm_end(_In_ pep_devnode_t* node);
BOOLEAN pep_enqueue_work(_In_ ULONG work_type, _In_ PVOID data, _In_ BOOLEAN add_to_head);
VOID pep_free_work_item(_In_ pep_work_item_t* item);
BOOLEAN pep_result_init_if_needed(VOID);
PVOID pep_alloc_result(_In_ ULONG size);
ULONG_PTR pep_work(_Inout_opt_ PVOID data);
ULONG_PTR pep_power_control_req(_Inout_opt_ PVOID data);
ULONG_PTR pep_power_control_complete(_Inout_opt_ PVOID data);
ULONG_PTR pep_parse_error_data(_Inout_opt_ PVOID data);
