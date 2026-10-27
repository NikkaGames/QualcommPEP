#include "pofx_perf.h"
#include <string.h>

static power_result_t denied(power_execution_t* trace, power_result_t result)
{
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        trace->failure_idx = UINT32_MAX;
        trace->result = result;
    }
    return result;
}

void po_fx_endpoint_retire(pofx_perf_endpoint_t* e)
{
    if (e)
        e->bound = 0u;
}

int po_fx_endpoint_catalog_ready(const pofx_perf_endpoint_t* e, uint64_t epoch, uint32_t comp)
{
    if (!e || !e->bound || !epoch || epoch != e->owner_epoch || !e->catalog || !e->owner ||
        !e->desc || !e->physical_d0 || !e->backend || !e->owner->bound || e->owner->quarantined ||
        e->owner->owner_epoch != epoch || !po_fx_catalog_validate(e->catalog, &e->owner->idx) ||
        comp >= e->catalog->components || !e->catalog->set_cnt[comp] ||
        e->physical_d0->quarantined || e->owner->comp[comp].quarantined || !e->backend->lock ||
        !e->backend->unlock || !e->backend->verify_current_state)
        return 0;
    return 1;
}

int po_fx_endpoint_ready(const pofx_perf_endpoint_t* e, uint64_t epoch, uint32_t comp)
{
    uint32_t s;
    if (!po_fx_endpoint_catalog_ready(e, epoch, comp) || !e->physical_d0->state_known ||
        e->physical_d0->current_d_state != 0u || !e->owner->comp[comp].state_known ||
        e->owner->comp[comp].quarantined || e->owner->comp[comp].current_d_state != 0u ||
        !e->backend->supports ||
        (!e->backend->execute_owned_program &&
         (!e->backend->snapshot || !e->backend->apply || !e->backend->verify ||
          !e->backend->restore || !e->backend->verify_restored)))
        return 0;
    for (s = 0u; s < e->catalog->set_cnt[comp]; ++s) {
        uint32_t selector, cnt;
        const power_dev_t* state;
        if (!po_fx_catalog_resolve_set(e->catalog, comp, s, &selector, &cnt) || !cnt)
            return 0;
        state = &e->owner->sets[comp][selector];
        if (!state->state_known || state->quarantined || state->current_d_state >= 32u ||
            !(e->owner->idx.p_state_mask[comp][selector] & (UINT32_C(1) << state->current_d_state)))
            return 0;
    }
    return 1;
}

int po_fx_endpoint_bind(pofx_perf_endpoint_t* e, const pofx_perf_catalog_t* catalog,
                        comp_state_owner_t* owner, const void* blob, size_t bytes,
                        const power_dev_t* d0, const power_backend_t* provider, uint64_t epoch,
                        comp_state_work_t* work)
{
    pofx_perf_endpoint_t candidate;
    uint32_t c, s;
    if (!work || !e || e->bound || !catalog || !catalog->bound || !owner || !blob || !bytes ||
        bytes != catalog->idx.desc_bytes || !d0 || !provider || !epoch)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.catalog = catalog;
    candidate.owner = owner;
    candidate.desc = blob;
    candidate.desc_bytes = bytes;
    candidate.physical_d0 = d0;
    candidate.backend = provider;
    candidate.owner_epoch = epoch;
    candidate.bound = 1u;
    for (c = 0u; c < catalog->components; ++c) {
        if (catalog->set_cnt[c]) {
            if (!po_fx_endpoint_ready(&candidate, epoch, c))
                return 0;
            for (s = 0u; s < catalog->set_cnt[c]; ++s) {
                uint32_t verified;
                if (!po_fx_endpoint_query_current(&candidate, epoch, c, s, work, &verified))
                    return 0;
            }
        }
    }
    *e = candidate;
    return 1;
}

