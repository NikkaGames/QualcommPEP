#ifndef CLOCK_OPCODE8480_H
#define CLOCK_OPCODE8480_H
/* SC8480 action opcodes do not establish client identity, ownership, completion or opaque argument
 * semantics. Verify scalar ordering separately. */

#include "state_txn.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum clock_action8480_kind {
    CLOCK8480_UNRESOLVED = 0,
    CLOCK8480_ENABLE = 1,
    CLOCK8480_DISABLE = 2,
    CLOCK8480_ENABLE_SET_FREQ = 8,
    CLOCK8480_CFG = 9
} clock_action8480_kind_t;

typedef struct clock_action8480 {
    clock_action8480_kind_t kind;
    /* opaque native arguments. The DSDT encodes 19200000 and 4 for QUP3, but the signed PEP's
     * NPA vote/IOCTL layout is not proven. */

    uint32_t parameter0;
    uint32_t parameter1;
    uint32_t param_cnt;
} clock_action8480_t;

/* this is metadata ONLY: success never authorizes a hardware vote or PoFx d0/d3. */

int decode_clock_opcode8480(const power_action_t* action, clock_action8480_t* decoded);
#ifdef __cplusplus
}
#endif
#endif
