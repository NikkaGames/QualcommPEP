#include "ufs_native_provision.h"
#include "domains.h"
#include "gdsc_native.h"
#include "ufs_cxmx_owner.h"
#include <string.h>

static const char k_ufs_domain[] = "gcc_ufs_phy_gdsc";
static const char k_normal_name[] = "/fs/gcc_ufs_phy_gdsc/enable";

/* these callbacks never accept an arbitrary client pointer. */

static int fs_complete_normal(void* ctx, const char* domain, uint64_t handle)
{
    ufs_native_scope_t* s = (ufs_native_scope_t*)ctx;
    fs_factory_t* f;
    if (!s || !s->bound || s->quarantined || !s->factory.bound || strcmp(domain, k_ufs_domain) ||
        s->epoch != s->factory.epoch || !s->factory.provider.complete_client ||
        strcmp(s->factory.normal_res, k_normal_name))
        return 0;
    f = &s->factory;
    if (handle != f->normal_handle || !f->normal_open)
        return 0;
    return f->provider.complete_client(f->provider.ctx, f->normal_res, handle);
}

static int fs_read_physical(void* ctx, const char* domain, uint64_t epoch, uint32_t* actual)
{
    ufs_native_scope_t* s = (ufs_native_scope_t*)ctx;
    uint64_t current = 0u;
    return s && s->bound && !s->quarantined && s->factory.bound && actual &&
           !strcmp(domain, k_ufs_domain) && epoch == s->epoch && s->read_physical &&
           s->factory.provider.owns_domain(s->factory.provider.ctx, k_ufs_domain) &&
           s->factory.provider.read_owner_epoch(s->factory.provider.ctx, k_ufs_domain, &current) &&
           current == epoch && s->read_physical(s->physical_ctx, k_ufs_domain, epoch, actual) &&
           (*actual == 1u || *actual == 2u);
}

static int fs_verify_physical(void* ctx, const char* domain, uint32_t expected, uint64_t epoch)
{
    ufs_native_scope_t* s = (ufs_native_scope_t*)ctx;
    uint32_t actual = 0;
    if (!s || !s->bound || s->quarantined || !s->factory.bound || !s->read_physical || !s->graph ||
        strcmp(domain, k_ufs_domain) || epoch != s->epoch || epoch != s->factory.epoch ||
        (expected != 0u && expected != 1u && expected != 2u) ||
        !s->factory.provider.owns_domain(s->factory.provider.ctx, k_ufs_domain) ||
        !s->factory.provider.read_owner_epoch(s->factory.provider.ctx, k_ufs_domain, &epoch) ||
        epoch != s->epoch || !s->read_physical(s->physical_ctx, k_ufs_domain, s->epoch, &actual))
        return 0;
    return actual == expected;
}

fs_result_t ufs_native_provision(ufs_native_scope_t* s, res_graph_t* g,
                                 const fs_factory_provider_t* provider, const char* client_name,
                                 void* physical_ctx,
                                 int (*physical)(void*, const char*, uint64_t, uint32_t*))
{
    fs_result_t r;
    uint32_t baseline = 0, domain_id = 0;
    if (!s || !g || !provider || !client_name || !physical || s->bound || s->quarantined ||
        s->factory.bound || s->factory.normal_handle || s->adapter.bound || !g->sealed ||
        g->retired || g->quiescing || g->providers_frozen || !g->ufs_catalog.compiled ||
        !g->backend_binding_serial || g->providers[POWER_FOOTSWITCH].supports ||
        !provider->complete_client)
        return FS_INVALID;
    /* an independently readable physical domain is mandatory, including before the first vote.
     * Existing firmware votes may leave it on/off; neither state is treated as a vote owned by
     * this client. */

    {
        uint64_t ep = 0;
        if (!provider->owns_domain || !provider->read_owner_epoch || !provider->resolve_domain_id ||
            !provider->resolve_domain_id(provider->ctx, k_ufs_domain, &domain_id) ||
            domain_id != OEM_UFS_PHY_DOMAIN_ID ||
            !provider->owns_domain(provider->ctx, k_ufs_domain) ||
            !provider->read_owner_epoch(provider->ctx, k_ufs_domain, &ep) || !ep ||
            !physical(physical_ctx, k_ufs_domain, ep, &baseline) ||
            (baseline != 1u && baseline != 2u))
            return FS_OWNERSHIP_LOST;
    }
    s->physical_ctx = physical_ctx;
    s->read_physical = physical;
    s->graph = g;
    r = fs_factory_open_for_action(&s->factory, k_ufs_domain, client_name, FS_ENABLE, provider);
    if (r != FS_OK) {
        if (s->factory.quarantined || s->factory.normal_open || s->factory.normal_handle)
            s->quarantined = 1u;
        return r;
    }
    if (s->factory.domain_id != OEM_UFS_PHY_DOMAIN_ID) {
        r = fs_factory_close(&s->factory);
        if (r != FS_OK) {
            s->quarantined = 1u;
            return r;
        }
        memset(s, 0, sizeof(*s));
        return FS_OWNERSHIP_LOST;
    }
    s->epoch = s->factory.epoch;
    /* the adapter is permitted to inspect the owner. */

    s->bound = 1u;
    if (!ufs_fs_graph_bind_verified(g, &s->adapter, &s->factory.native, fs_complete_normal,
                                    fs_verify_physical, fs_read_physical)) {
        s->bound = 0u;
        r = fs_factory_close(&s->factory);
        if (r != FS_OK) {
            s->quarantined = 1u; /* client may still exist: retain storage. */
            return r;
        }
        memset(s, 0, sizeof(*s));
        return FS_UNSUPPORTED;
    }
    /* after provider publication the entire scope/factory remains resident until external PoFx
     * callback rundown. */

    return FS_OK;
}

