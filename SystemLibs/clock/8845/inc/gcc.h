#ifndef GCC_CLOCK_H
#define GCC_CLOCK_H
/* CBCR bit 0 enables software branches; bit 31 reports CLK_OFF and bit 1 is HW_CTL.
 * APCS-votable branches use their shared vote register. Request-register votes require
 * their own readback, not downstream CLK_OFF. Offsets come from the Molokai Clock BSP. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GCC_WINDOW_PHYS UINT64_C(0x00100000)
/* keep the BSP register sequence, but do not allow its MMIO adapter to reach an unassigned
 * neighbor range. */

#define GCC_WINDOW_BYTES 0x001F4200u

typedef struct gcc_freq {
    uint32_t hz;
    uint32_t mux_sel;
    uint32_t div2x;
    uint32_t m;
    uint32_t n;
    uint32_t dfs_idx; /* bsp nDFSIdx; 0xff is an ordinary-RCG-only rate. */
    uint32_t src_kind;
} gcc_freq_t;

#define GCC_SRC_XO 0u
#define GCC_SRC_GPLL0 1u
#define GCC_SRC_GPLL4 2u /* firmware-defined UFS 403 MHz src. */
#define GCC_SRC_OTHER 3u /* never selected by this core. */

typedef struct gcc_domain {
    const char* name;
    uint32_t cmd_rcgr_offset;
    uint32_t mnd_width;
    uint32_t freq_first;
    uint32_t freq_cnt;
    uint32_t flags;
    uint32_t dfs_states; /* firmware-described DFS register bank, zero otherwise. */
} gcc_domain_t;

#define GCC_DOMAIN_READ_ONLY 1u
#define GCC_DOMAIN_NO_RCG 2u
#define GCC_DOMAIN_MUX_ONLY                                                                        \
    4u /* plain mux domain (vendor DomainMuxControl): never reprogrammed.                          \
        */

#define GCC_KIND_BRANCH 1u
#define GCC_KIND_VOTE 2u
#define GCC_KIND_GDSC 3u
/* hlos request registers expose a retained vote; CLK_OFF is downstream status and is not
 * the acknowledgement of this owner's request bit. */
#define GCC_KIND_REQ 4u

typedef struct gcc_clock {
    const char* name;
    uint32_t kind;
    uint32_t cbcr_offset;
    uint32_t vote_offset;
    uint32_t vote_mask;
    int32_t domain_idx;
    uint32_t gdscr_offset;
    uint32_t bcr_offset; /* informational only; never written. */
} gcc_clock_t;

extern const gcc_freq_t gcc_freqs[];
extern const unsigned int gcc_freq_cnt;
extern const gcc_domain_t gcc_domains[];
extern const unsigned int gcc_domain_cnt;
extern const gcc_clock_t gcc_clocks[];
extern const unsigned int gcc_clock_cnt;
extern const unsigned int gcc_gpll0_mode_offset;
extern const unsigned int gcc_gpll4_mode_offset;
extern const unsigned int gcc_apcs_pll_vote_offset;

#define CBCR_CLK_ENABLE 0x00000001u
#define CBCR_CLK_HW_CTL 0x00000002u
#define CBCR_CLK_OFF_MASK 0xF0000000u
#define CBCR_CLK_OFF_SHIFT 28u
#define CMD_RCGR_UPDATE 0x00000001u
#define CMD_RCGR_ROOT_EN 0x00000002u
#define CMD_RCGR_ROOT_OFF 0x80000000u
#define CFG_RCGR_SRC_DIV 0x0000001Fu
#define CFG_RCGR_SRC_SEL 0x00000700u
#define CFG_RCGR_SRC_SHIFT 8u
#define CFG_RCGR_MODE 0x00003000u
#define CFG_RCGR_MODE_DUAL 0x00002000u
#define CFG_RCGR_HW_CLK_CTL 0x00100000u
#define GDSCR_SW_COLLAPSE 0x00000001u
#define GDSCR_PWR_ON 0x80000000u
#define PLL_MODE_OUTCTRL 0x00000001u
#define PLL_MODE_LOCK_DET 0x80000000u
#define RCG_CFG_OFFSET 0x4u
#define RCG_M_OFFSET 0x8u
#define RCG_N_OFFSET 0xCu
#define RCG_D_OFFSET 0x10u

typedef struct gcc_io {
    void* ctx;
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    void (*stall_microseconds)(void*, uint32_t microseconds);
} gcc_io_t;

#define GCC_DFS_MAX_STATES 16u

typedef struct gcc_dfs_catalog {
    uint32_t enabled;
    uint32_t cnt;
    uint32_t hertz[GCC_DFS_MAX_STATES];
    uint64_t fingerprint;
} gcc_dfs_catalog_t;

/* read-only discovery. A disabled bank returns Enabled=0. Advertise only the programmed,
 * increasing prefix before a reset-valued slot; never compact a sparse bank. Consumers use
 * these array indices directly in GENI hardware. Unstable or unrecognized advertised slots
 * fail without static fallback. */
int gcc_read_dfs_catalog(const gcc_io_t* io, const gcc_clock_t* clock, gcc_dfs_catalog_t* out);

typedef enum gcc_result {
    GCC_OK = 0,
    GCC_INVALID,
    GCC_UNKNOWN_CLOCK,
    GCC_IO_FAILED,
    GCC_TIMEOUT,
    GCC_NO_FREQ,
    GCC_SRC_OFF,
    GCC_READ_ONLY,
    GCC_UNSUPPORTED
} gcc_result_t;

/* effective hardware state read back from the registers (never a request). */
typedef struct gcc_state {
    uint32_t cbcr;
    uint32_t vote_reg;
    uint32_t cmd_rcgr;
    uint32_t cfg_rcgr;
    uint32_t m, n;
    uint32_t hz;
    uint8_t on;
    uint8_t enabled;
} gcc_state_t;

const gcc_clock_t* gcc_find_clock(const char* name);
/* the FOOTSWITCH path uses separate native NPA clients, so the generic Windows clock
 * delegates must never accept a GDSC and toggle its SW_COLLAPSE register directly. */

const gcc_clock_t* gcc_find_req_clock(const char* name);
const gcc_domain_t* gcc_clock_domain(const gcc_clock_t* clock);

gcc_result_t gcc_select_freq(const gcc_clock_t* clock, uint32_t hz, const gcc_freq_t** chosen);
int gcc_src_ready(const gcc_io_t* io, uint32_t src_kind);

gcc_result_t gcc_set_freq(const gcc_io_t* io, const gcc_clock_t* clock, const gcc_freq_t* freq);

gcc_result_t gcc_enable(const gcc_io_t* io, const gcc_clock_t* clock);

gcc_result_t gcc_disable(const gcc_io_t* io, const gcc_clock_t* clock);
gcc_result_t gcc_readback(const gcc_io_t* io, const gcc_clock_t* clock, gcc_state_t* state);
uint64_t gcc_pack_state(const gcc_state_t* state);

#ifdef __cplusplus
}
#endif
#endif
