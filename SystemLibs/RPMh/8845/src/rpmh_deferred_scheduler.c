#include "rpmh_deferred_scheduler.h"
#include <string.h>

static rpmh_tcs_result_t arm_from_scheduler(void* ctx, uint64_t epoch, uint32_t req, uint8_t slot,
                                            const tcs_command_t* commands, uint32_t cnt)
{
    rpmh_deferred_scheduler_t* b = ctx;
    if (!b || !b->tcs || !b->ledger || !b->scheduler || !epoch || epoch != b->epoch ||
        b->ledger->epoch != b->ledger_epoch)
        return TCS_NO_OWNERSHIP;

    if (!cnt || cnt > TCS_BATCH_MAX)
        return TCS_INVALID;
    /* an active command set carries no dependency metadata. the OEM VRM batch uses WAIT=0; a
     * command-enable bit is not a dependency bit. */
    return rpmh_tcs_arm_deferred(b->tcs, b->ledger, b->ledger_epoch, req, slot, commands, cnt, 0u);
}

/* dropping/zeroing it during a synchronous->deferred transport promotion would silently
 * disable every BUSARB/PMIC/readback client. */

static int empty_scheduler_jobs(const rpmh_scheduler_t* s)
{
    uint32_t i;
    if (s->busy_mask || s->execution_authorized || s->quarantined_mask || s->owner_epoch)
        return 0;
    for (i = 0; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state != RPMH_JOB_FREE || s->jobs[i].id)
            return 0;
    return 1;
}

int rpmh_deferred_scheduler_bind_owner(rpmh_deferred_scheduler_t* b, rpmh_scheduler_t* s,
                                       rpmh_tcs_t* t, rsc_completion_t* l,
                                       const rpmh_driver_config_t* cfg, uint64_t owner_epoch,
                                       uint32_t ledger_epoch)
{
    rpmh_deferred_scheduler_t staged;
    rpmh_driver_config_t config;
    uint8_t mask;
    int promotion;
    if (!b || !s || !t || !l || !cfg || !owner_epoch || owner_epoch == UINT64_MAX ||
        !ledger_epoch || b->scheduler || b->tcs || b->ledger || b->epoch || b->ledger_epoch ||
        l->epoch != ledger_epoch || !l->irq_bound || !l->granted_mask ||
        !t->completion_handler_bound || !t->dpc_consumes_completion || !t->external_grant_mask ||
        !t->owned_amc_mask || t->deferred_active_mask || !t->io.read32 || !t->io.write32 ||
        !t->io.barrier || !t->io.lock || !t->io.unlock || !t->io.enter_irq_lock ||
        !t->io.leave_irq_lock || !t->io.irq_lock_ctx ||
        (t->external_grant_mask & (uint8_t)~l->granted_mask) ||
        (t->owned_amc_mask & (uint8_t)~t->external_grant_mask))
        return 0;
    /* native owner publication is strictly the Molokai OS topology. Historical six-slot UEFI
     * fixtures are useful for offline simulation, but may never become a Windows d0 hardware
     * owner. */

    if (cfg->hw_drv != MOLOKAI_WIN_HW_DRV || cfg->tcs_offset != 0u ||
        cfg->total_tcs != MOLOKAI_WIN_TCS_TOTAL || cfg->active_cnt != MOLOKAI_WIN_AMC_CNT ||
        cfg->sleep_start != MOLOKAI_WIN_SLEEP_START || cfg->wake_start != MOLOKAI_WIN_WAKE_START ||
        cfg->commands_per_tcs != TCS_BATCH_MAX)
        return 0;
    mask = (uint8_t)(t->owned_amc_mask & t->external_grant_mask & l->granted_mask);
    if (!mask)
        return 0;

    promotion = s->next_req != 0u;
    if (promotion) {
        if ((!s->execute && !s->execute_result) || s->arm_deferred || !empty_scheduler_jobs(s) ||
            memcmp(&s->driver, cfg, sizeof(*cfg)) != 0)
            return 0;
    } else {
        /* empty baseline must have no callback/client/physical state. */
        uint32_t i;
        if (s->execute || s->execute_result || s->arm_deferred || !empty_scheduler_jobs(s))
            return 0;
        for (i = 0; i < RPMH_CLIENTS; ++i)
            if (s->clients[i].in_use || s->clients[i].latest_req)
                return 0;

        config = *cfg;
        /* the native owner geometry was authenticated above. */

        if (config.hw_drv != MOLOKAI_WIN_HW_DRV || config.tcs_offset != 0u ||
            config.commands_per_tcs != TCS_BATCH_MAX || config.total_tcs != MOLOKAI_WIN_TCS_TOTAL ||
            config.active_cnt != MOLOKAI_WIN_AMC_CNT ||
            config.sleep_start != MOLOKAI_WIN_SLEEP_START ||
            config.wake_start != MOLOKAI_WIN_WAKE_START)
            return 0;
    }
    memset(&staged, 0, sizeof(staged));
    staged.scheduler = s;
    staged.tcs = t;
    staged.ledger = l;
    staged.epoch = owner_epoch;
    staged.ledger_epoch = ledger_epoch;
    staged.previous_execute = s->execute;
    staged.previous_execute_result = s->execute_result;
    staged.previous_execute_ctx = s->execute_ctx;
    /* publish both objects under the external owner admission lock; this block performs no hardware
     * access or callbacks. */

    if (!promotion) {
        s->driver = config;
        s->next_req = 1u;
    }
    s->execute = NULL;
    s->execute_result = NULL;
    s->arm_deferred = arm_from_scheduler;
    s->execute_ctx = b;
    s->owner_epoch = owner_epoch;
    s->execution_authorized = mask;
    /* the live Windows scheduler is the FIFO owner; other queues must not dispatch competing
     * TCSs. */
    s->preserve_busy_fifo = 1u;
    s->admission_paused = 0u;
    *b = staged;
    return 1;
}

