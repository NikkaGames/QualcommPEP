#ifndef POFX_PSTATE_PAYLOAD_H
#define POFX_PSTATE_PAYLOAD_H
/* WDF PoFx payload framing uses 8-byte pointers on ARM64.
 * report STATUS_NOT_SUPPORTED until the backend can execute and verify the operation. */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define POFX_MAX_SETS 32u

typedef struct pofx_v1_req {
    uint32_t comp;
    uint32_t p_state;
} pofx_v1_req_t;

typedef struct pofx_v2_header {
    uint32_t version;
    uint32_t comp;
    uint32_t req_type;
    uint32_t reserved;
    void* user_data;
} pofx_v2_header_t;

typedef struct pofx_v2_set {
    uint32_t set;
    uint32_t p_state;
} pofx_v2_set_t;

int po_fx_p_state_validate_v1(const void* input, size_t bytes, uint32_t* comp, uint32_t* p_state);
int po_fx_p_state_validate_v2(const void* input, size_t bytes, uint32_t* comp,
                              uint32_t* first_p_state, uint32_t* set_cnt);
#ifdef __cplusplus
}
#endif
#endif