int ufs_native_retains_graph(const ufs_native_scope_t* s)
{
    if (!s)
        return 0;
    return s->graph || s->bound || s->adapter.bound || s->quarantined || s->factory.bound ||
           s->factory.quarantined || s->factory.normal_handle || s->factory.hw_handle ||
           s->factory.normal_open || s->factory.hw_open;
}

fs_result_t ufs_native_discard_unbound(ufs_native_scope_t* s)
{
    fs_result_t r;
    if (!s || s->bound || s->adapter.bound || s->quarantined)
        return FS_INVALID;
    if (!s->factory.bound) {
        if (s->factory.normal_handle || s->factory.normal_open || s->factory.quarantined)
            return FS_QUARANTINED;
        memset(s, 0, sizeof(*s));
        return FS_OK;
    }
    r = fs_factory_close(&s->factory);
    if (r != FS_OK) {
        s->quarantined = 1u;
        return r;
    }
    memset(s, 0, sizeof(*s));
    return FS_OK;
}

fs_result_t ufs_native_rollback_unpublished(ufs_native_scope_t* s, struct fs_gdsc_driver* d,
                                            struct ufs_cxmx_owner* rails)
{
    if (!s || !d || !rails || s->bound || s->adapter.bound || s->quarantined ||
        s->factory.quarantined || d->quarantined || d->rails_held || d->npa.quarantined ||
        rails->quarantined || rails->held)
        return FS_QUARANTINED;

    if (ufs_native_discard_unbound(s) != FS_OK)
        goto failed;
    if (d->bound) {
        if (!d->npa.bound || d->npa.epoch != d->epoch ||
            !fs_nodes_begin_shutdown(&d->npa, d->epoch) || !fs_nodes_retire(&d->npa))
            goto failed;
        memset(d, 0, sizeof(*d));
    }
    if (rails->bound && !ufs_cx_mx_retire(rails))
        goto failed;
    return FS_OK;
failed:
    /* a partially retired factory/node cannot be assumed to own zero votes. */
    if (s->graph || s->factory.bound)
        s->quarantined = 1u;
    if (d->bound)
        d->quarantined = 1u;
    if (rails->bound)
        rails->quarantined = 1u;
    return FS_PROVIDER_FAILURE;
}

/* completes the src-owned NPA node lifetime, not a generic npa_complete_request. A prior
 * OFF vote must already have committed to hardware. */
fs_result_t ufs_native_retire_verified(ufs_native_scope_t* s, struct fs_gdsc_driver* d,
                                       struct ufs_cxmx_owner* rails)
{
    res_graph_t* graph;
    fs_result_t result = FS_UNSUPPORTED;
    uint32_t i, actual = 0u;
    int dev_proven = 0;
    if (!s || !d || !rails || !s->bound || !s->graph || s->quarantined || !s->factory.bound ||
        !s->adapter.bound || s->epoch != s->factory.epoch || s->epoch != d->epoch ||
        s->epoch != rails->epoch || !d->bound || !rails->bound || !s->factory.normal_handle)
        return FS_INVALID;
    graph = s->graph;
    if (!graph->acquire || !graph->release || !graph->acquire(graph->lock_ctx))
        return FS_UNSUPPORTED;
    if (!graph->quiescing || graph->retired || !graph->providers_frozen ||
        graph->providers[POWER_FOOTSWITCH].ctx != &s->adapter || !d->npa.bound ||
        d->npa.quarantined || d->rails_held || rails->held || d->npa.epoch != s->epoch ||
        graph->dev_cnt > GRAPH_MAX_DEVICES)
        goto done;
    for (i = 0u; i < graph->dev_cnt; ++i) {
        const graph_dev_t* node = &graph->devices[i];
        if (strcmp(node->path, "\\_SB.UFS0"))
            continue;
        if (dev_proven++)
            goto done;
        if (node->state.quarantined ||
            (node->state.state_known && node->state.current_d_state != 3u) ||
            (node->prepared && (!node->state.state_known || node->state.current_d_state != 3u)))
            goto done;
    }
    if (dev_proven != 1 || !s->read_physical || fs_factory_verify(&s->factory) != FS_OK ||
        (s->factory.native.normal_action != 0u && s->factory.native.normal_action != FS_DISABLE) ||
        s->factory.native.hw_ctl_action != 0u ||
        !s->read_physical(s->physical_ctx, k_ufs_domain, s->epoch, &actual) ||
        (actual != 1u && actual != 2u) ||
        (s->factory.native.normal_action == FS_DISABLE && actual != 2u))
        goto done;
    if (!fs_nodes_begin_shutdown(&d->npa, s->epoch))
        goto failed;
    if (s->factory.native.normal_action == FS_DISABLE &&
        fs_factory_retire_off(&s->factory, 1u) != FS_OK)
        goto failed;
    if (fs_factory_close(&s->factory) != FS_OK)
        goto failed;
    if (!fs_nodes_retire(&d->npa) || !ufs_cx_mx_retire(rails))
        goto failed;
    memset(&graph->providers[POWER_FOOTSWITCH], 0, sizeof(graph->providers[POWER_FOOTSWITCH]));
    memset(d, 0, sizeof(*d));
    memset(s, 0, sizeof(*s));
    result = FS_OK;
    goto done;
failed:
    /* failed completion may already have modified a client; retain every owner. */
    s->quarantined = 1u;
    d->quarantined = 1u;
    result = FS_PROVIDER_FAILURE;
done:
    graph->release(graph->lock_ctx);
    return result;
}
