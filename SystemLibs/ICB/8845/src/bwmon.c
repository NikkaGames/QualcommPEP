#include "bwmon.h"
#include <string.h>

enum {
    REG_ENABLE,
    REG_CLEAR,
    REG_WINDOW,
    REG_HIGH,
    REG_MEDIUM,
    REG_LOW,
    REG_ACTIONS,
    REG_ZONE_THRESHOLD,
    REG_COUNTS,
    REG_MAXIMUM,
    REG_STATUS,
    REG_IRQ_CLEAR,
    REG_IRQ_ENABLE,
    REG_FILTER,
    REG_MATCH,
};

static const uint16_t offsets[2][15] = {
    {0x2a0, 0x2a4, 0x2a8, 0x2ac, 0x2b0, 0x2b4, 0x2b8, 0x2bc, 0x2d8, 0x2e0, 0x100, 0x108, 0x10c,
     0x298, 0x29c},
    {0x010, 0x014, 0x020, 0x024, 0x028, 0x02c, 0x030, 0x034, 0x040, 0x044, 0x000, 0x008, 0x00c,
     0x018, 0x01c},
};
static const uint8_t saved_regs[] = {
    REG_ENABLE,  REG_WINDOW,         REG_HIGH,       REG_MEDIUM, REG_LOW,
    REG_ACTIONS, REG_ZONE_THRESHOLD, REG_IRQ_ENABLE, REG_FILTER, REG_MATCH,
};

static uint32_t offset(const bwmon_t* m, unsigned reg)
{
    return offsets[m->target->revision - 4u][reg];
}

static int live(const bwmon_t* m)
{
    return m && m->target && m->epoch && m->io.owns(m->io.ctx, m->epoch);
}

static int read_reg(bwmon_t* m, unsigned reg, uint32_t* value)
{
    return live(m) && m->io.read(m->io.ctx, 0u, offset(m, reg), value);
}

static int write_reg(bwmon_t* m, unsigned reg, uint32_t value, int checked)
{
    uint32_t observed;
    if (!live(m) || !m->io.write(m->io.ctx, 0u, offset(m, reg), value))
        return 0;
    m->io.barrier(m->io.ctx);
    return !checked || (read_reg(m, reg, &observed) && observed == value);
}

static int global_irq(bwmon_t* m, int enabled)
{
    uint32_t value, observed;
    if (!m->target->global_base)
        return 1;
    if (!live(m) || !m->io.read(m->io.ctx, 1u, 0x10cu, &value))
        return 0;
    value = enabled ? value | (1u << m->target->port) : value & ~(1u << m->target->port);
    if (!m->io.write(m->io.ctx, 1u, 0x10cu, value))
        return 0;
    m->io.barrier(m->io.ctx);
    return m->io.read(m->io.ctx, 1u, 0x10cu, &observed) && observed == value;
}

static int local_irq(bwmon_t* m, uint32_t enabled)
{
    uint32_t value;
    uint32_t mask = m->target->revision == 4u ? 0xf0u : 0xfu;
    return read_reg(m, REG_IRQ_ENABLE, &value) &&
           write_reg(m, REG_IRQ_ENABLE, (value & ~mask) | (enabled & mask), 1);
}

static int clear(bwmon_t* m, uint32_t counters)
{
    uint32_t irq_mask = m->target->revision == 4u ? 0xf0u : 0xfu;
    if (!write_reg(m, REG_CLEAR, counters, 0) ||
        (m->target->revision == 5u && !write_reg(m, REG_CLEAR, 0u, 1)) ||
        !write_reg(m, REG_IRQ_CLEAR, irq_mask, 0) ||
        (m->target->revision == 5u && !write_reg(m, REG_IRQ_CLEAR, 0u, 1)))
        return 0;
    if (m->target->global_base) {
        if (!m->io.write(m->io.ctx, 1u, 0x108u, 1u << m->target->port))
            return 0;
        m->io.barrier(m->io.ctx);
    }
    return 1;
}

