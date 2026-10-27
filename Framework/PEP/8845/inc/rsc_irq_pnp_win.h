#ifndef RSC_IRQ_PNP_WIN_H
#define RSC_IRQ_PNP_WIN_H
#include <ntddk.h>
#include <wdf.h>
#include "rsc_irq_assignment.h"
#include "rsc_irq_snapshot.h"

rsc_irq_report_t rsc_inspect_irq_resources(_In_ WDFCMRESLIST resources_raw,
                                           _In_ WDFCMRESLIST resources_translated);
/* returns the precise PnP desc pair, for inspection ONLY, while the exact PrepareHardware
 * generation is active. Not an ownership or ISR reg API. */

int rsc_select_irq_candidate_descriptors(_In_ WDFCMRESLIST resources_raw,
                                         _In_ WDFCMRESLIST resources_translated,
                                         _In_ const rsc_irq_snapshot_t* snapshot,
                                         uint64_t expected_generation,
                                         _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR* raw_out,
                                         _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR* translated_out);
#endif
