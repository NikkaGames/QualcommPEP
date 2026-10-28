#include "soc_transport.h"
#include "soc_model.h"
#include "perf_target.h"
#include "trace.h"
#include <arm64intr.h>
#include "soc_transport.tmh"

typedef struct cpu_channel {
    scmi_perf_domain_t firmware;
    volatile UCHAR* set;
    volatile UCHAR* get;
    volatile UCHAR* doorbell;
    KSPIN_LOCK lock;
    PEX_TIMER timer;
    ULONGLONG last_request;
    ULONG pending_level;
    ULONG requested_level;
    BOOLEAN pending;
    BOOLEAN requested;
} cpu_channel_t;

static cpu_channel_t channels[PERF_TARGET_DOMAINS];
static volatile UCHAR* mailbox;
static volatile UCHAR* control;
static volatile LONG state;
static volatile LONG fault;
static volatile LONG suspended;
static ULONG token;
static ULONGLONG affinities[PERF_TARGET_CPUS];
static KSPIN_LOCK doorbell_lock;
static ULONG timer_resolution;

static ULONGLONG now100ns(VOID)
{
    ULONGLONG qpc;
    return KeQueryInterruptTimePrecise(&qpc);
}

static VOID unmap_channels(VOID)
{
    const perf_target_t* target = perf_target();
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i) {
        cpu_channel_t* c = &channels[i];
        PVOID set, get, doorbell;
        KIRQL irql;
        KeAcquireSpinLock(&c->lock, &irql);
        set = (PVOID)c->set;
        get = (PVOID)c->get;
        doorbell = (PVOID)c->doorbell;
        c->set = c->get = c->doorbell = NULL;
        KeReleaseSpinLock(&c->lock, irql);
        if (set)
            MmUnmapIoSpace(set, 4u);
        if (get)
            MmUnmapIoSpace(get, 4u);
        if (doorbell)
            MmUnmapIoSpace(doorbell, c->firmware.set_channel.doorbell_bytes);
    }
    if (mailbox)
        MmUnmapIoSpace((PVOID)mailbox, target->mailbox_bytes);
    if (control)
        MmUnmapIoSpace((PVOID)control, target->control_bytes);
    mailbox = control = NULL;
}

static PVOID map_register(uint64_t address, ULONG bytes, BOOLEAN write)
{
    PHYSICAL_ADDRESS physical;
    if (!perf_target_channel(address, bytes, write))
        return NULL;
    physical.QuadPart = (LONGLONG)address;
    return MmMapIoSpaceEx(physical, bytes, (write ? PAGE_READWRITE : PAGE_READONLY) | PAGE_NOCACHE);
}

static int transfer(void* ctx, uint32_t message, const uint32_t* request, size_t count,
                    uint32_t* response, size_t* words)
{
    const perf_target_t* target = perf_target();
    ULONG header, length, status;
    ULONGLONG deadline;
    LARGE_INTEGER delay;
    UNREFERENCED_PARAMETER(ctx);
    if (!mailbox || !control || !response || !words || (!request && count) || count > 2u ||
        KeGetCurrentIrql() != PASSIVE_LEVEL || InterlockedCompareExchange(&fault, 0, 0))
        return 0;
    /* The HLOS channel must be free before ownership of its request buffer transfers. */
    status = READ_REGISTER_ULONG((PULONG)(mailbox + 4u));
    if (status != 1u)
        return 0;
    token = (token + 1u) & 0x3ffu;
    header = (token << 18) | (SCMI_PERF_PROTOCOL << 10) | message;
    WRITE_REGISTER_ULONG((PULONG)(mailbox + 4u), 0u);
    WRITE_REGISTER_ULONG((PULONG)(mailbox + 16u), 0u);
    WRITE_REGISTER_ULONG((PULONG)(mailbox + 20u), (ULONG)(4u + count * 4u));
    WRITE_REGISTER_ULONG((PULONG)(mailbox + 24u), header);
    for (size_t i = 0u; i < count; ++i)
        WRITE_REGISTER_ULONG((PULONG)(mailbox + 28u + i * 4u), request[i]);
    KeMemoryBarrier();
    /* The HLOS CPUCP mailbox carries the transfer identifier. */
    WRITE_REGISTER_ULONG((PULONG)(control + target->send_offset), token);
    deadline = now100ns() + 200000u;
    delay.QuadPart = -10000;
    while (!((status = READ_REGISTER_ULONG((PULONG)(mailbox + 4u))) & 1u)) {
        if (now100ns() >= deadline) {
            InterlockedExchange(&fault, 1);
            return 0;
        }
        (VOID) KeDelayExecutionThread(KernelMode, FALSE, &delay);
    }
    KeMemoryBarrier();
    length = READ_REGISTER_ULONG((PULONG)(mailbox + 20u));
    if (status != 1u || READ_REGISTER_ULONG((PULONG)(mailbox + 24u)) != header || length < 8u ||
        length > target->mailbox_bytes - 24u || (length & 3u) || (length - 8u) / 4u > *words) {
        InterlockedExchange(&fault, 1);
        return 0;
    }
    if (READ_REGISTER_ULONG((PULONG)(mailbox + 28u)) != 0u)
        return 0;
    *words = (length - 8u) / 4u;
    for (size_t i = 0u; i < *words; ++i)
        response[i] = READ_REGISTER_ULONG((PULONG)(mailbox + 32u + i * 4u));
    return 1;
}

