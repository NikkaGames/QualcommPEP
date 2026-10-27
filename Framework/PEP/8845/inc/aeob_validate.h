#ifndef AEOB_VALIDATE_H
#define AEOB_VALIDATE_H
#include "aeob.h"
#ifdef __cplusplus
extern "C" {
#endif
/* no MMIO, RPMh, GPIO, or ACPI state transitions are performed. Unknown actions and
 * references FAIL CLOSED, never convert to success. */

/* the first DWORD after the PPP res name is the vote kind - which is the rail's own kind -
 * and it fixes the argument count: 1 -> LDO [1, microvolts, enable, mode, headroom] 2 ->
 * SMPS [2, microvolts, enable, mode, headroom] 6 -> PMIC clock buf [6, enable] All three
 * ship on this device. */

#define AEOB_VREG_OP_LDO 1u
#define AEOB_VREG_OP_SMPS 2u
#define AEOB_VREG_OP_CLOCK 6u

#define AEOB_VREG_OP_REGULATOR AEOB_VREG_OP_LDO

typedef enum aeob_res_failure {
    AEO_B_RES_NONE = 0,
    AEO_B_RES_MALFORMED = 1,
    AEO_B_RES_UNKNOWN_ACTION = 2,
    AEO_B_RES_UNKNOWN_CLOCK = 3,
    AEO_B_RES_UNKNOWN_ICB = 4,
    AEO_B_RES_UNKNOWN_ROUTE = 5,
    /* unknown grammar and unknown resources have different admission failures. */

    AEO_B_RES_UNKNOWN_KIND = 6
} aeob_res_failure_t;

typedef struct aeob_validation {
    uint32_t clock_cnt;
    uint32_t bus_cnt;
    uint32_t gpio_cnt;
    uint32_t pmic_gpio_cnt;
    uint32_t pmic_regulator_cnt;
    uint32_t delay_cnt;
    uint32_t pstate_adjust_cnt;
    aeob_res_failure_t failure;
    uint32_t footswitch_cnt;
    uint32_t npa_res_cnt;
} aeob_validation_t;

aeob_status_t aeo_b_validate_molokai_dev_d_state(const void* buf, size_t avail_bytes,
                                                 const char* acpi_path, uint32_t state,
                                                 aeob_validation_t* result);
/* read-only; no provider is implied. */

aeob_status_t aeo_b_validate_molokai_comp_f_state(const void* buf, size_t avail_bytes,
                                                  const char* acpi_path, uint32_t comp,
                                                  uint32_t f_state, aeob_validation_t* result);
aeob_status_t aeo_b_validate_molokai_comp_p_state(const void* buf, size_t avail_bytes,
                                                  const char* acpi_path, uint32_t comp,
                                                  uint32_t set_id, uint32_t p_state,
                                                  aeob_validation_t* result);
#ifdef __cplusplus
}
#endif
#endif
