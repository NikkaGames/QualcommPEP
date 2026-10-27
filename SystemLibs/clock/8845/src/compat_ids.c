#include "compat_ids.h"

int validate_original_clock_id(uint32_t clock_id, uint32_t owned_table_cnt, uint32_t* table_idx)
{
    uint32_t idx;
    if (table_idx)
        *table_idx = 0u;
    if (!table_idx || !owned_table_cnt || owned_table_cnt > SC8480_CLOCK_MAX_IDX_CNT ||
        (clock_id >> 16) != SC8480_CLOCK_ID_TYPE)
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
        (snapshot->runtime_flags & SC8480_CLOCK_RUNTIME_INHIBIT) ||
        (snapshot->clock_object_flags & SC8480_CLOCK_RESTRICTED &&
         !(snapshot->dev_flags & SC8480_CLOCK_RESTRICTED)) ||
        (operation != SC8480_CLOCK_ENABLE && operation != SC8480_CLOCK_DISABLE &&
         operation != SC8480_CLOCK_FREQ) ||
        !validate_original_clock_id(clock_id, owned_table_cnt, &idx))
        return 0;
    *table_idx = idx;
    return 1;
}
