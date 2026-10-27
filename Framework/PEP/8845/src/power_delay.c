#include "power_delay.h"

NTSTATUS power_delay_ms(uint32_t ms)
{
    LARGE_INTEGER interval;
    if (ms > 5000u)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (ms == 0u)
        return STATUS_SUCCESS;

    interval.QuadPart = -(LONGLONG)ms * 10000LL;
    return KeDelayExecutionThread(KernelMode, FALSE, &interval);
}
