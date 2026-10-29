#ifndef ICB_BWMON_H
#define ICB_BWMON_H

#include <stddef.h>
#include <stdint.h>

#define BWMON_COUNT 3u
#define BWMON_SAVED_REGS 12u
#define LAGG_CPUS 8u

typedef struct bwmon_target {
    const char* name;
    const char* client;
    const char* master;
    const char* slave;
    uint64_t base;
    uint64_t global_base;
    uint64_t minimum_bytes_sec;
    uint64_t maximum_bytes_sec;
    uint32_t irq;
    uint32_t timer_hz;
    uint32_t count_unit;
    uint32_t window_ms;
    uint32_t cpu_mask;
    uint8_t revision;
    uint8_t port;
} bwmon_target_t;

extern const bwmon_target_t bwmon_targets[BWMON_COUNT];
extern const uint64_t lagg_base;
extern const uint8_t lagg_cpu_indices[LAGG_CPUS];

/* Region zero is the monitor; region one contains its shared interrupt registers. */
typedef struct bwmon_io {
    void* ctx;
    int (*owns)(void*, uint64_t epoch);
    int (*read)(void*, unsigned region, uint32_t offset, uint32_t* value);
    int (*write)(void*, unsigned region, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
} bwmon_io_t;

typedef struct bwmon_sample {
    uint64_t bytes;
    uint8_t zones;
} bwmon_sample_t;

typedef struct bwmon {
    bwmon_io_t io;
    const bwmon_target_t* target;
    uint64_t epoch;
    uint64_t average;
    uint64_t peak;
    uint32_t saved[BWMON_SAVED_REGS];
    uint8_t saved_count;
    uint8_t running;
    uint8_t faulted;
} bwmon_t;

typedef struct lagg {
    bwmon_io_t io;
    uint64_t epoch;
    uint64_t requests[LAGG_CPUS];
    uint32_t saved[LAGG_CPUS];
    uint8_t initialized;
} lagg_t;

int bwmon_init(bwmon_t*, const bwmon_target_t*, const bwmon_io_t*, uint64_t epoch);
int bwmon_idle_probe(const bwmon_target_t*, const bwmon_io_t*);
int bwmon_start(bwmon_t*);
/* Called under the interrupt lock. A captured sample leaves the monitor disabled. */
int bwmon_capture(bwmon_t*, bwmon_sample_t*);
int bwmon_rearm(bwmon_t*, uint64_t peak_bytes_sec);
int bwmon_pause(bwmon_t*);
int bwmon_stop(bwmon_t*);
int bwmon_select(const bwmon_t*, const bwmon_sample_t*, int lagg_active, uint64_t* average,
                 uint64_t* peak);
int lagg_start(lagg_t*, const bwmon_io_t*, uint64_t epoch);
int lagg_sample(lagg_t*, uint32_t* active_cpus);
int lagg_stop(lagg_t*);

#endif
