#pragma once
#include "driver.h"

typedef struct chipinfo {
    ULONG chip_id;
    ULONG chip_family;
    ULONG chip_version;
    ULONG chip_sku;
} chipinfo_t;

NTSTATUS chip_info_init_from_dev(_In_ WDFDEVICE dev);
chipinfo_t chip_info_get(VOID);
