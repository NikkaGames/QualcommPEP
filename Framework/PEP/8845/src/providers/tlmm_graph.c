#include "tlmm_graph.h"
#include <string.h>

#define TLMM_CTL_CONFIG_MASK 0x3ffu
#define TLMM_IO_OUTPUT_MASK 2u

static int action_config(const power_action_t* a, tlmm_gpio_t* cfg)
{
    return a && a->kind == POWER_TLMM_GPIO && tlmm_decode_dsd_t_fields(a->u.gpio.fields, cfg);
}

static int io_ready(const tlmm_graph_ctx_t* c)
{
    const tlmm_io_t* io = c ? c->io : NULL;
    return io && io->lock && io->unlock && io->owns_pin && io->read32 && io->write32 && io->barrier;
}

static int read_current(const tlmm_graph_ctx_t* c, uint32_t pin, uint32_t* ctl, uint32_t* gpio)
{
    const tlmm_io_t* io;
    int ok;
    if (!io_ready(c) || !ctl || !gpio)
        return 0;
    io = c->io;
    if (!io->lock(io->ctx))
        return 0;
    ok = io->owns_pin(io->ctx, pin) && io->read32(io->ctx, TLMM_CTL(pin), ctl) &&
         io->read32(io->ctx, TLMM_IO(pin), gpio) &&
         /* the controller lock alone does not establish a Windows PnP owner epoch. A revoked pin
          * must never be attested by an old readback, even if the register contents are unchanged.
          */

         io->owns_pin(io->ctx, pin);
    io->unlock(io->ctx);
    return ok;
}

static int checked(void* ctx, const char* path, uint32_t state, const power_action_t* action,
                   tlmm_gpio_t* cfg)
{
    (void)path;
    return (state == 0u || state == 3u) && io_ready((tlmm_graph_ctx_t*)ctx) &&
           action_config(action, cfg);
}

static int supports(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    tlmm_gpio_t cfg;
    const tlmm_graph_ctx_t* c = (const tlmm_graph_ctx_t*)ctx;
    uint32_t ctl, gpio;
    return checked(ctx, path, state, action, &cfg) && read_current(c, cfg.pin, &ctl, &gpio);
}

static int snapshot(void* ctx, const char* path, uint32_t state, const power_action_t* action,
                    power_snapshot_t* snap)
{
    tlmm_gpio_t cfg;
    uint32_t ctl, gpio;
    if (!snap || !checked(ctx, path, state, action, &cfg) ||
        !read_current((tlmm_graph_ctx_t*)ctx, cfg.pin, &ctl, &gpio))
        return 0;
    memset(snap, 0, sizeof(*snap));
    snap->opaque[0] = ctl;
    snap->opaque[1] = gpio;
    snap->opaque[2] = cfg.pin;
    snap->opaque[3] = 0x544c4d4du;
    return 1;
}

static int apply(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    tlmm_gpio_t cfg;
    const tlmm_graph_ctx_t* c = (const tlmm_graph_ctx_t*)ctx;
    return checked(ctx, path, state, action, &cfg) && tlmm_apply(c->io, &cfg) == TLMM_OK;
}

static int verify(void* ctx, const char* path, uint32_t state, const power_action_t* action)
{
    tlmm_gpio_t cfg;
    uint32_t ctl, io, expected;
    if (!checked(ctx, path, state, action, &cfg) ||
        !read_current((tlmm_graph_ctx_t*)ctx, cfg.pin, &ctl, &io))
        return 0;
    expected = cfg.pull | (cfg.function << 2u) | (cfg.drive << 6u) | (cfg.direction << 9u);
    return (ctl & TLMM_CTL_CONFIG_MASK) == expected &&
           (io & TLMM_IO_OUTPUT_MASK) == (cfg.state << 1u);
}

static int restore(void* ctx, const char* path, uint32_t state, const power_action_t* action,
                   const power_snapshot_t* snap)
{
    tlmm_gpio_t cfg;
    const tlmm_io_t* io;
    uint32_t offset, ctl, gpio;
    int ok;
    if (!snap || !checked(ctx, path, state, action, &cfg) || snap->opaque[2] != cfg.pin ||
        snap->opaque[3] != 0x544c4d4du || snap->opaque[0] > UINT32_MAX ||
        snap->opaque[1] > UINT32_MAX)
        return 0;
    io = ((tlmm_graph_ctx_t*)ctx)->io;
    if (!io->lock(io->ctx))
        return 0;
    offset = TLMM_CTL(cfg.pin);
    ok = io->owns_pin(io->ctx, cfg.pin) &&
         io->write32(io->ctx, offset + 4u, (uint32_t)snap->opaque[1]) &&
         /* if ownership disappears after the first write, stop rather than write another register
          * against a new controller owner. */

         io->owns_pin(io->ctx, cfg.pin) &&
         io->write32(io->ctx, offset, (uint32_t)snap->opaque[0]) && io->owns_pin(io->ctx, cfg.pin);
    if (ok) {
        io->barrier(io->ctx);
        ok = io->read32(io->ctx, offset, &ctl) && io->read32(io->ctx, offset + 4u, &gpio) &&
             ctl == (uint32_t)snap->opaque[0] && gpio == (uint32_t)snap->opaque[1] &&
             io->owns_pin(io->ctx, cfg.pin);
    }
    io->unlock(io->ctx);
    return ok;
}

static int verify_restored(void* ctx, const char* path, uint32_t state,
                           const power_action_t* action, const power_snapshot_t* snap)
{
    tlmm_gpio_t cfg;
    uint32_t ctl, gpio;
    return snap && checked(ctx, path, state, action, &cfg) && snap->opaque[2] == cfg.pin &&
           snap->opaque[3] == 0x544c4d4du &&
           read_current((tlmm_graph_ctx_t*)ctx, cfg.pin, &ctl, &gpio) && ctl == snap->opaque[0] &&
           gpio == snap->opaque[1];
}

static int final_state(void* ctx, const char* path, uint32_t state,
                       const power_sequence_t* sequence)
{
    uint32_t i, j;
    int seen = 0;
    if (!sequence || !path || strcmp(path, sequence->dev_path) || state != sequence->target_d_state)
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i) {
        tlmm_gpio_t cfg;
        const power_action_t* a = &sequence->actions[i];
        int superseded = 0;
        if (a->kind != POWER_TLMM_GPIO)
            continue;
        seen = 1;
        if (!action_config(a, &cfg))
            return 0;
        for (j = i + 1u; j < sequence->action_cnt; ++j) {
            tlmm_gpio_t newer;
            if (sequence->actions[j].kind != POWER_TLMM_GPIO)
                continue;
            if (!action_config(&sequence->actions[j], &newer))
                return 0;
            if (newer.pin == cfg.pin) {
                superseded = 1;
                break;
            }
        }
        if (!superseded && !verify(ctx, path, state, a))
            return 0;
    }
    return seen;
}

int graph_bind_owned_tlmm(res_graph_t* graph, tlmm_graph_ctx_t* ctx)
{
    graph_provider_t provider;
    if (!graph || !io_ready(ctx))
        return 0;
    memset(&provider, 0, sizeof(provider));
    provider.ctx = ctx;
    provider.supports = supports;
    provider.snapshot = snapshot;
    provider.apply = apply;
    provider.verify = verify;
    provider.restore = restore;
    provider.verify_restored = verify_restored;
    provider.verify_final = final_state;
    return graph_bind(graph, POWER_TLMM_GPIO, &provider);
}
