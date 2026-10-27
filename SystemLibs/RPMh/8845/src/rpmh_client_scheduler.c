#include "rpmh_client_scheduler.h"
#include <string.h>

static uint8_t active_mask(const rpmh_driver_config_t* c)
{
    return (uint8_t)((1u << c->active_cnt) - 1u);
}

static int init_scheduler(rpmh_scheduler_t* s, const rpmh_driver_config_t* c, void* ctx)
{
    if (!s || !c || c->hw_drv != MOLOKAI_WIN_HW_DRV || c->tcs_offset != 0u ||
        c->commands_per_tcs != TCS_BATCH_MAX ||
        !((c->total_tcs == MOLOKAI_WIN_TCS_TOTAL && c->active_cnt == MOLOKAI_WIN_AMC_CNT &&
           c->sleep_start == MOLOKAI_WIN_SLEEP_START && c->wake_start == MOLOKAI_WIN_WAKE_START) ||
          (c->total_tcs == MOLOKAI_UEFI_TCS_TOTAL && c->active_cnt == MOLOKAI_UEFI_AMC_CNT &&
           c->sleep_start == MOLOKAI_UEFI_SLEEP_START && c->wake_start == MOLOKAI_UEFI_WAKE_START)))
        return 0;
    memset(s, 0, sizeof(*s));
    s->driver = *c;
    s->execute_ctx = ctx;
    s->next_req = 1u;
    return 1;
}

int rpmh_scheduler_init(rpmh_scheduler_t* s, const rpmh_driver_config_t* c, rpmh_execute_t execute,
                        void* ctx)
{
    if (!execute || !init_scheduler(s, c, ctx))
        return 0;
    s->execute = execute;
    return 1;
}

int rpmh_scheduler_init_result(rpmh_scheduler_t* s, const rpmh_driver_config_t* c,
                               rpmh_execute_result_t execute, void* ctx)
{
    if (!execute || !init_scheduler(s, c, ctx))
        return 0;
    s->execute_result = execute;
    return 1;
}

int rpmh_scheduler_init_deferred(rpmh_scheduler_t* s, const rpmh_driver_config_t* c,
                                 rpmh_arm_deferred_t arm, void* ctx)
{
    if (!arm || !init_scheduler(s, c, ctx))
        return 0;
    s->arm_deferred = arm;
    return 1;
}

int rpmh_scheduler_add_client(rpmh_scheduler_t* s, const char* name, uint8_t explicit_sets,
                              uint8_t* out_client)
{
    uint32_t i, len = 0u;
    if (!s || !name || !out_client || (explicit_sets != 0u && explicit_sets != 1u))
        return 0;
    /* reject unterminated and empty names, never truncate a client name. */
    while (len < RPMH_NAME_BYTES && name[len])
        ++len;
    if (!len || len == RPMH_NAME_BYTES)
        return 0;
    for (i = 0; i < RPMH_CLIENTS; ++i) {
        if (s->clients[i].in_use && strcmp(s->clients[i].name, name) == 0)
            return 0;
    }
    for (i = 0; i < RPMH_CLIENTS; ++i) {
        if (!s->clients[i].in_use) {
            memcpy(s->clients[i].name, name, len + 1u);
            s->clients[i].in_use = 1u;
            s->clients[i].explicit_sets = explicit_sets;
            *out_client = (uint8_t)i;
            return 1;
        }
    }
    return 0;
}

void rpmh_scheduler_revoke(rpmh_scheduler_t* s)
{
    if (!s)
        return;
    uint32_t i;
    s->execution_authorized = 0u;
    /* an active vote queued under the old owner epoch must never be replayed after
     * reauthorization; mark it failed without a hardware action. */

    for (i = 0; i < RPMH_JOBS; ++i) {
        rpmh_job_t* job = &s->jobs[i];
        if (job->state == RPMH_JOB_QUEUED && job->set == RPMH_ACTIVE) {
            job->state = RPMH_JOB_FAILED;
            job->result = TCS_NO_OWNERSHIP;
        }
    }
    /* never zero busy_mask while the transport is executing. the caller must drain and join
     * the synchronous operation before destroying its owner. */
}

