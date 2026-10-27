#include "oem_sleepwake_bridge.h"
#include <string.h>

static const oem_voter_res_t* voter_res(const oem_voter_state_t* v, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < v->res_cnt; ++i)
        if (v->resources[i].key == key)
            return &v->resources[i];
    return 0;
}

static const oem_cmddb_res_t* cmd_res(const oem_cmddb_resolver_t* d, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < d->resources_cnt; ++i)
        if (d->resources[i].key == key)
            return &d->resources[i];
    return 0;
}

static int append(oem_sleepwake_tcs_image_t* images, uint32_t slots, uint32_t key, uint64_t data,
                  uint8_t response, const oem_cmddb_resolver_t* db)
{
    const oem_cmddb_res_t* r = cmd_res(db, key);
    uint32_t i;
    if (!r || !r->allow_write || !r->address || r->address > 0x7ffffu || (r->address & 3u) ||
        data > UINT32_MAX)
        return 0;
    for (i = 0; i < slots; ++i)
        if (images[i].cnt < TCS_BATCH_MAX) {
            uint8_t idx = images[i].cnt++;
            tcs_command_t* c = &images[i].commands[idx];
            c->address = r->address;
            c->data = (uint32_t)data;
            c->read = 0;
            if (response)
                images[i].response_mask |= (uint16_t)(1u << idx);
            return 1;
        }
    return 0;
}

int oem_sleep_wake_init(oem_sleepwake_bridge_t* b, oem_voter_state_t* v, oem_voteset_state_t* s,
                        oem_cmddb_resolver_t* db)
{
    if (!b || !v || !s || !db || !v->sealed || !s->sealed || !db->sealed || !v->io.owner_epoch ||
        v->io.owner_epoch != s->io.owner_epoch || v->res_cnt != s->cnt)
        return 0;
    memset(b, 0, sizeof(*b));
    b->voter = v;
    b->sets = s;
    b->db = db;
    return 1;
}

int oem_sleep_wake_ready(void* ctx, uint32_t key, uint32_t req_id, uint8_t is_read)
{
    oem_sleepwake_bridge_t* b = ctx;
    if (!b || !b->voter || !b->sets || !b->db || b->closed || b->faulted || b->sets->faulted ||
        b->db->faulted)
        return -1;
    if (b->ticket)
        return 0;
    return oem_voter_ready(b->voter, key, req_id, is_read);
}

int oem_sleep_wake_build(oem_sleepwake_bridge_t* b, oem_sleepwake_image_t* out)
{
    oem_sleepwake_image_t image;
    uint32_t i, j;
    oem_voter_state_t* v;
    oem_voteset_state_t* s;
    oem_cmddb_resolver_t* db;
    if (!b || !out || !b->voter || !b->sets || !b->db || b->closed || b->faulted || b->ticket ||
        b->hardware_started)
        return 0;
    v = b->voter;
    s = b->sets;
    db = b->db;
    if (!v->sealed || !s->sealed || !db->sealed || v->closed || s->closed || db->closed ||
        v->faulted || s->faulted || db->faulted || v->pending.ticket || v->sleep_guard_token ||
        s->resume_ticket || v->res_cnt != s->cnt || v->io.owner_epoch != s->io.owner_epoch ||
        v->res_cnt > OEM_VOTESET_RESOURCES)
        return 0;

    for (j = 0; j < OEM_CMDDB_REQUESTS; ++j)
        if (db->requests[j].token)
            return 0;
    memset(&image, 0, sizeof(image));
    image.epoch = s->io.owner_epoch;
    image.revision = s->revision;
    image.sleep_mask = s->sleep_mask;
    image.wake_mask = s->wake_mask;
    for (i = 0; i < OEM_SLEEP_TCS; ++i)
        image.sleep[i].slot = (uint8_t)(MOLOKAI_WIN_SLEEP_START + i);
    for (i = 0; i < OEM_WAKE_TCS; ++i)
        image.wake[i].slot = (uint8_t)(MOLOKAI_WIN_WAKE_START + i);
    for (i = 0; i < s->cnt; ++i) {
        const oem_voteset_res_t* r = &s->res[i];
        const oem_voter_res_t* vr = voter_res(v, r->key);
        uint32_t key = 0;
        uint64_t data = 0;
        if (!vr || !r->local[0].valid || !r->remote_valid || r->local[0].dirty != OEM_SET_CLEAN ||
            !r->local[0].completion || vr->committed != r->at_rpmh ||
            r->local[0].data != vr->committed || oem_voter_ready(v, r->key, 1u, 0u) != 1)
            return 0;

        if ((r->local[2].valid && !r->local[2].completion) ||
            (r->local[1].explicit && r->local[1].valid && r->local[1].data != vr->committed) ||
            (r->local[2].valid && r->local[2].data != vr->committed))
            return 0;
        if (image.sleep_mask & (1u << i)) {
            if (!oem_vote_sets_command(s, OEM_SET_SLEEP, i, &key, &data) || key != r->key ||
                !append(image.sleep, OEM_SLEEP_TCS, key, data, r->local[1].completion, db))
                return 0;
        }
        if (image.wake_mask & (1u << i)) {
            if (!oem_vote_sets_command(s, OEM_SET_WAKE, i, &key, &data) || key != r->key ||
                data != vr->committed ||
                !append(image.wake, OEM_WAKE_TCS, key, data,
                        r->local[2].valid ? r->local[2].completion : r->local[0].completion, db))
                return 0;
        }
    }
    *out = image;
    return 1;
}

