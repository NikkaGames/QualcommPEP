#include "state_txn.h"
#include <string.h>
#include <limits.h>

typedef struct txn_param {
    uint16_t type, length;
    const uint8_t* data;
} txn_param_t;

static uint16_t read16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int next_param(const uint8_t* bytes, uint32_t length, uint32_t* pos, txn_param_t* value)
{
    uint32_t end, pad;
    if (!bytes || !pos || !value || *pos > length || length - *pos < 4u)
        return 0;
    value->type = read16(bytes + *pos);
    value->length = read16(bytes + *pos + 2u);
    end = *pos + 4u;
    pad = value->length < 4u ? 4u : (uint32_t)value->length;
    if (pad > length - end)
        return 0;
    value->data = bytes + end;
    *pos = end + pad;
    return 1;
}

static int next_number(const uint8_t* b, uint32_t len, uint32_t* pos, uint64_t* out)
{
    txn_param_t p;
    uint32_t i;
    uint64_t n = 0;
    if (!next_param(b, len, pos, &p) || p.type != 0 ||
        (p.length != 2u && p.length != 4u && p.length != 8u))
        return 0;
    for (i = 0; i < p.length; ++i)
        n |= (uint64_t)p.data[i] << (8u * i);
    *out = n;
    return 1;
}

static int next_string(const uint8_t* b, uint32_t len, uint32_t* pos, char* name, size_t name_cap)
{
    txn_param_t p;
    if (!next_param(b, len, pos, &p) || p.type != 1 || p.length < 2u ||
        (size_t)p.length > name_cap || p.data[p.length - 1u] != 0 ||
        memchr(p.data, 0, p.length - 1u) != NULL)
        return 0;
    memcpy(name, p.data, p.length);
    return 1;
}

typedef struct plan_builder {
    power_plan_t* plan;
    int overflow;
    int comp_p_state;
} plan_builder_t;

