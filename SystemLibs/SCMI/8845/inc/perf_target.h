#ifndef SCMI_PERF_TARGET_H
#define SCMI_PERF_TARGET_H

#include <stdint.h>

#define PERF_TARGET_DOMAINS 2u
#define PERF_TARGET_CPUS 8u

typedef struct perf_target {
    uint64_t mailbox;
    uint32_t mailbox_bytes;
    uint64_t control;
    uint32_t control_bytes;
    uint32_t send_offset;
} perf_target_t;

const perf_target_t* perf_target(void);
uint32_t perf_target_capacity(uint32_t domain);
uint32_t perf_target_minimum_hz(uint32_t domain);
int perf_target_core(uint64_t mpidr, uint32_t* domain, uint32_t* core);
int perf_target_channel(uint64_t address, uint32_t bytes, int write);

#endif
