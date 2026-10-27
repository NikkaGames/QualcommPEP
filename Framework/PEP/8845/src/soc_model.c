#include "soc_model.h"

static const unsigned int s_domain0_hz[24] = {
    384000000u,  556800000u,  691200000u,  806400000u,  902400000u,  1036800000u,
    1152000000u, 1267200000u, 1401600000u, 1555200000u, 1651200000u, 1747200000u,
    1900800000u, 2016000000u, 2188800000u, 2265600000u, 2361600000u, 2476800000u,
    2611200000u, 2707200000u, 2841600000u, 3033600000u, 3129600000u, 3321600000u};

static const unsigned int s_domain1_hz[25] = {
    384000000u,  556800000u,  806400000u,  921600000u,  1017600000u, 1152000000u, 1248000000u,
    1382400000u, 1497600000u, 1651200000u, 1862400000u, 1996800000u, 2112000000u, 2227200000u,
    2476800000u, 2688000000u, 2841600000u, 2995200000u, 3091200000u, 3244800000u, 3436800000u,
    3532800000u, 3648000000u, 3724800000u, 3801600000u};

static const molokai_domain_model_t s_domains[2] = {
    {0u, 6u, 0u, 24u, 1024u, 21u, 1u, s_domain0_hz},
    {6u, 2u, 1u, 25u, 1208u, 20u, 2u, s_domain1_hz}};

const molokai_domain_model_t* molokai_domain(unsigned int domain)
{
    return domain < MOLOKAI_DOMAIN_CNT ? &s_domains[domain] : 0;
}

unsigned int molokai_domain_for_cpu(unsigned int cpu)
{
    return cpu < 6u ? 0u : (cpu < MOLOKAI_CPU_CNT ? 1u : MOLOKAI_DOMAIN_CNT);
}

unsigned int molokai_level_from_win_state(unsigned int domain, unsigned int state_idx,
                                          unsigned int enforce_android_floor)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    unsigned int last, level;
    if (!d)
        return 0u;
    /* CPU writes remain disabled until a transport owner is established. */

    last = d->level_cnt - 1u;
    if (state_idx > last)
        state_idx = last;
    level = last - state_idx;

    if (enforce_android_floor && level < d->android_idle_floor_level)
        level = d->android_idle_floor_level;
    return level;
}

unsigned int molokai_freq_from_win_state(unsigned int domain, unsigned int state_idx)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    return d ? d->freq_hz[molokai_level_from_win_state(domain, state_idx, 0u)] : 0u;
}

unsigned int molokai_normalized_performance(unsigned int domain, unsigned int freq_hz)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    unsigned int mhz;
    if (!d)
        return 0u;
    /* work entirely in 64-bit for the Prime cluster: 3802 MHz * 1208 overflows uint32. */

    mhz = (freq_hz + 500000u) / 1000000u;
    return (unsigned int)(((unsigned long long)mhz * d->capacity_dmips_mhz + 512u) / 1024u);
}
