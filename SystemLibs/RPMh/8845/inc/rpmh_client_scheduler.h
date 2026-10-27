#ifndef RPMH_CLIENT_SCHEDULER_H
#define RPMH_CLIENT_SCHEDULER_H
/* molokai RPMh software client/command-queue boundary. This code does NOT establish HLOS
 * ownership, map registers, install ISR/DPC, or grant direct TCS access. */

#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RPMH_CLIENTS 8u
#define RPMH_JOBS 16u
#define RPMH_NAME_BYTES 24u

typedef enum rpmh_set {
    RPMH_ACTIVE = 0,
    RPMH_SLEEP = 1,
    RPMH_WAKE = 2
} rpmh_set_t;

typedef enum rpmh_job_state {
    RPMH_JOB_FREE = 0,
    RPMH_JOB_QUEUED = 1,
    RPMH_JOB_EXECUTING = 2,
    RPMH_JOB_COMPLETED = 3,
    RPMH_JOB_FAILED = 4
} rpmh_job_state_t;

typedef struct rpmh_driver_config {
    uint8_t hw_drv;
    uint8_t tcs_offset;
    uint8_t total_tcs;
    uint8_t active_cnt;
    uint8_t sleep_start;
    uint8_t wake_start;
    uint8_t commands_per_tcs;
} rpmh_driver_config_t;

typedef struct rpmh_client {
    char name[RPMH_NAME_BYTES];
    uint8_t in_use;
    uint8_t explicit_sets;
    uint32_t latest_req;
} rpmh_client_t;

typedef struct rpmh_job {
    uint32_t id;
    uint64_t owner_epoch;
    rpmh_job_state_t state;
    rpmh_set_t set;
    uint8_t client;
    uint8_t slot;
    uint8_t cnt;
    uint8_t capture_responses;
    rpmh_tcs_result_t result;
    tcs_command_t commands[TCS_BATCH_MAX];
    uint32_t responses[TCS_BATCH_MAX];
} rpmh_job_t;

typedef int (*rpmh_execute_t)(void* ctx, uint8_t slot, const tcs_command_t* commands, uint32_t cnt,
                              uint32_t* responses);
/* the actual Windows TCS executor returns the precise hardware outcome. busy/no-owner are
 * pre-issue results; timeout/I/O/completion failures may have touched controller
 * registers. */

typedef rpmh_tcs_result_t (*rpmh_execute_result_t)(void* ctx, uint8_t slot,
                                                   const tcs_command_t* commands, uint32_t cnt,
                                                   uint32_t* responses);

/* split-phase transport: accepting an AMC trigger is NOT physical completion. The callback
 * must arm 's immutable RSC completion ledger BEFORE its hardware trigger. */

typedef rpmh_tcs_result_t (*rpmh_arm_deferred_t)(void* ctx, uint64_t owner_epoch, uint32_t req_id,
                                                 uint8_t slot, const tcs_command_t* commands,
                                                 uint32_t cnt);

typedef struct rpmh_scheduler {
    rpmh_driver_config_t driver;
    rpmh_client_t clients[RPMH_CLIENTS];
    rpmh_job_t jobs[RPMH_JOBS];
    uint32_t next_req;
    uint8_t next_slot;
    uint8_t busy_mask;
    uint8_t quarantined_mask;
    uint8_t execution_authorized; /* set only by a separately validated owner. */
    /* the production deferred owner uses the same no-write BUSY FIFO semantics as the OEM
     * queue. These are SOFTWARE ADMISSION flags, never evidence of hardware ownership or
     * completed transactions. */
    uint8_t preserve_busy_fifo;
    uint8_t admission_paused;
    uint64_t owner_epoch; /* stays pinned during revoked in-flight rundown. */
    rpmh_execute_t execute;
    rpmh_execute_result_t execute_result;
    rpmh_arm_deferred_t arm_deferred;
    void* execute_ctx;
    /* invoked exactly once after an accepted deferred hardware completion. Called under the
     * owner's scheduler lock, not from ISR/DIRQL. */

    void (*on_deferred_finished)(void* ctx, uint64_t owner_epoch, uint32_t req_id);
    void* on_deferred_finished_ctx;
} rpmh_scheduler_t;

