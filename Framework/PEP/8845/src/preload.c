#include "preload.h"
#include <string.h>

#define LANE_D 1024u
#define LANE_F 1025u

static int append(const aeob_preload_action_t* a, void* ctx)
{
    preload_t* s = ctx;
    uint32_t i = s->sequence.action_cnt;
    if (!power_append_action(&s->sequence, &a->resource, a->kind != AEOB_D_STATE)) {
        memcpy(s->failure_kind, a->resource.kind, sizeof(s->failure_kind));
        s->trace.failure_idx = i;
        return 0;
    }
    if (a->kind == AEOB_D_STATE)
        s->lane[i] = LANE_D;
    else if (a->kind == AEOB_F_STATE)
        s->lane[i] = (uint16_t)(LANE_F + a->component);
    else
        s->lane[i] = (uint16_t)(a->component * 32u + a->set);
    return 1;
}

power_result_t preload_compile(preload_t* s, const void* blob, size_t bytes, const char* path)
{
    size_t len;
    uint32_t i, delay = 0u;
    aeob_status_t status;
    if (!s || !blob || !path || s->committed || s->quarantined)
        return POWER_BAD_INPUT;
    len = strlen(path);
    if (len < 5u || len > 230u || path[0] != '\\')
        return POWER_BAD_INPUT;
    memset(&s->sequence, 0, sizeof(s->sequence));
    memset(&s->trace, 0, sizeof(s->trace));
    s->trace.failure_idx = UINT32_MAX;
    memset(s->failure_kind, 0, sizeof(s->failure_kind));
    memcpy(s->sequence.dev_path, path, len + 1u);
    status = aeo_b_visit_preload(blob, bytes, path, append, s, &s->selected);
    if (status != AEO_B_OK) {
        int full = s->sequence.action_cnt == POWER_MAX_ACTIONS;
        s->sequence.action_cnt = 0u;
        if (status != AEO_B_CALLBACK_ERROR)
            return POWER_INVALID_PROGRAM;
        return full ? POWER_TOO_MANY_ACTIONS : POWER_UNSUPPORTED;
    }
    for (i = 0u; i < s->sequence.action_cnt; ++i) {
        if (s->sequence.actions[i].kind == POWER_DELAY) {
            uint32_t ms = s->sequence.actions[i].u.delay.ms;
            if (ms > 5000u || delay > 10000u - ms) {
                s->sequence.action_cnt = 0u;
                return POWER_INVALID_PROGRAM;
            }
            delay += ms;
        }
    }
    return POWER_OK;
}

static int identity(preload_t* s, uint32_t lane, char out[256])
{
    size_t n = strlen(s->sequence.dev_path);
    uint32_t c, set;
    if (n > 230u || lane >= LANE_F + 32u)
        return 0;
    memcpy(out, s->sequence.dev_path, n);
    memcpy(out + n, "#PREL#", 6u);
    n += 6u;
    if (lane == LANE_D) {
        out[n++] = 'D';
    } else {
        c = lane >= LANE_F ? lane - LANE_F : lane / 32u;
        out[n++] = 'C';
        if (c >= 10u)
            out[n++] = (char)('0' + c / 10u);
        out[n++] = (char)('0' + c % 10u);
        out[n++] = '#';
        out[n++] = lane >= LANE_F ? 'F' : 'S';
        if (lane < LANE_D) {
            set = lane % 32u;
            if (set >= 10u)
                out[n++] = (char)('0' + set / 10u);
            out[n++] = (char)('0' + set % 10u);
        }
    }
    out[n] = 0;
    return 1;
}

static int enter(void* ctx)
{
    preload_t* s = ctx;
    if (!s || s->held || !s->serial || !s->begin || !s->end || !s->begin(s->ctx, &s->provider))
        return 0;
    if (!s->provider.supports || !s->provider.snapshot || !s->provider.apply ||
        !s->provider.verify || !s->provider.restore || !s->provider.verify_restored ||
        !s->provider.verify_final) {
        s->end(s->ctx);
        memset(&s->provider, 0, sizeof(s->provider));
        return 0;
    }
    s->held = 1u;
    return 1;
}

