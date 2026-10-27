#ifndef RPMH_OEM_PNP_WIN_H
#define RPMH_OEM_PNP_WIN_H
/* wdfcmreslist -> portable PnP res correlation. No ISR or MMIO. */

#include "driver.h"
#include "pnp_resources.h"
NTSTATUS rpmh_match_windows_interrupts(_In_ WDFCMRESLIST raw, _In_ WDFCMRESLIST translated,
                                       _In_reads_(req_cnt) const rpmh_oem_pnp_req_t* requests,
                                       _In_ ULONG req_cnt, _Out_ rpmh_pnp_resources_t* resources);
#endif
