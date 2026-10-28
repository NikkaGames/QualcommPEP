#ifndef RPMH_TCS_H
#define RPMH_TCS_H
/* reg facts (BOOT.MXF Molokai RpmhTargetLib rsc_hal_hwio.h, APSS RSC; offsets relative to
 * the DRV window, 0x10000 per DRV; op15r.dts apps_rsc drv-2 at physical 0x18920000):
 * RSC_ID +0x000 (POR 0x00040300, mask 0xFFFFFF) RSC_PARENTCHILD_CONFIG +0x00C
 * TCS_AMC_MODE_IRQ_ENABLE/STATUS/CLEAR +0xD00 / */

#include <stdint.h>
#include "rpmh_target.h"
#ifdef __cplusplus
extern "C" {
#endif
#define RSC_ID_OFFSET 0x0u
#define RSC_ID_MASK 0xFFFFFFu
#define RSC_ID_POR 0x040300u
#define RSC_PARENTCHILD_CONFIG_OFFSET 0xCu
#define TCS_BASE 0xD00u
#define TCS_STRIDE 0x2A0u
#define TCS_CMD_STRIDE 0x18u
#define TCS_BATCH_MAX 16u
#define TCS_IRQ_ENABLE 0xD00u
#define TCS_IRQ_STATUS 0xD04u
#define TCS_IRQ_CLEAR 0xD08u
#define TCS_WAIT(m) (0xD20u + (m) * TCS_STRIDE)
#define TCS_CONTROL(m) (0xD24u + (m) * TCS_STRIDE)
#define TCS_STATUS(m) (0xD28u + (m) * TCS_STRIDE)
#define TCS_ENABLE(m) (0xD2Cu + (m) * TCS_STRIDE)
#define TCS_MSGID(m) (0xD34u + (m) * TCS_STRIDE)
#define TCS_ADDR(m) (0xD38u + (m) * TCS_STRIDE)
#define TCS_DATA(m) (0xD3Cu + (m) * TCS_STRIDE)
#define TCS_CMD_STATUS(m) (0xD40u + (m) * TCS_STRIDE)
#define TCS_CMD_RESP(m) (0xD44u + (m) * TCS_STRIDE)
#define TCS_CMD_MSGID(m, n) (TCS_MSGID(m) + (n) * TCS_CMD_STRIDE)
#define TCS_CMD_ADDR(m, n) (TCS_ADDR(m) + (n) * TCS_CMD_STRIDE)
#define TCS_CMD_DATA(m, n) (TCS_DATA(m) + (n) * TCS_CMD_STRIDE)
#define TCS_CMD_STATUS_N(m, n) (TCS_CMD_STATUS(m) + (n) * TCS_CMD_STRIDE)
#define TCS_CMD_RESP_N(m, n) (TCS_CMD_RESP(m) + (n) * TCS_CMD_STRIDE)
#define TCS_CMD_COMPLETED (1u << 16)
#define TCS_CMD_ISSUED (1u << 8)
#define TCS_CMD_TRIGGERED 1u
#define TCS_CONTROL_AMC_MODE_EN (1u << 16)
#define TCS_CONTROL_AMC_TRIGGER (1u << 24)
#define TCS_STATUS_IDLE 1u
#define TCS_MSGID_LENGTH 8u
#define TCS_MSGID_RES_REQ (1u << 8)
#define TCS_MSGID_WRITE (1u << 16)
#define TCS_MSGID_WRITE_CMD (TCS_MSGID_LENGTH | TCS_MSGID_RES_REQ | TCS_MSGID_WRITE)
#define TCS_MSGID_READ_CMD (TCS_MSGID_LENGTH | TCS_MSGID_RES_REQ)

/* HLOS configuration at +0x0C encodes TCS count in bits [17:12]
 * and commands per TCS in bits [31:27]. */

typedef struct rsc_hlos_geometry {
    uint8_t tcs_cnt;
    uint8_t commands_per_tcs;
} rsc_hlos_geometry_t;

/* return 1 only if the reported hardware is large enough for the caller's requested
 * count/capacity. No MMIO. */

int rpmh_tcs_check_hlos_geometry(uint32_t parent_child_config, uint8_t required_tcs,
                                 uint8_t required_commands, rsc_hlos_geometry_t* result);

typedef struct rpmh_tcs_io {
    void* ctx;
    /* offset relative to the mapped HLOS drv2 window, not the RSCC base. */
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
    void (*spin_hint)(void*);
    int (*lock)(void*);
    void (*unlock)(void*);
    /* deferred Windows issuer runs under the native RPMh passive KMUTEX.
     * enter_irq_lock/leave_irq_lock are retained ABI names: binds these to read-only
     * owner/epoch checks, not WdfInterruptTryToAcquireLock. the WDF passive ISR obtains that
     * native RPMh mutex independently. */

    int (*enter_irq_lock)(void*);
    void (*leave_irq_lock)(void*);
    void* irq_lock_ctx;
    /* optional, read-only classification for an enter_irq_lock refusal. never substitutes for
     * the physical interrupt lock or an owner grant. */
    uint32_t (*get_irq_lock_refusal)(void*);
} rpmh_tcs_io_t;

/* read-only per-issue reason, written by the physical TCS issuer while its owner
 * serializes requests. It is not an admission or ownership grant. */
typedef enum amc_busy_reason {
    AMC_BUSY_NONE = 0,
    AMC_BUSY_TRANSPORT_LOCK = 1,
    AMC_BUSY_LEDGER_TICKET = 2,
    AMC_BUSY_INTERRUPT_LOCK = 3,
    AMC_BUSY_HARDWARE_STATE = 4,
    AMC_BUSY_INHERITED_MASK = 5,
    AMC_BUSY_IRQ_CTX = 6,
    AMC_BUSY_IRQ_FRAMEWORK = 7,
    AMC_BUSY_IRQ_OWNER = 8,
    AMC_BUSY_IRQ_TRACKER = 9,
    AMC_BUSY_IRQ_BRIDGE = 10,
    AMC_BUSY_IRQ_TRY_CONTENDED = 11,
    AMC_BUSY_IRQ_REVOKED = 12
} amc_busy_reason_t;

typedef struct rpmh_tcs {
    rpmh_tcs_io_t io;
    /* must be independently established by the OS/firmware integration. A zero mask blocks all
     * hardware writes. */

    uint8_t owned_amc_mask;
    /* establish external rights before prepare. Probes and idle reads do not set these flags. */

    uint8_t external_grant_mask;
    uint8_t completion_handler_bound;
    /* slots whose completion interrupt rpmh_tcs_prepare enabled and that rpmh_tcs_restore must
     * therefore disable again, so a failed probe does not leave the shared RSC altered. */

    uint8_t irq_enabled_by_us;
    /* this is an observation bound to the authenticated d0 lease, never an ownership grant:
     * prepare still requires external_grant_mask/ISR completion binding. restore must preserve
     * these bits rather than disabling them as ours. */

    uint8_t pre_enabled_irq_mask;
    /* separate completion consumers must not W1C the same AMC status bit. only change this
     * under io.lock while quiescent and with no in-flight transaction, as part of a verified
     * owner handoff. */

    uint8_t dpc_consumes_completion;
    /* command-enable and command-dependency masks may be different; both must match their
     * separate recorded values. No PnP/firmware ownership is inferred. */

    uint8_t completed_amc_mask;
    uint16_t last_completed_command_mask[3];
    /* keep the exact last verified WAIT mask for rearm, including the valid zero value. */

    uint16_t last_completed_dependency_mask[3];
    /* deferred AMC tickets: only the IRQ-owned writer may set these, and the exact request may
     * be retired once, after worker-verified W1C. Shared ISR/worker/submit serialization is
     * supplied by the owner. */

    /* last pre-dispatch BUSY cause for each AMC. Do not use for decisions. Cleared on the next
     * attempt to the same slot; never turns BUSY into OK. */
    uint8_t last_arm_busy_reason[3];
    uint8_t deferred_active_mask;
    uint8_t deferred_command_cnt[3];
    uint16_t deferred_read_mask[3];
    uint32_t deferred_epoch[3];
    uint32_t deferred_req_id[3];
    uint32_t poll_limit;
} rpmh_tcs_t;

/* there is no GIC access, ACK or TCS control write here. the installer supplies a stable
 * native owner epoch and the 32-bit ledger epoch used when issuing the deferred TCS
 * transaction. */

struct rsc_completion;

typedef struct rpmh_irq_src {
    rpmh_tcs_t* dev;
    struct rsc_completion* ledger;
    uint64_t owner_epoch;
    uint32_t ledger_epoch;
    uint32_t masked_amcs;
    uint8_t irq_mask_active;
    uint8_t irq_mask_faulted;
} rpmh_irq_src_t;

int rpmh_tcs_read_owned_irq_status(void* src, uint64_t owner_epoch);
/* caller holds dev->io.lock and the independently authenticated RSC lease. only owned AMC
 * src enables change; HAL owns the GIC line. restore also requires exact ledger
 * retirement, physical ACK and idle TCS readback. */
int rpmh_tcs_mask_owned_irq(rpmh_irq_src_t*, uint64_t owner_epoch);
int rpmh_tcs_restore_owned_irq(rpmh_irq_src_t*, uint64_t owner_epoch);

typedef enum rpmh_tcs_result {
    TCS_OK = 0,
    TCS_INVALID = 1,
    TCS_NO_OWNERSHIP = 2,
    TCS_IO_FAILURE = 3,
    TCS_BUSY = 4,
    TCS_NOT_AMC = 5,
    TCS_TIMEOUT = 6,
    TCS_COMMAND_INCOMPLETE = 7,
    TCS_IRQ_NOT_ENABLED = 8,
    TCS_COMPLETION_CONSUMER_CONFLICT = 9,
    TCS_ACK_PENDING = 10 /* w1C clear not confirmed by status readback. */
} rpmh_tcs_result_t;

typedef struct tcs_command {
    uint32_t address; /* command-db address + verified res offset. */
    uint32_t data;    /* encoded write payload; must be zero for reads. */
    uint8_t read;
} tcs_command_t;

/* requires externally established ownership and a bound completion handler. owner action:
 * verify the TCS is idle and unprogrammed, then enable its AMC completion interrupt bit
 * (the status bit is what the writer polls) and remember that did. */

rpmh_tcs_result_t rpmh_tcs_prepare(rpmh_tcs_t* dev, uint8_t tcs);
/* an unsuccessful attempt to acquire the owner lock is not a clean teardown, even if no
 * IRQ-enable bit is currently recorded: another thread may still be using mapped RSC
 * registers under that lock. */

#define TCS_CLEANUP_UNSYNCHRONIZED 0x80u

/* undo rpmh_tcs_prepare: revoke software access under io.lock and clear only completion
 * interrupt bits this writer enabled. this is a quiescent teardown operation: the owner
 * must first stop new submissions, drain ISR/DPC activity and prevent new prepare calls. */

uint8_t rpmh_tcs_restore(rpmh_tcs_t* dev);

/* it must not run when a live ISR/DPC completion consumer owns that bit; see
 * dpc_consumes_completion. An eventual DPC-backed submit API must use the immutable
 * completion ledger, not this synchronous poll-and-ACK API. */

rpmh_tcs_result_t rpmh_tcs_issue_batch(rpmh_tcs_t* dev, uint8_t tcs, const tcs_command_t* commands,
                                       uint32_t cnt);
/* dependency bits outside the submitted command range are rejected before owner checks or
 * MMIO. this is not an ownership grant or an asynchronous ISR/DPC implementation. */

rpmh_tcs_result_t rpmh_tcs_issue_batch_with_dependencies(rpmh_tcs_t* dev, uint8_t tcs,
                                                         const tcs_command_t* commands,
                                                         uint32_t cnt, uint32_t dependency_mask,
                                                         uint32_t* responses);
/* the complete output array is committed only after all commands finish, W1C ACK readback
 * shows the bit clear and AMC control cleanup succeeds. */

rpmh_tcs_result_t rpmh_tcs_issue_batch_ex(rpmh_tcs_t* dev, uint8_t tcs,
                                          const tcs_command_t* commands, uint32_t cnt,
                                          uint32_t* responses);
/* record the immutable ticket before triggering the owned AMC. Completion is split-phase, without
 * polling or local W1C. */

struct rsc_completion;
typedef struct rsc_completion rsc_completion_t;
struct rsc_irq_latch;
typedef struct rsc_irq_latch rsc_irq_latch_t;
rpmh_tcs_result_t rpmh_tcs_arm_deferred(rpmh_tcs_t* dev, rsc_completion_t* ledger, uint32_t epoch,
                                        uint32_t req_id, uint8_t slot,
                                        const tcs_command_t* commands, uint32_t cnt,
                                        uint32_t dependency_mask);
/* a completed ticket becomes eligible for TCS rearm only after AMC control is cleared and
 * read back. Response array is unchanged unless all steps succeed. */

rpmh_tcs_result_t rpmh_tcs_finish_deferred(rpmh_tcs_t* dev, rsc_completion_t* ledger,
                                           rsc_irq_latch_t* latched, uint32_t* responses);

rpmh_tcs_result_t rpmh_tcs_issue(rpmh_tcs_t* dev, uint8_t tcs, uint32_t address, uint32_t data,
                                 uint8_t response_required);
/* on failure, *data is unchanged, matching the atomic output contract of
 * rpmh_tcs_issue_batch_ex. */

rpmh_tcs_result_t rpmh_tcs_read(rpmh_tcs_t* dev, uint8_t tcs, uint32_t address, uint32_t* data);
#ifdef __cplusplus
}
#endif
#endif
