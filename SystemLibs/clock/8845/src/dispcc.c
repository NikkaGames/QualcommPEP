#include "dispcc.h"
#include <string.h>

/* Molokai DISP CRM v3, software DRV1, bandwidth node zero. */
#define VOTE_ACTIVE 0x1100u
#define VOTE_SLEEP 0x1104u
#define VOTE_WAKE 0x1108u
#define VOTE_IRQ_STATUS 0x1170u
#define VOTE_IRQ_CLEAR 0x1174u
#define VOTE_IRQ_ENABLE 0x1178u
#define VOTE_FSM 0x117Cu
#define MDP_CMD 0x8164u
#define MDP_CFG 0x8168u
#define WAIT_POLLS 20000u

static const uint32_t mdp_rates[] = {19200000u,  100000000u, 156000000u, 207000000u, 337000000u,
                                     417000000u, 532000000u, 600000000u, 650000000u};

static int rd(const gcc_io_t* io, uint32_t off, uint32_t* value)
{
    return io && io->read32 && io->read32(io->ctx, off, value) && *value != UINT32_MAX;
}

static int wr(const gcc_io_t* io, uint32_t off, uint32_t value)
{
    if (!io || !io->write32 || !io->barrier || !io->write32(io->ctx, off, value))
        return 0;
    io->barrier(io->ctx);
    return 1;
}

static int poll(const gcc_io_t* io, uint32_t off, uint32_t mask, uint32_t want)
{
    uint32_t i, value;
    if (!io->stall_microseconds)
        return 0;
    for (i = 0u; i < WAIT_POLLS; ++i) {
        if (!rd(io, off, &value))
            return 0;
        if ((value & mask) == want)
            return 1;
        io->stall_microseconds(io->ctx, 1u);
    }
    return 0;
}

int dispcc_rate_group(const gcc_clock_t* clock)
{
    /* The MDP branches share one CESTA-controlled RCG. */
    return clock && clock->controller == CLOCK_CTL_DISPCC &&
           (clock->cbcr_offset == 0x8010u || clock->cbcr_offset == 0x8020u ||
            clock->cbcr_offset == 0x8030u || clock->cbcr_offset == 0xA004u ||
            clock->cbcr_offset == 0xA014u || clock->cbcr_offset == 0xE010u);
}

int dispcc_probe(dispcc_t* d)
{
    uint32_t version, cfg, nodes, enabled, status;
    if (!d || d->quarantined || !rd(&d->common, 0u, &version) || !rd(&d->common, 4u, &cfg) ||
        !rd(&d->common, 8u, &nodes) || !rd(&d->common, 12u, &enabled) ||
        !rd(&d->crm, VOTE_FSM, &status))
        return 0;
    return ((version >> 16) & 0xffu) == 3u && ((cfg >> 18) & 0x1fu) > 1u && ((cfg >> 23) & 7u) &&
           ((nodes >> 26) & 0x1fu) && (enabled & 1u);
}

static int mdp_hz(dispcc_t* d, uint32_t* hz)
{
    uint32_t mode, l, alpha, user, cfg, divisor, source;
    uint64_t rate;
    if (!rd(&d->clocks, MDP_CFG, &cfg))
        return 0;
    source = (cfg >> 8) & 7u;
    divisor = (cfg & 31u) + 1u;
    if (divisor < 2u)
        divisor = 2u;
    if (!source) {
        *hz = (uint32_t)(UINT64_C(19200000) * 2u / divisor);
        return 1;
    }
    if (source != 1u || !rd(&d->clocks, 0u, &mode) || !rd(&d->clocks, 0x10u, &l) ||
        !rd(&d->clocks, 0x14u, &alpha) || !rd(&d->clocks, 0x18u, &user) || !(mode & 0x80000000u) ||
        (user & 0x10000000u))
        return 0;
    rate = (UINT64_C(19200000) * (((uint64_t)(l & 0xffu) << 16) | (alpha & 0xffffu))) >> 16;
    *hz = (uint32_t)(rate * 2u / divisor);
    return *hz != 0u;
}

