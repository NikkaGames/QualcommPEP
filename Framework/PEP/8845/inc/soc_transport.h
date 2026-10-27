#pragma once
#include "driver.h"

/* the fixed-address PDP/SCMI input transport below is NOT a confirmed Windows-owned CPU
 * performance provider. A successful MMIO map says nothing about firmware ownership. */

BOOLEAN molokai_transport_ownership_verified(VOID);
NTSTATUS molokai_transport_init(VOID);
VOID molokai_transport_deinit(VOID);
NTSTATUS molokai_transport_set_level(_In_ ULONG domain, _In_ ULONG level);
