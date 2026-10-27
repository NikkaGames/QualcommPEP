#ifndef NPA_CLIENT_LIFECYCLE_H
#define NPA_CLIENT_LIFECYCLE_H
/* state-owned NPA clients are keyed by resource identity and client type. */

#include "res_graph.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NPA_MAX_CLIENTS 128u
#define NPA_NAME_BYTES POWER_NAME_BYTES

typedef struct npa_key {
    power_action_kind_t kind;
    char dev_path[256];
    uint32_t d_state;
    char res[NPA_NAME_BYTES];
    /* busarb unique-client selectors from the unmodified ACPI res. client is NOT assumed to be
     * the Windows RPMh driver's numeric ID. */

    char slave[NPA_NAME_BYTES];
    uint32_t client;
    /* firmware ICB endpoint IDs are distinct from the RSC driver ID and NPA client type. */

    uint32_t icb_master_id;
    uint32_t icb_slave_id;
    /* bit n records raw ACPI selector n; extended selectors need a matching provider. */

    uint32_t clock_mode_bits;
} npa_key_t;

typedef struct npa_slot {
    npa_key_t key;
    void* handle;
    uint8_t active;
} npa_slot_t;

typedef struct npa_callbacks {
    void* ctx;
    /* reject unknown resource/client types; return a handle only after authenticated provider
     * creation. */

    int (*owns)(void*, const npa_key_t*);
    void* (*create)(void*, const npa_key_t*);
    /* read-only check that handle is STILL owned by this provider. */
    int (*validate)(void*, const npa_key_t*, void*);
    /* hardware-authoritative readback that THIS client has NO outstanding requested vote.
     * Called before destroy, not inferred from aggregate RPMh ACK or another client's vote. */

    int (*is_released)(void*, const npa_key_t*, void*);
    /* return 1 ONLY if handle was destroyed/released or independently confirmed absent. */

    int (*destroy)(void*, const npa_key_t*, void*);
} npa_callbacks_t;

typedef struct npa_registry {
    npa_slot_t slots[NPA_MAX_CLIENTS];
    uint32_t cnt;
    uint32_t res_operations;
    uint32_t non_npa_operations;
    uint8_t inventoried;
    uint8_t active;
    uint8_t quarantined;
    npa_callbacks_t provider;
    /* borrowed stable graph: caller ensures graph and lock lifetime extend through client
     * destruction. */

    const res_graph_t* owner_graph;
} npa_registry_t;

/* the src graph MUST already be sealed. Caller synchronizes publication/lifetime of
 * graph/registry externally. */

/* resource names are case-insensitive; device paths, endpoints and BUSARB selectors are exact. */

int npa_same_res_identity(const npa_key_t*, const npa_key_t*);
/* return the 8-byte [master, slave] reference only for verified firmware routes. */

int npa_build_bus_client_reference(const npa_key_t*, uint32_t out[2]);
int npa_inventory(npa_registry_t*, const res_graph_t*);
/* open every state-owned NPA client or fail with rollback. No firmware power vote is
 * issued here. */

int npa_activate(npa_registry_t*, const npa_callbacks_t*);
/* success requires all live per-client votes to be independently verified released, and
 * every client destroyed. graph lock serializes teardown with res transactions; destroy
 * and is_released MUST NOT re-enter it. */

int npa_release(npa_registry_t*);
/* retain graph, lock, registry and its provider context whenever ANY handle is unresolved.
 * Does not issue NPA requests and never clears a quarantine. */

int npa_needs_retention(const npa_registry_t*);
/* necessary but INSUFFICIENT for PoFx ownership: ignores component states, provider
 * votability, Windows driver ABI and hardware execution. */

int npa_dev_clients_ready(const npa_registry_t*, const char*);
/* caller must retain graph lifetime and must not already hold its nonrecursive lock. live
 * provider validate is called while the graph lock is held. */

/* publication-only check when caller ALREADY holds the graph lock. Requires matching
 * registry ownership and a live sealed non-draining graph. */

int npa_dev_clients_ready_held(const npa_registry_t*, const res_graph_t*, const char*);
int npa_dev_clients_ready_locked(const npa_registry_t*, const char*);
/* exact path + D-state + action, with live ownership revalidated. The slot is borrowed:
 * externally serialize registry/provider teardown with graph. */

int npa_resolve_action(const npa_registry_t*, const char*, uint32_t, const power_action_t*,
                       const npa_slot_t**);
#ifdef __cplusplus
}
#endif
#endif
