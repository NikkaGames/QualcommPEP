#include "bcm.h"
#include <string.h>
#include <stdint.h>

int bcm_decode_aux(const uint8_t* aux, size_t bytes, bcm_aux_t* out)
{
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!aux || bytes < 7u)
        return 0;
    out->bandwidth_unit = (uint32_t)aux[0] | ((uint32_t)aux[1] << 8u) | ((uint32_t)aux[2] << 16u) |
                          ((uint32_t)aux[3] << 24u);
    out->port = (uint16_t)(aux[4] | ((uint16_t)aux[5] << 8u));
    out->clock_id = aux[6];

    return 1;
}

static int checked_ceil_scale(uint64_t value, uint64_t mul, uint64_t div, uint64_t* result)
{
    uint64_t q, r, whole, fraction;
    if (!result || !div || !mul)
        return 0;
    q = value / div;
    r = value % div;
    if (q > UINT64_MAX / mul)
        return 0;
    whole = q * mul;
    if (r > UINT64_MAX / mul)
        return 0;
    fraction = r * mul / div;
    if ((r * mul) % div) {
        if (fraction == UINT64_MAX)
            return 0;
        ++fraction;
    }
    if (whole > UINT64_MAX - fraction)
        return 0;
    *result = whole + fraction;
    return 1;
}

int bcm_scale_req(uint64_t required_ab, uint64_t required_ib, uint64_t suppressible_ab,
                  uint64_t suppressible_ib, uint64_t bcm_width, uint64_t node_width,
                  uint32_t node_ports, bcm_bandwidth_t* out)
{
    uint64_t ports_width, total_ab;
    bcm_bandwidth_t tmp;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    memset(&tmp, 0, sizeof(tmp));
    if (!bcm_width || !node_width || !node_ports || node_width > UINT64_MAX / node_ports)
        return 0;
    ports_width = node_width * node_ports;
    if (!checked_ceil_scale(required_ib, bcm_width, node_width, &tmp.peak) ||
        !checked_ceil_scale(suppressible_ib, bcm_width, node_width, &tmp.suppressible_peak) ||
        !checked_ceil_scale(required_ab, bcm_width, ports_width, &tmp.average) ||
        !checked_ceil_scale(suppressible_ab, bcm_width, ports_width, &tmp.suppressible_average))
        return 0;
    if (tmp.average > UINT64_MAX - tmp.suppressible_average)
        return 0;
    total_ab = tmp.average + tmp.suppressible_average;
    tmp.total = tmp.peak > tmp.suppressible_peak ? tmp.peak : tmp.suppressible_peak;
    if (tmp.total < total_ab)
        tmp.total = total_ab;
    *out = tmp;
    return 1;
}
