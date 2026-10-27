

#include "runtime_res.h"
#include <string.h>

static uint32_t total(const aeob_validation_t* v)
{
    return v->clock_cnt + v->bus_cnt + v->gpio_cnt + v->pmic_gpio_cnt + v->pmic_regulator_cnt +
           v->delay_cnt + v->pstate_adjust_cnt + v->footswitch_cnt + v->npa_res_cnt;
}

static int fail(runtime_desc_info_t* info, aeob_status_t st, aeob_res_failure_t failure,
                const char* path, const char* state)
{
    info->status = (st == AEO_B_OK) ? AEO_B_INVALID : st;
    info->failure = failure;
    info->fail_path[0] = '\0';
    info->fail_state[0] = '\0';
    if (path) {
        size_t n = strlen(path);
        if (n >= sizeof(info->fail_path))
            n = sizeof(info->fail_path) - 1u;
        memcpy(info->fail_path, path, n);
        info->fail_path[n] = '\0';
    }
    if (state) {
        size_t n = strlen(state);
        if (n >= sizeof(info->fail_state))
            n = sizeof(info->fail_state) - 1u;
        memcpy(info->fail_state, state, n);
        info->fail_state[n] = '\0';
    }
    return 0;
}

static int res_gap(aeob_status_t st, aeob_res_failure_t f)
{
    if (st != AEO_B_CALLBACK_ERROR)
        return 0;
    return f == AEO_B_RES_UNKNOWN_ACTION || f == AEO_B_RES_UNKNOWN_CLOCK ||
           f == AEO_B_RES_UNKNOWN_ICB || f == AEO_B_RES_UNKNOWN_ROUTE;
}

static void mark_unsupported(runtime_dev_info_t* dev, aeob_status_t st, aeob_res_failure_t f,
                             const char* state)
{
    if (!dev->supported)
        return;
    dev->supported = 0u;
    dev->unsupported_status = (uint32_t)st;
    dev->unsupported_failure = (uint32_t)f;
    if (state) {
        size_t n = strlen(state);
        if (n >= sizeof(dev->unsupported_state))
            n = sizeof(dev->unsupported_state) - 1u;
        memcpy(dev->unsupported_state, state, n);
        dev->unsupported_state[n] = '\0';
    }
}

static void state_name(char out[32], const char* kind, uint32_t a, uint32_t b, int two)
{

    size_t n = strlen(kind), i = 0;
    char num[12];
    int k;
    if (n > 20u)
        n = 20u;
    memcpy(out, kind, n);
    i = n;
    out[i++] = ' ';
    k = 0;
    do {
        num[k++] = (char)('0' + a % 10u);
        a /= 10u;
    } while (a && k < 11);
    while (k)
        out[i++] = num[--k];
    if (two) {
        out[i++] = '/';
        k = 0;
        do {
            num[k++] = (char)('0' + b % 10u);
            b /= 10u;
        } while (b && k < 11);
        while (k)
            out[i++] = num[--k];
    }
    out[i] = '\0';
}

