#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef struct pep_notify_comp_idle_state {
    PVOID dev_handle;
    ULONG comp;
    ULONG idle_state;
    UCHAR driver_notified;
    UCHAR completed;
} pep_notify_comp_idle_state_t;
