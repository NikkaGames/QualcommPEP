/* the owner must verify the register state before publishing its client vote. An uncertain
 * write must not be silently undone only in software. */

#include "commit.h"
#include <string.h>

/* our additional owner proof (the original bit still held in MMIO) must allow bounded MMIO
 * propagation before calling a posted write uncertain. */

#define GCC_VOTE_VERIFY_POLLS 2000u

static int same_observation(const gcc_state_t* a, const gcc_state_t* b)
{
    uint32_t cbcr_mask = 0xffffffffu;
    /* while the hardware autonomously gates the branch, CLK_OFF may legitimately change
     * between two MMIO reads. Require matching HW_CTL, client vote/enable, config and rate
     * instead. */

    if ((a->cbcr & CBCR_CLK_HW_CTL) != (b->cbcr & CBCR_CLK_HW_CTL))
        return 0;
    if (a->cbcr & CBCR_CLK_HW_CTL)
        cbcr_mask = 0x7fffffffu;
    return (a->cbcr & cbcr_mask) == (b->cbcr & cbcr_mask) && a->vote_reg == b->vote_reg &&
           a->cmd_rcgr == b->cmd_rcgr && a->cfg_rcgr == b->cfg_rcgr && a->m == b->m &&
           a->n == b->n && a->hz == b->hz && a->enabled == b->enabled &&
           ((cbcr_mask == 0x7fffffffu) || a->on == b->on);
}

static int same_vote_observation(const gcc_clock_t* clock, const gcc_state_t* a,
                                 const gcc_state_t* b)
{
    gcc_state_t x, y;
    if (clock->kind != GCC_KIND_REQ)
        return same_observation(a, b);
    x = *a;
    y = *b;
    x.cbcr &= ~CBCR_CLK_OFF_MASK;
    y.cbcr &= ~CBCR_CLK_OFF_MASK;
    x.on = 0u;
    y.on = 0u;
    return same_observation(&x, &y);
}

/* a second client of a clock already held enabled must not poke the same CBCR again. the
 * original Qualcomm HAL increments per-client and per-clock references on 1->n; only the
 * first reference enables HW. */

gcc_commit_result_t gcc_verify_retained_vote(const gcc_io_t* io, const gcc_clock_t* clock,
                                             uint32_t programmed_hz)
{
    gcc_state_t a, b;
    uint32_t mask;
    if (!io || !io->read32 || !clock ||
        (clock->kind != GCC_KIND_BRANCH && clock->kind != GCC_KIND_VOTE &&
         clock->kind != GCC_KIND_REQ))
        return GCC_COMMIT_DECLINED;
    if (gcc_readback(io, clock, &a) != GCC_OK || gcc_readback(io, clock, &b) != GCC_OK ||
        !a.enabled || !b.enabled)
        return GCC_COMMIT_DECLINED;
    if (programmed_hz && (a.hz != programmed_hz || b.hz != programmed_hz))
        return GCC_COMMIT_DECLINED;
    if (a.vote_reg != b.vote_reg || a.cmd_rcgr != b.cmd_rcgr || a.cfg_rcgr != b.cfg_rcgr ||
        a.m != b.m || a.n != b.n)
        return GCC_COMMIT_DECLINED;
    /* hw_CTL changes when hardware takes control, so its agreement is mandatory. */

    if ((a.cbcr & CBCR_CLK_HW_CTL) != (b.cbcr & CBCR_CLK_HW_CTL))
        return GCC_COMMIT_DECLINED;
    mask = (a.cbcr & CBCR_CLK_HW_CTL) ? 0x7fffffffu : 0xffffffffu;
    if (clock->kind == GCC_KIND_REQ)
        mask &= ~CBCR_CLK_OFF_MASK;
    if ((a.cbcr & mask) != (b.cbcr & mask))
        return GCC_COMMIT_DECLINED;
    if (clock->kind != GCC_KIND_REQ && !(a.cbcr & CBCR_CLK_HW_CTL) && (!a.on || !b.on))
        return GCC_COMMIT_DECLINED;
    return GCC_COMMIT_OK;
}

