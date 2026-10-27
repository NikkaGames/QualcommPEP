#pragma once
#include "driver.h"

NTSTATUS timer_init(_In_ WDFDRIVER driver);
VOID timer_deinit(VOID);
