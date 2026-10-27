#include "oem_rsc_tcs_survey.h"
#include <string.h>

/* read only documented firmware RSC registers. Caller serializes map life, controller
 * access, PnP, interrupt callbacks and proof under one lock. */

static int capture(const oem_rsc_read_io_t* io, volatile void* base, oem_rsc_tcs_survey_t* s)
{
    uint32_t i;
    rsc_hlos_geometry_t preliminary = {0};
    if (io->read32(io->ctx, base, RSC_ID_OFFSET, &s->rsc_id) != 1 ||
        io->read32(io->ctx, base, RSC_PARENTCHILD_CONFIG_OFFSET, &s->parent_child_config) != 1 ||
        !rpmh_tcs_check_hlos_geometry(s->parent_child_config, MOLOKAI_WIN_TCS_TOTAL, TCS_BATCH_MAX,
                                      &preliminary) ||
        io->read32(io->ctx, base, TCS_IRQ_ENABLE, &s->irq_enable) != 1 ||
        io->read32(io->ctx, base, TCS_IRQ_STATUS, &s->irq_status) != 1)
        return 0;
    for (i = 0; i < OEM_RSC_SURVEY_TCS_CNT; i++) {
        oem_rsc_tcs_state_t* t = &s->tcs[i];
        if (io->read32(io->ctx, base, TCS_CONTROL(i), &t->control) != 1 ||
            io->read32(io->ctx, base, TCS_STATUS(i), &t->status) != 1 ||
            io->read32(io->ctx, base, TCS_ENABLE(i), &t->command_enable) != 1 ||
            io->read32(io->ctx, base, TCS_WAIT(i), &t->wait_for_completion) != 1)
            return 0;
    }
    return 1;
}

static int same_hardware(const oem_rsc_tcs_survey_t* a, const oem_rsc_tcs_survey_t* b)
{
    uint32_t i;
    if (a->rsc_id != b->rsc_id || a->parent_child_config != b->parent_child_config ||
        a->irq_enable != b->irq_enable || a->irq_status != b->irq_status)
        return 0;
    for (i = 0; i < OEM_RSC_SURVEY_TCS_CNT; i++) {
        if (a->tcs[i].control != b->tcs[i].control || a->tcs[i].status != b->tcs[i].status ||
            a->tcs[i].command_enable != b->tcs[i].command_enable ||
            a->tcs[i].wait_for_completion != b->tcs[i].wait_for_completion)
            return 0;
    }
    return 1;
}

int oem_rsc_tcs_survey(oem_direct_rsc_map_t* map, uint64_t epoch, const oem_rsc_read_io_t* io,
                       oem_rsc_tcs_survey_t* out)
{
    oem_rsc_tcs_survey_t a = {0}, b = {0};
    rsc_hlos_geometry_t geometry = {0};
    volatile void* base;
    uint32_t i;
    int ok = 0;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!map || !out || !io || !io->read32 || !epoch || map->phase != OEM_RSC_MAP_LIVE ||
        map->owner_epoch != epoch || !map->io.owns)
        return 0;
    base = (volatile void*)oem_direct_rsc_map_borrow(map, epoch);
    if (!base)
        return 0;
    /* map is borrowed before the first read and remains borrowed until the final ownership
     * proof. A scan cannot be carried over an owner epoch. */

    if (map->io.owns(map->io.ctx, epoch) != 1 || capture(io, base, &a) != 1 ||
        map->io.owns(map->io.ctx, epoch) != 1 || capture(io, base, &b) != 1 ||
        map->io.owns(map->io.ctx, epoch) != 1 || !same_hardware(&a, &b) || a.rsc_id == 0u ||
        a.rsc_id == UINT32_MAX || a.parent_child_config == 0u ||
        a.parent_child_config == UINT32_MAX ||
        !rpmh_tcs_check_hlos_geometry(a.parent_child_config, MOLOKAI_WIN_TCS_TOTAL, TCS_BATCH_MAX,
                                      &geometry))
        goto done;
    a.version = OEM_RSC_SURVEY_VERSION;
    a.owner_epoch = epoch;
    a.tcs_cnt = geometry.tcs_cnt;
    a.commands_per_tcs = geometry.commands_per_tcs;
    a.controller_id_matches_firmware = ((a.rsc_id & RSC_ID_MASK) == RSC_ID_POR);
    a.geometry_meets_firmware = 1u;
    a.pending_irq_mask = a.irq_status & ((1u << OEM_RSC_SURVEY_TCS_CNT) - 1u);
    for (i = 0; i < OEM_RSC_SURVEY_TCS_CNT; i++) {
        uint32_t bit = 1u << i;
        if ((a.tcs[i].status & TCS_STATUS_IDLE) == 0u)
            a.not_idle_mask |= bit;
        if (a.tcs[i].command_enable != 0u || a.tcs[i].wait_for_completion != 0u)
            a.configured_mask |= bit;
        if ((a.tcs[i].control & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)) != 0u)
            a.amc_mode_or_trigger_mask |= bit;
    }
    a.quiescent_observation =
        (a.controller_id_matches_firmware == 1u &&
         (a.irq_enable & ((1u << OEM_RSC_SURVEY_TCS_CNT) - 1u)) == 0u && a.pending_irq_mask == 0u &&
         a.not_idle_mask == 0u && a.configured_mask == 0u && a.amc_mode_or_trigger_mask == 0u);
    ok = 1;
done:
    if (!oem_direct_rsc_map_return(map, epoch, (void*)base))
        ok = 0;
    if (!ok) {
        map->phase = OEM_RSC_MAP_FAULT;
        return 0;
    }
    *out = a;
    return 1;
}
