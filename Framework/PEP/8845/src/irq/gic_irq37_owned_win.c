/* GICD INTID37 mask provider for the WDF enable/disable owner contract. Controller ownership must
 * be supplied independently. */

#include <ntddk.h>
#include "gic_irq37_owned_win.h"

static int valid(gic_irq37_owned_win_t* b, uint64_t epoch, void* interrupt)
{
    return b && b->active && !b->faulted && b->map && b->map->base &&
           b->map->pnp_generation == b->pnp_generation && b->pnp_generation && b->epoch == epoch &&
           epoch && b->wdf_interrupt == interrupt && interrupt && b->exclusive_owner &&
           b->exclusive_owner(b->proof_ctx, epoch, interrupt, b->pnp_generation) == 1;
}

static int exclusive(void* ctx, uint64_t epoch, void* interrupt)
{
    return valid((gic_irq37_owned_win_t*)ctx, epoch, interrupt);
}

/* enable state is observed through ISENABLER1, never read ICENABLER1 for state; the GICD
 * enable/disable registers have single-bit write semantics. */

static int read_masked(void* ctx, int* masked)
{
    gic_irq37_owned_win_t* b = ctx;
    uint32_t value;
    if (!b || !masked || !valid(b, b->epoch, b->wdf_interrupt))
        return 0;
    /* return one physical observation. The mask-owner layer compares fresh observations and
     * reconciles a KMDF enable edge after AMC retirement. A bit transition between
     * observations is not an invalid MMIO read. */
    value = (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(b->map->base + GICD_ISENABLER1));
    if (value == UINT32_MAX) {
        b->faulted = 1;
        return 0;
    }
    *masked = (value & GICD_INTID37_BIT) ? 0 : 1;
    return 1;
}

/* an owned GIC transition may straddle two reads. Observe again without rewriting; revoked
 * ownership or invalid MMIO still faults. */
static int read_postwrite_pair(gic_irq37_owned_win_t* b, int* observed)
{
    uint32_t a, c;
    if (!b || !observed || !valid(b, b->epoch, b->wdf_interrupt))
        return 0;
    a = (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(b->map->base + GICD_ISENABLER1));
    c = (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)(b->map->base + GICD_ISENABLER1));
    if (a == UINT32_MAX || c == UINT32_MAX)
        return 0;
    if (((a ^ c) & GICD_INTID37_BIT) != 0u)
        return 2;
    *observed = (c & GICD_INTID37_BIT) ? 0 : 1;
    return 1;
}

/* a GICD W1S/W1C write can be posted. Never resend an ambiguous write: verify a bounded
 * number of independent, stable ISENABLER1 read pairs while the exact external owner
 * remains valid. The mask-owner layer's post-write checks execute only AFTER this provider
 * reports success. */
#define GICD_WRITE_PROOF_PAIRS 8u

static int write_masked(void* ctx, int masked)
{
    gic_irq37_owned_win_t* b = ctx;
    int before = -1, after = -1;
    uint32_t off, attempt;
    if (!b)
        return 0;
    ++b->last_write_attempt_id;
    b->last_write_target = (int32_t)masked;
    b->last_write_observed = -1;
    b->last_write_proof_pairs = 0u;
    b->last_write_issued = 0u;
    b->last_write_failure_stage = GIC_WRITE_BAD_INPUT;
    if (masked != 0 && masked != 1)
        return 0;
    if (!valid(b, b->epoch, b->wdf_interrupt)) {
        b->last_write_failure_stage = GIC_WRITE_OWNER_PRE;
        return 0;
    }
    if (!read_masked(b, &before)) {
        b->last_write_failure_stage = GIC_WRITE_READ_PRE;
        return 0;
    }
    b->last_write_observed = (int32_t)before;
    if (before == masked) {
        b->last_write_failure_stage = GIC_WRITE_OK;
        return 1;
    }
    off = masked ? GICD_ICENABLER1 : GICD_ISENABLER1;
    WRITE_REGISTER_ULONG((volatile ULONG*)(b->map->base + off), (ULONG)GICD_INTID37_BIT);
    b->last_write_issued = 1u;
    for (attempt = 0u; attempt < GICD_WRITE_PROOF_PAIRS; ++attempt) {
        int proof;
        if (!valid(b, b->epoch, b->wdf_interrupt)) {
            b->last_write_failure_stage = GIC_WRITE_OWNER_POST;
            b->faulted = 1u;
            return 0;
        }
        b->last_write_proof_pairs = attempt + 1u;
        proof = read_postwrite_pair(b, &after);
        if (!proof) {
            b->last_write_failure_stage = GIC_WRITE_READ_POST;
            b->faulted = 1u;
            return 0;
        }
        if (proof == 2)
            continue;
        b->last_write_observed = (int32_t)after;
        if (after == masked) {
            if (!valid(b, b->epoch, b->wdf_interrupt)) {
                b->last_write_failure_stage = GIC_WRITE_OWNER_POST;
                b->faulted = 1u;
                return 0;
            }
            b->last_write_failure_stage = GIC_WRITE_OK;
            return 1;
        }
    }
    b->last_write_failure_stage = GIC_WRITE_NO_COMMIT;
    b->faulted = 1u;
    return 0;
}

