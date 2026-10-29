/* graph operations hold its nonrecursive lock at PASSIVE_LEVEL. gate probes also run
 * outside transactions and must not block or fault. */

#include "hw_win.h"
#include "clock_target.h"
#include <string.h>

#define SNAP_MAGIC 0x57374B42u

/* snapshot opaque[8]: [0] signature<<32|kind; [1] packed readback; [2] readback-valid; [3]
 * prior-vote-known; [4..7] packed prior vote. */

enum {
    SN_HDR = 0,
    SN_READ = 1,
    SN_READ_OK = 2,
    SN_VOTE_OK = 3,
    SN_VOTE0 = 4
};

const char* win_backend_probe_clock_name(uint32_t key)
{
    return clock_probe_name(key);
}

static int valid_device_path(const char* p)
{
    size_t n = 0;
    if (!p)
        return 0;
    while (n < 256u && p[n])
        ++n;
    return n >= 5u && n < 256u && p[0] == '\\';
}

static int valid_resource_name(const char* s, size_t cap, const char* prefix)
{
    size_t n = 0, pn = prefix ? strlen(prefix) : 0u;
    if (!s)
        return 0;
    while (n < cap && s[n]) {
        unsigned char c = (unsigned char)s[n];
        if (c < 0x20u || c > 0x7Eu)
            return 0;
        ++n;
    }
    if (n == 0u || n >= cap)
        return 0;
    if (pn && (n < pn || memcmp(s, prefix, pn) != 0))
        return 0;
    return 1;
}

static uint64_t pack2(uint32_t lo, uint32_t hi)
{
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static uint32_t lo32(uint64_t v)
{
    return (uint32_t)(v & 0xFFFFFFFFu);
}

static uint32_t hi32(uint64_t v)
{
    return (uint32_t)(v >> 32);
}

static uint64_t raw8_to_u64(const pm_gpio_state_raw_t* r)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8u; ++i)
        v |= (uint64_t)r->bytes[i] << (8u * i);
    return v;
}

static uint64_t tlmm_pack(const tlmm_gpio_t* g)
{
    return pack2(g->pin | (g->function << 16), (g->state & 1u) | ((g->direction & 1u) << 1) |
                                                   ((g->pull & 0xFu) << 2) |
                                                   ((g->drive & 0xFFu) << 8));
}

static int tlmm_equal(const tlmm_gpio_t* a, const tlmm_gpio_t* b)
{
    return a->pin == b->pin && a->state == b->state && a->function == b->function &&
           a->direction == b->direction && a->pull == b->pull && a->drive == b->drive;
}

static int valid_snapshot_header(const power_snapshot_t* s, uint32_t kind)
{
    return s && hi32(s->opaque[SN_HDR]) == SNAP_MAGIC && lo32(s->opaque[SN_HDR]) == kind;
}

static void snap_init(power_snapshot_t* s, uint32_t kind)
{
    memset(s, 0, sizeof(*s));
    s->opaque[SN_HDR] = pack2(kind, SNAP_MAGIC);
}

static win_backend_t* core_of(void* c)
{
    win_backend_t* b = (win_backend_t*)c;
    return (b && b->initialized && b->version == WIN_BACKEND_VERSION) ? b : 0;
}

static void diag_bump(uint32_t* arr, uint32_t kind)
{
    if (kind < POWER_KIND_COUNT && arr[kind] != 0xFFFFFFFFu)
        ++arr[kind];
}

static int same_str(const char* a, const char* b)
{
    if (!a && !b)
        return 1;
    if (!a || !b)
        return 0;
    return strncmp(a, b, POWER_NAME_BYTES) == 0;
}

/* native Windows installs its RPMh callbacks at PrepareHardware, while the actual RSC/IRQ
 * owner may not become operational until d0. Keep runtime capability decisions tied to the
 * owner's live execution state. */

static int rpmh_execution_ready(const win_backend_t* b)
{
    return b && (!b->io.rpmh_execution_ready || b->io.rpmh_execution_ready(b->io.ctx) == 1);
}

/* production supplies live readiness checks and repeats them under the owner mutex. */

static int pmic_gpio_execution_ready(const win_backend_t* b)
{
    return b &&
           (!b->io.pmic_gpio_execution_ready || b->io.pmic_gpio_execution_ready(b->io.ctx) == 1);
}

static int clock_execution_ready(const win_backend_t* b)
{
    return b && (!b->io.clock_execution_ready || b->io.clock_execution_ready(b->io.ctx) == 1);
}

static int tlmm_execution_ready(const win_backend_t* b)
{
    return b && (!b->io.tlmm_execution_ready || b->io.tlmm_execution_ready(b->io.ctx) == 1);
}

