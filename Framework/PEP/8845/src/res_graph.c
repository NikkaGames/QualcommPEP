#include "res_graph.h"
#include <string.h>

static int valid_device_path(const char* p)
{
    size_t n = 0;
    if (!p)
        return 0;
    while (n < 256u && p[n])
        ++n;
    return n >= 5u && n < 256u && p[0] == '\\';
}

static graph_dev_t* find_dev(res_graph_t* g, const char* path)
{
    uint32_t i;
    /* a corrupt DeviceCount must never turn a path lookup into an out-of-bounds Devices walk. */

    if (!g || !valid_device_path(path) || g->dev_cnt > GRAPH_MAX_DEVICES)
        return NULL;
    for (i = 0; i < g->dev_cnt; ++i) {
        /* never let an unterminated stored scope make strcmp read beyond the fixed path[]
         * allocation. */

        if (!valid_device_path(g->devices[i].path))
            return NULL;
        if (!strcmp(path, g->devices[i].path))
            return &g->devices[i];
    }
    return NULL;
}

/* provider admission/preflight MUST check the bounds again before walking Actions[] or
 * passing a sequence to a callback. */

int graph_has_d_state(const graph_dev_t* d, uint32_t state)
{
    return d && state < POWER_DSTATE_COUNT && (d->d_state_mask & (uint8_t)(1u << state));
}

static int valid_dstate_sequence(const graph_dev_t* d, const power_sequence_t* sequence,
                                 uint32_t target)
{
    return graph_has_d_state(d, target) && sequence && valid_device_path(d->path) &&
           valid_device_path(sequence->dev_path) && !strcmp(d->path, sequence->dev_path) &&
           sequence->target_d_state == target && sequence->action_cnt <= POWER_MAX_ACTIONS;
}

static int valid_dstate_sequences(const graph_dev_t* d)
{
    uint32_t state;
    if (!graph_has_d_state(d, 0u) || !graph_has_d_state(d, 3u) || (d->d_state_mask & ~0x0fu))
        return 0;
    for (state = 0u; state < POWER_DSTATE_COUNT; ++state)
        if (graph_has_d_state(d, state) && !valid_dstate_sequence(d, &d->d_states[state], state))
            return 0;
    return 1;
}

static int valid_provider(const graph_provider_t* p)
{
    return p && p->supports && p->snapshot && p->apply && p->verify && p->restore &&
           p->verify_restored && p->verify_final;
}

void graph_init(res_graph_t* g, void* ctx, int (*acquire)(void*), void (*release)(void*))
{
    if (!g)
        return;
    memset(g, 0, sizeof(*g));
    g->lock_ctx = ctx;
    g->acquire = acquire;
    g->release = release;
}

int graph_retain_sources(res_graph_t* g, graph_desc_t* sources, uint32_t cnt,
                         void (*release)(const void*))
{
    uint32_t i, j;
    if (!g || !g->sealed || g->retired || g->quiescing || g->providers_frozen || g->src_cnt ||
        g->release_src || g->dev_cnt > GRAPH_MAX_DEVICES || !sources || !cnt ||
        cnt > GRAPH_SRC_MAX || !release)
        return 0;
    for (i = 0u; i < g->dev_cnt; ++i)
        if (g->devices[i].prepared)
            return 0;
    for (i = 0u; i < cnt; ++i) {
        if (!sources[i].buf || !sources[i].bytes || sources[i].method[4] || !sources[i].method[0])
            return 0;
        for (j = 0u; j < i; ++j)
            if ((!strcmp(sources[i].method, sources[j].method) &&
                 sources[i].preload == sources[j].preload) ||
                sources[i].buf == sources[j].buf)
                return 0;
    }
    memcpy(g->sources, sources, cnt * sizeof(*sources));
    g->src_cnt = cnt;
    g->release_src = release;
    for (i = 0u; i < cnt; ++i) {
        sources[i].buf = NULL;
        sources[i].bytes = 0u;
    }
    return 1;
}

