

#include "mmio_win.h"
#include "clock_target.h"
#include "trace.h"
#include "mmio_win.tmh"

static NTSTATUS map_one(mmio_window_t* w, ULONGLONG physical, SIZE_T bytes, ULONG protect)
{
    PHYSICAL_ADDRESS pa;
    PVOID va;
    pa.QuadPart = (LONGLONG)physical;
    va = MmMapIoSpaceEx(pa, bytes, protect);
    if (!va)
        return STATUS_INSUFFICIENT_RESOURCES;
    w->base = (volatile UCHAR*)va;
    w->bytes = bytes;
    w->physical = physical;
    return STATUS_SUCCESS;
}

static VOID unmap_one(mmio_window_t* w)
{
    if (w->base) {
        MmUnmapIoSpace((PVOID)w->base, w->bytes);
        w->base = NULL;
    }
    w->bytes = 0;
}

/* an observed RSC _CRS memory window, GIC IRQ 37, a probe register identity, or
 * MolokaiHwOwners=1 is never a controller grant. The caller used to be able to map
 * writable GCC/TLMM/RSC registers after only checking PEP0 PnP assignment. */

static BOOLEAN production_mmio_ownership_verified(VOID)
{
    return FALSE;
}

NTSTATUS mmio_map(_Out_ mmio_t* mmio)
{
    NTSTATUS st;
    if (!mmio)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(mmio, sizeof(*mmio));
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!production_mmio_ownership_verified()) {
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                    "mmio: mapping rejected reason=owner-unavailable");
        return STATUS_NOT_SUPPORTED;
    }
    st = map_one(&mmio->gcc, GCC_WINDOW_PHYS, GCC_WINDOW_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (NT_SUCCESS(st))
        st = map_one(&mmio->tlmm, TLMM_PHYS, TLMM_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (NT_SUCCESS(st))
        st = map_one(&mmio->rsc, HLOS_RSC_DRV2_PHYS, RSC_DRV2_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (!NT_SUCCESS(st))
        mmio_unmap(mmio);
    return st;
}

VOID mmio_unmap(_Inout_ mmio_t* mmio)
{
    if (!mmio)
        return;
    unmap_one(&mmio->rsc);
    unmap_one(&mmio->tlmm);
    unmap_one(&mmio->gpucc);
    unmap_one(&mmio->gcc);
}

static VOID map_gpucc_vote(mmio_t* mmio)
{
    const clock_window_t* window = clock_window_get(CLOCK_CTL_GPUCC);
    ULONG a = 0u, b = 0u;
    NTSTATUS status;
    if (!window)
        return;
    status = map_one(&mmio->gpucc, window->physical, window->bytes, PAGE_READWRITE | PAGE_NOCACHE);
    if (NT_SUCCESS(status) &&
        (!mmio_read32(&mmio->gpucc, 0u, &a) || !mmio_read32(&mmio->gpucc, 0u, &b) ||
         a == MAXULONG || b == MAXULONG || ((a ^ b) & CBCR_CLK_ENABLE)))
        status = STATUS_DEVICE_HARDWARE_ERROR;
    if (!NT_SUCCESS(status)) {
        unmap_one(&mmio->gpucc);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK,
                    "clock: GPUCC vote mapping unavailable status=%08X", status);
    } else
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_CLOCK,
                    "clock: GPUCC vote mapped physical=%llX value=%08lX", window->physical, b);
}

/* clock_InitBases and TLMM_DeviceInit map their BSP controller ranges independently of the
 * RPMh DRV2 PnP memory desc. Unlike the obsolete all-or-nothing PrepareHardware mapper
 * above, these peripheral maps are activated at d0 only, after the PEP0 driver's live
 * firmware-configured DRV2/IRQ37. */

