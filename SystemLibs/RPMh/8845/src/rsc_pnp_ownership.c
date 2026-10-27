#include "rsc_pnp_ownership.h"

rsc_pnp_result_t rsc_check_translated_assignment(const rsc_pnp_memory_t* memory, size_t cnt,
                                                 unsigned interrupt_cnt)
{
    size_t i;
    unsigned matches = 0u;
    const uint64_t expected_start = RSC_DRV2_PNP_BASE;
    const uint64_t expected_end = expected_start + RSC_DRV2_PNP_SIZE;
    if ((!memory && cnt) || cnt > RSC_PNP_MAX_MEMORY)
        return RSC_PNP_INVALID;
    for (i = 0; i < cnt; ++i) {
        uint64_t start = memory[i].start, length = memory[i].length, end;
        if (!length || length > UINT64_MAX - start)
            return RSC_PNP_INVALID;
        end = start + length;
        if (start < expected_end && end > expected_start) {

            if (start != expected_start || length != RSC_DRV2_PNP_SIZE)
                return RSC_PNP_AMBIGUOUS;
            ++matches;
        }
    }
    if (!matches)
        return RSC_PNP_MISSING;
    if (matches != 1u)
        return RSC_PNP_AMBIGUOUS;
    if (!interrupt_cnt)
        return RSC_PNP_NO_INTERRUPT;
    return RSC_PNP_ASSIGNED_MEMORY;
}