power_result_t po_fx_endpoint_execute(pofx_perf_endpoint_t* e, uint64_t epoch, uint32_t comp,
                                      const uint32_t* sets, const uint32_t* states, uint32_t cnt,
                                      comp_state_work_t* work, power_execution_t* trace)
{
    uint32_t i, selectors[32], targets[32], used = 0u;
    power_result_t result;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!sets || !states || !work || !cnt || cnt > 32u || !po_fx_endpoint_ready(e, epoch, comp))
        return denied(trace, POWER_UNSUPPORTED);
    for (i = 0u; i < cnt; ++i) {
        if (!po_fx_catalog_resolve_state(e->catalog, comp, sets[i], states[i], &selectors[i],
                                         &targets[i]) ||
            (used & (UINT32_C(1) << selectors[i])))
            return denied(trace, POWER_INVALID_PROGRAM);
        used |= UINT32_C(1) << selectors[i];
    }
    result = comp_owner_execute_p_states(e->owner, e->desc, e->desc_bytes, comp, selectors, targets,
                                         cnt, e->physical_d0, e->backend, epoch, work, trace);
    if (result != POWER_OK)
        return result;
    for (i = 0u; i < cnt; ++i) {
        const power_dev_t* state = &e->owner->sets[comp][selectors[i]];
        if (!state->state_known || state->quarantined || state->current_d_state != targets[i]) {
            e->owner->quarantined = 1u;
            return denied(trace, POWER_STATE_DRIFT);
        }
    }
    return POWER_OK;
}

power_result_t po_fx_endpoint_execute_f_state(pofx_perf_endpoint_t* e, uint64_t epoch,
                                              uint32_t comp, uint32_t state,
                                              comp_state_work_t* work, power_execution_t* trace)
{
    power_result_t result;
    const power_dev_t* actual;
    if (!trace)
        return POWER_BAD_INPUT;
    if (!work || !e || !e->bound || !e->owner || !e->catalog || !e->catalog->bound || !e->desc ||
        !e->desc_bytes || !e->physical_d0 || !e->backend || !epoch || epoch != e->owner_epoch ||
        e->owner->owner_epoch != epoch || !e->owner->bound || e->owner->quarantined ||
        comp >= e->catalog->components || comp >= 32u || state >= 32u ||
        !(e->owner->idx.f_state_mask[comp] & (UINT32_C(1) << state)) ||
        !e->physical_d0->state_known || e->physical_d0->quarantined ||
        e->physical_d0->current_d_state != 0u ||
        !po_fx_catalog_validate(e->catalog, &e->owner->idx))
        return denied(trace, POWER_UNSUPPORTED);
    result = comp_owner_execute_f_state(e->owner, e->desc, e->desc_bytes, comp, state,
                                        e->physical_d0, e->backend, epoch, work, trace);
    if (result != POWER_OK)
        return result;
    actual = &e->owner->comp[comp];
    if (!actual->state_known || actual->quarantined || actual->current_d_state != state) {
        e->owner->quarantined = 1u;
        return denied(trace, POWER_STATE_DRIFT);
    }
    return POWER_OK;
}

int po_fx_endpoint_query_current(const pofx_perf_endpoint_t* e, uint64_t epoch, uint32_t comp,
                                 uint32_t set_ordinal, comp_state_work_t* work, uint32_t* out)
{
    uint32_t set, cnt, selector, ordinal;
    const power_dev_t* state;
    power_pstate_program_t* program;
    int ok = 0;
    if (!out || !work || !po_fx_endpoint_catalog_ready(e, epoch, comp) ||
        !po_fx_catalog_resolve_set(e->catalog, comp, set_ordinal, &set, &cnt))
        return 0;
    /* a mandatory current-state query can race an idle/D-state transition. read the committed
     * selector and physical program under the provider lock; answering this query neither
     * acquires d0 nor replays a res vote. */
    if (!e->backend->lock(e->backend->ctx))
        return 0;
    if (!po_fx_endpoint_catalog_ready(e, epoch, comp) || !e->physical_d0->state_known ||
        !e->owner->comp[comp].state_known)
        goto done;
    state = &e->owner->sets[comp][set];
    if (!state->state_known || state->quarantined)
        goto done;
    selector = state->current_d_state;
    if (!program_p_state_to_ordinal(&e->owner->idx, comp, set, selector, &ordinal))
        goto done;
    program = &work->p_states[0];
    if (program_idx_compile(&e->owner->idx, e->desc, e->desc_bytes, PROGRAM_P_STATE, comp, set,
                            selector, NULL, program) != POWER_OK ||
        !program->resources.action_cnt)
        goto done;
    ok = e->backend->verify_current_state(e->backend->ctx, &program->resources);
done:
    e->backend->unlock(e->backend->ctx);
    if (!ok)
        return 0;
    *out = ordinal;
    return 1;
}

