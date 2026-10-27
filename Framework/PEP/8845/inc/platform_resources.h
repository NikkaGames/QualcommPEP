#ifndef PLATFORM_RESOURCES_H
#define PLATFORM_RESOURCES_H
/* coverage is diagnostic; refuse only sequences that require an unresolved res. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dsdt_route {
    const char* master;
    const char* slave;
} dsdt_route_t;

extern const char* const platform_rail_names[];
extern const unsigned int platform_rail_cnt;
extern const dsdt_route_t platform_routes[];
extern const unsigned int platform_route_cnt;
extern const char* const platform_clock_names[];
extern const unsigned int platform_clock_cnt;
extern const unsigned short platform_tlmm_pins[];
extern const unsigned int platform_tlmm_pin_cnt;

#ifdef __cplusplus
}
#endif
#endif