int dispcc_read(dispcc_t* d, const gcc_clock_t* clock, gcc_state_t* state)
{
    if (!d || d->quarantined || !clock || clock->controller != CLOCK_CTL_DISPCC ||
        gcc_readback(&d->clocks, clock, state) != GCC_OK)
        return 0;
    if (!dispcc_rate_group(clock))
        return 1;
    if (!rd(&d->clocks, MDP_CMD, &state->cmd_rcgr) || !rd(&d->clocks, MDP_CFG, &state->cfg_rcgr))
        return 0;
    if (mdp_hz(d, &state->hz))
        return 1;
    /* A disabled root has no running rate to report. Its CRM request is snapshotted
     * separately and is not inferred from the stopped PLL configuration. */
    return !state->enabled && !state->on && (state->cmd_rcgr & CMD_RCGR_ROOT_OFF);
}

int dispcc_verify(dispcc_t* d, const gcc_clock_t* clock, uint32_t minimum_hz)
{
    gcc_state_t a, b;
    uint32_t vote;
    if (!dispcc_read(d, clock, &a) || !dispcc_read(d, clock, &b) || !a.enabled || !b.enabled ||
        (!a.on && !(a.cbcr & CBCR_CLK_HW_CTL)) || (!b.on && !(b.cbcr & CBCR_CLK_HW_CTL)) ||
        ((a.cbcr ^ b.cbcr) & (CBCR_CLK_ENABLE | CBCR_CLK_HW_CTL)))
        return 0;
    if (!minimum_hz)
        return 1;
    return dispcc_rate_group(clock) && dispcc_probe(d) && rd(&d->crm, VOTE_ACTIVE, &vote) &&
           (vote & 0x20000000u) && (vote & 0x3fffu) >= minimum_hz / 1000000u &&
           (uint64_t)a.hz + 2000u >= minimum_hz && (uint64_t)b.hz + 2000u >= minimum_hz;
}

int dispcc_rate_snapshot(dispcc_t* d, uint64_t words[3])
{
    static const uint32_t offsets[] = {VOTE_ACTIVE, VOTE_SLEEP, VOTE_WAKE};
    uint32_t i, a, b, irq;
    if (!words || !dispcc_probe(d) || !rd(&d->crm, VOTE_IRQ_STATUS, &irq) || (irq & 1u))
        return 0;
    for (i = 0u; i < 3u; ++i) {
        if (!rd(&d->crm, offsets[i], &a) || !rd(&d->crm, offsets[i], &b) || a != b ||
            (a & ~0xe0003fffu))
            return 0;
        words[i] = i ? a : a & 0x7fffffffu;
    }
    return 1;
}

static gcc_commit_result_t send_vote(dispcc_t* d, uint32_t data, uint32_t sleep_data,
                                     uint32_t wake_data)
{
    uint32_t before, irq, sleep, wake;
    if (!dispcc_probe(d) || !rd(&d->crm, VOTE_ACTIVE, &before) ||
        !rd(&d->crm, VOTE_IRQ_STATUS, &irq) || (irq & 1u) || !rd(&d->crm, VOTE_SLEEP, &sleep) ||
        !rd(&d->crm, VOTE_WAKE, &wake))
        return GCC_COMMIT_DECLINED;
    if ((before & 0x20003fffu) == (data & 0x20003fffu) && sleep == sleep_data && wake == wake_data)
        return GCC_COMMIT_OK;
    if (!rd(&d->crm, VOTE_IRQ_ENABLE, &d->irq_enable))
        return GCC_COMMIT_DECLINED;
    /* Completion is polled at PASSIVE_LEVEL. Preserve other nodes' interrupt masks. */
    ++d->write_sequence;
    if (!rd(&d->crm, VOTE_IRQ_ENABLE, &irq) || !wr(&d->crm, VOTE_IRQ_ENABLE, irq & ~1u) ||
        !wr(&d->crm, VOTE_ACTIVE, data | 0xc0000000u) || !poll(&d->crm, VOTE_IRQ_STATUS, 1u, 1u) ||
        !wr(&d->crm, VOTE_IRQ_CLEAR, 1u) || !poll(&d->crm, VOTE_IRQ_STATUS, 1u, 0u) ||
        !wr(&d->crm, VOTE_SLEEP, sleep_data) || !wr(&d->crm, VOTE_WAKE, wake_data) ||
        !rd(&d->crm, VOTE_ACTIVE, &before) || (before & 0x20003fffu) != (data & 0x20003fffu) ||
        !rd(&d->crm, VOTE_SLEEP, &sleep) || sleep != sleep_data || !rd(&d->crm, VOTE_WAKE, &wake) ||
        wake != wake_data || !rd(&d->crm, VOTE_IRQ_ENABLE, &irq) ||
        !wr(&d->crm, VOTE_IRQ_ENABLE, (irq & ~1u) | (d->irq_enable & 1u)) ||
        !rd(&d->crm, VOTE_IRQ_ENABLE, &irq) || (irq & 1u) != (d->irq_enable & 1u)) {
        d->quarantined = 1u;
        return GCC_COMMIT_UNCERTAIN;
    }
    return GCC_COMMIT_OK;
}

