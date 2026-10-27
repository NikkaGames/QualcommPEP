#ifndef RPMH_VOTE_BRIDGE_H
#define RPMH_VOTE_BRIDGE_H
#include "vote.h"
#include "rpmh_tcs.h"
#include "cmddb.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RPMH_MAX_ROUTES 32u

typedef struct rpmh_res_route {
    uint32_t res_id;
    uint32_t cmd_db_address;
    uint32_t last_acked_value; /* ack of submitted RSC command, not PMIC readback. */
    uint8_t has_ack;
} rpmh_res_route_t;

typedef struct rpmh_vote_bridge {
    rpmh_tcs_t* transport;
    rpmh_res_route_t routes[RPMH_MAX_ROUTES];
    uint32_t route_cnt;
    uint8_t active_tcs;

    uint8_t routes_sealed;
    void* lock_ctx;
    int (*lock)(void*);
    void (*unlock)(void*);
} rpmh_vote_bridge_t;

/* the bridge provides no guesswork about PMIC/BCM encoding: the caller must map a known
 * command DB address to a specifically encoded 32-bit vote. */

int rpmh_bridge_add(rpmh_vote_bridge_t* bridge, uint32_t id, uint32_t cmd_db_address);
/* configuration and sealing use bridge lock/unlock, the same synchronization domain as the
 * vote bank. A live bridge cannot accept new command addresses. */

int rpmh_bridge_seal(rpmh_vote_bridge_t* bridge);
/* resolves a named firmware command using the validated Molokai CMD_DB. */
int rpmh_bridge_add_by_name(rpmh_vote_bridge_t* bridge, uint32_t id, const uint8_t* database,
                            size_t bytes, const char* name);
vote_io_t rpmh_bridge_io(rpmh_vote_bridge_t* bridge);
#ifdef __cplusplus
}
#endif
#endif