int po_fx_producer_prepare(pofx_producer_t* p, const void* blob, size_t bytes, const char* path,
                           uint32_t cnt, const power_dev_t* d0, const power_backend_t* provider,
                           uint64_t epoch, runtime_desc_info_t* scratch, comp_state_work_t* work)
{
    program_idx_t idx;
    power_execution_t trace;
    uint32_t comp, set, selector, states, state;
    if (!p || p->retain || p->published || !blob || !bytes || !path || !cnt || cnt > 32u || !d0 ||
        !d0->state_known || d0->quarantined || d0->current_d_state != 0u || !provider || !epoch ||
        !scratch || !work || p->owner.bound || p->catalog.bound || p->endpoint.bound ||
        !program_idx_describe(blob, bytes, path, scratch, &idx))
        return 0;
    if (!po_fx_catalog_bind(&p->catalog, &idx, cnt) || !comp_owner_bind(&p->owner, &idx, epoch))
        goto no_votes;
    p->owner_epoch = epoch;
    p->desc = blob;
    p->desc_bytes = bytes;
    p->provider = provider;
    /* validate every initial program before issuing the first vote. */
    for (comp = 0u; comp < cnt; ++comp) {
        if (!(idx.f_state_mask[comp] & 1u) || !p->catalog.set_cnt[comp] ||
            program_idx_compile(&idx, blob, bytes, PROGRAM_F_STATE, comp, 0u, 0u, &work->f_state,
                                NULL) != POWER_OK)
            goto no_votes;
        for (set = 0u; set < p->catalog.set_cnt[comp]; ++set) {
            if (!po_fx_catalog_resolve_set(&p->catalog, comp, set, &selector, &states))
                goto no_votes;
            for (state = 0u; state < states; ++state) {
                uint32_t desc_state;
                if (!po_fx_catalog_resolve_state(&p->catalog, comp, set, state, NULL,
                                                 &desc_state) ||
                    program_idx_compile(&idx, blob, bytes, PROGRAM_P_STATE, comp, selector,
                                        desc_state, NULL, &work->p_states[0]) != POWER_OK)
                    goto no_votes;
            }
        }
    }
    for (comp = 0u; comp < cnt; ++comp) {
        memset(&trace, 0, sizeof(trace));
        /* failed f0 can leave a previous component or a partially dispatched res live. Do not drop
         * desc/provider references. */
        p->retain = 1u;
        if (comp_owner_execute_f_state(&p->owner, blob, bytes, comp, 0u, d0, provider, epoch, work,
                                       &trace) != POWER_OK)
            return 0;
    }
    if (!po_fx_endpoint_bind(&p->endpoint, &p->catalog, &p->owner, blob, bytes, d0, provider, epoch,
                             work))
        return 0;
    p->published = 1u;
    return 1;
no_votes:
    memset(p, 0, sizeof(*p));
    return 0;
}

void po_fx_producer_close(pofx_producer_t* p)
{
    if (!p)
        return;
    po_fx_endpoint_retire(&p->endpoint);
    p->published = 0u;
    /* retain is cleared only by the independently verified physical owner. */
}

/* validate the OFF programs that are still in effect. An f1 selector by itself proves no
 * withdrawal of nested P-state votes. The entire advertised set inventory must be present
 * in f1 and still match the retained owner cache. */
static int verify_f1_release(const pofx_producer_t* p, uint32_t comp, comp_state_work_t* work)
{
    const program_idx_t* idx = &p->owner.idx;
    power_pstate_selection_t selection;
    uint32_t i, n = 0u;
    const uint32_t required = idx->p_state_set_mask[comp];
    if (!(idx->f_state_mask[comp] & 2u) ||
        program_idx_compile(idx, p->desc, p->desc_bytes, PROGRAM_F_STATE, comp, 0u, 1u,
                            &work->f_state, NULL) != POWER_OK)
        return 0;
    for (i = 0u; i < work->f_state.action_cnt; ++i) {
        const power_action_t* a = &work->f_state.actions[i];
        uint32_t set, state;
        if (a->kind != POWER_PSTATE_ADJUST)
            continue;
        set = a->u.pstate_adjust.fields[0];
        state = a->u.pstate_adjust.fields[1];
        if (set >= 32u || state >= 32u || n >= 32u || !(required & (UINT32_C(1) << set)) ||
            !(idx->p_state_mask[comp][set] & (UINT32_C(1) << state)) ||
            !p->owner.sets[comp][set].state_known || p->owner.sets[comp][set].quarantined ||
            p->owner.sets[comp][set].current_d_state != state ||
            program_idx_compile(idx, p->desc, p->desc_bytes, PROGRAM_P_STATE, comp, set, state,
                                NULL, &work->p_states[n]) != POWER_OK)
            return 0;
        work->program_refs[n] = &work->p_states[n];
        ++n;
    }
    if (required) {
        if (power_expand_comp_f_state(&work->f_state, comp, work->program_refs, n, 32u,
                                      &work->expanded, &selection) != POWER_OK ||
            selection.used_sets != required || !work->expanded.action_cnt)
            return 0;
    } else {
        work->expanded = work->f_state;
        if (!work->expanded.action_cnt)
            return 1;
    }
    if (!p->provider->lock(p->provider->ctx))
        return 0;
    i = p->provider->verify_current_state(p->provider->ctx, &work->expanded) &&
        !p->owner.quarantined && p->owner.owner_epoch == p->owner_epoch;
    p->provider->unlock(p->provider->ctx);
    return i != 0u;
}