int graph_src_copy(res_graph_t* g, const char* method, uint64_t serial, void* buf, size_t capacity,
                   size_t* bytes)
{
    uint32_t i;
    int result = 0;
    if (!g || !method || !bytes || !serial || !g->acquire || !g->release ||
        !g->acquire(g->lock_ctx))
        return 0;
    *bytes = 0u;
    if (g->sealed && !g->retired && g->backend_binding_serial == serial && g->release_src &&
        g->src_cnt <= GRAPH_SRC_MAX) {
        for (i = 0u; i < g->src_cnt; ++i) {
            const graph_desc_t* src = &g->sources[i];
            if (src->preload || strcmp(src->method, method))
                continue;
            if (!src->buf || !src->bytes)
                break;
            *bytes = src->bytes;
            if (!buf || capacity < src->bytes)
                result = 2;
            else {
                memcpy(buf, src->buf, src->bytes);
                result = 1;
            }
            break;
        }
    }
    g->release(g->lock_ctx);
    return result;
}

void graph_free_sources(res_graph_t* g)
{
    uint32_t i;
    if (!g || !g->release_src || g->src_cnt > GRAPH_SRC_MAX)
        return;
    for (i = 0u; i < g->src_cnt; ++i) {
        if (g->sources[i].buf)
            g->release_src(g->sources[i].buf);
        memset(&g->sources[i], 0, sizeof(g->sources[i]));
    }
    g->src_cnt = 0u;
    g->release_src = NULL;
}

int graph_add_sequence(res_graph_t* g, const power_sequence_t* p)
{
    graph_dev_t* d;
    if (!g || g->sealed || g->retired || g->quiescing || g->dev_cnt > GRAPH_MAX_DEVICES || !p ||
        !valid_device_path(p->dev_path) || p->target_d_state >= POWER_DSTATE_COUNT ||
        p->action_cnt > POWER_MAX_ACTIONS)
        return 0;
    d = find_dev(g, p->dev_path);
    if (!d) {
        if (g->dev_cnt == GRAPH_MAX_DEVICES)
            return 0;
        d = &g->devices[g->dev_cnt++];
        d->last_nonempty_state = UINT32_MAX;
        memcpy(d->path, p->dev_path, strlen(p->dev_path) + 1u);
    }
    if (graph_has_d_state(d, p->target_d_state))
        return 0;
    d->d_states[p->target_d_state] = *p;
    d->d_state_mask |= (uint8_t)(1u << p->target_d_state);
    return 1;
}

int graph_bind(res_graph_t* g, power_action_kind_t k, const graph_provider_t* p)
{
    if (!g || g->sealed || g->retired || g->quiescing || k <= 0 ||
        (unsigned)k >= GRAPH_PROVIDER_CNT || !valid_provider(p) || valid_provider(&g->providers[k]))
        return 0;
    g->providers[k] = *p;
    return 1;
}

/* sealing validates ACPI and happens before creation of runtime NPA clients. Restrict
 * post-seal provisioning to the one native-footwitch provider that cannot be obtained
 * during the desc build. */

