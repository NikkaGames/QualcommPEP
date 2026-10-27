#include "oem_rsc_pnp_evidence.h"
#include <string.h>

static int overlaps(uint64_t base, uint64_t bytes)
{
    const uint64_t expected = RSC_EVIDENCE_DRV2_BASE;
    if (!bytes || bytes > UINT64_MAX - base)
        return -1;
    return base < expected + RSC_EVIDENCE_DRV2_BYTES && base + bytes > expected;
}

static int exclusive_level(const rsc_evidence_desc_t* r, const rsc_evidence_desc_t* t)
{
    return r->kind == RSC_EVIDENCE_INTERRUPT && t->kind == RSC_EVIDENCE_INTERRUPT && !r->message &&
           !t->message && !(r->flags & RSC_EVIDENCE_LATCHED) &&
           !(t->flags & RSC_EVIDENCE_LATCHED) && r->share == RSC_EVIDENCE_EXCLUSIVE &&
           t->share == RSC_EVIDENCE_EXCLUSIVE && t->vector && t->level;
}

rsc_evidence_finding_t rsc_audit_pnp_evidence(const rsc_evidence_desc_t* raw,
                                              const rsc_evidence_desc_t* translated, size_t cnt,
                                              uint64_t generation, rsc_evidence_report_t* out)
{
    rsc_evidence_report_t result = {0};
    size_t i, j;
    rsc_evidence_finding_t finding = RSC_EVIDENCE_INVALID;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!out)
        return finding;
    result.window_idx = UINT32_MAX;
    result.irq_idx = UINT32_MAX;
    result.res_cnt = (uint32_t)cnt;
    result.pnp_generation = generation;
    if (!generation || cnt > RSC_EVIDENCE_MAX || (cnt && (!raw || !translated)))
        goto done;
    for (i = 0; i < cnt; ++i) {
        int ro, to;
        const rsc_evidence_desc_t *r = &raw[i], *t = &translated[i];
        if (r->kind < 1u || r->kind > 3u || t->kind < 1u || t->kind > 3u || r->message > 1u ||
            t->message > 1u)
            goto done;
        ro = r->kind == RSC_EVIDENCE_MEMORY ? overlaps(r->base, r->bytes) : 0;
        to = t->kind == RSC_EVIDENCE_MEMORY ? overlaps(t->base, t->bytes) : 0;
        if (ro < 0 || to < 0)
            goto done;
        if (ro || to) {
            /* the raw and translated descriptors must represent the same dedicated DRV2 aperture at
             * exactly the same PnP idx. */

            if (r->kind != RSC_EVIDENCE_MEMORY || t->kind != RSC_EVIDENCE_MEMORY ||
                r->base != RSC_EVIDENCE_DRV2_BASE || t->base != RSC_EVIDENCE_DRV2_BASE ||
                r->bytes != RSC_EVIDENCE_DRV2_BYTES || t->bytes != RSC_EVIDENCE_DRV2_BYTES ||
                ro != 1 || to != 1) {
                finding = RSC_EVIDENCE_AMBIGUOUS_WINDOW;
                goto done;
            }
            if (++result.exact_win > 1) {
                finding = RSC_EVIDENCE_AMBIGUOUS_WINDOW;
                goto done;
            }
            result.window_idx = (uint32_t)i;
        }
    }
    if (!result.exact_win) {
        finding = RSC_EVIDENCE_NO_WINDOW;
        goto done;
    }
    result.has_dedicated_window = 1;
    for (i = 0; i < cnt; ++i) {
        const rsc_evidence_desc_t *r = &raw[i], *t = &translated[i];
        if (r->kind != RSC_EVIDENCE_INTERRUPT || r->vector != RSC_EVIDENCE_IRQ)
            continue;
        if (++result.matching_raw_irqs > 1) {
            finding = RSC_EVIDENCE_AMBIGUOUS_IRQ;
            goto done;
        }
        if (t->kind != RSC_EVIDENCE_INTERRUPT || !t->vector || !t->level || r->message ||
            t->message || (r->flags & RSC_EVIDENCE_LATCHED) || (t->flags & RSC_EVIDENCE_LATCHED)) {
            finding = RSC_EVIDENCE_MISMATCHED_PAIR;
            goto done;
        }
        if (!exclusive_level(r, t)) {
            finding = RSC_EVIDENCE_SHARED_IRQ;
            goto done;
        }
        for (j = 0; j < cnt; ++j) {
            if (j != i && translated[j].kind == RSC_EVIDENCE_INTERRUPT &&
                translated[j].vector == t->vector) {
                finding = RSC_EVIDENCE_AMBIGUOUS_IRQ;
                goto done;
            }
        }
        result.irq_idx = (uint32_t)i;
        result.win_vector = t->vector;
        result.win_level = t->level;
        result.has_unique_irq = 1;
    }
    finding = result.has_unique_irq ? RSC_EVIDENCE_PAIRED_CANDIDATE : RSC_EVIDENCE_NO_MATCHING_IRQ;
done:
    result.finding = finding;
    *out = result;
    return finding;
}
