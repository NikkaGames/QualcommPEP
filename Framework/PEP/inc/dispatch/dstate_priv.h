#pragma once
#include "driver.h"
#include <pepfx.h>
#include "dstate_types.h"
#include "node_types.h"

ULONG_PTR pep_prepare_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_abandon_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_dev(_Inout_opt_ PVOID data);
ULONG_PTR pep_unregister_dev(_Inout_opt_ PVOID data);
BOOLEAN pep_dpm_begin_transition(_Inout_ pep_devnode_t* node,
                                 _In_ DEVICE_POWER_STATE desired_state);
VOID pep_dpm_finish_transition(_Inout_ pep_devnode_t* node);
VOID pep_dev_power_state_work(_Inout_ pep_dev_power_state_t* p);
ULONG_PTR pep_dev_power_state(_Inout_opt_ PVOID data);
