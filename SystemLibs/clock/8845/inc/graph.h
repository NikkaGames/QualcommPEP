#ifndef CLOCK_GRAPH_HYBRID_H
#define CLOCK_GRAPH_HYBRID_H
/* the composite res pairs and ordinary clock provider must already have independently
 * established Windows ownership. All callbacks run under the external nonrecursive graph
 * lock. */

#include "composite.h"
#include "res_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
#define HYBRID_MAX_COMPOSITES 3u

typedef struct hybrid_clock_entry {
    char dev_path[256];
    char clock_name[POWER_NAME_BYTES];
    uint32_t hertz;
    uint32_t opaque_param;
    /* native NPA node ownership is attested before read-only query 0x1000. */
    uint64_t owner_epoch;
    clock_composite_t* composite;
} hybrid_clock_entry_t;

/* query 0x1001 may write on first use; observation and admission use the read-only query. */

#define CLOCK_NATIVE_MATCH_QUERY_ID 0x1000u
#define CLOCK_NATIVE_MUTATING_QUERY_ID 0x1001u
#define NPA_QUERY_TYPE_VALUE 1u

typedef struct clock_match_observation {
    uint32_t match_value;
    uint32_t query_result_type; /* actual NPA result type; never a synthesized echo. */
    uint32_t was_initialized_before_query;
    uint32_t non_mutating_query;
    uint32_t provider_owned;
    uint64_t owner_epoch;
} clock_match_observation_t;

typedef int (*clock_native_match_query_t)(void* ctx, const char* dev_path, const char* clock_name,
                                          uint32_t query_id,
                                          clock_match_observation_t* observation);

typedef struct hybrid_clock_ctx {
    graph_provider_t simple;
    hybrid_clock_entry_t entries[HYBRID_MAX_COMPOSITES];
    uint32_t entry_cnt;
    /* supplied NONPAGED scratch for final-state filtering, never stack. */
    power_sequence_t* filter_scratch;
    clock_native_match_query_t match_query;
    void* match_ctx;
} hybrid_clock_ctx_t;

/* resolve an ALREADY-BOUND dual NPA clock pair belonging to exactly this ACPI device/res.
 * Resolver must independently establish Windows ownership and return NULL for unavailable
 * providers. */

typedef clock_composite_t* (*clock_composite_resolve_t)(void* ctx, const char* dev_path,
                                                        const char* clock_name, uint32_t hertz,
                                                        uint32_t opaque_param);

/* caller holds graph setup lock, prior to GraphSeal, and supplies a stable nonpaged output
 * array. */

int clock_hybrid_discover(const res_graph_t* graph, clock_composite_resolve_t resolve,
                          void* resolver_ctx, clock_native_match_query_t match_query,
                          void* match_ctx, hybrid_clock_entry_t* entries, uint32_t capacity,
                          uint32_t* discovered);

int clock_hybrid_make_provider(hybrid_clock_ctx_t*, const res_graph_t*, const graph_provider_t*,
                               const hybrid_clock_entry_t*, uint32_t, power_sequence_t*,
                               clock_native_match_query_t, void* match_ctx, graph_provider_t*);
#ifdef __cplusplus
}
#endif
#endif
