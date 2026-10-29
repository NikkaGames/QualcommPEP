#include "lpi.h"
#include "acpi.h"
#include "cpu_device.h"
#include "soc_transport.h"
#include "hw_backend_windows_wdf.h"
#include "hw_backend.h"
#include <pepfx.h>
#include "trace.h"
#include "lpi.tmh"

#define LPI_PROCESSORS 64u
#define LPI_STATES 32u
#define LPI_CONFIG ((LONG64)MINLONGLONG)
#define LPI_READER ((LONG64)1 << 32)
#define LPI_IO_CLOSED 0x40000000L

typedef struct lpi_dependency {
    ULONG processor;
    ULONG options;
} lpi_dependency_t;

typedef struct lpi_state {
    BOOLEAN enabled;
    ULONG dependencies;
    lpi_dependency_t dependency[LPI_PROCESSORS];
    ULONGLONG processors;
    volatile LONG phase;
    BOOLEAN platform;
    BOOLEAN owns_io;
    uint64_t backend_serial;
    ULONGLONG start;
    ULONGLONG requests;
    ULONGLONG elapsed;
} lpi_state_t;

typedef struct lpi_catalog {
    ULONG count;
    lpi_state_t states[LPI_STATES];
} lpi_catalog_t;

static volatile LONG enabled;
static volatile LONG advertised;
static volatile LONG firmware_ready;
static volatile LONG firmware_initialized;
static KMUTEX configuration_lock;
static volatile LONG io_users;
static volatile LONG64 sessions;
static ULONG processor_count;
static ULONG processor_states[LPI_PROCESSORS];
static lpi_catalog_t* catalog;
static volatile LONG rejected_entries;
static volatile LONG ignored_completions;
static volatile LONG resume_failures;

BOOLEAN lpi_ready(VOID)
{
    return InterlockedCompareExchange(&enabled, 0, 0) != 0;
}

BOOLEAN lpi_io_enter(VOID)
{
    LONG value = InterlockedCompareExchange(&io_users, 0, 0);
    for (;;) {
        if (value < 0 || value >= LPI_IO_CLOSED - 1)
            return FALSE;
        LONG prior = InterlockedCompareExchange(&io_users, value + 1, value);
        if (prior == value)
            return TRUE;
        value = prior;
    }
}

VOID lpi_io_leave(VOID)
{
    InterlockedDecrement(&io_users);
}

BOOLEAN lpi_io_exclusive(VOID)
{
    return InterlockedCompareExchange(&io_users, 0, 0) == LPI_IO_CLOSED;
}

VOID lpi_io_acquire(VOID)
{
    LARGE_INTEGER delay;
    delay.QuadPart = -10000;
    while (!lpi_io_enter())
        (VOID) KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

VOID lpi_mutex_acquire(PKMUTEX mutex)
{
    lpi_io_acquire();
    (VOID) KeWaitForSingleObject(mutex, Executive, KernelMode, FALSE, NULL);
}

VOID lpi_mutex_release(PKMUTEX mutex)
{
    (VOID) KeReleaseMutex(mutex, FALSE);
    lpi_io_leave();
}

VOID lpi_init(ULONG processors)
{
    if (processors && processors <= LPI_PROCESSORS &&
        registry_query_u_long(L"CpuIdleEnable", 1u) == 1u) {
        processor_count = processors;
        KeInitializeMutex(&configuration_lock, 0);
        InterlockedExchange(&enabled, 1);
    }
}

NTSTATUS lpi_firmware_init(WDFDEVICE device)
{
    NTSTATUS status;
    if (!lpi_ready())
        return STATUS_SUCCESS;
    if (!device || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    InterlockedExchange(&firmware_ready, 0);
    if (!hw_backend_ready())
        return STATUS_DEVICE_NOT_READY;
    status = acpi_lpi_init(device);
    if (NT_SUCCESS(status)) {
        InterlockedExchange(&firmware_initialized, 1);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PPM, "lpi: firmware initialization acknowledged");
        status = hw_backend_win_sleep_bind(device);
        if (NT_SUCCESS(status))
            InterlockedExchange(&firmware_ready, 1);
    }
    return status;
}

VOID lpi_firmware_resume(WDFDEVICE device)
{
    if (InterlockedCompareExchange(&firmware_initialized, 0, 0)) {
        NTSTATUS status = lpi_firmware_init(device);
        if (!NT_SUCCESS(status))
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_PPM,
                        "lpi: D0 initialization failed status=%08X", status);
    }
}

static BOOLEAN read_enter(VOID)
{
    LONG64 value = InterlockedCompareExchange64(&sessions, 0, 0);
    for (;;) {
        if (value < 0 || (value >> 32) >= 0x10000)
            return FALSE;
        LONG64 prior = InterlockedCompareExchange64(&sessions, value + LPI_READER, value);
        if (prior == value)
            return TRUE;
        value = prior;
    }
}

static VOID read_leave(VOID)
{
    InterlockedAdd64(&sessions, -LPI_READER);
}

static VOID release_states(ULONG mask)
{
    InterlockedAnd64(&sessions, ~(LONG64)mask);
}

static BOOLEAN configure_begin(VOID)
{
    (VOID) KeWaitForSingleObject(&configuration_lock, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange64(&sessions, LPI_CONFIG, 0) == 0)
        return TRUE;
    (VOID) KeReleaseMutex(&configuration_lock, FALSE);
    return FALSE;
}

static VOID configure_end(VOID)
{
    InterlockedExchange64(&sessions, 0);
    (VOID) KeReleaseMutex(&configuration_lock, FALSE);
}

/* The ACPI dependency options retain OS state indices, including empty menus. */
static BOOLEAN coverage(lpi_catalog_t* data, ULONG index, ULONG visiting, ULONG* done)
{
    lpi_state_t* state = &data->states[index];
    ULONG bit = 1u << index;
    if (*done & bit)
        return TRUE;
    if (visiting & bit)
        return FALSE;
    for (ULONG i = 0u; i < state->dependencies; ++i) {
        const lpi_dependency_t* dep = &state->dependency[i];
        if (dep->processor != MAXULONG) {
            state->processors |= (ULONGLONG)1 << dep->processor;
            continue;
        }
        for (ULONG j = 0u; j < data->count; ++j) {
            if (!(dep->options & (1u << j)))
                continue;
            if (!coverage(data, j, visiting | bit, done))
                return FALSE;
            state->processors |= data->states[j].processors;
        }
    }
    *done |= bit;
    return TRUE;
}

static BOOLEAN processor_description(ULONG cpu, PPEP_PPM_LPI_PROCESSOR_STATES data)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || cpu >= processor_count ||
        !data->Count || data->Count > LPI_STATES)
        return FALSE;
    if (!configure_begin())
        return FALSE;
    /* Windows owns the entry registers, architectural flags and context restoration. */
    processor_states[cpu] = data->Count;
    configure_end();
    return TRUE;
}

static BOOLEAN coordinated_description(PPEP_PPM_LPI_COORDINATED_STATES data)
{
    lpi_catalog_t* next;
    lpi_catalog_t* previous;
    ULONG done = 0u;
    BOOLEAN valid = FALSE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || data->Count > LPI_STATES)
        return FALSE;
    next = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*next), POOL_TAG);
    if (!next)
        return FALSE;
    if (!configure_begin()) {
        ExFreePool(next);
        return FALSE;
    }
    next->count = data->Count;
    for (ULONG i = 0u; i < next->count; ++i) {
        PPEP_PPM_COORDINATED_LPI_STATE source = &data->LpiStates[i];
        lpi_state_t* state = &next->states[i];
        if (source->DependencyCount > LPI_PROCESSORS ||
            (source->DependencyCount && !source->Dependencies))
            goto done;
        state->enabled = source->Flags.Enabled != 0u;
        state->dependencies = source->DependencyCount;
        for (ULONG j = 0u; j < source->DependencyCount; ++j) {
            PPEP_PPM_LPI_COORDINATED_DEPENDENCY input = &source->Dependencies[j];
            lpi_dependency_t* dep = &state->dependency[j];
            dep->processor = input->TargetProcessor
                                 ? cpu_device_dependency(input->TargetProcessor)
                                 : MAXULONG;
            if ((input->TargetProcessor && dep->processor >= processor_count) ||
                input->OptionCount > LPI_STATES || (input->OptionCount && !input->Options))
                goto done;
            ULONG count = input->TargetProcessor ? processor_states[dep->processor] : next->count;
            for (ULONG k = 0u; k < input->OptionCount; ++k) {
                ULONG option = input->Options[k];
                if (option >= count || (dep->options & (1u << option)))
                    goto done;
                dep->options |= 1u << option;
            }
        }
    }
    for (ULONG i = 0u; i < next->count; ++i) {
        if (!coverage(next, i, 0u, &done))
            goto done;
        ULONGLONG all = MAXULONGLONG >> (64u - processor_count);
        next->states[i].platform = next->states[i].processors == all;
    }
    previous = catalog;
    catalog = next;
    next = previous;
    valid = TRUE;
