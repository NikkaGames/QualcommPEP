#include "bwmon.h"

/* OnePlus SM8845 platform data, BWMON v4/v5 and LLCC PMU v3. */
const bwmon_target_t bwmon_targets[BWMON_COUNT] = {
    {"llcc-gold", "\\_SB.PEP0#BWM0", "ICBID_MASTER_APPSS_PROC", "ICBID_SLAVE_LLCC",
     UINT64_C(0x310b3400), UINT64_C(0x310b3300), UINT64_C(4512000000), UINT64_C(19376000000), 613u,
     19200000u, 65536u, 2u, 0x3fu, 4u, 0u},
    {"llcc-prime", "\\_SB.PEP0#BWM1", "ICBID_MASTER_APPSS_PROC", "ICBID_SLAVE_LLCC",
     UINT64_C(0x310b7400), UINT64_C(0x310b7300), UINT64_C(4512000000), UINT64_C(19376000000), 613u,
     19200000u, 65536u, 2u, 0xc0u, 4u, 0u},
    {"ddr", "\\_SB.PEP0#BWM2", "ICBID_MASTER_LLCC", "ICBID_SLAVE_EBI1", UINT64_C(0x31091000), 0u,
     UINT64_C(2188000000), UINT64_C(19120000000), 113u, 19200000u, 65536u, 2u, 0xffu, 5u, 0u},
};

const uint64_t lagg_base = UINT64_C(0x31095000);
const uint8_t lagg_cpu_indices[LAGG_CPUS] = {2u, 3u, 4u, 5u, 6u, 7u, 0u, 1u};
