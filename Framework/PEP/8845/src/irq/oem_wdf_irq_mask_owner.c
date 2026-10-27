/* actual EvtInterruptEnable/Disable mask contract, hardware agnostic. No MMIO address, GIC
 * write, ownership grant, or production caller exists. */

#include "oem_wdf_irq_mask_owner.h"
#include <string.h>

static int read_twice(const oem_irq_mask_owner_t* o, int expected)
{
    int a = -1, b = -1;
    return o->io.read_masked(o->io.ctx, &a) == 1 && o->io.read_masked(o->io.ctx, &b) == 1 &&
           (a == 0 || a == 1) && a == b && a == expected;
}

static int valid(const oem_irq_mask_owner_t* o, void* irq, uint64_t epoch)
{
    return o && irq && epoch && o->interrupt == irq && o->epoch == epoch && o->io.exclusive_owner &&
           o->io.read_masked && o->io.write_masked;
}

void oem_irq_mask_quarantine(oem_irq_mask_owner_t* o)
{
    if (o)
        o->phase = OEM_IRQ_MASK_FAULT;
}

int oem_irq_mask_bind(oem_irq_mask_owner_t* o, const oem_irq_mask_ops_t* io, void* irq,
                      uint64_t epoch)
{
    if (!o || !io || o->phase || !irq || !epoch || !io->exclusive_owner || !io->read_masked ||
        !io->write_masked)
        return 0;
    o->io = *io;
    o->interrupt = irq;
    o->epoch = epoch;
    if (io->exclusive_owner(io->ctx, epoch, irq) != 1 || !read_twice(o, 1)) {
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    o->phase = OEM_IRQ_MASK_OFF;
    return 1;
}

/* a GICD ISENABLER/ICENABLER W1 write can be observed by the hardware before a following
 * observer sees consistently updated state. The write provider has its own required
 * readback; only if that completed may the owner allow a bounded sequence of independent,
 * two-read proofs. */

#define MASK_POSTWRITE_PROOF_ATTEMPTS 4u

int oem_irq_mask_transition(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch, int masked)
{
    uint32_t old_phase, attempt;
    if (!valid(o, irq, epoch) || (masked != 0 && masked != 1)) {
        if (o)
            o->last_failure = OEM_MASK_FAIL_INPUT;
        return 0;
    }
    if (o->phase == OEM_IRQ_MASK_FAULT || o->phase == OEM_IRQ_MASK_UNBOUND) {
        o->last_failure = OEM_MASK_FAIL_STATE;
        return 0;
    }
    o->framework_observed_transition = 0;
    old_phase = o->phase;
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    /* the pre-write state has no grace period: do not mask over a foreign change or unmask
     * over an interrupt already enabled by another owner. */

    if (!read_twice(o, old_phase == OEM_IRQ_MASK_OFF)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    if ((masked && old_phase == OEM_IRQ_MASK_OFF) || (!masked && old_phase == OEM_IRQ_MASK_ON)) {
        o->last_failure = OEM_MASK_FAIL_NONE;
        return 1;
    }
    if (o->io.write_masked(o->io.ctx, masked) != 1) {
        o->last_failure = OEM_MASK_FAIL_WRITE;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    /* do NOT write again or skip the external owner proof: allow at most four independent
     * two-read pairs to observe a committed GIC register state. This is lock-safe and
     * nonblocking at the WDF interrupt callback's configured IRQL. */

    for (attempt = 0u; attempt < MASK_POSTWRITE_PROOF_ATTEMPTS; ++attempt) {
        if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
            o->last_failure = OEM_MASK_FAIL_OWNER;
            o->phase = OEM_IRQ_MASK_FAULT;
            return 0;
        }
        if (read_twice(o, masked)) {
            if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
                o->last_failure = OEM_MASK_FAIL_OWNER;
                o->phase = OEM_IRQ_MASK_FAULT;
                return 0;
            }
            o->last_failure = OEM_MASK_FAIL_NONE;
            o->phase = masked ? OEM_IRQ_MASK_OFF : OEM_IRQ_MASK_ON;
            return 1;
        }
    }
    o->last_failure = OEM_MASK_FAIL_POST_READ;
    o->phase = OEM_IRQ_MASK_FAULT;
    return 0;
}

/* final AMC completion may race the KMDF-owned GIC enable transition. Accept an
 * already-enabled line only after fresh, independent physical drain/owner/readback proofs.
 * Never replay the GIC write or invent an ACK. */
static int terminal_adopt_enabled(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch)
{
    int pass;
    for (pass = 0; pass < 2; ++pass) {
        o->last_finalize_step = 7u + (uint32_t)pass * 3u;
        if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
            o->last_failure = OEM_MASK_FAIL_OWNER;
            return 0;
        }
        o->last_finalize_step = 8u + (uint32_t)pass * 3u;
        if (!o->io.completion_drained || o->io.completion_drained(o->io.ctx, epoch, irq) != 1) {
            o->last_failure = OEM_MASK_FAIL_SRC_BUSY;
            return 0;
        }
        o->last_finalize_step = 9u + (uint32_t)pass * 3u;
        if (!read_twice(o, 0)) {
            o->last_failure = OEM_MASK_FAIL_PRE_READ;
            return 0;
        }
    }
    o->last_finalize_step = 13u;
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        return 0;
    }
    o->phase = OEM_IRQ_MASK_ON;
    o->last_failure = OEM_MASK_FAIL_NONE;
    o->verified_completed_unmask = 1u;
    o->last_finalize_step = 0u;
    return 1;
}

