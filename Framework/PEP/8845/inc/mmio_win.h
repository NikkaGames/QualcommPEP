#ifndef MMIO_WIN_H
#define MMIO_WIN_H
/* reg windows identified from Molokai firmware/DTS, NOT proof that the replacement PEP
 * owns them. Production direct RW mapping is blocked until separate Windows ownership
 * acquisition is established: CLK_CTL (GCC + PLLs) 0x00100000 2 MiB read/write, uncached
 * TLMM tile 0 pins 0x0F100000 1 MiB. */

#include "driver.h"
#include "gcc.h"
#include "tlmm.h"
#include "rpmh_tcs.h"
#include "cmddb.h"

typedef struct mmio_window {
    volatile UCHAR* base;
    SIZE_T bytes;
    ULONGLONG physical;
} mmio_window_t;

typedef struct mmio {
    mmio_window_t gcc;
    mmio_window_t tlmm;
    mmio_window_t rsc;
} mmio_t;

/* requires verified production owner contract; currently returns STATUS_NOT_SUPPORTED
 * without mapping any writable register windows. */

NTSTATUS mmio_map(_Out_ mmio_t* mmio);
VOID mmio_unmap(_Inout_ mmio_t* mmio);

/* call ONLY after the caller establishes a live PEP0 d0 RSC/IRQ37 owner and serializes
 * PoFx callback admission. This helper neither grants ownership nor maps RSC; it verifies
 * stable peripheral register reads before returning. */

NTSTATUS mmio_d0_map_peripherals(_Inout_ mmio_t* mmio);
VOID mmio_d0_unmap_peripherals(_Inout_ mmio_t* mmio);

/* bounds-checked 32-bit accessors (offset relative to the window). */
int mmio_read32(const mmio_window_t* w, ULONG offset, ULONG* value);
int mmio_write32(const mmio_window_t* w, ULONG offset, ULONG value);

/* copy the command database to caller-owned nonpaged storage and unmap before returning. Validate
 * version and signature; release the buffer with ExFreePool. */

NTSTATUS mmio_snapshot_command_db(_Outptr_result_maybenull_ UCHAR** copy, _Out_ SIZE_T* bytes);

#endif
