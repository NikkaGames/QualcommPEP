#ifndef DELAY_PROVIDER_H
#define DELAY_PROVIDER_H
/* delay is a PASSIVE_LEVEL timing operation, NOT a hardware power vote. A wait callback
 * must implement a finite, non-alertable, kernel-mode delay. */

#include "res_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct delay_provider {
    void* ctx;
    int (*wait_ms)(void* ctx, uint32_t ms);
} delay_provider_t;

/* call only on an initialized, unsealed, non-retired graph; provider must remain alive
 * until graph retirement and external caller rundown complete. */

int graph_bind_delay(res_graph_t* graph, delay_provider_t* provider);
#ifdef __cplusplus
}
#endif
#endif