int oem_irq_mask_finalize_completed(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch)
{
    int a = -1, b = -1, read_a, read_b;
    if (o) {
        o->last_finalize_step = 1u;
        o->last_finalize_read_a = -1;
        o->last_finalize_read_b = -1;
        o->verified_completed_unmask = 0u;
    }
    if (!valid(o, irq, epoch)) {
        if (o)
            o->last_failure = OEM_MASK_FAIL_INPUT;
        return 0;
    }
    o->last_finalize_step = 2u;
    if (o->phase != OEM_IRQ_MASK_OFF) {
        o->last_failure = OEM_MASK_FAIL_STATE;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    o->last_finalize_step = 3u;
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    o->last_finalize_step = 4u;
    if (!o->io.completion_drained || o->io.completion_drained(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_SRC_BUSY;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    o->last_finalize_step = 5u;
    read_a = o->io.read_masked(o->io.ctx, &a);
    if (read_a == 1)
        o->last_finalize_read_a = a;
    read_b = read_a == 1 ? o->io.read_masked(o->io.ctx, &b) : 0;
    if (read_b == 1)
        o->last_finalize_read_b = b;
    if (read_a != 1 || read_b != 1 || (a != 0 && a != 1) || (b != 0 && b != 1)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    if (a != b) {
        /* an enable can complete between samples. Reconcile only stable enabled samples under the
         * same completed AMC and owner. */
        o->last_finalize_step = 6u;
        if (terminal_adopt_enabled(o, irq, epoch))
            return 1;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    if (a == 0) {
        if (terminal_adopt_enabled(o, irq, epoch))
            return 1;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    /* the line was masked in two independent reads. The ordinary strict transition still
     * checks the initial physical state before W1S. */
    o->last_finalize_step = 14u;
    if (oem_irq_mask_transition(o, irq, epoch, 0)) {
        o->last_finalize_step = 0u;
        return 1;
    }
    /* pre_READ happens strictly before write_masked; only this case may reconcile a HAL/KMDF
     * enable edge. All owner/write/post-write faults remain terminal and cannot be retried or
     * acknowledged. */
    if (o->last_failure == OEM_MASK_FAIL_PRE_READ) {
        o->last_finalize_step = 15u;
        if (terminal_adopt_enabled(o, irq, epoch))
            return 1;
    }
    o->phase = OEM_IRQ_MASK_FAULT;
    return 0;
}

/* requires an actual owned pending src under the WDF passive interrupt lock. Both ISR
 * entry and exact-ticket passive observation can supply that proof; a D-state request
 * alone never can. */

int oem_irq_mask_verified_isr_mask(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch,
                                   int verified_owned_pending)
{
    int masked = -1;
    if (!valid(o, irq, epoch) || verified_owned_pending != 1) {
        if (o)
            o->last_failure = OEM_MASK_FAIL_INPUT;
        return 0;
    }
    if (o->phase != OEM_IRQ_MASK_ON) {
        o->last_failure = OEM_MASK_FAIL_STATE;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    /* never infer a mask from an IRQ request or from the WDF handle. */

    if (o->io.read_masked(o->io.ctx, &masked) != 1 || (masked != 0 && masked != 1)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    if (masked == 0) {
        int result = oem_irq_mask_transition(o, irq, epoch, 1);
        if (result || o->last_failure != OEM_MASK_FAIL_PRE_READ)
            return result;
        /* pre_READ issued no write. Under the same verified pending ISR, HAL may have masked
         * between the initial read and transition. Adopt only the existing independent
         * owner/masked proofs below. */
    }

    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1 || !read_twice(o, 1) ||
        !read_twice(o, 1) || o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1 ||
        !read_twice(o, 1)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        o->phase = OEM_IRQ_MASK_FAULT;
        return 0;
    }
    o->phase = OEM_IRQ_MASK_OFF;
    o->last_failure = OEM_MASK_FAIL_NONE;
    o->framework_observed_transition = 1;
    return 1;
}

/* this is an evidence-checked handshake, not a force-success retry: the physical bit must
 * equal the REQUESTED state in four reads, the src must have no issued or pending AMC, and
 * the independent owner must remain valid before and after. */

int oem_irq_mask_wdf_transition(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch, int masked)
{
    int a = -1, b = -1;
    uint32_t target_phase;
    if (!valid(o, irq, epoch) || (masked != 0 && masked != 1)) {
        if (o)
            o->last_failure = OEM_MASK_FAIL_INPUT;
        return 0;
    }
    o->framework_observed_transition = 0;
    if (o->phase == OEM_IRQ_MASK_FAULT || o->phase == OEM_IRQ_MASK_UNBOUND) {
        o->last_failure = OEM_MASK_FAIL_STATE;
        return 0;
    }
    target_phase = masked ? OEM_IRQ_MASK_OFF : OEM_IRQ_MASK_ON;
    /* this branch is allowed only if the previous committed software state is opposite to the
     * KMDF-requested physical state. */

    if (o->phase != target_phase && o->io.exclusive_owner(o->io.ctx, epoch, irq) == 1 &&
        o->io.read_masked(o->io.ctx, &a) == 1 && o->io.read_masked(o->io.ctx, &b) == 1 && a == b &&
        a == masked) {
        if (!o->io.src_quiescent || o->io.src_quiescent(o->io.ctx, epoch, irq) != 1) {
            o->last_failure = OEM_MASK_FAIL_SRC_BUSY;
            o->phase = OEM_IRQ_MASK_FAULT;
            return 0;
        }
        if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
            o->last_failure = OEM_MASK_FAIL_OWNER;
            o->phase = OEM_IRQ_MASK_FAULT;
            return 0;
        }
        if (!read_twice(o, masked)) {
            o->last_failure = OEM_MASK_FAIL_PRE_READ;
            o->phase = OEM_IRQ_MASK_FAULT;
            return 0;
        }
        o->phase = target_phase;
        o->last_failure = OEM_MASK_FAIL_NONE;
        o->framework_observed_transition = 1;
        return 1;
    }
    /* no proof of a WDF/HAL-committed edge: retain the original strict
     * owner/pre-read/write/post-read state machine and quarantine rules. */

    return oem_irq_mask_transition(o, irq, epoch, masked);
}

/* kmdf/HAL connects the PnP line; the RPMh ISR and completion worker perform the
 * per-transaction GIC mask/unmask. observes the actual connect/disconnect result from the
 * corresponding DEVICE power callback. It never drives a GIC register or infers a grant
 * from a WDF handle. */

int oem_irq_mask_observe_framework(oem_irq_mask_owner_t* o, void* irq, uint64_t epoch, int masked)
{
    uint32_t expected_phase;
    if (!valid(o, irq, epoch) || (masked != 0 && masked != 1)) {
        if (o)
            o->last_failure = OEM_MASK_FAIL_INPUT;
        return 0;
    }
    if (o->phase == OEM_IRQ_MASK_FAULT || o->phase == OEM_IRQ_MASK_UNBOUND) {
        o->last_failure = OEM_MASK_FAIL_STATE;
        return 0;
    }
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        return 0;
    }
    if (!o->io.src_quiescent || o->io.src_quiescent(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_SRC_BUSY;
        return 0;
    }
    /* two pairs, each hardware provider double-reads GICD_ISENABLER1. Any unstable/mismatched
     * state FAILS; never coerce software phase. */

    if (!read_twice(o, masked) || !read_twice(o, masked)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        return 0;
    }
    if (o->io.exclusive_owner(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_OWNER;
        return 0;
    }
    if (o->io.src_quiescent(o->io.ctx, epoch, irq) != 1) {
        o->last_failure = OEM_MASK_FAIL_SRC_BUSY;
        return 0;
    }

    if (!read_twice(o, masked)) {
        o->last_failure = OEM_MASK_FAIL_PRE_READ;
        return 0;
    }
    expected_phase = masked ? OEM_IRQ_MASK_OFF : OEM_IRQ_MASK_ON;
    o->phase = expected_phase;
    o->last_failure = OEM_MASK_FAIL_NONE;
    o->framework_observed_transition = 1;
    return 1;
}

int oem_irq_mask_read_proof(const oem_irq_mask_owner_t* o, void* irq, uint64_t epoch, int masked)
{
    return valid(o, irq, epoch) && (masked == 0 || masked == 1) &&
           o->phase == (masked ? OEM_IRQ_MASK_OFF : OEM_IRQ_MASK_ON) &&
           o->io.exclusive_owner(o->io.ctx, epoch, irq) == 1 && read_twice(o, masked);
}