NTSTATUS mmio_d0_map_peripherals(_Inout_ mmio_t* mmio)
{
    NTSTATUS st;
    ULONG gcc_a = 0, gcc_b = 0, pin_a = 0, pin_b = 0;
    if (!mmio)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (mmio->gcc.base || mmio->tlmm.base || mmio->gpucc.base)
        return STATUS_DEVICE_BUSY;
    /* never disturb the separate RSC d0 mapping. */

    if (!mmio->rsc.base || mmio->rsc.physical != HLOS_RSC_DRV2_PHYS ||
        mmio->rsc.bytes != RSC_DRV2_BYTES)
        return STATUS_DEVICE_NOT_READY;
    st = map_one(&mmio->gcc, GCC_WINDOW_PHYS, GCC_WINDOW_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (!NT_SUCCESS(st))
        return st;
    st = map_one(&mmio->tlmm, TLMM_PHYS, TLMM_BYTES, PAGE_READWRITE | PAGE_NOCACHE);
    if (!NT_SUCCESS(st))
        goto rollback;
    /* verify two stable GPLL0 and GPIO52 samples without writes. */

    if (!mmio_read32(&mmio->gcc, gcc_gpll0_mode_offset, &gcc_a) ||
        !mmio_read32(&mmio->tlmm, TLMM_CTL(52u), &pin_a) ||
        !mmio_read32(&mmio->gcc, gcc_gpll0_mode_offset, &gcc_b) ||
        !mmio_read32(&mmio->tlmm, TLMM_CTL(52u), &pin_b) || gcc_a != gcc_b || pin_a != pin_b ||
        gcc_a == 0xFFFFFFFFu || pin_a == 0xFFFFFFFFu) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "mmio: GCC/TLMM identity changed GPLL0=%08lX/%08lX GPIO52=%08lX/%08lX", gcc_a,
                    gcc_b, pin_a, pin_b);
        st = STATUS_DEVICE_HARDWARE_ERROR;
        goto rollback;
    }
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                "mmio: GCC/TLMM mapped GPLL0=%08lX GPIO52=%08lX attestation=pending", gcc_a, pin_a);
    map_gpucc_vote(mmio);
    return STATUS_SUCCESS;
rollback:
    mmio_d0_unmap_peripherals(mmio);
    return st;
}

VOID mmio_d0_unmap_peripherals(_Inout_ mmio_t* mmio)
{
    if (!mmio)
        return;
    unmap_one(&mmio->tlmm);
    unmap_one(&mmio->gpucc);
    unmap_one(&mmio->gcc);
}

int mmio_read32(const mmio_window_t* w, ULONG offset, ULONG* value)
{
    if (!w || !w->base || !value || w->bytes < sizeof(ULONG) || (offset & 3u) ||
        (SIZE_T)offset > w->bytes - sizeof(ULONG))
        return 0;
    *value = READ_REGISTER_ULONG((volatile ULONG*)(w->base + offset));
    return 1;
}

int mmio_write32(const mmio_window_t* w, ULONG offset, ULONG value)
{
    if (!w || !w->base || w->bytes < sizeof(ULONG) || (offset & 3u) ||
        (SIZE_T)offset > w->bytes - sizeof(ULONG))
        return 0;
    WRITE_REGISTER_ULONG((volatile ULONG*)(w->base + offset), value);
    return 1;
}

NTSTATUS mmio_snapshot_command_db(_Outptr_result_maybenull_ UCHAR** copy, _Out_ SIZE_T* bytes)
{
    mmio_window_t w;
    UCHAR* buf;
    NTSTATUS st;
    ULONG version, magic;

    if (!copy || !bytes)
        return STATUS_INVALID_PARAMETER;
    *copy = NULL;
    *bytes = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(&w, sizeof(w));

    st = map_one(&w, CMDDB_PHYS, CMDDB_MAX_BYTES, PAGE_READONLY | PAGE_NOCACHE);
    if (!NT_SUCCESS(st))
        return st;
    if (!mmio_read32(&w, 0, &version) || !mmio_read32(&w, 4, &magic) || version != CMDDB_VERSION ||
        magic != CMDDB_MAGIC) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "cmddb: header invalid base=%llX version=%08X magic=%08X",
                    (ULONGLONG)CMDDB_PHYS, version, magic);
        unmap_one(&w);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    buf = (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, CMDDB_MAX_BYTES, POOL_TAG);
    if (!buf) {
        unmap_one(&w);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    /* a 32-bit read loop: the window is device-attribute memory, so avoid RtlCopyMemory's
     * unaligned and vector paths. */

    {
        SIZE_T off;
        for (off = 0; off + 4u <= CMDDB_MAX_BYTES; off += 4u)
            *(ULONG*)(buf + off) = READ_REGISTER_ULONG((volatile ULONG*)(w.base + off));
    }
    unmap_one(&w);
    *copy = buf;
    *bytes = CMDDB_MAX_BYTES;
    return STATUS_SUCCESS;
}
