#include "rsc_irq_assignment.h"
#include <limits.h>

/* never expose a stale res vector/idx from a failed inspection. on restart, a diagnostic
 * consumer must distinguish invalid telemetry from a unique binding even if a later desc
 * invalidates the scan. */

static rsc_irq_report_t sanitized(rsc_irq_report_t r)
{
    if (r.finding != RSC_IRQ_UNIQUE_RAW_CANDIDATE) {
        r.candidate_idx = UINT32_MAX;
        r.win_vector = 0u;
        r.win_level = 0u;
        r.raw_vector = r.raw_level = 0u;
        r.raw_flags = r.translated_flags = 0u;
        r.raw_share_disposition = r.translated_share_disposition = 0u;
    }
    return r;
}

rsc_irq_report_t rsc_analyze_irq_pairs(const rsc_irq_pair_t* pairs, size_t cnt)
{
    rsc_irq_report_t r = {0};
    size_t i;
    uint32_t suspect = 0;

    const uint16_t cm_latched_bit = 0x0001u;
    r.candidate_idx = UINT32_MAX;
    if ((!pairs && cnt) || cnt > RSC_IRQ_RES_LIMIT) {
        r.finding = RSC_IRQ_MALFORMED;
        return sanitized(r);
    }
    for (i = 0; i < cnt; ++i) {
        const rsc_irq_pair_t* p = &pairs[i];
        int mentions37;
        if (p->raw_is_interrupt)
            ++r.raw_interrupts;
        if (p->translated_is_interrupt)
            ++r.translated_interrupts;
        if (p->raw_is_interrupt != p->translated_is_interrupt) {
            r.finding = RSC_IRQ_MALFORMED;
            return sanitized(r);
        }
        if (!p->raw_is_interrupt)
            continue;
        /* cm_PARTIAL_RESOURCE_DESCRIPTOR.u.interrupt.level is a bus-specific IRQL in the raw list,
         * not the GIC INTID. in particular, a raw vector of 37 with level 0 or another IRQL can be
         * valid. */

        mentions37 = !p->raw_is_message && p->raw_vector == RSC_IRQ_EXPECTED_GIC_INTID;
        if (mentions37) {
            ++suspect;
            /* both raw desc fields must agree and the translated res must be valid. */

            if (p->raw_is_message || p->translated_is_message ||
                p->raw_vector != RSC_IRQ_EXPECTED_GIC_INTID || p->translated_vector == 0u ||
                p->translated_level == 0u || (p->raw_flags & cm_latched_bit) != 0u ||
                (p->translated_flags & cm_latched_bit) != 0u ||
                p->raw_share_disposition != p->translated_share_disposition) {
                r.finding = RSC_IRQ_AMBIGUOUS;
                r.candidate_idx = UINT32_MAX;
                r.win_vector = r.win_level = 0u;
                return sanitized(r);
            }
            ++r.matching_raw_pairs;
            r.candidate_idx = (uint32_t)i;
            r.win_vector = p->translated_vector;
            r.win_level = p->translated_level;
            r.raw_vector = p->raw_vector;
            r.raw_level = p->raw_level;
            r.raw_flags = p->raw_flags;
            r.translated_flags = p->translated_flags;
            r.raw_share_disposition = p->raw_share_disposition;
            r.translated_share_disposition = p->translated_share_disposition;
        }
    }
    if (r.matching_raw_pairs > 1u || suspect > 1u) {
        r.finding = RSC_IRQ_AMBIGUOUS;
        r.candidate_idx = UINT32_MAX;
        r.win_vector = r.win_level = 0u;
    } else if (r.matching_raw_pairs == 1u) {
        /* one raw src must not alias another translated interrupt desc. */

        for (i = 0; i < cnt; ++i) {
            if (i != r.candidate_idx && pairs[i].translated_is_interrupt &&
                !pairs[i].translated_is_message && pairs[i].translated_vector == r.win_vector) {
                r.finding = RSC_IRQ_AMBIGUOUS;
                return sanitized(r);
            }
        }
        r.finding = RSC_IRQ_UNIQUE_RAW_CANDIDATE;
    } else {
        for (i = 0; i < cnt; ++i) {
            if (pairs[i].translated_is_interrupt && !pairs[i].translated_is_message &&
                (pairs[i].translated_vector == RSC_IRQ_EXPECTED_GIC_INTID ||
                 pairs[i].translated_level == RSC_IRQ_EXPECTED_GIC_INTID)) {
                r.finding = RSC_IRQ_TRANSLATED_ONLY;
                return sanitized(r);
            }
        }
        r.finding = RSC_IRQ_NOT_OBSERVED;
    }
    return sanitized(r);
}
