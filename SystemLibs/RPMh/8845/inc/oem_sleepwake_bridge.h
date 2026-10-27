#ifndef OEM_SLEEPWAKE_BRIDGE_H
#define OEM_SLEEPWAKE_BRIDGE_H
/* construct sleep/wake TCS images without programming hardware or handing off to firmware. */

#include "oem_voter_state.h"
#include "oem_voter_sets.h"
#include "resolve.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_SLEEP_TCS MOLOKAI_WIN_SLEEP_CNT
#define OEM_WAKE_TCS MOLOKAI_WIN_WAKE_CNT
#define OEM_SLEEPWAKE_IMAGE_CNT (OEM_SLEEP_TCS + OEM_WAKE_TCS)

typedef struct oem_sleepwake_tcs_image {
    uint8_t slot;
    uint8_t cnt;

    uint16_t response_mask;
    tcs_command_t commands[TCS_BATCH_MAX];
} oem_sleepwake_tcs_image_t;

typedef struct oem_sleepwake_image {
    uint32_t epoch;
    uint32_t revision;
    uint32_t sleep_mask;
    uint32_t wake_mask;
    oem_sleepwake_tcs_image_t sleep[OEM_SLEEP_TCS];
    oem_sleepwake_tcs_image_t wake[OEM_WAKE_TCS];
} oem_sleepwake_image_t;

typedef struct oem_sleepwake_bridge {
    oem_voter_state_t* voter;
    oem_voteset_state_t* sets;
    oem_cmddb_resolver_t* db;
    oem_sleepwake_image_t image;
    uint64_t ticket;
    uint8_t faulted;
    uint8_t closed;
    /* set before first possible MMIO write; forbids an unstarted rollback after a partial or
     * completed non-AMC TCS program. */

    uint8_t hardware_started;
} oem_sleepwake_bridge_t;

int oem_sleep_wake_init(oem_sleepwake_bridge_t*, oem_voter_state_t*, oem_voteset_state_t*,
                        oem_cmddb_resolver_t*);
/* only constructs images, never arms or writes hardware. */

int oem_sleep_wake_build(oem_sleepwake_bridge_t*, oem_sleepwake_image_t*);
/* freeze the image under the shared lock and bind its resume token. */
int oem_sleep_wake_arm(oem_sleepwake_bridge_t*, uint64_t*);
/* commit only after provider resume_proof and double HW readbacks. */

int oem_sleep_wake_commit_resume(oem_sleepwake_bridge_t*, uint64_t);
/* only safe with external attestation no firmware side effect could start. */
int oem_sleep_wake_cancel_unstarted(oem_sleepwake_bridge_t*, uint64_t);

int oem_sleep_wake_ready(void*, uint32_t, uint32_t, uint8_t);
void oem_sleep_wake_close(oem_sleepwake_bridge_t*);
int oem_sleep_wake_can_release(const oem_sleepwake_bridge_t*);
#ifdef __cplusplus
}
#endif
#endif
