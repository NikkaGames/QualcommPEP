#include "graph.h"
#include <string.h>
#include <stdint.h>

#define HYBRID_SNAPSHOT_MAGIC UINT64_C(0x48383443504e4150)

static int valid_provider(const graph_provider_t* p)
{
    return p && p->supports && p->snapshot && p->apply && p->verify && p->restore &&
           p->verify_restored && p->verify_final;
}

static size_t bounded(const char* str, size_t cap)
{
    size_t i;
    if (!str)
        return cap;
    for (i = 0; i < cap && str[i]; ++i) {
    }
    return i;
}

static int valid_text(const char* s, size_t cap)
{
    size_t i, n = bounded(s, cap);
    if (!n || n == cap)
        return 0;
    for (i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_'))
            return 0;
    return 1;
}

static int action_for(const power_action_t* a, const hybrid_clock_entry_t* e, uint32_t st)
{
    if (!a || !e || a->kind != POWER_CLOCK || strcmp(a->u.clock.name, e->clock_name))
        return 0;
    if (st == 0u)
        return a->u.clock.vote == 8u && a->u.clock.extra_cnt == 2u &&
               a->u.clock.extra[0] == e->hertz && a->u.clock.extra[1] == e->opaque_param;
    if (st == 3u)
        return a->u.clock.vote == 2u && !a->u.clock.extra_cnt;
    return 0;
}

static hybrid_clock_entry_t* lookup(hybrid_clock_ctx_t* c, const char* path,
                                    const power_action_t* a)
{
    uint32_t i;
    if (!c || !path || !a || a->kind != POWER_CLOCK)
        return NULL;
    for (i = 0; i < c->entry_cnt; ++i)
        if (!strcmp(path, c->entries[i].dev_path) &&
            !strcmp(a->u.clock.name, c->entries[i].clock_name))
            return &c->entries[i];
    return NULL;
}

/* never make a mutating query as part of discovery or revalidation. The native provider
 * must independently attest that the initialization flag was already set, and return the
 * true NPA VALUE response and owner epoch. */

static int match_query_checked(clock_native_match_query_t query, void* ctx, const char* path,
                               const char* clock, uint32_t expected, uint64_t pinned_epoch,
                               uint64_t* discovered_epoch)
{
    clock_match_observation_t o;
    if (!query || !path || !clock)
        return 0;
    memset(&o, 0, sizeof(o));
    if (!query(ctx, path, clock, CLOCK_NATIVE_MATCH_QUERY_ID, &o) ||
        o.query_result_type != NPA_QUERY_TYPE_VALUE || o.was_initialized_before_query != 1u ||
        o.non_mutating_query != 1u || o.provider_owned != 1u || !o.owner_epoch ||
        o.match_value != expected || (pinned_epoch && o.owner_epoch != pinned_epoch))
        return 0;
    if (discovered_epoch)
        *discovered_epoch = o.owner_epoch;
    return 1;
}

static int match_authoritative(hybrid_clock_ctx_t* c, hybrid_clock_entry_t* e)
{
    if (!c || !e || !e->composite ||
        (e->composite->native_npa && (!e->composite->enable || !e->composite->freq ||
                                      e->composite->enable->owner_epoch != e->owner_epoch ||
                                      e->composite->freq->owner_epoch != e->owner_epoch)) ||
        !match_query_checked(c->match_query, c->match_ctx, e->dev_path, e->clock_name,
                             e->opaque_param, e->owner_epoch, NULL)) {
        /* replaced/uninitialized provider or a value supplied by our own first query must not be
         * treated as hardware metadata proof. */

        if (e && e->composite) {
            e->composite->quarantined = 1u;
            if (e->composite->enable)
                e->composite->enable->quarantined = 1u;
            if (e->composite->freq)
                e->composite->freq->quarantined = 1u;
        }
        return 0;
    }
    return 1;
}

static int supported(void* v, const char* path, uint32_t st, const power_action_t* a)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    if (!c || !a)
        return 0;
    if (e)
        return action_for(a, e, st) && match_authoritative(c, e) && e->composite &&
               e->composite->active && !e->composite->quarantined &&
               scalar_verify_owned(e->composite->enable) && scalar_verify_owned(e->composite->freq);
    return c->simple.supports(c->simple.ctx, path, st, a);
}