static uint32_t threshold(const bwmon_t* m, uint64_t bytes_sec)
{
    uint64_t divisor = (uint64_t)m->target->count_unit * 1000u;
    uint64_t numerator = bytes_sec * m->target->window_ms;
    uint64_t count = numerator / divisor + (numerator % divisor != 0u);
    return count > 0xffffu ? 0xffffu : (uint32_t)count;
}

int bwmon_init(bwmon_t* m, const bwmon_target_t* target, const bwmon_io_t* io, uint64_t epoch)
{
    if (!m || !target || !io || !io->owns || !io->read || !io->write || !io->barrier || !epoch ||
        (target->revision != 4u && target->revision != 5u) || target->port >= 32u ||
        !target->count_unit || (target->count_unit & (target->count_unit - 1u)) ||
        !target->window_ms || target->window_ms > 50u || !target->timer_hz ||
        !target->maximum_bytes_sec || target->minimum_bytes_sec > target->maximum_bytes_sec ||
        target->maximum_bytes_sec > UINT64_MAX / 50u)
        return 0;
    memset(m, 0, sizeof(*m));
    m->target = target;
    m->io = *io;
    m->epoch = epoch;
    return 1;
}

int bwmon_idle_probe(const bwmon_target_t* target, const bwmon_io_t* io)
{
    uint32_t control, pending;
    unsigned layout;
    if (!target || !io || !io->read || target->port >= 32u ||
        (target->revision != 4u && target->revision != 5u))
        return 0;
    layout = target->revision - 4u;
    if (target->global_base &&
        (!io->read(io->ctx, 1u, 0x100u, &pending) || (pending & (1u << target->port))))
        return 0;
    return io->read(io->ctx, 0u, offsets[layout][REG_ENABLE], &control) && control != UINT32_MAX &&
           !(control & 1u) && io->read(io->ctx, 0u, offsets[layout][REG_STATUS], &pending) &&
           !(pending & (layout ? 0xfu : 0xf0u));
}

int bwmon_rearm(bwmon_t* m, uint64_t peak)
{
    uint32_t enable, high, medium, irq;
    if (!live(m) || m->faulted || !m->saved_count || peak > m->target->maximum_bytes_sec ||
        !read_reg(m, REG_ENABLE, &enable))
        return 0;
    high = threshold(m, peak);
    if (high < 2u)
        high = 2u;
    medium = threshold(m, peak * 7u / 10u);
    if (!medium)
        medium = 1u;
    if (medium >= high)
        medium = high - 1u;
    irq = peak ? 0xbu : 8u;
    if (peak == m->target->maximum_bytes_sec)
        irq = 3u;
    if (m->target->revision == 4u)
        irq <<= 4u;
    if (!write_reg(m, REG_ENABLE, enable & ~1u, 1) || !write_reg(m, REG_HIGH, high, 1) ||
        !write_reg(m, REG_MEDIUM, medium, 1) || !write_reg(m, REG_LOW, 0u, 1) || !clear(m, 1u) ||
        !local_irq(m, irq) || !global_irq(m, 1) || !write_reg(m, REG_ENABLE, enable | 1u, 1)) {
        m->faulted = 1u;
        return 0;
    }
    m->running = 1u;
    return 1;
}

int bwmon_start(bwmon_t* m)
{
    uint32_t value, i;
    uint64_t ticks;
    if (!live(m) || m->running || m->saved_count || m->faulted ||
        !read_reg(m, REG_ENABLE, &value) || (value & 1u) || !bwmon_idle_probe(m->target, &m->io))
        return 0;
    for (i = 0u; i < sizeof(saved_regs); ++i) {
        if (!read_reg(m, saved_regs[i], &m->saved[i]))
            return 0;
    }
    if (m->target->global_base && !m->io.read(m->io.ctx, 1u, 0x10cu, &m->saved[i]))
        return 0;
    m->saved_count = (uint8_t)(i + (m->target->global_base != 0u));
    ticks = (uint64_t)m->target->timer_hz * m->target->window_ms / 1000u;
    if (!ticks || ticks > 0xffffffu || !global_irq(m, 0) || !local_irq(m, 0u) || !clear(m, 2u) ||
        !write_reg(m, REG_WINDOW, (uint32_t)ticks, 1) ||
        !write_reg(m, REG_ACTIONS, 0x95250901u, 1) ||
        !write_reg(m, REG_ZONE_THRESHOLD, 0x01ff0404u, 1) || !write_reg(m, REG_FILTER, 0u, 1) ||
        !write_reg(m, REG_MATCH, 0u, 1)) {
        m->faulted = 1u;
        return 0;
    }
    return 1;
}

