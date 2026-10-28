#pragma once
#include "driver.h"

VOID lpi_init(ULONG processors);
BOOLEAN lpi_ready(VOID);
VOID lpi_stop(VOID);
BOOLEAN lpi_notify(PVOID processor, ULONG notification, PVOID data, BOOLEAN* accepted);

/* Resident, nonblocking admission shared by idle entry and resource producers. */
BOOLEAN lpi_io_enter(VOID);
VOID lpi_io_leave(VOID);
VOID lpi_io_acquire(VOID);

/* Passive lifecycle operations wait for an in-progress idle transition to return. */
VOID lpi_mutex_acquire(PKMUTEX mutex);
VOID lpi_mutex_release(PKMUTEX mutex);
