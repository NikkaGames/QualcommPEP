#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"
#include "dstate_types.h"
#include "pstate_types.h"
#include "dpm_verified.h"
#include "hw_backend.h"
#include "peer_component.h"
#include "perf_clock_win.h"
#include "pofx_perf.h"
#include "res_graph.h"
#include "runtime_component_win.h"
#include "runtime_dstate.h"
#include "state_txn.h"
#include "ufs_pstate_graph_bridge.h"

/* physical component sequence admission is separate from DEVICE d0/d3. sequences and backend are
 * owned by the reviewed Windows hardware provider and remain alive until PoFx has
 * unregistered and all requests are drained. */
typedef struct pep_comp_fstate_scope {
    ULONG comp_cnt;
    ULONG sequence_slots;
    char dev_path[256];
    const power_sequence_t* const* sequences;
    power_dev_t* verified_states;   /* one per component, provider proof. */
    const power_backend_t* backend; /* full snapshot/apply/readback. */
    /* all of these pointers belong to the SAME attested owner/scope epoch. The owner must not
     * publish this scope until the native NPA/clock/rail backends and device d0 proof have
     * been independently verified. */

    const power_pstate_program_t* const* p_state_programs;
    ULONG p_state_program_cnt;
    power_dev_t* p_state_set_states;
    ULONG sets_per_comp;
    const power_dev_t* physical_dev_state; /* owner-verified d0. */
    ULONG src_verified;
    /* ufs only: one owner-locked atomic f0/f1 -> all-three-PSTATE bridge. NO current producer
     * binds this pointer; NPA FOOTSWITCH owner and native PoFx reg must be established first. */

    ufs_pstate_graph_bridge_t* ufs_graph_bridge;
} pep_comp_fstate_scope_t;

/* its provider serialization is shared with every graph D-state vote. */
typedef struct peer_native_scope {
    peer_comp_t comp;
    power_backend_t adapter;
    hw_backend_t leased;
    res_graph_t* graph; /* shared DSDT/runtime client serialization. */
    uint64_t serial;
    /* atomic to prevent overlapping Windows component callbacks from sharing leased ops. */

    volatile LONG lease_state;
    volatile LONG target_f_state; /* -1 vacant; selected by PoFx, never from vote data. */
} peer_native_scope_t;

/* generic runtime aeo_b d0..d3 owner, bound to the SAME graph serialization and native
 * provider epoch as DSDT devices. */
typedef struct runtime_dstate_native {
    runtime_dstate_scope_t program;
    power_backend_t adapter;
    hw_backend_t leased;
    res_graph_t* graph;
    uint64_t serial;
    volatile LONG lease_state;
    volatile LONG target_d_state;
    volatile LONG terminal_failure_reported;
} runtime_dstate_native_t;

/* one nonpaged owner for the desc, PoFx catalog, physical d0 reference and native
 * required-client provider. The graph lifetime is pinned by native endpoint admission
 * until explicit d3 reconciliation. */
typedef struct pep_native_producer_scope {
    KMUTEX transaction_mutex;
    power_backend_t backend;
    pofx_producer_t producer;
    res_graph_t* graph;
    const power_dev_t* verified_d0;
    UCHAR* desc;
    ULONG desc_bytes;
    uint64_t serial;
    UCHAR f1_release_verified;
} pep_native_producer_scope_t;

typedef struct pep_devnode {
    UNICODE_STRING instance_path;
    ULONG dev_id;
    ULONG flags;
    pep_dev_reg_state_t reg_state;
    POHANDLE kernel_handle;
    DEVICE_POWER_STATE current_d_state; /* windows-reported, not provider proof. */
    dpm_verified_t dpm_backend_state;   /* only verified votes. */
    UCHAR in_system_transition;
    UCHAR reserved[3];
    ULONG sub_dx_state_usb;
    ULONG sub_dx_state_usb_valid;
    UCHAR d_state_change_in_progress;
    UCHAR d_state_completion_claimed;
    USHORT d_state_reserved;
    DEVICE_POWER_STATE d_state_pending_power_state; /* only an initiated PoFx transition. */
    volatile LONG dpm_failure_reports;
    volatile LONG d_state_pre_notifications[4];
    volatile LONG d_state_complete_notifications[4];
    KEVENT d_state_progress_event;
    FAST_MUTEX d_state_change_f_mutex;
    ULONG comp_cnt;
    ULONG* current_p_state;
    ULONG* current_f_state;
    pep_comp_fstate_scope_t* f_state_scope; /* retained provider lifetime. */
    peer_native_scope_t* peer_scope;
    runtime_comp_win_t* runtime_comp;
    runtime_dstate_native_t* runtime_dstate_scope;
    ULONGLONG sdbus_sampling_interval;
    ULONG sdbus_perf_state_cnt;
    sdbus_pstate_t* sdbus_perf_states;
    ULONG sdbus_max_bus_freq;
    ULONG sdbus_current_idx;
    ULONGLONG sdbus_last_change100ns;
    ULONGLONG sdbus_residency100ns;
    ULONGLONG sdbus_transition_cnt;
    PVOID comp_perf_ctx;
    pofx_perf_endpoint_t* native_perf; /* provider-owned until PoFx rundown. */
    pep_native_producer_scope_t* native_owner;
    uint64_t native_perf_backend_serial;
    UCHAR crashdump_resources_prepared;
    UCHAR crashdump_reserved[7];
    /* deferred-work rundown is distinct from PoFx framework callback rundown. */

    KSPIN_LOCK deferred_work_lock;
    KEVENT deferred_work_drained;
    ULONG deferred_workers;
    volatile LONG deferred_generation;
    UCHAR deferred_closed;
    UCHAR deferred_unregistering;
    struct pep_devnode* next;
    clock_perf_scope_t* clock_perf;
} pep_devnode_t;