int rpmh_scheduler_authorize(rpmh_scheduler_t* s, uint8_t verified, uint8_t owned_amcs)
{
    if (!s || s->arm_deferred || (!s->execute && !s->execute_result))
        return 0;
    if (!verified) {
        rpmh_scheduler_revoke(s);
        return 1;
    }
    if (!owned_amcs || (owned_amcs & (uint8_t)~active_mask(&s->driver)) ||
        (owned_amcs & s->quarantined_mask))
        return 0;
    /* nothing here infers ownership from idle or firmware ExitBootServices. the caller must
     * already have independently validated its exclusive grant. */

    s->execution_authorized = owned_amcs;
    return 1;
}

int rpmh_scheduler_authorize_deferred(rpmh_scheduler_t* s, uint64_t epoch, uint8_t amcs)
{
    uint32_t i;
    if (!s || !s->arm_deferred || !epoch || !amcs || (amcs & (uint8_t)~active_mask(&s->driver)) ||
        (amcs & s->quarantined_mask))
        return 0;
    /* a new owner cannot inherit an outstanding, potentially physical vote. */
    for (i = 0u; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state == RPMH_JOB_EXECUTING)
            return 0;
    if (s->busy_mask)
        return 0;
    s->owner_epoch = epoch;
    s->execution_authorized = amcs;
    return 1;
}

int rpmh_scheduler_submit(rpmh_scheduler_t* s, uint8_t client, rpmh_set_t set,
                          const tcs_command_t* commands, uint32_t cnt, uint32_t* out_req)
{
    uint32_t i, j;
    rpmh_job_t* job = NULL;
    if (!s || (!s->execute && !s->execute_result && !s->arm_deferred) || !out_req || !commands ||
        client >= RPMH_CLIENTS || !s->clients[client].in_use || set > RPMH_WAKE || cnt == 0u ||
        cnt > s->driver.commands_per_tcs || !s->next_req)
        return 0;
    if (set != RPMH_ACTIVE && !s->clients[client].explicit_sets)
        return 0;
    if (set == RPMH_ACTIVE && (!s->execution_authorized || s->admission_paused))
        return 0;
    for (j = 0; j < cnt; ++j)
        if ((commands[j].address & ~0x7FFFFu) || commands[j].read > 1u ||
            (commands[j].read && commands[j].data != 0u))
            return 0;
    for (i = 0; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state == RPMH_JOB_FREE) {
            job = &s->jobs[i];
            break;
        }
    if (!job)
        return 0; /* backpressure; never silently overwrite requests. */
    memset(job, 0, sizeof(*job));
    job->id = s->next_req++;
    job->state = RPMH_JOB_QUEUED;
    job->set = set;
    job->client = client;
    job->cnt = (uint8_t)cnt;
    job->capture_responses = 1u;
    job->slot = 0xFFu;
    memcpy(job->commands, commands, cnt * sizeof(*commands));
    s->clients[client].latest_req = job->id;
    *out_req = job->id;
    return 1;
}

static int select_amc(rpmh_scheduler_t* s)
{
    uint8_t i;
    uint8_t avail =
        (uint8_t)(s->execution_authorized & (uint8_t)~s->quarantined_mask & (uint8_t)~s->busy_mask);
    for (i = 0; i < s->driver.active_cnt; ++i) {
        uint8_t slot = (uint8_t)((s->next_slot + i) % s->driver.active_cnt);
        if (avail & (1u << slot)) {
            s->next_slot = (uint8_t)((slot + 1u) % s->driver.active_cnt);
            return slot;
        }
    }
    return -1;
}

