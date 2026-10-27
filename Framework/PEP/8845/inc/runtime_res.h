/* these descriptors have the same serialization as the PEP0 DSDT methods (DEVICE /
 * COMPONENT / FSTATE / PSTATE_SET / DSTATE packages), so they are validated with the same
 * catalog-backed per-action rules that Stages 16-28 established for the DSDT: unknown
 * clocks, ICB endpoints, routes, PPP names. */

#ifndef RUNTIME_RESOURCES_H
#define RUNTIME_RESOURCES_H

#include "aeob_validate.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIME_MAX_DEVICES 32u

typedef struct runtime_dev_info {
    char path[256];
    uint32_t comp_mask;
    uint32_t comp_f_states[32];
    uint32_t comp_pstate_sets[32];
    uint32_t crashdump_state;
    uint32_t f_state_actions;
    uint32_t p_state_actions;
    uint32_t d_state_actions;
    uint8_t has_d0;
    uint8_t has_d3;
    uint8_t d_state_mask;
    /* an unsupported device is recorded and reported, never executed: ownership still requires
     * the gate, and the owners' own admission (known / RouteMissingBcm / LookupClock) refuses
     * an unknown res at apply time regardless of what was recorded here. */

    uint8_t supported;
    uint8_t empty;
    uint32_t unsupported_status;
    uint32_t unsupported_failure;
    char unsupported_state[32];
} runtime_dev_info_t;

/* large (about 5 KiB): caller-owned NONPAGED heap in the driver, never a kernel stack
 * object. */

typedef struct runtime_desc_info {
    uint32_t dev_cnt;
    runtime_dev_info_t devices[RUNTIME_MAX_DEVICES];
    uint32_t supported_devices;
    uint32_t unsupported_devices;
    aeob_status_t status;
    aeob_res_failure_t failure;
    char fail_path[256];
    char fail_state[32];
    char path_scratch[RUNTIME_MAX_DEVICES][256];
} runtime_desc_info_t;

/* returns non-zero when the desc is STRUCTURALLY valid: the buf walks, the DEVICE paths
 * are well formed and unique, and every action has a known kind with the exact argument
 * shape that kind requires. */

int validate_runtime_desc(const void* buf, size_t bytes, runtime_desc_info_t* info);

#ifdef __cplusplus
}
#endif

#endif