done:
    configure_end();
    if (next)
        ExFreePool(next);
    TraceEvents(valid ? TRACE_LEVEL_INFORMATION : TRACE_LEVEL_WARNING, TRACE_PPM,
                "lpi: coordinated description states=%lu accepted=%u", data->Count, valid);
    return valid;
}

static NTSTATUS prepare(PPEP_PPM_LPI_PRE_EXECUTE request)
{
    ULONG mask = 0u;
    ULONG root = MAXULONG;
    LONG64 value;
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    BOOLEAN fenced = FALSE;
    uint64_t serial = 0u;
    if (!read_enter())
        return STATUS_DEVICE_BUSY;
    if (!InterlockedCompareExchange(&enabled, 0, 0)) {
        status = STATUS_DEVICE_NOT_READY;
        goto done;
    }
    if (!catalog || request->CoordinatedStateCount > catalog->count ||
        (request->CoordinatedStateCount && !request->CoordinatedStates))
        goto done;
    for (ULONG i = 0u; i < request->CoordinatedStateCount; ++i) {
        ULONG index = request->CoordinatedStates[i];
        if (index >= catalog->count || !catalog->states[index].enabled ||
            (mask & (1u << index)))
            goto done;
        mask |= 1u << index;
        if (catalog->states[index].platform) {
            if (root != MAXULONG)
                goto done;
            root = index;
        }
    }
    if (root != MAXULONG && !InterlockedCompareExchange(&firmware_ready, 0, 0)) {
        status = STATUS_DEVICE_NOT_READY;
        goto done;
    }
    value = InterlockedCompareExchange64(&sessions, 0, 0);
    for (;;) {
        if ((ULONG)value & mask) {
            status = STATUS_DEVICE_BUSY;
            goto done;
        }
        LONG64 prior = InterlockedCompareExchange64(&sessions, value | mask, value);
        if (prior == value)
            break;
        value = prior;
    }
    if (root != MAXULONG) {
        if (InterlockedCompareExchange(&io_users, LPI_IO_CLOSED, 0) != 0) {
            status = STATUS_DEVICE_BUSY;
            goto rollback;
        }
        fenced = TRUE;
        if (!hw_backend_idle_begin(&serial) || !hw_backend_win_idle_quiescent() ||
            !molokai_transport_idle_begin()) {
            status = STATUS_DEVICE_BUSY;
            goto rollback;
        }
        if (!hw_backend_win_sleep_enter()) {
            molokai_transport_idle_end();
            status = STATUS_DEVICE_BUSY;
            goto rollback;
        }
    }
    for (ULONG i = 0u; i < catalog->count; ++i) {
        if (!(mask & (1u << i)))
            continue;
        lpi_state_t* state = &catalog->states[i];
        state->start = KeQueryInterruptTime();
        state->owns_io = i == root;
        state->backend_serial = state->owns_io ? serial : 0u;
        ++state->requests;
        InterlockedExchange(&state->phase, 1);
    }
    status = STATUS_SUCCESS;
    goto done;
rollback:
    if (serial && !hw_backend_idle_end(serial))
        InterlockedExchange(&enabled, 0);
    if (fenced)
        InterlockedExchange(&io_users, 0);
    release_states(mask);
done:
    if (!NT_SUCCESS(status))
        InterlockedIncrement(&rejected_entries);
    read_leave();
    return status;
}

