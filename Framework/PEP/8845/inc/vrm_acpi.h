#ifndef VRM_ACPI_BRIDGE_H
#define VRM_ACPI_BRIDGE_H
#include "rail_bank.h"
#include "state_txn.h"
#ifdef __cplusplus
extern "C" {
#endif
/* converts only the two EXACT DSDT PMICVREGVOTE layouts into per-device shared-rail votes.
 * This is not a complete d0/d3 executor and must not be invoked outside an owner-verified,
 * all-actions power transaction. */

rail_result_t rail_vote_exact_acpi_action(rail_bank_t*, const char* dev_path, const power_action_t*,
                                          uint32_t dstate, uint64_t client);
#ifdef __cplusplus
}
#endif
#endif
