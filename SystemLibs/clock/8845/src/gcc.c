/* register access is injected; only the authorized Windows adapter maps hardware. */

#include "gcc.h"
#include <string.h>

#define GCC_POLL_LIMIT 2000u

static int rd(const gcc_io_t* io, uint32_t off, uint32_t* v)
{
    return io && io->read32 && io->read32(io->ctx, off, v);
}

static int wr(const gcc_io_t* io, uint32_t off, uint32_t v)
{
    return io && io->write32 && io->write32(io->ctx, off, v);
}

static void barrier(const gcc_io_t* io)
{
    if (io && io->barrier)
        io->barrier(io->ctx);
}

static void stall(const gcc_io_t* io, uint32_t us)
{
    if (io && io->stall_microseconds)
        io->stall_microseconds(io->ctx, us);
}

static int has_register_io(const gcc_io_t* io)
{
    return io && io->read32 && io->write32 && io->barrier && io->stall_microseconds;
}

static int branch_on(uint32_t cbcr)
{

    return (cbcr & 0x80000000u) == 0u;
}

const gcc_clock_t* gcc_find_clock(const char* name)
{
    unsigned i;
    if (!name || !name[0])
        return 0;
    for (i = 0; i < gcc_clock_cnt; ++i)
        if (strcmp(gcc_clocks[i].name, name) == 0)
            return &gcc_clocks[i];
    return 0;
}

const gcc_clock_t* gcc_find_req_clock(const char* name)
{
    const gcc_clock_t* clock = gcc_find_clock(name);
    return clock && clock->kind != GCC_KIND_GDSC ? clock : 0;
}

const gcc_domain_t* gcc_clock_domain(const gcc_clock_t* clock)
{
    if (!clock || clock->domain_idx < 0 || (unsigned)clock->domain_idx >= gcc_domain_cnt)
        return 0;
    return &gcc_domains[clock->domain_idx];
}

gcc_result_t gcc_select_freq(const gcc_clock_t* clock, uint32_t hz, const gcc_freq_t** chosen)
{
    const gcc_domain_t* d;
    const gcc_freq_t* best = 0;
    unsigned i;
    if (chosen)
        *chosen = 0;
    if (!clock || !chosen || hz == 0u)
        return GCC_INVALID;
    if (clock->kind == GCC_KIND_GDSC)
        return GCC_UNSUPPORTED;
    d = gcc_clock_domain(clock);
    if (!d || (d->flags & GCC_DOMAIN_NO_RCG))
        return GCC_UNSUPPORTED;
    if (d->flags & GCC_DOMAIN_READ_ONLY)
        return GCC_READ_ONLY;
    if (d->freq_first > gcc_freq_cnt || d->freq_cnt > gcc_freq_cnt - d->freq_first)
        return GCC_INVALID;
    for (i = 0; i < d->freq_cnt; ++i) {
        const gcc_freq_t* f = &gcc_freqs[d->freq_first + i];
        if (f->src_kind == GCC_SRC_OTHER)
            continue;
        if (f->hz < hz)
            continue;
        if (!best || f->hz < best->hz)
            best = f;
    }
    if (!best)
        return GCC_NO_FREQ;
    *chosen = best;
    return GCC_OK;
}

int gcc_src_ready(const gcc_io_t* io, uint32_t src_kind)
{
    uint32_t mode = 0;
    if (src_kind == GCC_SRC_XO)
        return 1;
    if (src_kind != GCC_SRC_GPLL0 && src_kind != GCC_SRC_GPLL4)
        return 0;
    if (!rd(io, src_kind == GCC_SRC_GPLL4 ? gcc_gpll4_mode_offset : gcc_gpll0_mode_offset, &mode))
        return 0;
    return (mode & PLL_MODE_LOCK_DET) && (mode & PLL_MODE_OUTCTRL);
}

static gcc_result_t wait_update_clear(const gcc_io_t* io, uint32_t cmd_off)
{
    uint32_t i, v;
    for (i = 0; i < GCC_POLL_LIMIT; ++i) {
        if (!rd(io, cmd_off, &v))
            return GCC_IO_FAILED;
        if (!(v & CMD_RCGR_UPDATE))
            return GCC_OK;
        stall(io, 1u);
    }
    return GCC_TIMEOUT;
}

