/* exactly paired raw/translated Windows PnP descriptors for the same FDO. Neither nor the
 * pure core maps or writes hardware. */

#include "oem_rsc_pnp_evidence_win.h"

static int convert(PCM_PARTIAL_RESOURCE_DESCRIPTOR d, rsc_evidence_desc_t* out)
{
    RtlZeroMemory(out, sizeof(*out));
    if (!d)
        return 0;
    if (d->Type == CmResourceTypeMemory) {
        if (d->u.Memory.Start.QuadPart < 0)
            return 0;
        out->kind = RSC_EVIDENCE_MEMORY;
        out->base = (uint64_t)d->u.Memory.Start.QuadPart;
        out->bytes = d->u.Memory.Length;
    } else if (d->Type == CmResourceTypeInterrupt) {
        out->kind = RSC_EVIDENCE_INTERRUPT;
        out->message = (d->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
        out->flags = d->Flags;
        out->share = (uint8_t)d->ShareDisposition;
        out->vector = d->u.Interrupt.Vector;
        out->level = d->u.Interrupt.Level;
    } else
        out->kind = RSC_EVIDENCE_OTHER;
    return 1;
}

rsc_evidence_finding_t rsc_audit_win_pnp_evidence(WDFCMRESLIST raw, WDFCMRESLIST translated,
                                                  uint64_t generation,
                                                  rsc_evidence_report_t* report)
{
    rsc_evidence_desc_t r[RSC_EVIDENCE_MAX];
    rsc_evidence_desc_t t[RSC_EVIDENCE_MAX];
    ULONG i, cnt;
    if (!report)
        return RSC_EVIDENCE_INVALID;
    RtlZeroMemory(report, sizeof(*report));
    if (!raw || !translated || KeGetCurrentIrql() != PASSIVE_LEVEL || !generation)
        return RSC_EVIDENCE_INVALID;
    cnt = WdfCmResourceListGetCount(raw);
    if (cnt != WdfCmResourceListGetCount(translated) || cnt > RSC_EVIDENCE_MAX)
        return RSC_EVIDENCE_INVALID;
    for (i = 0; i < cnt; ++i) {
        if (!convert(WdfCmResourceListGetDescriptor(raw, i), &r[i]) ||
            !convert(WdfCmResourceListGetDescriptor(translated, i), &t[i]))
            return RSC_EVIDENCE_INVALID;
    }
    return rsc_audit_pnp_evidence(r, t, cnt, generation, report);
}
