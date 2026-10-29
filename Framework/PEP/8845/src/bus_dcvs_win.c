#include "bus_dcvs_win.h"
#include "bwmon.h"
#include "hw_backend.h"
#include "hw_backend_windows_wdf.h"
#include "trace.h"
#include "bus_dcvs_win.tmh"

typedef struct bus_dcvs bus_dcvs_t;

typedef struct monitor_native {
    bus_dcvs_t* owner;
    bwmon_t monitor;
    bwmon_sample_t sample;
    PUCHAR map;
    SIZE_T bytes;
    ULONG local_offset;
    ULONG group;
    BOOLEAN vote_held;
    BOOLEAN available;
    BOOLEAN reported;
} monitor_native_t;

typedef struct monitor_irq {
    bus_dcvs_t* owner;
    WDFINTERRUPT interrupt;
    ULONG members;
    ULONG intid;
} monitor_irq_t;

typedef struct monitor_irq_context {
    monitor_irq_t* group;
} monitor_irq_context_t;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(monitor_irq_context_t, monitor_irq_context)

typedef struct monitor_work_context {
    bus_dcvs_t* owner;
} monitor_work_context_t;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(monitor_work_context_t, monitor_work_context)

struct bus_dcvs {
    WDFDEVICE dev;
    WDFWORKITEM work;
    WDFDPC dpc;
    monitor_native_t monitors[BWMON_COUNT];
    monitor_irq_t groups[BWMON_COUNT];
    ULONG group_count;
    volatile LONG running;
    volatile LONG pending;
    volatile LONG work_queued;
    uint64_t epoch;
    PUCHAR lagg_map;
    lagg_t lagg;
    BOOLEAN lagg_enabled;
};

static EVT_WDF_INTERRUPT_ISR monitor_isr;
static EVT_WDF_DPC monitor_dpc;
static EVT_WDF_WORKITEM monitor_work;

static ULONG setting(WDFDEVICE dev, PCWSTR name, ULONG fallback)
{
    WDFKEY device_key = NULL, config_key = NULL;
    UNICODE_STRING config, value;
    ULONG result = fallback;
    RtlInitUnicodeString(&config, L"DcvsConfig");
    RtlInitUnicodeString(&value, name);
    if (NT_SUCCESS(WdfDeviceOpenRegistryKey(dev, PLUGPLAY_REGKEY_DEVICE, KEY_READ,
                                            WDF_NO_OBJECT_ATTRIBUTES, &device_key))) {
        if (NT_SUCCESS(WdfRegistryOpenKey(device_key, &config, KEY_READ, WDF_NO_OBJECT_ATTRIBUTES,
                                          &config_key))) {
            (void)WdfRegistryQueryULong(config_key, &value, &result);
            WdfRegistryClose(config_key);
        }
        WdfRegistryClose(device_key);
    }
    return result;
}

static int monitor_owns(void* ctx, uint64_t epoch)
{
    monitor_native_t* m = ctx;
    return m && m->map && epoch && m->owner->epoch == epoch;
}

static int monitor_read(void* ctx, unsigned region, uint32_t offset, uint32_t* value)
{
    monitor_native_t* m = ctx;
    SIZE_T start = region ? 0u : m->local_offset;
    if (!m->map || !value || region > 1u || (offset & 3u) ||
        start + offset + sizeof(ULONG) > m->bytes)
        return 0;
    *value = READ_REGISTER_ULONG((PULONG)(m->map + start + offset));
    return 1;
}

static int monitor_write(void* ctx, unsigned region, uint32_t offset, uint32_t value)
{
    monitor_native_t* m = ctx;
    SIZE_T start = region ? 0u : m->local_offset;
    if (!m->map || region > 1u || (offset & 3u) || start + offset + sizeof(ULONG) > m->bytes)
        return 0;
    WRITE_REGISTER_ULONG((PULONG)(m->map + start + offset), value);
    return 1;
}

