#ifndef CLOCK_COMPAT_IDS_H
#define CLOCK_COMPAT_IDS_H
/* not a DAL client API, handle allocator or MMIO authorization. the owner must
 * independently supply an authoritative table count, and remain valid throughout the
 * physical transaction. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CLOCK_ID_TYPE 3u
#define CLOCK_ID_MAX_INDICES 65536u
/* reject other handle types, absent/oversized live table and indices outside the
 * owner-supplied table. */

int validate_original_clock_id(uint32_t clock_id, uint32_t owned_table_cnt, uint32_t* table_idx);
/* a successful type-3 clock query does not establish hardware completion. */

#define CLOCK_RUNTIME_INHIBIT (1u << 11)
#define CLOCK_CLIENT_RESTRICTED (1u << 14)
#define CLOCK_ID_OP_ENABLE 1u
#define CLOCK_ID_OP_DISABLE 2u
#define CLOCK_ID_OP_FREQ 3u

typedef struct clock_owner_snapshot {
    uint64_t owner_epoch;
    uint32_t runtime_flags; /* authenticated owner context +0x6c. */
    uint32_t clock_object_flags;
    uint32_t dev_flags;
    uint32_t owner_synchronized; /* provider attests atomic snapshot. */
    uint32_t clock_entry_present;
    uint32_t dev_present;
} clock_owner_snapshot_t;

/* clears idx on failure; does not execute hardware or return success for a stopped
 * runtime. both the owner and clock table must be live, owned and locked by a future
 * provider. */

int preflight_original_clock_runtime(uint32_t clock_id, uint32_t owned_table_cnt,
                                     uint32_t operation, const clock_owner_snapshot_t* snapshot,
                                     uint32_t* table_idx);
#ifdef __cplusplus
}
#endif
#endif