int rpmh_deferred_scheduler_bind(rpmh_deferred_scheduler_t* b, rpmh_scheduler_t* s, rpmh_tcs_t* t,
                                 rsc_completion_t* l, const rpmh_driver_config_t* cfg,
                                 uint32_t epoch)
{
    /* compatibility: legacy tests use the same low-width identity for Windows owner and RSC
     * completion ledger. */

    return rpmh_deferred_scheduler_bind_owner(b, s, t, l, cfg, (uint64_t)epoch, epoch);
}

/* d0 shutdown must retire the transport epoch rather than recreating the client scheduler
 * (which loses BUSARB/rail/READBACK clients) or carrying stale WDF/TCS pointers into
 * resume. this is the LAST software step after the owner has physically drained and
 * restored all AMC IRQs. */

int rpmh_deferred_scheduler_retire_owner(rpmh_deferred_scheduler_t* b, rpmh_sync_deferred_t* w,
                                         uint64_t owner_epoch, uint32_t ledger_epoch)
{
    rpmh_scheduler_t* s;
    rpmh_tcs_t* t;
    rsc_completion_t* l;
    uint32_t i;
    if (!b || !w || !(s = b->scheduler) || !(t = b->tcs) || !(l = b->ledger) || !owner_epoch ||
        !ledger_epoch || b->epoch != owner_epoch || b->ledger_epoch != ledger_epoch ||
        l->epoch != ledger_epoch || l->granted_mask || l->irq_bound ||
        s->owner_epoch != owner_epoch || s->arm_deferred != arm_from_scheduler ||
        s->execute_ctx != b || s->execute || s->execute_result || s->execution_authorized ||
        s->busy_mask || s->quarantined_mask || !rpmh_scheduler_deferred_drained(s, owner_epoch) ||
        !rpmh_sync_deferred_can_retire(w, s, owner_epoch) || t->external_grant_mask ||
        t->owned_amc_mask || t->completion_handler_bound || t->dpc_consumes_completion ||
        t->irq_enabled_by_us || t->deferred_active_mask || t->completed_amc_mask ||
        /* a zero-grant transport is never permitted to auto-reissue a previous request, even if
         * its caller forgot to take the result. */

        (!b->previous_execute && !b->previous_execute_result))
        return 0;
    for (i = 0; i < RPMH_JOBS; ++i)
        if (s->jobs[i].state != RPMH_JOB_FREE || s->jobs[i].id)
            return 0;
    for (i = 0; i < RSC_AMCS; ++i)
        if (l->amc[i].state != RSC_AMC_VACANT || l->amc[i].owner_epoch || l->amc[i].req_id ||
            t->deferred_epoch[i] || t->deferred_req_id[i] || t->deferred_command_cnt[i] ||
            t->deferred_read_mask[i])
            return 0;

    s->on_deferred_finished = NULL;
    s->on_deferred_finished_ctx = NULL;
    s->execution_authorized = 0u;
    s->owner_epoch = 0u;
    s->preserve_busy_fifo = 0u;
    s->admission_paused = 0u;
    s->arm_deferred = NULL;
    s->execute = b->previous_execute;
    s->execute_result = b->previous_execute_result;
    s->execute_ctx = b->previous_execute_ctx;
    memset(w, 0, sizeof(*w));
    memset(b, 0, sizeof(*b));
    return 1;
}

