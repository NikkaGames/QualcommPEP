#ifndef CLOCK_PERF_WIN_H
#define CLOCK_PERF_WIN_H
#include "component_perf.h"
#include "perf_clock_desc.h"

typedef struct clock_perf_binding {
    void* ctx;
    const power_backend_t* backend;
    const power_dev_t* physical_state;
    int (*ready)(void*);
    /* fences D-state transitions and retains the verified current Dx. A CLOCK freq operation
     * does not require the peripheral branch enabled. */
    int (*begin)(void*);
    void (*end)(void*);
    /* owns graph serialization and provider rundown; returns independently checked programmed
     * hz even when the branch is gated in Dx. */
    int (*read_clock)(void*, const char*, uint64_t*);
    int (*read_dfs_catalog)(void*, const char*, gcc_dfs_catalog_t*);
} clock_perf_binding_t;

typedef struct clock_perf_scope clock_perf_scope_t;
clock_perf_scope_t* clock_perf_create(const void* blob, size_t bytes, const program_idx_t* idx,
                                      const clock_perf_binding_t* binding);
void clock_perf_destroy(clock_perf_scope_t* scope);
ULONG_PTR clock_perf_handle(clock_perf_scope_t* scope, ULONG notify, PVOID data);
#endif
