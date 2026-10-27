#include "rpmh_oem_pnp_win.h"

/* this PnP adapter is not connected to the production path without an independently verified
 * controller request table. */

NTSTATUS rpmh_oem_plan_win_interrupts(_In_ WDFCMRESLIST raw, _In_ WDFCMRESLIST translated,
                                      _In_reads_(req_cnt) const rpmh_oem_pnp_req_t* requests,
                                      _In_ ULONG req_cnt, _Out_ rpmh_oem_pnp_plan_t* plan)
{
    rsc_irq_pair_t pairs[RSC_IRQ_RES_LIMIT] = {0};
    ULONG raw_cnt, translated_cnt, i;
    if (!raw || !translated || !requests || !plan)
        return STATUS_INVALID_PARAMETER;
    raw_cnt = WdfCmResourceListGetCount(raw);
    translated_cnt = WdfCmResourceListGetCount(translated);
    if (!raw_cnt || raw_cnt != translated_cnt || raw_cnt > RSC_IRQ_RES_LIMIT)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    for (i = 0; i < raw_cnt; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = WdfCmResourceListGetDescriptor(raw, i);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR t = WdfCmResourceListGetDescriptor(translated, i);
        if (!r || !t)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        if (r->Type == CmResourceTypeInterrupt) {
            pairs[i].raw_is_interrupt = 1;
            pairs[i].raw_is_message = (r->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
            pairs[i].raw_vector = r->u.Interrupt.Vector;
            pairs[i].raw_level = r->u.Interrupt.Level;
            pairs[i].raw_flags = r->Flags;
            pairs[i].raw_share_disposition = (uint8_t)r->ShareDisposition;
        }
        if (t->Type == CmResourceTypeInterrupt) {
            pairs[i].translated_is_interrupt = 1;
            pairs[i].translated_is_message = (t->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;
            pairs[i].translated_vector = t->u.Interrupt.Vector;
            pairs[i].translated_level = t->u.Interrupt.Level;
            pairs[i].translated_flags = t->Flags;
            pairs[i].translated_share_disposition = (uint8_t)t->ShareDisposition;
        }
    }
    return rpmh_oem_build_pnp_plan(pairs, raw_cnt, requests, req_cnt, plan)
               ? STATUS_SUCCESS
               : STATUS_DEVICE_CONFIGURATION_ERROR;
}
