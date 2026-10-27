#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef struct pep_ppm_platform_state_residency {
    ULONGLONG residency;
    ULONGLONG transition_cnt;
} pep_ppm_platform_state_residency_t;

typedef struct pep_ppm_platform_state_residencies {
    ULONG cnt;
    pep_ppm_platform_state_residency_t* states;
} pep_ppm_platform_state_residencies_t;

typedef struct pep_ppm_query_veto_reason {
    ULONG veto_reason;
    USHORT name_size;
    PWSTR name;
} pep_ppm_query_veto_reason_t;

typedef struct pep_veto_ctx {
    volatile LONG boot_veto_enumerated;
    volatile LONG64 ref_counts[MAX_VETO_REASONS + 1];
} pep_veto_ctx_t;

typedef struct pep_residency_ctx {
    volatile LONG in_drips;
    volatile LONG64 drips_residency100ns;
    volatile LONG64 drips_transition_cnt;
    ULONGLONG drips_enter_time100ns;
} pep_residency_ctx_t;

typedef struct pep_mode_residency_ctx {
    volatile LONG in_state;
    volatile LONG64 residency100ns;
    volatile LONG64 transition_cnt;
    ULONGLONG enter_time100ns;
} pep_mode_residency_ctx_t;

typedef struct idle_registry_veto_entry {
    PCWSTR lpr_pattern;
    PCWSTR mode_pattern;
    ULONG registry_feature_id;
    ULONG mask;
    UCHAR force_disable;
} idle_registry_veto_entry_t;

typedef struct pep_pending_veto {
    SLIST_ENTRY link;
    ULONG lpr_id;
    ULONG mode_id;
    ULONG veto_id;
    UCHAR increment;
    UCHAR reserved[3];
} pep_pending_veto_t;

typedef struct idle_qupv3_geni_s_regs {
    volatile ULONG* qupv3_x_se1_geni_s_cmd0;
    volatile ULONG* qupv3_x_se1_geni_s_cmd_ctrl_reg;
    volatile ULONG* qupv3_x_se1_geni_s_irq_clear;
    volatile ULONG* qupv3_x_se1_geni_s_irq_status;
    volatile ULONG* qupv3_x_se1_geni_status;
} idle_qupv3_geni_s_regs_t;

typedef struct idle_engine {
    volatile LONG initialized;
    ULONG enabled;
    ULONG clock_flags;
    ULONG map_qup_registers;
    ULONG rpmh_enabled;
    ULONG rpmh_assume_cmdq_empty;
    volatile LONG in_rpmh;
    volatile LONG rpmh_enter_failures;
    ULONG wake_reason_enabled;
    ULONG wake_pdc_bitmap_enabled;
    ULONG wake_pdc_status_phys;
    ULONG wake_pdc_total_interrupts;
    ULONG wake_pdc_word_cnt;
    PVOID wake_pdc_map_base;
    SIZE_T wake_pdc_map_bytes;
    volatile ULONG* wake_pdc_status_map;
    ULONG transition_debugger_enabled;
    volatile LONG wake_reason_events;
    ULONG hw_drips_test_timeout_seconds;
    ULONG hw_drips_bugcheck_on_failure;
    WDFTIMER hw_drips_timer;
    volatile LONG hw_drips_active;
    ULONGLONG hw_drips_start_residency100ns;
    ULONGLONG hw_drips_start_transition_cnt;
    ULONGLONG hw_drips_start_time100ns;
    idle_qupv3_geni_s_regs_t qup;
    volatile LONG sleep_cnt;
    volatile LONG restore_cnt;
} idle_engine_t;

typedef struct pep_ppm_query_state_name {
    ULONG state_idx;
    USHORT name_size;
    PWSTR name;
} pep_ppm_query_state_name_t;

typedef struct pep_proc_idle_state_v2 {
    ULONG flags;
    ULONG latency;
    ULONG break_even_duration;
} pep_proc_idle_state_v2_t;

typedef struct pep_ppm_query_idle_states_v2 {
    ULONG cnt;
    pep_proc_idle_state_v2_t idle_states[1];
} pep_ppm_query_idle_states_v2_t;