int bwmon_pause(bwmon_t* m)
{
    uint32_t value;
    int ok = 1;
    if (!live(m) || !m->saved_count)
        return 0;
    /* Attempt every mask operation even if a previous register access fails. */
    if (!local_irq(m, 0u))
        ok = 0;
    if (!global_irq(m, 0))
        ok = 0;
    if (!read_reg(m, REG_ENABLE, &value) || !write_reg(m, REG_ENABLE, value & ~1u, 1))
        ok = 0;
    if (!clear(m, 1u))
        ok = 0;
    m->running = 0u;
    return ok;
}

int bwmon_capture(bwmon_t* m, bwmon_sample_t* sample)
{
    uint32_t status, counts, maximum = 0u, value, zone;
    if (!sample)
        return -1;
    memset(sample, 0, sizeof(*sample));
    if (!live(m) || !m->running || m->faulted)
        return -1;
    if (!read_reg(m, REG_STATUS, &status))
        goto fault;
    status = m->target->revision == 4u ? (status >> 4u) & 0xfu : status & 0xfu;
    if (!status)
        return 0;
    if (!read_reg(m, REG_ENABLE, &value) || !write_reg(m, REG_ENABLE, value & ~1u, 1) ||
        !local_irq(m, 0u) || !global_irq(m, 0) || !read_reg(m, REG_COUNTS, &counts))
        goto fault;
    for (zone = 0u; zone < 4u; ++zone) {
        if (!(status & (1u << zone)) && !(counts & (0xffu << (zone * 8u))))
            continue;
        if (!m->io.read(m->io.ctx, 0u, offset(m, REG_MAXIMUM) + zone * 4u, &value))
            goto fault;
        if (value > 0xffffu)
            goto fault;
        value = value ? value + 1u : 0u;
        if (value > maximum)
            maximum = value;
    }
    if (!clear(m, 1u))
        goto fault;
    sample->bytes = (uint64_t)maximum * m->target->count_unit;
    sample->zones = (uint8_t)status;
    m->running = 0u;
    return 1;
fault:
    m->faulted = 1u;
    (void)bwmon_pause(m);
    return -1;
}

int bwmon_select(const bwmon_t* m, const bwmon_sample_t* sample, int lagg_active, uint64_t* average,
                 uint64_t* peak)
{
    uint64_t measured, selected, limit;
    if (!m || !m->target || !sample || !sample->zones || !average || !peak ||
        sample->bytes > UINT64_MAX / 1000u)
        return 0;
    limit = m->target->maximum_bytes_sec;
    measured = sample->bytes * 1000u / m->target->window_ms;
    if (measured > limit)
        measured = limit;
    selected = measured > limit * 4u / 5u ? limit : measured * 5u / 4u;
    if (!(sample->zones & 8u) && selected < m->peak * 3u / 4u)
        selected = m->peak * 3u / 4u;
    /* LAGG reports requests, not bytes. Use activity to retain a measured floor. */
    if (lagg_active && !selected && m->peak)
        selected = m->target->count_unit * 1000u / m->target->window_ms;
    if (!measured && !lagg_active)
        selected = 0u;
    if (selected < m->target->count_unit * 1000u / m->target->window_ms && !measured)
        selected = 0u;
    *average = measured;
    *peak = selected;
    return 1;
}

