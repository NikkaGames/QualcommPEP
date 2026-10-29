#ifndef RPMH_SLEEP_H
#define RPMH_SLEEP_H

#include "rpmh_tcs.h"

#define RPMH_SLEEP_BANKS 4u
#define RPMH_SLEEP_RESOURCES 256u

typedef struct rpmh_sleep_layout {
    uint8_t total_tcs;
    uint8_t active_mask;
    uint8_t sleep_first, sleep_count;
    uint8_t wake_first, wake_count;
    uint8_t commands;
} rpmh_sleep_layout_t;

extern const rpmh_sleep_layout_t rpmh_sleep_target;

typedef struct rpmh_sleep_io {
    void* ctx;
    int (*read)(void*, uint32_t, uint32_t*);
    int (*write)(void*, uint32_t, uint32_t);
    void (*barrier)(void*);
    /* The native owner supplies lifetime and producer-rundown checks. */
    int (*authorized)(void*, uint64_t, uint32_t);
    int (*quiescent)(void*);
} rpmh_sleep_io_t;

typedef struct rpmh_sleep_command {
    uint32_t message, address, data;
} rpmh_sleep_command_t;

typedef struct rpmh_sleep_bank {
    uint32_t control, enable, wait;
    rpmh_sleep_command_t commands[TCS_BATCH_MAX];
} rpmh_sleep_bank_t;

typedef struct rpmh_sleep_resource {
    uint32_t address, active, sleep, vcd;
    uint8_t explicit_sleep, bcm;
} rpmh_sleep_resource_t;

typedef enum rpmh_sleep_result {
    RPMH_SLEEP_OK,
    RPMH_SLEEP_OWNER,
    RPMH_SLEEP_BUSY,
    RPMH_SLEEP_CAPACITY,
    RPMH_SLEEP_CONFLICT,
    RPMH_SLEEP_IO,
    RPMH_SLEEP_FAULT
} rpmh_sleep_result_t;

/* Resident owner storage. Callers serialize cache updates; entry requires producer rundown. */
typedef struct rpmh_sleep {
    rpmh_sleep_io_t io;
    rpmh_sleep_layout_t layout;
    uint64_t epoch;
    uint32_t generation, config, resource_count;
    uint64_t revision, staged_revision;
    rpmh_sleep_resource_t resources[RPMH_SLEEP_RESOURCES];
    rpmh_sleep_bank_t baseline[RPMH_SLEEP_BANKS];
    rpmh_sleep_bank_t current[RPMH_SLEEP_BANKS];
    rpmh_sleep_bank_t next[RPMH_SLEEP_BANKS];
    uint16_t order[RPMH_SLEEP_RESOURCES];
    uint16_t touched[RPMH_SLEEP_BANKS];
    uint8_t bound, frozen, faulted, cache_invalid, modified;
    uint32_t entries, returns, writes, failures;
    rpmh_sleep_result_t last_result;
} rpmh_sleep_t;

int rpmh_sleep_init(rpmh_sleep_t*, const rpmh_sleep_io_t*, const rpmh_sleep_layout_t*,
                    uint64_t epoch, uint32_t generation);
/* Record only physically completed ACTIVE commands; reads never change the cache. */
int rpmh_sleep_record(rpmh_sleep_t*, const tcs_command_t*, uint32_t count);
int rpmh_sleep_has_active(const rpmh_sleep_t*, uint32_t address);
int rpmh_sleep_vote(rpmh_sleep_t*, uint32_t address, uint32_t sleep, int bcm, uint32_t vcd);
int rpmh_sleep_bind(rpmh_sleep_t*);
int rpmh_sleep_enter(rpmh_sleep_t*);
int rpmh_sleep_exit(rpmh_sleep_t*);
int rpmh_sleep_restore(rpmh_sleep_t*);

#endif