gcc_result_t gcc_set_freq(const gcc_io_t* io, const gcc_clock_t* clock, const gcc_freq_t* freq)
{
    const gcc_domain_t* d;
    uint32_t cfg, cmd, mask, dval;
    gcc_result_t r;
    if (!has_register_io(io) || !clock || !freq)
        return GCC_INVALID;
    d = gcc_clock_domain(clock);
    if (!d || (d->flags & GCC_DOMAIN_NO_RCG) || d->cmd_rcgr_offset == 0u)
        return GCC_UNSUPPORTED;
    if (d->flags & GCC_DOMAIN_READ_ONLY)
        return GCC_READ_ONLY;
    if (freq->div2x < 2u || freq->div2x - 1u > CFG_RCGR_SRC_DIV || freq->mux_sel > 7u)
        return GCC_INVALID;
    if (!gcc_src_ready(io, freq->src_kind))
        return GCC_SRC_OFF;

    r = wait_update_clear(io, d->cmd_rcgr_offset);
    if (r != GCC_OK)
        return r;
    if (!rd(io, d->cmd_rcgr_offset + RCG_CFG_OFFSET, &cfg))
        return GCC_IO_FAILED;
    /* hw_CLK_CONTROL is left exactly as the firmware configured it. */
    cfg &= ~(CFG_RCGR_SRC_SEL | CFG_RCGR_SRC_DIV | CFG_RCGR_MODE);
    cfg |= (freq->mux_sel << CFG_RCGR_SRC_SHIFT) & CFG_RCGR_SRC_SEL;
    cfg |= (freq->div2x - 1u) & CFG_RCGR_SRC_DIV;
    if (freq->m != 0u && freq->m < freq->n) {
        if (d->mnd_width == 0u || d->mnd_width > 16u)
            return GCC_UNSUPPORTED;
        mask = (1u << d->mnd_width) - 1u;
        if (freq->n > mask || freq->m > mask)
            return GCC_INVALID;

        dval = freq->n;
        if (dval > (freq->n - freq->m) * 2u)
            dval = (freq->n - freq->m) * 2u;
        else if (dval < freq->m)
            dval = freq->m;
        if (!wr(io, d->cmd_rcgr_offset + RCG_M_OFFSET, freq->m) ||
            !wr(io, d->cmd_rcgr_offset + RCG_N_OFFSET, (~(freq->n - freq->m)) & mask) ||
            !wr(io, d->cmd_rcgr_offset + RCG_D_OFFSET, (~dval) & mask))
            return GCC_IO_FAILED;
        cfg |= CFG_RCGR_MODE_DUAL;
    }
    if (!wr(io, d->cmd_rcgr_offset + RCG_CFG_OFFSET, cfg))
        return GCC_IO_FAILED;
    barrier(io);
    if (!rd(io, d->cmd_rcgr_offset, &cmd))
        return GCC_IO_FAILED;
    if (!wr(io, d->cmd_rcgr_offset, cmd | CMD_RCGR_UPDATE))
        return GCC_IO_FAILED;
    barrier(io);
    return wait_update_clear(io, d->cmd_rcgr_offset);
}

static gcc_result_t enable_bits(const gcc_io_t* io, uint32_t off, uint32_t mask, int set)
{
    uint32_t v;
    if (!rd(io, off, &v))
        return GCC_IO_FAILED;
    v = set ? (v | mask) : (v & ~mask);
    if (!wr(io, off, v))
        return GCC_IO_FAILED;
    barrier(io);
    return GCC_OK;
}

static gcc_result_t wait_branch(const gcc_io_t* io, uint32_t cbcr_off, int want_on)
{
    uint32_t i, v;
    for (i = 0; i < GCC_POLL_LIMIT; ++i) {
        if (!rd(io, cbcr_off, &v))
            return GCC_IO_FAILED;
        /* molokai ClockDriver.c: HW_CTL delegates gating to hardware. The client vote must be
         * verified separately by the caller; do not require a CLK_OFF transition controlled by
         * another owner. */

        if (want_on && (v & CBCR_CLK_HW_CTL))
            return GCC_OK;
        if (branch_on(v) == (want_on ? 1 : 0))
            return GCC_OK;
        stall(io, 1u);
    }
    return GCC_TIMEOUT;
}

static gcc_result_t wait_gdsc(const gcc_io_t* io, uint32_t gdscr_off, int want_on)
{
    uint32_t i, v;
    for (i = 0; i < GCC_POLL_LIMIT; ++i) {
        if (!rd(io, gdscr_off, &v))
            return GCC_IO_FAILED;
        if (((v & GDSCR_PWR_ON) != 0u) == (want_on ? 1 : 0))
            return GCC_OK;
        stall(io, 1u);
    }
    return GCC_TIMEOUT;
}

