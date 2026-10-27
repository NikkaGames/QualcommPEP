#ifndef TLMM_GRAPH_PROVIDER_H
#define TLMM_GRAPH_PROVIDER_H
/* strict graph -> TLMM provider bridge. NEVER creates/claims Windows TLMM ownership. */

#include "res_graph.h"
#include "tlmm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct tlmm_graph_ctx {
    const tlmm_io_t* io;
} tlmm_graph_ctx_t;

int graph_bind_owned_tlmm(res_graph_t*, tlmm_graph_ctx_t*);
#ifdef __cplusplus
}
#endif
#endif
