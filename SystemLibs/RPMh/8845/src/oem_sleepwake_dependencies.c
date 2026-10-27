#include "oem_sleepwake_dependencies.h"
#include <string.h>

static const oem_sleepwake_tcs_image_t* get_image(const oem_sleepwake_image_t* im, unsigned idx)
{
    return idx < OEM_SLEEP_TCS ? &im->sleep[idx] : &im->wake[idx - OEM_SLEEP_TCS];
}

static int mask_valid(const oem_sleepwake_tcs_image_t* im, unsigned idx, uint16_t mask)
{
    uint32_t allowed;
    if (im->slot != (idx < OEM_SLEEP_TCS ? (MOLOKAI_WIN_SLEEP_START + idx)
                                         : (MOLOKAI_WIN_WAKE_START + idx - OEM_SLEEP_TCS)) ||
        im->cnt > TCS_BATCH_MAX)
        return 0;
    if (im->cnt < 2u)
        return mask == 0;
    allowed = (1u << (im->cnt - 1u)) - 1u;

    return !(mask & ~allowed) && !(mask & ~(uint32_t)im->response_mask);
}

static int authentic(const oem_sleepwake_dependencies_t* p, uint64_t token)
{
    if (!p || !p->bridge || !p->sealed || !token || p->token != token || !p->ops.authorize ||
        p->bridge->faulted || p->bridge->ticket != token ||
        p->bridge->image.epoch != p->snapshot.epoch ||
        p->bridge->sets->revision != p->snapshot.revision ||
        /* whole-image comparison includes command order, values, addresses, response encoding, res
         * masks, epoch and revision. */

        memcmp(&p->bridge->image, &p->snapshot, sizeof(p->snapshot)) != 0)
        return 0;
    return 1;
}

int oem_sleep_wake_deps_init(oem_sleepwake_dependencies_t* p, oem_sleepwake_bridge_t* b,
                             const oem_sleepwake_dep_ops_t* ops)
{
    if (!p || !b || !ops || !ops->authorize || !b->sets || !b->voter || !b->db || !b->image.epoch ||
        !b->ticket || b->closed || b->faulted || b->hardware_started)
        return 0;
    memset(p, 0, sizeof(*p));
    p->bridge = b;
    p->ops = *ops;
    return 1;
}

int oem_sleep_wake_deps_seal(oem_sleepwake_dependencies_t* p, uint64_t token,
                             const uint16_t masks[OEM_SLEEPWAKE_IMAGE_CNT])
{
    unsigned i;
    if (!p || !p->bridge || !masks || p->sealed || !token || p->bridge->ticket != token ||
        p->bridge->closed || p->bridge->faulted || p->bridge->hardware_started ||
        p->bridge->image.epoch != p->bridge->sets->io.owner_epoch ||
        p->bridge->voter->sleep_guard_token != token || p->bridge->sets->resume_ticket != token)
        return 0;
    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i) {
        const oem_sleepwake_tcs_image_t* im = get_image(&p->bridge->image, i);
        if (!mask_valid(im, i, masks[i]) ||
            (im->cnt && p->ops.authorize(p->ops.ctx, p->bridge->image.epoch, token, im->slot,
                                         im->cnt, im->response_mask, masks[i], im->commands) != 1))
            return 0;
    }
    p->snapshot = p->bridge->image;
    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i)
        p->wait_mask[i] = p->frozen_mask[i] = masks[i];
    p->token = token;
    p->sealed = 1;
    return 1;
}

int oem_sleep_wake_deps_get(const oem_sleepwake_dependencies_t* p, uint64_t token, unsigned idx,
                            uint16_t* out_mask)
{
    const oem_sleepwake_tcs_image_t* im;
    if (!out_mask || idx >= OEM_SLEEPWAKE_IMAGE_CNT || !authentic(p, token))
        return 0;
    im = get_image(&p->snapshot, idx);
    if (p->wait_mask[idx] != p->frozen_mask[idx] || !mask_valid(im, idx, p->wait_mask[idx]) ||
        (im->cnt && p->ops.authorize(p->ops.ctx, p->snapshot.epoch, token, im->slot, im->cnt,
                                     im->response_mask, p->wait_mask[idx], im->commands) != 1))
        return 0;
    *out_mask = p->wait_mask[idx];
    return 1;
}
