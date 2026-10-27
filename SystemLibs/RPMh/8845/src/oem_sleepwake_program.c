#include "oem_sleepwake_program.h"
#include <string.h>

static const oem_sleepwake_tcs_image_t* image_at(const oem_sleepwake_image_t* im, unsigned idx)
{
    return idx < OEM_SLEEP_TCS ? &im->sleep[idx] : &im->wake[idx - OEM_SLEEP_TCS];
}

static int healthy(oem_sleepwake_program_t* p, uint64_t token)
{
    oem_sleepwake_bridge_t* b;
    if (!p || !token || !(b = p->bridge) || b->closed || b->faulted || !b->voter || !b->sets ||
        !b->db || !b->ticket || b->ticket != token || b->image.epoch != p->epoch ||
        b->sets->revision != b->image.revision || b->voter->sleep_guard_token != token ||
        b->sets->resume_ticket != token || p->ops.quiesced(p->ops.ctx, p->epoch, token) != 1)
        return 0;
    return 1;
}

static int read_expected(oem_sleepwake_program_t* p, uint32_t off, uint32_t value)
{
    uint32_t r = 0;
    return off + 4u <= RSC_DRV2_BYTES && p->ops.read32(p->ops.ctx, off, &r) && r == value;
}

static int write_checked(oem_sleepwake_program_t* p, uint32_t off, uint32_t value)
{
    if (off + 4u > RSC_DRV2_BYTES || !p->ops.write32(p->ops.ctx, off, value))
        return 0;
    p->ops.barrier(p->ops.ctx);
    return read_expected(p, off, value);
}

/* do not trust the mutable image in isolation. must have produced precisely these
 * addresses and payloads from the sealed CMD_DB and frozen set. Reject tampered counts,
 * slots, masks and payloads before MMIO. */

static int valid_image(const oem_sleepwake_bridge_t* b)
{
    const oem_sleepwake_image_t* im = &b->image;
    const oem_voteset_state_t* s = b->sets;
    const oem_cmddb_resolver_t* d = b->db;
    unsigned i, j, mode;
    if (im->epoch != s->io.owner_epoch || im->revision != s->revision ||
        im->revision != s->snapshot_revision || im->sleep_mask != s->sleep_mask ||
        im->wake_mask != s->wake_mask || s->cnt > OEM_VOTESET_RESOURCES)
        return 0;
    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i) {
        const oem_sleepwake_tcs_image_t* img = image_at(im, i);
        if (img->slot != (i < OEM_SLEEP_TCS ? (MOLOKAI_WIN_SLEEP_START + i)
                                            : (MOLOKAI_WIN_WAKE_START + i - OEM_SLEEP_TCS)) ||
            img->cnt > TCS_BATCH_MAX || (img->cnt < 16u && (img->response_mask >> img->cnt)))
            return 0;
    }
    for (mode = 0; mode < 2; ++mode) {
        unsigned found = 0;
        uint32_t mask = mode ? im->wake_mask : im->sleep_mask;
        for (i = 0; i < s->cnt; ++i) {
            uint64_t value = 0;
            const oem_voteset_res_t* vr = &s->resume_snapshot[i];
            const oem_local_vote_t* local = &vr->local[mode ? 2u : 1u];
            const oem_cmddb_res_t* res = 0;
            const oem_sleepwake_tcs_image_t* img;
            const tcs_command_t* cmd;
            if (!(mask & (1u << i)))
                continue;
            for (j = 0; j < d->resources_cnt; ++j)
                if (d->resources[j].key == vr->key) {
                    res = &d->resources[j];
                    break;
                }
            if (!res || !res->allow_write || !res->address || res->address > 0x7ffffu ||
                (res->address & 3u) ||
                (local->dirty != OEM_SET_DIRTY &&
                 !(mode && local->dirty == OEM_SET_DIRTY_USE_ACTIVE && vr->local[0].valid)) ||
                (local->dirty == OEM_SET_DIRTY && !local->valid))
                return 0;
            value = (mode && local->dirty == OEM_SET_DIRTY_USE_ACTIVE) ? vr->local[0].data
                                                                       : local->data;
            if (value > UINT32_MAX)
                return 0;
            if (!mode) {
                if (found >= 32u)
                    return 0;
                img = &im->sleep[found / 16u];
                cmd = &img->commands[found % 16u];
            } else {
                if (found >= OEM_WAKE_TCS * TCS_BATCH_MAX)
                    return 0;
                img = &im->wake[found / TCS_BATCH_MAX];
                cmd = &img->commands[found % TCS_BATCH_MAX];
            }
            {
                unsigned pos = found % TCS_BATCH_MAX;
                uint8_t expected_response =
                    (uint8_t)(mode && local->dirty == OEM_SET_DIRTY_USE_ACTIVE
                                  ? vr->local[0].completion
                                  : local->completion);
                if (((img->response_mask >> pos) & 1u) != expected_response ||
                    cmd->address != res->address || cmd->data != (uint32_t)value || cmd->read)
                    return 0;
            }
            ++found;
        }
        if (!mode) {
            if (im->sleep[0].cnt != (found > 16u ? 16u : found) ||
                im->sleep[1].cnt != (found > 16u ? found - 16u : 0u))
                return 0;
        } else if (im->wake[0].cnt != (found > 16u ? 16u : found) ||
                   im->wake[1].cnt != (found > 16u ? found - 16u : 0u))
            return 0;
    }
    return 1;
}

