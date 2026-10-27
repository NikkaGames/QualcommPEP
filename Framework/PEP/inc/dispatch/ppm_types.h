#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef struct pep_proc_query_caps {
    ULONG version;
    ULONG reserved;
    UCHAR directed_p_state;
    UCHAR core_rotation;
    UCHAR discrete_perf_state_cnt;
    UCHAR reserved2;
} pep_proc_query_caps_t;
