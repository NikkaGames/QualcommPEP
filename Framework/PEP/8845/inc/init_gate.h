#ifndef INIT_GATE_H
#define INIT_GATE_H

/* caller supplies Windows InterlockedCompareExchange/InterlockedExchange and LONG (host
 * tests provide compatible atomic shims). Never spin at DISPATCH_LEVEL waiting for an
 * initializer on a preempted CPU. */

static __inline int init_gate_begin(volatile LONG* gate)
{
    LONG prev = InterlockedCompareExchange(gate, 1, 0);
    return prev == 0 ? 1 : (prev == 2 ? 2 : 0);
}

static __inline void init_gate_publish(volatile LONG* gate)
{
    InterlockedExchange(gate, 2);
}

static __inline int init_gate_ready(volatile LONG* gate)
{
    return InterlockedCompareExchange(gate, 2, 2) == 2;
}
#endif
