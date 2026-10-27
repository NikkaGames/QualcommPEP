#ifndef POFX_FS_REQUIRED_H
#define POFX_FS_REQUIRED_H

#include "perf_clients.h"
#include "factory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POFX_FS_REQUIRED_SLOTS 8u

typedef struct pofx_fs_required_slot {
    const pofx_required_action_t* action;
    pofx_required_action_t snapshot;
    uint64_t native;
    uintptr_t cookie;
    uint8_t active;
} pofx_fs_required_slot_t;

typedef struct pofx_fs_required {
    fs_factory_provider_t native;
    pofx_fs_required_slot_t slots[POFX_FS_REQUIRED_SLOTS];
    char domain[FS_NAME_BYTES];
    char res[FS_FACTORY_NAME_BYTES];
    uint64_t epoch;
    uintptr_t next_cookie;
    int (*read_physical)(void*, const char*, uint64_t, uint32_t*);
    void* physical_ctx;
    uint8_t bound;
    uint8_t quarantined;
} pofx_fs_required_t;

/* open_idle_client creates NPA_CLIENT_REQUIRED clients (0x40), independently of D-state software
 * handles. */
int po_fx_fs_required_bind(pofx_fs_required_t* scope, const fs_factory_provider_t* native,
                           const char* domain, uint64_t epoch, void* physical_ctx,
                           int (*read_physical)(void*, const char*, uint64_t, uint32_t*));
void po_fx_fs_required_provider(pofx_fs_required_t* scope, pofx_required_provider_t* out);
/* an issued vote must be witnessed by both its native client and GDSC. An inconclusive
 * write quarantines the scope and retains its handles. */
int po_fx_fs_required_issue(pofx_fs_required_t* scope, const pofx_required_action_t* action,
                            void* handle);
/* serialized ON/OFF handoff under the owning res-graph lock. The incoming request is
 * posted before withdrawing the previous one, so the OEM NPA aggregator never observes an
 * invalid zero request. */
int po_fx_fs_required_handoff(pofx_fs_required_t* scope, const pofx_required_action_t* previous,
                              void* previous_handle, const pofx_required_action_t* next,
                              void* next_handle);
int po_fx_fs_required_complete(pofx_fs_required_t* scope, const pofx_required_action_t* action,
                               void* handle);
/* the final OFF vote is not completed through the invalid-zero aggregate. */
int po_fx_fs_required_retire_off(pofx_fs_required_t* scope, const pofx_required_action_t* action,
                                 void* handle);
int po_fx_fs_required_unbind(pofx_fs_required_t* scope);

#ifdef __cplusplus
}
#endif
#endif
