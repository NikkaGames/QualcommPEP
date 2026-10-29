#include "aeob_validate.h"
#include "molokai_catalog.h"
#include "8845_acpi_resources.h"
#include <string.h>

typedef struct aeob_param {
    uint16_t type;
    uint16_t length;
    const uint8_t* data;
} aeob_param_t;

static uint16_t read16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int next(const uint8_t* b, uint32_t len, uint32_t* pos, aeob_param_t* out)
{
    uint32_t n, pad;
    if (*pos > len || len - *pos < 4u)
        return 0;
    out->type = read16(b + *pos);
    out->length = read16(b + *pos + 2u);
    n = *pos + 4u;
    pad = out->length < 4u ? 4u : out->length;
    if (pad > len - n)
        return 0;
    out->data = b + n;
    *pos = n + pad;
    return 1;
}

static int is_integer(const aeob_param_t* p)
{
    return p->type == 0 && (p->length == 2 || p->length == 4 || p->length == 8);
}

static uint64_t int_value(const aeob_param_t* p)
{
    uint64_t v = 0;
    uint16_t i;
    for (i = 0; i < p->length && i < 8u; ++i)
        v |= (uint64_t)p->data[i] << (8u * i);
    return v;
}

static int is_string(const aeob_param_t* p, char* name, size_t cap)
{
    if (p->type != 1 || p->length < 2 || p->length > cap || p->data[p->length - 1] != 0 ||
        memchr(p->data, 0, p->length - 1u) != NULL)
        return 0;
    memcpy(name, p->data, p->length);
    return 1;
}

static int same(const char* a, const char* b)
{
    return strcmp(a, b) == 0;
}

static int lookup_master(const char* symbol, unsigned* id)
{
    unsigned i;
    for (i = 0; i < icb_master_cnt; ++i)
        if (same(symbol, icb_masters[i].symbol)) {
            *id = icb_masters[i].id;
            return 1;
        }
    return 0;
}

static int lookup_slave(const char* symbol, unsigned* id)
{
    unsigned i;
    for (i = 0; i < icb_slave_cnt; ++i)
        if (same(symbol, icb_slaves[i].symbol)) {
            *id = icb_slaves[i].id;
            return 1;
        }
    return 0;
}

static int validate_action(const aeob_action_t* a, void* ctx)
{
    aeob_validation_t* v = (aeob_validation_t*)ctx;
    aeob_param_t p;
    uint32_t pos = 0;
    if (same(a->kind, "CLOCK")) {
        char name[128];
        if (!next(a->arguments, a->argument_bytes, &pos, &p) ||
            !is_string(&p, name, sizeof(name)) || !lookup_clock(name)) {
            v->failure = AEO_B_RES_UNKNOWN_CLOCK;
            return 0;
        }
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
            goto malformed;

        while (pos < a->argument_bytes) {
            if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
                goto malformed;
        }
        ++v->clock_cnt;
        return 1;
    }
    if (same(a->kind, "BUSARB")) {
        char master[128], slave[128];
        unsigned master_id, slave_id;
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            !next(a->arguments, a->argument_bytes, &pos, &p) ||
            !is_string(&p, master, sizeof(master)) ||
            !next(a->arguments, a->argument_bytes, &pos, &p) ||
            !is_string(&p, slave, sizeof(slave)))
            goto malformed;
        if (!lookup_master(master, &master_id) || !lookup_slave(slave, &slave_id)) {
            v->failure = AEO_B_RES_UNKNOWN_ICB;
            return 0;
        }
        if (!lookup_icb_route(master_id, slave_id)) {
            v->failure = AEO_B_RES_UNKNOWN_ROUTE;
            return 0;
        }
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            !next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            pos != a->argument_bytes)
            goto malformed;
        ++v->bus_cnt;
        return 1;
    }
    if (same(a->kind, "TLMMGPIO")) {
        unsigned cnt = 0;
        while (pos < a->argument_bytes) {
            if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
                goto malformed;
            ++cnt;
        }
        if (cnt != 6u)
            goto malformed;
        ++v->gpio_cnt;
        return 1;
    }
    if (same(a->kind, "PMICGPIO")) {
        char method[128];
        unsigned cnt = 0;
        if (!next(a->arguments, a->argument_bytes, &pos, &p) ||
            !is_string(&p, method, sizeof(method)) ||
            !same(method, "IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT"))
            goto malformed;
        while (pos < a->argument_bytes) {
            if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
                goto malformed;
            ++cnt;
        }
        if (cnt != 7u)
            goto malformed;
        ++v->pmic_gpio_cnt;
        return 1;
    }
    if (same(a->kind, "PMICVREGVOTE")) {
        char name[128];
        unsigned cnt = 0, want;
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_string(&p, name, sizeof(name)))
            goto malformed;

        if (!lookup_ppp_name(name)) {
            v->failure = AEO_B_RES_UNKNOWN_ACTION;
            return 0;
        }
        /* a fixed count of five refused every PMIC clock-buf vote, which is why \_SB.UFS0
         * PSTATE_SET 1 was rejected as malformed and the whole UFS0 desc with it; only accepting
         * opcode 1 refused every SMPS vote, which refused all of \_SB.BTH0. */

        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
            goto malformed;
        switch (int_value(&p)) {
            case AEOB_VREG_OP_LDO:
                want = 5u;
                break;
            case AEOB_VREG_OP_SMPS:
                want = 5u;
                break;
            case AEOB_VREG_OP_CLOCK:
                want = 2u;
                break;
            default:
                goto malformed;
        }
        cnt = 1u;
        while (pos < a->argument_bytes) {
            if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
                goto malformed;
            ++cnt;
        }
        if (cnt != want)
            goto malformed;
        ++v->pmic_regulator_cnt;
        return 1;
    }
    if (same(a->kind, "PSTATE_ADJUST")) {
        unsigned cnt = 0;
        while (pos < a->argument_bytes) {
            if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p))
                goto malformed;
            ++cnt;
        }
        if (cnt != 2u)
            goto malformed;
        ++v->pstate_adjust_cnt;
        return 1;
    }
    if (same(a->kind, "NPARESOURCE")) {
        char path[128];
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            int_value(&p) != 1u)
            goto malformed;
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_string(&p, path, sizeof(path)))
            goto malformed;
        if (!lookup_npa_res(path)) {
            v->failure = AEO_B_RES_UNKNOWN_ACTION;
            return 0;
        }
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            int_value(&p) > UINT32_MAX || pos != a->argument_bytes)
            goto malformed;
        ++v->npa_res_cnt;
        return 1;
    }
    if (same(a->kind, "DELAY")) {
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            pos != a->argument_bytes)
            goto malformed;

        uint64_t ms = 0;
        for (unsigned i = 0; i < p.length; ++i)
            ms |= (uint64_t)p.data[i] << (i * 8u);
        if (ms > 5000u)
            goto malformed;
        ++v->delay_cnt;
        return 1;
    }
    v->failure = AEO_B_RES_UNKNOWN_KIND;
    return 0;