static int decode(const power_snapshot_t* s, clock_composite_snapshot_t* out)
{
    if (!s || !out || s->opaque[0] != HYBRID_SNAPSHOT_MAGIC)
        return 0;
    memcpy(out, &s->opaque[1], sizeof(*out));
    return 1;
}

static int take_snapshot(void* v, const char* path, uint32_t st, const power_action_t* a,
                         power_snapshot_t* s)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    clock_composite_snapshot_t x;
    if (!s || !supported(v, path, st, a))
        return 0;
    if (!e)
        return c->simple.snapshot(c->simple.ctx, path, st, a, s);
    if (sizeof(x) > sizeof(s->opaque) - sizeof(s->opaque[0]) ||
        !clock_composite_snapshot(e->composite, &x))
        return 0;
    memset(s, 0, sizeof(*s));
    s->opaque[0] = HYBRID_SNAPSHOT_MAGIC;
    memcpy(&s->opaque[1], &x, sizeof(x));
    return 1;
}

static int apply(void* v, const char* path, uint32_t st, const power_action_t* a)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    clock_composite_snapshot_t old;
    if (!supported(v, path, st, a))
        return 0;
    if (!e)
        return c->simple.apply(c->simple.ctx, path, st, a);

    return st == 0u ? clock_composite_issue(e->composite, a, &old)
                    : clock_composite_release(e->composite, &old);
}

static int verify(void* v, const char* path, uint32_t st, const power_action_t* a)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    if (!supported(v, path, st, a))
        return 0;
    if (!e)
        return c->simple.verify(c->simple.ctx, path, st, a);
    return clock_composite_verify_state(e->composite, (uint8_t)(st == 0u),
                                        (st == 0u || e->composite->native_npa) ? e->hertz : 0u);
}

static int restore(void* v, const char* path, uint32_t st, const power_action_t* a,
                   const power_snapshot_t* s)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    clock_composite_snapshot_t x;
    if (!c || !s)
        return 0;
    if (!e)
        return c->simple.restore(c->simple.ctx, path, st, a, s);
    return action_for(a, e, st) && match_authoritative(c, e) && decode(s, &x) &&
           clock_composite_restore(e->composite, &x);
}

static int restored(void* v, const char* path, uint32_t st, const power_action_t* a,
                    const power_snapshot_t* s)
{
    hybrid_clock_ctx_t* c = v;
    hybrid_clock_entry_t* e = lookup(c, path, a);
    clock_composite_snapshot_t before, now;
    if (!c || !s)
        return 0;
    if (!e)
        return c->simple.verify_restored(c->simple.ctx, path, st, a, s);
    /* a provider may be removed or recreated between restore and verify_restored while
     * exposing an identical match value and an identical vote snapshot. Neither equality is
     * ownership proof: recheck the pinned native NPA owner epoch (and the preexisting
     * initialized flag) here too. */

    if (!action_for(a, e, st) || !match_authoritative(c, e) || !decode(s, &before) ||
        !clock_composite_snapshot(e->composite, &now))
        return 0;
    return before.enable_present == now.enable_present && before.freq_present == now.freq_present &&
           before.enable_req == now.enable_req && before.freq_req == now.freq_req &&
           before.enable_generation == now.enable_generation &&
           before.freq_generation == now.freq_generation && before.enable_slot == now.enable_slot &&
           before.freq_slot == now.freq_slot;
}

