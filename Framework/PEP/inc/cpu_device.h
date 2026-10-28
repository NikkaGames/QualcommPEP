#pragma once
#include "driver.h"

NTSTATUS cpu_device_init(VOID);
ULONG cpu_device_count(VOID);
BOOLEAN cpu_device_notify(ULONG, PVOID, BOOLEAN*);
ULONG cpu_device_index(PVOID);
ULONG cpu_device_dependency(PVOID);
