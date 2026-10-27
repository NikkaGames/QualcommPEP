#include "rsc_irq_pnp_win.h"
#include "trace.h"
#include "rsc_irq_pnp_win.tmh"

rsc_irq_report_t rsc_inspect_irq_resources(_In_ WDFCMRESLIST resources_raw,
                                           _In_ WDFCMRESLIST resources_translated)
{
    rsc_irq_report_t report = {0};
    rsc_irq_pair_t pairs[RSC_IRQ_RES_LIMIT] = {0};
    ULONG raw_cnt, translated_cnt, i;
    report.candidate_idx = UINT32_MAX;
    report.finding = RSC_IRQ_MALFORMED;
    if (!resources_raw || !resources_translated || KeGetCurrentIrql() != PASSIVE_LEVEL) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: PnP IRQ observation invalid reason=resources-or-IRQL");
        return report;
    }
    raw_cnt = WdfCmResourceListGetCount(resources_raw);
    translated_cnt = WdfCmResourceListGetCount(resources_translated);
    if (raw_cnt != translated_cnt || raw_cnt > RSC_IRQ_RES_LIMIT) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: PnP IRQ count invalid raw=%lu translated=%lu", raw_cnt, translated_cnt);
        return report;
    }
    for (i = 0; i < raw_cnt; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw = WdfCmResourceListGetDescriptor(resources_raw, i);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR trans =
            WdfCmResourceListGetDescriptor(resources_translated, i);
        rsc_irq_pair_t* p = &pairs[i];
        if (!raw || !trans) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: PnP IRQ descriptor missing index=%lu", i);
            return report;
        }
        p->raw_is_interrupt = raw->Type == CmResourceTypeInterrupt;
        if (p->raw_is_interrupt) {
            p->raw_flags = raw->Flags;
            p->raw_share_disposition = raw->ShareDisposition;
        }
        p->translated_is_interrupt = trans->Type == CmResourceTypeInterrupt;
        if (p->translated_is_interrupt) {
            p->translated_flags = trans->Flags;
            p->translated_share_disposition = trans->ShareDisposition;
        }
        if (!p->raw_is_interrupt && !p->translated_is_interrupt)
            continue;
        p->raw_is_message =
            p->raw_is_interrupt && (raw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
        p->translated_is_message =
            p->translated_is_interrupt && (trans->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
        if (p->raw_is_interrupt && !p->raw_is_message) {
            p->raw_vector = raw->u.Interrupt.Vector;
            p->raw_level = raw->u.Interrupt.Level;
        }
        if (p->translated_is_interrupt && !p->translated_is_message) {
            p->translated_vector = trans->u.Interrupt.Vector;
            p->translated_level = trans->u.Interrupt.Level;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: PnP IRQ index=%lu raw=[%lu/%lu/%u] translated=[%lu/%lu/%u] flags=%X/%X "
                    "share=%u/%u observation-only=1",
                    i, p->raw_vector, p->raw_level, p->raw_is_message, p->translated_vector,
                    p->translated_level, p->translated_is_message, (ULONG)raw->Flags,
                    (ULONG)trans->Flags, (unsigned)p->raw_share_disposition,
                    (unsigned)p->translated_share_disposition);
    }
    report = rsc_analyze_irq_pairs(pairs, raw_cnt);
    if (report.finding == RSC_IRQ_UNIQUE_RAW_CANDIDATE) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: IRQ37 PnP index=%lu vector=%lu level=%lu ownership=0",
                    report.candidate_idx, report.win_vector, report.win_level);
    } else {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: IRQ37 PnP match failed result=%lu",
                    (ULONG)report.finding);
    }
    return report;
}

/* the caller must guarantee no concurrent ReleaseHardware. No hardware action or WDF
 * interrupt attach. */

int rsc_select_irq_candidate_descriptors(_In_ WDFCMRESLIST resources_raw,
                                         _In_ WDFCMRESLIST resources_translated,
                                         _In_ const rsc_irq_snapshot_t* snapshot,
                                         uint64_t expected_generation,
                                         _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR* raw_out,
                                         _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR* translated_out)
{
    rsc_irq_report_t report;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR raw, trans;
    ULONG nr, nt;
    if (raw_out)
        *raw_out = NULL;
    if (translated_out)
        *translated_out = NULL;
    if (!raw_out || !translated_out || !resources_raw || !resources_translated ||
        KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !rsc_irq_snapshot_read(snapshot, expected_generation, &report))
        return 0;
    nr = WdfCmResourceListGetCount(resources_raw);
    nt = WdfCmResourceListGetCount(resources_translated);
    if (nr != nt || report.candidate_idx >= nr || nr > RSC_IRQ_RES_LIMIT)
        return 0;
    raw = WdfCmResourceListGetDescriptor(resources_raw, report.candidate_idx);
    trans = WdfCmResourceListGetDescriptor(resources_translated, report.candidate_idx);
    if (!raw || !trans || raw->Type != CmResourceTypeInterrupt ||
        trans->Type != CmResourceTypeInterrupt ||
        (raw->Flags & (CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED)) ||
        (trans->Flags & (CM_RESOURCE_INTERRUPT_MESSAGE | CM_RESOURCE_INTERRUPT_LATCHED)) ||
        raw->ShareDisposition != trans->ShareDisposition ||
        raw->u.Interrupt.Vector != RSC_IRQ_EXPECTED_GIC_INTID ||
        raw->u.Interrupt.Vector != report.raw_vector ||
        raw->u.Interrupt.Level != report.raw_level || raw->Flags != report.raw_flags ||
        trans->Flags != report.translated_flags ||
        raw->ShareDisposition != report.raw_share_disposition ||
        trans->ShareDisposition != report.translated_share_disposition ||
        trans->u.Interrupt.Vector != report.win_vector ||
        trans->u.Interrupt.Level != report.win_level)
        return 0;
    /* validate again before exposing a pair, instead of assuming an earlier scan cannot have
     * gone stale. */

    for (ULONG i = 0; i < nr; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR other_raw, other_trans;
        if (i == report.candidate_idx)
            continue;
        other_raw = WdfCmResourceListGetDescriptor(resources_raw, i);
        other_trans = WdfCmResourceListGetDescriptor(resources_translated, i);
        if (!other_raw || !other_trans ||
            (other_raw->Type == CmResourceTypeInterrupt) !=
                (other_trans->Type == CmResourceTypeInterrupt))
            return 0;
        if (other_raw->Type == CmResourceTypeInterrupt &&
            ((!(other_raw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
              other_raw->u.Interrupt.Vector == RSC_IRQ_EXPECTED_GIC_INTID) ||
             (!(other_trans->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
              other_trans->u.Interrupt.Vector == report.win_vector)))
            return 0;
    }
    *raw_out = raw;
    *translated_out = trans;
    return 1;
}