static int verify_state(void* v, const char* path, uint32_t st, const power_sequence_t* sequence)
{
    hybrid_clock_ctx_t* c = v;
    uint32_t i, extended = 0u;
    power_sequence_t* filtered;
    if (!c || !sequence || !path || !c->filter_scratch || strcmp(path, sequence->dev_path) ||
        st != sequence->target_d_state || sequence->action_cnt > POWER_MAX_ACTIONS)
        return 0;
    filtered = c->filter_scratch;
    memset(filtered, 0, sizeof(*filtered));
    memcpy(filtered->dev_path, path, strlen(path) + 1u);
    filtered->target_d_state = st;
    for (i = 0; i < sequence->action_cnt; ++i) {
        const power_action_t* a = &sequence->actions[i];
        hybrid_clock_entry_t* e;
        if (a->kind != POWER_CLOCK)
            continue;
        e = lookup(c, path, a);
        if (e) {
            if (!verify(c, path, st, a))
                return 0;
            ++extended;
        } else {
            filtered->actions[filtered->action_cnt++] = *a;
        }
    }
    if (!extended && !filtered->action_cnt)
        return 0;
    if (filtered->action_cnt && !c->simple.verify_final(c->simple.ctx, path, st, filtered))
        return 0;
    return 1;
}

static int cnt_action(const power_sequence_t* p, const hybrid_clock_entry_t* e, uint32_t st)
{
    uint32_t i;
    int cnt = 0;
    for (i = 0; i < p->action_cnt; ++i)
        if (p->actions[i].kind == POWER_CLOCK &&
            !strcmp(p->actions[i].u.clock.name, e->clock_name)) {
            if (!action_for(&p->actions[i], e, st))
                return 0;
            ++cnt;
        }
    return cnt == 1;
}

/* this check rejects an unlisted opcode-8 res even if a caller's fallback provider
 * incorrectly claims to support all CLOCK opcodes. All composite actions must be handled
 * by a proven dual-client adapter. */

static int complete_composite_coverage(const res_graph_t* g, const hybrid_clock_entry_t* entries,
                                       uint32_t cnt)
{
    uint32_t i, j, k, found = 0u;
    if (!g || !entries || !cnt)
        return 0;
    if (g->dev_cnt > GRAPH_MAX_DEVICES)
        return 0;
    for (i = 0; i < g->dev_cnt; ++i) {
        const graph_dev_t* d = &g->devices[i];
        if (d->d0.action_cnt > POWER_MAX_ACTIONS)
            return 0;
        for (j = 0; j < d->d0.action_cnt; ++j) {
            const power_action_t* a = &d->d0.actions[j];
            if (a->kind != POWER_CLOCK || a->u.clock.vote != 8u)
                continue;
            for (k = 0; k < cnt; ++k)
                if (!strcmp(d->path, entries[k].dev_path) &&
                    !strcmp(a->u.clock.name, entries[k].clock_name))
                    break;
            if (k == cnt || !action_for(a, &entries[k], 0u))
                return 0;
            if (++found > cnt)
                return 0;
        }
    }
    return found == cnt;
}

static int same_native_clock(const clock_composite_t* c, const char* clock_name)
{
    static const char start[] = "/clk/";
    static const char suffix[] = "/enable";
    size_t n;
    if (!c || !c->active || c->quarantined || !c->enable || !c->freq ||
        !valid_text(clock_name, POWER_NAME_BYTES))
        return 0;
    n = strlen(clock_name);
    if (n + sizeof(start) > sizeof(c->freq->name) ||
        n + sizeof(start) + sizeof(suffix) - 1u > sizeof(c->enable->name))
        return 0;
    return !strncmp(c->freq->name, start, sizeof(start) - 1u) &&
           !strcmp(c->freq->name + sizeof(start) - 1u, clock_name) &&
           !strncmp(c->enable->name, start, sizeof(start) - 1u) &&
           !strncmp(c->enable->name + sizeof(start) - 1u, clock_name, n) &&
           !strcmp(c->enable->name + sizeof(start) - 1u + n, suffix) &&
           scalar_verify_owned(c->enable) && scalar_verify_owned(c->freq);
}

