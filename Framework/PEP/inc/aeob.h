#pragma once
/* windows ACPI_EVAL_OUTPUT_BUFFER serialization (aeo_b), ABI-independent. No allocations
 * or kernel dependencies; bounds are checked before visits. */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AEOB_SIGNATURE 0x426f6541u
#define AEOB_MAX_SIZE (1024u * 1024u)
#define AEOB_MAX_DEPTH 32u
#define AEOB_MAX_ARGUMENTS 65536u

typedef enum aeob_status {
    AEO_B_OK = 0,
    AEO_B_INVALID = 1,
    AEO_B_TRUNCATED = 2,
    AEO_B_UNSUPPORTED = 3,
    AEO_B_TOO_DEEP = 4,
    AEO_B_CALLBACK_ERROR = 5,
    AEO_B_NOT_FOUND = 6,
    AEO_B_AMBIGUOUS = 7
} aeob_status_t;

typedef struct aeob_argument {
    uint16_t type;
    uint16_t data_length;
    uint32_t offset;
    uint32_t depth;
    const uint8_t* data;
} aeob_argument_t;

typedef int (*aeob_visitor_t)(const aeob_argument_t* arg, void* ctx);

/* the caller must only consume the data after successful validation. */

aeob_status_t aeo_b_walk(const void* buf, size_t avail_bytes, aeob_visitor_t visit, void* ctx,
                         uint32_t* top_argument_cnt);
/* Valid empty packages are distinct from malformed responses and integer zero. */
int aeo_b_empty(const void* buf, size_t bytes);

aeob_status_t aeo_b_decode_stnd(const void* buf, size_t avail_bytes, char* names, uint32_t capacity,
                                uint32_t* name_cnt);

/* no automatic device remapping or hardware writes. */

typedef struct aeob_action {
    char kind[24];
    const uint8_t* arguments;
    uint32_t argument_bytes;
} aeob_action_t;

typedef int (*aeob_action_visitor_t)(const aeob_action_t*, void*);
aeob_status_t aeo_b_visit_dev_d_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                      uint32_t state, aeob_action_visitor_t visit, void* ctx,
                                      uint32_t* action_cnt);
/* empty FSTATEs are valid descriptors but do not prove a working F-state provider. aeo_b
 * must be a reviewed read-only ACPI method result; no AML execution. */

aeob_status_t aeo_b_visit_comp_f_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                       uint32_t comp, uint32_t f_state, aeob_action_visitor_t visit,
                                       void* ctx, uint32_t* action_cnt);
/* callers must validate a complete hardware provider before using any action. parses only
 * a reviewed, already-evaluated aeo_b buf. */

aeob_status_t aeo_b_visit_comp_p_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                       uint32_t comp, uint32_t set_id, uint32_t p_state,
                                       aeob_action_visitor_t visit, void* ctx,
                                       uint32_t* action_cnt);
/* both state arrays must hold 32 uint32_t values. */

aeob_status_t aeo_b_discover_comp_clock(const void* buf, size_t avail_bytes, const char* acpi_path,
                                        uint32_t comp, char clock_name[128]);
aeob_status_t aeo_b_inventory_dev_components(const void* buf, size_t avail_bytes,
                                             const char* acpi_path, uint32_t* comp_mask,
                                             uint32_t comp_f_states[32],
                                             uint32_t comp_pstate_sets[32],
                                             uint32_t* crashdump_state);
/* this performs no method evaluation and never follows nested action payloads. */

aeob_status_t aeo_b_list_dev_paths(const void* buf, size_t avail_bytes, char* paths,
                                   uint32_t capacity, uint32_t* cnt);

#ifdef __cplusplus
}
#endif
