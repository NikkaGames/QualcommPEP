/* state-scoped FOOTSWITCH native NPA client registry. The original 8280 GetNpaClientHandle
 * searches the current state-set client list by res name and native client type before
 * constructing. */

#ifndef FOOTSWITCH_REGISTRY_H
#define FOOTSWITCH_REGISTRY_H
#include "factory.h"
#ifdef __cplusplus
extern "C" {
#endif

#define FS_REGISTRY_CAPACITY 32u
#define FS_REGISTRY_LEASES 64u

typedef struct fs_registry_entry {
    fs_factory_t factory;
    char client_name[FS_FACTORY_NAME_BYTES];
    uint32_t state_set;
    uint32_t state_idx;
    uint32_t references;
    uint8_t family;
    uint8_t in_use;
} fs_registry_entry_t;

typedef struct fs_registry_lease {
    uint32_t entry_idx;
    uint32_t generation;
    uint8_t active;
} fs_registry_lease_t;

typedef struct fs_registry {
    fs_registry_entry_t entries[FS_REGISTRY_CAPACITY];
    fs_registry_lease_t leases[FS_REGISTRY_LEASES];
    uint32_t last_generation[FS_REGISTRY_LEASES];
    uint8_t quarantined;
} fs_registry_t;

/* valid actions 1/2 share a normal NPA client, 3/4 share a HW-control client.
 * state_set/state_idx MUST come from the externally verified PEP device-state owner, never
 * derived from action or F/P state alone. */

fs_result_t fs_registry_acquire(fs_registry_t* registry, uint32_t state_set, uint32_t state_idx,
                                const char* domain, const char* client_name, uint32_t action,
                                const fs_factory_provider_t* provider, uint64_t* ticket);

fs_result_t fs_registry_issue(fs_registry_t* registry, uint64_t ticket, uint32_t action);
fs_result_t fs_registry_verify(fs_registry_t* registry, uint64_t ticket);
/* complete only an exclusively-held client; a shared borrow must not cancel another
 * holder's NPA vote. */

fs_result_t fs_registry_complete(fs_registry_t* registry, uint64_t ticket);

/* last release destroys the owned native client only if independently confirmed idle:
 * scalar DISABLE (2/4) is NOT idle or a destruction token. A failed close preserves the
 * slot and its reference for owner recovery. */

/* only after independently verified OFF and whole-res callback rundown. */
fs_result_t fs_registry_retire_off(fs_registry_t* registry, uint64_t ticket);
fs_result_t fs_registry_release(fs_registry_t* registry, uint64_t ticket);

#ifdef __cplusplus
}
#endif
#endif
