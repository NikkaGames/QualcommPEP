#ifndef DCVS_RUNDOWN_H
#define DCVS_RUNDOWN_H

/* in-process DCVS rundown for DISPATCH_LEVEL PPM callbacks. Interlocked operations provide
 * the acquire/release fence; the caller must ensure BeginStop and the subsequent wait/free
 * happen at PASSIVE_LEVEL. */

/* split the observed-epoch step so a host test can deterministically exercise an entrant
 * preempted across stop/re-initialize. */

static __inline BOOLEAN dcvs_rundown_enter_observed(volatile LONG* state, volatile LONG* users,
                                                    volatile LONG* epoch, LONG generation)
{
    if (InterlockedCompareExchange(state, 2, 2) != 2)
        return FALSE;
    InterlockedIncrement(users);
    if (InterlockedCompareExchange(state, 2, 2) != 2 ||
        InterlockedCompareExchange(epoch, 0, 0) != generation) {
        InterlockedDecrement(users);
        return FALSE;
    }
    return TRUE;
}

static __inline BOOLEAN dcvs_rundown_enter(volatile LONG* state, volatile LONG* users,
                                           volatile LONG* epoch)
{
    LONG generation = InterlockedCompareExchange(epoch, 0, 0);
    return dcvs_rundown_enter_observed(state, users, epoch, generation);
}

static __inline VOID dcvs_rundown_leave(volatile LONG* users)
{
    InterlockedDecrement(users);
}

static __inline BOOLEAN dcvs_rundown_begin_stop(volatile LONG* state)
{
    return InterlockedCompareExchange(state, 3, 2) == 2 ? TRUE : FALSE;
}

static __inline BOOLEAN dcvs_rundown_empty(volatile LONG* users)
{
    return InterlockedCompareExchange(users, 0, 0) == 0 ? TRUE : FALSE;
}
#endif
