#include "oem_voter_sets.h"
#include <string.h>

static int same(oem_voteset_state_t* s, uint32_t key, uint64_t expected)
{
    uint64_t a = 0, b = 0;
    return s->io.owns(s->io.ctx, key) == 1 && s->io.read_current(s->io.ctx, key, &a) == 1 &&
           s->io.read_current(s->io.ctx, key, &b) == 1 && a == b && a == expected &&
           s->io.owns(s->io.ctx, key) == 1;
}

static oem_voteset_res_t* lookup(oem_voteset_state_t* s, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < s->cnt; ++i)
        if (s->res[i].key == key)
            return &s->res[i];
    return 0;
}

static void update(oem_voteset_state_t* s, uint8_t check_completion)
{
    uint32_t i;
    s->sleep_mask = 0;
    s->wake_mask = 0;
    for (i = 0; i < s->cnt; ++i) {
        oem_voteset_res_t* r = &s->res[i];
        oem_local_vote_t *a = &r->local[0], *sl = &r->local[1], *w = &r->local[2];
        a->dirty = (!a->valid || (r->remote_valid && a->data == r->at_rpmh &&
                                  (!check_completion || a->completion)))
                       ? OEM_SET_CLEAN
                       : OEM_SET_DIRTY;
        if (sl->explicit)
            sl->dirty = OEM_SET_CLEAN_EXPLICIT;
        else if (!sl->valid || (a->valid && sl->data == a->data))
            sl->dirty = OEM_SET_CLEAN;
        else {
            sl->dirty = OEM_SET_DIRTY;
            s->sleep_mask |= 1u << i;
        }
        if (w->explicit)
            w->dirty = OEM_SET_CLEAN_EXPLICIT;
        else if (w->valid &&
                 ((a->valid && a->data != w->data) || (sl->valid && sl->data != w->data))) {
            w->dirty = OEM_SET_DIRTY;
            s->wake_mask |= 1u << i;
        } else if (!w->valid && a->valid && sl->valid && a->data != sl->data && !sl->explicit) {
            w->dirty = OEM_SET_DIRTY_USE_ACTIVE;
            s->wake_mask |= 1u << i;
        } else
            w->dirty = OEM_SET_CLEAN;
    }
}

int oem_vote_sets_init(oem_voteset_state_t* s, const oem_voteset_io_t* io)
{
    if (!s || !io || !io->owner_epoch || !io->owns || !io->read_current || !io->resume_proof ||
        !io->active_proof)
        return 0;
    memset(s, 0, sizeof(*s));
    s->io = *io;
    return 1;
}

int oem_vote_sets_add(oem_voteset_state_t* s, uint32_t key, uint64_t value)
{
    oem_voteset_res_t* r;
    if (!s || !s->io.owns || !key || s->closed || s->sealed || s->faulted ||
        s->cnt >= OEM_VOTESET_RESOURCES || lookup(s, key))
        return 0;
    if (!same(s, key, value))
        return 0;
    r = &s->res[s->cnt++];
    memset(r, 0, sizeof(*r));
    r->key = key;
    r->at_rpmh = value;
    r->remote_valid = 1;
    r->local[0].valid = 1;
    r->local[0].completion = 1;
    r->local[0].data = value;
    update(s, 0);
    ++s->revision;
    return 1;
}

int oem_vote_sets_seal(oem_voteset_state_t* s)
{
    if (!s || s->closed || s->faulted || s->sealed || !s->cnt)
        return 0;
    s->sealed = 1;
    return 1;
}

int oem_vote_sets_set(oem_voteset_state_t* s, uint32_t key, uint8_t set, uint8_t valid,
                      uint64_t data, uint8_t completion, uint8_t explicit_vote)
{
    oem_voteset_res_t* r;
    oem_local_vote_t* v;
    if (!s || !s->sealed || s->closed || s->faulted || s->resume_ticket || set > 2 || valid > 1 ||
        completion > 1 || explicit_vote > 1 ||
        (set == OEM_SET_ACTIVE && (explicit_vote || !valid)) ||
        (!valid && (completion || explicit_vote)))
        return 0;
    r = lookup(s, key);
    if (!r)
        return 0;
    /* all operations require positive ownership, even software-only edits. */
    if (s->io.owns(s->io.ctx, key) != 1) {
        s->faulted = 1;
        return 0;
    }
    v = &r->local[set];
    v->valid = valid;
    v->data = valid ? data : 0;
    v->completion = completion;
    v->explicit = explicit_vote;
    update(s, 0);
    ++s->revision;
    return 1;
}

int oem_vote_sets_refresh(oem_voteset_state_t* s, uint8_t completion)
{
    if (!s || !s->sealed || s->closed || s->faulted || s->resume_ticket || completion > 1)
        return 0;
    update(s, completion);
    return 1;
}

