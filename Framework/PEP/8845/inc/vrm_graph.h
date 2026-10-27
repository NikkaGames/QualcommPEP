#ifndef VRM_GRAPH_PROVIDER_H
#define VRM_GRAPH_PROVIDER_H
/* binds the verified LDO4_B and LDO15_B DSDT client votes to the portable device power
 * graph. This ADAPTER cannot establish RPMh/TCS ownership and must only be used with an
 * authoritative, already-adopted rail bank. */

#include "res_graph.h"
#include "rail_bank.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct vrm_graph_ctx {
    rail_bank_t* bank;
} vrm_graph_ctx_t;

/* context must remain alive through graph retirement and Windows callback rundown. */

int graph_bind_known_vrm(res_graph_t*, vrm_graph_ctx_t*);
#ifdef __cplusplus
}
#endif
#endif