static int prepare_mask_ops(gic_irq37_owned_win_t* b, gic_d0_map_win_t* map, uint64_t generation,
                            uint64_t epoch, void* irq, void* proof_ctx,
                            int (*proof)(void*, uint64_t, void*, uint64_t), oem_irq_mask_ops_t* ops,
                            int allow_owner_mask)
{
    int masked = -1;
    if (!b || !map || !map->base || map->bytes != GICD_BYTES || map->pnp_generation != generation ||
        !generation || !epoch || !irq || !proof || !ops || b->active || b->faulted ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || proof(proof_ctx, epoch, irq, generation) != 1)
        return 0;
    b->map = map;
    b->pnp_generation = generation;
    b->epoch = epoch;
    b->wdf_interrupt = irq;
    b->proof_ctx = proof_ctx;
    b->exclusive_owner = proof;
    b->active = 1;
    /* the native owner must establish exclusive IRQ37 authority and a separately synchronized
     * masked baseline before bind. */

    {
        int second = -1;
        if (!read_masked(b, &masked) || !read_masked(b, &second) || masked != second) {
            b->active = 0;
            b->faulted = 1;
            return 0;
        }
        if (masked != 1 && allow_owner_mask) {
            /* explicit OEM-equivalent handoff: only an independently proven, exclusive IRQ37/RSC
             * controller owner can make the GIC line quiescent before KMDF EvtInterruptEnable.
             * write EXACTLY the INTID37 bit to ICENABLER1 (W1C) and verify hardware twice.
             * write_masked rechecks the live external ownership. */

            if (!write_masked(b, 1) || !read_masked(b, &masked) || masked != 1 ||
                !read_masked(b, &second) || second != 1 || !valid(b, epoch, irq)) {
                b->faulted = 1;
                return 0;
            }
        }
        if (masked != 1) {
            /* observation-only API: never mask a live GIC interrupt. */

            b->active = 0;
            b->map = 0;
            b->pnp_generation = 0;
            b->epoch = 0;
            b->wdf_interrupt = 0;
            b->proof_ctx = 0;
            b->exclusive_owner = 0;
            return 0;
        }
    }
    ops->ctx = b;
    ops->exclusive_owner = exclusive;
    ops->read_masked = read_masked;
    ops->write_masked = write_masked;
    return 1;
}

int gic_irq37_prepare_mask_ops_win(gic_irq37_owned_win_t* b, gic_d0_map_win_t* map,
                                   uint64_t generation, uint64_t epoch, void* irq, void* proof_ctx,
                                   int (*proof)(void*, uint64_t, void*, uint64_t),
                                   oem_irq_mask_ops_t* ops)
{
    return prepare_mask_ops(b, map, generation, epoch, irq, proof_ctx, proof, ops, 0);
}

/* require an authorized masked baseline before writing GICD. Enabled startup requires exclusive
 * DRV2/IRQ37 control. */

int gic_irq37_acquire_masked_ops_win(gic_irq37_owned_win_t* b, gic_d0_map_win_t* map,
                                     uint64_t generation, uint64_t epoch, void* irq,
                                     void* proof_ctx,
                                     int (*proof)(void*, uint64_t, void*, uint64_t),
                                     oem_irq_mask_ops_t* ops)
{
    return prepare_mask_ops(b, map, generation, epoch, irq, proof_ctx, proof, ops, 1);
}

int gic_irq37_retire_mask_ops_win(gic_irq37_owned_win_t* b, uint64_t epoch, void* irq,
                                  int owner_drained)
{
    int masked = -1;
    if (!b || !owner_drained || KeGetCurrentIrql() != PASSIVE_LEVEL || !valid(b, epoch, irq) ||
        !read_masked(b, &masked) || masked != 1 || !read_masked(b, &masked) || masked != 1)
        return 0;
    b->active = 0;
    b->epoch = 0;
    b->wdf_interrupt = 0;
    b->pnp_generation = 0;
    b->map = 0;
    b->proof_ctx = 0;
    b->exclusive_owner = 0;
    return 1;
}
