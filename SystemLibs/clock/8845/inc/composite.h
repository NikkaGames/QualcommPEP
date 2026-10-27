#ifndef CLOCK_COMPOSITE_H
#define CLOCK_COMPOSITE_H
/* enable-and-rate uses separate enable and frequency requests in that order. */

#include "scalar.h"
#include "action_opcode.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct clock_composite {
    scalar_res_t* enable;
    scalar_res_t* freq;
    scalar_handle_t enable_client;
    scalar_handle_t freq_client;
    uint8_t active;
    uint8_t quarantined;
    uint8_t native_npa;
} clock_composite_t;

typedef struct clock_composite_snapshot {
    uint32_t enable_req;
    uint32_t freq_req;
    uint8_t enable_present;
    uint8_t freq_present;
    uint32_t match_opaque;
    uint32_t enable_slot;
    uint32_t enable_generation;
    uint32_t freq_slot;
    uint32_t freq_generation;
} clock_composite_snapshot_t;

/* no implicit mapping or physical ownership is granted. Returned clients are for a single
 * graph/D-state owner only. */

int clock_composite_bind(clock_composite_t*, scalar_res_t* enable, scalar_res_t* freq);
/* the second DSDT operand is an opaque match policy. */

int clock_composite_issue(clock_composite_t*, const power_action_t*,
                          clock_composite_snapshot_t* prior);
/* D3 issues enable=2 while retaining the frequency vote; OFF is distinct from completion and
 * destruction. */

int clock_composite_release(clock_composite_t*, clock_composite_snapshot_t* prior);
/* restore only this client's two votes, compensating a partially completed hardware
 * transaction; quarantine if either independent readback fails. */

int clock_composite_restore(clock_composite_t*, const clock_composite_snapshot_t* prior);
/* read-only, ownership-checked client snapshot and state validation. effective hardware
 * readback is mandatory; another client's aggregate vote can be higher than this
 * composite's own state. */

int clock_composite_snapshot(clock_composite_t*, clock_composite_snapshot_t*);
int clock_composite_verify_state(clock_composite_t*, uint8_t enabled, uint32_t hertz);
int clock_composite_unbind(clock_composite_t*);
#ifdef __cplusplus
}
#endif
#endif
