#ifndef VRM_RAIL_BANK_H
#define VRM_RAIL_BANK_H
/* provider-backed, multi-client PMIC VRM vote aggregator. No Windows PMIC provider is
 * registered by this module. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RAIL_CNT 2u
#define RAIL_MAX_CLIENTS 32u
#define RAIL_LDO4_B 1u
#define RAIL_LDO15_B 2u

typedef struct rail_state {
    uint32_t microvolts;
    uint8_t enabled;
} rail_state_t;

typedef struct rail_provider {
    void* ctx;
    int (*lock)(void*);
    void (*unlock)(void*);
    int (*owns)(void*, uint32_t rail);
    /* read_current must read actual authoritative provider state. */
    int (*read_current)(void*, uint32_t rail, rail_state_t*);
    /* apply may change hardware partially even if returning failure. */
    int (*apply)(void*, uint32_t rail, rail_state_t);
} rail_provider_t;

typedef struct rail_client {
    uint64_t id;
    rail_state_t vote;
    uint8_t in_use;
} rail_client_t;

typedef struct rail_bank_entry {
    rail_state_t baseline;
    rail_state_t applied;
    rail_client_t clients[RAIL_MAX_CLIENTS];
    uint8_t adopted;
    uint8_t state_known;
} rail_bank_entry_t;

typedef struct rail_bank {
    rail_provider_t provider;
    rail_bank_entry_t rails[RAIL_CNT];
} rail_bank_t;

typedef enum rail_result {
    RAIL_OK = 0,
    RAIL_INVALID = 1,
    RAIL_NO_OWNERSHIP = 2,
    RAIL_NO_CAPACITY = 3,
    RAIL_STATE_UNKNOWN = 4,
    RAIL_HARDWARE_FAILURE = 5,
    RAIL_ROLLBACK_FAILED = 6
} rail_result_t;

/* requires two matching provider readbacks under the lock, matching the supplied baseline. */

rail_result_t rail_adopt(rail_bank_t*, uint32_t rail, rail_state_t baseline);
/* disabled vote must have voltage 0. An explicit vote of disabled is equivalent to
 * removing a client's vote. */

rail_result_t rail_vote(rail_bank_t*, uint32_t rail, uint64_t client, rail_state_t desired);
/* inspect a single client's logical vote under the authoritative provider lock. the
 * physical aggregate is checked twice before any result is returned; unlike a cached
 * client-table lookup this cannot conceal an external rail modification. */

rail_result_t rail_inspect_client(rail_bank_t*, uint32_t rail, uint64_t client, rail_state_t* vote,
                                  uint8_t* found);

#ifdef __cplusplus
}
#endif
#endif
