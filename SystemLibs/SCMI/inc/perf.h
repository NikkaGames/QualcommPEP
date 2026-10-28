#ifndef SCMI_PERF_H
#define SCMI_PERF_H

#include <stddef.h>
#include <stdint.h>

#define SCMI_PERF_PROTOCOL 0x13u
#define SCMI_PERF_MAX_LEVELS 64u
#define SCMI_PERF_RESPONSE_WORDS 32u

typedef struct scmi_perf_io {
    void* ctx;
    /* Serialized transport returns response words after a successful SCMI status. */
    int (*transfer)(void*, uint32_t message, const uint32_t*, size_t, uint32_t*, size_t*);
} scmi_perf_io_t;

typedef struct scmi_perf_level {
    uint32_t performance;
    uint32_t wire_level;
    uint32_t frequency_hz;
    uint32_t latency_us;
} scmi_perf_level_t;

typedef struct scmi_fast_channel {
    uint64_t address;
    uint64_t doorbell;
    uint64_t set_mask;
    uint64_t preserve_mask;
    uint32_t rate_limit_us;
    uint8_t doorbell_bytes;
} scmi_fast_channel_t;

typedef struct scmi_perf_domain {
    uint32_t id;
    uint32_t version;
    uint32_t flags;
    uint32_t rate_limit_us;
    uint32_t sustained_hz;
    uint32_t sustained_performance;
    uint32_t level_count;
    uint32_t transition_latency_us;
    char name[17];
    scmi_perf_level_t levels[SCMI_PERF_MAX_LEVELS];
    scmi_fast_channel_t set_channel;
    scmi_fast_channel_t get_channel;
    uint8_t valid;
} scmi_perf_domain_t;

int scmi_perf_discover(const scmi_perf_io_t*, uint32_t domain, scmi_perf_domain_t*);
int scmi_perf_decode_level(const scmi_perf_domain_t*, uint32_t wire_level, uint32_t* level);
int scmi_perf_select(const scmi_perf_domain_t*, uint32_t minimum_state, uint32_t maximum_state,
                     uint32_t desired_state, uint32_t* level);

#endif
