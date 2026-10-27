#ifndef OEM_AMC_COMMAND_QUEUE_H
#define OEM_AMC_COMMAND_QUEUE_H
/* not a binary-identical reconstruction of the signed 8480 command queue or an MMIO owner.
 * All methods require the SAME externally owned completion lock as the ACK provider,
 * including callback implementations. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_AMC_QUEUE_MAX 32u
#define OEM_AMC_QUEUE_SLOTS 3u

typedef enum oem_amc_cmd_state {
    OEM_CMD_FREE = 0,
    OEM_CMD_QUEUED = 1,
    OEM_CMD_SUBMITTING = 2,
    OEM_CMD_INFLIGHT = 3,
    OEM_CMD_NOTIFYING = 4,
    OEM_CMD_NOTIFY_UNCERTAIN = 5
} oem_amc_cmd_state_t;

typedef struct oem_amc_cmd {
    uint64_t token;
    uint32_t req_id;  /* nonzero client request identifier. */
    uint32_t res_key; /* opaque caller-defined res identity. */
    uint8_t is_read;
    uint8_t slot;
    oem_amc_cmd_state_t state;
} oem_amc_cmd_t;

typedef struct oem_amc_cmd_ops {
    void* ctx;
    /* must include voter and read-dependency checks; the queue cannot infer these from an
     * address. */

    int (*res_ready)(void*, uint32_t res_key, uint32_t req_id, uint8_t is_read);
    /* submission results: 1 = accepted; 0 = ambiguous, retain and quarantine; -1 = proven
     * pre-dispatch refusal, keep the FIFO head. Refusal requires no MMIO, trigger, ledger,
     * owner mutation or worker acquisition. */

    int (*submit_tcs)(void*, uint8_t slot, uint64_t token, uint32_t req_id, uint32_t res_key,
                      uint8_t is_read);

    int (*notify_client)(void*, uint64_t token, uint32_t req_id);
} oem_amc_cmd_ops_t;

typedef struct oem_amc_command_queue {
    oem_amc_cmd_ops_t ops;
    oem_amc_cmd_t commands[OEM_AMC_QUEUE_MAX];
    uint8_t slot_entry[OEM_AMC_QUEUE_SLOTS];
    uint8_t external_grant_mask;
    uint8_t completion_handler_bound;
    uint8_t closed;
    uint8_t admission_paused; /* temporary sleep fence; under queue lock. */
    uint8_t faulted;
    uint64_t next_token;
    uint32_t completed;
} oem_amc_command_queue_t;

typedef enum oem_amc_queue_result {
    CMD_INVALID = -2,
    CMD_FAULT = -1,
    CMD_BLOCKED = 0,
    CMD_ACCEPTED = 1
} oem_amc_queue_result_t;

int oem_amc_queue_init(oem_amc_command_queue_t*, const oem_amc_cmd_ops_t*);
/* admission queues a request without MMIO. */
int oem_amc_queue_enqueue(oem_amc_command_queue_t*, uint32_t req_id, uint32_t res_key,
                          uint8_t is_read, uint64_t* token);
/* dispatch the FIFO head only with a verified grant; proven no-touch BUSY retains the FIFO
 * head, ambiguous failure quarantines the reserved slot. This queue is not yet the active
 * physical Windows transport. */

oem_amc_queue_result_t oem_amc_queue_dispatch_head(oem_amc_command_queue_t*, uint8_t slot);

int oem_amc_queue_resolve_inflight(void*, uint8_t slot, uint64_t* token);
int oem_amc_queue_complete_inflight(void*, uint8_t slot, uint64_t token);
/* read-only snapshot; under the same completion lock as the ACK provider. */
uint8_t oem_amc_queue_inflight_mask(const oem_amc_command_queue_t*);
/* all operations require the existing shared completion lock. Only the verified,
 * externally serialized owner may resume after has confirmed firmware return/TCS release. */

int oem_amc_queue_pause(oem_amc_command_queue_t*);
int oem_amc_queue_resume(oem_amc_command_queue_t*);

void oem_amc_queue_close(oem_amc_command_queue_t*);
int oem_amc_queue_can_release(const oem_amc_command_queue_t*);
#ifdef __cplusplus
}
#endif
#endif
