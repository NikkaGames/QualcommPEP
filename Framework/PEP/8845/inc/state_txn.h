#ifndef POWER_TXN_H
#define POWER_TXN_H
/* ordered resource actions for Molokai D-state transactions. */

#include "aeob.h"
#include "aeob_validate.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define POWER_MAX_ACTIONS 64u
#define POWER_NAME_BYTES 128u

typedef enum power_action_kind {
    POWER_CLOCK = 1,
    POWER_ICB = 2,
    POWER_TLMM_GPIO = 3,
    POWER_PMIC_GPIO = 4,
    POWER_PMIC_REGULATOR = 5,
    POWER_DELAY = 6,
    POWER_PSTATE_ADJUST = 7,

    POWER_FOOTSWITCH = 8
} power_action_kind_t;

typedef struct power_action {
    power_action_kind_t kind;

    union {
        /* preserve raw arguments; backend must interpret operation-specific semantics and must
         * reject unknown selectors. */

        struct {
            char name[POWER_NAME_BYTES];
            uint32_t vote;
            uint32_t extra_cnt;
            uint32_t extra[3];
        } clock;

        struct {
            uint32_t client;
            char master[POWER_NAME_BYTES];
            char slave[POWER_NAME_BYTES];
            uint64_t average, peak;
        } icb;

        struct {
            uint32_t fields[6];
        } gpio;

        struct {
            uint32_t ioctl;
            uint32_t fields[7];
        } pmic_gpio;

        /* numbers remain opaque until PPP provider ABI is fully verified. */

        struct {
            char name[POWER_NAME_BYTES];
            uint32_t fields[5];
        } pmic_regulator;

        struct {
            uint32_t ms;
        } delay;

        /* never invent a firmware operation from these opaque fields. */

        struct {
            uint32_t fields[2];
        } pstate_adjust;

        struct {
            char name[POWER_NAME_BYTES];
            uint32_t action;
        } footswitch;
    } u;
} power_action_t;

typedef struct power_sequence {
    char dev_path[256];
    uint32_t target_d_state;
    uint32_t action_cnt;
    power_action_t actions[POWER_MAX_ACTIONS];
} power_sequence_t;

typedef enum power_result {
    POWER_OK = 0,
    POWER_BAD_INPUT,
    POWER_INVALID_PROGRAM,
    POWER_TOO_MANY_ACTIONS,
    POWER_UNSUPPORTED,
    POWER_NO_KNOWN_STATE,
    POWER_SNAPSHOT_FAILED,
    POWER_APPLY_FAILED,
    POWER_VERIFY_FAILED,
    POWER_ROLLBACK_FAILED,
    POWER_QUARANTINED,
    POWER_STATE_DRIFT,
    POWER_FINAL_STATE_FAILED
} power_result_t;

typedef struct power_snapshot {
    /* opaque backend-owned old value; no guessed clock/BCM/PMIC semantics. */
    uint64_t opaque[8];
} power_snapshot_t;

typedef struct power_scratch {

    power_snapshot_t snapshots[POWER_MAX_ACTIONS];
} power_scratch_t;

typedef struct power_dev {
    uint32_t current_d_state;
    uint8_t state_known;
    /* sticky until authoritative device/provider reinitialization. A failed rollback must not
     * be mistaken for a legitimate unknown initial d0. */

    uint8_t quarantined;
} power_dev_t;

struct power_execution;

typedef struct power_backend {
    /* lock spans the full transaction; implementations must serialize shared client res votes
     * as well as this device's state. */

    int (*lock)(void* ctx);
    void (*unlock)(void* ctx);
    int (*snapshot)(void* ctx, const power_action_t*, power_snapshot_t*);
    int (*apply)(void* ctx, const power_action_t*);
    int (*verify)(void* ctx, const power_action_t*);
    int (*restore)(void* ctx, const power_action_t*, const power_snapshot_t*);
    int (*verify_restored)(void* ctx, const power_action_t*, const power_snapshot_t*);
    void* ctx;
    /* reject unsupported res operations before any hardware vote in the transaction is issued. */

    int (*supports)(void* ctx, const power_action_t*);
    /* must check provider ownership and this device/client's *effective* res votes against the
     * requested final D-state. Not a replay of intermediate GPIO/reset actions or a physical
     * equality check against shared aggregate votes. */

    int (*verify_current_state)(void* ctx, const power_sequence_t*);
    /* optional native res-group executor. The COMPLETE desc-derived sequence is one physical
     * transaction: either all vote identities are verified, or rollback is proven; uncertain
     * hardware must return RollbackFailed. Individual apply/verify callbacks are never
     * substituted with no-ops. */
    power_result_t (*execute_owned_program)(void* ctx, const power_sequence_t*,
                                            struct power_execution*);
} power_backend_t;

typedef struct power_execution {
    uint32_t applied;
    uint32_t restored;
    uint32_t failure_idx;
    power_result_t result;
} power_execution_t;

/* all selected resources must already pass the Molokai catalog and route validator. */

