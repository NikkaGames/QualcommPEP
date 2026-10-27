#ifndef OEM_TCS_ASYNC_H
#define OEM_TCS_ASYNC_H
/* this src DOES not grant ownership, map MMIO, or install a WDF callback. the same
 * validated controller-completion lock must serialize queue dispatch, this adapter, ACK
 * processing, and slot retirement. */

#include <stdint.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_TCS_ASYNC_SLOTS 3u

typedef struct oem_tcs_async_ops {
    void* ctx;
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    /* return 1 only for a verified single WRITE request with trusted address and value. never
     * infer address from the opaque res key. */

    int (*resolve_write)(void*, uint64_t token, uint32_t req_id, uint32_t res_key, uint32_t* addr,
                         uint32_t* data);
} oem_tcs_async_ops_t;

typedef enum oem_tcs_async_state {
    OEM_TCS_FREE = 0,
    OEM_TCS_SUBMISSION_UNCERTAIN = 1,
    OEM_TCS_INFLIGHT = 2,
    OEM_TCS_RETIRE_UNCERTAIN = 3
} oem_tcs_async_state_t;

typedef struct oem_tcs_async {
    oem_tcs_async_ops_t ops;
    uint8_t external_grant_mask;      /* zero until verified elsewhere. */
    uint8_t completion_handler_bound; /* zero until verified elsewhere. */
    uint8_t faulted;
    uint8_t closed;
    oem_tcs_async_state_t state[OEM_TCS_ASYNC_SLOTS];
    uint64_t token[OEM_TCS_ASYNC_SLOTS];
    uint32_t submitted;
    uint32_t retired;
} oem_tcs_async_t;

int oem_tcs_async_init(oem_tcs_async_t*, const oem_tcs_async_ops_t*);
/* an ambiguous result quarantines the reserved slot. */

int oem_tcs_async_submit(void* ctx, uint8_t slot, uint64_t token, uint32_t req_id, uint32_t res_key,
                         uint8_t is_read);
/* resolve the same request token in the completion ledger. */

int oem_tcs_async_validate_finished(oem_tcs_async_t*, uint8_t slot, uint64_t token);
/* after successful W1C and client completion, retire this exact token and clean the TCS
 * state. If any write/readback fails, quarantine, do not reuse or retry blindly. */

int oem_tcs_async_retire(oem_tcs_async_t*, uint8_t slot, uint64_t token);
void oem_tcs_async_close(oem_tcs_async_t*);
int oem_tcs_async_can_release(const oem_tcs_async_t*);
#ifdef __cplusplus
}
#endif
#endif