gcc_commit_result_t dispcc_rate_restore(dispcc_t* d, const uint64_t words[3])
{
    uint32_t i;
    if (!words)
        return GCC_COMMIT_DECLINED;
    for (i = 0u; i < 3u; ++i)
        if (words[i] & ~(i ? UINT64_C(0xe0003fff) : UINT64_C(0x60003fff)))
            return GCC_COMMIT_DECLINED;
    /* A zero request cancels this software vote; retain each bank's saved VALID bit. */
    return send_vote(d, (uint32_t)words[0], (uint32_t)words[1], (uint32_t)words[2]);
}

static gcc_commit_result_t rate_vote(dispcc_t* d, uint32_t hz)
{
    uint32_t i, data;
    for (i = 0u; hz && i < sizeof(mdp_rates) / sizeof(mdp_rates[0]); ++i)
        if (mdp_rates[i] >= hz)
            break;
    if (hz && i == sizeof(mdp_rates) / sizeof(mdp_rates[0]))
        return GCC_COMMIT_DECLINED;
    if (hz)
        hz = mdp_rates[i];
    /* CRMB consumes MHz requests and performs the source/voltage transition. */
    data = 0xe0000000u | (hz / 1000000u);
    return send_vote(d, data, data, data);
}

gcc_commit_result_t dispcc_branch_restore(dispcc_t* d, const gcc_clock_t* clock, int enable)
{
    gcc_state_t state;
    uint32_t i;
    if (!d || !clock || d->quarantined || !d->clocks.stall_microseconds ||
        !dispcc_read(d, clock, &state))
        return GCC_COMMIT_DECLINED;
    if (state.enabled != (enable != 0)) {
        ++d->write_sequence;
        if ((enable ? gcc_enable(&d->clocks, clock) : gcc_disable(&d->clocks, clock)) != GCC_OK) {
            d->quarantined = 1u;
            return GCC_COMMIT_UNCERTAIN;
        }
    }
    for (i = 0u; i < WAIT_POLLS; ++i) {
        if (dispcc_read(d, clock, &state) && state.enabled == (enable != 0) &&
            (!enable || state.on || (state.cbcr & CBCR_CLK_HW_CTL)))
            return GCC_COMMIT_OK;
        d->clocks.stall_microseconds(d->clocks.ctx, 1u);
    }
    d->quarantined = 1u;
    return GCC_COMMIT_UNCERTAIN;
}

gcc_commit_result_t dispcc_commit(dispcc_t* d, const gcc_clock_t* clock, uint32_t hz, int enable,
                                  int rate_only, int update_rate, uint32_t* programmed_hz)
{
    gcc_commit_result_t result;
    gcc_state_t state;
    uint32_t before;
    if (!d || !clock || !programmed_hz || d->quarantined || clock->controller != CLOCK_CTL_DISPCC ||
        (hz && !dispcc_rate_group(clock)) || !dispcc_read(d, clock, &state))
        return GCC_COMMIT_DECLINED;
    before = d->write_sequence;
    if (dispcc_rate_group(clock) && enable && !update_rate &&
        (state.cmd_rcgr & CMD_RCGR_ROOT_OFF)) {
        hz = 19200000u;
        update_rate = 1;
    }
    if (dispcc_rate_group(clock) && update_rate) {
        result = rate_vote(d, hz);
        if (result != GCC_COMMIT_OK)
            return result;
        *programmed_hz = hz;
    }
    if (!rate_only) {
        result = dispcc_branch_restore(d, clock, enable);
        if (result != GCC_COMMIT_OK) {
            if (d->write_sequence != before) {
                d->quarantined = 1u;
                return GCC_COMMIT_UNCERTAIN;
            }
            return result;
        }
    }
    if (enable && !dispcc_verify(d, clock, hz)) {
        d->quarantined = 1u;
        return GCC_COMMIT_UNCERTAIN;
    }
    return GCC_COMMIT_OK;
}