int bwmon_stop(bwmon_t* m)
{
    uint32_t i;
    int ok = 1;
    if (!m || (!m->target && !m->saved_count))
        return 1;
    if (!m->saved_count)
        return !m->faulted;
    if (!live(m))
        return 0;
    if (!bwmon_pause(m) || !clear(m, 2u))
        return 0;
    for (i = 1u; i < sizeof(saved_regs); ++i)
        if (!(saved_regs[i] == REG_IRQ_ENABLE ? local_irq(m, m->saved[i])
                                              : write_reg(m, saved_regs[i], m->saved[i], 1)))
            ok = 0;
    if (!global_irq(m, m->target->global_base &&
                           (m->saved[sizeof(saved_regs)] & (1u << m->target->port))))
        ok = 0;
    m->io.barrier(m->io.ctx);
    if (!write_reg(m, REG_ENABLE, m->saved[0], 1))
        ok = 0;
    if (ok) {
        m->running = 0u;
        m->saved_count = 0u;
        m->faulted = 0u;
    }
    return ok;
}

static int lagg_write(lagg_t* l, uint32_t cpu, uint32_t value)
{
    uint32_t observed;
    uint32_t off = 0x300u + 4u * lagg_cpu_indices[cpu];
    if (!l->io.owns(l->io.ctx, l->epoch) || !l->io.write(l->io.ctx, 0u, off, value))
        return 0;
    l->io.barrier(l->io.ctx);
    return l->io.read(l->io.ctx, 0u, off, &observed) && observed == value;
}

int lagg_start(lagg_t* l, const bwmon_io_t* io, uint64_t epoch)
{
    uint32_t cpu;
    if (!l || !io || !io->owns || !io->read || !io->write || !io->barrier || !epoch ||
        !io->owns(io->ctx, epoch))
        return 0;
    memset(l, 0, sizeof(*l));
    l->io = *io;
    l->epoch = epoch;
    /* Leave firmware or another counter consumer's active configuration intact. */
    for (cpu = 0u; cpu < LAGG_CPUS; ++cpu)
        if (!io->read(io->ctx, 0u, 0x300u + 4u * lagg_cpu_indices[cpu], &l->saved[cpu]) ||
            (l->saved[cpu] & 0x11u))
            return 0;
    l->initialized = 1u;
    for (cpu = 0u; cpu < LAGG_CPUS; ++cpu)
        if (!lagg_write(l, cpu, l->saved[cpu] | 0x10u) ||
            !lagg_write(l, cpu, l->saved[cpu] & ~0x10u) ||
            !lagg_write(l, cpu, (l->saved[cpu] & ~0x10u) | 1u))
            return 0;
    return 1;
}

int lagg_sample(lagg_t* l, uint32_t* active)
{
    uint32_t cpu, value;
    if (!l || !active || !l->initialized || !l->io.owns(l->io.ctx, l->epoch))
        return 0;
    *active = 0u;
    for (cpu = 0u; cpu < LAGG_CPUS; ++cpu) {
        uint32_t config = l->saved[cpu] & ~0x11u;
        if (!lagg_write(l, cpu, config) ||
            !l->io.read(l->io.ctx, 0u, 0x220u + 4u * lagg_cpu_indices[cpu], &value) ||
            !lagg_write(l, cpu, config | 0x10u) || !lagg_write(l, cpu, config) ||
            !lagg_write(l, cpu, config | 1u))
            return 0;
        value &= 0xffffffu;
        l->requests[cpu] += (uint64_t)value * 8u;
        if (value)
            *active |= 1u << cpu;
    }
    return 1;
}

int lagg_stop(lagg_t* l)
{
    uint32_t cpu;
    int ok = 1;
    if (!l || !l->initialized)
        return 1;
    for (cpu = 0u; cpu < LAGG_CPUS; ++cpu)
        if (!lagg_write(l, cpu, l->saved[cpu]))
            ok = 0;
    if (ok)
        l->initialized = 0u;
    return ok;
}
