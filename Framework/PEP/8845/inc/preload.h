#ifndef PRELOAD_H
#define PRELOAD_H

#include "hw_backend.h"

typedef struct preload {
    power_sequence_t sequence, group;
    power_scratch_t baseline;
    power_backend_t adapter;
    hw_backend_t provider;
    aeob_preload_result_t selected;
    power_execution_t trace;
    uint16_t lane[POWER_MAX_ACTIONS];
    char failure_kind[24];
    uint64_t serial;
    void* ctx;
    int (*begin)(void*, hw_backend_t*);
    void (*end)(void*);
    uint8_t held, committed, quarantined;
} preload_t;

/* Storage is caller-owned and nonpaged. A committed owner retains its resource clients
 * until rollback or provider rundown; it never publishes an OS device power state. */
power_result_t preload_compile(preload_t*, const void*, size_t, const char*);
power_result_t preload_execute(preload_t*);
int preload_rollback(preload_t*);

#endif
