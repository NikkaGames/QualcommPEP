#pragma once
#include <stddef.h>
#include <stdint.h>

#define CPU_TOPOLOGY_MAX 64u
#define CPU_PATH_MAX 256u

typedef struct cpu_identity {
    char path[CPU_PATH_MAX];
    uint32_t domain;
    uint32_t core;
} cpu_identity_t;

typedef struct cpu_topology {
    uint32_t count;
    cpu_identity_t cpus[CPU_TOPOLOGY_MAX];
} cpu_topology_t;

/* CCGP maps ACPI identities to firmware domain and core numbers. */
int cpu_topology_decode(const void*, size_t, cpu_topology_t*);
