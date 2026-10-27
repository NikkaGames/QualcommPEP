#include "rsc_irq_snapshot.h"
#include <string.h>

static void clear_observation(rsc_irq_snapshot_t* s)
{
    memset(&s->report, 0, sizeof(s->report));
    s->report.candidate_idx = UINT32_MAX;
    s->report.finding = RSC_IRQ_MALFORMED;
    s->prepared = 0;
    s->unique_candidate = 0;
}

static void next_generation(rsc_irq_snapshot_t* s)
{
    ++s->generation;
    if (s->generation == 0)
        s->generation = 1; /* zero is never valid. */
}

void rsc_irq_snapshot_init(rsc_irq_snapshot_t* s)
{
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
    s->generation = 1;
    clear_observation(s);
}

void rsc_irq_snapshot_revoke(rsc_irq_snapshot_t* s)
{
    if (!s)
        return;
    next_generation(s);
    clear_observation(s);
}

void rsc_irq_snapshot_capture(rsc_irq_snapshot_t* s, rsc_irq_report_t report)
{
    if (!s)
        return;
    rsc_irq_snapshot_revoke(s);
    s->prepared = 1;
    /* these checks are diagnostic invariants, not hardware attestation. */

    if (report.finding == RSC_IRQ_UNIQUE_RAW_CANDIDATE && report.matching_raw_pairs == 1u &&
        report.candidate_idx < RSC_IRQ_RES_LIMIT && report.raw_interrupts > 0u &&
        report.translated_interrupts > 0u && report.win_vector != 0u && report.win_level != 0u &&
        report.raw_vector == RSC_IRQ_EXPECTED_GIC_INTID) {
        s->report = report;
        s->unique_candidate = 1;
    }
}

int rsc_irq_snapshot_read(const rsc_irq_snapshot_t* s, uint64_t generation, rsc_irq_report_t* out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
        out->candidate_idx = UINT32_MAX;
        out->finding = RSC_IRQ_MALFORMED;
    }
    if (!s || !out || !generation || s->generation != generation || !s->prepared ||
        !s->unique_candidate || s->report.finding != RSC_IRQ_UNIQUE_RAW_CANDIDATE)
        return 0;
    *out = s->report;
    return 1;
}