gcc_commit_result_t gcc_commit_freq_ex(const gcc_io_t* io, const gcc_clock_t* clock,
                                       uint32_t requested_hz, uint32_t* programmed_hz,
                                       gcc_commit_diag_t* diag)
{
    gcc_state_t before1, before2, after1, after2;
    const gcc_freq_t* selected = 0;
    const gcc_domain_t* domain;
    uint32_t attempt, cbcr_mask;
    if (diag)
        memset(diag, 0, sizeof(*diag));
    if (!io || !io->read32 || !io->write32 || !io->barrier || !io->stall_microseconds || !clock ||
        !programmed_hz || !requested_hz ||
        (clock->kind != GCC_KIND_BRANCH && clock->kind != GCC_KIND_VOTE) ||
        !(domain = gcc_clock_domain(clock)) || domain->flags || !domain->cmd_rcgr_offset ||
        gcc_select_freq(clock, requested_hz, &selected) != GCC_OK || !selected ||
        selected->hz != requested_hz)
        return GCC_COMMIT_DECLINED;
    if (diag)
        diag->stage = 1u;
    if (gcc_readback(io, clock, &before1) != GCC_OK)
        return GCC_COMMIT_DECLINED;
    if (diag) {
        diag->before1 = before1;
        diag->stage = 2u;
    }
    if (gcc_readback(io, clock, &before2) != GCC_OK)
        return GCC_COMMIT_DECLINED;
    if (diag) {
        diag->before2 = before2;
        diag->stage = 3u;
    }
    if (!same_observation(&before1, &before2) || (*programmed_hz && before2.hz != *programmed_hz))
        return GCC_COMMIT_DECLINED;
    if (before2.hz == requested_hz) {
        *programmed_hz = requested_hz;
        return GCC_COMMIT_OK;
    }
    if (!gcc_src_ready(io, selected->src_kind))
        return GCC_COMMIT_DECLINED;
    if (diag) {
        diag->stage = 4u;
        diag->write_started = 1u;
    }
    if (gcc_set_freq(io, clock, selected) != GCC_OK)
        return GCC_COMMIT_UNCERTAIN;
    cbcr_mask = (before2.cbcr & CBCR_CLK_HW_CTL) ? 0x7fffffffu : 0xffffffffu;
    for (attempt = 0u; attempt < GCC_VOTE_VERIFY_POLLS; ++attempt) {
        if (diag) {
            diag->stage = 7u;
            diag->post_poll_cnt = attempt + 1u;
        }
        if (gcc_readback(io, clock, &after1) != GCC_OK)
            return GCC_COMMIT_UNCERTAIN;
        if (diag) {
            diag->after1 = after1;
            diag->stage = 8u;
        }
        if (gcc_readback(io, clock, &after2) != GCC_OK)
            return GCC_COMMIT_UNCERTAIN;
        if (diag) {
            diag->after2 = after2;
            diag->stage = 9u;
        }
        if (same_observation(&after1, &after2) && after1.hz == requested_hz &&
            after2.hz == requested_hz && (before2.cbcr & cbcr_mask) == (after2.cbcr & cbcr_mask) &&
            before2.vote_reg == after2.vote_reg && before2.enabled == after2.enabled)
            break;
        if (attempt + 1u < GCC_VOTE_VERIFY_POLLS)
            io->stall_microseconds(io->ctx, 1u);
    }
    if (attempt == GCC_VOTE_VERIFY_POLLS)
        return GCC_COMMIT_UNCERTAIN;
    *programmed_hz = requested_hz;
    return GCC_COMMIT_OK;
}

