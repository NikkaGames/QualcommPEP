#include "soc_transport.h"
#include "soc_model.h"

/* mmio authority is NOT inferred from this evidence. */

#define MOLOKAI_PDP_TX_BYTES 0x300u
#define MOLOKAI_LEVEL_OFFSET 0x118u
#define MOLOKAI_DOORBELL_OFFSET 0x11Cu
#define MOLOKAI_DOORBELL_MASK 0xFu
#if MOLOKAI_DOORBELL_OFFSET + 4u > MOLOKAI_PDP_TX_BYTES
#error PDP doorbell register outside OEM TX aperture
#endif
#if MOLOKAI_LEVEL_OFFSET + 4u > MOLOKAI_PDP_TX_BYTES
#error PDP level register outside OEM TX aperture
#endif

static const ULONGLONG s_domain_base[MOLOKAI_DOMAIN_CNT] = {0x19980000ull, 0x1A980000ull};
static volatile UCHAR* s_domain_map[MOLOKAI_DOMAIN_CNT];
static KSPIN_LOCK s_domain_lock[MOLOKAI_DOMAIN_CNT];
/* spinlocks are initialized once for the lifetime of the driver. They must not be reset on
 * reinitialization while a stale caller is waiting. */

static volatile LONG s_locks_init_state;
/* only 2 allows MMIO writes. An MMIO write AND the pointer validity check occur under the
 * domain's spinlock. */

static volatile LONG s_transport_init_state;

/* DVFS remains disabled until transport ownership and completion are verified. */

BOOLEAN molokai_transport_ownership_verified(VOID)
{
    return FALSE;
}

NTSTATUS molokai_transport_init(VOID)
{
    ULONG i;
    LONG previous;
    /* no fixed-address mapping is allowed without an exclusive CPU owner. */
    if (!molokai_transport_ownership_verified())
        return STATUS_NOT_SUPPORTED;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    previous = InterlockedCompareExchange(&s_transport_init_state, 1, 0);
    if (previous == 2)
        return STATUS_SUCCESS;
    if (previous != 0)
        return STATUS_DEVICE_NOT_READY;

    if (InterlockedCompareExchange(&s_locks_init_state, 1, 0) == 0) {
        for (i = 0; i < MOLOKAI_DOMAIN_CNT; ++i)
            KeInitializeSpinLock(&s_domain_lock[i]);
        InterlockedExchange(&s_locks_init_state, 2);
    } else {
        while (InterlockedCompareExchange(&s_locks_init_state, 2, 2) != 2)
            YieldProcessor();
    }
    for (i = 0; i < MOLOKAI_DOMAIN_CNT; ++i)
        s_domain_map[i] = NULL;
    for (i = 0; i < MOLOKAI_DOMAIN_CNT; ++i) {
        PHYSICAL_ADDRESS pa;
        pa.QuadPart = (LONGLONG)s_domain_base[i];
        s_domain_map[i] = (volatile UCHAR*)MmMapIoSpaceEx(pa, MOLOKAI_PDP_TX_BYTES,
                                                          PAGE_READWRITE | PAGE_NOCACHE);
        if (!s_domain_map[i]) {

            for (ULONG j = 0; j < i; ++j) {
                MmUnmapIoSpace((PVOID)s_domain_map[j], MOLOKAI_PDP_TX_BYTES);
                s_domain_map[j] = NULL;
            }
            InterlockedExchange(&s_transport_init_state, 0);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    InterlockedExchange(&s_transport_init_state, 2);
    return STATUS_SUCCESS;
}

VOID molokai_transport_deinit(VOID)
{
    ULONG i;
    /* unmap must not happen at raised IRQL. */

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    /* a teardown racing initial mapping must wait for mapping to finish, rather than returning
     * and leaving the newly mapped pages live. */

    for (;;) {
        LONG state = InterlockedCompareExchange(&s_transport_init_state, 3, 2);
        if (state == 2)
            break;
        if (state == 0)
            return;

        YieldProcessor();
    }

    for (i = 0; i < MOLOKAI_DOMAIN_CNT; ++i) {
        KIRQL irql;
        volatile UCHAR* map;
        KeAcquireSpinLock(&s_domain_lock[i], &irql);
        map = s_domain_map[i];
        s_domain_map[i] = NULL;
        KeReleaseSpinLock(&s_domain_lock[i], irql);
        /* no writer can have this pointer after releasing the lock. */
        if (map)
            MmUnmapIoSpace((PVOID)map, MOLOKAI_PDP_TX_BYTES);
    }
    InterlockedExchange(&s_transport_init_state, 0);
}

NTSTATUS molokai_transport_set_level(_In_ ULONG domain, _In_ ULONG level)
{
    const molokai_domain_model_t* model = molokai_domain(domain);
    KIRQL irql;
    volatile UCHAR* base;

    if (!model || level >= model->level_cnt)
        return STATUS_INVALID_PARAMETER;

    if (!molokai_transport_ownership_verified())
        return STATUS_NOT_SUPPORTED;
    if (KeGetCurrentIrql() > DISPATCH_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (InterlockedCompareExchange(&s_transport_init_state, 2, 2) != 2)
        return STATUS_DEVICE_NOT_READY;

    KeAcquireSpinLock(&s_domain_lock[domain], &irql);
    /* teardown may have started between our initial check and lock. */
    base = s_domain_map[domain];
    if (InterlockedCompareExchange(&s_transport_init_state, 2, 2) != 2 || !base) {
        KeReleaseSpinLock(&s_domain_lock[domain], irql);
        return STATUS_DEVICE_NOT_READY;
    }
    WRITE_REGISTER_ULONG((PULONG)(base + MOLOKAI_LEVEL_OFFSET), level);
    KeMemoryBarrier();
    WRITE_REGISTER_ULONG((PULONG)(base + MOLOKAI_DOORBELL_OFFSET), MOLOKAI_DOORBELL_MASK);
    KeReleaseSpinLock(&s_domain_lock[domain], irql);
    return STATUS_SUCCESS;
}