uint32_t rpmh_scheduler_pump(rpmh_scheduler_t* s)
{
    uint32_t dispatched = 0u;
    if (!s || (!s->execute && !s->execute_result && !s->arm_deferred) || !s->execution_authorized ||
        s->admission_paused)
        return 0u;
    while (s->execution_authorized) {
        rpmh_job_t* job = NULL;
        uint32_t i, oldest = 0xFFFFFFFFu;
        int slot;
        int success;
        for (i = 0; i < RPMH_JOBS; ++i) {
            rpmh_job_t* j = &s->jobs[i];
            if (j->state == RPMH_JOB_QUEUED && j->set == RPMH_ACTIVE && j->id < oldest) {
                job = j;
                oldest = j->id;
            }
        }
        if (!job || (slot = select_amc(s)) < 0)
            break;
        job->slot = (uint8_t)slot;
        job->state = RPMH_JOB_EXECUTING;
        s->busy_mask |= (uint8_t)(1u << slot);
        if (s->arm_deferred) {
            rpmh_tcs_result_t transport;
            /* the owner epoch, ticket and selected slot are bound before the TCS issuer can trigger
             * a fast interrupt. */

            job->owner_epoch = s->owner_epoch;
            if (!job->owner_epoch) {
                job->result = TCS_NO_OWNERSHIP;
                job->state = RPMH_JOB_FAILED;
                s->busy_mask &= (uint8_t)~(1u << slot);
                rpmh_scheduler_revoke(s);
                ++dispatched;
                continue;
            }
            transport = s->arm_deferred(s->execute_ctx, job->owner_epoch, job->id, (uint8_t)slot,
                                        job->commands, job->cnt);
            ++dispatched;
            if (transport == TCS_OK) {
                /* keep busy_mask and the immutable ticket until the deferred hardware worker
                 * confirms it. */

                continue;
            }
            s->busy_mask &= (uint8_t)~(1u << slot);
            if (transport == TCS_BUSY && s->preserve_busy_fifo) {
                /* the deferred issuer promises BUSY only before the first hardware/ledger write.
                 * Keep the original request and FIFO position. A synchronous BUSARB/rail client
                 * explicitly cancels this QUEUED ticket before returning BUSY, so there can be no
                 * ghost vote after the caller has returned. */
                job->state = RPMH_JOB_QUEUED;
                job->owner_epoch = 0u;
                job->slot = 0xFFu;
                job->result = TCS_BUSY;
                break; /* never spin repeatedly on the same blocked head */
            }
            job->result = transport;
            job->state = RPMH_JOB_FAILED;
            if (transport == TCS_NO_OWNERSHIP || transport == TCS_COMPLETION_CONSUMER_CONFLICT)
                rpmh_scheduler_revoke(s);
            else if (transport == TCS_IRQ_NOT_ENABLED || transport == TCS_NOT_AMC ||
                     transport == TCS_INVALID)
                s->execution_authorized &= (uint8_t)~(1u << slot);
            else if (transport != TCS_BUSY) {
                s->quarantined_mask |= (uint8_t)(1u << slot);
                s->execution_authorized &= (uint8_t)~(1u << slot);
            }
            continue;
        }
        /* the production hardware callback preserves the exact TCS status. A read-only busy/owner
         * refusal must not poison a reusable AMC. */

        if (s->execute_result) {
            rpmh_tcs_result_t transport =
                s->execute_result(s->execute_ctx, (uint8_t)slot, job->commands, job->cnt,
                                  job->capture_responses ? job->responses : NULL);
            s->busy_mask &= (uint8_t)~(1u << slot);
            ++dispatched;
            job->result = transport;
            job->state = transport == TCS_OK ? RPMH_JOB_COMPLETED : RPMH_JOB_FAILED;
            if (transport == TCS_NO_OWNERSHIP || transport == TCS_COMPLETION_CONSUMER_CONFLICT) {
                /* the transport admitted no command, but its owner contract is no longer usable.
                 * pending ACTIVE work must not run after that revocation. */

                rpmh_scheduler_revoke(s);
            } else if (transport == TCS_IRQ_NOT_ENABLED || transport == TCS_NOT_AMC ||
                       transport == TCS_INVALID) {
                /* a configuration mismatch is a re-admission problem, not proof of a partially
                 * executed hardware command. */

                s->execution_authorized &= (uint8_t)~(1u << slot);
            } else if (transport != TCS_OK && transport != TCS_BUSY) {
                /* i/O error, timeout, command-incomplete or ACK-pending: hardware may have been
                 * touched. */

                s->quarantined_mask |= (uint8_t)(1u << slot);
                s->execution_authorized &= (uint8_t)~(1u << slot);
            }
            continue;
        }
        success = s->execute(s->execute_ctx, (uint8_t)slot, job->commands, job->cnt,
                             job->capture_responses ? job->responses : NULL);
        s->busy_mask &= (uint8_t)~(1u << slot);
        ++dispatched;
        if (!success) {

            s->quarantined_mask |= (uint8_t)(1u << slot);
            s->execution_authorized &= (uint8_t)~(1u << slot);
            job->state = RPMH_JOB_FAILED;
            job->result = TCS_IO_FAILURE;
        } else {
            job->state = RPMH_JOB_COMPLETED;
            job->result = TCS_OK;
        }
    }
    return dispatched;
}

