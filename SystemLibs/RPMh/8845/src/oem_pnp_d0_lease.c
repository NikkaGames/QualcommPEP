#include "oem_pnp_d0_lease.h"
#include <string.h>

/* verify fields, not structure padding, and never accept a guessed INTID, vector, or PnP
 * epoch as the independent owner epoch. */

static int same(const oem_d0_proof_t* a, const oem_d0_proof_t* b)
{
    return a->pnp_generation == b->pnp_generation && a->owner_epoch == b->owner_epoch &&
           a->nonce == b->nonce && a->wdf_interrupt == b->wdf_interrupt &&
           a->driver_id == b->driver_id && a->gic_intid == b->gic_intid &&
           a->win_vector == b->win_vector && a->win_level == b->win_level &&
           a->res_idx == b->res_idx && a->rights == b->rights && a->exclusive == b->exclusive &&
           a->d0_active == b->d0_active;
}

static int fits(const oem_pnp_d0_lease_t* l, const oem_d0_proof_t* p, void* irq, uint64_t epoch,
                uint32_t needed)
{
    const rpmh_oem_pnp_match_t* m = &l->match;
    return irq && epoch && p->pnp_generation == l->pnp_generation && p->owner_epoch == epoch &&
           p->nonce != 0 && p->wdf_interrupt == irq && p->driver_id == m->driver_id &&
           p->gic_intid == m->gic_intid && p->win_vector == m->win_vector &&
           p->win_level == m->win_level && p->res_idx == m->res_idx && p->exclusive == 1 &&
           p->d0_active == 1 && (p->rights & ~D0_RIGHT_ALL) == 0 && (p->rights & needed) == needed;
}

static int read_twice(oem_pnp_d0_lease_t* l, void* irq, uint64_t epoch, uint32_t needed,
                      oem_d0_proof_t* out)
{
    oem_d0_proof_t a = {0}, b = {0};
    if (!l->provider.read || l->provider.read(l->provider.ctx, &a) != 1 ||
        l->provider.read(l->provider.ctx, &b) != 1 || !same(&a, &b) ||
        !fits(l, &a, irq, epoch, needed))
        return 0;
    *out = a;
    return 1;
}

int oem_d0_lease_prepare(oem_pnp_d0_lease_t* l, const rpmh_oem_pnp_observation_t* obs,
                         uint64_t generation)
{
    rpmh_oem_pnp_plan_t p = {0};
    const rpmh_oem_pnp_match_t* m;
    if (!l || (l->phase != D0_LEASE_COLD && l->phase != D0_LEASE_CLOSED) ||
        !rpmh_oem_pnp_observation_read(obs, generation, &p) || p.cnt != 1)
        return 0;
    m = &p.matches[0];
    if (m->driver_id != RPMH_OEM_8480_DRIVER_ID || m->gic_intid != RPMH_OEM_8480_INTID ||
        m->owner_epoch != generation || !m->win_vector || !m->win_level || (m->raw_flags & 1u) ||
        (m->translated_flags & 1u))
        return 0;
    memset(&l->provider, 0, sizeof(l->provider));
    memset(&l->proof, 0, sizeof(l->proof));
    l->pnp_generation = generation;
    l->match = *m;
    l->phase = D0_LEASE_PREPARED;
    return 1;
}

int oem_d0_lease_enter(oem_pnp_d0_lease_t* l, const oem_d0_probe_t* probe, void* irq,
                       uint64_t epoch)
{
    oem_d0_proof_t p = {0};
    if (!l || l->phase != D0_LEASE_PREPARED || !probe || !probe->read || !irq || !epoch)
        return 0;
    l->provider = *probe;
    if (!read_twice(l, irq, epoch, D0_RIGHT_ALL, &p)) {
        l->phase = D0_LEASE_FAULT;
        return 0;
    }
    l->proof = p;
    l->phase = D0_LEASE_ACTIVE;
    return 1;
}

int oem_d0_lease_can_use(oem_pnp_d0_lease_t* l, void* irq, uint64_t epoch, uint32_t rights)
{
    oem_d0_proof_t p = {0};
    if (!l || l->phase != D0_LEASE_ACTIVE || !rights || (rights & ~D0_RIGHT_ALL) ||
        irq != l->proof.wdf_interrupt || epoch != l->proof.owner_epoch)
        return 0;
    if (!read_twice(l, irq, epoch, rights, &p) || !same(&p, &l->proof)) {
        l->phase = D0_LEASE_FAULT;
        return 0;
    }
    return 1;
}

int oem_d0_lease_irq_owner(void* ctx, uint64_t epoch, void* irq)
{
    return oem_d0_lease_can_use((oem_pnp_d0_lease_t*)ctx, irq, epoch, D0_RIGHT_ALL);
}

void oem_d0_lease_exit(oem_pnp_d0_lease_t* l)
{
    if (l && (l->phase == D0_LEASE_ACTIVE || l->phase == D0_LEASE_PREPARED)) {
        l->phase = D0_LEASE_CLOSED;
        memset(&l->provider, 0, sizeof(l->provider));
        memset(&l->proof, 0, sizeof(l->proof));
    }
}

void oem_d0_lease_revoke(oem_pnp_d0_lease_t* l)
{
    if (l) {
        memset(&l->provider, 0, sizeof(l->provider));
        memset(&l->proof, 0, sizeof(l->proof));
        memset(&l->match, 0, sizeof(l->match));
        l->pnp_generation = 0;
        l->phase = D0_LEASE_CLOSED;
    }
}
