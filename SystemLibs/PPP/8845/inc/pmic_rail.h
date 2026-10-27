#ifndef PMIC_RAIL_H
#define PMIC_RAIL_H
/* molokai PMIC rail owner over RPMh VRM - portable core. Facts: - the first DWORD after
 * the PPP res name is the vote kind, and it is the rail's own kind: 1 for an LDO, 2 for an
 * SMPS, 6 for a PMIC clock buf. */

#include <stdint.h>
#include <stddef.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif

#define RAIL_MAX 64u
#define RAIL_MAX_CLIENTS 64u
#define RAIL_NAME_BYTES 64u
#define RAIL_CMDDB_BYTES 9u
#define RAIL_PATH_BYTES 256u

#define VRM_VOLTAGE_OFFSET 0u
#define VRM_ENABLE_OFFSET 4u
#define VRM_MODE_OFFSET 8u
#define VRM_HEADROOM_OFFSET 12u

#define RAIL_TYPE_NONE 0u
#define RAIL_TYPE_LDO 1u
#define RAIL_TYPE_SMPS 2u
#define RAIL_TYPE_CLOCK 6u

#define RAIL_TYPE_REGULATOR RAIL_TYPE_LDO

typedef struct rail_vote {
    uint32_t type;
    uint32_t microvolts;
    uint32_t enable;
    uint32_t mode;
    uint32_t headroom;
} rail_vote_t;

typedef struct rail {
    char ppp[RAIL_NAME_BYTES];
    char cmd_db_name[RAIL_CMDDB_BYTES];
    uint32_t address;
    uint32_t type;
    uint8_t resolved;
    uint8_t issued;
    rail_vote_t last_issued;
} rail_t;

typedef struct rail_client {
    uint8_t in_use;
    char dev_path[RAIL_PATH_BYTES];
    uint32_t rail_idx;
    rail_vote_t vote;
} rail_client_t;

typedef struct rail_owner {
    rail_t rails[RAIL_MAX];
    uint32_t rail_cnt;
    rail_client_t clients[RAIL_MAX_CLIENTS];

    const uint8_t* cmd_db;
    size_t cmd_db_bytes;
    uint8_t initialized;
} rail_owner_t;

/* returns 0 for a shape it cannot map. */
int rail_cmd_db_name(const char* ppp_name, char out[RAIL_CMDDB_BYTES], uint32_t* type);
/* decode the DSDT vote DWORDs: five for an LDO or SMPS, two for a PMIC clock buf. */

int rail_decode_fields(const uint32_t fields[5], rail_vote_t* vote);

uint32_t rail_owner_init(rail_owner_t* owner, const uint8_t* cmd_db, size_t cmd_db_bytes,
                         const char* const* ppp_names, uint32_t cnt);
int rail_find(const rail_owner_t* owner, const char* ppp_name);

int rail_ensure(rail_owner_t* owner, const char* ppp_name);
int rail_supported(const rail_owner_t* owner, const char* ppp_name);

/* record (vote != NULL) or clear (vote == NULL) one client's vote. */
int rail_set_client_vote(rail_owner_t* owner, const char* dev_path, const char* ppp_name,
                         const rail_vote_t* vote);
int rail_get_client_vote(const rail_owner_t* owner, const char* dev_path, const char* ppp_name,
                         rail_vote_t* vote);

int rail_aggregate(const rail_owner_t* owner, uint32_t rail_idx, rail_vote_t* aggregate);
/* prepare the full VRM batch before triggering: enable/voltage/mode, or enable=0. */

int rail_program(const rail_owner_t* owner, uint32_t rail_idx, const rail_vote_t* aggregate,
                 tcs_command_t out[3], uint32_t* cnt);
/* clock buffers read enable only (one response); regulators read voltage, enable, mode
 * (three responses). never issue reads of absent clock fields. */
int rail_read_program(const rail_owner_t* owner, uint32_t rail_idx, tcs_command_t out[3],
                      uint32_t* cnt);
uint64_t rail_pack_state(uint32_t millivolts, uint32_t enable, uint32_t mode);
#ifdef __cplusplus
}
#endif
#endif