static VOID monitor_barrier(void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    KeMemoryBarrier();
}

static int lagg_owns(void* ctx, uint64_t epoch)
{
    bus_dcvs_t* owner = ctx;
    return owner && owner->lagg_map && epoch && owner->epoch == epoch;
}

static int lagg_read(void* ctx, unsigned region, uint32_t offset, uint32_t* value)
{
    bus_dcvs_t* owner = ctx;
    if (!owner->lagg_map || region || !value || (offset & 3u) || offset >= 0x400u)
        return 0;
    *value = READ_REGISTER_ULONG((PULONG)(owner->lagg_map + offset));
    return 1;
}

static int lagg_write(void* ctx, unsigned region, uint32_t offset, uint32_t value)
{
    bus_dcvs_t* owner = ctx;
    if (!owner->lagg_map || region || (offset & 3u) || offset >= 0x400u)
        return 0;
    WRITE_REGISTER_ULONG((PULONG)(owner->lagg_map + offset), value);
    return 1;
}

static VOID make_vote(power_action_t* action, const bwmon_target_t* target, uint64_t average,
                      uint64_t peak)
{
    RtlZeroMemory(action, sizeof(*action));
    action->kind = POWER_ICB;
    RtlStringCchCopyA(action->u.icb.master, RTL_NUMBER_OF(action->u.icb.master), target->master);
    RtlStringCchCopyA(action->u.icb.slave, RTL_NUMBER_OF(action->u.icb.slave), target->slave);
    action->u.icb.average = average;
    action->u.icb.peak =
        peak && peak < target->minimum_bytes_sec ? target->minimum_bytes_sec : peak;
}

static VOID monitor_update(bus_dcvs_t* owner, ULONG mask)
{
    uint32_t active = 0u;
    if (!InterlockedCompareExchange(&owner->running, 0, 0))
        return;
    if (owner->lagg_enabled && !lagg_sample(&owner->lagg, &active)) {
        owner->lagg_enabled = FALSE;
        (void)lagg_stop(&owner->lagg);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "bus: LAGG counter read failed");
    }
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        const bwmon_target_t* target = &bwmon_targets[i];
        uint64_t average, peak;
        power_action_t action;
        int ok;
        if (!(mask & (1u << i)) || !m->available)
            continue;
        ok =
            !m->monitor.faulted && bwmon_select(&m->monitor, &m->sample,
                                                (active & target->cpu_mask) != 0u, &average, &peak);
        if (ok && (average != m->monitor.average || peak != m->monitor.peak)) {
            make_vote(&action, target, average, peak);
            m->vote_held = TRUE;
            ok = hw_backend_win_bus_request(owner->epoch, target->client, &action,
                                            BUS_REQUEST_UPDATE);
            if (ok) {
                m->monitor.average = average;
                m->monitor.peak = peak;
            }
        }
        WdfInterruptAcquireLock(owner->groups[m->group].interrupt);
        if (ok && InterlockedCompareExchange(&owner->running, 0, 0))
            ok = bwmon_rearm(&m->monitor, m->monitor.peak);
        if (!ok) {
            (void)bwmon_stop(&m->monitor);
            m->available = FALSE;
        }
        WdfInterruptReleaseLock(owner->groups[m->group].interrupt);
        if (!ok && !m->reported) {
            m->reported = TRUE;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "bus: monitor=%s update failed; monitor stopped", target->name);
        }
    }
}

static VOID monitor_work(WDFWORKITEM item)
{
    bus_dcvs_t* owner = monitor_work_context(item)->owner;
    for (;;) {
        ULONG mask = (ULONG)InterlockedExchange(&owner->pending, 0);
        if (mask && InterlockedCompareExchange(&owner->running, 0, 0)) {
            monitor_update(owner, mask);
            continue;
        }
        InterlockedExchange(&owner->work_queued, 0);
        /* Either this worker drains the new sample or its DPC queues the next worker. */
        if (InterlockedCompareExchange(&owner->pending, 0, 0) &&
            InterlockedCompareExchange(&owner->running, 0, 0) &&
            InterlockedCompareExchange(&owner->work_queued, 1, 0) == 0)
            continue;
        return;
    }
}

