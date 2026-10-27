#ifndef OEM_TCS_BATCH_ASYNC_H
#define OEM_TCS_BATCH_ASYNC_H
/* complements single-write provider; neither is boot activated. Caller serializes ALL
 * methods and callbacks under the SAME completion lock used by and ; callbacks must not
 * reenter. neither establishes ownership, maps registers, clears IRQ W1C, polls for
 * completion, nor notifies clients. */

#include <stdint.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_TCS_BATCH_SLOTS 3u
#define OEM_TCS_BATCH_COMMANDS TCS_BATCH_MAX

typedef struct oem_tcs_batch_ops {
    void* ctx;
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    /* resolve a verified request/token and return 1..16 validated commands. *count is
     * initially capacity (16) and returns actual length. A read request may have READ and
     * WRITE commands, but must contain a READ. */

    int (*resolve_batch)(void*, uint64_t token, uint32_t req_id, uint32_t res_key, uint8_t is_read,
                         tcs_command_t* commands, uint32_t* cnt);
} oem_tcs_batch_ops_t;

typedef enum oem_tcs_batch_state {
    OEM_BATCH_FREE = 0,
    OEM_BATCH_SUBMIT_UNCERTAIN = 1,
    OEM_BATCH_INFLIGHT = 2,
    OEM_BATCH_RETIRE_UNCERTAIN = 3
} oem_tcs_batch_state_t;

typedef struct oem_tcs_batch_slot {
    uint64_t token;
    uint32_t req_id;
    uint32_t res_key;
    uint8_t cnt;
    uint8_t has_read;
    uint8_t results_valid;
    uint8_t ack_confirmed;
    uint8_t results_copied;
    oem_tcs_batch_state_t state;
    tcs_command_t commands[OEM_TCS_BATCH_COMMANDS];
    uint32_t read_values[OEM_TCS_BATCH_COMMANDS];
} oem_tcs_batch_slot_t;

typedef struct oem_tcs_batch {
    oem_tcs_batch_ops_t ops;
    uint8_t external_grant_mask;      /* must be set by a verified external owner. */
    uint8_t completion_handler_bound; /* must be set by a verified external owner. */
    uint8_t faulted;
    uint8_t closed;
    oem_tcs_batch_slot_t slots[OEM_TCS_BATCH_SLOTS];
    uint32_t submitted;
    uint32_t retired;
} oem_tcs_batch_t;

typedef struct oem_tcs_batch_result {
    uint8_t is_read;
    uint32_t value;
} oem_tcs_batch_result_t;

int oem_tcs_batch_init(oem_tcs_batch_t*, const oem_tcs_batch_ops_t*);
/* no polling, no W1C, no client callback. */
int oem_tcs_batch_submit(void*, uint8_t slot, uint64_t token, uint32_t req_id, uint32_t res_key,
                         uint8_t is_read);

int oem_tcs_batch_validate_finished(oem_tcs_batch_t*, uint8_t slot, uint64_t token);
/* called ONLY from complete_inflight after W1C status readback succeeded. */

int oem_tcs_batch_commit_ack(oem_tcs_batch_t*, uint8_t slot, uint64_t token);
/* the caller delivers completions to the client outside this module. */

int oem_tcs_batch_copy_results(oem_tcs_batch_t*, uint8_t slot, uint64_t token,
                               oem_tcs_batch_result_t* out, uint32_t capacity, uint32_t* cnt);
/* after 's successful client notify. */

int oem_tcs_batch_retire(oem_tcs_batch_t*, uint8_t slot, uint64_t token);
void oem_tcs_batch_close(oem_tcs_batch_t*);
int oem_tcs_batch_can_release(const oem_tcs_batch_t*);
#ifdef __cplusplus
}
#endif
#endif
