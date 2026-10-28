#include "perf_target.h"

/* Molokai HLOS PDP mailbox and CPU topology from the platform firmware interface. */
static const perf_target_t target = {UINT64_C(0x81f08100), 0x80u, UINT64_C(0x19980000), 0x300u,
                                     0x104u};

const perf_target_t* perf_target(void)
{
    return &target;
}

uint32_t perf_target_capacity(uint32_t domain)
{
    static const uint32_t capacity[PERF_TARGET_DOMAINS] = {1024u, 1208u};
    return domain < PERF_TARGET_DOMAINS ? capacity[domain] : 0u;
}

uint32_t perf_target_minimum_hz(uint32_t domain)
{
    static const uint32_t minimum_hz[PERF_TARGET_DOMAINS] = {556800000u, 806400000u};
    return domain < PERF_TARGET_DOMAINS ? minimum_hz[domain] : 0u;
}

int perf_target_core(uint64_t mpidr, uint32_t* domain, uint32_t* core)
{
    uint64_t affinity = mpidr & UINT64_C(0xff00ffffff);
    uint32_t cluster = (uint32_t)(affinity >> 16);
    uint32_t number = (uint32_t)((affinity >> 8) & 0xffu);
    if (!domain || !core || (affinity & 0xffu) || cluster >= PERF_TARGET_DOMAINS ||
        number >= (cluster ? 2u : 6u))
        return 0;
    *domain = cluster;
    *core = number;
    return 1;
}

int perf_target_channel(uint64_t address, uint32_t bytes, int write)
{
    static const uint64_t windows[] = {UINT64_C(0x19980000), UINT64_C(0x1a980000)};
    uint32_t i;
    if (!bytes || address > UINT64_MAX - bytes)
        return 0;
    for (i = 0u; i < PERF_TARGET_DOMAINS; ++i)
        if (address >= windows[i] && address + bytes <= windows[i] + 0x300u)
            return 1;
    return !write &&
           ((address >= UINT64_C(0x17610000) && address + bytes <= UINT64_C(0x17614c08)) ||
            (address >= UINT64_C(0x81f00000) && address + bytes <= UINT64_C(0x81f10000)));
}
