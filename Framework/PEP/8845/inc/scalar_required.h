#ifndef POFX_SCALAR_REQUIRED_H
#define POFX_SCALAR_REQUIRED_H

#include "perf_clients.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* these identities belong to separate F/P required clients; they must never alias the
 * D-state ACPI-path voter or one another. */
typedef struct scalar_required_io {
    void* ctx;
    int (*read_epoch)(void*, uint64_t*);
    int (*owns)(void*, const power_action_t*);
    int (*read_vote)(void*, const char*, const power_action_t*, uint32_t*);
    int (*issue)(void*, const char*, const power_action_t*);
    int (*withdraw)(void*, const char*, const power_action_t*);
    /* independent transport/MMIO verification, not an echo of cached votes. */
    int (*verify_physical)(void*, const power_action_t*);
} scalar_required_io_t;

typedef struct scalar_required_slot {
    const pofx_required_action_t* action;
    pofx_required_action_t snapshot;
    char identity[256];
    uintptr_t cookie;
    uint8_t live;
} scalar_required_slot_t;

typedef struct scalar_required {
    scalar_required_io_t io;
    scalar_required_slot_t slots[POFX_REQUIRED_MAX_ACTIONS];
    uint64_t epoch;
    uintptr_t serial;
    uint8_t bound;
    uint8_t quarantined;
} scalar_required_t;

int scalar_required_bind(scalar_required_t* scope, const scalar_required_io_t* io, uint64_t epoch);
void scalar_required_provider(scalar_required_t* scope, pofx_required_provider_t* out);
int scalar_required_issue(scalar_required_t* scope, const pofx_required_action_t* action,
                          void* handle);
int scalar_required_withdraw(scalar_required_t* scope, const pofx_required_action_t* action,
                             void* handle);
int scalar_required_unbind(scalar_required_t* scope);

#ifdef __cplusplus
}
#endif
#endif
