#pragma once
#include "driver.h"

VOID lpi_init(ULONG processors);
BOOLEAN lpi_ready(VOID);
NTSTATUS lpi_firmware_init(WDFDEVICE device);
VOID lpi_firmware_resume(WDFDEVICE device);
VOID lpi_stop(VOID);
BOOLEAN lpi_notify(PVOID processor, ULONG notification, PVOID data, BOOLEAN* accepted);

/* Resident, nonblocking admission shared by idle entry and resource producers. */
BOOLEAN lpi_io_enter(VOID);
VOID lpi_io_leave(VOID);
BOOLEAN lpi_io_exclusive(VOID);
VOID lpi_io_acquire(VOID);

/* Passive lifecycle operations wait for an in-progress idle transition to return. */
VOID lpi_mutex_acquire(PKMUTEX mutex);
VOID lpi_mutex_release(PKMUTEX mutex);