gcc_result_t gcc_enable(const gcc_io_t* io, const gcc_clock_t* clock)
{
    gcc_result_t r;
    if (!has_register_io(io) || !clock)
        return GCC_INVALID;
    switch (clock->kind) {
        case GCC_KIND_REQ:
            if (!clock->cbcr_offset)
                return GCC_INVALID;
            return enable_bits(io, clock->cbcr_offset, CBCR_CLK_ENABLE, 1);
        case GCC_KIND_VOTE:
            if (!clock->vote_offset || !clock->vote_mask || !clock->cbcr_offset)
                return GCC_INVALID;
            r = enable_bits(io, clock->vote_offset, clock->vote_mask, 1);
            if (r != GCC_OK)
                return r;
            return wait_branch(io, clock->cbcr_offset, 1);
        case GCC_KIND_BRANCH:
            if (!clock->cbcr_offset)
                return GCC_INVALID;
            r = enable_bits(io, clock->cbcr_offset, CBCR_CLK_ENABLE, 1);
            if (r != GCC_OK)
                return r;
            return wait_branch(io, clock->cbcr_offset, 1);
        case GCC_KIND_GDSC:
            if (!clock->gdscr_offset)
                return GCC_INVALID;
            if (clock->vote_offset && clock->vote_mask)
                r = enable_bits(io, clock->vote_offset, clock->vote_mask, 0);
            else
                r = enable_bits(io, clock->gdscr_offset, GDSCR_SW_COLLAPSE, 0);
            if (r != GCC_OK)
                return r;
            return wait_gdsc(io, clock->gdscr_offset, 1);
        default:
            return GCC_UNSUPPORTED;
    }
}

gcc_result_t gcc_disable(const gcc_io_t* io, const gcc_clock_t* clock)
{
    gcc_result_t r;
    uint32_t v = 0u;
    if (!has_register_io(io) || !clock)
        return GCC_INVALID;
    switch (clock->kind) {
        case GCC_KIND_VOTE:
            if (!clock->vote_offset || !clock->vote_mask || !clock->cbcr_offset)
                return GCC_INVALID;
            r = enable_bits(io, clock->vote_offset, clock->vote_mask, 0);
            if (r != GCC_OK)
                return r;

            return GCC_OK;
        case GCC_KIND_REQ:
        case GCC_KIND_BRANCH:
            if (!clock->cbcr_offset)
                return GCC_INVALID;
            r = enable_bits(io, clock->cbcr_offset, CBCR_CLK_ENABLE, 0);
            if (r != GCC_OK)
                return r;
            /* another voter can retain CLK_ON. The caller proves this owner's request bit was
             * withdrawn; polling CLK_OFF adds no such proof. */
            return GCC_OK;
        case GCC_KIND_GDSC:
            if (!clock->gdscr_offset)
                return GCC_INVALID;
            if (clock->vote_offset && clock->vote_mask)
                r = enable_bits(io, clock->vote_offset, clock->vote_mask, 1);
            else
                r = enable_bits(io, clock->gdscr_offset, GDSCR_SW_COLLAPSE, 1);
            if (r != GCC_OK)
                return r;
            /* a second firmware/hardware voter may hold PWR_ON even after our SW_COLLAPSE request.
             * Nevertheless the requested bit MUST survive a fresh MMIO read; a dropped write cannot
             * succeed. */

            if (!rd(io, clock->gdscr_offset, &v))
                return GCC_IO_FAILED;
            if (!(v & GDSCR_SW_COLLAPSE))
                return GCC_TIMEOUT;
            return GCC_OK;
        default:
            return GCC_UNSUPPORTED;
    }
}