int graph_bind_prepublish_footswitch(res_graph_t* g, const graph_provider_t* p)
{
    uint32_t i;
    int ok = 0;
    if (!g || !p || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    if (!g->sealed || g->providers_frozen || g->quiescing || g->retired ||
        !g->comp_inventory_verified || !g->dev_cnt || !g->ufs_catalog.compiled ||
        !g->backend_binding_serial || !valid_provider(p) || !p->ctx ||
        valid_provider(&g->providers[POWER_FOOTSWITCH]) || g->dev_cnt > GRAPH_MAX_DEVICES)
        goto done;
    /* a graph already performing power transitions is never provisionable. */
    for (i = 0u; i < g->dev_cnt; ++i) {
        const graph_dev_t* d = &g->devices[i];
        if (d->prepared || d->provider_transaction_entered || d->state.state_known ||
            d->state.quarantined)
            goto done;
    }
    /* the exact signed BPMD set-0 selectors must be independently supported by the native
     * client BEFORE the graph ever exposes these callbacks. A generic backend or dummy
     * callback cannot take the reserved slot. */

    {
        const power_pstate_program_t* on = &g->ufs_catalog.p_states[0][0];
        const power_pstate_program_t* off = &g->ufs_catalog.p_states[0][1];
        if (on->comp != 0u || off->comp != 0u || on->set != 0u || off->set != 0u ||
            on->state != 0u || off->state != 1u ||
            memcmp(on->resources.dev_path, "\\_SB.UFS0", sizeof("\\_SB.UFS0")) ||
            memcmp(off->resources.dev_path, "\\_SB.UFS0", sizeof("\\_SB.UFS0")) ||
            on->resources.action_cnt != 1u || off->resources.action_cnt != 1u ||
            on->resources.actions[0].kind != POWER_FOOTSWITCH ||
            off->resources.actions[0].kind != POWER_FOOTSWITCH ||
            !p->supports(p->ctx, "\\_SB.UFS0", 0u, &on->resources.actions[0]) ||
            !p->supports(p->ctx, "\\_SB.UFS0", 3u, &off->resources.actions[0]))
            goto done;
    }
    g->providers[POWER_FOOTSWITCH] = *p;
    ok = 1;
done:
    g->release(g->lock_ctx);
    return ok;
}

int graph_freeze_providers_held(res_graph_t* g)
{
    if (!g || !g->sealed || g->retired || g->quiescing || g->providers_frozen ||
        !g->backend_binding_serial)
        return 0;
    g->providers_frozen = 1u;
    return 1;
}

/* within a device, repeated GPIO operations are intentional reset sequences; across
 * devices, the same physical TLMM pin or PMIC-idx/GPIO pair must not be treated as two
 * independently owned, restorable resources. A future authoritative shared-pin provider
 * may replace this conservative policy. */

static int pins_exclusive(const res_graph_t* g)
{
    uint32_t i, j, sa, sb, a, b;
    for (i = 0; i < g->dev_cnt; ++i)
        for (j = i + 1u; j < g->dev_cnt; ++j) {
            const graph_dev_t* da = &g->devices[i];
            const graph_dev_t* db = &g->devices[j];
            for (sa = 0; sa < POWER_DSTATE_COUNT; ++sa)
                for (sb = 0; sb < POWER_DSTATE_COUNT; ++sb) {
                    const power_sequence_t* pa = &da->d_states[sa];
                    const power_sequence_t* pb = &db->d_states[sb];
                    if (!graph_has_d_state(da, sa) || !graph_has_d_state(db, sb))
                        continue;
                    for (a = 0; a < pa->action_cnt; ++a) {
                        const power_action_t* x = &pa->actions[a];
                        if (x->kind != POWER_TLMM_GPIO && x->kind != POWER_PMIC_GPIO)
                            continue;
                        for (b = 0; b < pb->action_cnt; ++b) {
                            const power_action_t* y = &pb->actions[b];
                            if (x->kind != y->kind)
                                continue;
                            if (x->kind == POWER_TLMM_GPIO &&
                                x->u.gpio.fields[0] == y->u.gpio.fields[0])
                                return 0;
                            if (x->kind == POWER_PMIC_GPIO &&
                                x->u.pmic_gpio.fields[0] == y->u.pmic_gpio.fields[0] &&
                                x->u.pmic_gpio.fields[1] == y->u.pmic_gpio.fields[1])
                                return 0;
                        }
                    }
                }
        }
    return 1;
}

int graph_seal(res_graph_t* g)
{
    uint32_t i;
    if (!g || g->sealed || g->retired || g->quiescing || !g->dev_cnt ||
        g->dev_cnt > GRAPH_MAX_DEVICES || !g->acquire || !g->release)
        return 0;
    for (i = 0; i < g->dev_cnt; ++i) {
        graph_dev_t* d = &g->devices[i];
        if (!valid_dstate_sequences(d))
            return 0;
    }
    if (!pins_exclusive(g))
        return 0;
    g->sealed = 1u;
    return 1;
}

static graph_provider_t* provider(res_graph_t* g, const power_action_t* a)
{
    unsigned k;
    if (!a)
        return NULL;
    k = (unsigned)a->kind;
    return k > 0 && k < GRAPH_PROVIDER_CNT && valid_provider(&g->providers[k]) ? &g->providers[k]
                                                                               : NULL;
}

static int supported(res_graph_t* g, graph_dev_t* d, const power_sequence_t* sequence)
{
    uint32_t i;
    if (!d || !valid_dstate_sequence(d, sequence, sequence->target_d_state))
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i) {
        const power_action_t* a = &sequence->actions[i];
        graph_provider_t* p = provider(g, a);
        if (!p || !p->supports(p->ctx, d->path, sequence->target_d_state, a))
            return 0;
    }
    return 1;
}

