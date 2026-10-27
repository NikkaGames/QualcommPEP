#pragma once
#include "driver.h"

NTSTATUS xo_test_init(_In_ WDFDRIVER driver);
VOID xo_test_deinit(VOID);

VOID xo_test_handle_req(_In_ LONG req);
