#include "group_vote.h"
#include <string.h>

static int valid_path(const char* p, size_t* length)
{
    size_t n = 0u;
    if (!p || p[0] != '\\')
        return 0;
    while (n < 256u && p[n])
        ++n;
    if (length)
        *length = n;
    return n >= 5u && n < 240u;
}

static int live(const group_vote_t* scope)
{
    uint64_t epoch = 0u;
    return scope && scope->bound && !scope->quarantined && scope->io.read_epoch &&
           scope->io.read_epoch(scope->io.ctx, &epoch) && epoch == scope->epoch;
}

static power_result_t fail(power_execution_t* t, power_result_t r, uint32_t where)
{
    if (t) {
        t->result = r;
        t->failure_idx = where;
    }
    return r;
}

int group_vote_bind(group_vote_t* s, const group_vote_io_t* io, const char* path, uint32_t comp,
                    uint32_t set, uint64_t epoch)
{
    size_t len;
    uint64_t actual = 0u;
    unsigned n, values[2], field;
    if (!s || s->bound || s->quarantined || !io || !epoch || comp >= 32u || set >= 32u ||
        !valid_path(path, &len) || !io->lock || !io->unlock || !io->read_epoch || !io->supports ||
        !io->snapshot || !io->apply || !io->verify || !io->restore || !io->verify_restored ||
        !io->verify_final || !io->verify_released || !io->read_clock_config ||
        !io->read_epoch(io->ctx, &actual) || actual != epoch)
        return 0;
    memset(s, 0, sizeof(*s));
    s->io = *io;
    s->epoch = epoch;
    s->comp = comp;
    s->set = set;
    memcpy(s->dev_path, path, len + 1u);
    memcpy(s->identity, path, len);
    s->identity[len++] = '.';
    s->identity[len++] = 'P';
    s->identity[len++] = 'F';
    values[0] = comp;
    values[1] = set;
    for (field = 0u; field < 2u; ++field) {
        char digits[11];
        unsigned cnt = 0u;
        n = values[field];
        do {
            digits[cnt++] = (char)('0' + n % 10u);
            n /= 10u;
        } while (n);
        while (cnt)
            s->identity[len++] = digits[--cnt];
        if (!field)
            s->identity[len++] = '.';
    }
    s->identity[len] = 0;
    s->bound = 1u;
    return 1;
}

