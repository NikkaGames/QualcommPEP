#ifndef DPM_VERIFIED_H
#define DPM_VERIFIED_H
/* a PEP_DEVICE_POWER_STATE complete=1 is an OS lifecycle notify; it is NOT proof the graph
 * provider successfully issued and read back votes. No MMIO, ownership or callback
 * authorization is conferred by this struct. */

#include <stdint.h>

typedef struct dpm_verified {
    uint32_t known; /* 1 only after a verified graph transaction. */
    uint32_t state; /* verified graph target: d0..d3, not DEVICE_POWER_STATE. */
    uint32_t failed;
} dpm_verified_t;

static __inline void dpm_verified_reset(dpm_verified_t* v)
{
    if (!v)
        return;
    v->known = 0u;
    v->state = 0u;
    v->failed = 0u;
}

static __inline void dpm_verified_record(dpm_verified_t* v, uint32_t target, int verified)
{
    if (!v)
        return;
    if (target > 3u || !verified) {
        v->known = 0u;
        v->failed = 1u;
        return;
    }
    v->state = target;
    v->known = 1u;
    v->failed = 0u;
}

static __inline int dpm_verified_is(const dpm_verified_t* v, uint32_t target)
{
    return v && v->known && !v->failed && v->state == target;
}
#endif