static const gcc_freq_t* detect_freq(const gcc_domain_t* d, uint32_t cfg, uint32_t mreg,
                                     uint32_t nreg)
{
    uint32_t mux = (cfg & CFG_RCGR_SRC_SEL) >> CFG_RCGR_SRC_SHIFT;
    uint32_t div = cfg & CFG_RCGR_SRC_DIV;
    uint32_t div2x = div == 0u ? 2u : div + 1u;
    uint32_t m = 0, n = 0, i, mask;
    if (d->mnd_width && (cfg & CFG_RCGR_MODE)) {
        mask = d->mnd_width >= 32u ? 0xFFFFFFFFu : ((1u << d->mnd_width) - 1u);
        m = mreg & mask;
        n = ((~nreg) & mask) + m;
    }
    if (d->freq_first > gcc_freq_cnt || d->freq_cnt > gcc_freq_cnt - d->freq_first)
        return 0u;
    for (i = 0; i < d->freq_cnt; ++i) {
        const gcc_freq_t* f = &gcc_freqs[d->freq_first + i];
        int mnd = f->m != 0u && f->m < f->n;
        if (f->mux_sel != mux || f->div2x != div2x)
            continue;
        if (mnd ? (f->m == m && f->n == n) : (m == 0u))
            return f;
    }
    return 0u;
}

static uint32_t detect_hz(const gcc_domain_t* domain, uint32_t cfg, uint32_t m, uint32_t n)
{
    const gcc_freq_t* freq = detect_freq(domain, cfg, m, n);
    return freq ? freq->hz : 0u;
}

gcc_result_t gcc_readback(const gcc_io_t* io, const gcc_clock_t* clock, gcc_state_t* state)
{
    const gcc_domain_t* d;
    uint32_t v = 0;
    if (!state)
        return GCC_INVALID;
    memset(state, 0, sizeof(*state));
    if (!io || !io->read32 || !clock)
        return GCC_INVALID;
    if (clock->kind == GCC_KIND_GDSC) {
        if (!rd(io, clock->gdscr_offset, &v))
            return GCC_IO_FAILED;
        state->cbcr = v;
        state->on = (v & GDSCR_PWR_ON) ? 1u : 0u;
        if (clock->vote_offset && clock->vote_mask) {
            if (!rd(io, clock->vote_offset, &state->vote_reg))
                return GCC_IO_FAILED;
            state->enabled = (state->vote_reg & clock->vote_mask) ? 0u : 1u;
        } else {
            state->enabled = (v & GDSCR_SW_COLLAPSE) ? 0u : 1u;
        }
        return GCC_OK;
    }
    if (!rd(io, clock->cbcr_offset, &v))
        return GCC_IO_FAILED;
    state->cbcr = v;
    state->on = branch_on(v) ? 1u : 0u;
    if (clock->kind == GCC_KIND_VOTE) {
        if (!rd(io, clock->vote_offset, &state->vote_reg))
            return GCC_IO_FAILED;
        state->enabled = (state->vote_reg & clock->vote_mask) ? 1u : 0u;
    } else {
        state->enabled = (v & CBCR_CLK_ENABLE) ? 1u : 0u;
    }
    d = gcc_clock_domain(clock);
    /* a BSP RCG address without a freq table is not readable authorization. Reading
     * GCCCONFIGNOC RCG 0x74400 caused WHEA 0x124. validate CBCR/vote only, report unknown hz,
     * and refuse rate programming. */
    if (d && d->freq_cnt != 0u && d->cmd_rcgr_offset && !(d->flags & GCC_DOMAIN_NO_RCG)) {
        if (!rd(io, d->cmd_rcgr_offset, &state->cmd_rcgr) ||
            !rd(io, d->cmd_rcgr_offset + RCG_CFG_OFFSET, &state->cfg_rcgr))
            return GCC_IO_FAILED;
        if (d->mnd_width && (state->cfg_rcgr & CFG_RCGR_MODE)) {
            if (!rd(io, d->cmd_rcgr_offset + RCG_M_OFFSET, &state->m) ||
                !rd(io, d->cmd_rcgr_offset + RCG_N_OFFSET, &state->n))
                return GCC_IO_FAILED;
        }
        state->hz = detect_hz(d, state->cfg_rcgr, state->m, state->n);
    }
    return GCC_OK;
}

uint64_t gcc_pack_state(const gcc_state_t* state)
{
    uint64_t v;
    if (!state)
        return 0u;
    v = (uint64_t)state->hz;
    v |= (uint64_t)(state->on ? 1u : 0u) << 32;
    v |= (uint64_t)(state->enabled ? 1u : 0u) << 33;
    v |= (uint64_t)((state->cbcr >> CBCR_CLK_OFF_SHIFT) & 0xFu) << 36;
    v |= (uint64_t)(state->cfg_rcgr & 0x3F1Fu) << 40;
    return v;
}

typedef struct dfs_snapshot {
    uint32_t control;
    uint32_t slots[GCC_DFS_MAX_STATES][4];
} dfs_snapshot_t;

