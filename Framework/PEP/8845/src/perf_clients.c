#include "perf_clients.h"
#include <string.h>

static int supported_action(const power_action_t* a)
{
    return a && (a->kind == POWER_CLOCK || a->kind == POWER_ICB ||
                 a->kind == POWER_PMIC_REGULATOR || a->kind == POWER_FOOTSWITCH);
}

static int append(pofx_required_catalog_t* c, program_class_t kind, uint32_t comp, uint32_t set,
                  uint32_t state, const power_plan_t* plan)
{
    uint32_t i;
    if (!c || !plan || plan->action_cnt > POWER_MAX_ACTIONS ||
        c->cnt > POFX_REQUIRED_MAX_ACTIONS - plan->action_cnt)
        return 0;
    for (i = 0u; i < plan->action_cnt; ++i) {
        pofx_required_action_t* dst;
        if (!supported_action(&plan->actions[i]))
            return 0;
        dst = &c->actions[c->cnt++];
        dst->action = plan->actions[i];
        dst->program_class = (uint8_t)kind;
        dst->comp = comp;
        dst->set = set;
        dst->state = state;
    }
    return 1;
}

int po_fx_required_compile(pofx_required_catalog_t* c, const program_idx_t* idx, const void* blob,
                           size_t bytes, power_plan_t* plan, power_pstate_program_t* ps)
{
    uint32_t comp, set, state;
    if (!c)
        return 0;
    memset(c, 0, sizeof(*c));
    if (!idx || !idx->valid || !idx->comp_mask || !blob || !bytes || idx->desc_bytes != bytes ||
        !plan || !ps)
        return 0;
    for (comp = 0u; comp < 32u; ++comp) {
        if (!(idx->comp_mask & (UINT32_C(1) << comp)))
            continue;
        if (!(idx->f_state_mask[comp] & 1u))
            goto fail;
        c->comp_mask |= UINT32_C(1) << comp;
        c->set_masks[comp] = idx->p_state_set_mask[comp];
        c->f_state_masks[comp] = idx->f_state_mask[comp];
        for (state = 0u; state < 32u; ++state) {
            if (!(idx->f_state_mask[comp] & (UINT32_C(1) << state)))
                continue;
            if (program_idx_compile(idx, blob, bytes, PROGRAM_F_STATE, comp, 0u, state, plan,
                                    NULL) != POWER_OK)
                goto fail;
            {
                uint32_t i, seen = 0u;
                uint32_t kept = 0u;
                for (i = 0u; i < plan->action_cnt; ++i) {
                    const power_action_t* a = &plan->actions[i];
                    if (a->kind == POWER_PSTATE_ADJUST) {
                        const uint32_t selector = a->u.pstate_adjust.fields[0];
                        const uint32_t target = a->u.pstate_adjust.fields[1];
                        if (selector >= 32u || target >= 32u ||
                            !(idx->p_state_set_mask[comp] & (UINT32_C(1) << selector)) ||
                            !(idx->p_state_mask[comp][selector] & (UINT32_C(1) << target)) ||
                            (seen & (UINT32_C(1) << selector)))
                            goto fail;
                        seen |= UINT32_C(1) << selector;
                    } else {
                        if (!supported_action(a))
                            goto fail;
                        plan->actions[kept++] = *a;
                    }
                }
                /* a nested F-state must resolve every advertised set. */
                if (seen != idx->p_state_set_mask[comp])
                    goto fail;
                plan->action_cnt = kept;
                if (!append(c, PROGRAM_F_STATE, comp, 0u, state, plan))
                    goto fail;
            }
        }
        for (set = 0u; set < 32u; ++set) {
            if (!(idx->p_state_set_mask[comp] & (UINT32_C(1) << set)))
                continue;
            c->p_state_masks[comp][set] = idx->p_state_mask[comp][set];
            if (!c->p_state_masks[comp][set])
                goto fail;
            for (state = 0u; state < 32u; ++state) {
                if (!(idx->p_state_mask[comp][set] & (UINT32_C(1) << state)))
                    continue;
                if (program_idx_compile(idx, blob, bytes, PROGRAM_P_STATE, comp, set, state, NULL,
                                        ps) != POWER_OK ||
                    !append(c, PROGRAM_P_STATE, comp, set, state, &ps->resources))
                    goto fail;
            }
        }
    }
    if (!c->cnt)
        goto fail;
    c->bound = 1u;
    return 1;
fail:
    memset(c, 0, sizeof(*c));
    return 0;
}

