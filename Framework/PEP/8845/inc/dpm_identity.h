#ifndef DPM_IDENTITY_H
#define DPM_IDENTITY_H
/* never infer an ACPI path from a PnP hardware ID or a suffix match. It does not evaluate
 * AML, register power ownership or touch hardware. */

#include "res_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
int resolve_pep_dev_path(const res_graph_t* graph, const uint16_t* unicode_id, size_t length_bytes,
                         char out_path[256]);
power_result_t dpm_prepare_unicode(res_graph_t*, const uint16_t*, size_t, power_execution_t*,
                                   power_scratch_t*);
power_result_t dpm_abandon_unicode(res_graph_t*, const uint16_t*, size_t, power_execution_t*,
                                   power_scratch_t*);
#ifdef __cplusplus
}
#endif
#endif
