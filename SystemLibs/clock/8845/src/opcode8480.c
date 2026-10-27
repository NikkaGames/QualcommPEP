#include "opcode8480.h"
#include <string.h>

int decode_clock_opcode8480(const power_action_t* action, clock_action8480_t* decoded)
{
    if (!decoded)
        return 0;
    memset(decoded, 0, sizeof(*decoded));
    if (!action || action->kind != POWER_CLOCK)
        return 0;
    if (action->u.clock.extra_cnt == 0u &&
        (action->u.clock.vote == 1u || action->u.clock.vote == 2u)) {
        decoded->kind = action->u.clock.vote == 1u ? CLOCK8480_ENABLE : CLOCK8480_DISABLE;
        return 1;
    }
    /* the supplied Molokai BSMD packages carry exactly two parameters for this opcode.
     * established native scalar request ordering, but this decoder alone does not execute or
     * verify hardware. */

    if (action->u.clock.vote == 8u && action->u.clock.extra_cnt == 2u) {
        decoded->kind = CLOCK8480_ENABLE_SET_FREQ;
        decoded->parameter0 = action->u.clock.extra[0];
        decoded->parameter1 = action->u.clock.extra[1];
        decoded->param_cnt = 2u;
        return 1;
    }

    if (action->u.clock.vote == 9u && action->u.clock.extra_cnt == 1u &&
        action->u.clock.extra[0] == 18u &&
        strncmp(action->u.clock.name, "gcc_ufs_phy_ice_core_clk", sizeof(action->u.clock.name)) ==
            0) {
        decoded->kind = CLOCK8480_CFG;
        decoded->parameter0 = 18u;
        decoded->param_cnt = 1u;
        return 1;
    }
    return 0;
}