malformed:
    v->failure = AEO_B_RES_MALFORMED;
    return 0;
}

static int validate_comp_action(const aeob_action_t* a, void* ctx)
{
    aeob_validation_t* v = (aeob_validation_t*)ctx;
    aeob_param_t p;
    uint32_t pos = 0;
    if (same(a->kind, "FOOTSWITCH")) {
        char name[128];
        if (!next(a->arguments, a->argument_bytes, &pos, &p) ||
            !is_string(&p, name, sizeof(name)) || !lookup_gdsc(name)) {
            v->failure = AEO_B_RES_UNKNOWN_CLOCK;
            return 0;
        }
        if (!next(a->arguments, a->argument_bytes, &pos, &p) || !is_integer(&p) ||
            pos != a->argument_bytes) {
            v->failure = AEO_B_RES_MALFORMED;
            return 0;
        }
        ++v->footswitch_cnt;
        return 1;
    }
    return validate_action(a, ctx);
}

aeob_status_t aeo_b_validate_molokai_comp_f_state(const void* buf, size_t avail_bytes,
                                                  const char* acpi_path, uint32_t comp,
                                                  uint32_t fstate, aeob_validation_t* result)
{
    uint32_t cnt = 0;
    if (!result)
        return AEO_B_INVALID;
    memset(result, 0, sizeof(*result));
    return aeo_b_visit_comp_f_state(buf, avail_bytes, acpi_path, comp, fstate, validate_comp_action,
                                    result, &cnt);
}

aeob_status_t aeo_b_validate_molokai_comp_p_state(const void* buf, size_t avail_bytes,
                                                  const char* acpi_path, uint32_t comp,
                                                  uint32_t set_id, uint32_t pstate,
                                                  aeob_validation_t* result)
{
    uint32_t cnt = 0;
    if (!result)
        return AEO_B_INVALID;
    memset(result, 0, sizeof(*result));
    return aeo_b_visit_comp_p_state(buf, avail_bytes, acpi_path, comp, set_id, pstate,
                                    validate_comp_action, result, &cnt);
}

aeob_status_t aeo_b_validate_molokai_dev_d_state(const void* buf, size_t avail_bytes,
                                                 const char* acpi_path, uint32_t state,
                                                 aeob_validation_t* result)
{
    uint32_t cnt = 0;
    if (!result)
        return AEO_B_INVALID;
    memset(result, 0, sizeof(*result));
    return aeo_b_visit_dev_d_state(buf, avail_bytes, acpi_path, state, validate_action, result,
                                   &cnt);
}
