#ifndef OEM_CMDDB_RESOLVER_H
#define OEM_CMDDB_RESOLVER_H
/* immutable, CMD_DB-addressed request allowlist and fail-closed external voter adapter for
 * /199. Source-only, NOT voting ABI. */

#include <stdint.h>
#include <stddef.h>
#include "rpmh_tcs.h"
#include "oem_tcs_batch_async.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_CMDDB_RESOURCES 32u /* two 16-command sleep/wake TCS images. */
#define OEM_CMDDB_REQUESTS 32u
#define OEM_CMDDB_CMDS TCS_BATCH_MAX

typedef struct oem_cmddb_res {
    uint32_t key;
    uint32_t address;
    uint32_t last_offset; /* exact allowed inclusive range; 32-bit aligned. */
    uint8_t allow_read;
    uint8_t allow_write;
} oem_cmddb_res_t;

typedef struct oem_cmddb_offset_cmd {
    uint32_t offset;
    uint32_t data;
    uint8_t read;
} oem_cmddb_offset_cmd_t;

typedef struct oem_cmddb_req {
    uint64_t token;
    uint32_t req_id;
    uint32_t key;
    uint8_t is_read;
    uint8_t cnt;
    uint8_t ready_approved;
    uint8_t claimed;
    tcs_command_t commands[OEM_CMDDB_CMDS];
} oem_cmddb_req_t;

typedef struct oem_cmddb_resolver {
    /* must validate actual voter ownership and dependency state; a null callback is NOT ready. */

    int (*voter_ready)(void*, uint32_t key, uint32_t req_id, uint8_t is_read);
    void* voter_ctx;
    oem_cmddb_res_t resources[OEM_CMDDB_RESOURCES];
    oem_cmddb_req_t requests[OEM_CMDDB_REQUESTS];
    uint8_t resources_cnt;
    uint8_t sealed;
    uint8_t closed;
    uint8_t faulted;
} oem_cmddb_resolver_t;

int oem_cmd_db_resolver_init(oem_cmddb_resolver_t*,
                             int (*voter)(void*, uint32_t, uint32_t, uint8_t), void* voter_ctx);

int oem_cmd_db_add_res(oem_cmddb_resolver_t*, const uint8_t* db, size_t db_bytes, const char* name,
                       uint32_t res_key, uint32_t last_offset, uint8_t allow_read,
                       uint8_t allow_write);
int oem_cmd_db_seal(oem_cmddb_resolver_t*);
/* offset is RELATIVE to the validated CMD_DB address; caller cannot directly supply
 * arbitrary RSC address. */

int oem_cmd_db_reg(oem_cmddb_resolver_t*, uint64_t token, uint32_t req_id, uint32_t res_key,
                   uint8_t is_read, const oem_cmddb_offset_cmd_t*, uint32_t cnt);

int oem_cmd_db_res_ready(void*, uint32_t key, uint32_t req_id, uint8_t is_read);

int oem_cmd_db_resolve_batch(void*, uint64_t token, uint32_t req_id, uint32_t key, uint8_t is_read,
                             tcs_command_t* commands, uint32_t* cnt);
/* under the shared lock, clear the claimed token only after authenticated ACK/client-notify
 * retirement and TCS cleanup. */

int oem_cmd_db_retire_after_tcs(oem_cmddb_resolver_t*, oem_tcs_batch_t*, uint8_t slot,
                                uint64_t token);

int oem_cmd_db_cancel_unclaimed(oem_cmddb_resolver_t*, uint64_t token);
void oem_cmd_db_close(oem_cmddb_resolver_t*);
int oem_cmd_db_can_release(const oem_cmddb_resolver_t*);
#ifdef __cplusplus
}
#endif
#endif