static int dfs_snapshot(const gcc_io_t* io, const gcc_domain_t* domain, dfs_snapshot_t* snapshot)
{
    static const uint32_t offsets[4] = {0x1cu, 0x5cu, 0x9cu, 0xdcu};
    uint32_t i, j, control;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!rd(io, domain->cmd_rcgr_offset + 0x14u, &control))
        return 0;
    /* curr_PERF_STATE follows the GENI consumer and is not part of the immutable slot table.
     * Never mask software-override/control changes. */
    snapshot->control = control & ~0x1eu;
    if (!(control & 1u))
        return 1;
    for (i = 0u; i < domain->dfs_states; ++i)
        for (j = 0u; j < 4u; ++j)
            if (!rd(io, domain->cmd_rcgr_offset + offsets[j] + i * 4u, &snapshot->slots[i][j]))
                return 0;
    if (!rd(io, domain->cmd_rcgr_offset + 0x14u, &control) ||
        snapshot->control != (control & ~0x1eu))
        return 0;
    return 1;
}

int gcc_read_dfs_catalog(const gcc_io_t* io, const gcc_clock_t* clock, gcc_dfs_catalog_t* out)
{
    const gcc_domain_t* domain = gcc_clock_domain(clock);
    dfs_snapshot_t first, second;
    uint32_t i, attempt, tail = 0u, mask;
    uint64_t fingerprint = UINT64_C(14695981039346656037);
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!clock || !domain || clock->kind == GCC_KIND_GDSC)
        return 0;
    if (!domain->dfs_states)
        return 1;
    if (!io || !io->read32 || domain->flags || !domain->cmd_rcgr_offset || !domain->freq_cnt ||
        !domain->mnd_width || domain->mnd_width > 16u || domain->dfs_states > GCC_DFS_MAX_STATES ||
        domain->freq_first > gcc_freq_cnt || domain->freq_cnt > gcc_freq_cnt - domain->freq_first ||
        domain->cmd_rcgr_offset > GCC_WINDOW_BYTES - 0x120u)
        return 0;
    for (attempt = 0u; attempt < 4u; ++attempt) {
        if (!dfs_snapshot(io, domain, &first) || !dfs_snapshot(io, domain, &second))
            return 0;
        if (!memcmp(&first, &second, sizeof(first)))
            break;
    }
    if (attempt == 4u)
        return 0;
    if (!(second.control & 1u))
        return 1;
    /* only the inherited hardware-controlled mode is supported. A software override would
     * disconnect GENI's slot selection from this catalog. */
    if (second.control != 0x21u)
        return 0;
    mask = (1u << domain->mnd_width) - 1u;
    for (i = 0u; i < domain->dfs_states; ++i) {
        const uint32_t* slot = second.slots[i];
        const gcc_freq_t* match = NULL;
        uint32_t cfg = slot[0], hz;
        if (!(slot[0] | slot[1] | slot[2] | slot[3])) {
            tail = 1u;
            continue;
        }
        /* a loader may program slots 0 and 2 only. Keep the usable prefix before the gap; exposing
         * slot 2 as array idx 1 would change the consumer's hardware selection. hash the omitted
         * suffix as well. */
        if (tail)
            continue;
        match = detect_freq(domain, cfg, slot[1], slot[2]);
        hz = match ? match->hz : 0u;
        if (!hz || (out->cnt && hz <= out->hertz[out->cnt - 1u]))
            return 0;
        if (!match || match->src_kind > GCC_SRC_GPLL4)
            return 0;
        if (match->m && match->m < match->n) {
            uint32_t duty = match->n;
            if (duty > (match->n - match->m) * 2u)
                duty = (match->n - match->m) * 2u;
            else if (duty < match->m)
                duty = match->m;
            if ((cfg & CFG_RCGR_MODE) != CFG_RCGR_MODE_DUAL || (slot[3] & mask) != ((~duty) & mask))
                return 0;
        } else if (cfg & CFG_RCGR_MODE)
            return 0;
        out->hertz[out->cnt++] = hz;
    }
    if (!out->cnt)
        return 0;
    /* hash the complete physical snapshot, including omitted reset slots. */
    for (i = 0u; i < sizeof(second); ++i)
        fingerprint = (fingerprint ^ ((const unsigned char*)&second)[i]) * UINT64_C(1099511628211);
    out->fingerprint = fingerprint;
    out->enabled = 1u;
    return 1;
}