static int deferred_contract_ready(const rpmh_deferred_scheduler_t* b,
                                   const rpmh_sync_deferred_t* w)
{
    const rpmh_scheduler_t* s;
    const rpmh_tcs_t* t;
    const rsc_completion_t* l;
    uint8_t granted;
    if (!b || !w || !(s = b->scheduler) || !(t = b->tcs) || !(l = b->ledger) || !b->epoch ||
        !b->ledger_epoch || l->epoch != b->ledger_epoch || !l->irq_bound ||
        !t->completion_handler_bound || !t->dpc_consumes_completion || !t->io.enter_irq_lock ||
        !t->io.leave_irq_lock || !t->io.irq_lock_ctx || !s->arm_deferred ||
        s->owner_epoch != b->epoch || !s->owner_epoch || w->scheduler != s ||
        w->owner_epoch != b->epoch || w->faulted || s->quarantined_mask)
        return 0;
    granted = (uint8_t)(t->owned_amc_mask & t->external_grant_mask & l->granted_mask);
    /* an extra scheduler bit would be permission not actually in the independently
     * granted/registered TCS+IRQ intersection. */

    if (!granted || !(s->execution_authorized & granted) ||
        (s->execution_authorized & (uint8_t)~granted))
        return 0;
    return 1;
}

int rpmh_deferred_runtime_ready(const rpmh_deferred_scheduler_t* b, const rpmh_sync_deferred_t* w)
{
    /* busy/in-flight is valid while a previously admitted transaction is awaiting its physical
     * IRQ. admission remains controlled by the scheduler and WDF lock. */

    return deferred_contract_ready(b, w);
}

int rpmh_deferred_probe_ready(const rpmh_deferred_scheduler_t* b, const rpmh_sync_deferred_t* w)
{
    /* a new d0 hardware READ probe must start from a quiescent client, not overlap a ticket
     * borrowed by a previous request. */

    return deferred_contract_ready(b, w) && !w->active_req && !b->scheduler->busy_mask;
}

rpmh_tcs_result_t rpmh_deferred_scheduler_finish(rpmh_deferred_scheduler_t* b,
                                                 rsc_irq_latch_t* latched)
{
    rpmh_tcs_result_t result;
    uint32_t responses[TCS_BATCH_MAX] = {0};
    uint32_t i;
    rpmh_job_t* job = 0;
    if (!b || !b->scheduler || !b->tcs || !b->ledger || !latched || !latched->valid ||
        latched->owner_epoch != b->ledger_epoch || latched->slot >= b->scheduler->driver.active_cnt)
        return TCS_INVALID;
    /* a latched IRQ from a different request must not be allowed to ACK the controller through
     * a matching slot alone. */

    for (i = 0u; i < RPMH_JOBS; ++i)
        if (b->scheduler->jobs[i].id == latched->req_id &&
            b->scheduler->jobs[i].state == RPMH_JOB_EXECUTING &&
            b->scheduler->jobs[i].owner_epoch == b->epoch &&
            b->scheduler->jobs[i].slot == latched->slot) {
            job = &b->scheduler->jobs[i];
            break;
        }
    if (!job)
        return TCS_NO_OWNERSHIP;
    result = rpmh_tcs_finish_deferred(b->tcs, b->ledger, latched,
                                      job->capture_responses ? responses : NULL);
    if (result != TCS_OK && latched->valid)
        return result; /* hardware ticket not proven retired; no reuse. */
    if (!rpmh_scheduler_finish_deferred(b->scheduler, b->epoch, job->slot, job->id, result,
                                        result == TCS_OK && job->capture_responses ? responses
                                                                                   : NULL,
                                        result == TCS_OK && job->capture_responses ? job->cnt : 0u))
        return TCS_IO_FAILURE;
    return result;
}
