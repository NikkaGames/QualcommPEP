/* hw_backend8845.c - Production hardware-backend reg, the fail-closed execution
 * verification gate, and the graph-provider forwarder. in the WDK build, reg happens once
 * at EvtDevicePrepareHardware after the authoritative Windows res drivers are enumerated;
 * callers serialize. */

#include "hw_backend.h"
#include <string.h>
/* windows kernel: MSVC atomic intrinsics. */
#if defined(_MSC_VER)
#include <intrin.h>
static volatile long g_dispatch_state = (-2147483647L - 1L);

static uint32_t rundown_load(void)
{
    return (uint32_t)_InterlockedCompareExchange(&g_dispatch_state, 0L, 0L);
}

static int rundown_cas(uint32_t previous, uint32_t desired)
{
    return (uint32_t)_InterlockedCompareExchange(&g_dispatch_state, (long)desired,
                                                 (long)previous) == previous;
}
#else
#include <stdatomic.h>
static _Atomic(uint32_t) g_dispatch_state = ATOMIC_VAR_INIT(0x80000000u);

static uint32_t rundown_load(void)
{
    return atomic_load_explicit(&g_dispatch_state, memory_order_acquire);
}

static int rundown_cas(uint32_t previous, uint32_t desired)
{
    return atomic_compare_exchange_strong_explicit(&g_dispatch_state, &previous, desired,
                                                   memory_order_acq_rel, memory_order_acquire);
}
#endif
#define RUNDOWN_CLOSED 0x80000000u
#define RUNDOWN_COUNT 0x7fffffffu

/* only the lifecycle owner reopens after installing and validating a complete new table. */

static void rundown_close(void)
{
    uint32_t previous;
    do {
        previous = rundown_load();
        if (previous & RUNDOWN_CLOSED)
            return;
    } while (!rundown_cas(previous, previous | RUNDOWN_CLOSED));
}

static int rundown_idle(void)
{
    return (rundown_load() & RUNDOWN_COUNT) == 0u;
}

static int rundown_open(void)
{
    return rundown_cas(RUNDOWN_CLOSED, 0u);
}

static hw_backend_t g_backend;
static int g_registered;
static uint64_t g_epoch; /* latched owner epoch. */
/* only hw_backend_validate sets this field, after hardware readback. */

static int g_validated;
/* a validation started for an old reg must not authorize a later reg, even when the owner
 * reuses the same epoch value. */

static uint64_t g_reg_serial;

static int backend_ops_complete(const hw_backend_t* b)
{
    return b && b->supports && b->snapshot && b->apply && b->verify && b->restore &&
           b->verify_restored && b->verify_final && b->probe_readback && b->self_test;
}

int hw_backend_reg(const hw_backend_t* backend)
{
    if (!backend)
        return -1;
    if (backend->version != HW_BACKEND_VERSION)
        return -2;
    if (backend->owner_epoch == 0u)
        return -3;
    if (!backend_ops_complete(backend))
        return -4;
    /* reject silent replacement by a different owner. Re-reg by the same epoch is idempotent
     * (a provider re-asserting after a transient). */

    if (g_registered && g_epoch != 0u && g_epoch != backend->owner_epoch)
        return -5;

    rundown_close();
    if (!rundown_idle())
        return -6;
    memcpy(&g_backend, backend, sizeof(g_backend));
    ++g_reg_serial;
    g_epoch = backend->owner_epoch;
    g_registered = 1;
    /* a re-reg must re-prove itself on the hardware. */
    g_validated = 0;
    return 0;
}

/* wdf d0 exit must close PoFx/DPM admission BEFORE the framework tears down the interrupt
 * and the backend unmaps RSC/GICD. Unlike unregister, retain the immutable callback table
 * so that a later d0 generation can undergo a *new* physical verification. */

int hw_backend_quiesce_d0(uint64_t owner_epoch)
{
    if (!g_registered || !owner_epoch || owner_epoch != g_epoch)
        return 0;
    rundown_close();
    g_validated = 0;
    return rundown_idle();
}

