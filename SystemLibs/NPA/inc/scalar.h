#ifndef NPA_SCALAR_CORE_H
#define NPA_SCALAR_CORE_H
/* internal scalar NPA res core, modeled on the request lifecycle in Molokai BOOT.MXF
 * NpaLib/framework/npa_client.c (not copied from firmware). It provides per-client vote
 * aggregation and failure-atomic requests for an EXCLUSIVELY owned, verified scalar res. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SCALAR_MAX_CLIENTS 16u
#define SCALAR_NAME_BYTES 128u
/* generic max-vote clients retain their old semantics and CANNOT be mixed with native
 * resources. */

#define SCALAR_AGGREGATE_MAX 0u
#define SCALAR_AGGREGATE_CLOCK_ENABLE 1u
#define SCALAR_AGGREGATE_CLOCK_FREQ 2u
#define SCALAR_CLIENT_REQUIRED 0x40u
#define SCALAR_CLIENT_SUPPRESSIBLE 0x800u

typedef struct scalar_io {
    void* ctx;
    /* must establish and continuously verify exclusive Windows ownership. */

    int (*owns_exclusive)(void*, const char*);
    /* hardware operations must complete before returning true. */
    /* must be NULL when attaching a native per-client transport; simultaneous writers are
     * forbidden. */

    int (*write_effective)(void*, const char*, uint32_t);
    /* independent authoritative physical state, not a software vote cache. */
    int (*read_effective)(void*, const char*, uint32_t*);
    /* mandatory res-specific encoder guard: must reject unknown NPA state values, not assume
     * that all uint32_t values are valid. */

    int (*allows_vote)(void*, const char*, uint32_t);
    /* optional independent proof that a nonzero boot-time hardware state belongs to this owner
     * AND is an intentionally retained baseline vote. This callback must fail when
     * Windows/firmware clients outside this owner may vote. */

    int (*attests_boot_baseline)(void*, const char*, uint32_t);
    /* native NPA ONLY: authoritative stable generation of the Windows res owner, observed
     * under that provider's synchronization. A nonzero res identity must be pinned BEFORE
     * client creation. */

    int (*read_owner_epoch)(void*, const char*, uint64_t*);
    /* native scalar callbacks use Windows-owned providers and return after request completion. They
     * are independent of aggregate write_effective callbacks. */

    int (*create_native_client)(void*, const char*, uint32_t, uint64_t*);
    int (*issue_native_client)(void*, const char*, uint64_t, uint32_t);
    int (*complete_native_client)(void*, const char*, uint64_t);
    int (*destroy_native_client)(void*, const char*, uint64_t);
    /* under provider synchronization: independently observe this client's outstanding request
     * (0/1 present), not the PEP's cached request. */

    int (*read_native_client)(void*, const char*, uint64_t, uint32_t*, uint32_t*);
} scalar_io_t;

typedef struct scalar_client {
    uint8_t exists;
    uint8_t has_req;
    uint16_t reserved;
    uint32_t req;
    uint32_t generation;
    uint32_t client_type;
    uint64_t native_token; /* nonzero only after provider creation. */
} scalar_client_t;

typedef struct scalar_res {
    char name[SCALAR_NAME_BYTES];
    scalar_io_t io;
    scalar_client_t clients[SCALAR_MAX_CLIENTS];
    uint32_t generation;
    uint32_t effective;
    /* a verified, owned boot-time floor, preserved across client release. It cannot be removed
     * by graph d3 or normal NPA client destruction. */

    uint32_t baseline_effective;
    uint32_t aggregate_mode;
    uint64_t owner_epoch;
    uint8_t active;
    uint8_t quarantined;
    uint8_t first_req_observed;
    uint8_t native_transport; /* opt-in: per-client execution, not aggregate write. */
} scalar_res_t;

typedef struct scalar_handle {
    uint32_t slot;
    uint32_t generation;
} scalar_handle_t;

/* callers must zero-initialize the res before its first Attach and serialize all
 * operations with a stable nonrecursive external lock. begin ONLY at verified hardware
 * state zero with exclusive owner. */

int scalar_attach(scalar_res_t*, const char*, const scalar_io_t*);
int scalar_create(scalar_res_t*, scalar_handle_t*);
/* explicit SC8480 client-class creation. */

int scalar_create_typed(scalar_res_t*, uint32_t client_type, scalar_handle_t*);
/* not a Windows NPA provider. Enables a signed-binary-verified aggregation policy only on
 * an independently owned, zero-state res. */

int scalar_attach_native_clock(scalar_res_t*, const char*, const scalar_io_t*,
                               uint32_t aggregate_mode);
/* production admission requires all native callbacks and an owner epoch. Injected aggregate
 * transport alone cannot admit DSDT devices. */

int scalar_attach_native_clock_client_transport(scalar_res_t*, const char*, const scalar_io_t*,
                                                uint32_t aggregate_mode);
/* a nonzero boot-time baseline must be explicitly attested by the hardware owner:
 * exclusive Windows ownership, legitimate vote provenance, exact state readback, and
 * allowed encoding. This does not invent another owner or grant hardware access. */

int scalar_attach_with_baseline(scalar_res_t*, const char*, const scalar_io_t*,
                                uint32_t verified_baseline);
/* each client's first request is always forwarded even if it requests 0. For subsequent
 * equal-value requests, authoritative readback is mandatory. */

int scalar_issue(scalar_res_t*, scalar_handle_t, uint32_t);
/* as in Qualcomm NPA, completion releases THIS client's outstanding vote. completion of
 * one client must not remove another client's vote. */

int scalar_complete(scalar_res_t*, scalar_handle_t);
/* auto-complete before destruction; release must be independently verified. */

int scalar_destroy(scalar_res_t*, scalar_handle_t);
/* revalidate exclusive ownership, attested baseline (when nonzero), and independent
 * physical effective state. Ownership/provenance revocation quarantines the res; readback
 * drift prevents readiness. */

int scalar_verify_owned(scalar_res_t*);
/* hardware state and all released clients must be verified before detach. on suspected
 * divergence, preserve a sticky quarantined res. */

int scalar_detach(scalar_res_t*);
#ifdef __cplusplus
}
#endif
#endif