static void release_provider(void* ctx)
{
    preload_t* s = ctx;
    if (s && s->held) {
        s->held = 0u;
        s->end(s->ctx);
        memset(&s->provider, 0, sizeof(s->provider));
    }
}

static int index(preload_t* s, const power_action_t* a)
{
    uint32_t i;
    if (!s || !s->held || !a)
        return -1;
    for (i = 0u; i < s->sequence.action_cnt; ++i)
        if (a == &s->sequence.actions[i])
            return (int)i;
    return -1;
}

#define RESOURCE_CALLBACK(name, member)                                                            \
    static int name(void* ctx, const power_action_t* a)                                            \
    {                                                                                              \
        preload_t* s = ctx;                                                                        \
        char client[256];                                                                          \
        int i = index(s, a);                                                                       \
        return i >= 0 && identity(s, s->lane[i], client) &&                                        \
               s->provider.member(s->provider.ctx, client, 0u, a);                                 \
    }
RESOURCE_CALLBACK(supports, supports)
RESOURCE_CALLBACK(apply, apply)
RESOURCE_CALLBACK(verify, verify)

#define SNAPSHOT_CALLBACK(name, member, qualifier)                                                 \
    static int name(void* ctx, const power_action_t* a, qualifier power_snapshot_t* p)             \
    {                                                                                              \
        preload_t* s = ctx;                                                                        \
        char client[256];                                                                          \
        int i = index(s, a);                                                                       \
        return i >= 0 && identity(s, s->lane[i], client) &&                                        \
               s->provider.member(s->provider.ctx, client, 0u, a, p);                              \
    }
SNAPSHOT_CALLBACK(snapshot, snapshot, )
SNAPSHOT_CALLBACK(restore, restore, const)
SNAPSHOT_CALLBACK(restored, verify_restored, const)

static int verify_final(void* ctx, const power_sequence_t* sequence)
{
    preload_t* s = ctx;
    uint32_t i, j, k;
    if (!s || !s->held || sequence != &s->sequence)
        return 0;
    for (i = 0u; i < sequence->action_cnt; ++i) {
        for (j = 0u; j < i; ++j)
            if (s->lane[j] == s->lane[i])
                break;
        if (j != i)
            continue;
        memset(&s->group, 0, sizeof(s->group));
        if (!identity(s, s->lane[i], s->group.dev_path))
            return 0;
        for (k = i; k < sequence->action_cnt; ++k)
            if (s->lane[k] == s->lane[i])
                s->group.actions[s->group.action_cnt++] = sequence->actions[k];
        if (!s->provider.verify_final(s->provider.ctx, s->group.dev_path, 0u, &s->group))
            return 0;
    }
    return 1;
}

power_result_t preload_execute(preload_t* s)
{
    power_result_t result;
    if (!s || s->committed || s->quarantined)
        return POWER_BAD_INPUT;
    s->adapter.ctx = s;
    s->adapter.lock = enter;
    s->adapter.unlock = release_provider;
    s->adapter.supports = supports;
    s->adapter.snapshot = snapshot;
    s->adapter.apply = apply;
    s->adapter.verify = verify;
    s->adapter.restore = restore;
    s->adapter.verify_restored = restored;
    s->adapter.verify_current_state = verify_final;
    result = power_execute_action_batch(&s->sequence, &s->adapter, &s->trace, &s->baseline);
    s->committed = result == POWER_OK;
    s->quarantined = result == POWER_ROLLBACK_FAILED;
    return result;
}

int preload_rollback(preload_t* s)
{
    uint32_t i;
    int ok = 1;
    if (!s || s->quarantined)
        return 0;
    if (!s->committed)
        return 1;
    if (!enter(s))
        return 0;
    for (i = s->sequence.action_cnt; i > 0u; --i) {
        const power_action_t* a = &s->sequence.actions[i - 1u];
        if (a->kind == POWER_DELAY)
            continue;
        if (!restore(s, a, &s->baseline.snapshots[i - 1u]) ||
            !restored(s, a, &s->baseline.snapshots[i - 1u]))
            ok = 0;
    }
    release_provider(s);
    if (ok)
        s->committed = 0u;
    else
        s->quarantined = 1u;
    return ok;
}