/* target_d_state in the sequence carries the F-state selector only for this serialized action
 * container; it is NOT a D-state transaction and MUST NOT be passed to
 * power_execute/GraphTransition. A valid program is not permission to complete a PoFx
 * notify: the owner must independently. */

/* a compiled program alone proves no physical completion or native NPA rights. */

typedef struct power_pstate_program {
    uint32_t comp;
    uint32_t set;
    uint32_t state;
    power_sequence_t resources;
} power_pstate_program_t;

power_result_t power_compile_aeo_bp_state(const void* buf, size_t length, const char* dev_path,
                                          uint32_t comp, uint32_t set, uint32_t state,
                                          power_pstate_program_t* program);
/* only execute while the independently verified device is in d0 and the component in f0,
 * per Qualcomm's PepSetPStateForComponent contract. P-state set state is independent of
 * component F-state; the supplied per_set state must be dedicated to this exact component
 * and P-state set. */

/* one native owner-locked transaction for a single PoFx V2 request that selects multiple
 * independent PSTATE_SETs. Caller must bind every program to its device/component and
 * maintain separate per-set state. */

/* internal owner-locked action executor. Does not modify a device or component software
 * cache; runs every action and verifies final hardware votes. */

power_result_t power_execute_action_batch(const power_sequence_t* actions,
                                          const power_backend_t* backend, power_execution_t* trace,
                                          power_scratch_t* scratch);
/* a desc-bound, rate-only CLOCK request may configure a gated RCG in verified Dx. The
 * caller fences D-state changes; this cannot execute an enable, rail, bus or
 * peripheral-register action, or publish a D-state. */
power_result_t power_execute_clock_rate(const power_sequence_t* actions,
                                        const power_backend_t* backend, power_execution_t* trace,
                                        power_scratch_t* scratch);
/* nested F-state transition: the caller supplies the CURRENT, verified F-state
 * prerequisite (f0 or f1). This is separate from ordinary PoFx P-state requests, which
 * remain f0-only. */

power_result_t power_execute_p_state_batch_from_f_state(
    const power_pstate_program_t* const* programs, uint32_t cnt, const char* dev_path,
    uint32_t comp, uint32_t set_cnt, const power_dev_t* dev, const power_dev_t* f_state,
    uint32_t expected_current_f_state, power_dev_t* per_set, const power_backend_t* backend,
    power_execution_t* trace, power_sequence_t* combined, power_scratch_t* scratch);
power_result_t power_execute_p_state_batch(const power_pstate_program_t* const* programs,
                                           uint32_t cnt, const char* dev_path, uint32_t comp,
                                           uint32_t set_cnt, const power_dev_t* dev,
                                           const power_dev_t* f_state, power_dev_t* per_set,
                                           const power_backend_t* backend, power_execution_t* trace,
                                           power_sequence_t* combined, power_scratch_t* scratch);
power_result_t power_execute_p_state(const power_pstate_program_t* program, const power_dev_t* dev,
                                     const power_dev_t* comp, power_dev_t* per_set,
                                     const power_backend_t* backend, power_execution_t* trace,
                                     power_scratch_t* scratch);

/* resolve it against compiled component PSTATE_SET entries before taking the nonrecursive
 * backend lock. */

typedef struct power_pstate_selection {
    uint32_t used_sets;
    uint8_t target_state[32];
} power_pstate_selection_t;

power_result_t power_expand_comp_f_state(const power_sequence_t* f_state, uint32_t comp,
                                         const power_pstate_program_t* const* programs,
                                         uint32_t program_cnt, uint32_t set_cnt,
                                         power_sequence_t* expanded,
                                         power_pstate_selection_t* selection);
power_result_t power_execute_comp_f_state(const power_sequence_t* f_state, uint32_t comp,
                                          const power_pstate_program_t* const* programs,
                                          uint32_t program_cnt, const power_dev_t* dev,
                                          power_dev_t* f_state_dev, power_dev_t* per_set,
                                          uint32_t set_cnt, const power_backend_t* backend,
                                          power_execution_t* trace, power_sequence_t* expanded,
                                          power_scratch_t* scratch);
power_result_t power_compile_aeo_bf_state(const void* buf, size_t length, const char* dev_path,
                                          uint32_t comp, uint32_t f_state,
                                          power_sequence_t* sequence);
power_result_t power_compile_aeo_b(const void* buf, size_t length, const char* dev_path,
                                   uint32_t d_state, power_sequence_t* sequence);
/* empty F-state programs still require a validated desc and physical current-state
 * verification. */

power_result_t power_execute_f_state(const power_sequence_t* sequence, power_dev_t* comp,
                                     const power_backend_t* backend, power_execution_t* trace,
                                     power_scratch_t* scratch);
/* never reports a device d0..d3 state change unless every apply AND verify succeeds.
 * rollback failure sets quarantined=1 so even initial d0 cannot retry without externally
 * verified recovery. */

power_result_t power_execute(const power_sequence_t* sequence, power_dev_t* dev,
                             const power_backend_t* backend, power_execution_t* trace,
                             power_scratch_t* scratch);
#ifdef __cplusplus
}
#endif
#endif