power_result_t group_vote_transition(group_vote_t* s, const power_pstate_program_t* program,
                                     power_execution_t* trace)
{
    const power_plan_t* plan;
    uint32_t i, applied = 0u, restore_cnt = 0u;
    power_result_t result = POWER_BAD_INPUT;
    int clean = 1;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!s || !program || !live(s) || s->busy || program->comp != s->comp ||
        program->set != s->set || program->state >= 32u ||
        program->resources.target_d_state != program->state || !program->resources.action_cnt ||
        program->resources.action_cnt > POWER_MAX_ACTIONS ||
        strcmp(program->resources.dev_path, s->dev_path))
        return fail(trace, POWER_BAD_INPUT, UINT32_MAX);
    if (s->state_known && s->state == program->state)
        return fail(trace, POWER_NO_KNOWN_STATE, UINT32_MAX);
    s->busy = 1u;
    if (!s->io.lock(s->io.ctx)) {
        s->busy = 0u;
        return fail(trace, POWER_UNSUPPORTED, UINT32_MAX);
    }
    if (!live(s)) {
        result = POWER_QUARANTINED;
        goto done;
    }
    plan = &program->resources;
    /* no hardware writes until the complete desc program is supported. */
    for (i = 0; i < plan->action_cnt; ++i) {
        const power_action_t* a = &plan->actions[i];
        if (a->kind != POWER_CLOCK && a->kind != POWER_ICB && a->kind != POWER_PMIC_REGULATOR) {
            result = POWER_UNSUPPORTED;
            trace->failure_idx = i;
            goto done;
        }
        if (!s->io.supports(s->io.ctx, s->identity, program->state, a)) {
            result = POWER_UNSUPPORTED;
            trace->failure_idx = i;
            goto done;
        }
    }
    /* a stable per-set identity updates the old client's vote. For example, CLOCK enable,
     * CONFIGURE and rate all act on that same native client. */
    for (i = 0; i < plan->action_cnt; ++i) {
        const power_action_t* a = &plan->actions[i];
        if (a->kind == POWER_CLOCK && a->u.clock.vote == 9u) {
            uint32_t observed = UINT32_MAX;
            if (!s->io.read_clock_config(s->io.ctx, a->u.clock.name, &observed) || observed > 1u) {
                result = POWER_SNAPSHOT_FAILED;
                trace->failure_idx = i;
                goto rollback;
            }
            if (s->config_captured) {
                if (strcmp(s->config_clock, a->u.clock.name)) {
                    result = POWER_UNSUPPORTED;
                    trace->failure_idx = i;
                    goto rollback;
                }
            } else {
                memcpy(s->config_clock, a->u.clock.name, sizeof(s->config_clock));
                s->config_clock[sizeof(s->config_clock) - 1u] = 0;
                s->config_baseline = observed;
                s->config_captured = 1u;
            }
        }
        if (!s->io.snapshot(s->io.ctx, s->identity, program->state, a, &s->snapshots[i])) {
            result = POWER_SNAPSHOT_FAILED;
            trace->failure_idx = i;
            goto rollback;
        }
        /* apply may have dispatched before reporting an error: restore it. */
        applied = i + 1u;
        s->touched = 1u;
        if (!s->io.apply(s->io.ctx, s->identity, program->state, a)) {
            result = POWER_APPLY_FAILED;
            trace->failure_idx = i;
            goto rollback;
        }
        if (!s->io.verify(s->io.ctx, s->identity, program->state, a)) {
            result = POWER_VERIFY_FAILED;
            trace->failure_idx = i;
            goto rollback;
        }
    }
    s->final_plan = *plan;
    memcpy(s->final_plan.dev_path, s->identity, sizeof(s->identity));
    if (!s->io.verify_final(s->io.ctx, s->identity, program->state, &s->final_plan)) {
        result = POWER_FINAL_STATE_FAILED;
        trace->failure_idx = plan->action_cnt;
        goto rollback;
    }
    /* epoch can change during a slow RPMh round trip; old ownership is never success. */
    if (!live(s)) {
        result = POWER_QUARANTINED;
        trace->failure_idx = plan->action_cnt;
        goto rollback;
    }
    s->state_known = 1u;
    s->state = program->state;
    trace->applied = applied;
    result = POWER_OK;
    goto done;
rollback:
    trace->applied = applied;
    while (applied) {
        uint32_t at = --applied;
        if (!s->io.restore(s->io.ctx, s->identity, program->state, &plan->actions[at],
                           &s->snapshots[at]) ||
            !s->io.verify_restored(s->io.ctx, s->identity, program->state, &plan->actions[at],
                                   &s->snapshots[at]))
            clean = 0;
        else
            ++restore_cnt;
    }
    trace->restored = restore_cnt;
    if (!clean || !live(s)) {
        s->quarantined = 1u;
        s->state_known = 0u;
        result = POWER_ROLLBACK_FAILED;
    }
done:
    s->io.unlock(s->io.ctx);
    s->busy = 0u;
    return fail(trace, result, trace->failure_idx);
}

int group_vote_close(group_vote_t* s)
{
    if (!s || s->busy || s->quarantined)
        return 0;
    if (!s->bound && !s->touched) {
        memset(s, 0, sizeof(*s));
        return 1;
    }
    if (!s->bound)
        return 0;
    if (!s->touched) {
        /* bind allocated no required client and issued no physical write. */
        memset(s, 0, sizeof(*s));
        return 1;
    }
    if (!live(s) || !s->state_known || s->state != 1u)
        return 0;
    if (!s->io.lock(s->io.ctx))
        return 0;
    int released = live(s) && s->io.verify_released(s->io.ctx, s->identity);
    if (released && s->config_captured) {
        uint32_t now = UINT32_MAX;
        released =
            s->io.read_clock_config(s->io.ctx, s->config_clock, &now) && now == s->config_baseline;
    }
    s->io.unlock(s->io.ctx);
    if (!released)
        return 0;
    memset(s, 0, sizeof(*s));
    return 1;
}
