#pragma once
#include "driver.h"

NTSTATUS cpu_device_init(VOID);
BOOLEAN cpu_device_notify(ULONG, PVOID, BOOLEAN*);
ULONG cpu_device_index(PVOID);