/* all operations below require the external native RPMh owner mutex. A closed admission
 * fence does not invalidate previously issued tickets; the ISR/DPC worker is still
 * entitled to finish their exact epoch. */
int rpmh_scheduler_pause_deferred(rpmh_scheduler_t* s, uint64_t epoch)
{
    if (!s || !epoch || !s->arm_deferred || s->owner_epoch != epoch || !s->preserve_busy_fifo ||
        s->admission_paused)
        return 0;
    s->admission_paused = 1u;
    return 1;
}

int rpmh_scheduler_resume_deferred(rpmh_scheduler_t* s, uint64_t epoch)
{
    if (!s || !epoch || !s->arm_deferred || s->owner_epoch != epoch || !s->preserve_busy_fifo ||
        !s->admission_paused || !s->execution_authorized || s->quarantined_mask)
        return 0;
    s->admission_paused = 0u;
    return 1;
}

int rpmh_scheduler_deferred_drained(const rpmh_scheduler_t* s, uint64_t epoch)
{
    uint32_t i;
    if (!s || !epoch || s->owner_epoch != epoch || !s->arm_deferred || !s->preserve_busy_fifo ||
        s->busy_mask || s->quarantined_mask)
        return 0;
    for (i = 0u; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state != RPMH_JOB_FREE || s->jobs[i].id)
            return 0;
    return 1;
}

int rpmh_scheduler_finish_deferred(rpmh_scheduler_t* s, uint64_t epoch, uint8_t slot, uint32_t req,
                                   rpmh_tcs_result_t hardware_result, const uint32_t* responses,
                                   uint32_t capacity)
{
    uint32_t i;
    uint8_t bit;
    rpmh_job_t* job = 0;
    if (!s || !s->arm_deferred || !epoch || !req || epoch != s->owner_epoch ||
        slot >= s->driver.active_cnt || hardware_result == TCS_BUSY ||
        !(s->busy_mask & (uint8_t)(1u << slot)))
        return 0;
    bit = (uint8_t)(1u << slot);
    for (i = 0u; i < RPMH_JOBS; ++i)
        if (s->jobs[i].id == req && s->jobs[i].state == RPMH_JOB_EXECUTING &&
            s->jobs[i].slot == slot && s->jobs[i].owner_epoch == epoch) {
            job = &s->jobs[i];
            break;
        }
    if (!job)
        return 0;
    if (hardware_result == TCS_OK && job->capture_responses && (!responses || capacity < job->cnt))
        return 0;
    if (hardware_result == TCS_OK && job->capture_responses)
        memcpy(job->responses, responses, job->cnt * sizeof(uint32_t));
    job->result = hardware_result;
    job->state = hardware_result == TCS_OK ? RPMH_JOB_COMPLETED : RPMH_JOB_FAILED;
    s->busy_mask &= (uint8_t)~bit;
    if (hardware_result == TCS_NO_OWNERSHIP || hardware_result == TCS_COMPLETION_CONSUMER_CONFLICT)
        rpmh_scheduler_revoke(s);
    else if (hardware_result == TCS_IRQ_NOT_ENABLED || hardware_result == TCS_NOT_AMC ||
             hardware_result == TCS_INVALID)
        s->execution_authorized &= (uint8_t)~bit;
    else if (hardware_result != TCS_OK) {
        s->quarantined_mask |= bit;
        s->execution_authorized &= (uint8_t)~bit;
    }
    /* the listener must not reenter the scheduler's owner lock. */

    if (s->on_deferred_finished)
        s->on_deferred_finished(s->on_deferred_finished_ctx, epoch, req);
    return 1;
}

