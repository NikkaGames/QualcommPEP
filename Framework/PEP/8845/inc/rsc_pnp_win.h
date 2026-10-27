#ifndef RSC_PNP_WIN_H
#define RSC_PNP_WIN_H
/* windows-only PnP res census for THIS PEP0 WDFDEVICE. Success is NOT a grant to map,
 * trigger or own RPMh TCS slots. */

#include <ntddk.h>
#include <wdf.h>
NTSTATUS rsc_check_this_dev_translated_resources(_In_ WDFCMRESLIST translated_resources,
                                                 _Out_ BOOLEAN* has_dedicated_rsc_window);
#endif
