#include "gdsc_native.h"
#include "domains.h"
#include <string.h>

static int valid_identifier(const char* s, size_t bytes)
{
    size_t i;
    if (!s || bytes == 0u)
        return 0;
    for (i = 0u; i < bytes; ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (!c)
            return i > 0u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 0;
}

static int owned(const fs_gdsc_driver_t* d)
{
    return d && d->bound && !d->quarantined && !d->npa.quarantined && d->epoch && d->owns &&
           d->owns(d->owner_ctx, d->domain, d->epoch);
}

static int rails_consistent(const fs_gdsc_driver_t* d)
{
    return d && (!d->requires_rails ||
                 (d->rails.verify_votes && d->rails.verify_votes(d->rails.ctx, d->domain, d->epoch,
                                                                 d->rails_held ? 1u : 0u)));
}

/* do not treat PWR_ON remaining set after a collapse request as proof the local GDSC vote
 * failed: an independent hardware/firmware voter may hold the domain. SW_COLLAPSE is the
 * local software request readback. */

/* this is verified for the UFS PHY only. */

static int supports_hw_control(const fs_gdsc_driver_t* d)
{
    return d && !strcmp(d->domain, "gcc_ufs_phy_gdsc");
}

static int verify_hw(fs_gdsc_driver_t* d, uint32_t action)
{
    uint32_t v = 0u, w = 0u;
    if (!owned(d) || !d->io.read32 || !d->io.read32(d->io.ctx, d->clock->gdscr_offset, &v) ||
        !d->io.read32(d->io.ctx, d->clock->gdscr_offset, &w) || v != w || !owned(d))
        return 0;
    if (action == 1u)
        return !(v & GDSCR_SW_COLLAPSE) && (v & GDSCR_PWR_ON);
    if (action == 2u)
        return (v & GDSCR_SW_COLLAPSE) != 0u;
    if ((action == 3u || action == 4u) && supports_hw_control(d))
        return !!(v & GDSCR_HW_CONTROL) == (action == 3u);
    return 0;
}

static int check_hw(fs_gdsc_driver_t* d)
{
    if (!owned(d))
        return 0;
    return rails_consistent(d) &&
           (!((d->effective_action == 1u || d->effective_hw_action == 3u) && !d->rails_held)) &&
           (d->effective_action == 0u || verify_hw(d, d->effective_action)) &&
           (d->effective_hw_action == 0u || verify_hw(d, d->effective_hw_action));
}

/* the HAL is MMIO RMW and verifies the same bit twice, retaining uncertain controller
 * state after any write failure. Caller holds the physical owner lock. */

static int apply_hw_control(fs_gdsc_driver_t* d, uint32_t action)
{
    uint32_t before = 0u, value;
    if (!supports_hw_control(d) || (action != 3u && action != 4u) || !owned(d) || !d->io.read32 ||
        !d->io.write32 || !d->io.barrier ||
        !d->io.read32(d->io.ctx, d->clock->gdscr_offset, &before) || !owned(d))
        return 0;
    value = action == 3u ? before | GDSCR_HW_CONTROL : before & ~GDSCR_HW_CONTROL;
    if (value != before) {
        if (!d->io.write32(d->io.ctx, d->clock->gdscr_offset, value))
            return 0;
        d->io.barrier(d->io.ctx);
    }
    return verify_hw(d, action);
}

static int node_owns(void* ctx, const char* domain, uint32_t id, uint64_t epoch)
{
    fs_gdsc_driver_t* d = (fs_gdsc_driver_t*)ctx;
    return owned(d) && domain && !strcmp(domain, d->domain) && id == d->domain_id &&
           epoch == d->epoch;
}

static int node_read(void* ctx, const char* domain, uint64_t epoch, uint32_t family,
                     uint32_t* action)
{
    fs_gdsc_driver_t* d = (fs_gdsc_driver_t*)ctx;
    uint32_t a, b;
    if (!d || !action || !node_owns(d, domain, d->domain_id, epoch) || family > 1u ||
        (family && !supports_hw_control(d)) || !rails_consistent(d) ||
        !d->io.read32(d->io.ctx, d->clock->gdscr_offset, &a) ||
        !d->io.read32(d->io.ctx, d->clock->gdscr_offset, &b) || a != b || !owned(d))
        return 0;
    if (family)
        *action = (a & GDSCR_HW_CONTROL) ? 3u : 4u;
    else if (a & GDSCR_SW_COLLAPSE)
        *action = 2u;
    else if (a & GDSCR_PWR_ON)
        *action = 1u;
    else
        return 0;
    return 1;
}

static int node_apply(void* ctx, const char* domain, uint64_t epoch, uint32_t action)
{
    fs_gdsc_driver_t* d = (fs_gdsc_driver_t*)ctx;
    uint32_t normal, hardware;
    int enabling;
    if (!d || !node_owns(d, domain, d->domain_id, epoch) || action < 1u || action > 4u ||
        (action > 2u && !supports_hw_control(d)))
        return 0;
    if (!check_hw(d))
        goto lost;
    enabling = action == 1u || action == 3u;
    if (enabling && d->requires_rails && !d->rails_held) {
        if (!d->rails.acquire_votes(d->rails.ctx, d->domain, d->epoch))
            goto lost;
        d->rails_held = 1u;
        if (!owned(d) || !rails_consistent(d))
            goto lost;
    }
    if (action > 2u) {
        if (!apply_hw_control(d, action))
            goto lost;
    } else {
        gcc_result_t result =
            action == 1u ? gcc_enable(&d->io, d->clock) : gcc_disable(&d->io, d->clock);
        if (result != GCC_OK || !verify_hw(d, action))
            goto lost;
    }
    normal = action < 3u ? action : d->effective_action;
    hardware = action > 2u ? action : d->effective_hw_action;
    if (!enabling && d->requires_rails && d->rails_held && normal != 1u && hardware != 3u) {
        if (!owned(d) || !d->rails.release_votes(d->rails.ctx, d->domain, d->epoch))
            goto lost;
        d->rails_held = 0u;
    }
    if (!owned(d) || !rails_consistent(d) || !verify_hw(d, action))
        goto lost;
    d->effective_action = normal;
    d->effective_hw_action = hardware;
    return 1;
lost:
    d->quarantined = 1u;
    return 0;
}

int fs_gdsc_init_window_with_rails(fs_gdsc_driver_t* d, const gcc_io_t* io, const char* domain,
                                   uint32_t id, uint64_t window_phys, uint32_t window_bytes,
                                   void* owner, int (*owns)(void*, const char*, uint64_t),
                                   uint64_t epoch, const fs_gdsc_rail_provider_t* rails)
{
    molokai_gdsc_location_t location;
    const gcc_clock_t* gcc;
    gcc_clock_t local = {0};
    uint32_t baseline;
    fs_node_io_t nodes = {0};
    if (!d || d->bound || d->quarantined || !io || !io->read32 || !io->write32 || !io->barrier ||
        !io->stall_microseconds || !valid_identifier(domain, FS_NAME_BYTES) || !owns || !epoch ||
        !id || !owns(owner, domain, epoch) || !molokai_gdsc_location(domain, &location) ||
        (!rails || !rails->acquire_votes || !rails->verify_votes || !rails->release_votes ||
         rails->rail_mask != (!strcmp(domain, "gcc_ufs_phy_gdsc") ? (RAIL_VDD_CX | RAIL_VDD_MX)
                                                                  : (RAIL_VDD_MM | RAIL_VDD_MX)) ||
         !rails->verify_votes(rails->ctx, domain, epoch, 0u)) ||
        location.legacy8480_clock_id != id || location.controller_physical != window_phys ||
        location.controller_bytes != window_bytes ||
        location.gdscr_physical != window_phys + (uint64_t)location.gdscr_offset)
        return 0;
    /* disp_CC has a separate verified HAL GenericPowerDomainControl GDSCR, not a GCC register. */

    gcc = gcc_find_clock(domain);
    if (gcc) {
        if (gcc->kind != GCC_KIND_GDSC || gcc->gdscr_offset != location.gdscr_offset ||
            (gcc->vote_offset && gcc->vote_mask))
            return 0;
        local = *gcc;
    } else if (!strcmp(domain, "disp_cc_mdss_core_gdsc")) {
        local.name = "disp_cc_mdss_core_gdsc";
        local.kind = GCC_KIND_GDSC;
        local.domain_idx = -1;
        local.gdscr_offset = location.gdscr_offset;
    } else
        return 0;
    if (!io->read32(io->ctx, location.gdscr_offset, &baseline) || !owns(owner, domain, epoch))
        return 0;
    /* the baseline is read, never counted as this client's hardware vote. */
    (void)baseline;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->local_clock = local;
    d->clock = &d->local_clock;
    d->owner_ctx = owner;
    d->owns = owns;
    d->epoch = epoch;
    d->domain_id = id;

    d->rails = *rails;
    d->requires_rails = 1u;
    memcpy(d->domain, domain, strlen(domain) + 1u);
    d->bound = 1u;
    if (!owned(d) || !rails_consistent(d)) {
        d->quarantined = 1u;
        return 0;
    }
    nodes.ctx = d;
    nodes.owns = node_owns;
    nodes.read_action = node_read;
    nodes.apply_action = node_apply;
    if (!fs_nodes_bind(&d->npa, domain, id,
                       FS_NODE_NORMAL | (supports_hw_control(d) ? FS_NODE_HWCTL : 0u), &nodes,
                       epoch)) {
        d->quarantined = 1u;
        return 0;
    }
    return 1;
}

int fs_gdsc_init_window(fs_gdsc_driver_t* d, const gcc_io_t* io, const char* domain, uint32_t id,
                        uint64_t window_phys, uint32_t window_bytes, void* owner,
                        int (*owns)(void*, const char*, uint64_t), uint64_t epoch)
{

    return fs_gdsc_init_window_with_rails(d, io, domain, id, window_phys, window_bytes, owner, owns,
                                          epoch, NULL);
}

int fs_gdsc_init(fs_gdsc_driver_t* d, const gcc_io_t* io, const char* domain, uint32_t id,
                 void* owner, int (*owns)(void*, const char*, uint64_t), uint64_t epoch)
{
    /* never map DISP_CC through an IO callback meant for the GCC physical register window. */

    return fs_gdsc_init_window(d, io, domain, id, GCC_WINDOW_PHYS, GCC_WINDOW_BYTES, owner, owns,
                               epoch);
}

int fs_gdsc_provider(fs_gdsc_driver_t* d, fs_factory_provider_t* p)
{
    if (!d || !p || !owned(d) || !check_hw(d))
        return 0;
    return fs_nodes_provider(&d->npa, p);
}
