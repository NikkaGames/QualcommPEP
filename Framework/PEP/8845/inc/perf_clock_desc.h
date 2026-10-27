#ifndef CLOCK_PERF_DISCOVERY_H
#define CLOCK_PERF_DISCOVERY_H
#include "aeob_idx.h"
#include "gcc.h"

typedef struct clock_perf_catalog {
    char clock[128];
    char path[256];
    uint64_t desc_fingerprint;
    uint32_t hertz[32];
    uint32_t cnt;
} clock_perf_catalog_t;

/* a clock identity must come from the exact pinned desc. Frequencies come from that
 * clock's writable BSP domain, never the CPU DCVS catalog. */
int clock_perf_discover(clock_perf_catalog_t* catalog, const void* desc, size_t bytes,
                        const program_idx_t* idx);
#endif
