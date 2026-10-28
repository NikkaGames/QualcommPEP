#pragma once
#include "driver.h"

#include "perf.h"

/* Performance control is published only after firmware discovery and channel readback. */

BOOLEAN molokai_transport_ready(VOID);
NTSTATUS molokai_transport_init(VOID);
VOID molokai_transport_deinit(VOID);
VOID molokai_transport_suspend(BOOLEAN suspended);
BOOLEAN molokai_transport_idle_begin(VOID);
VOID molokai_transport_idle_end(VOID);
NTSTATUS molokai_transport_set_level(_In_ ULONG domain, _In_ ULONG level);
NTSTATUS molokai_transport_get_level(_In_ ULONG domain, _Out_ ULONG* level);
const scmi_perf_domain_t* molokai_transport_domain(_In_ ULONG domain);
ULONG molokai_transport_latency(ULONG domain);
