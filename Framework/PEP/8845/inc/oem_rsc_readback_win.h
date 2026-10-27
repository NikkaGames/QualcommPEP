#ifndef OEM_RSC_READBACK_WIN_H
#define OEM_RSC_READBACK_WIN_H
#include "oem_direct_rsc_map_win.h"
#include "oem_rsc_readback.h"
/* READ_REGISTER_ULONG requires a pinned, independently authorized mapping lifetime. */

int oem_rsc_win_readback(oem_rsc_win_map_t*, oem_rsc_snapshot_t*);
#endif
