#pragma once
#include "driver.h"

NTSTATUS bus_dcvs_prepare(WDFDEVICE dev, WDFCMRESLIST raw, WDFCMRESLIST translated);
VOID bus_dcvs_start(WDFDEVICE dev);
NTSTATUS bus_dcvs_stop(WDFDEVICE dev);
NTSTATUS bus_dcvs_release(WDFDEVICE dev);
