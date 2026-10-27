#pragma once
#include <ntddk.h>

#ifndef IOCTL_THERMAL_READ_TEMPERATURE

#define IOCTL_THERMAL_READ_TEMPERATURE 0x00294090u
#endif

#ifndef IOCTL_THERMAL_READ_POLICY

#define IOCTL_THERMAL_READ_POLICY 0x00294094u
#endif

typedef struct _THERMAL_WAIT_READ {
    ULONG Timeout;
} THERMAL_WAIT_READ, *PTHERMAL_WAIT_READ;

/* policy values use a 32-bit wire encoding. */
typedef ULONG THERMAL_POLICY, *PTHERMAL_POLICY;