int rpmh_scheduler_cancel_queued_active(rpmh_scheduler_t* s, uint8_t client, uint32_t req)
{
    uint32_t i;
    if (!s || !req || client >= RPMH_CLIENTS || !s->clients[client].in_use)
        return 0;
    for (i = 0u; i < RPMH_JOBS; ++i) {
        rpmh_job_t* job = &s->jobs[i];
        if (job->id == req && job->client == client && job->set == RPMH_ACTIVE &&
            job->state == RPMH_JOB_QUEUED) {
            memset(job, 0, sizeof(*job));
            if (s->clients[client].latest_req == req)
                s->clients[client].latest_req = 0u;
            return 1;
        }
    }
    return 0;
}

int rpmh_scheduler_take(rpmh_scheduler_t* s, uint32_t req, rpmh_tcs_result_t* out_result,
                        uint32_t* responses, uint32_t capacity)
{
    uint32_t i;
    if (!s || !out_result || !req)
        return 0;
    for (i = 0; i < RPMH_JOBS; ++i) {
        rpmh_job_t* job = &s->jobs[i];
        if (job->id != req || (job->state != RPMH_JOB_COMPLETED && job->state != RPMH_JOB_FAILED))
            continue;
        if (responses && capacity < job->cnt)
            return 0;
        *out_result = job->result;
        if (responses && job->state == RPMH_JOB_COMPLETED)
            memcpy(responses, job->responses, job->cnt * sizeof(uint32_t));
        memset(job, 0, sizeof(*job));
        return 1;
    }
    return 0;
}

rpmh_tcs_result_t rpmh_scheduler_execute_sync(rpmh_scheduler_t* s, uint8_t client,
                                              const tcs_command_t* commands, uint32_t cnt,
                                              uint32_t* responses)
{
    uint32_t id;
    rpmh_tcs_result_t result;
    if (!s || !s->execution_authorized)
        return TCS_NO_OWNERSHIP;
    if (s->arm_deferred)
        return TCS_COMPLETION_CONSUMER_CONFLICT;
    if (!rpmh_scheduler_submit(s, client, RPMH_ACTIVE, commands, cnt, &id))
        return TCS_INVALID;
    /* preserve the original write-only transport contract: do not read command DATA/response
     * registers unless the caller requested them. */

    if (!responses) {
        uint32_t i;
        for (i = 0; i < RPMH_JOBS; ++i)
            if (s->jobs[i].id == id) {
                s->jobs[i].capture_responses = 0u;
                break;
            }
    }
    (void)rpmh_scheduler_pump(s);
    if (!rpmh_scheduler_take(s, id, &result, responses, cnt)) {
        uint32_t i;
        /* otherwise a later unrelated DPM request could pump it after its owner has already
         * handled this call as a failure. never erase an executing ticket: it may already have
         * touched the RSC and requires completion/rundown by its owner. */

        (void)i;
        (void)rpmh_scheduler_cancel_queued_active(s, client, id);
        return TCS_BUSY;
    }
    return result;
}

uint32_t rpmh_scheduler_invalidate_set(rpmh_scheduler_t* s, uint8_t client, rpmh_set_t set)
{
    uint32_t i, cnt = 0u;
    if (!s || client >= RPMH_CLIENTS || !s->clients[client].in_use || set == RPMH_ACTIVE ||
        set > RPMH_WAKE)
        return 0u;
    for (i = 0u; i < RPMH_JOBS; ++i) {
        rpmh_job_t* job = &s->jobs[i];
        if (job->state == RPMH_JOB_QUEUED && job->set == set && job->client == client) {
            memset(job, 0, sizeof(*job));
            ++cnt;
        }
    }
    return cnt;
}

uint32_t rpmh_scheduler_pending_set(const rpmh_scheduler_t* s, rpmh_set_t set)
{
    uint32_t i, cnt = 0u;
    if (!s || set > RPMH_WAKE)
        return 0u;
    for (i = 0u; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state == RPMH_JOB_QUEUED && s->jobs[i].set == set)
            ++cnt;
    return cnt;
}
