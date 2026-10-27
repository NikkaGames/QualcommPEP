#ifndef NPA_NATIVE_IDENTITY_H
#define NPA_NATIVE_IDENTITY_H
/* static native NPA client identity / scalar-request PLAN construction. Signed SC8480
 * establishes the CLOCK request ordering and explicit scalar values; this does NOT
 * implement NPA, grant Windows ownership or prove hardware completion. */

#include "npa_client.h"
#ifdef __cplusplus
extern "C" {
#endif
#define NATIVE_NPA_NAME_SIZE 192u
/* the signed SC8480 CLOCK setup passes w2=0x40 to its internal client resolver at RVA
 * 0x070D08, which forwards it to the NPA client factory (0x2319F8 -> 0x2313A0). 0x40 is
 * NPA_CLIENT_REQUIRED in Qualcomm NPA. */

#define SC8480_CLOCK_NPA_CLIENT_REQUIRED 0x40u

typedef enum native_npa_class {
    NATIVE_NPA_UNRESOLVED = 0,
    NATIVE_NPA_CLOCK_ENABLE = 1,
    NATIVE_NPA_CLOCK_FREQ = 2,
    NATIVE_NPA_BUS = 3
} native_npa_class_t;

/* original SC8480 signed res-definition links: /clk/<name>/enable -> aggregation RVA
 * 0x2354D0: nonzero MIN (1 ON beats 2 OFF) /clk/<name> -> aggregation RVA 0x235690:
 * unsigned MAX (freq hz). These are NPA vote aggregation functions, not hardware request
 * callbacks. */

typedef enum native_clock_aggregation {
    NATIVE_CLOCK_AGGREGATION_UNKNOWN = 0,
    NATIVE_CLOCK_ENABLE_MIN_NONZERO = 1,
    NATIVE_CLOCK_FREQ_MAX = 2
} native_clock_aggregation_t;

#define SC8480_CLOCK_NPA_CLIENT_SUPPRESSIBLE 0x800u
#define NATIVE_CLOCK_MAX_AGGREGATE_VOTES 16u

typedef struct native_clock_vote {
    uint32_t scalar;
    uint32_t client_type;
    uint8_t is_issued; /* 0 means completed/released; never a native OFF vote. */
} native_clock_vote_t;

/* REQUIRED/SUPPRESSIBLE scalar aggregation has no hardware side effects.
 * It neither dispatches native client requests nor verifies physical clock state. */

int npa_aggregate_native_clock_votes(native_clock_aggregation_t aggregation,
                                     const native_clock_vote_t* votes, uint32_t vote_cnt,
                                     uint32_t* aggregate, uint32_t* issued_cnt);

typedef struct native_npa_client_desc {
    native_npa_class_t class;
    char res_name[NATIVE_NPA_NAME_SIZE];
    uint32_t client_reference[2];
    uint32_t client_reference_bytes;
    uint32_t rsc_driver_id;
    uint32_t client_type;
    native_clock_aggregation_t clock_aggregation;
} native_npa_client_desc_t;

/* sc8480's signed native descriptors distinguish freq (hz) from enable (1 ON / 2 OFF) NPA
 * clock resources. d0 EN_SETFREQ requires two *distinct* client identities. */

typedef struct native_npa_composite_clock_desc {
    native_npa_client_desc_t enable;
    native_npa_client_desc_t freq;
    uint32_t requested_hertz;
    uint32_t opaque_match_policy;
} native_npa_composite_clock_desc_t;

/* higher layers MUST independently verify the preexisting NPA clock policy (read-only
 * 0x1000) and native res ownership before any client creation or physical request. */

int npa_describe_composite_clock_clients(const npa_key_t*, const power_action_t*,
                                         native_npa_composite_clock_desc_t*);

/* opcode 8 (EN_SETFREQ) requires a separate dual-client plan; it MUST never be
 * misrepresented by this single-res function. */

int npa_describe_clock_client(const npa_key_t* key, const power_action_t* action,
                              native_npa_client_desc_t* out);

/* SC8480 RVA 0x231BA8 orders EN_SETFREQ as enable=1, then frequency in Hz;
 * DISABLE explicitly votes 2. This immutable plan does not execute or prove completion. */

#define NATIVE_CLOCK_MAX_SCALAR_ISSUES 2u
#define SC8480_CLOCK_ENABLE_REQ 1u
#define SC8480_CLOCK_DISABLE_REQ 2u

typedef struct native_npa_scalar_issue {
    native_npa_client_desc_t client;
    uint32_t requested_scalar;
} native_npa_scalar_issue_t;

typedef struct native_npa_clock_issue_plan {
    uint32_t cnt;
    uint32_t opaque_match_policy;
    native_npa_scalar_issue_t steps[NATIVE_CLOCK_MAX_SCALAR_ISSUES];
} native_npa_clock_issue_plan_t;

/* no client creation, physical write, returned hardware result or permission to register a
 * DPM owner occurs. invalid actions MUST NOT leave a partially usable output plan. */

int npa_plan_clock_scalar_issues(const npa_key_t* key, const power_action_t* action,
                                 native_npa_clock_issue_plan_t* out);

/* explicit verified native driver/TYPE mapping supplied by the eventual legitimate NPA
 * backend. It must not be inferred from DSDT client, nor from the Linux RSC MMIO base. */

int npa_describe_bus_client(const npa_key_t* key, const char* resolved_native_res_name,
                            uint32_t rsc_driver_id, uint32_t native_client_type,
                            native_npa_client_desc_t* out);
#ifdef __cplusplus
}
#endif
#endif