static int all_states_supported(res_graph_t* g, graph_dev_t* d)
{
    uint32_t state;
    for (state = 0u; state < POWER_DSTATE_COUNT; ++state)
        if (graph_has_d_state(d, state) && !supported(g, d, &d->d_states[state]))
            return 0;
    return 1;
}

int graph_d_state_declared(res_graph_t* g, const char* path, uint32_t state)
{
    graph_dev_t* d;
    int declared;
    if (!g || state >= POWER_DSTATE_COUNT || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    declared =
        g->sealed && !g->retired && d && valid_dstate_sequences(d) && graph_has_d_state(d, state);
    g->release(g->lock_ctx);
    return declared;
}

int graph_retire(res_graph_t* g)
{
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    /* serializes with graph callbacks holding the lock. A caller delayed before acquiring this
     * lock still needs external Windows rundown. */

    g->retired = 1u;
    g->release(g->lock_ctx);
    return 1;
}

/* graceful shutdown must preserve the ability to execute d3 on the already accepted res
 * clients; emergency retirement cannot provide that guarantee. All admission and shutdown
 * flags are serialized with the same graph lock used by transactions. */

int graph_begin_drain(res_graph_t* g)
{
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    if (g->retired) {
        g->release(g->lock_ctx);
        return 0;
    }
    g->quiescing = 1u;
    g->release(g->lock_ctx);
    return 1;
}

int graph_finish_drain(res_graph_t* g)
{
    uint32_t i;
    int okay = 1;
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    if (g->retired || !g->quiescing || g->dev_cnt > GRAPH_MAX_DEVICES)
        okay = 0;

    for (i = 0; okay && i < g->dev_cnt; ++i) {
        const graph_dev_t* d = &g->devices[i];
        if (d->prepared || d->provider_transaction_entered || d->state.quarantined ||
            (d->state.state_known && d->state.current_d_state != 3u))
            okay = 0;
    }
    if (okay)
        g->retired = 1u;
    g->release(g->lock_ctx);
    return okay;
}

/* read-only admission checks are necessary but do not replace D0 execution, F-state support or
 * verified provider lifetime. */

/* caller already owns the nonrecursive graph lock. This is used when publishing the
 * Windows DPM graph so a drain or NPA teardown cannot begin between validation of
 * successive devices/clients. */

static int reviewed_ic_f0(const graph_dev_t* d)
{
    return d && d->requires_comp_runtime && d->verified_ic_empty_f0 &&
           (!strcmp(d->path, "\\_SB.IC17") || !strcmp(d->path, "\\_SB.IC18") ||
            !strcmp(d->path, "\\_SB.IC21"));
}

static int complete_comp_contract(const res_graph_t* g, const graph_dev_t* d)
{
    return g && d &&
           (!d->requires_comp_runtime || reviewed_ic_f0(d) ||
            (!strcmp(d->path, "\\_SB.UFS0") && g->ufs_catalog.compiled &&
             g->native_ufs_owner_epoch && g->native_ufs_owner_epoch == g->backend_binding_serial));
}

int graph_bind_native_ufs_owner(res_graph_t* g, uint64_t epoch, void* ctx,
                                int (*verify)(void*, uint64_t))
{
    int ok = 0;
    /* hardware/client readback may enter provider locks. Perform it before the graph lock;
     * revalidate its epoch and graph generation inside. */
    if (!g || !epoch || !verify || !g->acquire || !g->release || !verify(ctx, epoch) ||
        !g->acquire(g->lock_ctx))
        return 0;
    if (g->sealed && !g->retired && !g->quiescing && g->ufs_catalog.compiled &&
        !g->native_ufs_owner_epoch && g->backend_binding_serial == epoch) {
        g->native_ufs_owner_epoch = epoch;
        ok = 1;
    }
    g->release(g->lock_ctx);
    return ok;
}

int graph_can_claim_held(res_graph_t* g, const char* path)
{
    graph_dev_t* d = find_dev(g, path);
    return g && g->sealed && !g->retired && !g->quiescing && d && complete_comp_contract(g, d) &&
           valid_dstate_sequences(d) && !d->prepared && !d->provider_transaction_entered &&
           !d->state.quarantined && (!d->state.state_known || d->state.current_d_state == 3u) &&
           all_states_supported(g, d);
}

int graph_can_claim(res_graph_t* g, const char* path)
{
    int ok;
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    ok = graph_can_claim_held(g, path);
    g->release(g->lock_ctx);
    return ok;
}

int graph_audit_dstate_scopes_held(res_graph_t* g, void* ctx, scope_client_ready_t ready,
                                   uint32_t* eligible)
{
    uint32_t i, cnt = 0u;
    if (eligible)
        *eligible = 0u;
    if (!g || !eligible || !ready || !g->comp_inventory_verified || !g->sealed || g->retired ||
        g->quiescing || !g->dev_cnt || g->dev_cnt > GRAPH_MAX_DEVICES)
        return 0;
    for (i = 0u; i < g->dev_cnt; ++i) {
        const graph_dev_t* d = &g->devices[i];
        /* other scopes' F/P-state requirements do not grant or deny d0/d3 ownership on this one;
         * they remain unclaimed. */

        if (!complete_comp_contract(g, d))
            continue;
        /* a fully owned scope must not be held hostage by an unrelated device's unsupported action
         * or unavailable client. This does not permit partially initialized NPA registries: the
         * caller must have activated the complete registry successfully. */

        if (!graph_can_claim_held(g, d->path) || !ready(ctx, d->path))
            continue;
        ++cnt;
    }
    *eligible = cnt;
    return cnt != 0u;
}

/* preserve that distinction here: an OS Dx notify never establishes a verified d3 vote.
 * Firmware may alter the provider resources during suspend. */

int graph_forget_firmware_state(res_graph_t* g, const char* path)
{
    graph_dev_t* d;
    int okay = 0;
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    if (g->sealed && !g->retired && d && d->prepared && d->provider_transaction_entered &&
        !d->state.quarantined && valid_dstate_sequences(d)) {
        d->state.state_known = 0u;
        okay = 1;
    }
    g->release(g->lock_ctx);
    return okay;
}

int graph_is_prepared(res_graph_t* g, const char* path)
{
    graph_dev_t* d;
    int ready;
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    ready = g->sealed && !g->retired && !g->quiescing && d && d->prepared &&
            complete_comp_contract(g, d) && !d->state.quarantined && valid_dstate_sequences(d);
    g->release(g->lock_ctx);
    return ready;
}

int graph_reg_dstate_only(res_graph_t* g, const char* path, uint32_t components)
{
    graph_dev_t* d;
    int ready;
    /* the exposed graph contract requires zero components. Component F/P support needs separate
     * provider initialization. */

    if (components != 0u || !g || !path || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    ready = g->comp_inventory_verified && g->sealed && !g->retired && !g->quiescing && d &&
            d->prepared && !d->requires_comp_runtime && !d->state.quarantined &&
            valid_dstate_sequences(d);
    g->release(g->lock_ctx);
    return ready;
}

int graph_reg_reviewed_ic_comp(res_graph_t* g, const char* path, uint32_t components)
{
    graph_dev_t* d;
    int ready = 0;
    if (components != 1u || !g || !path || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    ready = g->comp_inventory_verified && g->sealed && !g->retired && !g->quiescing &&
            reviewed_ic_f0(d) && d->prepared && d->provider_transaction_entered &&
            d->state.state_known && d->state.current_d_state == 0u && !d->state.quarantined &&
            valid_dstate_sequences(d);
    g->release(g->lock_ctx);
    return ready;
}

int graph_prepare(res_graph_t* g, const char* path)
{
    graph_dev_t* d;
    int okay;
    if (!g || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    /* resolve inside the same lock used for retirement and transitions. PREPARE must not
     * inspect prepared or sequence pointers before admission. */

    d = find_dev(g, path);
    /* admission and device recovery state must be checked under the SAME lock as transitions.
     * GraphPrepareD0 is a callable API in addition to the guarded PoFx path; it must not
     * bypass GraphCanClaim's quarantine checks after a failed rollback or an externally
     * detected state drift. */

    if (!d || !g->sealed || g->retired || g->quiescing || d->prepared ||
        d->provider_transaction_entered || !complete_comp_contract(g, d) || d->state.quarantined ||
        !valid_dstate_sequences(d) || (d->state.state_known && d->state.current_d_state != 3u)) {
        g->release(g->lock_ctx);
        return 0;
    }
    okay = all_states_supported(g, d);
    if (okay)
        d->prepared = 1u;
    g->release(g->lock_ctx);
    return okay;
}

typedef struct run_ctx {
    res_graph_t* graph;
    graph_dev_t* dev;
    const power_sequence_t* sequence;
} run_ctx_t;

static uint16_t client_key(const graph_dev_t* d, const power_sequence_t* sequence, uint32_t i)
{
    if (sequence->actions[i].kind == POWER_FOOTSWITCH)
        return 0u;
    return d->state_clients[sequence->target_d_state][i];
}

static int action_client(const graph_dev_t* d, const power_sequence_t* sequence,
                         const power_action_t* action, char identity[256])
{
    uint32_t i;
    for (i = 0u; i < sequence->action_cnt; ++i)
        if (action == &sequence->actions[i]) {
            uint32_t client = client_key(d, sequence, i);
            if (client)
                return power_set_identity(d->path, (client - 1u) / 32u, (client - 1u) % 32u,
                                          identity);
            memcpy(identity, d->path, 256u);
            return 1;
        }
    return 0;
}

static graph_provider_t* run_provider(run_ctx_t* r, const power_action_t* a)
{
    return provider(r->graph, a);
}

static int run_lock(void* ctx)
{
    run_ctx_t* r = (run_ctx_t*)ctx;
    if (!r->graph->acquire(r->graph->lock_ctx))
        return 0;
    /* graphtransition checked prepared before acquiring lock. Retire may have completed in
     * between; never touch stale providers. */

    if (r->graph->retired || r->graph->dev_cnt > GRAPH_MAX_DEVICES ||
        !valid_dstate_sequences(r->dev) ||
        (r->graph->quiescing && r->sequence->target_d_state != 3u) || !r->dev->prepared ||
        !complete_comp_contract(r->graph, r->dev)) {
        r->graph->release(r->graph->lock_ctx);
        return 0;
    }

    if (r->sequence->target_d_state == 0u)
        r->dev->provider_transaction_entered = 1u;
    return 1;
}

static void run_unlock(void* ctx)
{
    run_ctx_t* r = (run_ctx_t*)ctx;
    if (r->sequence->action_cnt && r->dev->state.state_known && !r->dev->state.quarantined &&
        r->dev->state.current_d_state == r->sequence->target_d_state)
        r->dev->last_nonempty_state = r->sequence->target_d_state;
    r->graph->release(r->graph->lock_ctx);
}

static int run_supports(void* ctx, const power_action_t* a)
{
    run_ctx_t* r = ctx;
    char identity[256];
    graph_provider_t* p = run_provider(r, a);
    return p && action_client(r->dev, r->sequence, a, identity) &&
           p->supports(p->ctx, identity, r->sequence->target_d_state, a);
}

/* probe ownership immediately before every physical operation too. The sequence builder's locked
 * preflight is not a lifetime guarantee from an external provider; the provider's own
 * callbacks must additionally synchronize their physical writes with revocation. */

static int run_apply(void* ctx, const power_action_t* a)
{
    run_ctx_t* r = ctx;
    char identity[256];
    graph_provider_t* p = run_provider(r, a);
    return p && action_client(r->dev, r->sequence, a, identity) &&
           p->supports(p->ctx, identity, r->sequence->target_d_state, a) &&
           p->apply(p->ctx, identity, r->sequence->target_d_state, a);
}

static int run_verify(void* ctx, const power_action_t* a)
{
    run_ctx_t* r = ctx;
    char identity[256];
    graph_provider_t* p = run_provider(r, a);
    return p && action_client(r->dev, r->sequence, a, identity) &&
           p->supports(p->ctx, identity, r->sequence->target_d_state, a) &&
           p->verify(p->ctx, identity, r->sequence->target_d_state, a);
}

#undef RUN_FN
#define RUN_SNAPSHOT(name, field, type)                                                            \
    static int name(void* ctx, const power_action_t* a, type* s)                                   \
    {                                                                                              \
        run_ctx_t* r = ctx;                                                                        \
        char identity[256];                                                                        \
        graph_provider_t* p = run_provider(r, a);                                                  \
        return p && action_client(r->dev, r->sequence, a, identity) &&                             \
               p->field(p->ctx, identity, r->sequence->target_d_state, a, s);                      \
    }
RUN_SNAPSHOT(run_snapshot, snapshot, power_snapshot_t)
#undef RUN_SNAPSHOT
#define RUN_RESTORE(name, field)                                                                   \
    static int name(void* ctx, const power_action_t* a, const power_snapshot_t* s)                 \
    {                                                                                              \
        run_ctx_t* r = ctx;                                                                        \
        char identity[256];                                                                        \
        graph_provider_t* p = run_provider(r, a);                                                  \
        return p && action_client(r->dev, r->sequence, a, identity) &&                             \
               p->field(p->ctx, identity, r->sequence->target_d_state, a, s);                      \
    }
RUN_RESTORE(run_restore, restore)
RUN_RESTORE(run_verify_restored, verify_restored)
#undef RUN_RESTORE

static int run_verify_final(void* ctx, const power_sequence_t* sequence)
{
    run_ctx_t* r = ctx;
    power_sequence_t* group = &r->graph->provider_sequence;
    char identity[256];
    uint32_t i, j;
    if (!sequence->action_cnt) {
        uint32_t previous = r->dev->last_nonempty_state;
        run_ctx_t prior = *r;
        if (!r->dev->state.state_known || r->dev->state.quarantined ||
            !graph_has_d_state(r->dev, previous) || !r->dev->d_states[previous].action_cnt)
            return 0;
        prior.sequence = &r->dev->d_states[previous];
        return run_verify_final(&prior, prior.sequence);
    }
    for (i = 0; i < sequence->action_cnt; ++i) {
        unsigned k = (unsigned)sequence->actions[i].kind;
        uint16_t key = client_key(r->dev, sequence, i);
        graph_provider_t* p = run_provider(r, &sequence->actions[i]);
        if (!p || !action_client(r->dev, sequence, &sequence->actions[i], identity) ||
            !p->supports(p->ctx, identity, sequence->target_d_state, &sequence->actions[i]))
            return 0;
        for (j = 0u; j < i; ++j)
            if ((unsigned)sequence->actions[j].kind == k && client_key(r->dev, sequence, j) == key)
                break;
        if (j != i)
            continue;
        memset(group, 0, sizeof(*group));
        memcpy(group->dev_path, identity, sizeof(group->dev_path));
        group->target_d_state = sequence->target_d_state;
        for (j = 0u; j < sequence->action_cnt; ++j)
            if ((unsigned)sequence->actions[j].kind == k && client_key(r->dev, sequence, j) == key)
                group->actions[group->action_cnt++] = sequence->actions[j];
        if (!p->verify_final(p->ctx, identity, sequence->target_d_state, group))
            return 0;
    }
    return 1;
}

int graph_verify_reviewed_ic_f0(res_graph_t* g, const char* path, uint32_t comp, uint32_t state)
{
    graph_dev_t* d;
    run_ctx_t run;
    int ok = 0;
    if (!g || comp != 0u || state != 0u || !g->acquire || !g->release || !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    if (g->comp_inventory_verified && g->sealed && !g->retired && !g->quiescing &&
        reviewed_ic_f0(d) && d->prepared && d->provider_transaction_entered &&
        d->state.state_known && d->state.current_d_state == 0u && !d->state.quarantined &&
        valid_dstate_sequences(d)) {
        run.graph = g;
        run.dev = d;
        run.sequence = &d->d_states[0];
        /* read-only provider-owned final state verification; no f0 writes exist in the exact ACPI
         * desc. provider retirement and failed readback both refuse the PoFx completion. */

        ok = run_verify_final(&run, &d->d_states[0]);
    }
    g->release(g->lock_ctx);
    return ok;
}

int graph_verify_native_ufs_d3(res_graph_t* g, const char* path, uint64_t epoch)
{
    graph_dev_t* d;
    run_ctx_t run;
    int ok = 0;
    if (!g || !path || strcmp(path, "\\_SB.UFS0") || !epoch || !g->acquire || !g->release ||
        !g->acquire(g->lock_ctx))
        return 0;
    d = find_dev(g, path);
    if (g->sealed && !g->retired && d && !d->prepared && !d->provider_transaction_entered &&
        valid_dstate_sequences(d) && g->native_ufs_owner_epoch == epoch &&
        g->backend_binding_serial == epoch && d->state.state_known && !d->state.quarantined &&
        d->state.current_d_state == 3u) {
        run.graph = g;
        run.dev = d;
        run.sequence = &d->d_states[3];
        ok = run_verify_final(&run, &d->d_states[3]);
    }
    g->release(g->lock_ctx);
    return ok;
}

power_result_t graph_transition(res_graph_t* g, const char* path, uint32_t state,
                                power_execution_t* trace, power_scratch_t* scratch)
{
    graph_dev_t* d = find_dev(g, path);
    power_backend_t backend;
    run_ctx_t run;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    if (!d || !g->sealed || !d->prepared || !scratch || !graph_has_d_state(d, state) ||
        !valid_dstate_sequences(d)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    run.graph = g;
    run.dev = d;
    run.sequence = &d->d_states[state];
    memset(&backend, 0, sizeof(backend));
    backend.ctx = &run;
    backend.lock = run_lock;
    backend.unlock = run_unlock;
    backend.supports = run_supports;
    backend.snapshot = run_snapshot;
    backend.apply = run_apply;
    backend.verify = run_verify;
    backend.restore = run_restore;
    backend.verify_restored = run_verify_restored;
    backend.verify_current_state = run_verify_final;
    return power_execute(run.sequence, &d->state, &backend, trace, scratch);
}

/* PREPARE executes initial D0 only after complete provider and hardware admission. */

power_result_t graph_prepare_d0(res_graph_t* g, const char* path, power_execution_t* trace,
                                power_scratch_t* scratch)
{
    power_result_t result;
    graph_dev_t* d;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    if (!g || !scratch || !graph_prepare(g, path)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    result = graph_transition(g, path, 0u, trace, scratch);
    if (result == POWER_OK)
        return result;
    /* verified rollback releases admission for retry; uncertain state or failed rollback retains
     * quarantine. */

    if (!g->acquire(g->lock_ctx))
        return result;
    d = find_dev(g, path);
    if (!g->retired && d && d->prepared && !d->state.quarantined &&
        (!d->state.state_known || d->state.current_d_state == 3u)) {
        /* a failed initial d0 which never wrote hardware, or whose writes were fully restored and
         * read back, has no outstanding provider transaction. intentionally marks entry before the
         * first callback so ABANDON cannot discard an uncertain transaction; but retaining that
         * marker after PROVEN rollback. */

        d->prepared = 0u;
        d->provider_transaction_entered = 0u;
    }
    g->release(g->lock_ctx);
    return result;
}

/* pep_DPM_ABANDON_DEVICE must release d3 before lifetime teardown. This cannot be
 * implemented by just dropping the node or blindly clearing a power vote. */

power_result_t graph_abandon_d3(res_graph_t* g, const char* path, power_execution_t* trace,
                                power_scratch_t* scratch)
{
    graph_dev_t* d;
    power_result_t result;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    if (!g || !scratch) {
        trace->result = POWER_BAD_INPUT;
        return trace->result;
    }
    /* an abandoned metadata-only admission (e.g. an interrupted failed preparation) may never
     * have entered a provider transaction. normal PREPARE enters the initial d0 transaction
     * and MUST go through the verified d3 release below. Sending d3 for an unknown,
     * never-entered transaction could cause. */

    if (!g->acquire || !g->release || !g->acquire(g->lock_ctx)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    d = find_dev(g, path);
    if (!g->retired && d && d->prepared && !d->provider_transaction_entered &&
        !d->state.quarantined && (!d->state.state_known || d->state.current_d_state == 3u) &&
        valid_dstate_sequences(d)) {
        d->prepared = 0u;
        g->release(g->lock_ctx);
        trace->failure_idx = UINT32_MAX;
        trace->result = POWER_OK;
        return POWER_OK;
    }
    g->release(g->lock_ctx);
    result = graph_transition(g, path, 3u, trace, scratch);
    if (result != POWER_OK)
        return result;
    if (!g->acquire(g->lock_ctx)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    d = find_dev(g, path);
    if (g->retired || !d || !d->prepared || d->state.quarantined || !d->state.state_known ||
        d->state.current_d_state != 3u) {
        result = POWER_UNSUPPORTED;
    } else {
        d->prepared = 0u;
        d->provider_transaction_entered = 0u;
    }
    g->release(g->lock_ctx);
    trace->result = result;
    return result;
}