static int owner(oem_sleepwake_program_t* p, uint64_t t, uint8_t slot)
{
    return p->ops.exclusive_owner(p->ops.ctx, p->epoch, t, slot) == 1;
}

int oem_sleep_wake_program_init(oem_sleepwake_program_t* p, oem_sleepwake_bridge_t* b,
                                const oem_sleepwake_program_ops_t* ops)
{
    if (!p || !b || !ops || !ops->exclusive_owner || !ops->quiesced || !ops->read32 ||
        !ops->write32 || !ops->barrier || !ops->firmware_released || !b->voter || !b->sets ||
        !b->db || !b->image.epoch || !b->ticket)
        return 0;
    memset(p, 0, sizeof(*p));
    p->bridge = b;
    p->ops = *ops;
    p->token = b->ticket;
    p->epoch = b->image.epoch;
    return 1;
}

int oem_sleep_wake_program_attach_dependencies(oem_sleepwake_program_t* p,
                                               const oem_sleepwake_dependencies_t* d)
{
    unsigned i;
    uint16_t mask = 0;
    if (!p || !d || p->closed || p->state != SLEEP_WAKE_PROGRAM_IDLE || p->dependencies ||
        d->bridge != p->bridge || d->token != p->token || p->bridge->hardware_started)
        return 0;
    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i)
        if (!oem_sleep_wake_deps_get(d, p->token, i, &mask))
            return 0;
    p->dependencies = d;
    return 1;
}

int oem_sleep_wake_program_commit(oem_sleepwake_program_t* p, uint64_t token)
{
    const oem_sleepwake_image_t* im;
    uint32_t id = 0, cfg = 0;
    uint16_t wait_masks[OEM_SLEEPWAKE_IMAGE_CNT] = {0};
    unsigned i, j;
    if (!p || p->closed || p->state != SLEEP_WAKE_PROGRAM_IDLE || p->token != token ||
        !healthy(p, token))
        return 0;
    im = &p->bridge->image;
    /* approval and immutable snapshot checked before the first MMIO write. */
    if (p->dependencies)
        for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i)
            if (!oem_sleep_wake_deps_get(p->dependencies, token, i, &wait_masks[i]))
                return 0;
    if (!valid_image(p->bridge) || !p->ops.read32(p->ops.ctx, RSC_ID_OFFSET, &id) ||
        ((id & RSC_ID_MASK) != RSC_ID_POR) ||
        !p->ops.read32(p->ops.ctx, RSC_PARENTCHILD_CONFIG_OFFSET, &cfg))
        return 0;
    {
        rsc_hlos_geometry_t g;
        if (!rpmh_tcs_check_hlos_geometry(cfg, MOLOKAI_WIN_TCS_TOTAL, TCS_BATCH_MAX, &g))
            return 0;
    }
    /* every slot checked before the first write, never overwrite a foreign configuration and
     * never infer ownership from an idle register. */

    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i) {
        const oem_sleepwake_tcs_image_t* s = image_at(im, i);
        uint32_t st = 0, ctrl = 0, enable = 0, wait = 0;
        if (!s->cnt)
            continue;
        if (!owner(p, token, s->slot) || !p->ops.read32(p->ops.ctx, TCS_STATUS(s->slot), &st) ||
            !p->ops.read32(p->ops.ctx, TCS_CONTROL(s->slot), &ctrl) ||
            !p->ops.read32(p->ops.ctx, TCS_ENABLE(s->slot), &enable) ||
            !p->ops.read32(p->ops.ctx, TCS_WAIT(s->slot), &wait) || !(st & TCS_STATUS_IDLE) ||
            (ctrl & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) || enable || wait)
            return 0;
    }
    /* once writes begin, any error is ambiguous: never rollback/replay without a separately
     * owned firmware protocol capable of proving it is safe. */

    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i) {
        const oem_sleepwake_tcs_image_t* s = image_at(im, i);
        uint32_t mask;
        if (!s->cnt)
            continue;
        p->state = SLEEP_WAKE_PROGRAM_QUARANTINED;
        if (!healthy(p, token) || !owner(p, token, s->slot))
            return 0;
        p->bridge->hardware_started = 1; /* before any possible MMIO mutation. */
        for (j = 0; j < s->cnt; ++j) {
            if (!write_checked(p, TCS_CMD_MSGID(s->slot, j),
                               TCS_MSGID_LENGTH | TCS_MSGID_WRITE |
                                   ((s->response_mask & (1u << j)) ? TCS_MSGID_RES_REQ : 0u)) ||
                !write_checked(p, TCS_CMD_ADDR(s->slot, j), s->commands[j].address) ||
                !write_checked(p, TCS_CMD_DATA(s->slot, j), s->commands[j].data))
                return 0;
        }
        mask = s->cnt == 16u ? 0xffffu : ((1u << s->cnt) - 1u);
        if (!write_checked(p, TCS_WAIT(s->slot), wait_masks[i]) ||
            !write_checked(p, TCS_ENABLE(s->slot), mask))
            return 0;
        p->slot_mask |= (uint8_t)(1u << s->slot);
    }
    p->state = SLEEP_WAKE_PROGRAM_COMMITTED;
    return 1;
}