static int compile_action(const aeob_action_t* a, void* ctx)
{
    plan_builder_t* build = (plan_builder_t*)ctx;
    power_action_t* out;
    uint64_t n;
    uint32_t pos = 0, i, want;
    if (build->plan->action_cnt >= POWER_MAX_ACTIONS) {
        build->overflow = 1;
        return 0;
    }
    out = &build->plan->actions[build->plan->action_cnt];
    memset(out, 0, sizeof(*out));
    if (strcmp(a->kind, "CLOCK") == 0) {
        out->kind = POWER_CLOCK;
        if (!next_string(a->arguments, a->argument_bytes, &pos, out->u.clock.name,
                         sizeof(out->u.clock.name)) ||
            !next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
            return 0;
        out->u.clock.vote = (uint32_t)n;
        while (pos < a->argument_bytes) {
            if (out->u.clock.extra_cnt >= 3u ||
                !next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
                return 0;
            out->u.clock.extra[out->u.clock.extra_cnt++] = (uint32_t)n;
        }

        /* preserve bytes; a provider must independently establish each opcode and preflight MUST
         * reject unsupported (currently #9). Parsing is never an authorization for hardware
         * execution. */

        if (!(out->u.clock.extra_cnt == 0u &&
              (out->u.clock.vote == 1u || out->u.clock.vote == 2u)) &&
            !(out->u.clock.extra_cnt == 2u && out->u.clock.vote == 8u) &&
            !(build->comp_p_state && out->u.clock.extra_cnt == 2u && out->u.clock.vote == 3u) &&
            !(build->comp_p_state && out->u.clock.extra_cnt == 1u && out->u.clock.vote == 9u))
            return 0;
    } else if (strcmp(a->kind, "BUSARB") == 0) {
        out->kind = POWER_ICB;
        if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
            return 0;
        out->u.icb.client = (uint32_t)n;
        if (!next_string(a->arguments, a->argument_bytes, &pos, out->u.icb.master,
                         sizeof(out->u.icb.master)) ||
            !next_string(a->arguments, a->argument_bytes, &pos, out->u.icb.slave,
                         sizeof(out->u.icb.slave)) ||
            !next_number(a->arguments, a->argument_bytes, &pos, &out->u.icb.average) ||
            !next_number(a->arguments, a->argument_bytes, &pos, &out->u.icb.peak) ||
            pos != a->argument_bytes)
            return 0;
    } else if (strcmp(a->kind, "TLMMGPIO") == 0) {
        out->kind = POWER_TLMM_GPIO;
        for (i = 0; i < 6u; ++i) {
            if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
                return 0;
            out->u.gpio.fields[i] = (uint32_t)n;
        }
        if (pos != a->argument_bytes)
            return 0;
    } else if (strcmp(a->kind, "PMICGPIO") == 0) {
        char method[128];
        out->kind = POWER_PMIC_GPIO;
        if (!next_string(a->arguments, a->argument_bytes, &pos, method, sizeof(method)) ||
            strcmp(method, "IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT") != 0)
            return 0;
        out->u.pmic_gpio.ioctl = 0x80028FA8u;
        for (i = 0; i < 7u; ++i) {
            if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
                return 0;
            out->u.pmic_gpio.fields[i] = (uint32_t)n;
        }
        if (pos != a->argument_bytes)
            return 0;
    } else if (strcmp(a->kind, "PMICVREGVOTE") == 0) {
        out->kind = POWER_PMIC_REGULATOR;
        if (!next_string(a->arguments, a->argument_bytes, &pos, out->u.pmic_regulator.name,
                         sizeof(out->u.pmic_regulator.name)))
            return 0;
        /* the first DWORD is the vote opcode and it fixes the arity: an LDO (1) or SMPS (2) vote
         * carries [kind, uV, enable, mode, headroom], a PMIC clock-buf vote only [6, enable]. A
         * fixed count of five silently refused every clock-buf vote. */

        if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
            return 0;
        out->u.pmic_regulator.fields[0] = (uint32_t)n;
        if (n == AEOB_VREG_OP_LDO || n == AEOB_VREG_OP_SMPS)
            want = 5u;
        else if (n == AEOB_VREG_OP_CLOCK)
            want = 2u;
        else
            return 0;
        for (i = 1; i < want; ++i) {
            if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
                return 0;
            out->u.pmic_regulator.fields[i] = (uint32_t)n;
        }
        for (; i < 5u; ++i)
            out->u.pmic_regulator.fields[i] = 0u;
        if (pos != a->argument_bytes)
            return 0;
    } else if (strcmp(a->kind, "FOOTSWITCH") == 0) {
        if (!build->comp_p_state)
            return 0;
        out->kind = POWER_FOOTSWITCH;
        if (!next_string(a->arguments, a->argument_bytes, &pos, out->u.footswitch.name,
                         sizeof(out->u.footswitch.name)) ||
            !next_number(a->arguments, a->argument_bytes, &pos, &n) ||
            (n != 1u && n != 2u && n != 3u && n != 4u) || pos != a->argument_bytes)
            return 0;
        out->u.footswitch.action = (uint32_t)n;
    } else if (strcmp(a->kind, "PSTATE_ADJUST") == 0) {
        out->kind = POWER_PSTATE_ADJUST;
        for (i = 0; i < 2u; ++i) {
            if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > UINT32_MAX)
                return 0;
            out->u.pstate_adjust.fields[i] = (uint32_t)n;
        }
        if (pos != a->argument_bytes)
            return 0;
    } else if (strcmp(a->kind, "DELAY") == 0) {
        out->kind = POWER_DELAY;
        if (!next_number(a->arguments, a->argument_bytes, &pos, &n) || n > 5000u ||
            pos != a->argument_bytes)
            return 0;
        out->u.delay.ms = (uint32_t)n;
    } else
        return 0;
    build->plan->action_cnt++;
    return 1;
}

power_result_t power_compile_aeo_b(const void* buf, size_t length, const char* path, uint32_t state,
                                   power_plan_t* plan)
{
    aeob_validation_t validation;
    aeob_status_t status;
    plan_builder_t builder;
    uint32_t visits = 0;
    size_t path_len;
    if (!plan || !buf || !path || state > 3u)
        return POWER_BAD_INPUT;
    memset(plan, 0, sizeof(*plan));
    for (path_len = 0; path_len < sizeof(plan->dev_path) && path[path_len]; ++path_len) {
    }
    if (path_len < 5u || path_len >= sizeof(plan->dev_path))
        return POWER_BAD_INPUT;
    status = aeo_b_validate_molokai_dev_d_state(buf, length, path, state, &validation);
    if (status != AEO_B_OK)
        return POWER_INVALID_PROGRAM;
    memcpy(plan->dev_path, path, path_len + 1u);
    plan->target_d_state = state;
    builder.plan = plan;
    builder.overflow = 0;
    builder.comp_p_state = 0;
    status = aeo_b_visit_dev_d_state(buf, length, path, state, compile_action, &builder, &visits);
    if (status != AEO_B_OK || visits != plan->action_cnt) {
        /* a failed compile must not expose a partial plan. */
        memset(plan, 0, sizeof(*plan));
        return builder.overflow ? POWER_TOO_MANY_ACTIONS : POWER_INVALID_PROGRAM;
    }
    /* empty plans can only reverify a committed nonempty plan under its provider lease. They cannot
     * establish initial power or final withdrawal. */

    if (plan->action_cnt == 0u && (state == 0u || state == 3u)) {
        memset(plan, 0, sizeof(*plan));
        return POWER_INVALID_PROGRAM;
    }
    /* executing arbitrarily long ACPI delays on the PoFx worker could deadlock unrelated
     * device power. */

    uint32_t accumulated_delay = 0;
    for (uint32_t i = 0; i < plan->action_cnt; ++i) {
        if (plan->actions[i].kind != POWER_DELAY)
            continue;
        uint32_t ms = plan->actions[i].u.delay.ms;
        if (ms > 5000u || accumulated_delay > 10000u - ms) {
            memset(plan, 0, sizeof(*plan));
            return POWER_INVALID_PROGRAM;
        }
        accumulated_delay += ms;
    }
    return POWER_OK;
}

power_result_t power_compile_aeo_bf_state(const void* buf, size_t length, const char* path,
                                          uint32_t comp, uint32_t fstate, power_plan_t* plan)
{
    aeob_validation_t validation;
    aeob_status_t status;
    plan_builder_t builder;
    uint32_t visits = 0, accumulated_delay = 0, i;
    size_t path_len;
    if (!buf || !path || !plan || comp >= 32u || fstate >= 32u)
        return POWER_BAD_INPUT;
    memset(plan, 0, sizeof(*plan));
    for (path_len = 0; path_len < sizeof(plan->dev_path) && path[path_len]; ++path_len) {
    }
    if (path_len < 5u || path_len >= sizeof(plan->dev_path) || path[0] != '\\')
        return POWER_BAD_INPUT;
    /* never compile unreviewed bytes solely because the parser finds a matching label. */

    status = aeo_b_validate_molokai_comp_f_state(buf, length, path, comp, fstate, &validation);
    if (status != AEO_B_OK || validation.failure != AEO_B_RES_NONE)
        return POWER_INVALID_PROGRAM;
    memcpy(plan->dev_path, path, path_len + 1u);
    plan->target_d_state = fstate; /* f-state selector, never Dx execution. */
    builder.plan = plan;
    builder.overflow = 0;
    builder.comp_p_state = 0;
    status = aeo_b_visit_comp_f_state(buf, length, path, comp, fstate, compile_action, &builder,
                                      &visits);
    if (status != AEO_B_OK || visits != plan->action_cnt) {
        memset(plan, 0, sizeof(*plan));
        return builder.overflow ? POWER_TOO_MANY_ACTIONS : POWER_INVALID_PROGRAM;
    }
    for (i = 0; i < plan->action_cnt; ++i) {
        uint32_t ms;
        if (plan->actions[i].kind != POWER_DELAY)
            continue;
        ms = plan->actions[i].u.delay.ms;
        if (ms > 5000u || accumulated_delay > 10000u - ms) {
            memset(plan, 0, sizeof(*plan));
            return POWER_INVALID_PROGRAM;
        }
        accumulated_delay += ms;
    }
    /* only validated empty programs are accepted as descriptors, NOT as verified hardware
     * transitions. */

    return POWER_OK;
}

power_result_t power_compile_aeo_bp_state(const void* buf, size_t length, const char* path,
                                          uint32_t comp, uint32_t set, uint32_t state,
                                          power_pstate_program_t* program)
{
    aeob_validation_t validation;
    aeob_status_t status;
    plan_builder_t builder;
    power_plan_t* plan;
    uint32_t visits = 0, delay = 0, i;
    size_t path_len;
    if (!program || !buf || !path || comp >= 32u || set >= 32u || state >= 32u)
        return POWER_BAD_INPUT;
    memset(program, 0, sizeof(*program));
    plan = &program->resources;
    for (path_len = 0; path_len < sizeof(plan->dev_path) && path[path_len]; ++path_len) {
    }
    if (path_len < 5u || path_len >= sizeof(plan->dev_path) || path[0] != '\\')
        return POWER_BAD_INPUT;
    status = aeo_b_validate_molokai_comp_p_state(buf, length, path, comp, set, state, &validation);
    if (status != AEO_B_OK || validation.failure != AEO_B_RES_NONE)
        return POWER_INVALID_PROGRAM;
    memcpy(plan->dev_path, path, path_len + 1u);
    plan->target_d_state = state;
    builder.plan = plan;
    builder.overflow = 0;
    builder.comp_p_state = 1;
    status = aeo_b_visit_comp_p_state(buf, length, path, comp, set, state, compile_action, &builder,
                                      &visits);
    if (status != AEO_B_OK || visits != plan->action_cnt || !visits) {
        memset(program, 0, sizeof(*program));
        return builder.overflow ? POWER_TOO_MANY_ACTIONS : POWER_INVALID_PROGRAM;
    }
    for (i = 0; i < plan->action_cnt; ++i) {
        uint32_t ms;
        if (plan->actions[i].kind != POWER_DELAY)
            continue;
        ms = plan->actions[i].u.delay.ms;
        if (ms > 5000u || delay > 10000u - ms) {
            memset(program, 0, sizeof(*program));
            return POWER_INVALID_PROGRAM;
        }
        delay += ms;
    }
    program->comp = comp;
    program->set = set;
    program->state = state;
    return POWER_OK;
}

static power_result_t rollback(const power_plan_t* p, power_dev_t* dev, const power_backend_t* be,
                               const power_snapshot_t* snapshots, uint32_t started,
                               power_execution_t* trace, power_result_t original_failure)
{
    uint32_t i;
    int failed = 0;

    for (i = started; i > 0u; --i) {
        const power_action_t* a = &p->actions[i - 1u];
        if (a->kind == POWER_DELAY)
            continue;
        if (!be->restore(be->ctx, a, &snapshots[i - 1u]) ||
            !be->verify_restored(be->ctx, a, &snapshots[i - 1u]))
            failed = 1;
        else
            ++trace->restored;
    }
    if (failed) {
        dev->state_known = 0u;
        dev->quarantined = 1u;
        return POWER_ROLLBACK_FAILED;
    }
    return original_failure;
}

static power_result_t power_execute_mode(const power_plan_t* plan, power_dev_t* dev,
                                         const power_backend_t* be, power_execution_t* trace,
                                         power_scratch_t* scratch, int is_comp, int force_actions)
{
    power_result_t result = POWER_BAD_INPUT;
    uint32_t i, started = 0;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!plan || !dev || !scratch || !be || !be->lock || !be->unlock ||
        (!be->execute_owned_program &&
         (!be->snapshot || !be->apply || !be->verify || !be->restore || !be->verify_restored)) ||
        !be->supports || !be->verify_current_state ||
        (plan->action_cnt == 0u && !is_comp &&
         (plan->target_d_state == 0u || plan->target_d_state == 3u)) ||
        plan->action_cnt > POWER_MAX_ACTIONS ||
        (is_comp ? plan->target_d_state >= 32u : (plan->target_d_state > 3u))) {
        trace->result = POWER_BAD_INPUT;
        return trace->result;
    }
    /* caller-owned scratch prevents 4-KiB automatic allocations on the limited Windows kernel
     * stack. Must remain valid through unlock. */

    memset(scratch, 0, sizeof(*scratch));
    if (!be->lock(be->ctx)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    if (!force_actions && dev->quarantined) {
        result = POWER_QUARANTINED;
        goto done;
    }
    /* it needs a previously *verified* D-state and the native verify_current_state callback
     * must prove that last nonempty plan is still held. */

    if (!is_comp && plan->action_cnt == 0u && !dev->state_known) {
        result = POWER_NO_KNOWN_STATE;
        goto done;
    }
    /* first D0 may start unknown: snapshot actual provider state before each write and
     * restore attempted writes on failure. Unknown -> D3 could drop a boot-critical vote. */

    if (!force_actions && !dev->state_known) {
        if (plan->target_d_state != 0u) {
            result = POWER_NO_KNOWN_STATE;
            goto done;
        }
    } else if (!force_actions && ((!is_comp && dev->current_d_state > 3u) ||
                                  (is_comp && dev->current_d_state >= 32u))) {
        result = POWER_NO_KNOWN_STATE;
        goto done;
    }
    /* the missing Molokai AOP/PPP/CLOCKS/ICB backends must never look like a successful
     * transaction that programmed only an initial prefix. */

    for (i = 0; i < plan->action_cnt; ++i) {
        if (!be->supports(be->ctx, &plan->actions[i])) {
            trace->failure_idx = i;
            result = POWER_UNSUPPORTED;
            goto done;
        }
    }
    /* never claim a cached D-state is still true without checking the provider-owned logical
     * votes and current ownership. */

    if (!force_actions && dev->state_known && dev->current_d_state == plan->target_d_state) {
        if (!be->verify_current_state) {
            result = POWER_UNSUPPORTED;
            goto done;
        }
        if (!be->verify_current_state(be->ctx, plan)) {
            dev->state_known = 0u;
            dev->quarantined = 1u;
            result = POWER_STATE_DRIFT;
            goto done;
        }
        trace->failure_idx = UINT32_MAX;
        result = POWER_OK;
        goto done;
    }
    /* native F/P grouped resources cannot be programmed action-by-action: CLOCK
     * enable/config/rate share one client, and a multi-set plan must preserve old votes until
     * every incoming state is physically verified. */
    if (be->execute_owned_program) {
        result = be->execute_owned_program(be->ctx, plan, trace);
        if (result == POWER_OK && !be->verify_current_state(be->ctx, plan)) {
            result = POWER_FINAL_STATE_FAILED;
            dev->state_known = 0u;
            dev->quarantined = 1u;
        } else if (result == POWER_OK) {
            if (!force_actions) {
                dev->current_d_state = plan->target_d_state;
                dev->state_known = 1u;
            }
            trace->failure_idx = UINT32_MAX;
        } else if (result == POWER_ROLLBACK_FAILED) {
            dev->state_known = 0u;
            dev->quarantined = 1u;
        }
        goto done;
    }
    for (i = 0; i < plan->action_cnt; ++i) {
        const power_action_t* a = &plan->actions[i];
        trace->failure_idx = i;
        if (a->kind == POWER_DELAY) {
            /* preserve execution order but do not treat elapsed time as a hardware vote with a
             * restorable prior state. */

            if (!be->apply(be->ctx, a)) {
                result =
                    rollback(plan, dev, be, scratch->snapshots, started, trace, POWER_APPLY_FAILED);
                goto done;
            }
            ++trace->applied;
            continue;
        }
        if (!be->snapshot(be->ctx, a, &scratch->snapshots[i])) {
            result =
                rollback(plan, dev, be, scratch->snapshots, started, trace, POWER_SNAPSHOT_FAILED);
            goto done;
        }

        started = i + 1u;
        if (!be->apply(be->ctx, a)) {
            result =
                rollback(plan, dev, be, scratch->snapshots, started, trace, POWER_APPLY_FAILED);
            goto done;
        }
        ++trace->applied;
        if (!be->verify(be->ctx, a)) {
            result =
                rollback(plan, dev, be, scratch->snapshots, started, trace, POWER_VERIFY_FAILED);
            goto done;
        }
    }
    /* check the *final* logical res/client votes while retaining the transaction lock and
     * before publishing a new D-state. Verification must be read-only and performed by the
     * authoritative res owner, not a cached TCS ACK. */

    if (!be->verify_current_state(be->ctx, plan)) {
        trace->failure_idx = plan->action_cnt;
        result =
            rollback(plan, dev, be, scratch->snapshots, started, trace, POWER_FINAL_STATE_FAILED);
        goto done;
    }
    if (!force_actions) {
        dev->current_d_state = plan->target_d_state;
        dev->state_known = 1u;
    }
    result = POWER_OK;
    trace->failure_idx = UINT32_MAX;
done:
    be->unlock(be->ctx);
    trace->result = result;
    return result;
}

power_result_t power_execute(const power_plan_t* plan, power_dev_t* dev,
                             const power_backend_t* backend, power_execution_t* trace,
                             power_scratch_t* scratch)
{
    return power_execute_mode(plan, dev, backend, trace, scratch, 0, 0);
}

power_result_t power_execute_f_state(const power_plan_t* plan, power_dev_t* comp,
                                     const power_backend_t* backend, power_execution_t* trace,
                                     power_scratch_t* scratch)
{
    return power_execute_mode(plan, comp, backend, trace, scratch, 1, 0);
}

/* batch callers verify D0/F0 and every constituent P-state. One provider lock covers the
 * transaction; no batch state is cached. */

power_result_t power_execute_action_batch(const power_plan_t* actions,
                                          const power_backend_t* backend, power_execution_t* trace,
                                          power_scratch_t* scratch)
{
    power_dev_t transient = {0};
    if (!trace)
        return POWER_BAD_INPUT;
    if (!actions || actions->target_d_state != 0u || !actions->action_cnt ||
        actions->action_cnt > POWER_MAX_ACTIONS || actions->dev_path[0] != '\\') {
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        return (trace->result = POWER_BAD_INPUT);
    }
    /* transient is solely a sticky rollback-failure detector. It never represents a PoFx state
     * and cannot be published as one. */

    return power_execute_mode(actions, &transient, backend, trace, scratch, 1, 1);
}

power_result_t power_execute_clock_rate(const power_plan_t* actions, const power_backend_t* backend,
                                        power_execution_t* trace, power_scratch_t* scratch)
{
    power_dev_t transient = {0};
    const power_action_t* a;
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!actions || actions->target_d_state > 3u || actions->action_cnt != 1u ||
        actions->dev_path[0] != '\\' || !memchr(actions->dev_path, 0, sizeof(actions->dev_path)))
        return (trace->result = POWER_BAD_INPUT);
    a = &actions->actions[0];
    if (a->kind != POWER_CLOCK || a->u.clock.vote != 3u || a->u.clock.extra_cnt != 2u ||
        !a->u.clock.extra[0] || a->u.clock.extra[1] != 4u || !a->u.clock.name[0] ||
        !memchr(a->u.clock.name, 0, sizeof(a->u.clock.name)))
        return (trace->result = POWER_BAD_INPUT);
    return power_execute_mode(actions, &transient, backend, trace, scratch, 1, 1);
}

power_result_t power_execute_p_state(const power_pstate_program_t* program, const power_dev_t* dev,
                                     const power_dev_t* comp, power_dev_t* per_set,
                                     const power_backend_t* backend, power_execution_t* trace,
                                     power_scratch_t* scratch)
{
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    trace->failure_idx = UINT32_MAX;
    if (!program || !dev || !comp || !per_set || !backend || !scratch || program->comp >= 32u ||
        program->set >= 32u || program->state >= 32u ||
        program->resources.target_d_state != program->state ||
        program->resources.action_cnt == 0u || program->resources.action_cnt > POWER_MAX_ACTIONS ||
        program->resources.dev_path[0] != '\\')
        return (trace->result = POWER_BAD_INPUT);
    /* oem: cannot change P-state of a component in f1 or of a device in Dx. */

    if (!dev->state_known || dev->quarantined || dev->current_d_state != 0u || !comp->state_known ||
        comp->quarantined || comp->current_d_state != 0u)
        return (trace->result = POWER_NO_KNOWN_STATE);
    /* per_set state is private to this component/set; its initialization and owner-generation
     * are controlled by the caller's device lifecycle. */

    return power_execute_mode(&program->resources, per_set, backend, trace, scratch, 1, 0);
}
