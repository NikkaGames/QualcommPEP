#include "compat_ids.h"

int validate_original_clock_id(uint32_t clock_id, uint32_t owned_table_cnt, uint32_t* table_idx)
{
    uint32_t idx;
    if (table_idx)
        *table_idx = 0u;
    if (!table_idx || !owned_table_cnt || owned_table_cnt > CLOCK_ID_MAX_INDICES ||
        (clock_id >> 16) != CLOCK_ID_TYPE)
        return 0;
    idx = clock_id & 0xffffu;
    if (idx >= owned_table_cnt)
        return 0;
    *table_idx = idx;
    return 1;
}

int preflight_original_clock_runtime(uint32_t clock_id, uint32_t owned_table_cnt,
                                     uint32_t operation, const clock_owner_snapshot_t* snapshot,
                                     uint32_t* table_idx)
{
    uint32_t idx = 0u;
    if (table_idx)
        *table_idx = 0u;
    if (!snapshot || !table_idx || !snapshot->owner_epoch || snapshot->owner_synchronized != 1u ||
        snapshot->clock_entry_present != 1u || snapshot->dev_present != 1u ||
        (snapshot->runtime_flags & CLOCK_RUNTIME_INHIBIT) ||
        (snapshot->clock_object_flags & CLOCK_CLIENT_RESTRICTED &&
         !(snapshot->dev_flags & CLOCK_CLIENT_RESTRICTED)) ||
        (operation != CLOCK_ID_OP_ENABLE && operation != CLOCK_ID_OP_DISABLE &&
         operation != CLOCK_ID_OP_FREQ) ||
        !validate_original_clock_id(clock_id, owned_table_cnt, &idx))
        return 0;
    *table_idx = idx;
    return 1;
}