typedef struct pep_coordinated_dependency_option {
    UCHAR expected_state_idx;
    UCHAR loose_dependency;
    UCHAR initiating_state;
    UCHAR dependent_state;
} pep_coordinated_dependency_option_t;

typedef struct pep_ppm_query_coordinated_dependency {
    ULONG state_idx;
    ULONG dependency_idx;
    ULONG dependency_size;
    ULONG dependency_size_used;
    ULONGLONG u4;
    pep_coordinated_dependency_option_t options[1];
} pep_ppm_query_coordinated_dependency_t;

typedef struct pep_ppm_lpi_reg {
    UCHAR address_space_id;
    UCHAR bit_width;
    UCHAR bit_offset;
    UCHAR access_size;
    LARGE_INTEGER address;
} pep_ppm_lpi_reg_t;

typedef struct pep_ppm_lpi_coordinated_dependency {
    union {
        PVOID target_proc;
        PVOID kernel_ctx;
    } u;

    ULONG option_cnt;
    ULONG* options;
} pep_ppm_lpi_coordinated_dependency_t;

typedef struct pep_ppm_proc_lpi_state {
    ULONG min_residency;
    ULONG wakeup_latency;
    ULONG flags;
    ULONG arch_ctx_flags;
    ULONG residency_counter_freq;
    pep_ppm_lpi_reg_t entry_method;
    pep_ppm_lpi_reg_t residency_counter;
    pep_ppm_lpi_reg_t usage_counter;
    PWSTR state_name;
} pep_ppm_proc_lpi_state_t;

typedef struct pep_ppm_lpi_proc_states {
    ULONG cnt;
    pep_ppm_proc_lpi_state_t lpi_states[1];
} pep_ppm_lpi_proc_states_t;

typedef struct pep_ppm_coordinated_lpi_state {
    ULONG min_residency;
    ULONG wakeup_latency;
    ULONG flags;
    ULONG arch_ctx_flags;
    ULONG residency_counter_freq;
    UCHAR integer_entry_method;
    UCHAR reserved0[7];
    ULONGLONG entry_method;
    pep_ppm_lpi_reg_t residency_counter;
    pep_ppm_lpi_reg_t usage_counter;
    PWSTR state_name;
    ULONG dependency_cnt;
    pep_ppm_lpi_coordinated_dependency_t* dependencies;
} pep_ppm_coordinated_lpi_state_t;

typedef struct pep_ppm_lpi_coordinated_states {
    ULONG cnt;
    pep_ppm_coordinated_lpi_state_t lpi_states[1];
} pep_ppm_lpi_coordinated_states_t;

typedef struct idle_mode_attributes {
    ULONG latency;
    ULONG break_even;
} idle_mode_attributes_t;

typedef struct idle_dependency {
    ULONG cnt;
    ULONG* options;
    ULONG lpr_id;
} idle_dependency_t;

typedef struct idle_mode {
    ULONG lpr_id;
    ULONG mode_id;
    ULONG coordinated_mode_id;
    ULONGLONG psci_adder;
    ULONG mode_flags;
    UNICODE_STRING mode_name;
    idle_mode_attributes_t attr;
    ULONG dependency_cnt;
    idle_dependency_t* dependencies;
    volatile LONG veto_reference_cnt;
} idle_mode_t;

typedef struct idle_lpr {
    UNICODE_STRING lpr_name;
    ULONG lpr_id;
    ULONG core_mask;
    ULONG mode_cnt;
    idle_mode_t* mode_array;
    UCHAR is_avail;
} idle_lpr_t;

typedef struct idle_ctx {
    ULONG cpu_cnt;
    ULONG lpr_array_size;
    idle_lpr_t* lpr_array;
    ULONG coordinated_modes_array_size;
    idle_mode_t** coordinated_modes_array;
} idle_ctx_t;

typedef int(__fastcall* pfn_idle_veto_t)(PVOID kernel_handle, unsigned int mode_id,
                                         unsigned int veto_id, unsigned __int8 increment);

typedef struct pep_lpi_list {
    NTSTATUS status;
    ULONG cnt;
    ULONG_PTR indices;
} pep_lpi_list_t;

typedef struct pep_lpi_states {
    ULONG cnt;
    ULONG_PTR ptr;
} pep_lpi_states_t;