static VOID monitor_dpc(WDFDPC dpc)
{
    bus_dcvs_t* owner = monitor_work_context(dpc)->owner;
    if (InterlockedCompareExchange(&owner->running, 0, 0) &&
        InterlockedCompareExchange(&owner->work_queued, 1, 0) == 0)
        WdfWorkItemEnqueue(owner->work);
}

static BOOLEAN monitor_isr(WDFINTERRUPT interrupt, ULONG message)
{
    monitor_irq_t* group = monitor_irq_context(interrupt)->group;
    bus_dcvs_t* owner = group->owner;
    BOOLEAN handled = FALSE;
    UNREFERENCED_PARAMETER(message);
    if (!InterlockedCompareExchange(&owner->running, 0, 0))
        return FALSE;
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        int result;
        if (!(group->members & (1u << i)) || !m->available || !m->monitor.running)
            continue;
        result = bwmon_capture(&m->monitor, &m->sample);
        if (!result)
            continue;
        InterlockedOr(&owner->pending, (LONG)(1u << i));
        handled = TRUE;
    }
    if (handled)
        (void)WdfDpcEnqueue(owner->dpc);
    return handled;
}

NTSTATUS bus_dcvs_prepare(WDFDEVICE dev, WDFCMRESLIST raw, WDFCMRESLIST translated)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_WORKITEM_CONFIG work_config;
    WDF_DPC_CONFIG dpc_config;
    bus_dcvs_t* owner;
    NTSTATUS status;
    if (!ctx || ctx->is_control_dev || setting(dev, L"DisableBWMonBusDCVS", 0u))
        return STATUS_SUCCESS;
    if (ctx->bus_dcvs || !raw || !translated ||
        WdfCmResourceListGetCount(raw) != WdfCmResourceListGetCount(translated))
        return STATUS_INVALID_DEVICE_STATE;
    owner = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*owner), POOL_TAG);
    if (!owner)
        return STATUS_INSUFFICIENT_RESOURCES;
    owner->dev = dev;
    ctx->bus_dcvs = owner;
    WDF_WORKITEM_CONFIG_INIT(&work_config, monitor_work);
    work_config.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, monitor_work_context_t);
    attributes.ParentObject = dev;
    status = WdfWorkItemCreate(&work_config, &attributes, &owner->work);
    if (!NT_SUCCESS(status))
        goto fail;
    monitor_work_context(owner->work)->owner = owner;
    WDF_DPC_CONFIG_INIT(&dpc_config, monitor_dpc);
    dpc_config.AutomaticSerialization = FALSE;
    status = WdfDpcCreate(&dpc_config, &attributes, &owner->dpc);
    if (!NT_SUCCESS(status))
        goto fail;
    monitor_work_context(owner->dpc)->owner = owner;
    /* Inspect before connecting an interrupt; an active firmware monitor remains unclaimed. */
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        const bwmon_target_t* target = &bwmon_targets[i];
        PHYSICAL_ADDRESS address;
        bwmon_io_t io = {0};
        int idle;
        address.QuadPart = target->global_base ? target->global_base : target->base;
        m->local_offset = target->global_base ? (ULONG)(target->base - target->global_base) : 0u;
        m->bytes = target->revision == 4u ? 0x400u : 0x1000u;
        m->map = MmMapIoSpaceEx(address, m->bytes, PAGE_READONLY | PAGE_NOCACHE);
        io.ctx = m;
        io.read = monitor_read;
        idle = m->map && bwmon_idle_probe(target, &io);
        if (m->map)
            MmUnmapIoSpace(m->map, m->bytes);
        m->map = NULL;
        if (!idle) {
            status = STATUS_DEVICE_BUSY;
            goto fail;
        }
    }
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        ULONG group, matches = 0u;
        PCM_PARTIAL_RESOURCE_DESCRIPTOR r = NULL, t = NULL;
        monitor_native_t* m = &owner->monitors[i];
        m->owner = owner;
        for (group = 0u; group < owner->group_count; ++group)
            if (owner->groups[group].intid == bwmon_targets[i].irq)
                break;
        if (group == owner->group_count) {
            WDF_INTERRUPT_CONFIG config;
            for (ULONG j = 0u; j < WdfCmResourceListGetCount(raw); ++j) {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR a = WdfCmResourceListGetDescriptor(raw, j);
                PCM_PARTIAL_RESOURCE_DESCRIPTOR b = WdfCmResourceListGetDescriptor(translated, j);
                if (a && b && a->Type == CmResourceTypeInterrupt &&
                    b->Type == CmResourceTypeInterrupt &&
                    a->u.Interrupt.Vector == bwmon_targets[i].irq &&
                    !(a->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                    !(b->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)) {
                    ++matches;
                    r = a;
                    t = b;
                }
            }
            if (matches != 1u) {
                status = STATUS_NOT_FOUND;
                goto fail;
            }
            owner->groups[group].owner = owner;
            owner->groups[group].intid = bwmon_targets[i].irq;
            WDF_INTERRUPT_CONFIG_INIT(&config, monitor_isr, NULL);
            config.AutomaticSerialization = FALSE;
            config.InterruptRaw = r;
            config.InterruptTranslated = t;
            WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, monitor_irq_context_t);
            status = WdfInterruptCreate(dev, &config, &attributes, &owner->groups[group].interrupt);
            if (!NT_SUCCESS(status))
                goto fail;
            monitor_irq_context(owner->groups[group].interrupt)->group = &owner->groups[group];
            ++owner->group_count;
        }
        m->group = group;
        owner->groups[group].members |= 1u << i;
    }
    return STATUS_SUCCESS;
