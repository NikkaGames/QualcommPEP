#pragma once
#include "driver.h"
#include <pepfx.h>
#include "fstate_types.h"
#include "work_types.h"

VOID pep_notify_comp_idle_state_work(_Inout_ pep_notify_comp_idle_state_t* s,
                                     _In_opt_ pep_work_information_t* wi);
ULONG_PTR pep_notify_comp_idle_state(_Inout_opt_ PVOID data);
ULONG_PTR pep_dev_idle_constraints(_Inout_opt_ PVOID data);
ULONG_PTR pep_comp_idle_constraints(_Inout_opt_ PVOID data);
