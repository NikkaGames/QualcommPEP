#ifndef OEM_SLEEPWAKE_PROGRAM_H
#define OEM_SLEEPWAKE_PROGRAM_H
/* non-AMC TCS programmer for a previously armed image. Requires an independently proven,
 * exclusive Windows ownership lease for slots 3/4/5/6 and a separately quiesced HLOS RPMh
 * controller. */

#include "oem_sleepwake_bridge.h"
#include "oem_sleepwake_dependencies.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct oem_sleepwake_program_ops {
    void* ctx;
    int (*exclusive_owner)(void*, uint32_t epoch, uint64_t token, uint8_t slot);
    int (*quiesced)(void*, uint32_t epoch, uint64_t token);
    int (*read32)(void*, uint32_t offset, uint32_t* out);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    /* this callback must come from an independent, trusted firmware/OS integration and must
     * prove the non-AMC image can be released after confirmed resume. Software readbacks
     * cannot prove that firmware used it. */

    int (*firmware_released)(void*, uint32_t epoch, uint64_t token);
} oem_sleepwake_program_ops_t;

typedef enum oem_sleepwake_program_state {
    SLEEP_WAKE_PROGRAM_IDLE = 0,
    SLEEP_WAKE_PROGRAM_COMMITTED = 1,
    SLEEP_WAKE_PROGRAM_QUARANTINED = 2,
    SLEEP_WAKE_PROGRAM_RETIRED = 3
} oem_sleepwake_program_state_t;

typedef struct oem_sleepwake_program {
    oem_sleepwake_program_ops_t ops;
    oem_sleepwake_bridge_t* bridge;

    const oem_sleepwake_dependencies_t* dependencies;
    uint64_t token;
    uint32_t epoch;
    uint8_t slot_mask;
    uint8_t closed;
    oem_sleepwake_program_state_t state;
} oem_sleepwake_program_t;

int oem_sleep_wake_program_init(oem_sleepwake_program_t*, oem_sleepwake_bridge_t*,
                                const oem_sleepwake_program_ops_t*);
/* precheck all nonempty TCSs before any writes. image must be armed; each slot must be
 * independently owned, idle, in NON-AMC mode and unused. */

int oem_sleep_wake_program_attach_dependencies(oem_sleepwake_program_t*,
                                               const oem_sleepwake_dependencies_t*);
int oem_sleep_wake_program_commit(oem_sleepwake_program_t*, uint64_t token);

int oem_sleep_wake_program_verify(oem_sleepwake_program_t*, uint64_t token);
/* only after independently proven firmware release AND successful resume. A readback of
 * command registers alone never allows retirement. */

int oem_sleep_wake_program_retire(oem_sleepwake_program_t*, uint64_t token);
void oem_sleep_wake_program_close(oem_sleepwake_program_t*);
int oem_sleep_wake_program_can_release(const oem_sleepwake_program_t*);
#ifdef __cplusplus
}
#endif
#endif