int po_fx_producer_release(pofx_producer_t* p, const power_dev_t* d3, const void* d3blob,
                           size_t d3bytes, runtime_desc_info_t* desc_work,
                           power_sequence_t* sequence_work, comp_state_work_t* work, uint64_t epoch)
{
    program_idx_t d3index;
    uint32_t comp;
    int verified;
    if (!p || !p->retain || p->published || p->endpoint.bound || !p->desc || !p->desc_bytes ||
        !p->provider || !p->owner.bound || p->owner.quarantined || !epoch ||
        epoch != p->owner_epoch || p->owner.owner_epoch != epoch || !d3 || !d3->state_known ||
        d3->quarantined || d3->current_d_state != 3u || !d3blob || !d3bytes || !desc_work ||
        !sequence_work || !work || !p->provider->lock || !p->provider->unlock ||
        !p->provider->verify_current_state)
        return 0;
    for (comp = 0u; comp < 32u; ++comp) {
        const power_dev_t* state;
        if (!(p->owner.comp_mask & (UINT32_C(1) << comp)))
            continue;
        state = &p->owner.comp[comp];
        /* declared f1 must have been physically committed before d3. */
        if (!state->state_known || state->quarantined || state->current_d_state != 1u ||
            !verify_f1_release(p, comp, work))
            return 0;
    }
    if (!program_idx_describe(d3blob, d3bytes, p->owner.idx.path, desc_work, &d3index) ||
        !d3index.has_d3 || !(d3index.d_state_mask & 8u) ||
        program_idx_compile(&d3index, d3blob, d3bytes, PROGRAM_D_STATE, 0u, 0u, 3u, sequence_work,
                            NULL) != POWER_OK ||
        !sequence_work->action_cnt || sequence_work->target_d_state != 3u ||
        strcmp(sequence_work->dev_path, p->owner.idx.path))
        return 0;
    /* d3 can carry nested PSTATE_ADJUST resources. Expand only when exactly one component owns
     * every referenced set, with verified final selectors. The provider must never receive raw
     * nested actions. */
    {
        uint32_t refs = 0u;
        const power_sequence_t* physical_sequence = sequence_work;
        for (comp = 0u; comp < sequence_work->action_cnt; ++comp) {
            const power_action_t* action = &sequence_work->actions[comp];
            uint32_t set, state;
            if (action->kind != POWER_PSTATE_ADJUST)
                continue;
            set = action->u.pstate_adjust.fields[0];
            state = action->u.pstate_adjust.fields[1];
            if (p->owner.comp_mask != 1u || refs >= 32u || set >= 32u || state >= 32u ||
                !(p->owner.idx.p_state_set_mask[0] & (UINT32_C(1) << set)) ||
                !(p->owner.idx.p_state_mask[0][set] & (UINT32_C(1) << state)) ||
                !p->owner.sets[0][set].state_known || p->owner.sets[0][set].quarantined ||
                p->owner.sets[0][set].current_d_state != state ||
                program_idx_compile(&p->owner.idx, p->desc, p->desc_bytes, PROGRAM_P_STATE, 0u, set,
                                    state, NULL, &work->p_states[refs]) != POWER_OK)
                return 0;
            work->program_refs[refs] = &work->p_states[refs];
            ++refs;
        }
        if (refs) {
            power_pstate_selection_t selection;
            if (power_expand_comp_f_state(sequence_work, 0u, work->program_refs, refs, 32u,
                                          &work->expanded, &selection) != POWER_OK ||
                !work->expanded.action_cnt)
                return 0;
            physical_sequence = &work->expanded;
        }
        if (!p->provider->lock(p->provider->ctx))
            return 0;
        verified = p->owner.owner_epoch == epoch && !p->owner.quarantined &&
                   p->provider->verify_current_state(p->provider->ctx, physical_sequence);
    }
    p->provider->unlock(p->provider->ctx);
    if (!verified)
        return 0;
    memset(p, 0, sizeof(*p));
    return 1;
}
