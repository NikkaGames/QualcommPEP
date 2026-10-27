#ifndef OEM_VOTER_STATE_H
#define OEM_VOTER_STATE_H
/* this is a behavioral reconstruction, not an ABI/owner assignment. EVERY method and
 * callback must execute at PASSIVE_LEVEL under the SAME externally provided non-reentrant
 * passive controller lock; never call blocking provider readback while holding a WDF spin
 * lock / ISR lock. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_VOTER_RESOURCES 32u /* two 16-command sleep/wake TCS images. */
#define OEM_VOTER_CLIENTS 64u
#define OEM_VOTER_MAX 1u
#define OEM_VOTER_OR 2u
#define OEM_VOTER_SUM 3u
#define OEM_VOTER_NO_WRITE 2

typedef struct oem_voter_io {
    void* ctx;
    /* unique nonzero controller/ownership generation supplied by the host. Must change across
     * device reinitialization to reject stale ACK tickets. */

    uint32_t owner_epoch;
    /* must attest current exclusive owner for the requested res. */
    int (*owns)(void*, uint32_t key);
    /* must read authoritative current HW/res-provider state, NOT cached last ACK. */

    int (*read_current)(void*, uint32_t key, uint64_t* out);
    /* a trusted completion provider proves the exact request ticket completed; a successful
     * return does not substitute for read_current. */

    int (*ack_ticket)(void*, uint64_t ticket, uint32_t key, uint64_t target);
} oem_voter_io_t;

typedef struct oem_voter_res {
    uint32_t key;
    uint32_t depends_on;
    uint64_t base_vote;
    uint64_t committed;
    uint8_t mode;
} oem_voter_res_t;

typedef struct oem_voter_client {
    uint64_t client;
    uint64_t value;
    uint32_t key;
    uint8_t active;
} oem_voter_client_t;

typedef struct oem_voter_pending {
    uint64_t ticket;
    uint64_t client;
    uint64_t value;
    uint64_t target;
    uint32_t key;
    uint8_t remove;
    uint8_t issued;
} oem_voter_pending_t;

typedef struct oem_voter_state {
    oem_voter_io_t io;
    oem_voter_res_t resources[OEM_VOTER_RESOURCES];
    oem_voter_client_t clients[OEM_VOTER_CLIENTS];
    oem_voter_pending_t pending;
    uint64_t sleep_guard_token;
    uint32_t last_sequence;
    uint32_t res_cnt;
    uint8_t sealed;
    uint8_t closed;
    uint8_t faulted;
} oem_voter_state_t;

int oem_voter_init(oem_voter_state_t*, const oem_voter_io_t*);
/* reg only a provider-owned, double-readback-verified baseline. Each dependency must be
 * registered first, giving a cycle-free graph. */

int oem_voter_add(oem_voter_state_t*, uint32_t key, uint32_t predecessor, uint8_t mode,
                  uint64_t baseline);
int oem_voter_seal(oem_voter_state_t*);
/* 1 => new pending HW write and (ticket, target) outputs; 2 => aggregate unchanged, client
 * state committed without any HW write; 0 => refused. one pending write at a time:
 * serializes ambiguous TCS completion epochs. */

int oem_voter_begin(oem_voter_state_t*, uint64_t client, uint32_t key, uint64_t value,
                    uint8_t remove, uint64_t* ticket, uint64_t* target);
/* failed/ambiguous submission must remain pending; never replay it automatically. */

int oem_voter_mark_issued(oem_voter_state_t*, uint64_t ticket);
/* explicitly established completion ticket + DOUBLE authoritative readback before applying
 * the new client state. */

int oem_voter_finish(oem_voter_state_t*, uint64_t ticket);
/* cancel is allowed only before a submit could have reached hardware. */
int oem_voter_cancel_unissued(oem_voter_state_t*, uint64_t ticket);
/* blocks pending votes and their dependents; faults on lost ownership or divergent
 * committed readback. */

int oem_voter_ready(void*, uint32_t key, uint32_t req_id, uint8_t is_read);
/* freeze the active client-vote ledger for an exact resume ticket. These functions never
 * issue hardware commands. */

int oem_voter_enter_sleep(oem_voter_state_t*, uint64_t token);
int oem_voter_exit_sleep(oem_voter_state_t*, uint64_t token);
int oem_voter_cancel_unstarted_sleep(oem_voter_state_t*, uint64_t token);
void oem_voter_close(oem_voter_state_t*);
int oem_voter_can_release(const oem_voter_state_t*);
#ifdef __cplusplus
}
#endif
#endif
