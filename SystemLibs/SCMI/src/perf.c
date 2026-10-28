#include "perf.h"
#include <string.h>

#define PERF_SET_LEVEL (1u << 30)
#define PERF_FAST_CHANNELS (1u << 27)
#define PERF_LEVEL_INDEXING (1u << 25)

static int command(const scmi_perf_io_t* io, uint32_t message, const uint32_t* request,
                   size_t count, uint32_t* response, size_t* words)
{
    *words = SCMI_PERF_RESPONSE_WORDS;
    memset(response, 0, SCMI_PERF_RESPONSE_WORDS * sizeof(*response));
    return io->transfer(io->ctx, message, request, count, response, words) &&
           *words <= SCMI_PERF_RESPONSE_WORDS;
}

static int fast_channel(const scmi_perf_io_t* io, uint32_t domain, uint32_t operation,
                        scmi_fast_channel_t* channel)
{
    uint32_t request[2] = {domain, operation};
    uint32_t response[SCMI_PERF_RESPONSE_WORDS];
    size_t words;
    uint8_t bytes;
    if (!command(io, 2u, &operation, 1u, response, &words) || words < 1u || !(response[0] & 1u) ||
        !command(io, 0xbu, request, 2u, response, &words) || words < 5u || response[4] != 4u)
        return 0;
    memset(channel, 0, sizeof(*channel));
    channel->address = response[2] | ((uint64_t)response[3] << 32);
    channel->rate_limit_us = response[1] & 0xfffffu;
    if (!channel->address || (channel->address & 3u) || channel->address > UINT64_MAX - 4u)
        return 0;
    if (!(response[0] & 1u))
        return 1;
    if (words < 11u)
        return 0;
    bytes = (uint8_t)(1u << ((response[0] >> 1) & 3u));
    channel->doorbell = response[5] | ((uint64_t)response[6] << 32);
    channel->set_mask = response[7] | ((uint64_t)response[8] << 32);
    channel->preserve_mask = response[9] | ((uint64_t)response[10] << 32);
    channel->doorbell_bytes = bytes;
    if (!channel->doorbell || (channel->doorbell & (bytes - 1u)) || !channel->set_mask ||
        channel->doorbell > UINT64_MAX - bytes ||
        (bytes < 8u && ((channel->set_mask | channel->preserve_mask) >> (bytes * 8u))))
        return 0;
    return 1;
}

static int add_level(scmi_perf_domain_t* domain, const uint32_t* row, uint32_t row_words)
{
    scmi_perf_level_t level;
    uint64_t frequency;
    uint32_t i;
    int indexed = domain->version >= 0x40000u && (domain->flags & PERF_LEVEL_INDEXING);
    memset(&level, 0, sizeof(level));
    level.performance = row[0];
    level.wire_level = indexed ? row[4] : row[0];
    level.latency_us = row[2] & 0xffffu;
    if (row_words == 5u && row[3]) {
        frequency = (uint64_t)row[3] * 1000u;
    } else {
        if (indexed || !domain->sustained_performance || !domain->sustained_hz)
            return 0;
        frequency = (uint64_t)row[0] * domain->sustained_hz / domain->sustained_performance;
    }
    if (!frequency || frequency > UINT32_MAX || domain->level_count == SCMI_PERF_MAX_LEVELS)
        return 0;
    level.frequency_hz = (uint32_t)frequency;
    for (i = 0u; i < domain->level_count; ++i)
        if (domain->levels[i].wire_level == level.wire_level ||
            domain->levels[i].performance == level.performance ||
            domain->levels[i].frequency_hz == level.frequency_hz)
            return 0;
    i = domain->level_count++;
    while (i && domain->levels[i - 1u].frequency_hz > level.frequency_hz) {
        domain->levels[i] = domain->levels[i - 1u];
        --i;
    }
    domain->levels[i] = level;
    if (level.latency_us > domain->transition_latency_us)
        domain->transition_latency_us = level.latency_us;
    return 1;
}

