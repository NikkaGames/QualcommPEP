#ifndef GIC_D0_MAP_WIN_H
#define GIC_D0_MAP_WIN_H
/* the HAL retains interrupt-controller ownership until the authorized lifecycle is bound. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GICD_PHYS 0x17000000ULL
#define GICD_BYTES 0x390000u
#define GICD_TYPER 0x004u
#define GICD_IIDR 0x008u
#define GICD_ISENABLER1 0x104u
#define GICD_INTID37_BIT 0x20u

typedef struct gic_d0_map_win {
    volatile unsigned char* base;
    uint64_t pnp_generation;
    uint32_t bytes;
    uint32_t typer;
    uint32_t iidr;
    uint32_t irq37_enabled;
} gic_d0_map_win_t;

/* caller must hold the backend d0 lifetime mutex, ensure an active RSC map and uniquely
 * assigned PnP IRQ37. */

int gic_d0_observe_win(gic_d0_map_win_t* map, uint64_t pnp_generation);
/* caller must have disabled the WDF interrupt and drained all work. */

int gic_d0_close_win(gic_d0_map_win_t* map, uint64_t pnp_generation, int quiesced);
#ifdef __cplusplus
}
#endif
#endif
