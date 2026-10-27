#ifndef RES_GRAPH_H
#define RES_GRAPH_H
/* molokai device -> D-state -> provider binding, independent of PoFx. No global device
 * ownership is inferred from an ACPI method or plan. */

#include "state_txn.h"
#include "ufs_catalog.h"
#ifdef __cplusplus
extern "C" {
#endif
/* runtime res submissions already support 32 DEVICE packages; graph discovery must not
 * silently reject the ninth future PEP scope. graph/working memory remains caller-owned
 * NONPAGED pool (roughly 1.21 MiB graph at this capacity on 64-bit hosts). */

#define GRAPH_MAX_DEVICES 32u
#define GRAPH_PROVIDER_CNT 9u
#define GRAPH_SRC_MAX 32u

typedef struct graph_desc {
    char method[5];
    const void* buf;
    size_t bytes;
} graph_desc_t;

typedef struct graph_provider {
    void* ctx;

    int (*supports)(void*, const char*, uint32_t, const power_action_t*);
    int (*snapshot)(void*, const char*, uint32_t, const power_action_t*, power_snapshot_t*);
    int (*apply)(void*, const char*, uint32_t, const power_action_t*);
    int (*verify)(void*, const char*, uint32_t, const power_action_t*);
    int (*restore)(void*, const char*, uint32_t, const power_action_t*, const power_snapshot_t*);
    int (*verify_restored)(void*, const char*, uint32_t, const power_action_t*,
                           const power_snapshot_t*);
    /* check FINAL provider-owned votes, not replay intermediate reset GPIO steps. */
    int (*verify_final)(void*, const char*, uint32_t, const power_plan_t*);
} graph_provider_t;

typedef struct graph_dev {
    char path[256];
    power_plan_t d0, d3;
    uint8_t has_d0, has_d3, prepared;
    /* distinguish PREPARE admission from the first provider transaction. ABANDON before the
     * first d0 is metadata-only; after an attempted d0 it must never discard ownership without
     * confirmed d3 reconciliation. */

    uint8_t provider_transaction_entered;
    /* DPM admission requires component support and a resource-client backend. */

    uint8_t requires_comp_runtime;
    /* only the reviewed IC17/18/21 one-component f0-only aeo_b profile may use the native
     * d0/d3 provider graph. set by desc inventory after checking exact F/P state IDs and zero
     * f0 actions; never by PnP. */

    uint8_t verified_ic_empty_f0;
    power_dev_t state;
} graph_dev_t;

typedef struct res_graph {
    graph_dev_t devices[GRAPH_MAX_DEVICES];
    graph_provider_t providers[GRAPH_PROVIDER_CNT];
    void* lock_ctx;
    int (*acquire)(void*);
    void (*release)(void*);
    uint32_t dev_cnt;
    /* only the reviewed ACPI desc builder may attest that every discovered device was examined
     * for F/P-state and crashdump contracts. This is an admission gate, never proof of
     * hardware/provider ownership. */

    uint8_t comp_inventory_verified;
    uint8_t sealed;
    /* exactly one owner-verified kind-8 provider may be bound under the graph lock after Seal,
     * but NEVER after PEP publication. */

    uint8_t providers_frozen;
    /* stop graph construction/mutation, new admissions and d0 requests while permitting
     * previously prepared devices to execute their provider-verified d3 release. */

    uint8_t quiescing;
    /* the caller must quiesce incoming Windows callbacks and hold an external lifetime
     * reference before freeing graph storage: a thread paused before graph-lock acquisition is
     * NOT covered by Retire itself. */

    uint8_t retired;
    /* immutable token for the backend reg that bound the provider callbacks. Context points
     * here (graph-owned NONPAGED memory), never to the mutable process-wide backend ops table. */

    /* catalog presence DOES NOT grant UFS PoFx admission or hardware provider rights. */

    ufs_catalog_t ufs_catalog;
    uint64_t native_ufs_owner_epoch; /* physical F/P client attestation, not catalog presence */
    uint64_t backend_binding_serial;
    /* exact ACPI evaluation buffers used to compile this generation. They outlive native F/P
     * clients and are freed only with graph storage. */
    graph_desc_t sources[GRAPH_SRC_MAX];
    uint32_t src_cnt;
    void (*release_src)(const void*);
} res_graph_t;

/* all graph allocations must be caller-owned NONPAGED memory; do not put res_graph_t on a
 * Windows kernel stack. */

void graph_init(res_graph_t*, void*, int (*)(void*), void (*)(void*));
int graph_add_plan(res_graph_t*, const power_plan_t*);
int graph_bind(res_graph_t*, power_action_kind_t, const graph_provider_t*);
int graph_seal(res_graph_t*);
/* takes the nonrecursive graph lock. Cannot replace a provider or mutate an
 * executing/prepared graph. */

int graph_bind_prepublish_footswitch(res_graph_t*, const graph_provider_t*);
/* caller already holds the graph lock. */

int graph_freeze_providers_held(res_graph_t*);

/* exact ACPI path, NOT a Windows PnP hardware ID. */

/* prevents future admission at the graph lock. GraphRetire does NOT provide external
 * callback rundown: stop incoming Windows callbacks and drain all callers BEFORE freeing
 * the graph/provider storage. */

int graph_retire(res_graph_t*);
/* finishdrain fails while any device is prepared, quarantined, or still known to be in d0;
 * the caller must keep providers alive and reconcile before retrying. External Windows
 * callback rundown is still required before freeing the graph. */

int graph_begin_drain(res_graph_t*);
int graph_finish_drain(res_graph_t*);
/* must be invoked while the same graph's nonrecursive lock is held; never call the locking
 * wrapper from inside this check. Not a Windows provider ownership or component/F-state
 * readiness guarantee. */

int graph_can_claim_held(res_graph_t*, const char*);
/* only a physically established required-client owner can promote UFS from
 * component-incomplete to native d0/F/P admission. The witness must be bound to this same
 * graph/backend epoch; this does not apply to any other device. */
int graph_bind_native_ufs_owner(res_graph_t*, uint64_t epoch, void* ctx,
                                int (*verify_physical_owner)(void*, uint64_t));
/* physical d3 readback using the sealed graph's original provider and d3 script. May be
 * queried after GraphAbandonD3 clears prepared. */
int graph_verify_native_ufs_d3(res_graph_t*, const char* exact_path, uint64_t native_epoch);
int graph_can_claim(res_graph_t*, const char*);
/* caller holds this graph's NONRECURSIVE lock. Count each verified d0/d3-only scope
 * independently; COMPONENT/F/P/crashdump scopes are not yet PEP-owned. */

typedef int (*scope_client_ready_t)(void*, const char*);
int graph_audit_dstate_scopes_held(res_graph_t*, void*, scope_client_ready_t,
                                   uint32_t* eligible_scopes);

/* recheck an already PREPARED device at REGISTER, under the graph lock. It does not attest
 * hardware ownership, keep the graph alive, or perform a provider transaction. */

/* a Windows system-transition Dx notify may be consumed by firmware without an OS-issued
 * d3 res script (8280/8480 behavior). Invalidate ONLY the graph's cached state, under its
 * transaction lock. */

int graph_forget_firmware_state(res_graph_t*, const char*);
int graph_is_prepared(res_graph_t*, const char*);
/* the desc inventory has to be verified and the device still prepared. Such a profile
 * advertises zero PoFx components: accepting a nonzero Windows ComponentCount would leave
 * F/P-state callbacks without a provider. */

int graph_reg_dstate_only(res_graph_t*, const char*, uint32_t win_comp_cnt);
/* the IC17/18/21 F0 profile is available only after its D0/D3 transaction is verified. */

int graph_reg_reviewed_ic_comp(res_graph_t*, const char*, uint32_t win_comp_cnt);
/* read-only hardware revalidation of the d0 providers before acknowledging an empty f0
 * ACPI program. Never implies permission to acknowledge f1. */

int graph_verify_reviewed_ic_f0(res_graph_t*, const char*, uint32_t comp, uint32_t f_state);
int graph_prepare(res_graph_t*, const char*);
power_result_t graph_transition(res_graph_t*, const char*, uint32_t, power_execution_t*,
                                power_scratch_t*);
/* dev lifetime adapter for the verified PREPARE/ABANDON path. GraphPrepare is
 * metadata-only; GraphPrepareD0 additionally executes and verifies d0 on OEM-like PREPARE
 * (only with independently verified owner providers). */

power_result_t graph_prepare_d0(res_graph_t*, const char*, power_execution_t*, power_scratch_t*);
power_result_t graph_abandon_d3(res_graph_t*, const char*, power_execution_t*, power_scratch_t*);

/* construction-only ownership transfer. The caller has compiled these same buffers and has
 * not published the graph or created any clients. */
int graph_retain_sources(res_graph_t*, graph_desc_t*, uint32_t, void (*)(const void*));
/* 2: size query, 1: copied, 0: absent/invalid. Locks the graph and requires the exact live
 * backend generation; never falls back to a peer record. */
int graph_src_copy(res_graph_t*, const char*, uint64_t, void*, size_t, size_t*);
/* caller has independently drained graph/provider/PoFx lifetime. This is storage cleanup,
 * not a rundown operation or a hardware release. */
void graph_free_sources(res_graph_t*);

/* build the namespace without allocating approximately 4 KiB of ACPI path tables on an
 * ARM64 kernel stack. workspace must remain unique to an in-progress build; it is zeroed
 * before use and contains no persistent graph pointers. */

typedef struct graph_build_workspace {
    char paths[GRAPH_MAX_DEVICES][256];
    char candidates[GRAPH_MAX_DEVICES][256];
} graph_build_workspace_t;

int graph_build_from_descriptors(res_graph_t* graph, const graph_desc_t* descriptors,
                                 uint32_t desc_cnt, power_plan_t* plan_scratch,
                                 graph_build_workspace_t* workspace);

#ifdef __cplusplus
}
#endif
#endif