static ULONG_PTR capture_affinity(ULONG_PTR context)
{
    PROCESSOR_NUMBER number;
    ULONG cpu;
    UNREFERENCED_PARAMETER(context);
    KeGetCurrentProcessorNumberEx(&number);
    cpu = KeGetProcessorIndexFromNumber(&number);
    if (cpu < PERF_TARGET_CPUS)
        affinities[cpu] = _ReadStatusReg(ARM64_SYSREG(3, 0, 0, 0, 5));
    return 0u;
}

static int delivered(cpu_channel_t* c, ULONG* level)
{
    return c->get &&
           scmi_perf_decode_level(&c->firmware, READ_REGISTER_ULONG((PULONG)c->get), level);
}

static int issue(cpu_channel_t* c, ULONG level)
{
    const scmi_fast_channel_t* fc = &c->firmware.set_channel;
    ULONGLONG value = 0u;
    ULONG observed;
    ULONG wire = c->firmware.levels[level].wire_level;
    if (!delivered(c, &observed))
        return 0;
    WRITE_REGISTER_ULONG((PULONG)c->set, wire);
    KeMemoryBarrier();
    if (READ_REGISTER_ULONG((PULONG)c->set) != wire)
        return 0;
    if (c->doorbell) {
        KeAcquireSpinLockAtDpcLevel(&doorbell_lock);
        if (fc->preserve_mask) {
            switch (fc->doorbell_bytes) {
                case 1u:
                    value = READ_REGISTER_UCHAR(c->doorbell);
                    break;
                case 2u:
                    value = READ_REGISTER_USHORT((PUSHORT)c->doorbell);
                    break;
                case 4u:
                    value = READ_REGISTER_ULONG((PULONG)c->doorbell);
                    break;
                case 8u:
                    value = READ_REGISTER_ULONG64((PULONG64)c->doorbell);
                    break;
                default:
                    KeReleaseSpinLockFromDpcLevel(&doorbell_lock);
                    return 0;
            }
        }
        value = (value & fc->preserve_mask) | fc->set_mask;
        switch (fc->doorbell_bytes) {
            case 1u:
                WRITE_REGISTER_UCHAR(c->doorbell, (UCHAR)value);
                break;
            case 2u:
                WRITE_REGISTER_USHORT((PUSHORT)c->doorbell, (USHORT)value);
                break;
            case 4u:
                WRITE_REGISTER_ULONG((PULONG)c->doorbell, (ULONG)value);
                break;
            case 8u:
                WRITE_REGISTER_ULONG64((PULONG64)c->doorbell, value);
                break;
            default:
                KeReleaseSpinLockFromDpcLevel(&doorbell_lock);
                return 0;
        }
        KeReleaseSpinLockFromDpcLevel(&doorbell_lock);
    }
    KeMemoryBarrier();
    c->last_request = now100ns();
    c->requested_level = level;
    c->requested = TRUE;
    c->pending = FALSE;
    return 1;
}

static VOID schedule(cpu_channel_t* c, ULONGLONG remaining)
{
    (VOID) ExSetTimer(c->timer, -(LONGLONG)(remaining ? remaining : 1u), 0, NULL);
}

