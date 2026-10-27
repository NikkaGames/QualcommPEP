#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef struct pep_bugcheck_snapshot_v1 {
    ULONG signature;
    ULONG version;
    ULONG size;
    ULONG cpu;
    ULONG current_freq_hz;
    ULONG callback_invocations;
    ULONG last_reason;
    ULONG last_reason_data_length;
    ULONG bugcheck_enable_mask;
    ULONG debugger_present;
    ULONGLONG drips_residency100ns;
    ULONGLONG drips_transition_cnt;
    ULONGLONG snapshot_time100ns;
} pep_bugcheck_snapshot_v1_t;

typedef struct pep_crashdump_information {
    PVOID dev_handle;
    PVOID dev_ctx;
} pep_crashdump_information_t;

typedef UCHAR(__fastcall* pfn_power_on_dump_dev_t)(
    _In_opt_ pep_crashdump_information_t* crashdump_information);

typedef struct pep_reg_crashdump_dev {
    pfn_power_on_dump_dev_t power_on_dump_dev_callback;
    PVOID dev_handle;
} pep_reg_crashdump_dev_t;