int hw_backend_unregister(uint64_t owner_epoch)
{
    if (!g_registered || owner_epoch == 0u || owner_epoch != g_epoch)
        return 0;
    rundown_close();
    if (!rundown_idle())
        return 0; /* close admission, but never clear the in-use table. */
    memset(&g_backend, 0, sizeof(g_backend));
    ++g_reg_serial;
    g_epoch = 0u;
    g_registered = 0;
    g_validated = 0;
    return 1;
}

const hw_backend_t* hw_backend_get(void)
{
    return g_registered ? &g_backend : 0;
}

/* all checks must pass, including attestation and independent live readback. */

/* this is an OWNER operation, not a way to revoke someone else's verified backend. A
 * mismatched epoch must be a pure rejection. */

int hw_backend_validate(uint64_t owner_epoch)
{
    const hw_backend_t* b;
    uint64_t v1 = 0u, v2 = 0u;
    uint64_t serial;

    if (!g_registered || owner_epoch == 0u || owner_epoch != g_epoch)
        return 0;
    rundown_close();
    if (!rundown_idle())
        return 0; /* live graph callbacks must finish. */
    serial = g_reg_serial;
    g_validated = 0;
    b = &g_backend;
    if (b->version != HW_BACKEND_VERSION || !backend_ops_complete(b) ||
        b->production_attestation != HW_ATTEST_MAGIC)
        return 0;
    if (!b->self_test(b->ctx))
        return 0;
    if (!g_registered || g_epoch != owner_epoch || g_reg_serial != serial)
        return 0;
    if (!b->probe_readback(b->ctx, (uint32_t)HW_DOMAIN_CLOCK, 0u, &v1))
        return 0;
    if (!g_registered || g_epoch != owner_epoch || g_reg_serial != serial)
        return 0;
    if (!b->probe_readback(b->ctx, (uint32_t)HW_DOMAIN_CLOCK, 0u, &v2))
        return 0;
    if (!g_registered || g_epoch != owner_epoch || g_reg_serial != serial || v1 != v2)
        return 0;
    g_validated = 1;
    return rundown_open();
}

int hw_backend_ready(void)
{
    /* no unleased reading of the mutable callback table from PoFx DPM. */

    return !(rundown_load() & RUNDOWN_CLOSED);
}

/* epoch alone is not sufficient for bound-graph identity. */
uint64_t hw_backend_verified_serial(void)
{
    return hw_backend_ready() ? g_reg_serial : 0u;
}

const hw_backend_t* hw_backend_get_bound(uint64_t serial)
{
    /* graph callbacks MUST use BeginBound/EndBound instead. */

    if (!serial || serial != g_reg_serial || !hw_backend_ready())
        return 0;
    return &g_backend;
}

/* pin the original reg across an entire provider call. Close admission before mutation,
 * and DO NOT free owner storage until all leases drain (or quarantine the owner when
 * teardown cannot wait). */

int hw_backend_begin_bound(uint64_t serial, hw_backend_t* copy)
{
    uint32_t previous;
    if (!serial || !copy)
        return 0;
    for (;;) {
        previous = rundown_load();
        if ((previous & RUNDOWN_CLOSED) || (previous & RUNDOWN_COUNT) == RUNDOWN_COUNT)
            return 0;
        if (rundown_cas(previous, previous + 1u))
            break;
    }
    /* lifecycle writers must first close admission and wait for zero leases, so this table and
     * serial remain immutable until EndBound. */

    if (g_registered && g_validated && g_reg_serial == serial && backend_ops_complete(&g_backend)) {
        memcpy(copy, &g_backend, sizeof(*copy));
        return 1;
    }
    hw_backend_end_bound();
    return 0;
}

void hw_backend_end_bound(void)
{
    uint32_t previous;
    do {
        previous = rundown_load();
        if ((previous & RUNDOWN_COUNT) == 0u)
            return;
    } while (!rundown_cas(previous, previous - 1u));
}

uint32_t hw_backend_rundown_cnt(void)
{
    return rundown_load() & RUNDOWN_COUNT;
}
