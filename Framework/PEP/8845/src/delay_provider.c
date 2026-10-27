#include "delay_provider.h"

static int valid(const power_action_t* action)
{
    return action && action->kind == POWER_DELAY && action->u.delay.ms <= 5000u;
}

static int supports(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    const delay_provider_t* p = (const delay_provider_t*)ctx;
    return p && p->wait_ms && path && (state == 0u || state == 3u) && valid(action);
}

static int snapshot(void* ctx, const char* path, uint32_t state, const power_action_t* action,
                    power_snapshot_t* snap)
{
    (void)ctx;
    (void)path;
    (void)state;
    (void)action;
    (void)snap;
    /* timing has no restorable hardware state. Power txn must skip snapshot. */
    return 0;
}

static int apply(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    delay_provider_t* p = (delay_provider_t*)ctx;
    return supports(ctx, path, state, action) && p->wait_ms(p->ctx, action->u.delay.ms);
}

static int verify(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    return supports(ctx, path, state, action);
}

static int restore(void* ctx, const char* path, uint32_t state, const power_action_t* action,
                   const power_snapshot_t* snap)
{
    (void)ctx;
    (void)path;
    (void)state;
    (void)action;
    (void)snap;
    /* an elapsed delay cannot be undone. */
    return 0;
}

static int verify_state(void* ctx, const char* path, uint32_t state,
                        const power_sequence_t* sequence)
{
    uint32_t i;
    if (!sequence || sequence->target_d_state != state)
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i)
        if (sequence->actions[i].kind == POWER_DELAY &&
            !supports(ctx, path, state, &sequence->actions[i]))
            return 0;
    return 1;
}

int graph_bind_delay(res_graph_t* graph, delay_provider_t* delay)
{
    graph_provider_t p = {0};
    if (!graph || !delay || !delay->wait_ms)
        return 0;
    p.ctx = delay;
    p.supports = supports;
    p.snapshot = snapshot;
    p.apply = apply;
    p.verify = verify;
    p.restore = restore;
    p.verify_restored = restore;
    p.verify_final = verify_state;
    return graph_bind(graph, POWER_DELAY, &p);
}
