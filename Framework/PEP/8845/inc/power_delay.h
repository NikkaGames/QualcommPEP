#ifndef POWER_DELAY_H
#define POWER_DELAY_H
#include <ntddk.h>
#include <stdint.h>
/* invokable only by a PASSIVE_LEVEL power-transaction backend, never from a PoFx DISPATCH
 * callback. Maximal single delay matches the bounds of the aeo_b compiler. */

NTSTATUS power_delay_ms(uint32_t ms);
#endif