int clock_hybrid_discover(const res_graph_t* g, clock_composite_resolve_t resolver,
                          void* resolver_ctx, clock_native_match_query_t query, void* query_ctx,
                          hybrid_clock_entry_t* entries, uint32_t capacity, uint32_t* discovered)
{
    hybrid_clock_entry_t temporary[HYBRID_MAX_COMPOSITES];
    uint32_t i, j, k, cnt = 0u;
    if (discovered)
        *discovered = 0u;
    /* an old manifest must not survive a failed admission attempt. */
    if (entries && capacity && capacity <= HYBRID_MAX_COMPOSITES)
        memset(entries, 0, (size_t)capacity * sizeof(*entries));
    if (!g || !resolver || !query || !entries || !discovered || !capacity ||
        capacity > HYBRID_MAX_COMPOSITES || !g->dev_cnt || g->dev_cnt > GRAPH_MAX_DEVICES ||
        g->sealed || g->retired || g->quiescing)
        return 0;
    memset(temporary, 0, sizeof(temporary));
    for (i = 0; i < g->dev_cnt; ++i) {
        const graph_dev_t* d = &g->devices[i];
        if (!d->has_d0 || !d->has_d3 || bounded(d->path, sizeof(d->path)) == sizeof(d->path) ||
            bounded(d->d0.dev_path, sizeof(d->d0.dev_path)) == sizeof(d->d0.dev_path) ||
            bounded(d->d3.dev_path, sizeof(d->d3.dev_path)) == sizeof(d->d3.dev_path) ||
            strcmp(d->d0.dev_path, d->path) || strcmp(d->d3.dev_path, d->path) ||
            d->d0.target_d_state != 0u || d->d3.target_d_state != 3u ||
            d->d0.action_cnt > POWER_MAX_ACTIONS || d->d3.action_cnt > POWER_MAX_ACTIONS)
            return 0;
        for (j = 0; j < d->d0.action_cnt; ++j) {
            const power_action_t* a = &d->d0.actions[j];
            hybrid_clock_entry_t* e;
            clock_composite_t* c;
            uint32_t same_d0 = 0u, same_d3 = 0u;
            if (a->kind != POWER_CLOCK || a->u.clock.vote != 8u)
                continue;
            if (cnt == capacity || !valid_text(a->u.clock.name, sizeof(a->u.clock.name)) ||
                a->u.clock.extra_cnt != 2u || !a->u.clock.extra[0])
                return 0;
            for (k = 0; k < d->d0.action_cnt; ++k)
                if (d->d0.actions[k].kind == POWER_CLOCK &&
                    !strcmp(d->d0.actions[k].u.clock.name, a->u.clock.name))
                    ++same_d0;
            for (k = 0; k < d->d3.action_cnt; ++k)
                if (d->d3.actions[k].kind == POWER_CLOCK &&
                    !strcmp(d->d3.actions[k].u.clock.name, a->u.clock.name)) {
                    if (d->d3.actions[k].u.clock.vote != 2u || d->d3.actions[k].u.clock.extra_cnt)
                        return 0;
                    ++same_d3;
                }
            if (same_d0 != 1u || same_d3 != 1u)
                return 0;
            /* non-mutating native NPA 0x1000 query plus independent provider initialization/epoch
             * attestation. */

            {
                uint64_t native_epoch = 0;
                if (!match_query_checked(query, query_ctx, d->path, a->u.clock.name,
                                         a->u.clock.extra[1], 0u, &native_epoch))
                    return 0;
                /* saved with the client manifest; provider replacement is denied at publication AND
                 * every later state transition. */

                temporary[cnt].owner_epoch = native_epoch;
            }
            c = resolver(resolver_ctx, d->path, a->u.clock.name, a->u.clock.extra[0],
                         a->u.clock.extra[1]);
            if (!same_native_clock(c, a->u.clock.name) ||
                (c->native_npa && (c->enable->owner_epoch != temporary[cnt].owner_epoch ||
                                   c->freq->owner_epoch != temporary[cnt].owner_epoch)))
                return 0;
            /* a composite is state owned: it may never be assigned to a second graph device even if
             * both refer to the same clock. */

            for (k = 0; k < cnt; ++k)
                if (temporary[k].composite == c)
                    return 0;
            e = &temporary[cnt++];
            memcpy(e->dev_path, d->path, strlen(d->path) + 1u);
            memcpy(e->clock_name, a->u.clock.name, strlen(a->u.clock.name) + 1u);
            e->hertz = a->u.clock.extra[0];
            e->opaque_param = a->u.clock.extra[1];
            e->composite = c;
        }
    }
    if (!cnt || !complete_composite_coverage(g, temporary, cnt))
        return 0;
    memcpy(entries, temporary, (size_t)cnt * sizeof(*entries));
    *discovered = cnt;
    return 1;
}

