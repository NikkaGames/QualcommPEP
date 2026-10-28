#ifndef CLOCK_TARGET_H
#define CLOCK_TARGET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct clock_config_rule {
    const char* name;
    uint32_t selector;
    uint32_t offset;
    uint32_t mask;
} clock_config_rule_t;

extern const char clock_ufs_domain[];
extern const char clock_ufs_ice[];
extern const char clock_ufs_enable[];

const clock_config_rule_t* clock_config_rule(const char* name, uint32_t selector);
const char* clock_probe_name(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif
