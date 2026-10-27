#ifndef RPMH_OEM_READ_SEQUENCE_H
#define RPMH_OEM_READ_SEQUENCE_H
/* boot.MXF Qualcomm RpmhLib rpmh_cmdq_send_read(): one READ per AMC, dependency_bmsk=0. An
 * active read is completed before the next read is issued, matching the OEM in-flight READ
 * limitation. this is a client-side serialization policy, not a substitute completion,
 * owner grant or retry. */
#include "rpmh_tcs.h"

typedef rpmh_tcs_result_t (*rpmh_read_one_t)(void* ctx, uint8_t client,
                                             const tcs_command_t* command, uint32_t cnt,
                                             uint32_t* response);

/* completed is a count of individually VERIFIED reads, not an indication of success for a
 * partially observed multi-register snapshot. */
static inline rpmh_tcs_result_t rpmh_read_sequence(void* ctx, rpmh_read_one_t submit,
                                                   uint8_t client, const tcs_command_t* commands,
                                                   uint32_t cnt, uint32_t* responses,
                                                   uint32_t* completed)
{
    uint32_t i;
    rpmh_tcs_result_t status;
    if (completed)
        *completed = 0u;
    if (!ctx || !submit || !commands || !responses || !cnt || cnt > TCS_BATCH_MAX)
        return TCS_INVALID;
    /* validate the ENTIRE group before any physical operation. */
    for (i = 0u; i < cnt; ++i)
        if (commands[i].read != 1u || commands[i].data || (commands[i].address & ~0x7FFFFu))
            return TCS_INVALID;
    for (i = 0u; i < cnt; ++i) {
        uint32_t value = 0u;
        /* count=1 selects the OEM's zero dependency mask. the caller's existing synchronous
         * transport confirms the exact IRQ/ACK and final IRQ restore before returning TCS_OK. */
        status = submit(ctx, client, &commands[i], 1u, &value);
        if (status != TCS_OK)
            return status;
        responses[i] = value;
        if (completed)
            *completed = i + 1u;
    }
    return TCS_OK;
}
#endif
