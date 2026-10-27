#ifndef NPA_CLIENT_LIFECYCLE_H
#define NPA_CLIENT_LIFECYCLE_H
/* source-derived Qualcomm NPA state-res client lifecycle. SC8280
 * PepCreateDeviceResourceNpaHandles/ProcessState creates state-owned res clients and
 * GetBusClient keys the interconnect client by master/slave/driver/res type. */

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
    /* molokai firmware icbid.h numeric endpoints for BUSARB. These IDs are NOT a validated
     * 8480 RSC driver ID or NPA client type. */

    uint32_t icb_master_id;
    uint32_t icb_slave_id;
    /* this mask preserves EVERY raw ACPI action selector (bit n = selector n) required of the
     * eventual multi-handle Windows clock provider. Not an interpretation of the 8480 opcode
     * or NPA client type; in particular extended selector 8 needs separate 8480 proof. */

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

/* qualcomm legacy NPA identity comparison: ASCII res names are case-insensitive, ACPI
 * instance paths, numeric ICB endpoints and the raw ACPI BUSARB client selector remain
 * exact. The unknown 8480 RSC driver and NPA res type are NOT inferred from that selector. */

int npa_same_res_identity(const npa_key_t*, const npa_key_t*);
/* original 8280 GetBusClient passes a pointer to two 32-bit endpoints with client_value=8
 * to npa_create_sync_client_ex. Return this exact 8-byte [Master,Slave] reference only for
 * verified Molokai firmware routes; invalid/unowned res identities leave output zero. */

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
