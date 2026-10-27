#ifndef OEM_SLEEPWAKE_DEPENDENCIES_H
#define OEM_SLEEPWAKE_DEPENDENCIES_H
/* non-AMC TCS CMD_WAIT_FOR_CMPL provenance guard. Qualcomm firmware accepts an externally
 * prepared per-command dependency bitmap. */

#include "oem_sleepwake_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct oem_sleepwake_dep_ops {
    void* ctx;
    /* must represent independently established OEM/firmware dependency policy; returning 1 is
     * not proof of Windows controller ownership. */

    int (*authorize)(void* ctx, uint32_t epoch, uint64_t token, uint8_t slot, uint8_t cnt,
                     uint16_t responses, uint16_t wait_mask, const tcs_command_t* commands);
} oem_sleepwake_dep_ops_t;

typedef struct oem_sleepwake_dependencies {
    oem_sleepwake_bridge_t* bridge;
    oem_sleepwake_dep_ops_t ops;
    oem_sleepwake_image_t snapshot;
    uint64_t token;
    uint16_t wait_mask[OEM_SLEEPWAKE_IMAGE_CNT];
    uint16_t frozen_mask[OEM_SLEEPWAKE_IMAGE_CNT];
    uint8_t sealed;
} oem_sleepwake_dependencies_t;

int oem_sleep_wake_deps_init(oem_sleepwake_dependencies_t*, oem_sleepwake_bridge_t*,
                             const oem_sleepwake_dep_ops_t*);
/* sealing is atomic: reject invalid/unauthorized masks without modifying the previous
 * state. */

int oem_sleep_wake_deps_seal(oem_sleepwake_dependencies_t*, uint64_t token,
                             const uint16_t masks[OEM_SLEEPWAKE_IMAGE_CNT]);
int oem_sleep_wake_deps_get(const oem_sleepwake_dependencies_t*, uint64_t token, unsigned image_idx,
                            uint16_t* out_mask);
#ifdef __cplusplus
}
#endif
#endif
