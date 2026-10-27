/* windows backend owner and its physical verification contract. */

#ifndef HW_BACKEND_WIN_H
#define HW_BACKEND_WIN_H

#include "hw_backend.h"
#include "gpio.h"
#include "tlmm.h"
#include "npa_client.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIN_BACKEND_VERSION 0x00990002u

typedef enum hw_delegation {
    HW_DELEGATION_NOT_ESTABLISHED = 0, /* no owner primitive bound. */
    HW_DELEGATION_LIVE = 1,            /* owner primitives present. */
    HW_DELEGATION_REFUSED_BY_RULE = 2
} hw_delegation_t;

/* injected owner I/O. All run at PASSIVE_LEVEL under the graph's serialization (the gate
 * self-test may run at a higher IRQL; the primitives must then fail, never block or
 * fault). `path` is the ACPI device path of the voting client. */

typedef struct hwio {
    void* ctx;

    int (*delay_ms)(void* ctx, uint32_t ms);

    /* qcpmgpio_8480 CONFIG_DIGITAL_OUTPUT (28-byte request); success only when the provider
     * completed and returned pm_err_flag 0. */

    int (*pmic_gpio_cfg_digital_output)(void* ctx, const pm_gpio_digital_output_t* config);
    /* qcpmgpio_8480 GET_GPIO_STATE: eight opaque bytes read from the provider, independent of
     * anything this PEP requested. */

    int (*pmic_gpio_query_raw_state)(void* ctx, const pm_gpio_state_query_t* query,
                                     pm_gpio_state_raw_t* raw);

    /* tlmm tile 0 (owner): configure one pin; read back its effective configuration (all six
     * ACPI-order fields). */

    int (*tlmm_cfg)(void* ctx, const tlmm_gpio_t* config);
    int (*tlmm_readback)(void* ctx, uint32_t pin, tlmm_gpio_t* effective);

    /* pmic rail (RPMh VRM, owner): record the client's five DSDT DWORDs and realize the rail
     * aggregate; withdraw the client's vote; read back the rail (VRM read commands). */

    int (*pmic_regulator_vote)(void* ctx, const char* path, const char* ppp_name,
                               const uint32_t fields[5]);
    int (*pmic_regulator_withdraw)(void* ctx, const char* path, const char* ppp_name);
    int (*pmic_regulator_readback)(void* ctx, const char* ppp_name, uint64_t* effective);
    int (*pmic_regulator_known)(void* ctx, const char* ppp_name);
    int (*pmic_regulator_has_vote)(void* ctx, const char* path, const char* ppp_name);

    /* gcc clock (owner): apply the raw DSDT selector for the client (1 enable, 2 disable, 3
     * rate, 8 enable+rate) and realize the clock's aggregate; withdraw the client's vote; read
     * back the hardware state. */

    int (*clock_apply)(void* ctx, const char* path, const power_action_t* action);
    int (*clock_withdraw)(void* ctx, const char* path, const char* clock_name);
    int (*clock_readback)(void* ctx, const char* clock_name, uint64_t* effective);
    int (*clock_known)(void* ctx, const char* clock_name);
    int (*clock_has_vote)(void* ctx, const char* path, const char* clock_name);
    /* the only verified production form is UFS ICE CORE config 18 (force memory core ON).
     * Configuration changes have independent register snapshots/restore; they must NOT
     * overwrite a clock enable/rate client vote record. */

    int (*clock_config_readback)(void* ctx, const char* clock_name, uint32_t* force_on);
    int (*clock_config_write)(void* ctx, const char* clock_name, uint32_t force_on);

    /* icb/BCM (owner): record the client's bandwidth on the route and commit the affected BCM
     * votes; withdraw; read back the BCMs. */

    int (*bus_vote)(void* ctx, const char* path, const power_action_t* action);
    int (*bus_withdraw)(void* ctx, const char* path, const char* master, const char* slave);
    int (*bus_readback)(void* ctx, const char* master, const char* slave, uint64_t* effective);
    int (*bus_route_known)(void* ctx, const char* master, const char* slave);
    int (*bus_has_vote)(void* ctx, const char* path, const char* master, const char* slave);

    const char* (*bus_probe_route)(void* ctx, uint32_t key, const char** slave);
    const char* (*pmic_regulator_probe_name)(void* ctx, uint32_t key);

    uint32_t self_test_pmic_number;
    uint32_t self_test_gpio;

    /* a published callback is NOT proof of a live physical res. Checked at use/report time,
     * never cached as a PrepareHardware capability. */

    int (*rpmh_execution_ready)(void* ctx);
    /* callback addresses stay fixed across D0/resume. Production attach requires current-epoch
     * provider authorization and readback; NULL is reserved for injected providers. */

    int (*clock_execution_ready)(void* ctx);
    int (*tlmm_execution_ready)(void* ctx);
    /* a PMIC GPIO provider can appear after PrepareHardware. native Windows supplies a live,
     * independently revalidated provider predicate; the callback table remains immutable
     * across d0 and resume. */

    int (*pmic_gpio_execution_ready)(void* ctx);
} hwio_t;

