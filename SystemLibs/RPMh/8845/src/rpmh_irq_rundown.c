#include "rpmh_irq_rundown.h"
#include <string.h>

/* kernel builds use WDK interlocked operations, not C11 stdatomic support (MSVC/WDK
 * compatibility). */

#if defined(_MSC_VER)
#include <ntddk.h>

static int64_t load64(const volatile int64_t* p)
{
    return (int64_t)InterlockedCompareExchange64((volatile LONG64*)p, 0, 0);
}

static int64_t cas64(volatile int64_t* p, int64_t expected, int64_t wanted)
{
    return (int64_t)InterlockedCompareExchange64((volatile LONG64*)p, (LONG64)wanted,
                                                 (LONG64)expected);
}

static void store64(volatile int64_t* p, int64_t v)
{
    (void)InterlockedExchange64((volatile LONG64*)p, (LONG64)v);
}
#else
static int64_t load64(const volatile int64_t* p)
{
    return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

static int64_t cas64(volatile int64_t* p, int64_t expected, int64_t wanted)
{
    (void)__atomic_compare_exchange_n(p, &expected, wanted, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

static void store64(volatile int64_t* p, int64_t v)
{
    __atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}
#endif

/* owner epochs are full unsigned 64-bit identities. last_epoch is stored with a 64-bit
 * signed interlocked primitive only because WDK exposes that signature; bit-copy instead
 * of performing out-of-range signed conversions. */

static uint64_t load_epoch(const volatile int64_t* p)
{
    int64_t bits = load64(p);
    uint64_t epoch;
    memcpy(&epoch, &bits, sizeof(epoch));
    return epoch;
}

static void store_epoch(volatile int64_t* p, uint64_t epoch)
{
    int64_t bits;
    memcpy(&bits, &epoch, sizeof(bits));
    store64(p, bits);
}

/* epoch and the packed counter are separate atomics. An ISR can observe the old epoch, be
 * preempted across Close/next Open, then succeed in the same 0->1 packed-word state (ABA). */

#if defined(RPMH_IRQ_GATE_TEST_HOOK)
extern void rpmh_irq_gate_before_pin_cas_test_hook(void);
#define GATE_PIN_CAS_TEST_HOOK() rpmh_irq_gate_before_pin_cas_test_hook()
#else
#define GATE_PIN_CAS_TEST_HOOK() ((void)0)
#endif

/* bit 0 = open; the remaining bits count outstanding lifetime tickets. */
#define GATE_OPEN ((int64_t)1)
#define GATE_TICKET ((int64_t)2)

/* fast-fail owner-control CAS. */
static int gate_control_enter(rpmh_irq_gate_t* g)
{
    return cas64(&g->control_busy, 0, 1) == 0;
}

static void gate_control_leave(rpmh_irq_gate_t* g)
{
    store64(&g->control_busy, 0);
}
#if defined(RPMH_IRQ_CLOSE_TEST_HOOK)
extern void rpmh_irq_gate_before_close_cas_test_hook(void);
#define GATE_CLOSE_TEST_HOOK() rpmh_irq_gate_before_close_cas_test_hook()
#else
#define GATE_CLOSE_TEST_HOOK() ((void)0)
#endif
/* test-only barrier while Close has cleared the admission bit but still owns the
 * owner-control field. */

#if defined(RPMH_IRQ_CLOSE_COMMIT_TEST_HOOK)
extern void rpmh_irq_gate_after_close_cas_test_hook(void);
#define GATE_CLOSE_COMMIT_TEST_HOOK() rpmh_irq_gate_after_close_cas_test_hook()
#else
#define GATE_CLOSE_COMMIT_TEST_HOOK() ((void)0)
#endif
/* keep well away from signed overflow (all comparisons stay in int64_t). */
#define GATE_MAX_PACKED ((int64_t)0x7FFFFFFFFFFFFFFD)

void rpmh_irq_gate_init(rpmh_irq_gate_t* g)
{

    if (!g)
        return;
    store_epoch(&g->last_epoch, 0);
    store64(&g->packed, 0);
    store64(&g->control_busy, 0);
}

int rpmh_irq_gate_open(rpmh_irq_gate_t* g, uint64_t epoch)
{
    uint64_t last;
    int ok = 0;
    if (!g || !epoch || epoch == UINT64_MAX)
        return 0;
    /* without this mutual exclusion, an old Close could read the old epoch, pause, then CAS a
     * new epoch's re-opened packed=1 back to zero. The ISR never takes this lock. */

    if (!gate_control_enter(g))
        return 0;
    if (load64(&g->packed) == 0) {
        last = load_epoch(&g->last_epoch);
        if (epoch > last) {
            /* the control lock prevents another owner transition while last_epoch and packed are
             * published separately. */

            store_epoch(&g->last_epoch, epoch);
            ok = cas64(&g->packed, 0, GATE_OPEN) == 0;
        }
    }
    gate_control_leave(g);
    return ok;
}

int rpmh_irq_gate_try_pin(rpmh_irq_gate_t* g, uint64_t epoch, rpmh_irq_ticket_t* t)
{
    int64_t cur;
    if (!g || !t || !epoch || t->active || t->epoch)
        return 0;
    if (load_epoch(&g->last_epoch) != epoch)
        return 0;
    for (;;) {
        cur = load64(&g->packed);
        if (!(cur & GATE_OPEN) || cur > GATE_MAX_PACKED)
            return 0;
        if (load_epoch(&g->last_epoch) != epoch)
            return 0;
        GATE_PIN_CAS_TEST_HOOK();
        if (cas64(&g->packed, cur, cur + GATE_TICKET) == cur) {
            int64_t live;
            if (load_epoch(&g->last_epoch) == epoch) {
                /* the successful CAS owns one count: Close cannot reopen the gate until that count
                 * is retired. */

                t->epoch = epoch;
                t->active = 1;
                return 1;
            }
            /* won an ABA race against a later epoch. The new gate's count includes our unpublished
             * ticket, so Close/Open cannot recycle this generation until remove exactly that count.
             */

            for (;;) {
                live = load64(&g->packed);
                if (live < GATE_TICKET)
                    return 0; /* defensive invariant. */
                if (cas64(&g->packed, live, live - GATE_TICKET) == live)
                    return 0;
            }
        }
    }
}

int rpmh_irq_gate_retain_ticket(rpmh_irq_gate_t* g, const rpmh_irq_ticket_t* src,
                                rpmh_irq_ticket_t* retained)
{
    int64_t cur;
    if (!g || !src || !retained || src == retained || !src->active || !src->epoch ||
        retained->active || retained->epoch || load_epoch(&g->last_epoch) != src->epoch)
        return 0;
    /* never change the bit or reopen admissions. The owner serializes this with retirement of
     * 'src'. */

    for (;;) {
        cur = load64(&g->packed);
        if ((cur & ~GATE_OPEN) < GATE_TICKET || cur > GATE_MAX_PACKED)
            return 0;
        if (cas64(&g->packed, cur, cur + GATE_TICKET) == cur) {
            retained->epoch = src->epoch;
            retained->active = 1;
            return 1;
        }
    }
}

int rpmh_irq_gate_close(rpmh_irq_gate_t* g, uint64_t epoch)
{
    int64_t cur;
    int ok = 0;
    if (!g || !epoch)
        return 0;
    if (!gate_control_enter(g))
        return 0;
    if (load_epoch(&g->last_epoch) == epoch) {
        GATE_CLOSE_TEST_HOOK();
        for (;;) {
            cur = load64(&g->packed);
            if (!(cur & GATE_OPEN))
                break;
            if (cas64(&g->packed, cur, cur - GATE_OPEN) == cur) {
                GATE_CLOSE_COMMIT_TEST_HOOK();
                ok = 1;
                break;
            }
        }
    }
    gate_control_leave(g);
    return ok;
}

int rpmh_irq_gate_retire_ticket(rpmh_irq_gate_t* g, rpmh_irq_ticket_t* t)
{
    int64_t cur;
    if (!g || !t || !t->active || !t->epoch || load_epoch(&g->last_epoch) != t->epoch)
        return 0;
    for (;;) {
        cur = load64(&g->packed);
        if ((cur & ~GATE_OPEN) < GATE_TICKET)
            return 0;
        if (cas64(&g->packed, cur, cur - GATE_TICKET) == cur) {
            t->active = 0;
            t->epoch = 0;
            return 1;
        }
    }
}

int rpmh_irq_gate_closed_and_drained(const rpmh_irq_gate_t* g, uint64_t epoch)
{
    /* packed == 0 is insufficient while owner control is still committing the transition. A
     * concurrent release must fail closed during this interval instead of observing a
     * prematurely drained controller. */

    if (!g || !epoch || load64(&g->control_busy) != 0)
        return 0;
    if (load_epoch(&g->last_epoch) != epoch || load64(&g->packed) != 0)
        return 0;
    return load64(&g->control_busy) == 0 && load_epoch(&g->last_epoch) == epoch &&
           load64(&g->packed) == 0;
}

uint64_t rpmh_irq_gate_outstanding(const rpmh_irq_gate_t* g)
{
    if (!g)
        return 0;
    return ((uint64_t)load64(&g->packed)) >> 1;
}
