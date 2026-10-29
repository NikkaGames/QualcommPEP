/* hw_backend_bind8845.c - graph-provider forwarder. A single generic provider whose
 * Context is the reg-bound graph serial; each method admits only the same verified backend
 * reg before forwarding to its action-level op. */

#include "hw_backend.h"
#include <string.h>

static int bound_begin(void* ctx, hw_backend_t* out)
{
    const uint64_t* serial = (const uint64_t*)ctx;
    return serial && hw_backend_begin_bound(*serial, out);
}

static int fwd_supports(void* c, const char* p, uint32_t d, const power_action_t* a)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.supports && b.supports(b.ctx, p, d, a);
    hw_backend_end_bound();
    return ok;
}

static int fwd_snapshot(void* c, const char* p, uint32_t d, const power_action_t* a,
                        power_snapshot_t* s)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.snapshot && b.snapshot(b.ctx, p, d, a, s);
    hw_backend_end_bound();
    return ok;
}

static int fwd_apply(void* c, const char* p, uint32_t d, const power_action_t* a)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.apply && b.apply(b.ctx, p, d, a);
    hw_backend_end_bound();
    return ok;
}

static int fwd_verify(void* c, const char* p, uint32_t d, const power_action_t* a)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.verify && b.verify(b.ctx, p, d, a);
    hw_backend_end_bound();
    return ok;
}

static int fwd_restore(void* c, const char* p, uint32_t d, const power_action_t* a,
                       const power_snapshot_t* s)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.restore && b.restore(b.ctx, p, d, a, s);
    hw_backend_end_bound();
    return ok;
}

static int fwd_verify_restored(void* c, const char* p, uint32_t d, const power_action_t* a,
                               const power_snapshot_t* s)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.verify_restored && b.verify_restored(b.ctx, p, d, a, s);
    hw_backend_end_bound();
    return ok;
}

static int fwd_verify_final(void* c, const char* p, uint32_t d, const power_sequence_t* pl)
{
    hw_backend_t b;
    int ok;
    if (!bound_begin(c, &b))
        return 0;
    ok = b.verify_final && b.verify_final(b.ctx, p, d, pl);
    hw_backend_end_bound();
    return ok;
}

int hw_backend_bind_graph(res_graph_t* graph)
{
    graph_provider_t prov;
    int kind;
    uint64_t binding_serial;
    /* only ever bind when the verified backend is live. Never on a host or unattested path. */

    if (!hw_backend_ready())
        return -1;
    if (!graph || graph->sealed || graph->retired)
        return -2;
    binding_serial = hw_backend_verified_serial();
    if (!binding_serial)
        return -1;

    memset(&prov, 0, sizeof(prov));
    /* pin one verified registration for the graph lifetime. Retire partial bindings; backend
     * re-registration does not change the graph token. */

    graph->backend_binding_serial = binding_serial;
    prov.ctx = &graph->backend_binding_serial;
    prov.supports = fwd_supports;
    prov.snapshot = fwd_snapshot;
    prov.apply = fwd_apply;
    prov.verify = fwd_verify;
    prov.restore = fwd_restore;
    prov.verify_restored = fwd_verify_restored;
    prov.verify_final = fwd_verify_final;

    /* kind 8 is an NPA FOOTSWITCH client with its own lifetime and physical readback
     * semantics. ufs_fs_graph_bind must attach that independent, already authenticated client
     * BEFORE Seal. */

    for (kind = (int)POWER_CLOCK; kind < (int)POWER_KIND_COUNT; ++kind) {
        if (kind == (int)POWER_FOOTSWITCH)
            continue;
        if (!graph_bind(graph, (power_action_kind_t)kind, &prov))
            return -3;
    }
    return 0;
}
