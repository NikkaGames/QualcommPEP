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
#define RAIL_VDD_CX 0x01u
#define RAIL_VDD_MM 0x08u
#define RAIL_VDD_MX 0x10u

typedef struct molokai_gdsc_location {
    const char* domain;
    uint32_t native_clock_id;
    uint64_t controller_physical;
    uint32_t controller_bytes;
    uint32_t gdscr_offset;
    uint64_t gdscr_physical;
    uint32_t rail_mask;
    uint8_t required_client;
} molokai_gdsc_location_t;

/* map only address-verified domains; reference BSP addresses require Molokai validation. */

int molokai_gdsc_location(const char* domain, molokai_gdsc_location_t* out);
const molokai_gdsc_location_t* molokai_gdsc_at(uint32_t index);
#ifdef __cplusplus
}
#endif
#endif
