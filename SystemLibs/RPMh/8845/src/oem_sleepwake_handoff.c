#include "oem_sleepwake_handoff.h"
#include <string.h>

static int fenced(oem_sleepwake_handoff_t* h)
{
    return h && h->fence_held && h->lease &&
           h->ops.fence_still_drained(h->ops.ctx, h->epoch, h->lease) == 1;
}

static int release_fence(oem_sleepwake_handoff_t* h)
{
    if (!h->fence_held || !h->lease || h->ops.release_fence(h->ops.ctx, h->epoch, h->lease) != 1)
        return 0;
    h->fence_held = 0;
    h->lease = 0;
    return 1;
}

static int quarantine(oem_sleepwake_handoff_t* h)
{
    if (h)
        h->phase = HANDOFF_QUARANTINED;
    return 0;
}

int oem_handoff_init(oem_sleepwake_handoff_t* h, oem_sleepwake_bridge_t* b,
                     const oem_sleepwake_program_ops_t* p, const oem_sleepwake_handoff_ops_t* ops)
{
    if (!h || !b || !p || !ops || !b->voter || !b->sets || !b->db || !b->sets->io.owner_epoch ||
        b->voter->io.owner_epoch != b->sets->io.owner_epoch || !p->exclusive_owner ||
        !p->quiesced || !p->read32 || !p->write32 || !p->barrier || !p->firmware_released ||
        !ops->fence_and_drain || !ops->fence_still_drained || !ops->release_fence ||
        !ops->never_started || !ops->transfer_to_firmware || !ops->firmware_returned)
        return 0;
    memset(h, 0, sizeof(*h));
    h->bridge = b;
    h->ops = *ops;
    h->program_ops = *p;
    h->epoch = b->sets->io.owner_epoch;
    h->phase = HANDOFF_INITIALIZED;
    return 1;
}

int oem_handoff_begin(oem_sleepwake_handoff_t* h)
{
    uint64_t lease = 0, token = 0;
    if (!h || h->closed || h->phase != HANDOFF_INITIALIZED || h->fence_held || h->bridge->ticket ||
        h->bridge->hardware_started || h->bridge->faulted || h->bridge->closed ||
        h->bridge->sets->io.owner_epoch != h->epoch)
        return 0;
    /* failure may be ambiguous: do not automatically release an incompletely established host
     * fence, even if the callback did not return a lease. */

    if (h->ops.fence_and_drain(h->ops.ctx, h->epoch, &lease) != 1 || !lease)
        return quarantine(h);
    h->lease = lease;
    h->fence_held = 1;
    if (!fenced(h))
        return quarantine(h);

    if (!oem_sleep_wake_arm(h->bridge, &token)) {
        if (h->bridge->ticket || h->bridge->hardware_started || h->bridge->faulted ||
            !release_fence(h))
            return quarantine(h);
        return 0; /* no hardware action, cleanly return to initialized. */
    }
    h->ticket = token;
    if (!oem_sleep_wake_program_init(&h->program, h->bridge, &h->program_ops))
        return quarantine(h);
    h->phase = HANDOFF_ARMED;
    return 1;
}

int oem_handoff_attach_dependencies(oem_sleepwake_handoff_t* h,
                                    const oem_sleepwake_dependencies_t* d)
{
    return h && !h->closed && h->phase == HANDOFF_ARMED && fenced(h) &&
           oem_sleep_wake_program_attach_dependencies(&h->program, d);
}

int oem_handoff_program(oem_sleepwake_handoff_t* h)
{
    if (!h || h->closed || h->phase != HANDOFF_ARMED)
        return 0;
    if (!fenced(h))
        return quarantine(h);
    if (!oem_sleep_wake_program_commit(&h->program, h->ticket)) {
        /* even a preflight failure has no automatic owner-retake or retry. */
        if (h->bridge->hardware_started || h->program.state == SLEEP_WAKE_PROGRAM_QUARANTINED)
            return quarantine(h);
        return 0;
    }
    if (!fenced(h) || !oem_sleep_wake_program_verify(&h->program, h->ticket))
        return quarantine(h);
    h->phase = HANDOFF_PROGRAMMED;
    return 1;
}

int oem_handoff_transfer(oem_sleepwake_handoff_t* h)
{
    if (!h || h->closed || h->phase != HANDOFF_PROGRAMMED)
        return 0;
    if (!fenced(h) || !oem_sleep_wake_program_verify(&h->program, h->ticket))
        return quarantine(h);
    /* mark ambiguous before crossing into an external firmware provider. No software replay or
     * rollback follows a failed handoff. */

    h->phase = HANDOFF_QUARANTINED;
    if (h->ops.transfer_to_firmware(h->ops.ctx, h->epoch, h->ticket, h->lease,
                                    h->program.slot_mask) != 1)
        return 0;
    h->phase = HANDOFF_TRANSFERRED;
    return 1;
}

int oem_handoff_complete_resume(oem_sleepwake_handoff_t* h)
{
    if (!h || h->closed || h->phase != HANDOFF_TRANSFERRED)
        return 0;
    /* post-resume IRQ conditions can change, but the producer gate must still be closed and
     * the drain invariant must remain authoritative. */

    if (!fenced(h) || h->ops.firmware_returned(h->ops.ctx, h->epoch, h->ticket, h->lease) != 1)
        return quarantine(h);
    /* resume requires separate firmware and voltage proof; retirement also requires release proof.
     */

    if (!oem_sleep_wake_commit_resume(h->bridge, h->ticket))
        return quarantine(h);
    if (!oem_sleep_wake_program_retire(&h->program, h->ticket))
        return quarantine(h);

    if (!release_fence(h))
        return quarantine(h);
    h->phase = HANDOFF_RETIRED;
    return 1;
}

int oem_handoff_cancel_unstarted(oem_sleepwake_handoff_t* h)
{
    if (!h || h->closed || h->phase != HANDOFF_ARMED ||
        h->program.state != SLEEP_WAKE_PROGRAM_IDLE || h->bridge->hardware_started || !fenced(h) ||
        h->ops.never_started(h->ops.ctx, h->epoch, h->ticket, h->lease) != 1)
        return 0;
    if (!oem_sleep_wake_cancel_unstarted(h->bridge, h->ticket) || !release_fence(h))
        return quarantine(h);
    h->phase = HANDOFF_CANCELLED;
    return 1;
}

void oem_handoff_close(oem_sleepwake_handoff_t* h)
{
    if (!h)
        return;
    h->closed = 1;
    oem_sleep_wake_program_close(&h->program);
    if (h->bridge)
        oem_sleep_wake_close(h->bridge);
}

int oem_handoff_can_release(const oem_sleepwake_handoff_t* h)
{
    return h && h->closed && !h->fence_held && !h->lease &&
           (h->phase == HANDOFF_RETIRED || h->phase == HANDOFF_CANCELLED) &&
           oem_sleep_wake_program_can_release(&h->program) && oem_sleep_wake_can_release(h->bridge);
}
