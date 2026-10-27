#ifndef OEM_RSC_PNP_EVIDENCE_WIN_H
#define OEM_RSC_PNP_EVIDENCE_WIN_H
#include <ntddk.h>
#include <wdf.h>
#include "oem_rsc_pnp_evidence.h"
/* read-ONLY: returns a diagnostic, not an MMIO/IRQ/TCS grant. */
rsc_evidence_finding_t rsc_audit_win_pnp_evidence(WDFCMRESLIST raw, WDFCMRESLIST translated,
                                                  uint64_t pnp_generation,
                                                  rsc_evidence_report_t* report);
#endif
