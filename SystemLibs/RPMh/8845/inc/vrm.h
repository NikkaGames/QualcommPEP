#ifndef RPMH_VRM_H
#define RPMH_VRM_H
/* does not own RSC/TCS, does not dispatch votes, and is not a PPP private-ABI adapter. */

#include <stdint.h>
#include <stddef.h>
#include "state_txn.h"
#include "rpmh_tcs.h"
#include "rail_bank.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VRM_VOLTAGE_OFFSET 0u
#define VRM_ENABLE_OFFSET 4u
#define VRM_MODE_OFFSET 8u
#define VRM_MAX_CMDS 2u

typedef struct vrm_desc {
    const char* ppp_name;
    const char* command_db_name;
    const char* dts_name;
    uint32_t min_microvolts;
    uint32_t max_microvolts;
} vrm_desc_t;

typedef struct vrm_command {
    /* offset from the exact runtime CMD_DB lookup, never a guessed address. */
    uint32_t offset;
    uint32_t data;
} vrm_command_t;

typedef struct vrm_program {
    const vrm_desc_t* res;
    uint32_t cnt;
    vrm_command_t commands[VRM_MAX_CMDS];
} vrm_program_t;

/* d3 sends an enable=0 vote; it must not overwrite the regulator voltage with zero. */

int vrm_prepare_known_dstate(const char* dev_path, const power_action_t* action,
                             uint32_t target_d_state, vrm_program_t* program);

int vrm_prepare_tsc1_ldo4_b(const power_action_t* action, uint32_t target_d_state,
                            vrm_program_t* program);

/* convert a SHARED-CLIENT AGGREGATE, not a raw d3 device request, to verified VRM
 * voltage/enable commands. the caller must have established res ownership and obtained a
 * successful, verified aggregate vote. */

int vrm_prepare_aggregate(uint32_t rail, rail_state_t state, vrm_program_t* program);

/* resolve offsets against a separately validated runtime command-db address. never issues
 * a hardware transaction. */

int vrm_resolve_address(uint32_t command_db_address, const vrm_program_t* program,
                        uint32_t* addresses, uint32_t capacity);
/* submit a previously prepared, strictly validated LDO program using an already-owned TCS.
 * Not an NPA/PPP vote aggregator: its caller must prove exclusive regulator ownership,
 * dependencies and lifecycle. */

rpmh_tcs_result_t vrm_submit_known_program(rpmh_tcs_t* transport, uint8_t active_tcs,
                                           uint32_t command_db_address,
                                           const vrm_program_t* program);
/* preferred for future provider integration: command-db name matches the desc's exact
 * L4B_E0 or L15B_E0 res. Requires independently validated ownership, shared vote lifetime
 * and a stable read-only command-db snapshot; not called by PoFx today. */

rpmh_tcs_result_t vrm_submit_from_cmd_db(rpmh_tcs_t* transport, uint8_t active_tcs,
                                         const uint8_t* database, size_t database_bytes,
                                         const vrm_program_t* program);
#ifdef __cplusplus
}
#endif
#endif