int scmi_perf_discover(const scmi_perf_io_t* io, uint32_t id, scmi_perf_domain_t* domain)
{
    uint32_t response[SCMI_PERF_RESPONSE_WORDS], request[2], offset = 0u, remaining;
    uint32_t row_words, i, returned, version, total = 0u;
    uint64_t sustained_hz;
    size_t words;
    if (!domain)
        return 0;
    memset(domain, 0, sizeof(*domain));
    if (!io || !io->transfer || !command(io, 0u, NULL, 0u, response, &words) || words != 1u)
        return 0;
    version = response[0];
    if (version < 0x10000u || version >= 0x50000u || !command(io, 1u, NULL, 0u, response, &words) ||
        words < 1u || id >= (response[0] & 0xffffu) ||
        !command(io, 3u, &id, 1u, response, &words) || words < 8u ||
        (response[0] & (PERF_SET_LEVEL | PERF_FAST_CHANNELS)) !=
            (PERF_SET_LEVEL | PERF_FAST_CHANNELS))
        goto fail;
    domain->id = id;
    domain->version = version;
    domain->flags = response[0];
    domain->rate_limit_us = response[1] & 0xfffffu;
    sustained_hz = (uint64_t)response[2] * 1000u;
    if (sustained_hz > UINT32_MAX)
        goto fail;
    domain->sustained_hz = (uint32_t)sustained_hz;
    domain->sustained_performance = response[3];
    memcpy(domain->name, &response[4], 16u);
    domain->name[16] = 0;
    row_words = version >= 0x40000u ? 5u : 3u;
    do {
        request[0] = id;
        request[1] = offset;
        if (!command(io, 4u, request, 2u, response, &words) || !words)
            goto fail;
        returned = response[0] & 0xffffu;
        remaining = response[0] >> 16;
        if (!returned || returned > SCMI_PERF_MAX_LEVELS - offset ||
            remaining > SCMI_PERF_MAX_LEVELS - offset - returned ||
            words != 1u + returned * row_words)
            goto fail;
        if (!offset)
            total = returned + remaining;
        if (offset + returned + remaining != total)
            goto fail;
        for (i = 0u; i < returned; ++i)
            if (!add_level(domain, &response[1u + i * row_words], row_words))
                goto fail;
        offset += returned;
    } while (remaining);
    if (domain->level_count < 2u || domain->sustained_hz < domain->levels[0].frequency_hz ||
        !fast_channel(io, id, 7u, &domain->set_channel) ||
        !fast_channel(io, id, 8u, &domain->get_channel) ||
        domain->set_channel.address == domain->get_channel.address)
        goto fail;
    for (i = 1u; i < domain->level_count; ++i)
        if (domain->levels[i].performance <= domain->levels[i - 1u].performance)
            goto fail;
    if (domain->set_channel.rate_limit_us > domain->rate_limit_us)
        domain->rate_limit_us = domain->set_channel.rate_limit_us;
    if (domain->rate_limit_us > domain->transition_latency_us)
        domain->transition_latency_us = domain->rate_limit_us;
    domain->valid = 1u;
    return 1;
fail:
    memset(domain, 0, sizeof(*domain));
    return 0;
}

int scmi_perf_decode_level(const scmi_perf_domain_t* domain, uint32_t wire_level, uint32_t* level)
{
    uint32_t i;
    if (!domain || !domain->valid || !level || domain->level_count > SCMI_PERF_MAX_LEVELS)
        return 0;
    for (i = 0u; i < domain->level_count; ++i)
        if (domain->levels[i].wire_level == wire_level) {
            *level = i;
            return 1;
        }
    return 0;
}

int scmi_perf_select(const scmi_perf_domain_t* domain, uint32_t minimum, uint32_t maximum,
                     uint32_t desired, uint32_t* level)
{
    if (!domain || !domain->valid || !level || !domain->level_count ||
        domain->level_count > SCMI_PERF_MAX_LEVELS || minimum >= domain->level_count ||
        maximum >= domain->level_count || desired >= domain->level_count)
        return 0;
    /* Windows discrete states are ordered from highest to lowest performance. */
    if (maximum > minimum)
        return 0;
    if (desired < maximum)
        desired = maximum;
    if (desired > minimum)
        desired = minimum;
    *level = domain->level_count - 1u - desired;
    return 1;
}
