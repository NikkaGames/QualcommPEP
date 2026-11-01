#ifndef DISPCC_H
#define DISPCC_H

#include "commit.h"

#define DISP_CRM_PHYS UINT64_C(0x09BC3000)
#define DISP_CRM_BYTES 0x9000u
#define DISP_CRM_COMMON_PHYS UINT64_C(0x09BCEE00)
#define DISP_CRM_COMMON_BYTES 0x200u

typedef struct dispcc {
    gcc_io_t clocks, crm, common;
    uint32_t irq_enable;
    uint32_t write_sequence;
    uint8_t quarantined;
} dispcc_t;

/* CESTA owns the MDP source and its voltage scaling. Requests use the display
 * CRM software bandwidth voter; this interface never reprograms its PLL. */
int dispcc_probe(dispcc_t*);
int dispcc_rate_group(const gcc_clock_t*);
int dispcc_read(dispcc_t*, const gcc_clock_t*, gcc_state_t*);
int dispcc_verify(dispcc_t*, const gcc_clock_t*, uint32_t minimum_hz);
int dispcc_rate_snapshot(dispcc_t*, uint64_t words[3]);
gcc_commit_result_t dispcc_rate_restore(dispcc_t*, const uint64_t words[3]);
gcc_commit_result_t dispcc_branch_restore(dispcc_t*, const gcc_clock_t*, int enable);
gcc_commit_result_t dispcc_commit(dispcc_t*, const gcc_clock_t*, uint32_t hz, int enable,
                                  int rate_only, int update_rate, uint32_t* programmed_hz);

#endif
