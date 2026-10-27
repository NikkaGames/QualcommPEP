#pragma once
/* firmware levels map to physical Hz in ascending SCMI order. The header has no WDK dependencies.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MOLOKAI_CPU_CNT 8u
#define MOLOKAI_DOMAIN_CNT 2u
#define MOLOKAI_MAX_LEVEL_CNT 25u

/* the firmware owns voltage transitions; these values are NOT regulator votes. */
typedef struct molokai_domain_model {
    unsigned int first_cpu;
    unsigned int cpu_cnt;
    unsigned int scmi_domain_id;
    unsigned int level_cnt;
    unsigned int capacity_dmips_mhz;
    unsigned int non_boost_max_level;
    unsigned int android_idle_floor_level;
    const unsigned int* freq_hz;
} molokai_domain_model_t;

const molokai_domain_model_t* molokai_domain(unsigned int domain);
unsigned int molokai_domain_for_cpu(unsigned int cpu);
unsigned int molokai_level_from_win_state(unsigned int domain, unsigned int state_idx,
                                          unsigned int enforce_android_floor);
unsigned int molokai_freq_from_win_state(unsigned int domain, unsigned int state_idx);
unsigned int molokai_normalized_performance(unsigned int domain, unsigned int freq_hz);

#ifdef __cplusplus
}
#endif
