#include "vrm_acpi.h"
#include "vrm.h"
#include <string.h>

rail_result_t rail_vote_exact_acpi_action(rail_bank_t* bank, const char* path,
                                          const power_action_t* action, uint32_t dstate,
                                          uint64_t client)
{
    vrm_program_t program;
    rail_state_t vote;
    uint32_t rail;
    if (!bank || !action || !client || !vrm_prepare_known_dstate(path, action, dstate, &program))
        return RAIL_INVALID;
    if (!strcmp(program.res->command_db_name, "L4B_E0"))
        rail = RAIL_LDO4_B;
    else if (!strcmp(program.res->command_db_name, "L15B_E0"))
        rail = RAIL_LDO15_B;
    else
        return RAIL_INVALID;
    memset(&vote, 0, sizeof(vote));
    if (dstate == 0u) {
        vote.enabled = 1u;
        vote.microvolts = action->u.pmic_regulator.fields[1];
    }
    /* d3 releases THIS CLIENT's vote instead of writing a global disable request which could
     * turn off another live consumer. */

    return rail_vote(bank, rail, client, vote);
}
