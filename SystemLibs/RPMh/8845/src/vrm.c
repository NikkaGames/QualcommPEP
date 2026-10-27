#include "vrm.h"
#include "cmddb.h"
#include <string.h>

static const vrm_desc_t k_known_ldos[] = {
    {"PPP_RESOURCE_ID_LDO4_B", "L4B_E0", "pm_humu_l4", 1650000u, 1950000u},
    {"PPP_RESOURCE_ID_LDO15_B", "L15B_E0", "pm_humu_l15", 1620000u, 2000000u}};

int vrm_prepare_known_dstate(const char* dev_path, const power_action_t* action, uint32_t state,
                             vrm_program_t* program)
{
    static const uint32_t d0[5] = {1u, 1800000u, 1u, 7u, 0u};
    static const uint32_t d3[5] = {1u, 0u, 0u, 7u, 0u};
    const uint32_t* expected;
    const vrm_desc_t* desc;
    uint32_t uv;
    if (!program)
        return 0;
    memset(program, 0, sizeof(*program));
    if (!action || !dev_path || action->kind != POWER_PMIC_REGULATOR ||
        (state != 0u && state != 3u))
        return 0;
    if (!strcmp(dev_path, "\\_SB.TSC1"))
        desc = &k_known_ldos[0];
    else if (!strcmp(dev_path, "\\_SB.SPK1"))
        desc = &k_known_ldos[1];
    else
        return 0;
    if (strcmp(action->u.pmic_regulator.name, desc->ppp_name) != 0)
        return 0;
    expected = state == 0u ? d0 : d3;
    if (memcmp(action->u.pmic_regulator.fields, expected, sizeof(d0)) != 0)
        return 0;
    program->res = desc;
    if (state == 3u) {
        program->cnt = 1u;
        program->commands[0].offset = VRM_ENABLE_OFFSET;
        program->commands[0].data = 0u;
        return 1;
    }
    uv = action->u.pmic_regulator.fields[1];
    if (uv < desc->min_microvolts || uv > desc->max_microvolts) {
        memset(program, 0, sizeof(*program));
        return 0;
    }
    /* this data conversion is not a claim that a TCS slot is owned. */

    program->cnt = 2u;
    program->commands[0].offset = VRM_VOLTAGE_OFFSET;
    program->commands[0].data = (uv + 999u) / 1000u;
    program->commands[1].offset = VRM_ENABLE_OFFSET;
    program->commands[1].data = 1u;
    return 1;
}

int vrm_prepare_tsc1_ldo4_b(const power_action_t* action, uint32_t state, vrm_program_t* program)
{
    return vrm_prepare_known_dstate("\\_SB.TSC1", action, state, program);
}

int vrm_prepare_aggregate(uint32_t rail, rail_state_t state, vrm_program_t* program)
{
    const vrm_desc_t* d;
    if (!program)
        return 0;
    memset(program, 0, sizeof(*program));
    if (rail == RAIL_LDO4_B)
        d = &k_known_ldos[0];
    else if (rail == RAIL_LDO15_B)
        d = &k_known_ldos[1];
    else
        return 0;
    if (state.enabled > 1u)
        return 0;
    if (!state.enabled) {
        if (state.microvolts)
            return 0;
        program->res = d;
        program->cnt = 1;
        program->commands[0].offset = VRM_ENABLE_OFFSET;
        return 1;
    }
    if (state.microvolts < d->min_microvolts || state.microvolts > d->max_microvolts)
        return 0;
    program->res = d;
    program->cnt = 2;
    program->commands[0].offset = VRM_VOLTAGE_OFFSET;
    program->commands[0].data = (state.microvolts + 999u) / 1000u;
    program->commands[1].offset = VRM_ENABLE_OFFSET;
    program->commands[1].data = 1;
    return 1;
}

int vrm_resolve_address(uint32_t base, const vrm_program_t* program, uint32_t* addresses,
                        uint32_t capacity)
{
    uint32_t i;
    if (!base || !program ||
        (program->res != &k_known_ldos[0] && program->res != &k_known_ldos[1]) || !addresses ||
        !program->cnt || program->cnt > VRM_MAX_CMDS || capacity < program->cnt || (base & 3u) ||
        base > 0x7ffffu - VRM_MODE_OFFSET ||
        /* a VRM register offset may not silently spill into the next slave's address space. */

        ((base + VRM_MODE_OFFSET) >> 16) != (base >> 16))
        return 0;
    /* a caller-modified command list cannot bypass device-specific preparation to target an
     * unrelated VRM register. */

    if (program->cnt == 1u) {
        if (program->commands[0].offset != VRM_ENABLE_OFFSET || program->commands[0].data != 0u)
            return 0;
    } else if (program->cnt == 2u) {
        if (program->commands[0].offset != VRM_VOLTAGE_OFFSET ||
            program->commands[0].data < (program->res->min_microvolts + 999u) / 1000u ||
            program->commands[0].data > (program->res->max_microvolts + 999u) / 1000u ||
            program->commands[1].offset != VRM_ENABLE_OFFSET || program->commands[1].data != 1u)
            return 0;
    } else
        return 0;
    for (i = 0; i < program->cnt; ++i)
        addresses[i] = base + program->commands[i].offset;
    return 1;
}

rpmh_tcs_result_t vrm_submit_known_program(rpmh_tcs_t* transport, uint8_t active_tcs,
                                           uint32_t command_db_address,
                                           const vrm_program_t* program)
{
    uint32_t addresses[VRM_MAX_CMDS];
    tcs_command_t commands[VRM_MAX_CMDS];
    uint32_t i;
    if (!transport || !vrm_resolve_address(command_db_address, program, addresses, VRM_MAX_CMDS))
        return TCS_INVALID;
    for (i = 0; i < program->cnt; ++i) {
        commands[i].read = 0u;
        commands[i].address = addresses[i];
        commands[i].data = program->commands[i].data;
    }
    /* failure of a later command can leave an earlier command applied; the transport
     * quarantines that slot, but cannot reverse rail changes. */

    return rpmh_tcs_issue_batch(transport, active_tcs, commands, program->cnt);
}

/* the caller must separately prove that this snapshot belongs to the current firmware and
 * that it owns the RSC/TCS and regulator client. success here is an acknowledgement of
 * only the RPMh commands, not of any PoFx device transition or voltage readback. */

rpmh_tcs_result_t vrm_submit_from_cmd_db(rpmh_tcs_t* transport, uint8_t active_tcs,
                                         const uint8_t* database, size_t database_bytes,
                                         const vrm_program_t* program)
{
    cmddb_entry_t entry;
    if (!program || (program->res != &k_known_ldos[0] && program->res != &k_known_ldos[1]) ||
        !cmd_db_lookup(database, database_bytes, program->res->command_db_name, &entry))
        return TCS_INVALID;
    return vrm_submit_known_program(transport, active_tcs, entry.address, program);
}