static VOID deferred_request(PEX_TIMER timer, PVOID context)
{
    cpu_channel_t* c = context;
    KIRQL irql;
    ULONGLONG elapsed, interval;
    BOOLEAN failed = FALSE;
    UNREFERENCED_PARAMETER(timer);
    KeAcquireSpinLock(&c->lock, &irql);
    if (InterlockedCompareExchange(&state, 0, 0) == 2 &&
        !InterlockedCompareExchange(&fault, 0, 0) &&
        !InterlockedCompareExchange(&suspended, 0, 0) && c->pending) {
        elapsed = now100ns() - c->last_request;
        interval = (ULONGLONG)c->firmware.rate_limit_us * 10u;
        if (elapsed < interval)
            schedule(c, interval - elapsed);
        else if (!issue(c, c->pending_level))
            failed = InterlockedCompareExchange(&fault, 1, 0) == 0;
    }
    KeReleaseSpinLock(&c->lock, irql);
    if (failed)
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_PPM,
                    "ppm: deferred performance request failed domain=%lu", c->firmware.id);
}

BOOLEAN molokai_transport_ready(VOID)
{
    return InterlockedCompareExchange(&state, 0, 0) == 2 &&
           !InterlockedCompareExchange(&fault, 0, 0) &&
           !InterlockedCompareExchange(&suspended, 0, 0);
}

VOID molokai_transport_suspend(BOOLEAN pause)
{
    KIRQL irql;
    if (InterlockedCompareExchange(&state, 0, 0) != 2)
        return;
    InterlockedExchange(&suspended, 1);
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i) {
        cpu_channel_t* c = &channels[i];
        KeAcquireSpinLock(&c->lock, &irql);
        if (c->timer)
            (VOID) ExCancelTimer(c->timer, NULL);
        c->pending = FALSE;
        c->requested = FALSE;
        KeReleaseSpinLock(&c->lock, irql);
    }
    if (!pause)
        InterlockedExchange(&suspended, 0);
}

NTSTATUS molokai_transport_init(VOID)
{
    const perf_target_t* target = perf_target();
    scmi_perf_io_t io = {NULL, transfer};
    PHYSICAL_ADDRESS physical;
    ULONG maximum, current;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL ||
        KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS) != PERF_TARGET_CPUS)
        return STATUS_NOT_SUPPORTED;
    if (InterlockedCompareExchange(&state, 1, 0) != 0)
        return molokai_transport_ready() ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    molokai_model_reset();
    KeInitializeSpinLock(&doorbell_lock);
    ExQueryTimerResolution(&maximum, &timer_resolution, &current);
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i)
        KeInitializeSpinLock(&channels[i].lock);
    /* Check physical affinities before accessing the target mailbox. */
    KeIpiGenericCall(capture_affinity, 0u);
    for (ULONG cpu = 0u; cpu < PERF_TARGET_CPUS; ++cpu) {
        uint32_t domain, core;
        if (!perf_target_core(affinities[cpu], &domain, &core))
            goto fail;
        for (ULONG other = 0u; other < cpu; ++other)
            if (affinities[other] == affinities[cpu])
                goto fail;
    }
    physical.QuadPart = target->mailbox;
    mailbox = MmMapIoSpaceEx(physical, target->mailbox_bytes, PAGE_READWRITE | PAGE_NOCACHE);
    physical.QuadPart = target->control;
    control = MmMapIoSpaceEx(physical, target->control_bytes, PAGE_READWRITE | PAGE_NOCACHE);
    if (!mailbox || !control) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto fail;
    }
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i) {
        cpu_channel_t* c = &channels[i];
        ULONG level;
        if (!scmi_perf_discover(&io, i, &c->firmware))
            goto fail;
        c->set = map_register(c->firmware.set_channel.address, 4u, TRUE);
        c->get = map_register(c->firmware.get_channel.address, 4u, FALSE);
        if (c->firmware.set_channel.doorbell_bytes)
            c->doorbell = map_register(c->firmware.set_channel.doorbell,
                                       c->firmware.set_channel.doorbell_bytes, TRUE);
        if (!c->set || !c->get || (c->firmware.set_channel.doorbell_bytes && !c->doorbell) ||
            !delivered(c, &level) || !molokai_model_bind(i, &c->firmware))
            goto fail;
        if (c->firmware.rate_limit_us) {
            c->timer = ExAllocateTimer(deferred_request, c, EX_TIMER_HIGH_RESOLUTION);
            if (!c->timer) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto fail;
            }
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_PPM,
                    "ppm: domain=%lu levels=%lu set=%I64x get=%I64x rate-us=%lu", i,
                    c->firmware.level_count, c->firmware.set_channel.address,
                    c->firmware.get_channel.address, c->firmware.rate_limit_us);
    }
    for (ULONG cpu = 0u; cpu < PERF_TARGET_CPUS; ++cpu) {
        uint32_t domain, core;
        if (!perf_target_core(affinities[cpu], &domain, &core) ||
            !molokai_model_bind_cpu(cpu, domain, core))
            goto fail;
    }
    InterlockedExchange(&state, 2);
    return STATUS_SUCCESS;
