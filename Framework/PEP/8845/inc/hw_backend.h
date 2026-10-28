/* register the backend and publish graph providers only after physical verification. */

#ifndef HW_BACKEND_H
#define HW_BACKEND_H

#include "state_txn.h"
#include "res_graph.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HW_BACKEND_VERSION 0x00960002u

/* res domains for independent readback (probe_readback `domain`). */
typedef enum hw_domain {
    HW_DOMAIN_CLOCK = 1,
    HW_DOMAIN_PMIC_VREG = 2,
    HW_DOMAIN_BUS = 3,
    HW_DOMAIN_TLMM = 4,
    HW_DOMAIN_PMIC_GPIO = 5,
    HW_DOMAIN_UFS_PSTATE = 6,
    HW_DOMAIN_COMP_PERF = 7
} hw_domain_t;

typedef struct hw_backend {
    uint32_t version; /* must equal HW_BACKEND_VERSION. */
    uint64_t owner_epoch;

    int (*supports)(void* ctx, const char* path, uint32_t d_state, const power_action_t*);
    int (*snapshot)(void* ctx, const char* path, uint32_t d_state, const power_action_t*,
                    power_snapshot_t*);
    int (*apply)(void* ctx, const char* path, uint32_t d_state, const power_action_t*);
    int (*verify)(void* ctx, const char* path, uint32_t d_state, const power_action_t*);
    int (*restore)(void* ctx, const char* path, uint32_t d_state, const power_action_t*,
                   const power_snapshot_t*);
    int (*verify_restored)(void* ctx, const char* path, uint32_t d_state, const power_action_t*,
                           const power_snapshot_t*);
    int (*verify_final)(void* ctx, const char* path, uint32_t d_state, const power_sequence_t*);

    /* mandatory independent hardware readback. Reads the *effective* current state of (domain,
     * key) directly from the owning controller - not a replay of the PEP's own intended vote,
     * not a shared aggregate. */

    int (*probe_readback)(void* ctx, uint32_t domain, uint32_t key, uint64_t* out_value);

    /* return nonzero only after an independent read through a live provider. */

    int (*self_test)(void* ctx);

    void* ctx;

    /* set the attestation signature only after independent hardware validation. Structurally valid
     * provider metadata cannot open admission. */

    uint64_t production_attestation; /* must equal HW_ATTEST_MAGIC. */
} hw_backend_t;

#define HW_ATTEST_MAGIC 0x484F4E4553543936ull

/* returns 0 on success, negative on rejection (bad version, zero epoch, incomplete ops, or
 * a backend already registered with a different live epoch). */

int hw_backend_reg(const hw_backend_t* backend);

/* unregister the backend with the given epoch (no-op on epoch mismatch). */
/* returns 1 when the table was cleared; returns 0 on foreign epoch OR when outstanding
 * provider leases require owner quarantine/deferred teardown. */

int hw_backend_unregister(uint64_t owner_epoch);
/* owner-only d0 quiesce: closes PoFx/backend admission before IRQ teardown. Returns 1 if
 * all graph callbacks drained; on 0 caller must retain MMIO, WDF/IRQ and backend storage. */

int hw_backend_quiesce_d0(uint64_t owner_epoch);

/* provider rundown. No waiting or pool operations occur in these functions; safe at graph
 * callback IRQL. */

int hw_backend_begin_bound(uint64_t serial, hw_backend_t* copy);
void hw_backend_end_bound(void);
uint32_t hw_backend_rundown_cnt(void);

/* called at PASSIVE_LEVEL with the reg lifecycle serialized, from the backend owner's
 * attach path; returns non-zero when verification passed. A foreign/stale owner epoch has
 * no side effects. */

int hw_backend_validate(uint64_t owner_epoch);

/* returns non-zero iff a complete, epoch-valid, production-attested backend is currently
 * registered AND its one-shot on-device verification passed. Side-effect free and callable
 * at any IRQL: it only reads latched state. */

int hw_backend_ready(void);
int hw_backend_idle_begin(uint64_t* serial);
int hw_backend_idle_end(uint64_t serial);

const hw_backend_t* hw_backend_get(void);

/* graph binding identity, not a hardware ownership grant. Only a currently verified reg
 * returns a nonzero serial, and a bound provider may dispatch only while that exact reg
 * remains verified. */

uint64_t hw_backend_verified_serial(void);
const hw_backend_t* hw_backend_get_bound(uint64_t serial);

/* bind graph action providers before publishing the verified production backend. */

int hw_backend_bind_graph(res_graph_t* graph);

#ifdef __cplusplus
}
#endif

#endif
