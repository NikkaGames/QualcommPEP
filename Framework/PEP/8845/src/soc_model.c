#include "soc_model.h"
#include "perf_target.h"
#include <string.h>
#include <stdint.h>

static molokai_domain_model_t domains[MOLOKAI_DOMAIN_CNT];
static unsigned int frequencies[MOLOKAI_DOMAIN_CNT][MOLOKAI_MAX_LEVEL_CNT];
static unsigned int cpu_domains[MOLOKAI_CPU_CNT];
static unsigned int cpu_cores[MOLOKAI_CPU_CNT];

void molokai_model_reset(void)
{
    memset(domains, 0, sizeof(domains));
    memset(frequencies, 0, sizeof(frequencies));
    memset(cpu_domains, 0xff, sizeof(cpu_domains));
    memset(cpu_cores, 0xff, sizeof(cpu_cores));
}

int molokai_model_bind(unsigned int domain, const scmi_perf_domain_t* firmware)
{
    molokai_domain_model_t* model;
    unsigned int i;
    if (domain >= MOLOKAI_DOMAIN_CNT || !firmware || !firmware->valid || firmware->id != domain ||
        firmware->level_count < 2u || firmware->level_count > MOLOKAI_MAX_LEVEL_CNT ||
        domains[domain].level_cnt)
        return 0;
    for (i = 0u; i < firmware->level_count; ++i) {
        uint64_t value =
            ((uint64_t)firmware->levels[i].frequency_hz * perf_target_capacity(domain) +
             512000000u) /
            UINT64_C(1024000000);
        uint64_t previous =
            i ? ((uint64_t)firmware->levels[i - 1u].frequency_hz * perf_target_capacity(domain) +
                 512000000u) /
                    UINT64_C(1024000000)
              : 0u;
        if (!value || value > UINT32_MAX || value <= previous)
            return 0;
    }
    model = &domains[domain];
    model->scmi_domain_id = domain;
    model->level_cnt = firmware->level_count;
    model->capacity_dmips_mhz = perf_target_capacity(domain);
    model->freq_hz = frequencies[domain];
    for (i = 0u; i < firmware->level_count; ++i) {
        frequencies[domain][i] = firmware->levels[i].frequency_hz;
        if (firmware->levels[i].frequency_hz <= firmware->sustained_hz)
            model->non_boost_max_level = i;
    }
    return model->capacity_dmips_mhz != 0u;
}

int molokai_model_bind_cpu(unsigned int cpu, unsigned int domain, unsigned int core)
{
    unsigned int i;
    if (cpu >= MOLOKAI_CPU_CNT || domain >= MOLOKAI_DOMAIN_CNT || !domains[domain].level_cnt ||
        cpu_domains[cpu] != UINT32_MAX)
        return 0;
    for (i = 0u; i < MOLOKAI_CPU_CNT; ++i)
        if (cpu_domains[i] == domain && cpu_cores[i] == core)
            return 0;
    cpu_domains[cpu] = domain;
    cpu_cores[cpu] = core;
    ++domains[domain].cpu_cnt;
    return 1;
}

const molokai_domain_model_t* molokai_domain(unsigned int domain)
{
    return domain < MOLOKAI_DOMAIN_CNT && domains[domain].level_cnt ? &domains[domain] : NULL;
}

unsigned int molokai_domain_for_cpu(unsigned int cpu)
{
    return cpu < MOLOKAI_CPU_CNT ? cpu_domains[cpu] : MOLOKAI_DOMAIN_CNT;
}

unsigned int molokai_core_for_cpu(unsigned int cpu)
{
    return cpu < MOLOKAI_CPU_CNT ? cpu_cores[cpu] : UINT32_MAX;
}

unsigned int molokai_level_from_win_state(unsigned int domain, unsigned int state)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    return d && state < d->level_cnt ? d->level_cnt - 1u - state : UINT32_MAX;
}

unsigned int molokai_freq_from_win_state(unsigned int domain, unsigned int state)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    unsigned int level = molokai_level_from_win_state(domain, state);
    return d && level < d->level_cnt ? d->freq_hz[level] : 0u;
}

unsigned int molokai_normalized_performance(unsigned int domain, unsigned int frequency)
{
    const molokai_domain_model_t* d = molokai_domain(domain);
    return d ? (unsigned int)(((uint64_t)frequency * d->capacity_dmips_mhz + 512000000u) /
                              UINT64_C(1024000000))
             : 0u;
}
