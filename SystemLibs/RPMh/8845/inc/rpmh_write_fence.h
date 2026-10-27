#ifndef RPMH_WRITE_FENCE_H
#define RPMH_WRITE_FENCE_H
/* post-submit uncertainty fence; it provides no ownership, retry, transport or completion
 * authority. */

#include <stdint.h>
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rpmh_write_fence {
    uint64_t epoch;
    uint8_t quarantined;
    uint8_t last_client;
    uint16_t last_result;
} rpmh_write_fence_t;

/* called once for a new, distinct native owner lifetime, before any request is issued.
 * never call this to repair a failed d0 revalidation. */

static inline int rpmh_write_fence_init(rpmh_write_fence_t* f, uint64_t owner_epoch)
{
    if (!f || !owner_epoch || f->epoch || f->quarantined)
        return 0;
    f->epoch = owner_epoch;
    f->last_result = TCS_OK;
    return 1;
}

/* invalid and NoOwnership are transport preflight refusals which never submit a command;
 * all other failed *post-dispatch* outcomes are treated as ambiguous (including BUSY, IRQ
 * conflict, ACK or timeout). the native caller must not invoke record for errors
 * discovered without dispatching. */

static inline int rpmh_write_fence_record(rpmh_write_fence_t* f, uint64_t owner_epoch,
                                          uint8_t client, rpmh_tcs_result_t result)
{
    if (!f || !f->epoch || !owner_epoch || f->epoch != owner_epoch || f->quarantined)
        return 0;
    f->last_client = client;
    f->last_result = (uint16_t)result;
    if (result == TCS_OK)
        return 1;
    if (result == TCS_INVALID || result == TCS_NO_OWNERSHIP)
        return 0;
    f->quarantined = 1u;
    return 0;
}

/* reset only after work/DPC flush, GIC/IRQ retirement, scheduler drain and final lease release.
 * Uncertain hardware cannot become a new epoch. */

static inline int rpmh_write_fence_retire(rpmh_write_fence_t* f, uint64_t owner_epoch)
{
    if (!f || !owner_epoch || f->epoch != owner_epoch || f->quarantined)
        return 0;
    f->epoch = 0u;
    f->last_result = TCS_OK;
    f->last_client = 0u;
    return 1;
}

static inline int rpmh_write_fence_live(const rpmh_write_fence_t* f, uint64_t owner_epoch)
{
    return f && f->epoch && f->epoch == owner_epoch && !f->quarantined;
}
#ifdef __cplusplus
}
#endif
#endif