int oem_sleep_wake_arm(oem_sleepwake_bridge_t* b, uint64_t* ticket)
{
    oem_sleepwake_image_t image;
    uint64_t token = 0;
    if (!b || !ticket)
        return 0;
    *ticket = 0;
    if (!oem_sleep_wake_build(b, &image) || !oem_vote_sets_arm_resume(b->sets, &token))
        return 0;
    if (!oem_voter_enter_sleep(b->voter, token)) {

        if (!oem_vote_sets_cancel_unstarted(b->sets, token))
            b->faulted = 1;
        return 0;
    }
    b->image = image;
    b->ticket = token;
    *ticket = token;
    return 1;
}

int oem_sleep_wake_commit_resume(oem_sleepwake_bridge_t* b, uint64_t token)
{
    uint32_t i;
    if (!b || !token || b->faulted || b->ticket != token ||
        b->sets->revision != b->image.revision || b->sets->io.owner_epoch != b->image.epoch ||
        b->voter->sleep_guard_token != token)
        return 0;
    /* only an independently proven firmware transition can mutate. */
    if (!oem_vote_sets_commit_resume(b->sets, token)) {
        b->faulted = 1;
        return 0;
    }
    /* stores ACTIVE client aggregates. Their committed state must still match every now-active
     * value; rebasing would silently invent new client votes, so quarantine any discrepancy
     * instead. */

    for (i = 0; i < b->sets->cnt; ++i) {
        const oem_voteset_res_t* r = &b->sets->res[i];
        const oem_voter_res_t* vr = voter_res(b->voter, r->key);
        if (!vr || !r->local[0].valid || r->local[0].data != vr->committed) {
            b->faulted = 1;
            return 0;
        }
    }
    if (!oem_voter_exit_sleep(b->voter, token)) {
        b->faulted = 1;
        return 0;
    }
    b->ticket = 0;
    b->hardware_started = 0;
    return 1;
}

int oem_sleep_wake_cancel_unstarted(oem_sleepwake_bridge_t* b, uint64_t token)
{
    if (!b || !token || b->faulted || b->ticket != token || b->hardware_started ||
        b->voter->sleep_guard_token != token || b->sets->resume_ticket != token)
        return 0;
    if (!oem_vote_sets_cancel_unstarted(b->sets, token)) {
        b->faulted = 1;
        return 0;
    }
    if (!oem_voter_cancel_unstarted_sleep(b->voter, token)) {
        b->faulted = 1;
        return 0;
    }
    b->ticket = 0;
    return 1;
}

void oem_sleep_wake_close(oem_sleepwake_bridge_t* b)
{
    if (b)
        b->closed = 1;
}

int oem_sleep_wake_can_release(const oem_sleepwake_bridge_t* b)
{
    return b && b->closed && !b->faulted && !b->ticket;
}