static VOID complete(PPEP_PPM_LPI_COMPLETE request)
{
    if (!read_enter())
        return;
    if (!catalog || request->CoordinatedStateCount > catalog->count ||
        (request->CoordinatedStateCount && !request->CoordinatedStates)) {
        InterlockedIncrement(&ignored_completions);
        goto done;
    }
    for (ULONG i = 0u; i < request->CoordinatedStateCount; ++i) {
        ULONG index = request->CoordinatedStates[i];
        if (index >= catalog->count) {
            InterlockedIncrement(&ignored_completions);
            continue;
        }
        lpi_state_t* state = &catalog->states[index];
        if (InterlockedCompareExchange(&state->phase, 2, 1) != 1) {
            /* Completions after a declined entry have no active resource lease. */
            InterlockedIncrement(&ignored_completions);
            continue;
        }
        if (state->owns_io) {
            BOOLEAN restored = hw_backend_win_sleep_exit();
            BOOLEAN quiescent = hw_backend_win_idle_quiescent();
            BOOLEAN reopened = hw_backend_idle_end(state->backend_serial) != 0;
            molokai_transport_idle_end();
            if (!restored || !quiescent || !reopened) {
                InterlockedIncrement(&resume_failures);
                InterlockedExchange(&enabled, 0);
            }
            state->backend_serial = 0u;
            state->owns_io = FALSE;
            InterlockedExchange(&io_users, 0);
        }
        /* This measures the callback interval, not hardware DRIPS residency. */
        state->elapsed += KeQueryInterruptTime() - state->start;
        InterlockedExchange(&state->phase, 0);
        release_states(1u << index);
    }
done:
    read_leave();
}

BOOLEAN lpi_notify(PVOID processor, ULONG notification, PVOID data, BOOLEAN* accepted)
{
    if (!accepted)
        return FALSE;
    *accepted = FALSE;
    switch (notification) {
        case PEP_NOTIFY_PPM_LPI_SUPPORTED:
        case PEP_NOTIFY_PPM_LPI_PROCESSOR_STATES:
        case PEP_NOTIFY_PPM_LPI_COORDINATED_STATES:
        case PEP_NOTIFY_PPM_LPI_PRE_EXECUTE:
        case PEP_NOTIFY_PPM_LPI_COMPLETE:
            break;
        default:
            return FALSE;
    }
    if (data && notification == PEP_NOTIFY_PPM_LPI_PRE_EXECUTE &&
        InterlockedCompareExchange(&advertised, 0, 0) && !lpi_ready()) {
        ((PPEP_PPM_LPI_PRE_EXECUTE)data)->Status = STATUS_DEVICE_NOT_READY;
        *accepted = TRUE;
        return TRUE;
    }
    if (!data || (!InterlockedCompareExchange(&enabled, 0, 0) &&
                  notification != PEP_NOTIFY_PPM_LPI_COMPLETE))
        return TRUE;
    switch (notification) {
        case PEP_NOTIFY_PPM_LPI_SUPPORTED:
            ((PPEP_PPM_LPI_SUPPORTED)data)->OverrideLpiStates = FALSE;
            InterlockedExchange(&advertised, 1);
            *accepted = TRUE;
            break;
        case PEP_NOTIFY_PPM_LPI_PROCESSOR_STATES:
            *accepted = processor_description(cpu_device_index(processor), data);
            break;
        case PEP_NOTIFY_PPM_LPI_COORDINATED_STATES:
            *accepted = coordinated_description(data);
            break;
        case PEP_NOTIFY_PPM_LPI_PRE_EXECUTE:
            ((PPEP_PPM_LPI_PRE_EXECUTE)data)->Status = prepare(data);
            *accepted = TRUE;
            break;
        case PEP_NOTIFY_PPM_LPI_COMPLETE:
            complete(data);
            *accepted = TRUE;
            break;
    }
    return TRUE;
}

VOID lpi_stop(VOID)
{
    InterlockedExchange(&enabled, 0);
    InterlockedExchange(&firmware_ready, 0);
    InterlockedExchange(&firmware_initialized, 0);
    /* Active firmware transitions retain their resident catalog until completion. */
    if (InterlockedCompareExchange64(&sessions, LPI_CONFIG, 0) == 0) {
        lpi_catalog_t* previous = catalog;
        catalog = NULL;
        if (previous)
            ExFreePool(previous);
        InterlockedExchange64(&sessions, 0);
    }
}