int po_fx_required_open(pofx_required_owner_t* o, const pofx_required_catalog_t* c,
                        const pofx_required_provider_t* p, uint64_t epoch)
{
    uint32_t i;
    uint64_t owner_epoch = 0u;
    uint32_t outstanding = UINT32_MAX;
    if (!o || o->bound || o->live || o->quarantined || !c || !c->bound || !c->cnt ||
        c->cnt > POFX_REQUIRED_MAX_ACTIONS || !p || !p->owns || !p->create || !p->validate ||
        !p->is_released || !p->destroy || !p->read_client || !p->read_owner_epoch || !epoch ||
        !p->read_owner_epoch(p->ctx, &owner_epoch) || owner_epoch != epoch)
        return 0;
    o->catalog = c;
    o->provider = *p;
    o->epoch = epoch;
    /* validate the entire program inventory before opening a client. */
    for (i = 0u; i < c->cnt; ++i)
        if (!o->provider.owns(o->provider.ctx, &c->actions[i]))
            goto failed;
    for (i = 0u; i < c->cnt; ++i) {
        o->handles[i] = o->provider.create(o->provider.ctx, &c->actions[i]);
        if (!o->handles[i])
            goto failed;
        ++o->live;
        if (!o->provider.validate(o->provider.ctx, &c->actions[i], o->handles[i]) ||
            !o->provider.read_client(o->provider.ctx, &c->actions[i], o->handles[i],
                                     &outstanding) ||
            outstanding != 0u || !o->provider.read_owner_epoch(o->provider.ctx, &owner_epoch) ||
            owner_epoch != epoch)
            goto failed;
    }
    o->bound = 1u;
    return 1;
failed:
    /* a failed factory operation may have already acquired a client. Never return a success or
     * discard its lifetime without release proof. */
    if (o->live)
        o->quarantined = 1u;
    else
        memset(o, 0, sizeof(*o));
    return 0;
}

int po_fx_required_ready(const pofx_required_owner_t* o, uint64_t epoch)
{
    uint32_t i;
    uint64_t owner_epoch = 0u;
    uint32_t outstanding = UINT32_MAX;
    if (!o || !o->bound || o->quarantined || !epoch || epoch != o->epoch || !o->catalog ||
        !o->catalog->bound || o->live != o->catalog->cnt || !o->provider.validate ||
        !o->provider.read_client || !o->provider.read_owner_epoch ||
        !o->provider.read_owner_epoch(o->provider.ctx, &owner_epoch) || owner_epoch != epoch)
        return 0;
    for (i = 0u; i < o->catalog->cnt; ++i)
        if (!o->handles[i] ||
            !o->provider.validate(o->provider.ctx, &o->catalog->actions[i], o->handles[i]) ||
            !o->provider.read_client(o->provider.ctx, &o->catalog->actions[i], o->handles[i],
                                     &outstanding) ||
            outstanding > 1u)
            return 0;
    return 1;
}

int po_fx_required_close(pofx_required_owner_t* o, uint64_t epoch)
{
    uint32_t i;
    uint32_t outstanding = UINT32_MAX;
    uint64_t physical_epoch = 0u;
    int clean = 1;
    if (!o || !epoch || epoch != o->epoch || !o->catalog || !o->provider.is_released ||
        !o->provider.destroy || !o->provider.read_client || !o->provider.read_owner_epoch ||
        o->live > POFX_REQUIRED_MAX_ACTIONS)
        return 0;
    for (i = o->catalog->cnt; i > 0u; --i) {
        const uint32_t j = i - 1u;
        if (!o->handles[j])
            continue;
        if (!o->provider.read_owner_epoch(o->provider.ctx, &physical_epoch) ||
            physical_epoch != epoch ||
            !o->provider.read_client(o->provider.ctx, &o->catalog->actions[j], o->handles[j],
                                     &outstanding) ||
            outstanding != 0u ||
            !o->provider.is_released(o->provider.ctx, &o->catalog->actions[j], o->handles[j]) ||
            !o->provider.destroy(o->provider.ctx, &o->catalog->actions[j], o->handles[j])) {
            clean = 0;
            continue;
        }
        o->handles[j] = NULL;
        --o->live;
    }
    if (!clean || o->live) {
        o->quarantined = 1u;
        return 0;
    }
    memset(o, 0, sizeof(*o));
    return 1;
}