/* initialization is read-only and does not grant ownership. */

int rpmh_scheduler_init(rpmh_scheduler_t* s, const rpmh_driver_config_t* config,
                        rpmh_execute_t execute, void* ctx);
/* init the production typed hardware transport; uses the same owner gate. */
int rpmh_scheduler_init_result(rpmh_scheduler_t* s, const rpmh_driver_config_t* config,
                               rpmh_execute_result_t execute, void* ctx);
/* requires an external, proven IRQ owner to authorize an epoch. */

int rpmh_scheduler_init_deferred(rpmh_scheduler_t* s, const rpmh_driver_config_t* config,
                                 rpmh_arm_deferred_t arm, void* ctx);
int rpmh_scheduler_authorize_deferred(rpmh_scheduler_t* s, uint64_t verified_owner_epoch,
                                      uint8_t granted_active_amcs);
/* failed, stale and duplicate completions cannot unlock or overwrite a different AMC.
 * Still valid after dispatch revoke for the SAME owner epoch, until hardware rundown
 * retires this ticket. */

int rpmh_scheduler_finish_deferred(rpmh_scheduler_t* s, uint64_t owner_epoch, uint8_t slot,
                                   uint32_t req_id, rpmh_tcs_result_t hardware_result,
                                   const uint32_t* responses, uint32_t capacity);
int rpmh_scheduler_add_client(rpmh_scheduler_t* s, const char* name, uint8_t explicit_sets,
                              uint8_t* out_client);
/* configures the adapter after independent ownership verification. owned_amcs must be a
 * subset of configured active slots; the other sets remain purely staged. */

int rpmh_scheduler_authorize(rpmh_scheduler_t* s, uint8_t verified, uint8_t owned_amcs);
int rpmh_scheduler_submit(rpmh_scheduler_t* s, uint8_t client, rpmh_set_t set,
                          const tcs_command_t* commands, uint32_t cnt, uint32_t* out_req);
/* run bounded active work using an EXISTING synchronous TCS transport. This never issues
 * sleep/wake hardware writes and never runs without grant. */

uint32_t rpmh_scheduler_pump(rpmh_scheduler_t* s);
/* owner-lock-required quiescence: pause refuses new ACTIVE submissions and dispatch but
 * does NOT cancel already issued/acknowledging AMC tickets. */
int rpmh_scheduler_pause_deferred(rpmh_scheduler_t* s, uint64_t epoch);
int rpmh_scheduler_resume_deferred(rpmh_scheduler_t* s, uint64_t epoch);

int rpmh_scheduler_deferred_drained(const rpmh_scheduler_t* s, uint64_t epoch);
/* only a queued ACTIVE request may be cancelled: never erase a request already issued to
 * the hardware. External owner scheduler lock required. */

int rpmh_scheduler_cancel_queued_active(rpmh_scheduler_t* s, uint8_t client, uint32_t req_id);
int rpmh_scheduler_take(rpmh_scheduler_t* s, uint32_t req, rpmh_tcs_result_t* out_result,
                        uint32_t* responses, uint32_t capacity);
/* synchronous adapter currently used by BUSARB/PMIC and readback; may be replaced with
 * IRQ/DPC completion once Windows ownership is reconstructed. */

rpmh_tcs_result_t rpmh_scheduler_execute_sync(rpmh_scheduler_t* s, uint8_t client,
                                              const tcs_command_t* commands, uint32_t cnt,
                                              uint32_t* responses);

uint32_t rpmh_scheduler_invalidate_set(rpmh_scheduler_t* s, uint8_t client, rpmh_set_t set);
/* software-only count of queued set records, never a hardware-state probe. */
uint32_t rpmh_scheduler_pending_set(const rpmh_scheduler_t* s, rpmh_set_t set);
void rpmh_scheduler_revoke(rpmh_scheduler_t* s);
#ifdef __cplusplus
}
#endif
#endif