gcc_commit_result_t gcc_commit_branch_vote_ex(const gcc_io_t* io, const gcc_clock_t* clock,
                                              uint32_t requested_hz, int enable,
                                              uint32_t* programmed_hz, gcc_commit_diag_t* diag)
{
    gcc_state_t before1, before2, after1, after2;
    const gcc_freq_t* selected = 0;
    uint32_t target_hz;
    int needs_rate;
    if (diag)
        memset(diag, 0, sizeof(*diag));
    if (!io || !io->read32 || !io->write32 || !io->barrier || !io->stall_microseconds || !clock ||
        !programmed_hz || (enable != 0 && enable != 1) ||
        (clock->kind != GCC_KIND_BRANCH && clock->kind != GCC_KIND_VOTE &&
         clock->kind != GCC_KIND_REQ))
        return GCC_COMMIT_DECLINED;

    if (requested_hz && gcc_select_freq(clock, requested_hz, &selected) != GCC_OK)
        return GCC_COMMIT_DECLINED;
    target_hz = selected ? selected->hz : *programmed_hz;
    needs_rate = selected && target_hz != *programmed_hz;

    /* it must refuse the vote, but it cannot quarantine as if this owner had written an
     * unverified value. */

    if (diag)
        diag->stage = 1u;
    if (gcc_readback(io, clock, &before1) != GCC_OK)
        return GCC_COMMIT_DECLINED;
    if (diag) {
        diag->before1 = before1;
        diag->stage = 2u;
    }
    if (gcc_readback(io, clock, &before2) != GCC_OK)
        return GCC_COMMIT_DECLINED;
    if (diag) {
        diag->before2 = before2;
        diag->stage = 3u;
    }
    if (!same_vote_observation(clock, &before1, &before2) ||
        (*programmed_hz && before2.hz != *programmed_hz))
        return GCC_COMMIT_DECLINED;

    if (needs_rate) {
        /* once the HAL setter runs cannot infer whether a register write occurred when it fails:
         * keep the conservative uncertainty fence. */

        if (diag) {
            diag->stage = 4u;
            diag->write_started = 1u;
        }
        if (gcc_set_freq(io, clock, selected) != GCC_OK)
            return GCC_COMMIT_UNCERTAIN;
        if (diag)
            diag->stage = 5u;
        if (gcc_readback(io, clock, &after1) != GCC_OK)
            return GCC_COMMIT_UNCERTAIN;
        if (diag)
            diag->after1 = after1;
        if (after1.hz != target_hz)
            return GCC_COMMIT_UNCERTAIN;
    }

    /* it is never permissible to turn a failed post-write readback into a software-only
     * successful vote. */

    if (diag) {
        diag->stage = 6u;
        diag->write_started = 1u;
    }
    if ((enable ? gcc_enable(io, clock) : gcc_disable(io, clock)) != GCC_OK)
        return GCC_COMMIT_UNCERTAIN;
    /* a posted MMIO write and the clock's own state machine may converge later than the first
     * immediate read. the HAL polls CLK_OFF up to its timeout; unlike that HAL, this PEP also
     * needs to prove its OWN enable bit survived (a physical ON from another voter is not
     * enough). */

    {
        uint32_t attempt;
        for (attempt = 0u; attempt < GCC_VOTE_VERIFY_POLLS; ++attempt) {
            if (diag) {
                diag->stage = 7u;
                diag->post_poll_cnt = attempt + 1u;
            }
            if (gcc_readback(io, clock, &after1) != GCC_OK)
                return GCC_COMMIT_UNCERTAIN;
            if (diag) {
                diag->after1 = after1;
                diag->stage = 8u;
            }
            if (gcc_readback(io, clock, &after2) != GCC_OK)
                return GCC_COMMIT_UNCERTAIN;
            if (diag) {
                diag->after2 = after2;
                diag->stage = 9u;
            }
            if (same_vote_observation(clock, &after1, &after2) && (int)after1.enabled == enable &&
                (int)after2.enabled == enable &&
                (!enable || clock->kind == GCC_KIND_REQ || after2.on ||
                 (after2.cbcr & CBCR_CLK_HW_CTL)) &&
                (!selected || (after1.hz == target_hz && after2.hz == target_hz)))
                break;
            if (attempt + 1u < GCC_VOTE_VERIFY_POLLS)
                io->stall_microseconds(io->ctx, 1u);
        }
        if (attempt == GCC_VOTE_VERIFY_POLLS)
            return GCC_COMMIT_UNCERTAIN;
    }
    *programmed_hz = target_hz;
    return GCC_COMMIT_OK;
}

gcc_commit_result_t gcc_commit_branch_vote(const gcc_io_t* io, const gcc_clock_t* clock,
                                           uint32_t requested_hz, int enable,
                                           uint32_t* programmed_hz)
{
    return gcc_commit_branch_vote_ex(io, clock, requested_hz, enable, programmed_hz, 0);
}
