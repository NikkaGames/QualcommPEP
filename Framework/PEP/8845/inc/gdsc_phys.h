#ifndef MOLOKAI_GDSC_PHYS_H
#define MOLOKAI_GDSC_PHYS_H
/* exact Molokai ClockBSP/HWIO/DT physical GDSCR locations. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define DISP_CC_PHYS UINT64_C(0x09BA2000)
#define DISP_CC_BYTES 0x00020000u
#define DISP_CORE_GDSCR_OFFSET 0x00009000u
#define DISP_CORE_GDSCR_PHYS UINT64_C(0x09BAB000)

typedef struct molokai_gdsc_location {
    const char* domain;
    uint32_t native_clock_id;
    uint64_t controller_physical;
    uint32_t controller_bytes;
    uint32_t gdscr_offset;
    uint64_t gdscr_physical;
} molokai_gdsc_location_t;

/* map only address-verified domains; reference BSP addresses require Molokai validation. */

int molokai_gdsc_location(const char* domain, molokai_gdsc_location_t* out);
#ifdef __cplusplus
}
#endif
#endif
