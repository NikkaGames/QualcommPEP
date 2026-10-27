#ifndef RPMH_BUSY_ARBITRATION_H
#define RPMH_BUSY_ARBITRATION_H
/* retry BUSY only after proving no dispatch occurred or cancelling the queued ticket. */

#include "rpmh_tcs.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef rpmh_tcs_result_t (*rpmh_busy_dispatch_t)(void*, uint8_t, const tcs_command_t*, uint32_t,
                                                  uint32_t*);

static inline rpmh_tcs_result_t rpmh_try_owned_amcs(rpmh_busy_dispatch_t dispatch, void* ctx,
                                                    uint32_t active_cnt, uint8_t client,
                                                    const tcs_command_t* commands, uint32_t cnt,
                                                    uint32_t* responses, uint32_t* attempts)
{
    uint32_t i;
    rpmh_tcs_result_t result = TCS_INVALID;
    if (attempts)
        *attempts = 0u;
    if (!dispatch || !active_cnt || active_cnt > 3u || !commands || !cnt || cnt > TCS_BATCH_MAX)
        return TCS_INVALID;
    for (i = 0u; i < active_cnt; ++i) {
        result = dispatch(ctx, client, commands, cnt, responses);
        if (attempts)
            *attempts = i + 1u;
        if (result != TCS_BUSY)
            break;
    }
    return result;
}
#ifdef __cplusplus
}
#endif
#endif
