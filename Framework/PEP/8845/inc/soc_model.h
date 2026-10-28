#pragma once
/* Firmware-discovered CPU performance domains and verified processor affinity. */

#include "perf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOLOKAI_CPU_CNT 8u
#define MOLOKAI_DOMAIN_CNT 2u
#define MOLOKAI_MAX_LEVEL_CNT SCMI_PERF_MAX_LEVELS

/* Firmware operating points and platform policy limits. */
typedef struct molokai_domain_model {
    unsigned int cpu_cnt;
    unsigned int scmi_domain_id;
    unsigned int level_cnt;
    unsigned int minimum_level;
    unsigned int capacity_dmips_mhz;
    unsigned int non_boost_max_level;
    const unsigned int* freq_hz;
} molokai_domain_model_t;

const molokai_domain_model_t* molokai_domain(unsigned int domain);
void molokai_model_reset(void);
int molokai_model_bind(unsigned int domain, const scmi_perf_domain_t*);
int molokai_model_bind_cpu(unsigned int cpu, unsigned int domain, unsigned int core);
unsigned int molokai_core_for_cpu(unsigned int cpu);
unsigned int molokai_domain_for_cpu(unsigned int cpu);
unsigned int molokai_level_from_win_state(unsigned int domain, unsigned int state_idx);
unsigned int molokai_freq_from_win_state(unsigned int domain, unsigned int state_idx);
unsigned int molokai_normalized_performance(unsigned int domain, unsigned int freq_hz);

#ifdef __cplusplus
}
#endif
