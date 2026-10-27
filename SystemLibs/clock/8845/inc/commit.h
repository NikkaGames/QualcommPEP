#ifndef GCC_CLOCK_COMMIT_H
#define GCC_CLOCK_COMMIT_H
#include "gcc.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum gcc_commit_result {
    GCC_COMMIT_UNCERTAIN = -1, /* mmio may have changed: retire owner. */
    GCC_COMMIT_DECLINED = 0,
    GCC_COMMIT_OK = 1 /* hardware register readback verified. */
} gcc_commit_result_t;

/* stage numbers identify the last attempted operation, and write_started distinguishes a
 * declined preflight from uncertain hardware after a possible mutation. */

typedef struct gcc_commit_diag {
    uint32_t stage;
    uint32_t write_started;
    uint32_t post_poll_cnt; /* physical MMIO verification samples. */
    gcc_state_t before1, before2, after1, after2;
} gcc_commit_diag_t;

/* clock_EnableClockEx: subsequent clients share the first physical enable. This read-only
 * operation re-attests the existing client vote without making a redundant GCC write. */

gcc_commit_result_t gcc_verify_retained_vote(const gcc_io_t* io, const gcc_clock_t* clock,
                                             uint32_t programmed_hz);

/* rate requests do not acquire an enable vote. Preserve the observed branch and vote
 * registers, including when its peripheral is in Dx. */
gcc_commit_result_t gcc_commit_freq_ex(const gcc_io_t* io, const gcc_clock_t* clock,
                                       uint32_t requested_hz, uint32_t* programmed_hz,
                                       gcc_commit_diag_t* diag);

gcc_commit_result_t gcc_commit_branch_vote_ex(const gcc_io_t* io, const gcc_clock_t* clock,
                                              uint32_t requested_hz, int enable,
                                              uint32_t* programmed_hz, gcc_commit_diag_t* diag);
gcc_commit_result_t gcc_commit_branch_vote(const gcc_io_t* io, const gcc_clock_t* clock,
                                           uint32_t requested_hz, int enable,
                                           uint32_t* programmed_hz);
#ifdef __cplusplus
}
#endif
#endif