int clock_hybrid_make_provider(hybrid_clock_ctx_t* c, const res_graph_t* g,
                               const graph_provider_t* simple, const hybrid_clock_entry_t* entries,
                               uint32_t cnt, power_sequence_t* scratch,
                               clock_native_match_query_t query, void* query_ctx,
                               graph_provider_t* out)
{
    uint32_t i, j;
    /* the destination must be cleared even when a newly revoked native provider makes a
     * previously valid publication impossible. */

    if (out)
        memset(out, 0, sizeof(*out));
    if (!c || !g || !simple || !valid_provider(simple) || !entries || !cnt ||
        cnt > HYBRID_MAX_COMPOSITES || !scratch || !query || !out || g->sealed || g->retired ||
        g->quiescing || !complete_composite_coverage(g, entries, cnt))
        return 0;
    for (i = 0; i < cnt; ++i) {
        const hybrid_clock_entry_t* e = &entries[i];
        int found = 0;
        if (!e->dev_path[0] || e->dev_path[0] != '\\' ||
            bounded(e->dev_path, sizeof(e->dev_path)) == sizeof(e->dev_path) ||
            !valid_text(e->clock_name, sizeof(e->clock_name)) || !e->hertz || !e->owner_epoch ||
            !e->composite || !e->composite->active || e->composite->quarantined ||
            (e->composite->native_npa && (!e->composite->enable || !e->composite->freq ||
                                          e->composite->enable->owner_epoch != e->owner_epoch ||
                                          e->composite->freq->owner_epoch != e->owner_epoch)) ||
            !scalar_verify_owned(e->composite->enable) || !scalar_verify_owned(e->composite->freq))
            return 0;
        if (!match_query_checked(query, query_ctx, e->dev_path, e->clock_name, e->opaque_param,
                                 e->owner_epoch, NULL))
            return 0;
        for (j = 0; j < i; ++j)
            if (!strcmp(entries[j].dev_path, e->dev_path) &&
                !strcmp(entries[j].clock_name, e->clock_name))
                return 0;
        for (j = 0; j < g->dev_cnt; ++j) {
            const graph_dev_t* d = &g->devices[j];
            if (strcmp(d->path, e->dev_path))
                continue;
            if (!d->has_d0 || !d->has_d3 || !cnt_action(&d->d0, e, 0u) ||
                !cnt_action(&d->d3, e, 3u))
                return 0;
            ++found;
        }
        if (found != 1)
            return 0;
    }
    memset(c, 0, sizeof(*c));
    c->simple = *simple;
    memcpy(c->entries, entries, (size_t)cnt * sizeof(*entries));
    c->entry_cnt = cnt;
    c->filter_scratch = scratch;
    c->match_query = query;
    c->match_ctx = query_ctx;
    out->ctx = c;
    out->supports = supported;
    out->snapshot = take_snapshot;
    out->apply = apply;
    out->verify = verify;
    out->restore = restore;
    out->verify_restored = restored;
    out->verify_final = verify_state;
    return 1;
}