fail:
    (void)bus_dcvs_release(dev);
    return status;
}

VOID bus_dcvs_start(WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    bus_dcvs_t* owner = ctx ? ctx->bus_dcvs : NULL;
    PHYSICAL_ADDRESS address;
    bwmon_io_t io;
    ULONG started = 0u;
    if (!owner || InterlockedCompareExchange(&owner->running, 0, 0) || owner->epoch ||
        !hw_backend_ready())
        return;
    owner->epoch = hw_backend_verified_serial();
    if (!owner->epoch)
        return;
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        const bwmon_target_t* target = &bwmon_targets[i];
        power_action_t action;
        make_vote(&action, target, 0u, 0u);
        if (!hw_backend_win_bus_request(owner->epoch, target->client, &action, BUS_REQUEST_QUERY))
            continue;
        address.QuadPart = target->global_base ? target->global_base : target->base;
        m->local_offset = target->global_base ? (ULONG)(target->base - target->global_base) : 0u;
        m->bytes = target->revision == 4u ? 0x400u : 0x1000u;
        if (!m->map)
            m->map = MmMapIoSpaceEx(address, m->bytes, PAGE_READWRITE | PAGE_NOCACHE);
        if (!m->map)
            continue;
        io.ctx = m;
        io.owns = monitor_owns;
        io.read = monitor_read;
        io.write = monitor_write;
        io.barrier = monitor_barrier;
        WdfInterruptAcquireLock(owner->groups[m->group].interrupt);
        if (bwmon_init(&m->monitor, target, &io, owner->epoch) && bwmon_start(&m->monitor)) {
            m->available = TRUE;
            m->reported = FALSE;
            ++started;
        } else
            (void)bwmon_stop(&m->monitor);
        WdfInterruptReleaseLock(owner->groups[m->group].interrupt);
    }
    if (started && setting(dev, L"EnableLaggBwmon", 1u)) {
        address.QuadPart = lagg_base;
        if (!owner->lagg_map)
            owner->lagg_map = MmMapIoSpaceEx(address, 0x400u, PAGE_READWRITE | PAGE_NOCACHE);
        if (owner->lagg_map) {
            io.ctx = owner;
            io.owns = lagg_owns;
            io.read = lagg_read;
            io.write = lagg_write;
            io.barrier = monitor_barrier;
            owner->lagg_enabled = (BOOLEAN)lagg_start(&owner->lagg, &io, owner->epoch);
            if (!owner->lagg_enabled)
                (void)lagg_stop(&owner->lagg);
        }
    }
    InterlockedExchange(&owner->running, started ? 1 : 0);
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        if (!m->available)
            continue;
        WdfInterruptAcquireLock(owner->groups[m->group].interrupt);
        if (!bwmon_rearm(&m->monitor, 0u)) {
            (void)bwmon_stop(&m->monitor);
            m->available = FALSE;
            --started;
        }
        WdfInterruptReleaseLock(owner->groups[m->group].interrupt);
    }
    if (!started)
        (void)bus_dcvs_stop(dev);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE, "bus: bandwidth monitors=%lu LAGG=%u",
                started, (unsigned)owner->lagg_enabled);
}

