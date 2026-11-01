#ifndef UFS_CXMX_OWNER_H
#define UFS_CXMX_OWNER_H
/* clockbsp's UFS GDSC depends on VDD_CX and VDD_MX. ClockCore requests MIN_SVS before
 * enabling the domain; the VCS provider maps that abstract level through each ARC
 * command-DB LUT. PMIC addresses and microvolts do not describe this interface. Caller
 * serializes this object and RPMh. */

#include <stdint.h>
#include "gdsc_native.h"
#include "domain_rails.h"
#ifdef __cplusplus
extern "C" {
#endif
#define UFS_CX_NAME "cx.lvl"
#define UFS_MX_NAME "mx.lvl"
#define UFS_CX_LEVEL 0x30u
#define UFS_MX_LEVEL 0x30u
#define UFS_RAIL_CLIENT "\\_SB.UFS0#GDSC"

typedef domain_rail_io_t ufs_cxmx_io_t;

typedef domain_rails_t ufs_cxmx_owner_t;

/* bind ONLY to verified idle per-client slots; preexisting firmware votes are permitted,
 * and must NOT be adopted as the PEP client's vote. */

int ufs_cx_mx_bind(ufs_cxmx_owner_t*, const ufs_cxmx_io_t*, uint64_t epoch);
/* export the exact ClockBSP rail provider required by the existing physical GDSC executor. */

int ufs_cx_mx_rail_provider(ufs_cxmx_owner_t*, fs_gdsc_rail_provider_t*);
/* unbind only after graph rundown and verified release. Quarantine is sticky. */
int ufs_cx_mx_retire(ufs_cxmx_owner_t*);
#ifdef __cplusplus
}
#endif
#endif