int validate_runtime_desc(const void* buf, size_t bytes, runtime_desc_info_t* info)
{
    aeob_status_t st;
    uint32_t top = 0, n = 0, i, c, s, p, d;
    char state[32];

    if (!info)
        return 0;
    memset(info, 0, sizeof(*info));
    info->status = AEO_B_INVALID;
    if (!buf || bytes < 12u || bytes > AEOB_MAX_SIZE)
        return fail(info, AEO_B_INVALID, AEO_B_RES_MALFORMED, 0, "HEADER");

    st = aeo_b_walk(buf, bytes, 0, 0, &top);
    if (st != AEO_B_OK || top == 0u)
        return fail(info, st, AEO_B_RES_MALFORMED, 0, "WALK");

    st = aeo_b_list_dev_paths(buf, bytes, (char*)info->path_scratch, RUNTIME_MAX_DEVICES, &n);
    if (st != AEO_B_OK || n == 0u || n > RUNTIME_MAX_DEVICES)
        return fail(info, st, AEO_B_RES_MALFORMED, 0, "DEVICE");
    info->dev_cnt = n;

    for (i = 0; i < n; ++i) {
        runtime_dev_info_t* dev = &info->devices[i];
        const char* path = info->path_scratch[i];
        aeob_validation_t v;
        int any = 0;
        memcpy(dev->path, path, 256u);
        dev->crashdump_state = 0xFFFFFFFFu;
        dev->supported = 1u;

        st = aeo_b_inventory_dev_components(buf, bytes, path, &dev->comp_mask, dev->comp_f_states,
                                            dev->comp_pstate_sets, &dev->crashdump_state);
        if (st != AEO_B_OK)
            return fail(info, st, AEO_B_RES_MALFORMED, path, "COMPONENT");

        for (c = 0; c < 32u; ++c) {
            if (!(dev->comp_mask & (1u << c)))
                continue;
            any = 1;
            /* every advertised F-state must validate action by action. */
            for (s = 0; s < 32u; ++s) {
                if (!(dev->comp_f_states[c] & (1u << s)))
                    continue;
                st = aeo_b_validate_molokai_comp_f_state(buf, bytes, path, c, s, &v);
                if (st != AEO_B_OK) {
                    state_name(state, "FSTATE", s, 0, 0);
                    if (!res_gap(st, v.failure))
                        return fail(info, st, v.failure, path, state);
                    mark_unsupported(dev, st, v.failure, state);
                    continue;
                }
                dev->f_state_actions += total(&v);
            }
            /* every advertised PSTATE_SET must hold at least PSTATE 0 and every present PSTATE must
             * validate. */

            for (s = 0; s < 32u; ++s) {
                if (!(dev->comp_pstate_sets[c] & (1u << s)))
                    continue;
                for (p = 0; p < 32u; ++p) {
                    st = aeo_b_validate_molokai_comp_p_state(buf, bytes, path, c, s, p, &v);
                    if (st == AEO_B_NOT_FOUND) {
                        if (p == 0u) {
                            state_name(state, "PSTATE_SET", s, 0, 0);
                            return fail(info, st, AEO_B_RES_MALFORMED, path, state);
                        }
                        break;
                    }
                    if (st != AEO_B_OK) {
                        state_name(state, "PSTATE_SET", s, p, 1);
                        if (!res_gap(st, v.failure))
                            return fail(info, st, v.failure, path, state);
                        mark_unsupported(dev, st, v.failure, state);
                        continue;
                    }
                    dev->p_state_actions += total(&v);
                }
            }
        }

        /* missing d1/d2 is normal; present intermediate states must not be discarded or silently
         * treated as d3 by the native PEP. */

        for (d = 0; d <= 3u; ++d) {
            st = aeo_b_validate_molokai_dev_d_state(buf, bytes, path, d, &v);
            if (st == AEO_B_NOT_FOUND)
                continue;
            if (st != AEO_B_OK) {
                state_name(state, "DSTATE", d, 0, 0);
                if (!res_gap(st, v.failure))
                    return fail(info, st, v.failure, path, state);
                mark_unsupported(dev, st, v.failure, state);
                any = 1;
                dev->d_state_mask |= (uint8_t)(1u << d);
                if (d == 0u)
                    dev->has_d0 = 1u;
                if (d == 3u)
                    dev->has_d3 = 1u;
                continue;
            }
            any = 1;
            dev->d_state_mask |= (uint8_t)(1u << d);
            if (d == 0u)
                dev->has_d0 = 1u;
            if (d == 3u)
                dev->has_d3 = 1u;
            dev->d_state_actions += total(&v);
        }

        if (!any)
            dev->empty = 1u;
        if (dev->supported)
            ++info->supported_devices;
        else
            ++info->unsupported_devices;
    }

    info->status = AEO_B_OK;
    info->failure = AEO_B_RES_NONE;
    return 1;
}
