#ifndef OEM_RSC_TCS_SURVEY_H
#define OEM_RSC_TCS_SURVEY_H
/* non-destructive, independently authorized RPMh RSC/TCS status census. This does not
 * attest ownership, issue commands, clear IRQs or permit TCS writes. */

#include "oem_rsc_readback.h"
#include "rpmh_tcs.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_RSC_SURVEY_VERSION 1u
#define OEM_RSC_SURVEY_TCS_CNT MOLOKAI_WIN_TCS_TOTAL

typedef struct oem_rsc_tcs_state {
    uint32_t control;
    uint32_t status;
    uint32_t command_enable;
    uint32_t wait_for_completion;
} oem_rsc_tcs_state_t;

typedef struct oem_rsc_tcs_survey {
    uint32_t version;
    uint32_t rsc_id;
    uint32_t parent_child_config;
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t tcs_cnt;
    uint32_t commands_per_tcs;
    uint32_t controller_id_matches_firmware;
    uint32_t geometry_meets_firmware;
    uint32_t not_idle_mask;
    uint32_t configured_mask;
    uint32_t amc_mode_or_trigger_mask;
    uint32_t pending_irq_mask;
    uint32_t quiescent_observation; /* never an ownership or write grant. */
    uint64_t owner_epoch;
    oem_rsc_tcs_state_t tcs[OEM_RSC_SURVEY_TCS_CNT];
} oem_rsc_tcs_survey_t;

/* two full independently sourced MMIO scans are required to match exactly. Unstable
 * snapshots, I/O faults and lost ownership quarantine the mapping; unsupported controller
 * geometry reports failure without probing TCS slots. */

int oem_rsc_tcs_survey(oem_direct_rsc_map_t* map, uint64_t epoch, const oem_rsc_read_io_t* io,
                       oem_rsc_tcs_survey_t* out);
#ifdef __cplusplus
}
#endif
#endif
