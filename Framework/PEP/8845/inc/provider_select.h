#ifndef PROVIDER_SELECT_H
#define PROVIDER_SELECT_H
/* the length is in 16-bit code units, including the double terminator; the caller owns the
 * system-provided buf and must keep it alive. */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

int is_unique_interface_multi_sz(const uint16_t* data, size_t units);
#ifdef __cplusplus
}
#endif
#endif
