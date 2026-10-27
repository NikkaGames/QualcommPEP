#pragma once
#include "driver.h"

/* pofx/idle-state scaffolding (public interface). */
NTSTATUS po_fx_init(_In_ WDFDEVICE dev);
VOID po_fx_deinitialize(_In_ WDFDEVICE dev);