fail:
    InterlockedExchange(&state, 3);
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i)
        if (channels[i].timer) {
            (VOID) ExDeleteTimer(channels[i].timer, TRUE, TRUE, NULL);
            channels[i].timer = NULL;
        }
    unmap_channels();
    TraceEvents(TRACE_LEVEL_WARNING, TRACE_PPM, "ppm: discovery failed status=%08X fault=%ld",
                status, InterlockedCompareExchange(&fault, 0, 0));
    return status;
}

VOID molokai_transport_deinit(VOID)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || InterlockedCompareExchange(&state, 3, 2) != 2)
        return;
    for (ULONG i = 0u; i < PERF_TARGET_DOMAINS; ++i) {
        cpu_channel_t* c = &channels[i];
        KIRQL irql;
        KeAcquireSpinLock(&c->lock, &irql);
        c->pending = FALSE;
        if (c->timer)
            (VOID) ExCancelTimer(c->timer, NULL);
        KeReleaseSpinLock(&c->lock, irql);
        if (c->timer) {
            (VOID) ExDeleteTimer(c->timer, TRUE, TRUE, NULL);
            c->timer = NULL;
        }
    }
    unmap_channels();
}

ULONG molokai_transport_latency(ULONG domain)
{
    const scmi_perf_domain_t* d = molokai_transport_domain(domain);
    /* Include firmware rate limiting and one high-resolution timer tick. */
    return d ? d->transition_latency_us * 10u +
                   (d->rate_limit_us ? d->rate_limit_us * 10u + timer_resolution : 0u)
             : 0u;
}

const scmi_perf_domain_t* molokai_transport_domain(ULONG domain)
{
    return molokai_transport_ready() && domain < PERF_TARGET_DOMAINS ? &channels[domain].firmware
                                                                     : NULL;
}

NTSTATUS molokai_transport_get_level(ULONG domain, ULONG* level)
{
    cpu_channel_t* c;
    KIRQL irql;
    BOOLEAN valid;
    if (!level || domain >= PERF_TARGET_DOMAINS || KeGetCurrentIrql() > DISPATCH_LEVEL ||
        !molokai_transport_ready())
        return STATUS_DEVICE_NOT_READY;
    c = &channels[domain];
    KeAcquireSpinLock(&c->lock, &irql);
    valid = molokai_transport_ready() && delivered(c, level);
    KeReleaseSpinLock(&c->lock, irql);
    return valid ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

NTSTATUS molokai_transport_set_level(ULONG domain, ULONG level)
{
    cpu_channel_t* c;
    KIRQL irql;
    NTSTATUS status = STATUS_DEVICE_NOT_READY;
    ULONGLONG elapsed, interval;
    BOOLEAN failed = FALSE;
    if (domain >= PERF_TARGET_DOMAINS || KeGetCurrentIrql() > DISPATCH_LEVEL ||
        !molokai_transport_ready())
        return status;
    c = &channels[domain];
    KeAcquireSpinLock(&c->lock, &irql);
    if (!molokai_transport_ready() || level >= c->firmware.level_count)
        goto done;
    if (c->requested && c->requested_level == level && !c->pending &&
        READ_REGISTER_ULONG((PULONG)c->set) == c->firmware.levels[level].wire_level) {
        status = STATUS_SUCCESS;
        goto done;
    }
    elapsed = now100ns() - c->last_request;
    interval = (ULONGLONG)c->firmware.rate_limit_us * 10u;
    if (c->requested && elapsed < interval) {
        c->pending_level = level;
        c->pending = TRUE;
        schedule(c, interval - elapsed);
        status = STATUS_PENDING;
    } else if (issue(c, level)) {
        status = STATUS_SUCCESS;
    } else {
        failed = InterlockedCompareExchange(&fault, 1, 0) == 0;
    }
done:
    KeReleaseSpinLock(&c->lock, irql);
    if (failed)
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_PPM,
                    "ppm: performance request failed domain=%lu level=%lu", domain, level);
    return status;
}