hw_delegation_t win_backend_delegation(const win_backend_t* b, power_action_kind_t kind)
{
    if (!b)
        return HW_DELEGATION_NOT_ESTABLISHED;
    switch (kind) {
        case POWER_DELAY:
            return b->io.delay_ms ? HW_DELEGATION_LIVE : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_PMIC_GPIO:
            return (b->io.pmic_gpio_cfg_digital_output && b->io.pmic_gpio_query_raw_state &&
                    pmic_gpio_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_TLMM_GPIO:
            return (b->io.tlmm_cfg && b->io.tlmm_readback && tlmm_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_PMIC_REGULATOR:
            return (b->io.pmic_regulator_vote && b->io.pmic_regulator_readback &&
                    rpmh_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_CLOCK:
            return (b->io.clock_apply && b->io.clock_readback && clock_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_ICB:
            return (b->io.bus_vote && b->io.bus_readback && rpmh_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_NPA:
            return (b->io.arc_known && b->io.arc_vote && b->io.arc_withdraw && b->io.arc_readback &&
                    b->io.arc_client && rpmh_execution_ready(b))
                       ? HW_DELEGATION_LIVE
                       : HW_DELEGATION_NOT_ESTABLISHED;
        case POWER_PSTATE_ADJUST:
        case POWER_FOOTSWITCH:
            /* nested references execute only through the pinned component owner. */

            return HW_DELEGATION_REFUSED_BY_RULE;
        default:
            return HW_DELEGATION_NOT_ESTABLISHED;
    }
}

void win_backend_report(const win_backend_t* b, win_backend_report_t* r)
{
    int k;
    if (!r)
        return;
    memset(r, 0, sizeof(*r));
    if (!b)
        return;
    for (k = (int)POWER_CLOCK; k < (int)POWER_KIND_COUNT; ++k) {
        hw_delegation_t d = win_backend_delegation(b, (power_action_kind_t)k);
        r->delegation[k] = d;
        if (d == HW_DELEGATION_LIVE)
            ++r->live_kinds;
        else if (d == HW_DELEGATION_REFUSED_BY_RULE)
            ++r->refused_kinds;
        else
            ++r->not_established_kinds;
    }
    r->clock_readback_present = b->io.clock_readback != 0;
    r->registered = b->registered;
}

int win_backend_five_delegations_live(const win_backend_t* b)
{
    static const power_action_kind_t kinds[5] = {POWER_CLOCK, POWER_ICB, POWER_TLMM_GPIO,
                                                 POWER_PMIC_GPIO, POWER_PMIC_REGULATOR};
    unsigned i;
    if (!b || !b->registered)
        return 0;
    for (i = 0; i < 5u; ++i)
        if (win_backend_delegation(b, kinds[i]) != HW_DELEGATION_LIVE)
            return 0;
    return 1;
}

static int clock_mem_config(const power_action_t* a)
{
    return a && a->kind == POWER_CLOCK && a->u.clock.vote == 9u && a->u.clock.extra_cnt == 1u &&
           clock_config_rule(a->u.clock.name, a->u.clock.extra[0]);
}

static int validate_action(const power_action_t* a)
{
    tlmm_gpio_t cfg;
    if (!a)
        return 0;
    switch (a->kind) {
        case POWER_DELAY:
            return a->u.delay.ms <= 5000u;
        case POWER_PMIC_GPIO:
            return a->u.pmic_gpio.ioctl == PM_GPIO_CONFIG_DIGITAL_OUTPUT_IOCTL;
        case POWER_TLMM_GPIO:

            return tlmm_decode_dsd_t_fields(a->u.gpio.fields, &cfg);
        case POWER_PMIC_REGULATOR:
            return valid_resource_name(a->u.pmic_regulator.name, POWER_NAME_BYTES,
                                       "PPP_RESOURCE_ID_");
        case POWER_CLOCK:
            if (!valid_resource_name(a->u.clock.name, POWER_NAME_BYTES, NULL))
                return 0;
            if (clock_mem_config(a))
                return 1;

            if (a->u.clock.extra_cnt == 0u && (a->u.clock.vote == 1u || a->u.clock.vote == 2u))
                return 1;
            if (a->u.clock.extra_cnt == 2u && (a->u.clock.vote == 8u || a->u.clock.vote == 3u) &&
                a->u.clock.extra[0] != 0u)
                return 1;
            return 0;
        case POWER_ICB:
            return valid_resource_name(a->u.icb.master, POWER_NAME_BYTES, "ICBID_MASTER_") &&
                   valid_resource_name(a->u.icb.slave, POWER_NAME_BYTES, "ICBID_SLAVE_");
        case POWER_NPA:
            return a->u.npa.type == 1u &&
                   valid_resource_name(a->u.npa.name, POWER_NAME_BYTES, "/arc/client/");
        case POWER_PSTATE_ADJUST:
            return 0; /* never executed by this backend. */
        default:
            return 0;
    }
}

/* owner-side admission: a res the owner could not resolve is refused even though the shape
 * is fine. */

static int owner_knows(win_backend_t* b, const power_action_t* a)
{
    /* resource lookup must reject a staged RPMh provider without its deferred-completion owner. */

    if ((a->kind == POWER_ICB || a->kind == POWER_PMIC_REGULATOR || a->kind == POWER_NPA) &&
        !rpmh_execution_ready(b))
        return 0;
    switch (a->kind) {
        case POWER_CLOCK:
            return !b->io.clock_known || b->io.clock_known(b->io.ctx, a->u.clock.name);
        case POWER_ICB:
            return !b->io.bus_route_known ||
                   b->io.bus_route_known(b->io.ctx, a->u.icb.master, a->u.icb.slave);
        case POWER_PMIC_REGULATOR:
            return !b->io.pmic_regulator_known ||
                   b->io.pmic_regulator_known(b->io.ctx, a->u.pmic_regulator.name);
        case POWER_NPA:
            return b->io.arc_known && b->io.arc_known(b->io.ctx, a->u.npa.name, a->u.npa.level);
        default:
            return 1;
    }
}

/* last-vote cache (keyed by client and res). */

static int vote_key(const power_action_t* a, uint32_t* key, const char** n1, const char** n2)
{
    *key = 0;
    *n1 = 0;
    *n2 = 0;
    switch (a->kind) {
        case POWER_PMIC_GPIO:
            *key = PROBE_PMIC_GPIO_KEY(a->u.pmic_gpio.fields[0], a->u.pmic_gpio.fields[1]);
            return 1;
        case POWER_TLMM_GPIO:
            *key = a->u.gpio.fields[0];
            return 1;
        case POWER_CLOCK:
            *n1 = a->u.clock.name;
            return 1;
        case POWER_PMIC_REGULATOR:
            *n1 = a->u.pmic_regulator.name;
            return 1;
        case POWER_NPA:
            *n1 = a->u.npa.name;
            *key = a->u.npa.type;
            return 1;
        case POWER_ICB:
            *n1 = a->u.icb.master;
            *n2 = a->u.icb.slave;
            *key = a->u.icb.client;
            return 1;
        default:
            return 0;
    }
}

static int vote_shaped(uint32_t kind)
{
    return kind == POWER_CLOCK || kind == POWER_ICB || kind == POWER_PMIC_REGULATOR ||
           kind == POWER_NPA;
}

static last_vote_t* vote_find(win_backend_t* b, const char* path, const power_action_t* a,
                              int create)
{
    uint32_t key, i;
    const char* n1;
    const char* n2;
    last_vote_t* free_slot = 0;
    if (!vote_key(a, &key, &n1, &n2))
        return 0;
    for (i = 0; i < WIN_BACKEND_MAX_VOTES; ++i) {
        last_vote_t* v = &b->votes[i];
        if (!v->in_use) {
            if (!free_slot)
                free_slot = v;
            continue;
        }
        if (v->kind != (uint8_t)a->kind || v->key != key)
            continue;
        if (!same_str(n1 ? v->name : 0, n1) || !same_str(n2 ? v->name2 : 0, n2))
            continue;
        /* vote-shaped records belong to one client; pin records to the pin. */
        if (vote_shaped(a->kind) && strncmp(v->path, path, sizeof(v->path)) != 0)
            continue;
        return v;
    }
    if (!create || !free_slot)
        return 0;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->in_use = 1u;
    free_slot->kind = (uint8_t)a->kind;
    free_slot->key = key;
    strncpy(free_slot->path, path, sizeof(free_slot->path) - 1u);
    if (n1)
        memcpy(free_slot->name, n1, strlen(n1) + 1u);
    if (n2)
        memcpy(free_slot->name2, n2, strlen(n2) + 1u);
    return free_slot;
}

static void vote_record(win_backend_t* b, const char* path, const power_action_t* a)
{
    last_vote_t* v = vote_find(b, path, a, 1);
    if (v) {
        memcpy(&v->action, a, sizeof(*a));
        v->recorded = 1u;
    }
}

static void vote_forget(win_backend_t* b, const char* path, const power_action_t* a)
{
    last_vote_t* v = vote_find(b, path, a, 0);
    if (v)
        memset(v, 0, sizeof(*v));
}

static const last_vote_t* vote_prior(win_backend_t* b, const char* path, const power_action_t* a)
{
    last_vote_t* v = vote_find(b, path, a, 0);
    return (v && v->recorded) ? v : 0;
}

/* independent readback per kind. */

static int readback_pmic_gpio(win_backend_t* b, uint32_t pmic, uint32_t gpio, uint64_t* out)
{
    pm_gpio_state_query_t q;
    pm_gpio_state_raw_t raw;
    if (!b->io.pmic_gpio_query_raw_state)
        return 0;
    q.pmic_number = pmic;
    q.gpio = gpio;
    q.write_configured_pins = 0u;
    memset(&raw, 0, sizeof(raw));
    if (!b->io.pmic_gpio_query_raw_state(b->io.ctx, &q, &raw))
        return 0;
    *out = raw8_to_u64(&raw);
    return 1;
}

static int readback_action(win_backend_t* b, const power_action_t* a, uint64_t* out,
                           tlmm_gpio_t* tlmm_out)
{
    uint64_t v = 0;
    switch (a->kind) {
        case POWER_PMIC_GPIO:
            if (!readback_pmic_gpio(b, a->u.pmic_gpio.fields[0], a->u.pmic_gpio.fields[1], &v))
                return 0;
            break;
        case POWER_TLMM_GPIO: {
            tlmm_gpio_t eff;
            if (!b->io.tlmm_readback)
                return 0;
            memset(&eff, 0, sizeof(eff));
            if (!b->io.tlmm_readback(b->io.ctx, a->u.gpio.fields[0], &eff))
                return 0;
            if (eff.pin != a->u.gpio.fields[0])
                return 0;
            if (tlmm_out)
                *tlmm_out = eff;
            v = tlmm_pack(&eff);
            break;
        }
        case POWER_CLOCK:
            if (clock_mem_config(a)) {
                uint32_t force = 0;
                if (!b->io.clock_config_readback ||
                    !b->io.clock_config_readback(b->io.ctx, a->u.clock.name, &force) || force > 1u)
                    return 0;
                v = force;
                break;
            }
            if (!b->io.clock_readback || !b->io.clock_readback(b->io.ctx, a->u.clock.name, &v))
                return 0;
            break;
        case POWER_PMIC_REGULATOR:
            if (!b->io.pmic_regulator_readback ||
                !b->io.pmic_regulator_readback(b->io.ctx, a->u.pmic_regulator.name, &v))
                return 0;
            break;
        case POWER_ICB:
            if (!b->io.bus_readback ||
                !b->io.bus_readback(b->io.ctx, a->u.icb.master, a->u.icb.slave, &v))
                return 0;
            break;
        case POWER_NPA:
            if (!b->io.arc_readback || !b->io.arc_readback(b->io.ctx, a->u.npa.name, &v))
                return 0;
            break;
        default:
            return 0;
    }
    *out = v;
    return 1;
}

static int apply_action(win_backend_t* b, const char* path, const power_action_t* a)
{
    switch (a->kind) {
        case POWER_DELAY:
            return b->io.delay_ms && b->io.delay_ms(b->io.ctx, a->u.delay.ms);
        case POWER_PMIC_GPIO: {
            pm_gpio_digital_output_t cfg;
            if (!b->io.pmic_gpio_cfg_digital_output)
                return 0;
            cfg.pmic_number = a->u.pmic_gpio.fields[0];
            cfg.gpio = a->u.pmic_gpio.fields[1];
            cfg.out_buf_config = a->u.pmic_gpio.fields[2];
            cfg.voltage_src = a->u.pmic_gpio.fields[3];
            cfg.src = a->u.pmic_gpio.fields[4];
            cfg.out_buf_strength = a->u.pmic_gpio.fields[5];
            cfg.i_src_pulls = a->u.pmic_gpio.fields[6];
            return b->io.pmic_gpio_cfg_digital_output(b->io.ctx, &cfg);
        }
        case POWER_TLMM_GPIO: {
            tlmm_gpio_t cfg;
            if (!b->io.tlmm_cfg || !tlmm_decode_dsd_t_fields(a->u.gpio.fields, &cfg))
                return 0;
            return b->io.tlmm_cfg(b->io.ctx, &cfg);
        }
        case POWER_PMIC_REGULATOR:
            return b->io.pmic_regulator_vote &&
                   b->io.pmic_regulator_vote(b->io.ctx, path, a->u.pmic_regulator.name,
                                             a->u.pmic_regulator.fields);
        case POWER_CLOCK:
            if (clock_mem_config(a))
                return b->io.clock_config_write &&
                       b->io.clock_config_write(b->io.ctx, a->u.clock.name, 1u);
            return b->io.clock_apply && b->io.clock_apply(b->io.ctx, path, a);
        case POWER_ICB:
            return b->io.bus_vote && b->io.bus_vote(b->io.ctx, path, a);
        case POWER_NPA:
            return b->io.arc_vote && b->io.arc_vote(b->io.ctx, path, a->u.npa.name, a->u.npa.level);
        default:
            return 0;
    }
}

static int withdraw_action(win_backend_t* b, const char* path, const power_action_t* a)
{
    switch (a->kind) {
        case POWER_PMIC_REGULATOR:
            return b->io.pmic_regulator_withdraw &&
                   b->io.pmic_regulator_withdraw(b->io.ctx, path, a->u.pmic_regulator.name);
        case POWER_CLOCK:
            return b->io.clock_withdraw && b->io.clock_withdraw(b->io.ctx, path, a->u.clock.name);
        case POWER_ICB:
            return b->io.bus_withdraw &&
                   b->io.bus_withdraw(b->io.ctx, path, a->u.icb.master, a->u.icb.slave);
        case POWER_NPA:
            return b->io.arc_withdraw && b->io.arc_withdraw(b->io.ctx, path, a->u.npa.name);
        default:
            return 0;
    }
}

static int op_supports(void* c, const char* path, uint32_t dstate, const power_action_t* a)
{
    win_backend_t* b = core_of(c);
    if (!b || !valid_device_path(path) || dstate > 3u || !a)
        return 0;
    if (win_backend_delegation(b, a->kind) != HW_DELEGATION_LIVE || !validate_action(a) ||
        !owner_knows(b, a) ||
        (clock_mem_config(a) && (!b->io.clock_config_readback || !b->io.clock_config_write))) {
        diag_bump(b->diag.refused, (uint32_t)a->kind);
        return 0;
    }
    return 1;
}

static int op_snapshot(void* c, const char* path, uint32_t dstate, const power_action_t* a,
                       power_snapshot_t* s)
{
    win_backend_t* b = core_of(c);
    const last_vote_t* prior;
    uint64_t read = 0;
    if (!b || !s || !op_supports(c, path, dstate, a))
        return 0;
    if (a->kind == POWER_DELAY) {
        /* timing has no restorable state; the transaction engine never snapshots a delay. */

        return 0;
    }
    snap_init(s, (uint32_t)a->kind);
    /* independent readback BEFORE touching anything. A class without a live readback cannot be
     * snapshotted and therefore cannot be applied. */

    if (!readback_action(b, a, &read, 0))
        return 0;
    s->opaque[SN_READ] = read;
    s->opaque[SN_READ_OK] = 1u;
    if (a->kind == POWER_PMIC_GPIO)
        b->diag.last_pmic_gpio_raw_before = read;
    /* prior vote by THIS client for the same res, if any. */
    /* configure is a physical controller bit, not a freq/enable vote. Its exact pre-state
     * comes from independent CBCR readback, never from another clock action's last-vote cache. */

    if (clock_mem_config(a))
        return 1;
    prior = vote_prior(b, path, a);
    if (prior) {
        const power_action_t* p = &prior->action;
        s->opaque[SN_VOTE_OK] = 1u;
        switch (p->kind) {
            case POWER_PMIC_GPIO:
                s->opaque[SN_VOTE0 + 0] = pack2(p->u.pmic_gpio.fields[0], p->u.pmic_gpio.fields[1]);
                s->opaque[SN_VOTE0 + 1] = pack2(p->u.pmic_gpio.fields[2], p->u.pmic_gpio.fields[3]);
                s->opaque[SN_VOTE0 + 2] = pack2(p->u.pmic_gpio.fields[4], p->u.pmic_gpio.fields[5]);
                s->opaque[SN_VOTE0 + 3] = pack2(p->u.pmic_gpio.fields[6], 0u);
                break;
            case POWER_TLMM_GPIO:
                s->opaque[SN_VOTE0 + 0] = pack2(p->u.gpio.fields[0], p->u.gpio.fields[1]);
                s->opaque[SN_VOTE0 + 1] = pack2(p->u.gpio.fields[2], p->u.gpio.fields[3]);
                s->opaque[SN_VOTE0 + 2] = pack2(p->u.gpio.fields[4], p->u.gpio.fields[5]);
                break;
            case POWER_CLOCK:
                s->opaque[SN_VOTE0 + 0] = pack2(p->u.clock.vote, p->u.clock.extra_cnt);
                s->opaque[SN_VOTE0 + 1] = pack2(p->u.clock.extra[0], p->u.clock.extra[1]);
                s->opaque[SN_VOTE0 + 2] = pack2(p->u.clock.extra[2], 0u);
                break;
            case POWER_PMIC_REGULATOR:
                s->opaque[SN_VOTE0 + 0] =
                    pack2(p->u.pmic_regulator.fields[0], p->u.pmic_regulator.fields[1]);
                s->opaque[SN_VOTE0 + 1] =
                    pack2(p->u.pmic_regulator.fields[2], p->u.pmic_regulator.fields[3]);
                s->opaque[SN_VOTE0 + 2] = pack2(p->u.pmic_regulator.fields[4], 0u);
                break;
            case POWER_ICB:
                s->opaque[SN_VOTE0 + 0] = p->u.icb.average;
                s->opaque[SN_VOTE0 + 1] = p->u.icb.peak;
                s->opaque[SN_VOTE0 + 2] = pack2(p->u.icb.client, 0u);
                break;
            case POWER_NPA:
                s->opaque[SN_VOTE0] = pack2(p->u.npa.level, p->u.npa.type);
                break;
            default:
                s->opaque[SN_VOTE_OK] = 0u;
                break;
        }
    }
    return 1;
}

static int op_apply(void* c, const char* path, uint32_t dstate, const power_action_t* a)
{
    win_backend_t* b = core_of(c);
    if (!b || !op_supports(c, path, dstate, a))
        return 0;
    /* reserve rollback storage before writing hardware so every attempted vote can be restored. */

    if (a->kind != POWER_DELAY && !clock_mem_config(a) && !vote_find(b, path, a, 1)) {
        diag_bump(b->diag.refused, (uint32_t)a->kind);
        return 0;
    }
    if (!apply_action(b, path, a))
        return 0;
    diag_bump(b->diag.applied, (uint32_t)a->kind);
    if (a->kind != POWER_DELAY && !clock_mem_config(a))
        vote_record(b, path, a);
    return 1;
}

static int op_verify(void* c, const char* path, uint32_t dstate, const power_action_t* a)
{
    win_backend_t* b = core_of(c);
    tlmm_gpio_t eff, want;
    uint64_t read = 0;
    if (!b || !op_supports(c, path, dstate, a))
        return 0;
    if (a->kind == POWER_DELAY)
        return 1;
    if (!readback_action(b, a, &read, &eff)) {
        diag_bump(b->diag.verify_failed, (uint32_t)a->kind);
        return 0;
    }
    if (clock_mem_config(a)) {
        if (read != 1u) {
            diag_bump(b->diag.verify_failed, (uint32_t)a->kind);
            return 0;
        }
        return 1;
    }
    switch (a->kind) {
        case POWER_NPA: {
            uint32_t level;
            return b->io.arc_client && b->io.arc_client(b->io.ctx, path, a->u.npa.name, &level) &&
                   (a->u.npa.level ? level >= a->u.npa.level : level == 0u) && read >= level;
        }
        case POWER_TLMM_GPIO:
            /* full configuration readback: must equal the request exactly. */
            if (!tlmm_decode_dsd_t_fields(a->u.gpio.fields, &want) || !tlmm_equal(&eff, &want)) {
                diag_bump(b->diag.verify_failed, (uint32_t)a->kind);
                return 0;
            }
            return 1;
        case POWER_PMIC_GPIO:
            /* get_GPIO_STATE is opaque (eight bytes, ). The provider's own pm_err_flag == 0 is the
             * only completion signal. */

            b->diag.last_pmic_gpio_raw_after = read;
            return 1;
        default:
            /* owner-defined effective state: liveness of the independent readback is required;
             * equality semantics belong to the owner (the aggregate of all clients, not this
             * client's request). */

            return 1;
    }
}

static int op_restore(void* c, const char* path, uint32_t dstate, const power_action_t* a,
                      const power_snapshot_t* s)
{
    win_backend_t* b = core_of(c);
    power_action_t prior;
    if (!b || !s || !op_supports(c, path, dstate, a))
        return 0;
    if (a->kind == POWER_DELAY)
        return 0; /* cannot un-elapse; never called. */
    if (!valid_snapshot_header(s, (uint32_t)a->kind))
        return 0;
    if (clock_mem_config(a)) {
        if (!s->opaque[SN_READ_OK] || s->opaque[SN_READ] > 1u || !b->io.clock_config_write ||
            !b->io.clock_config_write(b->io.ctx, a->u.clock.name, (uint32_t)s->opaque[SN_READ])) {
            diag_bump(b->diag.restore_failed, (uint32_t)a->kind);
            return 0;
        }
        diag_bump(b->diag.restored, (uint32_t)a->kind);
        return 1;
    }
    memcpy(&prior, a, sizeof(prior));
    if (s->opaque[SN_VOTE_OK]) {
        /* re-issue the exact vote this client issued before. */
        switch (a->kind) {
            case POWER_PMIC_GPIO:
                prior.u.pmic_gpio.fields[0] = lo32(s->opaque[SN_VOTE0 + 0]);
                prior.u.pmic_gpio.fields[1] = hi32(s->opaque[SN_VOTE0 + 0]);
                prior.u.pmic_gpio.fields[2] = lo32(s->opaque[SN_VOTE0 + 1]);
                prior.u.pmic_gpio.fields[3] = hi32(s->opaque[SN_VOTE0 + 1]);
                prior.u.pmic_gpio.fields[4] = lo32(s->opaque[SN_VOTE0 + 2]);
                prior.u.pmic_gpio.fields[5] = hi32(s->opaque[SN_VOTE0 + 2]);
                prior.u.pmic_gpio.fields[6] = lo32(s->opaque[SN_VOTE0 + 3]);
                break;
            case POWER_TLMM_GPIO:
                prior.u.gpio.fields[0] = lo32(s->opaque[SN_VOTE0 + 0]);
                prior.u.gpio.fields[1] = hi32(s->opaque[SN_VOTE0 + 0]);
                prior.u.gpio.fields[2] = lo32(s->opaque[SN_VOTE0 + 1]);
                prior.u.gpio.fields[3] = hi32(s->opaque[SN_VOTE0 + 1]);
                prior.u.gpio.fields[4] = lo32(s->opaque[SN_VOTE0 + 2]);
                prior.u.gpio.fields[5] = hi32(s->opaque[SN_VOTE0 + 2]);
                break;
            case POWER_CLOCK:
                prior.u.clock.vote = lo32(s->opaque[SN_VOTE0 + 0]);
                prior.u.clock.extra_cnt = hi32(s->opaque[SN_VOTE0 + 0]);
                prior.u.clock.extra[0] = lo32(s->opaque[SN_VOTE0 + 1]);
                prior.u.clock.extra[1] = hi32(s->opaque[SN_VOTE0 + 1]);
                prior.u.clock.extra[2] = lo32(s->opaque[SN_VOTE0 + 2]);
                break;
            case POWER_PMIC_REGULATOR:
                prior.u.pmic_regulator.fields[0] = lo32(s->opaque[SN_VOTE0 + 0]);
                prior.u.pmic_regulator.fields[1] = hi32(s->opaque[SN_VOTE0 + 0]);
                prior.u.pmic_regulator.fields[2] = lo32(s->opaque[SN_VOTE0 + 1]);
                prior.u.pmic_regulator.fields[3] = hi32(s->opaque[SN_VOTE0 + 1]);
                prior.u.pmic_regulator.fields[4] = lo32(s->opaque[SN_VOTE0 + 2]);
                break;
            case POWER_ICB:
                prior.u.icb.average = s->opaque[SN_VOTE0 + 0];
                prior.u.icb.peak = s->opaque[SN_VOTE0 + 1];
                prior.u.icb.client = lo32(s->opaque[SN_VOTE0 + 2]);
                break;
            case POWER_NPA:
                prior.u.npa.level = lo32(s->opaque[SN_VOTE0]);
                prior.u.npa.type = hi32(s->opaque[SN_VOTE0]);
                break;
            default:
                return 0;
        }
        if (!validate_action(&prior) || !apply_action(b, path, &prior)) {
            diag_bump(b->diag.restore_failed, (uint32_t)a->kind);
            return 0;
        }
        vote_record(b, path, &prior);
        diag_bump(b->diag.restored, (uint32_t)a->kind);
        return 1;
    }
    if (vote_shaped(a->kind)) {
        /* without a prior client vote, restore by withdrawing that client from the aggregate.
         * Verify the resulting readback against the snapshot. */

        if (!withdraw_action(b, path, a)) {
            diag_bump(b->diag.restore_failed, (uint32_t)a->kind);
            return 0;
        }
        vote_forget(b, path, a);
        diag_bump(b->diag.withdrawn, (uint32_t)a->kind);
        diag_bump(b->diag.restored, (uint32_t)a->kind);
        return 1;
    }
    if (a->kind == POWER_TLMM_GPIO && s->opaque[SN_READ_OK]) {
        /* no prior vote, but the readback was a FULL configuration: write that observed
         * configuration back. */

        tlmm_gpio_t cfg;
        uint64_t v = s->opaque[SN_READ];
        cfg.pin = lo32(v) & 0xFFFFu;
        cfg.function = (lo32(v) >> 16) & 0xFFFFu;
        cfg.state = hi32(v) & 1u;
        cfg.direction = (hi32(v) >> 1) & 1u;
        cfg.pull = (hi32(v) >> 2) & 0xFu;
        cfg.drive = (hi32(v) >> 8) & 0xFFu;
        if (!b->io.tlmm_cfg || cfg.pin != a->u.gpio.fields[0] || !b->io.tlmm_cfg(b->io.ctx, &cfg)) {
            diag_bump(b->diag.restore_failed, (uint32_t)a->kind);
            return 0;
        }

        memcpy(&prior, a, sizeof(prior));
        prior.u.gpio.fields[0] = cfg.pin;
        prior.u.gpio.fields[1] = cfg.state;
        prior.u.gpio.fields[2] = cfg.function;
        prior.u.gpio.fields[3] = cfg.direction;
        prior.u.gpio.fields[4] = cfg.pull;
        prior.u.gpio.fields[5] = cfg.drive;
        vote_record(b, path, &prior);
        diag_bump(b->diag.restored, (uint32_t)a->kind);
        return 1;
    }
    /* opaque readback without a prior client vote provides no valid restore target. */

    diag_bump(b->diag.restore_failed, (uint32_t)a->kind);
    return 0;
}

static int op_verify_restored(void* c, const char* path, uint32_t dstate, const power_action_t* a,
                              const power_snapshot_t* s)
{
    win_backend_t* b = core_of(c);
    uint64_t now = 0;
    if (!b || !s || !op_supports(c, path, dstate, a))
        return 0;
    if (a->kind == POWER_DELAY)
        return 0;
    if (!valid_snapshot_header(s, (uint32_t)a->kind) || !s->opaque[SN_READ_OK])
        return 0;
    /* the independent readback must return exactly what it returned before the failed
     * transaction touched this res. Byte equality is a valid check even for an opaque
     * readback. */

    if (!readback_action(b, a, &now, 0))
        return 0;
    if (a->kind == POWER_NPA) {
        uint32_t level, prior = s->opaque[SN_VOTE_OK] ? lo32(s->opaque[SN_VOTE0]) : 0u;
        return b->io.arc_client && b->io.arc_client(b->io.ctx, path, a->u.npa.name, &level) &&
               (prior ? level >= prior : level == 0u) && now >= level;
    }
    return now == s->opaque[SN_READ];
}

static int op_verify_final(void* c, const char* path, uint32_t dstate,
                           const power_sequence_t* sequence)
{
    win_backend_t* b = core_of(c);
    uint32_t i, j;
    if (!b || !sequence || !valid_device_path(path) || sequence->target_d_state != dstate ||
        strncmp(sequence->dev_path, path, 256u) != 0 || sequence->action_cnt == 0u ||
        sequence->action_cnt > POWER_MAX_ACTIONS)
        return 0;
    for (i = 0; i < sequence->action_cnt; ++i) {
        const power_action_t* a = &sequence->actions[i];
        int last = 1;
        uint64_t read = 0;
        tlmm_gpio_t eff, want;
        if (!op_supports(c, path, dstate, a))
            return 0;
        if (a->kind == POWER_DELAY)
            continue;

        for (j = i + 1u; j < sequence->action_cnt; ++j) {
            const power_action_t* o = &sequence->actions[j];
            if (o->kind != a->kind)
                continue;
            if (a->kind == POWER_TLMM_GPIO && o->u.gpio.fields[0] == a->u.gpio.fields[0]) {
                last = 0;
                break;
            }
            if (a->kind == POWER_PMIC_GPIO &&
                o->u.pmic_gpio.fields[0] == a->u.pmic_gpio.fields[0] &&
                o->u.pmic_gpio.fields[1] == a->u.pmic_gpio.fields[1]) {
                last = 0;
                break;
            }
            if (a->kind == POWER_CLOCK && clock_mem_config(a) == clock_mem_config(o) &&
                same_str(o->u.clock.name, a->u.clock.name)) {
                last = 0;
                break;
            }
            if (a->kind == POWER_PMIC_REGULATOR &&
                same_str(o->u.pmic_regulator.name, a->u.pmic_regulator.name)) {
                last = 0;
                break;
            }
            if (a->kind == POWER_NPA && same_str(o->u.npa.name, a->u.npa.name)) {
                last = 0;
                break;
            }
            if (a->kind == POWER_ICB && same_str(o->u.icb.master, a->u.icb.master) &&
                same_str(o->u.icb.slave, a->u.icb.slave)) {
                last = 0;
                break;
            }
        }
        if (!last)
            continue;
        if (!readback_action(b, a, &read, &eff))
            return 0;
        if (a->kind == POWER_NPA && !op_verify(c, path, dstate, a))
            return 0;
        if (clock_mem_config(a) && read != 1u)
            return 0;
        if (a->kind == POWER_TLMM_GPIO &&
            (!tlmm_decode_dsd_t_fields(a->u.gpio.fields, &want) || !tlmm_equal(&eff, &want)))
            return 0;
    }
    return 1;
}

static int op_release(void* c, const char* path, const power_action_t* a)
{
    win_backend_t* b = core_of(c);
    uint64_t read;
    uint32_t level;
    int released = 0;
    if (!b || !a || !vote_shaped(a->kind) || clock_mem_config(a) || !op_supports(c, path, 0u, a) ||
        !readback_action(b, a, &read, NULL) || !withdraw_action(b, path, a) ||
        !readback_action(b, a, &read, NULL))
        return 0;
    switch (a->kind) {
        case POWER_CLOCK:
            released =
                b->io.clock_has_vote && !b->io.clock_has_vote(b->io.ctx, path, a->u.clock.name);
            break;
        case POWER_ICB:
            released = b->io.bus_has_vote &&
                       !b->io.bus_has_vote(b->io.ctx, path, a->u.icb.master, a->u.icb.slave);
            break;
        case POWER_PMIC_REGULATOR:
            released = b->io.pmic_regulator_has_vote &&
                       !b->io.pmic_regulator_has_vote(b->io.ctx, path, a->u.pmic_regulator.name);
            break;
        case POWER_NPA:
            released = b->io.arc_client &&
                       b->io.arc_client(b->io.ctx, path, a->u.npa.name, &level) && !level;
            break;
        default:
            break;
    }
    if (released) {
        vote_forget(b, path, a);
        diag_bump(b->diag.withdrawn, (uint32_t)a->kind);
    }
    return released;
}

static int op_probe(void* c, uint32_t domain, uint32_t key, uint64_t* out)
{
    win_backend_t* b = core_of(c);
    uint64_t v = 0;
    int ok = 0;
    if (!b || !out)
        return 0;
    *out = 0;
    if (b->diag.probe_calls != 0xFFFFFFFFu)
        ++b->diag.probe_calls;
    switch (domain) {
        case HW_DOMAIN_CLOCK: {
            const char* name = win_backend_probe_clock_name(key);
            ok = name && b->io.clock_readback && b->io.clock_readback(b->io.ctx, name, &v);
            break;
        }
        case HW_DOMAIN_PMIC_GPIO:
            ok = readback_pmic_gpio(b, key >> 16, key & 0xFFFFu, &v);
            break;
        case HW_DOMAIN_TLMM: {
            tlmm_gpio_t eff;
            memset(&eff, 0, sizeof(eff));
            ok = b->io.tlmm_readback && b->io.tlmm_readback(b->io.ctx, key, &eff) && eff.pin == key;
            if (ok)
                v = tlmm_pack(&eff);
            break;
        }
        case HW_DOMAIN_PMIC_VREG: {
            const char* name = b->io.pmic_regulator_probe_name
                                   ? b->io.pmic_regulator_probe_name(b->io.ctx, key)
                                   : 0;
            ok = name && b->io.pmic_regulator_readback &&
                 b->io.pmic_regulator_readback(b->io.ctx, name, &v);
            break;
        }
        case HW_DOMAIN_BUS: {
            const char* slave = 0;
            const char* master =
                b->io.bus_probe_route ? b->io.bus_probe_route(b->io.ctx, key, &slave) : 0;
            ok = master && slave && b->io.bus_readback &&
                 b->io.bus_readback(b->io.ctx, master, slave, &v);
            break;
        }
        case HW_DOMAIN_UFS_PSTATE:
        case HW_DOMAIN_COMP_PERF:
            ok = 0;
            break;
        default:
            ok = 0;
            break;
    }
    if (!ok) {
        if (b->diag.probe_failed != 0xFFFFFFFFu)
            ++b->diag.probe_failed;
        return 0;
    }
    *out = v;
    return 1;
}

static int op_selftest(void* c)
{
    win_backend_t* b = core_of(c);
    const char* name = win_backend_probe_clock_name(0);
    uint64_t a = 0, d = 0;
    int ok;
    if (!b)
        return 0;
    if (b->diag.self_tests != 0xFFFFFFFFu)
        ++b->diag.self_tests;
    /* use local independent reads: validate the timing primitive and two clock sentinel samples. A
     * synchronous PMIC query can enumerate peer interfaces and must not run in this self-test. */

    ok = b->io.delay_ms && b->io.delay_ms(b->io.ctx, 0u) && name && b->io.clock_readback &&
         b->io.clock_readback(b->io.ctx, name, &a) && b->io.clock_readback(b->io.ctx, name, &d) &&
         a == d;
    if (!ok && b->diag.self_test_failed != 0xFFFFFFFFu)
        ++b->diag.self_test_failed;
    return ok;
}

/* npa client callbacks over the owner pool. */

static int valid_npa_key(const npa_key_t* k)
{
    return k && valid_device_path(k->dev_path) && k->res[0] && vote_shaped(k->kind);
}

static int npa_owns(void* c, const npa_key_t* k)
{
    win_backend_t* b = core_of(c);
    if (!b || !valid_npa_key(k))
        return 0;
    switch (k->kind) {
        case POWER_CLOCK:
            return b->io.clock_apply && b->io.clock_readback && b->io.clock_known &&
                   b->io.clock_known(b->io.ctx, k->res);
        case POWER_ICB:
            return b->io.bus_vote && b->io.bus_readback && b->io.bus_route_known && k->slave[0] &&
                   b->io.bus_route_known(b->io.ctx, k->res, k->slave);
        case POWER_PMIC_REGULATOR:
            return b->io.pmic_regulator_vote && b->io.pmic_regulator_readback &&
                   b->io.pmic_regulator_known && b->io.pmic_regulator_known(b->io.ctx, k->res);
        case POWER_NPA:
            return k->client == 1u && b->io.arc_known && b->io.arc_known(b->io.ctx, k->res, 0u);
        default:
            return 0;
    }
}

/* handles are opaque, non-dereferenceable cookies. A destroyed/recycled pool slot can
 * never validate its old cookie. */

static npa_client_slot_t* npa_slot_of(win_backend_t* b, void* h)
{
    uintptr_t token = (uintptr_t)h;
    uintptr_t raw_slot = token & (uintptr_t)0xFFu;
    npa_client_slot_t* s;
    if (!b || raw_slot == 0u || raw_slot > WIN_BACKEND_MAX_NPA_CLIENTS || (token >> 8u) == 0u)
        return 0;
    s = &b->npa_clients[(size_t)(raw_slot - 1u)];
    return s->in_use && s->handle_cookie == token ? s : 0;
}

static void* npa_create(void* c, const npa_key_t* k)
{
    win_backend_t* b = core_of(c);
    uint32_t i;
    if (!b || !npa_owns(c, k)) {
        if (b)
            ++b->diag.npa_refused;
        return 0;
    }
    /* one client per identity: a duplicate is a lifecycle bug, not a slot. */
    for (i = 0; i < WIN_BACKEND_MAX_NPA_CLIENTS; ++i)
        if (b->npa_clients[i].in_use && b->npa_clients[i].key.d_state == k->d_state &&
            npa_same_res_identity(&b->npa_clients[i].key, k) &&
            strncmp(b->npa_clients[i].key.dev_path, k->dev_path, sizeof(k->dev_path)) == 0) {
            ++b->diag.npa_refused;
            return 0;
        }
    for (i = 0; i < WIN_BACKEND_MAX_NPA_CLIENTS; ++i) {
        npa_client_slot_t* s = &b->npa_clients[i];
        uintptr_t serial, token;
        if (s->in_use)
            continue;
        /* never recycle a serial, including after destroy or failed admission. Reaching the
         * representable limit is a permanent fail-closed exhaustion, not permission to wrap and
         * revive stale cookies. */

        if (b->next_npa_handle_serial >= (UINTPTR_MAX >> 8u))
            break;
        serial = ++b->next_npa_handle_serial;
        token = (serial << 8u) | (uintptr_t)(i + 1u);
        memset(s, 0, sizeof(*s));
        s->in_use = 1u;
        s->handle_cookie = token;
        memcpy(&s->key, k, sizeof(s->key));
        ++b->diag.npa_created;
        return (void*)token;
    }
    ++b->diag.npa_refused;
    return 0;
}

static int npa_validate(void* c, const npa_key_t* k, void* h)
{
    win_backend_t* b = core_of(c);
    npa_client_slot_t* s;
    if (!b || !valid_npa_key(k))
        return 0;
    s = npa_slot_of(b, h);
    return s && s->key.kind == k->kind && s->key.d_state == k->d_state &&
           npa_same_res_identity(&s->key, k) &&
           strncmp(s->key.dev_path, k->dev_path, sizeof(k->dev_path)) == 0 && npa_owns(c, k);
}

static int npa_released(void* c, const npa_key_t* k, void* h)
{
    win_backend_t* b = core_of(c);
    if (!b || !npa_validate(c, k, h))
        return 0;
    /* owner-authoritative: the client must hold NO vote on the res. */

    switch (k->kind) {
        case POWER_CLOCK:
            return b->io.clock_has_vote && !b->io.clock_has_vote(b->io.ctx, k->dev_path, k->res);
        case POWER_ICB:
            return b->io.bus_has_vote &&
                   !b->io.bus_has_vote(b->io.ctx, k->dev_path, k->res, k->slave);
        case POWER_PMIC_REGULATOR:
            return b->io.pmic_regulator_has_vote &&
                   !b->io.pmic_regulator_has_vote(b->io.ctx, k->dev_path, k->res);
        case POWER_NPA: {
            uint32_t level;
            return b->io.arc_client && b->io.arc_client(b->io.ctx, k->dev_path, k->res, &level) &&
                   !level;
        }
        default:
            return 0;
    }
}

static int npa_destroy(void* c, const npa_key_t* k, void* h)
{
    win_backend_t* b = core_of(c);
    npa_client_slot_t* s;
    if (!b || !k)
        return 0;
    /* the provider's destroy callback is a security/lifetime boundary, not a blind pool-slot
     * free. Require the exact active identity AND a fresh owner-authoritative no-vote readback
     * even when the registry has already performed its own pre-destroy is_released check. */

    s = npa_slot_of(b, h);
    if (!s || !npa_released(c, k, h))
        return 0;
    memset(s, 0, sizeof(*s));
    ++b->diag.npa_destroyed;
    return 1;
}

void win_backend_npa_callbacks(win_backend_t* b, npa_callbacks_t* cb)
{
    if (!cb)
        return;
    memset(cb, 0, sizeof(*cb));
    cb->ctx = b;
    cb->owns = npa_owns;
    cb->create = npa_create;
    cb->validate = npa_validate;
    cb->is_released = npa_released;
    cb->destroy = npa_destroy;
}

int win_backend_init(win_backend_t* b, const hwio_t* io, uint64_t epoch)
{
    if (!b || !io || epoch == 0u)
        return -1;
    memset(b, 0, sizeof(*b));
    b->version = WIN_BACKEND_VERSION;
    b->owner_epoch = epoch;
    b->io = *io;
    b->table.version = HW_BACKEND_VERSION;
    b->table.owner_epoch = epoch;
    b->table.supports = op_supports;
    b->table.snapshot = op_snapshot;
    b->table.apply = op_apply;
    b->table.verify = op_verify;
    b->table.restore = op_restore;
    b->table.verify_restored = op_verify_restored;
    b->table.verify_final = op_verify_final;
    b->table.release = op_release;
    b->table.probe_readback = op_probe;
    b->table.self_test = op_selftest;
    b->table.ctx = b;

    b->table.production_attestation = 0u;
    b->initialized = 1u;
    return 0;
}

int win_backend_reg(win_backend_t* b)
{
    int rc;
    if (!b || !b->initialized)
        return -1;
    rc = hw_backend_reg(&b->table);
    if (rc == 0)
        b->registered = 1u;
    return rc;
}

int win_backend_unregister(win_backend_t* b)
{
    if (!b || !b->initialized)
        return 0;
    if (!hw_backend_unregister(b->owner_epoch))
        return 0; /* in-flight graph callback: Context must remain allocated. */
    b->registered = 0u;
    return 1;
}
