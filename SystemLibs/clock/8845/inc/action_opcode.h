#ifndef CLOCK_ACTION_OPCODE_H
#define CLOCK_ACTION_OPCODE_H
/* decoding an ACPI opcode does not bind clients or establish hardware completion. */

#include "state_txn.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum clock_opcode_kind {
    CLOCK_OP_UNRESOLVED = 0,
    CLOCK_OP_ENABLE = 1,
    CLOCK_OP_DISABLE = 2,
    CLOCK_OP_ENABLE_SET_FREQ = 8,
    CLOCK_OP_CFG = 9
} clock_opcode_kind_t;

typedef struct clock_opcode {
    clock_opcode_kind_t kind;
    /* opaque native arguments. the DSDT encodes 19200000 and 4 for QUP3, but the signed PEP's
     * NPA vote/IOCTL layout is not proven. */

    uint32_t parameter0;
    uint32_t parameter1;
    uint32_t param_cnt;
} clock_opcode_t;

/* this is metadata only: success never authorizes a hardware vote or PoFx d0/d3. */

int decode_clock_opcode(const power_action_t* action, clock_opcode_t* decoded);
#ifdef __cplusplus
}
#endif
#endif