int oem_vote_sets_command(const oem_voteset_state_t* s, uint8_t set, uint32_t idx, uint32_t* key,
                          uint64_t* data)
{
    uint32_t mask;
    const oem_voteset_res_t* r;
    const oem_local_vote_t* v;
    if (!s || !s->sealed || s->closed || s->faulted || s->resume_ticket ||
        (set != OEM_SET_SLEEP && set != OEM_SET_WAKE) || idx >= s->cnt || !key || !data)
        return 0;
    mask = set == OEM_SET_SLEEP ? s->sleep_mask : s->wake_mask;
    if (!(mask & (1u << idx)))
        return 0;
    r = &s->res[idx];
    v = &r->local[set];
    if (v->dirty == OEM_SET_DIRTY_USE_ACTIVE) {
        if (set != OEM_SET_WAKE || !r->local[0].valid)
            return 0;
        *data = r->local[0].data;
    } else if (v->dirty == OEM_SET_DIRTY && v->valid)
        *data = v->data;
    else
        return 0;
    *key = r->key;
    return 1;
}

int oem_vote_sets_commit_active(oem_voteset_state_t* s, uint32_t key, uint64_t token)
{
    oem_voteset_res_t* r;
    if (!s || !s->sealed || s->closed || s->faulted || s->resume_ticket || !token ||
        (uint32_t)(token >> 32) != s->io.owner_epoch)
        return 0;
    r = lookup(s, key);
    if (!r || !r->local[0].valid)
        return 0;
    if (s->io.active_proof(s->io.ctx, token, key, r->local[0].data) != 1 ||
        !same(s, key, r->local[0].data)) {
        s->faulted = 1;
        return 0;
    }
    r->at_rpmh = r->local[0].data;
    r->remote_valid = 1;
    update(s, 0);
    ++s->revision;
    return 1;
}

int oem_vote_sets_arm_resume(oem_voteset_state_t* s, uint64_t* ticket)
{
    uint32_t i;
    if (!s || !ticket || !s->sealed || s->closed || s->faulted || s->resume_ticket)
        return 0;
    *ticket = 0;
    /* reject outstanding active votes: sleep/wake planning cannot be sealed against an active
     * state that has not reached its verified baseline. */

    for (i = 0; i < s->cnt; ++i) {
        const oem_voteset_res_t* r = &s->res[i];
        if (!r->local[0].valid || !r->remote_valid || r->local[0].dirty != OEM_SET_CLEAN ||
            !r->local[0].completion || r->local[0].data != r->at_rpmh)
            return 0;
        if (!same(s, r->key, r->at_rpmh)) {
            s->faulted = 1;
            return 0;
        }
    }
    if (s->sequence == UINT32_MAX) {
        s->faulted = 1;
        return 0;
    }
    memcpy(s->resume_snapshot, s->res, s->cnt * sizeof(s->res[0]));
    s->snapshot_revision = s->revision;
    s->resume_ticket = ((uint64_t)s->io.owner_epoch << 32) | (++s->sequence);
    *ticket = s->resume_ticket;
    return 1;
}

int oem_vote_sets_commit_resume(oem_voteset_state_t* s, uint64_t ticket)
{
    uint32_t i;
    if (!s || !ticket || s->faulted || s->resume_ticket != ticket)
        return 0;
    if (s->snapshot_revision != s->revision || s->io.resume_proof(s->io.ctx, ticket) != 1) {
        s->faulted = 1;
        return 0;
    }

    for (i = 0; i < s->cnt; ++i) {
        const oem_voteset_res_t* r = &s->resume_snapshot[i];
        const oem_local_vote_t* w = &r->local[2];
        uint64_t desired =
            w->valid ? w->data
                     : (w->dirty == OEM_SET_DIRTY_USE_ACTIVE ? r->local[0].data : r->at_rpmh);
        /* a res not slated for wake must ALSO retain its recorded state; no silent drift across
         * system sleep/resume is permitted. */

        if (!same(s, r->key, desired)) {
            s->faulted = 1;
            return 0;
        }
    }
    for (i = 0; i < s->cnt; ++i) {
        oem_voteset_res_t* r = &s->res[i];
        const oem_local_vote_t* w = &s->resume_snapshot[i].local[2];
        if (w->valid) {
            r->local[0].valid = 1;
            r->local[0].data = w->data;
            r->local[0].completion = w->completion;
            r->at_rpmh = w->data;
            r->remote_valid = 1;
            if (!w->explicit)
                memset(&r->local[2], 0, sizeof(r->local[2]));
        } else if (w->dirty == OEM_SET_DIRTY_USE_ACTIVE) {
            r->at_rpmh = r->local[0].data;
            r->remote_valid = 1;
        }
    }
    s->resume_ticket = 0;
    update(s, 0);
    ++s->revision;
    return 1;
}

int oem_vote_sets_cancel_unstarted(oem_voteset_state_t* s, uint64_t ticket)
{
    if (!s || !ticket || s->faulted || s->resume_ticket != ticket)
        return 0;
    s->resume_ticket = 0;
    return 1;
}

void oem_vote_sets_close(oem_voteset_state_t* s)
{
    if (s)
        s->closed = 1;
}

int oem_vote_sets_can_release(const oem_voteset_state_t* s)
{
    return s && s->closed && !s->faulted && !s->resume_ticket;
}