int oem_sleep_wake_program_verify(oem_sleepwake_program_t* p, uint64_t token)
{
    unsigned i, j;
    uint16_t wait_masks[OEM_SLEEPWAKE_IMAGE_CNT] = {0};
    if (!p || p->closed || p->state != SLEEP_WAKE_PROGRAM_COMMITTED || token != p->token ||
        !healthy(p, token))
        return 0;
    if (p->dependencies)
        for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i)
            if (!oem_sleep_wake_deps_get(p->dependencies, token, i, &wait_masks[i]))
                goto uncertain;
    for (i = 0; i < OEM_SLEEPWAKE_IMAGE_CNT; ++i) {
        const oem_sleepwake_tcs_image_t* s = image_at(&p->bridge->image, i);
        uint32_t mask;
        if (!s->cnt)
            continue;
        mask = s->cnt == 16u ? 0xffffu : ((1u << s->cnt) - 1u);
        if (!owner(p, token, s->slot) || !read_expected(p, TCS_WAIT(s->slot), wait_masks[i]) ||
            !read_expected(p, TCS_ENABLE(s->slot), mask))
            goto uncertain;
        for (j = 0; j < s->cnt; ++j)
            if (!read_expected(p, TCS_CMD_MSGID(s->slot, j),
                               TCS_MSGID_LENGTH | TCS_MSGID_WRITE |
                                   ((s->response_mask & (1u << j)) ? TCS_MSGID_RES_REQ : 0u)) ||
                !read_expected(p, TCS_CMD_ADDR(s->slot, j), s->commands[j].address) ||
                !read_expected(p, TCS_CMD_DATA(s->slot, j), s->commands[j].data))
                goto uncertain;
    }
    return 1;
uncertain:
    p->state = SLEEP_WAKE_PROGRAM_QUARANTINED;
    return 0;
}

int oem_sleep_wake_program_retire(oem_sleepwake_program_t* p, uint64_t token)
{
    if (!p || p->state != SLEEP_WAKE_PROGRAM_COMMITTED || p->token != token || !p->bridge ||
        p->bridge->faulted || p->bridge->ticket || p->bridge->voter->sleep_guard_token ||
        p->bridge->sets->resume_ticket ||
        p->ops.firmware_released(p->ops.ctx, p->epoch, token) != 1)
        return 0;
    p->state = SLEEP_WAKE_PROGRAM_RETIRED;
    return 1;
}

void oem_sleep_wake_program_close(oem_sleepwake_program_t* p)
{
    if (p)
        p->closed = 1;
}

int oem_sleep_wake_program_can_release(const oem_sleepwake_program_t* p)
{
    return p && p->closed &&
           (p->state == SLEEP_WAKE_PROGRAM_IDLE || p->state == SLEEP_WAKE_PROGRAM_RETIRED);
}
