#include "rsc_pnp_win.h"
#include "rsc_pnp_ownership.h"

/* an exact PnP desc is still not exclusive TCS ownership. */

NTSTATUS rsc_check_this_dev_translated_resources(_In_ WDFCMRESLIST translated_resources,
                                                 _Out_ BOOLEAN* has_dedicated_rsc_window)
{
    rsc_pnp_memory_t ranges[RSC_PNP_MAX_MEMORY];
    ULONG i, cnt, used = 0u, interrupts = 0u;
    rsc_pnp_result_t r;
    if (!has_dedicated_rsc_window)
        return STATUS_INVALID_PARAMETER;
    *has_dedicated_rsc_window = FALSE;
    if (!translated_resources || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    cnt = WdfCmResourceListGetCount(translated_resources);
    for (i = 0; i < cnt; ++i) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc =
            WdfCmResourceListGetDescriptor(translated_resources, i);
        if (!desc)
            return STATUS_INVALID_DEVICE_STATE;
        if (desc->Type == CmResourceTypeMemory) {
            /* an exact physical range with shared/unspecified disposition is not an exclusive OS
             * allocation. The new d0 acquisition route is allowed only for an explicit PnP
             * device-exclusive assignment; unrelated PEP0 ranges do not need to be exclusive. */

            {
                uint64_t start = (uint64_t)desc->u.Memory.Start.QuadPart;
                uint64_t length = (uint64_t)desc->u.Memory.Length;
                uint64_t target = RSC_DRV2_PNP_BASE;
                uint64_t target_end = target + RSC_DRV2_PNP_SIZE;
                if (start < target_end && length && length <= UINT64_MAX - start &&
                    start + length > target &&
                    desc->ShareDisposition != CmResourceShareDeviceExclusive)
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            if (used == RSC_PNP_MAX_MEMORY)
                return STATUS_BUFFER_TOO_SMALL;
            if (desc->u.Memory.Start.QuadPart < 0)
                return STATUS_INVALID_PARAMETER;
            ranges[used].start = (uint64_t)desc->u.Memory.Start.QuadPart;
            ranges[used].length = desc->u.Memory.Length;
            ++used;
        } else if (desc->Type == CmResourceTypeInterrupt) {
            if (interrupts == 0xffffffffu)
                return STATUS_INVALID_PARAMETER;
            ++interrupts;
        }
    }
    r = rsc_check_translated_assignment(ranges, used, interrupts);
    if (r == RSC_PNP_ASSIGNED_MEMORY) {
        *has_dedicated_rsc_window = TRUE;
        return STATUS_SUCCESS;
    }

    if (r == RSC_PNP_MISSING)
        return STATUS_SUCCESS;
    if (r == RSC_PNP_NO_INTERRUPT)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    return STATUS_INVALID_DEVICE_STATE;
}
