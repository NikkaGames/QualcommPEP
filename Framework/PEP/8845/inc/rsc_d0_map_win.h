#ifndef RSC_D0_MAP_WIN_H
#define RSC_D0_MAP_WIN_H
/* DRV2 mapping is scoped to one PnP/D0 generation; it grants no TCS or IRQ write authority. */

#include <stdint.h>
#include "oem_rsc_identity_win.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rsc_d0_map_win {
    volatile unsigned char* base;
    uint64_t pnp_generation;
    uint32_t bytes;
    uint32_t id;
    uint32_t config;
} rsc_d0_map_win_t;

int rsc_d0_map_open_win(rsc_d0_map_win_t* map, uint64_t pnp_generation,
                        const rsc_identity_t* expected);
/* caller must establish completion, IRQ/DPC and TCS rundown. */

int rsc_d0_map_close_win(rsc_d0_map_win_t* map, uint64_t pnp_generation, int quiesced);
#ifdef __cplusplus
}
#endif
#endif