/* last vote this backend issued for one (client, res); the rollback target for vote-shaped
 * classes (a readback is not a vote). The slot is reserved before any hardware write, so
 * exhaustion refuses the action instead of silently losing its rollback record. */

/* rollback records cover all enabled peripheral descriptors together. */
#define WIN_BACKEND_MAX_VOTES 512u

typedef struct last_vote {
    uint8_t in_use;
    uint8_t kind;
    uint8_t recorded;
    uint32_t key;
    char path[256]; /* voting client (device path). */
    char name[POWER_NAME_BYTES];
    char name2[POWER_NAME_BYTES];
    power_action_t action;
} last_vote_t;

/* npa client pool: one slot per state-owned client created by the owner. */
#define WIN_BACKEND_MAX_NPA_CLIENTS NPA_MAX_CLIENTS

typedef struct npa_client_slot {
    uint8_t in_use;
    /* an opaque per-creation identifier. NEVER expose/reuse the address of this recyclable
     * slot as the NPA handle (ABA after destroy/create). */

    uintptr_t handle_cookie;
    npa_key_t key;
} npa_client_slot_t;

typedef struct win_backend_diag {
    uint32_t refused[8];
    uint32_t applied[8];
    uint32_t verify_failed[8];
    uint32_t restored[8];
    uint32_t restore_failed[8];
    uint32_t withdrawn[8];
    uint32_t probe_calls;
    uint32_t probe_failed;
    uint32_t self_tests;
    uint32_t self_test_failed;
    uint32_t npa_created;
    uint32_t npa_destroyed;
    uint32_t npa_refused;
    uint64_t last_pmic_gpio_raw_before;
    uint64_t last_pmic_gpio_raw_after;
} win_backend_diag_t;

/* caller-owned NONPAGED storage; never a kernel stack object. */
typedef struct win_backend {
    uint32_t version;
    uint64_t owner_epoch;
    hwio_t io;
    /* the Windows attach sets it only after its runtime probe battery passed on the device
     * (src/molokai/hw_backend_windows8845.c); host builds never do. */

    hw_backend_t table;
    last_vote_t votes[WIN_BACKEND_MAX_VOTES];
    npa_client_slot_t npa_clients[WIN_BACKEND_MAX_NPA_CLIENTS];

    uintptr_t next_npa_handle_serial;
    win_backend_diag_t diag;
    uint8_t initialized;
    uint8_t registered;
} win_backend_t;

typedef struct win_backend_report {
    hw_delegation_t delegation[8];
    uint32_t live_kinds;
    uint32_t not_established_kinds;
    uint32_t refused_kinds;
    int clock_readback_present;
    int registered;
} win_backend_report_t;

/* initialize a zeroed core with the injected I/O and a nonzero owner epoch. */

int win_backend_init(win_backend_t* backend, const hwio_t* io, uint64_t owner_epoch);

hw_delegation_t win_backend_delegation(const win_backend_t* backend, power_action_kind_t kind);

void win_backend_report(const win_backend_t* backend, win_backend_report_t* report);

int win_backend_five_delegations_live(const win_backend_t* backend);

int win_backend_reg(win_backend_t* backend);
int win_backend_unregister(win_backend_t* backend);

/* npa client callbacks for npa_activate. owns answers from the owner's known primitives;
 * create allocates a pool slot; validate checks the slot and its identity; is_released
 * asks the owner's HasVote primitive (fail closed when absent); destroy frees the slot. */

void win_backend_npa_callbacks(win_backend_t* backend, npa_callbacks_t* callbacks);

#define PROBE_PMIC_GPIO_KEY(pmic, gpio) ((((uint32_t)(pmic)) << 16) | ((uint32_t)(gpio) & 0xFFFFu))

const char* win_backend_probe_clock_name(uint32_t key);

#ifdef __cplusplus
}
#endif

#endif