NTSTATUS bus_dcvs_stop(WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    bus_dcvs_t* owner = ctx ? ctx->bus_dcvs : NULL;
    BOOLEAN released = TRUE;
    if (!owner)
        return STATUS_SUCCESS;
    InterlockedExchange(&owner->running, 0);
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        if (!m->map || !m->monitor.saved_count)
            continue;
        WdfInterruptAcquireLock(owner->groups[m->group].interrupt);
        if (!bwmon_pause(&m->monitor))
            released = FALSE;
        m->available = FALSE;
        WdfInterruptReleaseLock(owner->groups[m->group].interrupt);
    }
    if (owner->dpc)
        (void)WdfDpcCancel(owner->dpc, TRUE);
    if (owner->work)
        WdfWorkItemFlush(owner->work);
    InterlockedExchange(&owner->pending, 0);
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        if (!m->map)
            continue;
        WdfInterruptAcquireLock(owner->groups[m->group].interrupt);
        if (!bwmon_stop(&m->monitor))
            released = FALSE;
        WdfInterruptReleaseLock(owner->groups[m->group].interrupt);
    }
    if (!lagg_stop(&owner->lagg))
        released = FALSE;
    for (ULONG i = 0u; i < BWMON_COUNT; ++i) {
        monitor_native_t* m = &owner->monitors[i];
        if (m->vote_held) {
            power_action_t action;
            make_vote(&action, &bwmon_targets[i], 0u, 0u);
            if (!hw_backend_win_bus_request(owner->epoch, bwmon_targets[i].client, &action,
                                            BUS_REQUEST_RELEASE))
                released = FALSE;
            else
                m->vote_held = FALSE;
        }
    }
    if (released) {
        owner->epoch = 0u;
        owner->lagg_enabled = FALSE;
    }
    return released ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;
}

NTSTATUS bus_dcvs_release(WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    bus_dcvs_t* owner = ctx ? ctx->bus_dcvs : NULL;
    NTSTATUS status = bus_dcvs_stop(dev);
    if (!NT_SUCCESS(status) || !owner)
        return status;
    for (ULONG i = 0u; i < owner->group_count; ++i)
        if (owner->groups[i].interrupt)
            WdfObjectDelete(owner->groups[i].interrupt);
    if (owner->work)
        WdfObjectDelete(owner->work);
    if (owner->dpc)
        WdfObjectDelete(owner->dpc);
    for (ULONG i = 0u; i < BWMON_COUNT; ++i)
        if (owner->monitors[i].map)
            MmUnmapIoSpace(owner->monitors[i].map, owner->monitors[i].bytes);
    if (owner->lagg_map)
        MmUnmapIoSpace(owner->lagg_map, 0x400u);
    ExFreePool(owner);
    ctx->bus_dcvs = NULL;
    return STATUS_SUCCESS;
}
