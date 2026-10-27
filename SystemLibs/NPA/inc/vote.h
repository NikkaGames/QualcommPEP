#ifndef VOTE_ARBITER_H
#define VOTE_ARBITER_H
/* portable shared-client vote core. not a firmware vote encoder. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MAX_VOTE_RESOURCES 64u
#define MAX_VOTE_CLIENTS 128u

typedef enum vote_aggregation {
    VOTE_MAX = 1,
    VOTE_SUM = 2,
    VOTE_BOOLEAN_OR = 3
} vote_aggregation_t;

typedef struct vote_res {
    uint32_t id;
    vote_aggregation_t aggregation;
    uint64_t applied;
    /* implicit owner/baseline vote confirmed at res reg. For SUM it is added; for MAX it is a
     * floor; for OR a boolean owner. */

    uint64_t base_vote;
    uint8_t known;
    /* adopted resources must remain provider-readable and exclusively owned. ACK-only RPMh
     * routes never set this flag or claim physical readback. */

    uint8_t requires_readback;
} vote_res_t;

typedef struct vote_client {
    uint64_t client;
    uint32_t id;
    uint64_t value;
    uint8_t in_use;
} vote_client_t;

typedef struct vote_io {
    void* ctx;
    int (*lock)(void*);
    void (*unlock)(void*);
    /* return zero without an authorized execution path. */
    int (*supports)(void*, uint32_t res_id);
    int (*apply)(void*, uint32_t res_id, uint64_t aggregate);
    /* must verify acknowledgement/readback where supported. */
    int (*verify)(void*, uint32_t res_id, uint64_t aggregate);
    /* optional: reserved for provider-backed, non-destructive adoption. owns must establish
     * exclusive control over this res; read_current must observe actual provider state rather
     * than an ACK or software cache. */

    int (*owns)(void*, uint32_t res_id);
    int (*read_current)(void*, uint32_t res_id, uint64_t* value);
} vote_io_t;

typedef struct vote_bank {
    vote_res_t resources[MAX_VOTE_RESOURCES];
    vote_client_t clients[MAX_VOTE_CLIENTS];
    uint32_t res_cnt;
    vote_io_t io;
} vote_bank_t;

typedef enum vote_result {
    VOTE_OK = 0,
    VOTE_BAD_REQ,
    VOTE_UNSUPPORTED,
    VOTE_NO_CAPACITY,
    VOTE_OVERFLOW,
    VOTE_HARDWARE_FAILURE,
    VOTE_ROLLBACK_FAILED,
    VOTE_STATE_UNKNOWN
} vote_result_t;

/* caller must first establish exclusive firmware/res ownership. Registration writes AND
 * verifies confirmed_initial; calling this with zero on a shared or externally powered
 * rail may disable it. */

vote_result_t vote_add_res(vote_bank_t* bank, uint32_t id, vote_aggregation_t mode,
                           uint64_t confirmed_initial);
/* both ownership and repeated stable hardware readback are mandatory; failure must leave
 * the res unregistered. Not avail for a write-only RPMh endpoint without a separate
 * authoritative provider. */

vote_result_t vote_adopt_res(vote_bank_t* bank, uint32_t id, vote_aggregation_t mode,
                             uint64_t expected_current);
vote_result_t vote_change(vote_bank_t* bank, uint64_t client, uint32_t res_id, uint64_t value,
                          int remove);
#ifdef __cplusplus
}
#endif
#endif
