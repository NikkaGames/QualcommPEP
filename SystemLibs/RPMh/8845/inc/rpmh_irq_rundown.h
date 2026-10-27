#ifndef RPMH_IRQ_RUNDOWN_H
#define RPMH_IRQ_RUNDOWN_H
/* owner-NEUTRAL asynchronous callback lifetime contract. No IRQ, MMIO, WDF handle, or
 * hardware ownership token exists here. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct rpmh_irq_gate {

    volatile int64_t packed;
    volatile int64_t last_epoch; /* atomic bits of FULL uint64 owner epoch. */
    /* serializes owner Open/Close on this exact instance. Only owner-control transitions use
     * this atomic; ISR admission remains nonblocking and relies on its existing post-CAS epoch
     * validation. */

    volatile int64_t control_busy;
} rpmh_irq_gate_t;

typedef struct rpmh_irq_ticket {
    uint64_t epoch;
    uint8_t active;
} rpmh_irq_ticket_t;

void rpmh_irq_gate_init(rpmh_irq_gate_t* gate);
/* only for a provider whose ownership has been independently verified. Externally
 * serialized PASSIVE_LEVEL entry: requires closed+drained and a strictly increasing epoch,
 * including across d0 suspend/resume. */

int rpmh_irq_gate_open(rpmh_irq_gate_t* gate, uint64_t epoch);
/* lock-free admission before touching owned hardware in the ISR. */
int rpmh_irq_gate_try_pin(rpmh_irq_gate_t* gate, uint64_t epoch, rpmh_irq_ticket_t* ticket);
/* retain a second lifetime token from an already active, authenticated ticket, including
 * AFTER Close. used to protect the owner during the hardware unmask operation after the
 * original ISR ticket is retired. */

int rpmh_irq_gate_retain_ticket(rpmh_irq_gate_t* gate, const rpmh_irq_ticket_t* src,
                                rpmh_irq_ticket_t* retained);
/* deny admissions immediately; never claims outstanding work completed. */
int rpmh_irq_gate_close(rpmh_irq_gate_t* gate, uint64_t epoch);
/* release only after verified terminal disposition: completed ACK/unmask, or no ISR hardware
 * access. Pending, quarantined or unknown IRQs retain ownership. */

int rpmh_irq_gate_retire_ticket(rpmh_irq_gate_t* gate, rpmh_irq_ticket_t* ticket);
/* can delete WDF objects or unmap MMIO ONLY after the framework has also
 * synchronized/disabled its IRQ, flushed DPCs, and drained all work items. refuses to
 * report a drained owner while control_busy is held: clearing packed during Close does not
 * complete the owner transition. This is a non-owning. */

int rpmh_irq_gate_closed_and_drained(const rpmh_irq_gate_t* gate, uint64_t epoch);
uint64_t rpmh_irq_gate_outstanding(const rpmh_irq_gate_t* gate);
#ifdef __cplusplus
}
#endif
#endif
