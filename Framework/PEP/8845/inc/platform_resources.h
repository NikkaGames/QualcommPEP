#ifndef PLATFORM_RESOURCES_H
#define PLATFORM_RESOURCES_H
/* coverage is diagnostic; refuse only plans that require an unresolved res. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dsdt_route {
    const char* master;
    const char* slave;
} dsdt_route_t;

extern const char* const QcPep8845DsdtRailNames[];
extern const unsigned int QcPep8845DsdtRailNameCount;
extern const dsdt_route_t QcPep8845DsdtRoutes[];
extern const unsigned int QcPep8845DsdtRouteCount;
extern const char* const QcPep8845DsdtClockNames[];
extern const unsigned int QcPep8845DsdtClockNameCount;
extern const unsigned short QcPep8845DsdtTlmmPins[];
extern const unsigned int QcPep8845DsdtTlmmPinCount;

#ifdef __cplusplus
}
#endif
#endif
