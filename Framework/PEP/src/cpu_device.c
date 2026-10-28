#include "cpu_device.h"
#include "cpu_topology.h"
#include "soc_model.h"
#include "dcvs.h"
#include <pepfx.h>

typedef struct cpu_device {
    WCHAR path[CPU_PATH_MAX];
    UNICODE_STRING id;
    volatile LONG prepared;
    volatile LONG registered;
} cpu_device_t;

static cpu_device_t devices[MOLOKAI_CPU_CNT];
static BOOLEAN identities_ready;

NTSTATUS cpu_device_init(VOID)
{
    cpu_topology_t* topology;
    void* buffer = NULL;
    ULONG bytes = 0u;
    NTSTATUS status;
    BOOLEAN assigned[MOLOKAI_CPU_CNT] = {0};
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || identities_ready)
        return STATUS_INVALID_DEVICE_STATE;
    topology = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*topology), POOL_TAG);
    if (!topology)
        return STATUS_INSUFFICIENT_RESOURCES;
    status = registry_query_resource(L"CCGP", &buffer, &bytes);
    if (!NT_SUCCESS(status))
        goto done;
    status = STATUS_DEVICE_CONFIGURATION_ERROR;
    if (!cpu_topology_decode(buffer, bytes, topology) || topology->count != MOLOKAI_CPU_CNT)
        goto done;
    for (ULONG i = 0u; i < topology->count; ++i) {
        const cpu_identity_t* identity = &topology->cpus[i];
        ULONG index;
        for (index = 0u; index < MOLOKAI_CPU_CNT; ++index)
            if (molokai_domain_for_cpu(index) == identity->domain &&
                molokai_core_for_cpu(index) == identity->core)
                break;
        if (index == MOLOKAI_CPU_CNT || assigned[index])
            goto done;
        assigned[index] = TRUE;
        for (ULONG j = 0u; identity->path[j]; ++j)
            devices[index].path[j] = (WCHAR)identity->path[j];
        RtlInitUnicodeString(&devices[index].id, devices[index].path);
    }
    identities_ready = TRUE;
    status = STATUS_SUCCESS;
done:
    if (buffer)
        ExFreePool(buffer);
    ExFreePool(topology);
    return status;
}

static cpu_device_t* find_id(PCUNICODE_STRING id)
{
    if (!identities_ready || !id || !id->Buffer || (id->Length & 1u) ||
        id->Length > id->MaximumLength)
        return NULL;
    for (ULONG i = 0u; i < MOLOKAI_CPU_CNT; ++i)
        if (RtlEqualUnicodeString(id, &devices[i].id, TRUE))
            return &devices[i];
    return NULL;
}

static ULONG handle_index(PVOID handle)
{
    if (identities_ready && handle)
        for (ULONG i = 0u; i < MOLOKAI_CPU_CNT; ++i)
            if (handle == &devices[i])
                return i;
    return MAXULONG;
}

ULONG cpu_device_index(PVOID handle)
{
    ULONG index = handle_index(handle);
    return index != MAXULONG && InterlockedCompareExchange(&devices[index].registered, 0, 0)
               ? index
               : MAXULONG;
}

static BOOLEAN registration_valid(PPEP_DEVICE_REGISTER registration)
{
    PPEP_COMPONENT component;
    if (!registration || registration->ComponentCount > 1u)
        return FALSE;
    if (!registration->ComponentCount)
        return TRUE;
    /* The processor driver registers an F0 component with PoFx. */
    component = registration->Components[0];
    return component && !component->Flags && !component->DeepestWakeableIdleState &&
           component->IdleStateCount == 1u && component->IdleStates;
}

BOOLEAN cpu_device_notify(ULONG notify, PVOID data, BOOLEAN* result)
{
    cpu_device_t* cpu = NULL;
    if (!data || !result || !identities_ready)
        return FALSE;
    *result = FALSE;
    switch (notify) {
        case PEP_DPM_PREPARE_DEVICE: {
            PPEP_PREPARE_DEVICE request = data;
            cpu = find_id(request->DeviceId);
            if (!cpu)
                return FALSE;
            request->DeviceAccepted = dcvs_ready();
            if (request->DeviceAccepted)
                InterlockedExchange(&cpu->prepared, 1);
            *result = TRUE;
            return TRUE;
        }
        case PEP_DPM_ABANDON_DEVICE: {
            PPEP_ABANDON_DEVICE request = data;
            cpu = find_id(request->DeviceId);
            if (!cpu)
                return FALSE;
            InterlockedExchange(&cpu->prepared, 0);
            request->DeviceAccepted = TRUE;
            *result = TRUE;
            return TRUE;
        }
        case PEP_DPM_REGISTER_DEVICE: {
            PPEP_REGISTER_DEVICE request = data;
            cpu = find_id(request->DeviceId);
            if (!cpu)
                return FALSE;
            request->DeviceAccepted = PepDeviceNotAccepted;
            request->DeviceHandle = NULL;
            if (dcvs_ready() && request->KernelHandle && registration_valid(request->Register) &&
                InterlockedCompareExchange(&cpu->prepared, 0, 0) &&
                InterlockedCompareExchange(&cpu->registered, 1, 0) == 0) {
                request->DeviceHandle = (PEPHANDLE)cpu;
                request->DeviceAccepted = PepDeviceAccepted;
            }
            *result = TRUE;
            return TRUE;
        }
        case PEP_DPM_UNREGISTER_DEVICE: {
            PPEP_UNREGISTER_DEVICE request = data;
            ULONG index = handle_index(request->DeviceHandle);
            if (index == MAXULONG)
                return FALSE;
            InterlockedExchange(&devices[index].registered, 0);
            *result = TRUE;
            return TRUE;
        }
        case PEP_DPM_REGISTER_CRASHDUMP_DEVICE:
            return handle_index(((PPEP_REGISTER_CRASHDUMP_DEVICE)data)->DeviceHandle) != MAXULONG;
        case PEP_DPM_DEVICE_POWER_STATE:
        case PEP_DPM_POWER_CONTROL_REQUEST:
        case PEP_DPM_POWER_CONTROL_COMPLETE:
        case PEP_DPM_DEVICE_STARTED:
        case PEP_DPM_NOTIFY_COMPONENT_IDLE_STATE:
        case PEP_DPM_DEVICE_IDLE_CONSTRAINTS:
        case PEP_DPM_COMPONENT_IDLE_CONSTRAINTS:
        case PEP_DPM_QUERY_COMPONENT_PERF_CAPABILTIES:
        case PEP_DPM_QUERY_COMPONENT_PERF_SET:
        case PEP_DPM_QUERY_COMPONENT_PERF_SET_NAME:
        case PEP_DPM_QUERY_COMPONENT_PERF_STATES:
        case PEP_DPM_REGISTER_COMPONENT_PERF_STATES:
        case PEP_DPM_REQUEST_COMPONENT_PERF_STATE:
        case PEP_DPM_QUERY_CURRENT_COMPONENT_PERF_STATE:
            /* These notification structures start with DeviceHandle. */
            return handle_index(*(PVOID*)data) != MAXULONG;
        default:
            return FALSE;
    }
}
