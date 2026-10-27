#ifndef POFX_GROUP_VOTE_H
#define POFX_GROUP_VOTE_H

#include "state_txn.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a single client identity owns all ordered commands for one PSTATE_SET. Nonpaged caller
 * storage; the same identity is retained across both states. */
typedef struct group_vote_io {
    void* ctx;
    int (*lock)(void*);
    void (*unlock)(void*);
    int (*read_epoch)(void*, uint64_t*);
    int (*supports)(void*, const char*, uint32_t, const power_action_t*);
    int (*snapshot)(void*, const char*, uint32_t, const power_action_t*, power_snapshot_t*);
    int (*apply)(void*, const char*, uint32_t, const power_action_t*);
    int (*verify)(void*, const char*, uint32_t, const power_action_t*);
    int (*restore)(void*, const char*, uint32_t, const power_action_t*, const power_snapshot_t*);
    int (*verify_restored)(void*, const char*, uint32_t, const power_action_t*,
                           const power_snapshot_t*);
    int (*verify_final)(void*, const char*, uint32_t, const power_sequence_t*);
    /* independent proof that the src-owned required client has no vote. */
    int (*verify_released)(void*, const char*);
    int (*read_clock_config)(void*, const char*, uint32_t*);
} group_vote_io_t;

typedef struct group_vote {
    group_vote_io_t io;
    power_snapshot_t snapshots[POWER_MAX_ACTIONS];
    power_sequence_t final_sequence;
    char dev_path[256];
    char identity[256];
    char config_clock[POWER_NAME_BYTES];
    uint32_t config_baseline;
    uint64_t epoch;
    uint32_t comp;
    uint32_t set;
    uint32_t state;
    uint8_t state_known;
    uint8_t bound;
    uint8_t touched;
    uint8_t config_captured;
    uint8_t quarantined;
    uint8_t busy;
} group_vote_t;

int group_vote_bind(group_vote_t*, const group_vote_io_t*, const char* dev_path, uint32_t comp,
                    uint32_t set, uint64_t epoch);
power_result_t group_vote_transition(group_vote_t*, const power_pstate_program_t*,
                                     power_execution_t*);
int group_vote_close(group_vote_t*);

#ifdef __cplusplus
}
#endif
#endif
