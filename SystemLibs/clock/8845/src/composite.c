#include "composite.h"
#include <string.h>

static int ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same_prefix(const char* a, const char* b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if (ascii_lower((unsigned char)a[i]) != ascii_lower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int same_clock(const char* en, const char* hz)
{
    const char suffix[] = "/enable";
    size_t a = 0u, b = 0u;
    if (!en || !hz)
        return 0;
    while (a < SCALAR_NAME_BYTES && en[a])
        ++a;
    while (b < SCALAR_NAME_BYTES && hz[b])
        ++b;
    if (a >= SCALAR_NAME_BYTES || b >= SCALAR_NAME_BYTES || b <= 5u ||
        a != b + sizeof(suffix) - 1u || !same_prefix(en, "/clk/", 5u) ||
        !same_prefix(hz, "/clk/", 5u))
        return 0;
    return same_prefix(en, hz, b) && same_prefix(en + b, suffix, sizeof(suffix) - 1u);
}

static int action_matches_clock(const power_action_t* a, const scalar_res_t* hz)
{
    size_t i;
    const char* n;
    if (!a || !hz || a->kind != POWER_CLOCK || !same_prefix(hz->name, "/clk/", 5u))
        return 0;
    n = hz->name + 5u;
    for (i = 0u; i < POWER_NAME_BYTES; ++i) {
        if (!a->u.clock.name[i] || !n[i])
            return i > 0u && !a->u.clock.name[i] && !n[i];
        if (ascii_lower((unsigned char)a->u.clock.name[i]) != ascii_lower((unsigned char)n[i]))
            return 0;
    }
    return 0;
}

static int ready(clock_composite_t* c)
{
    if (!c || !c->active || c->quarantined || !c->enable || !c->freq)
        return 0;
    if (!scalar_verify_owned(c->enable) || !scalar_verify_owned(c->freq)) {
        c->quarantined = 1u;
        return 0;
    }
    return 1;
}

static scalar_client_t* locate(scalar_res_t* r, scalar_handle_t h)
{
    scalar_client_t* p;
    if (!r || !r->active || h.slot >= SCALAR_MAX_CLIENTS)
        return NULL;
    p = &r->clients[h.slot];
    return p->exists && p->generation == h.generation ? p : NULL;
}

static int snapshot(clock_composite_t* c, clock_composite_snapshot_t* out)
{
    scalar_client_t *e, *f;
    if (!out || !ready(c))
        return 0;
    e = locate(c->enable, c->enable_client);
    f = locate(c->freq, c->freq_client);
    if (!e || !f) {
        c->quarantined = 1u;
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->enable_present = e->has_req;
    out->enable_req = e->req;
    out->freq_present = f->has_req;
    out->freq_req = f->req;
    out->enable_slot = c->enable_client.slot;
    out->enable_generation = c->enable_client.generation;
    out->freq_slot = c->freq_client.slot;
    out->freq_generation = c->freq_client.generation;
    return 1;
}

int clock_composite_bind(clock_composite_t* c, scalar_res_t* en, scalar_res_t* hz)
{
    if (!c || !en || !hz || en == hz || c->active || c->quarantined ||
        !same_clock(en->name, hz->name) ||
        !((en->aggregate_mode == SCALAR_AGGREGATE_MAX &&
           hz->aggregate_mode == SCALAR_AGGREGATE_MAX) ||
          (en->aggregate_mode == SCALAR_AGGREGATE_CLOCK_ENABLE &&
           hz->aggregate_mode == SCALAR_AGGREGATE_CLOCK_FREQ)) ||
        /* the NPA enable+hz pair is one physical clock. Never mix a per-client native writer with
         * a legacy aggregate writer, nor bind two native clients from different controller
         * generations. */

        (en->aggregate_mode != SCALAR_AGGREGATE_MAX &&
         (en->native_transport != hz->native_transport || !en->owner_epoch ||
          en->owner_epoch != hz->owner_epoch)) ||
        !scalar_verify_owned(en) || !scalar_verify_owned(hz))
        return 0;
    memset(c, 0, sizeof(*c));
    c->enable = en;
    c->freq = hz;
    c->native_npa = (uint8_t)(en->aggregate_mode == SCALAR_AGGREGATE_CLOCK_ENABLE);
    if (!scalar_create(en, &c->enable_client))
        return 0;
    if (!scalar_create(hz, &c->freq_client)) {
        if (!scalar_destroy(en, c->enable_client))
            c->quarantined = 1u;
        return 0;
    }
    c->active = 1u;
    return 1;
}

static int restore_one(scalar_res_t* r, scalar_handle_t h, int present, uint32_t value)
{
    return present ? scalar_issue(r, h, value) : scalar_complete(r, h);
}

int clock_composite_restore(clock_composite_t* c, const clock_composite_snapshot_t* old)
{
    clock_composite_snapshot_t now;
    if (!old || !ready(c) || old->enable_present > 1u || old->freq_present > 1u ||
        old->enable_slot != c->enable_client.slot ||
        old->enable_generation != c->enable_client.generation ||
        old->freq_slot != c->freq_client.slot ||
        old->freq_generation != c->freq_client.generation ||
        (!old->enable_present && old->enable_req != 0u) ||
        (!old->freq_present && old->freq_req != 0u) ||
        (old->enable_present && old->enable_req != 1u && !(c->native_npa && old->enable_req == 2u)))
        return 0;
    /* otherwise withdraw freq first, so no surviving freq request relies on an already
     * disabled clock. */

    if (old->enable_present && old->enable_req == 1u &&
        !restore_one(c->enable, c->enable_client, 1, old->enable_req))
        goto fail;
    if (!restore_one(c->freq, c->freq_client, old->freq_present, old->freq_req))
        goto fail;
    if (old->enable_present && old->enable_req == 2u &&
        !restore_one(c->enable, c->enable_client, 1, 2u))
        goto fail;
    if (!old->enable_present && !restore_one(c->enable, c->enable_client, 0, 0u))
        goto fail;
    if (!snapshot(c, &now) || now.enable_present != old->enable_present ||
        now.freq_present != old->freq_present || now.enable_req != old->enable_req ||
        now.freq_req != old->freq_req)
        goto fail;
    return 1;
fail:
    c->quarantined = 1u;
    return 0;
}

static int recover(clock_composite_t* c, const clock_composite_snapshot_t* old)
{
    /* failure MUST NOT be silently committed. */

    if (!clock_composite_restore(c, old)) {
        if (c->enable)
            c->enable->quarantined = 1u;
        if (c->freq)
            c->freq->quarantined = 1u;
    }
    return 0;
}

int clock_composite_issue(clock_composite_t* c, const power_action_t* a,
                          clock_composite_snapshot_t* old)
{
    clock_action8480_t decoded;
    clock_composite_snapshot_t verify;
    if (!old || !decode_clock_opcode8480(a, &decoded) ||
        decoded.kind != CLOCK8480_ENABLE_SET_FREQ || decoded.parameter0 == 0u || !c ||
        !action_matches_clock(a, c->freq) || !snapshot(c, old))
        return 0;
    /* this is still a mock scalar-core path; no Windows provider or hardware completion is
     * implied. */

    if (!scalar_issue(c->enable, c->enable_client, 1u) ||
        !scalar_issue(c->freq, c->freq_client, decoded.parameter0))
        return recover(c, old);
    if (!snapshot(c, &verify) || !verify.enable_present || verify.enable_req != 1u ||
        !verify.freq_present || verify.freq_req != decoded.parameter0)
        return recover(c, old);
    old->match_opaque = decoded.parameter1;
    return 1;
}

int clock_composite_release(clock_composite_t* c, clock_composite_snapshot_t* old)
{
    clock_composite_snapshot_t check;
    if (!old || !snapshot(c, old))
        return 0;
    if (c->native_npa) {
        /* the freq client is retained until its own final teardown, rather than silently
         * completed. */

        if (!old->enable_present || old->enable_req != 1u || !old->freq_present ||
            !scalar_issue(c->enable, c->enable_client, 2u))
            return recover(c, old);
    } else if (!scalar_complete(c->freq, c->freq_client) ||
               !scalar_complete(c->enable, c->enable_client))
        return recover(c, old);
    if (!snapshot(c, &check) ||
        (c->native_npa ? (!check.enable_present || check.enable_req != 2u || !check.freq_present ||
                          check.freq_req != old->freq_req)
                       : (check.freq_present || check.enable_present)))
        return recover(c, old);
    return 1;
}

int clock_composite_unbind(clock_composite_t* c)
{
    if (!ready(c))
        return 0;
    if (!scalar_destroy(c->freq, c->freq_client) || !scalar_destroy(c->enable, c->enable_client)) {
        c->quarantined = 1u;
        return 0;
    }
    c->active = 0u;
    return 1;
}

int clock_composite_snapshot(clock_composite_t* c, clock_composite_snapshot_t* out)
{
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    return snapshot(c, out);
}

int clock_composite_verify_state(clock_composite_t* c, uint8_t enabled, uint32_t hertz)
{
    clock_composite_snapshot_t now;
    if (enabled > 1u || (enabled && !hertz) || (!enabled && (c->native_npa ? !hertz : !!hertz)) ||
        !snapshot(c, &now))
        return 0;
    if (enabled)
        return now.enable_present && now.enable_req == 1u && now.freq_present &&
               now.freq_req == hertz;
    if (c->native_npa)
        return now.enable_present && now.enable_req == 2u && now.freq_present &&
               now.freq_req == hertz;
    return !now.enable_present && !now.freq_present;
}
