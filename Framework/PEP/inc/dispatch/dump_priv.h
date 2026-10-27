#pragma once
#include "driver.h"
#include <pepfx.h>
#include "node_types.h"

BOOLEAN pep_is_bugcheck_enabled(_In_ ULONG scope);
VOID pep_reg_bugcheck_callback_if_needed(VOID);
VOID pep_unregister_bugcheck_callback_if_needed(VOID);
VOID pep_transition_debugger_critical_res(_In_ UCHAR transition);
__declspec(noreturn) VOID pep_bug_check(_In_ ULONG src, _In_opt_ PVOID ctx);
__declspec(noreturn) VOID pep_bug_check_power_control(_In_opt_ PVOID ctx);
BOOLEAN pep_has_verified_crashdump_backend(VOID);
BOOLEAN pep_enter_lockless_polling_mode(VOID);
VOID pep_crashdump_rpmh_prep(VOID);
BOOLEAN pep_set_crash_dump_dev_state(_Inout_opt_ pep_devnode_t* node);
BOOLEAN pep_ocd_timer_armed(VOID);
VOID pep_display_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_d_state_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_f_state_ocd_test(_In_opt_ pep_devnode_t* node);
VOID pep_p_state_ocd_test(_In_opt_ pep_devnode_t* node, _In_ ULONG comp, _In_ ULONG p_state);
VOID pep_ocd_init(VOID);
VOID pep_ocd_deinit(VOID);
ULONG_PTR pep_reg_debugger(_Inout_opt_ PVOID data);
ULONG_PTR pep_reg_crashdump_dev(_Inout_opt_ PVOID data);
