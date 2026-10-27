#ifndef POFX_PERF_ENDPOINT_H
#define POFX_PERF_ENDPOINT_H

#include "perf_desc.h"
#include "state_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* desc, owner, and provider outlive the endpoint and every admitted PoFx callback. */
typedef struct pofx_perf_endpoint {
    const pofx_perf_catalog_t* catalog;
    comp_state_owner_t* owner;
    const void* desc;
    size_t desc_bytes;
    const power_dev_t* physical_d0;
    const power_backend_t* backend;
    uint64_t owner_epoch;
    uint8_t bound;
} pofx_perf_endpoint_t;

int po_fx_endpoint_bind(pofx_perf_endpoint_t* endpoint, const pofx_perf_catalog_t* catalog,
                        comp_state_owner_t* owner, const void* pinned_desc, size_t desc_bytes,
                        const power_dev_t* physical_d0, const power_backend_t* backend,
                        uint64_t live_owner_epoch, comp_state_work_t* nonpaged_work);

/* caller owns NONPAGED storage for the desc, scope and work buf. A failed post-vote
 * publish retains every owner until d3 reconciliation. */
typedef struct pofx_producer {
    comp_state_owner_t owner;
    pofx_perf_catalog_t catalog;
    pofx_perf_endpoint_t endpoint;
    const void* desc;
    size_t desc_bytes;
    const power_backend_t* provider;
    uint64_t owner_epoch;
    uint8_t retain;
    uint8_t published;
} pofx_producer_t;

/* purely desc-driven; requires a physical d0 owner and a held provider. Initial f0 must
 * establish and verify every advertised P-state set. */
int po_fx_producer_prepare(pofx_producer_t* producer, const void* pinned_desc, size_t desc_bytes,
                           const char* path, uint32_t comp_cnt, const power_dev_t* physical_d0,
                           const power_backend_t* provider, uint64_t owner_epoch,
                           runtime_desc_info_t* scratch, comp_state_work_t* work);

/* only after Windows callback rundown. Hardware F/D-state retirement is external; this
 * never claims res release or deletes a provider vote. */
void po_fx_producer_close(pofx_producer_t* producer);
/* d3 may be in a separate BSMD desc. Recompile it before accepting external physical
 * readback; a caller-supplied sequence is not provenance. */
int po_fx_producer_release(pofx_producer_t* producer, const power_dev_t* verified_d3,
                           const void* pinned_d3_desc, size_t d3_desc_bytes,
                           runtime_desc_info_t* desc_work, power_sequence_t* sequence_work,
                           comp_state_work_t* nonpaged_work, uint64_t live_epoch);

void po_fx_endpoint_retire(pofx_perf_endpoint_t* endpoint);

/* published metadata remains valid in Dx/Fx. executing a new vote still requires the
 * stricter d0/f0 readiness check below. */
int po_fx_endpoint_catalog_ready(const pofx_perf_endpoint_t* endpoint, uint64_t live_owner_epoch,
                                 uint32_t comp);

int po_fx_endpoint_ready(const pofx_perf_endpoint_t* endpoint, uint64_t live_owner_epoch,
                         uint32_t comp);

power_result_t po_fx_endpoint_execute(pofx_perf_endpoint_t* endpoint, uint64_t live_owner_epoch,
                                      uint32_t comp, const uint32_t* set_ordinals,
                                      const uint32_t* state_ordinals, uint32_t cnt,
                                      comp_state_work_t* nonpaged_work, power_execution_t* trace);

/* f1 must remain callable after f0 is no longer the current state. No P-state endpoint
 * readiness check may gate f1 -> f0 recovery. */
power_result_t po_fx_endpoint_execute_f_state(pofx_perf_endpoint_t* endpoint,
                                              uint64_t live_owner_epoch, uint32_t comp,
                                              uint32_t f_state, comp_state_work_t* nonpaged_work,
                                              power_execution_t* trace);

/* reads are authoritative only when the provider re-verifies the selected program. */
int po_fx_endpoint_query_current(const pofx_perf_endpoint_t* endpoint, uint64_t live_owner_epoch,
                                 uint32_t comp, uint32_t set_ordinal,
                                 comp_state_work_t* nonpaged_work, uint32_t* state_ordinal);

#ifdef __cplusplus
}
#endif
#endif
