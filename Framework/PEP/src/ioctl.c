#include "driver.h"
#include "soc_subsystem.h"
#include "public.h"
#include "bcl.h"
#include "stats.h"
#include "resource_cache.h"
#include "ioctl_contract.h"
#include "vendor_payloads.h"
#include "chipinfo.h"
#include "tsens.h"
#include "thermalioct.h"
#include "diag.h"
#include "peplog.h"
#include "xo_diag.h"
#include "ppm_diag.h"
#include "crash_dump.h"
#include "io_target.h"
#include "gpio.h"
#include "mmrm.h"
#include "thermal_request.h"
#include "interrupt.h"
#include "dcvs.h"
#include "ex_soc_mgmt.h"
#include "resource_diag.h"

static BOOLEAN is_compat_pmic_passthrough_family(_In_ ULONG code)
{
    switch (code & 0xFFFF0000u) {
        case 0x80020000u:
        case 0x80100000u:
        case 0x80190000u:
        case 0x801C0000u:
        case 0x80200000u:
        case 0x80260000u:
        case 0x80270000u:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOLEAN is_unsupported_parity_ioctl(_In_ ULONG code)
{
    return (code >= IOCTL_PEP_PING && code <= IOCTL_PEP_RSC_HARDEN_HISTORY);
}

static ULONG rsc_sw_subsystem_for_ioctl(_In_ ULONG ioctl_code)
{
    if (is_unsupported_parity_ioctl(ioctl_code))
        return RSC_SW_SUBSYSTEM_MAX;

    switch (ioctl_code) {
        case IOCTL_PEP_SUBSYS_ADSP_STATE:
            return RSC_SW_SUBSYSTEM_AOP;
        case IOCTL_PEP_SUBSYS_ADSP_CTRL:
            return RSC_SW_SUBSYSTEM_GLINK;
        case IOCTL_PEP_SUBSYS_CDSP_STATE:
            return RSC_SW_SUBSYSTEM_CMD_DB;
        case IOCTL_PEP_RSC_SNAPSHOT:
        case IOCTL_PEP_RSC_HISTORY:
        case IOCTL_PEP_RSC_SET_NODE:
        case IOCTL_PEP_RSC_QUERY_NODE:
        case IOCTL_PEP_RSC_APPLY_BATCH:
        case IOCTL_PEP_RSC_GET_CATALOG:
        case IOCTL_PEP_RSC_RUN_SCENARIO:
        case IOCTL_PEP_RSC_SCENARIO_HISTORY:
        case IOCTL_PEP_RSC_DIAG_SNAPSHOT:
        case IOCTL_PEP_RSC_DIAG_RUN_MATRIX:
        case IOCTL_PEP_RSC_DIAG_HISTORY:
        case IOCTL_PEP_RSC_PROFILE_CATALOG:
        case IOCTL_PEP_RSC_PROFILE_RUN:
        case IOCTL_PEP_RSC_PROFILE_HISTORY:
        case IOCTL_PEP_RSC_GRAPH_SNAPSHOT:
        case IOCTL_PEP_RSC_ACTION_SEQUENCE_EXECUTE:
        case IOCTL_PEP_RSC_ACTION_SEQUENCE_HISTORY:
        case IOCTL_PEP_RSC_CHECKPOINT_SNAPSHOT:
        case IOCTL_PEP_RSC_CHECKPOINT_CAPTURE:
        case IOCTL_PEP_RSC_CHECKPOINT_APPLY:
        case IOCTL_PEP_RSC_CHECKPOINT_HISTORY:
        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_SNAPSHOT:
        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_EXECUTE:
        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY:
        case IOCTL_PEP_RSC_COVERAGE_SNAPSHOT:
        case IOCTL_PEP_RSC_COVERAGE_ANALYZE:
        case IOCTL_PEP_RSC_COVERAGE_HISTORY:
        case IOCTL_PEP_RSC_REMEDIATION_SNAPSHOT:
        case IOCTL_PEP_RSC_REMEDIATION_RUN:
        case IOCTL_PEP_RSC_REMEDIATION_HISTORY:
        case IOCTL_PEP_RSC_STABILIZATION_SNAPSHOT:
        case IOCTL_PEP_RSC_STABILIZATION_RUN:
        case IOCTL_PEP_RSC_STABILIZATION_HISTORY:
        case IOCTL_PEP_RSC_CONFORMANCE_SNAPSHOT:
        case IOCTL_PEP_RSC_CONFORMANCE_RUN:
        case IOCTL_PEP_RSC_CONFORMANCE_HISTORY:
        case IOCTL_PEP_RSC_ORCHESTRATION_SNAPSHOT:
        case IOCTL_PEP_RSC_ORCHESTRATION_RUN:
        case IOCTL_PEP_RSC_ORCHESTRATION_HISTORY:
        case IOCTL_PEP_RSC_POLICY_SNAPSHOT:
        case IOCTL_PEP_RSC_POLICY_HISTORY:
        case IOCTL_PEP_RSC_FABRIC_SNAPSHOT:
        case IOCTL_PEP_RSC_FABRIC_RUN:
        case IOCTL_PEP_RSC_FABRIC_HISTORY:
        case IOCTL_PEP_RSC_DOMAIN_SNAPSHOT:
        case IOCTL_PEP_RSC_DOMAIN_RUN:
        case IOCTL_PEP_RSC_DOMAIN_HISTORY:
        case IOCTL_PEP_RSC_INTERCONNECT_SNAPSHOT:
        case IOCTL_PEP_RSC_INTERCONNECT_RUN:
        case IOCTL_PEP_RSC_INTERCONNECT_HISTORY:
        case IOCTL_PEP_RSC_TOPOLOGY_SNAPSHOT:
        case IOCTL_PEP_RSC_CATALOG:
        case IOCTL_PEP_RSC_SYNC_SNAPSHOT:
        case IOCTL_PEP_RSC_SYNC_RUN:
        case IOCTL_PEP_RSC_SYNC_HISTORY:
        case IOCTL_PEP_RSC_VALIDATE_SNAPSHOT:
        case IOCTL_PEP_RSC_VALIDATE_RUN:
        case IOCTL_PEP_RSC_VALIDATE_HISTORY:
        case IOCTL_PEP_RSC_SEQUENCE_SNAPSHOT:
        case IOCTL_PEP_RSC_SEQUENCE_RUN:
        case IOCTL_PEP_RSC_SEQUENCE_HISTORY:
        case IOCTL_PEP_RSC_LOOKUP_SNAPSHOT:
        case IOCTL_PEP_RSC_LOOKUP_RUN:
        case IOCTL_PEP_RSC_LOOKUP_HISTORY:
        case IOCTL_PEP_RSC_PROVISION_SNAPSHOT:
        case IOCTL_PEP_RSC_PROVISION_RUN:
        case IOCTL_PEP_RSC_PROVISION_HISTORY:
        case IOCTL_PEP_RSC_RAIL_SNAPSHOT:
        case IOCTL_PEP_RSC_RAIL_RUN:
        case IOCTL_PEP_RSC_RAIL_HISTORY:
        case IOCTL_PEP_RSC_TCS_SNAPSHOT:
        case IOCTL_PEP_RSC_TCS_RUN:
        case IOCTL_PEP_RSC_TCS_HISTORY:
        case IOCTL_PEP_RSC_THERMAL_SNAPSHOT:
        case IOCTL_PEP_RSC_THERMAL_RUN:
        case IOCTL_PEP_RSC_THERMAL_HISTORY:
        case IOCTL_PEP_RSC_COORD_SNAPSHOT:
        case IOCTL_PEP_RSC_COORD_RUN:
        case IOCTL_PEP_RSC_COORD_HISTORY:
        case IOCTL_PEP_RSC_AUDIT_SNAPSHOT:
        case IOCTL_PEP_RSC_AUDIT_RUN:
        case IOCTL_PEP_RSC_AUDIT_HISTORY:
        case IOCTL_PEP_RSC_SUPERVISOR_SNAPSHOT:
        case IOCTL_PEP_RSC_SUPERVISOR_RUN:
        case IOCTL_PEP_RSC_SUPERVISOR_HISTORY:
        case IOCTL_PEP_RSC_AUTOTUNE_SNAPSHOT:
        case IOCTL_PEP_RSC_AUTOTUNE_RUN:
        case IOCTL_PEP_RSC_AUTOTUNE_HISTORY:
        case IOCTL_PEP_RSC_HARDEN_SNAPSHOT:
        case IOCTL_PEP_RSC_HARDEN_RUN:
        case IOCTL_PEP_RSC_HARDEN_HISTORY:
            return RSC_SW_SUBSYSTEM_RSC;

        case IOCTL_PEP_PPP_GET_RPMH_COMMAND_SET:
            return RSC_SW_SUBSYSTEM_RPMH;
        case IOCTL_PEP_PPP_RESOURCE_VOTE:
        case IOCTL_PEP_PPP_GET_RESOURCE_SETTINGS:
        case IOCTL_PEP_PPP_RESOURCE_ACTION:
            return RSC_SW_SUBSYSTEM_PPP;

        case IOCTL_PEP_TSENS_SNAPSHOT:
        case IOCTL_PEP_TSENS_HISTORY:
        case IOCTL_PEP_TSENS_SENSOR_RANGE:
        case IOCTL_PEP_TSENS_SET_THRESHOLD:
        case IOCTL_PEP_TSENS_GET_TEMPS:
        case IOCTL_PEP_TSENS_GET_TEMP_FOR_IDX:
        case IOCTL_PEP_TSENS_STABLE_REQ:
            return RSC_SW_SUBSYSTEM_TSENS;

        case IOCTL_PEP_SOC_SUBSYS_COUNT:
        case IOCTL_PEP_SOC_SUBSYS_QUERY:
        case IOCTL_PEP_SOC_SUBSYS_RESET:
        case IOCTL_PEP_SOC_SUBSYS_BLOCKTIME:
        case IOCTL_PEP_SOC_SUBSYS_METADATA:
            return RSC_SW_SUBSYSTEM_RSC;

        case IOCTL_PEP_INTERRUPT_SNAPSHOT:
        case IOCTL_PEP_INTERRUPT_HISTORY:
        case IOCTL_PEP_INTERRUPT_WAKECOUNTERS:
        case IOCTL_PEP_INTERRUPT_LOOKUP:
            return RSC_SW_SUBSYSTEM_ICB;

        case IOCTL_PEP_MMRM_SNAPSHOT:
        case IOCTL_PEP_MMRM_HISTORY:
        case IOCTL_PEP_MMRM_DOMAIN_QUERY:
            return RSC_SW_SUBSYSTEM_ARC;

        case IOCTL_PEP_VENDOR_88882058:
        case IOCTL_PEP_VENDOR_88882088:
            return RSC_SW_SUBSYSTEM_CLOCK;

        case IOCTL_PEP_DEEPSTANDBY_SNAPSHOT:
        case IOCTL_PEP_DEEPSTANDBY_HISTORY:
        case IOCTL_PEP_DEEPSTANDBY_MODEM_GET:
        case IOCTL_PEP_DEEPSTANDBY_MODEM_SET:
        case IOCTL_PEP_VENDOR_88884084:
            return RSC_SW_SUBSYSTEM_PDC;

        case IOCTL_PEP_GPIO_CONFIG:
        case IOCTL_PEP_GPIO_REQUEST:
        case IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE:
        case IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT:
        case IOCTL_PEP_GPIO_PMIC_SET_STATE:
        case IOCTL_PEP_GPIO_PMIC_SET_PULL:
        case IOCTL_PEP_GPIO_PMIC_SET_DRIVE:
        case IOCTL_PM_GPIO_GET_GPIO_STATE:
        case IOCTL_PM_GPIO_SET_GPIO_OUTPUT:
        case IOCTL_PMIC_PUBLIC_USB_OTG_CONTROL:
        case IOCTL_PMIC_PUBLIC_USB_OTG_ENABLE:
        case IOCTL_PM_3P_PUBLIC_USB_OTG_ENABLE:
        case IOCTL_PM_WLED_MODULE_ENABLE:
        case IOCTL_PM_IBBLAB_MODULE_ENABLE:
        case IOCTL_PM_MPP_CONFIG_DIGITAL_OUTPUT:
        case IOCTL_PM_MPP_CONFIG_DIGITAL_INPUT:
        case IOCTL_PM_MPP_CONFIG_DIGITAL_INOUT:
        case IOCTL_PM_MPP_CONFIG_ANALOG_INPUT:
        case IOCTL_PM_MPP_CONFIG_ANALOG_OUTPUT:
        case IOCTL_PM_MPP_CONFIG_I_SINK:
        case IOCTL_PM_MPP_CONFIG_ATEST:
        case IOCTL_PM_MPP_GET_INPUT_STATE:
        case IOCTL_PM_MPP_SET_OUTPUT_STATE:
        case IOCTL_PM_LPG_PATTERN_CONFIG:
        case IOCTL_PM_LPG_PWM_SIZE:
        case IOCTL_PM_LPG_PWM_CLK:
        case IOCTL_PM_LPG_LUT_CFG:
        case IOCTL_PM_LPG_PWM_ENABLE:
        case IOCTL_PM_LPG_PWM_SET:
        case IOCTL_PM_LPG_GET_STATUS:
        case IOCTL_PM_LPG_SET_RAMP:
        case IOCTL_PM_LPG_SET_PAUSE:
        case IOCTL_PM_LPG_SET_LUT:
        case IOCTL_PM_LPG_SET_DTEST:
        case IOCTL_PM_LPG_ATEST:
            return RSC_SW_SUBSYSTEM_TLMM;

        case IOCTL_PEP_BMS_SET_THRESHOLDS:
        case IOCTL_PEP_BMS_QUERY:
            return RSC_SW_SUBSYSTEM_NPA;

        default:
            break;
    }

    switch (ioctl_code & 0xFFFF0000u) {
        case 0x80190000u:
            return RSC_SW_SUBSYSTEM_CLOCK;
        case 0x80040000u:
            return RSC_SW_SUBSYSTEM_PPP;
        case 0x80020000u:
        case 0x80030000u:
        case 0x800B0000u:
            return RSC_SW_SUBSYSTEM_TLMM;
        default:
            break;
    }

    return RSC_SW_SUBSYSTEM_MAX;
}

static ULONG hash_ascii_id(_In_reads_bytes_(len) const UCHAR* s, _In_ size_t len)
{
    if (!s || len == 0)
        return 0;

    ULONG h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (ULONG)s[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static VOID core_bsp_write_framework_log(_In_ BOOLEAN stress_framework, _In_opt_z_ const CHAR* text)
{
    if (!text)
        return;

    size_t text_len = 0;
    if (!NT_SUCCESS(RtlStringCchLengthA(text, NTSTRSAFE_MAX_CCH, &text_len)) || text_len == 0 ||
        text_len > MAXULONG)
        return;

    UNICODE_STRING path;
    RtlInitUnicodeString(&path, stress_framework ? L"\\DosDevices\\C:\\WINDOWS\\StressFramework.txt"
                                                 : L"\\DosDevices\\C:\\WINDOWS\\TestFramework.txt");

    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &path, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK iosb;
    RtlZeroMemory(&iosb, sizeof(iosb));

    HANDLE file_handle = NULL;
    NTSTATUS status =
        ZwCreateFile(&file_handle, FILE_GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, NULL,
                     FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OVERWRITE_IF,
                     FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status))
        return;

    (VOID)
        ZwWriteFile(file_handle, NULL, NULL, NULL, &iosb, (PVOID)text, (ULONG)text_len, NULL, NULL);

    ZwClose(file_handle);
}

typedef struct exsoc_parsed {
    ULONG dev_hash;
    ULONG dev_id_len;
    ULONG dev_id_word0;
    ULONG dev_id_word1;
    UCHAR d_state;
    CHAR dev_id[20];
} exsoc_parsed_t;

typedef struct mmrm_pending_ctx {
    volatile LONG infra_state;
    WDFSPINLOCK lock;
    WDFWORKITEM work_item;
    WDFTIMER timer;
    WDFREQUEST req;
    mmrm_policy_req_t req_payload;
    ULONGLONG wait_deadline_tick100ns;
    ULONG wait_timeout_ms;
    ULONG poll_interval_ms;
    BOOLEAN complete_on_timeout;
    BOOLEAN timed_out;
    BOOLEAN completion_queued;
    UCHAR reserved_pad;
} mmrm_pending_ctx_t;

static mmrm_pending_ctx_t g_mmrm_pending;

typedef struct ppp_vote_rec {
    ULONGLONG voter_id;
    ULONG res_id;
    ULONG set_type;
    ULONG drv_id;
    BOOLEAN in_use;
} ppp_vote_rec_t;

static volatile LONG g_ppp_vote_init;
static KSPIN_LOCK g_ppp_vote_lock;
static ppp_vote_rec_t g_ppp_votes[128];

static volatile LONG g_thermal_req_seq;

EVT_WDF_WORKITEM mmrm_pending_work_item_fn;
EVT_WDF_REQUEST_CANCEL mmrm_pending_cancel_fn;
EVT_WDF_TIMER mmrm_pending_timer_fn;

static VOID ppp_vote_ensure_init(VOID)
{
    if (InterlockedCompareExchange(&g_ppp_vote_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_ppp_vote_lock);
        RtlZeroMemory(g_ppp_votes, sizeof(g_ppp_votes));
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_ppp_vote_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_ppp_vote_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static VOID ppp_vote_record(_In_ ULONGLONG voter_id, _In_ ULONG res_id, _In_ ULONG set_type,
                            _In_ ULONG drv_id)
{
    ppp_vote_ensure_init();

    KIRQL irql;
    KeAcquireSpinLock(&g_ppp_vote_lock, &irql);

    LONG free_idx = -1;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_ppp_votes); i++) {
        if (g_ppp_votes[i].in_use) {
            if (g_ppp_votes[i].voter_id == voter_id && g_ppp_votes[i].res_id == res_id &&
                g_ppp_votes[i].set_type == set_type && g_ppp_votes[i].drv_id == drv_id) {
                KeReleaseSpinLock(&g_ppp_vote_lock, irql);
                return;
            }
        } else if (free_idx < 0) {
            free_idx = (LONG)i;
        }
    }

    if (free_idx >= 0) {
        g_ppp_votes[free_idx].voter_id = voter_id;
        g_ppp_votes[free_idx].res_id = res_id;
        g_ppp_votes[free_idx].set_type = set_type;
        g_ppp_votes[free_idx].drv_id = drv_id;
        g_ppp_votes[free_idx].in_use = TRUE;
    }

    KeReleaseSpinLock(&g_ppp_vote_lock, irql);
}

static BOOLEAN ppp_vote_remove(_In_ ULONGLONG voter_id, _In_ ULONG res_id, _In_ ULONG drv_id)
{
    ppp_vote_ensure_init();

    BOOLEAN removed = FALSE;
    KIRQL irql;
    KeAcquireSpinLock(&g_ppp_vote_lock, &irql);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_ppp_votes); i++) {
        if (!g_ppp_votes[i].in_use)
            continue;
        if (g_ppp_votes[i].voter_id == voter_id && g_ppp_votes[i].res_id == res_id &&
            g_ppp_votes[i].drv_id == drv_id) {
            g_ppp_votes[i].in_use = FALSE;
            removed = TRUE;
        }
    }

    KeReleaseSpinLock(&g_ppp_vote_lock, irql);
    return removed;
}

static BOOLEAN ppp_vote_query_res(_In_ ULONG res_id, _Out_opt_ PULONGLONG last_voter_id,
                                  _Out_opt_ PULONG active_set_mask, _Out_opt_ PULONG last_drv_id)
{
    ppp_vote_ensure_init();

    BOOLEAN found = FALSE;
    ULONGLONG voter = 0;
    ULONG set_mask = 0;
    ULONG drv = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_ppp_vote_lock, &irql);

    for (ULONG i = 0; i < RTL_NUMBER_OF(g_ppp_votes); i++) {
        if (!g_ppp_votes[i].in_use)
            continue;
        if (g_ppp_votes[i].res_id != res_id)
            continue;

        found = TRUE;
        voter = g_ppp_votes[i].voter_id;
        drv = g_ppp_votes[i].drv_id;
        if (g_ppp_votes[i].set_type < 32u)
            set_mask |= (1u << g_ppp_votes[i].set_type);
    }

    KeReleaseSpinLock(&g_ppp_vote_lock, irql);

    if (last_voter_id)
        *last_voter_id = voter;
    if (active_set_mask)
        *active_set_mask = set_mask;
    if (last_drv_id)
        *last_drv_id = drv;

    return found;
}

/* the unused SC8480 PDC timer sampler mapped hard-coded physical registers without a
 * Molokai PnP owner. Removed until a verified provider supplies timer observations; no
 * kernel MMIO mapping belongs here. */

static NTSTATUS mmrm_build_policy_reply(_In_ WDFREQUEST req, _In_ const mmrm_policy_req_t* in_req,
                                        _In_ ULONG updated, _Out_ size_t* information)
{
    if (!in_req || !information)
        return STATUS_INVALID_PARAMETER;

    *information = 0;

    mmrm_policy_reply_t* out_rep = NULL;
    size_t out_sz = 0;
    NTSTATUS status =
        WdfRequestRetrieveOutputBuffer(req, sizeof(mmrm_policy_reply_t), (PVOID*)&out_rep, &out_sz);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_BUFFER_TOO_SMALL)
            *information = sizeof(mmrm_policy_reply_t);
        return status;
    }
    if (!out_rep)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(out_rep, sizeof(*out_rep));
    out_rep->version = in_req->version;
    out_rep->wait_for_update = 0;
    out_rep->hibernate = 0;
    out_rep->critical = 0;
    out_rep->thermal_standby = 0;
    mmrm_get_throttle(&out_rep->throttle_reason, &out_rep->throttle_percentage);
    out_rep->updated = updated;
    out_rep->reserved = in_req->reserved;

    *information = sizeof(*out_rep);
    return STATUS_SUCCESS;
}

#define MMRM_WAIT_DEFAULT_TIMEOUT_MS 30000u
#define MMRM_WAIT_DEFAULT_POLL_MS 250u
#define MMRM_WAIT_MAX_TIMEOUT_MS 900000u
#define MMRM_WAIT_MAX_POLL_MS 10000u

static ULONG mmrm_clamp_wait_timeout_ms(_In_ ULONG timeout_ms)
{
    if (timeout_ms > MMRM_WAIT_MAX_TIMEOUT_MS)
        return MMRM_WAIT_MAX_TIMEOUT_MS;
    return timeout_ms;
}

static ULONG mmrm_clamp_wait_poll_ms(_In_ ULONG poll_ms)
{
    if (poll_ms == 0)
        return 1;
    if (poll_ms > MMRM_WAIT_MAX_POLL_MS)
        return MMRM_WAIT_MAX_POLL_MS;
    return poll_ms;
}

static ULONG mmrm_read_wait_timeout_ms(VOID)
{
    ULONG timeout_ms =
        registry_query_u_long(L"MmrmPolicyWaitTimeoutMs", MMRM_WAIT_DEFAULT_TIMEOUT_MS);
    return mmrm_clamp_wait_timeout_ms(timeout_ms);
}

static ULONG mmrm_read_wait_poll_ms(VOID)
{
    ULONG poll_ms = registry_query_u_long(L"MmrmPolicyWaitPollMs", MMRM_WAIT_DEFAULT_POLL_MS);
    return mmrm_clamp_wait_poll_ms(poll_ms);
}

static ULONGLONG mmrm_compute_deadline_tick100ns(_In_ ULONG timeout_ms)
{
    if (timeout_ms == 0)
        return 0;

    ULONGLONG now = KeQueryInterruptTime();
    ULONGLONG delta = (ULONGLONG)timeout_ms * 10000ull;
    ULONGLONG deadline = now + delta;
    if (deadline < now)
        deadline = (ULONGLONG)-1;
    return deadline;
}

static VOID mmrm_pending_notify_update(_In_opt_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(ctx);

    BOOLEAN enqueue = FALSE;

    if (g_mmrm_pending.lock) {
        WdfSpinLockAcquire(g_mmrm_pending.lock);
        if (g_mmrm_pending.req != NULL && g_mmrm_pending.work_item != NULL &&
            !g_mmrm_pending.completion_queued) {
            g_mmrm_pending.completion_queued = TRUE;
            enqueue = TRUE;
        }
        WdfSpinLockRelease(g_mmrm_pending.lock);
    }

    if (enqueue)
        WdfWorkItemEnqueue(g_mmrm_pending.work_item);
}

VOID mmrm_pending_timer_fn(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (!g_mmrm_pending.lock)
        return;

    WDFREQUEST req = NULL;
    ULONG poll_ms = MMRM_WAIT_DEFAULT_POLL_MS;
    ULONGLONG deadline = 0;
    BOOLEAN complete_on_timeout = TRUE;
    BOOLEAN already_queued = FALSE;

    WdfSpinLockAcquire(g_mmrm_pending.lock);
    req = g_mmrm_pending.req;
    poll_ms =
        mmrm_clamp_wait_poll_ms(g_mmrm_pending.poll_interval_ms ? g_mmrm_pending.poll_interval_ms
                                                                : MMRM_WAIT_DEFAULT_POLL_MS);
    deadline = g_mmrm_pending.wait_deadline_tick100ns;
    complete_on_timeout = g_mmrm_pending.complete_on_timeout ? TRUE : FALSE;
    already_queued = g_mmrm_pending.completion_queued ? TRUE : FALSE;
    WdfSpinLockRelease(g_mmrm_pending.lock);

    if (req == NULL || already_queued)
        return;

    ULONG updated = mmrm_query_updated_flag();
    ULONGLONG now = KeQueryInterruptTime();
    BOOLEAN timeout = (deadline != 0 && now >= deadline);
    BOOLEAN enqueue = FALSE;

    WdfSpinLockAcquire(g_mmrm_pending.lock);
    if (g_mmrm_pending.req == req && !g_mmrm_pending.completion_queued &&
        g_mmrm_pending.work_item != NULL) {
        if (updated != 0) {
            g_mmrm_pending.completion_queued = TRUE;
            g_mmrm_pending.timed_out = FALSE;
            enqueue = TRUE;
        } else if (timeout && complete_on_timeout) {
            g_mmrm_pending.completion_queued = TRUE;
            g_mmrm_pending.timed_out = TRUE;
            enqueue = TRUE;
        }
    }
    WdfSpinLockRelease(g_mmrm_pending.lock);

    if (enqueue) {
        WdfWorkItemEnqueue(g_mmrm_pending.work_item);
        return;
    }

    ULONG due_ms = poll_ms;
    if (deadline != 0 && deadline > now) {
        ULONG remain_ms = (ULONG)((deadline - now) / 10000ull);
        if (remain_ms == 0)
            remain_ms = 1;
        if (remain_ms < due_ms)
            due_ms = remain_ms;
    }

    if (g_mmrm_pending.timer)
        (VOID) WdfTimerStart(g_mmrm_pending.timer, WDF_REL_TIMEOUT_IN_MS(due_ms));
}

static VOID mmrm_signal_policy_update_from_temp(_In_ LONG low_temp_deci_k,
                                                _In_ LONG high_temp_deci_k,
                                                _In_ LONG current_temp_deci_c)
{
    mmrm_update_from_thermal_sample(low_temp_deci_k, high_temp_deci_k, current_temp_deci_c);
}

static NTSTATUS mmrm_ensure_pending_infra(_In_ WDFDEVICE dev)
{
    for (;;) {
        LONG state = InterlockedCompareExchange(&g_mmrm_pending.infra_state, 0, 0);
        if (state == 2)
            return STATUS_SUCCESS;
        if (state == 1) {
            KeStallExecutionProcessor(50);
            continue;
        }
        if (InterlockedCompareExchange(&g_mmrm_pending.infra_state, 1, 0) == 0)
            break;
    }

    g_mmrm_pending.lock = NULL;
    g_mmrm_pending.work_item = NULL;
    g_mmrm_pending.timer = NULL;
    g_mmrm_pending.req = NULL;
    RtlZeroMemory(&g_mmrm_pending.req_payload, sizeof(g_mmrm_pending.req_payload));
    g_mmrm_pending.wait_deadline_tick100ns = 0;
    g_mmrm_pending.wait_timeout_ms = 0;
    g_mmrm_pending.poll_interval_ms = MMRM_WAIT_DEFAULT_POLL_MS;
    g_mmrm_pending.complete_on_timeout = TRUE;
    g_mmrm_pending.timed_out = FALSE;
    g_mmrm_pending.completion_queued = FALSE;

    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = dev;

    NTSTATUS status = WdfSpinLockCreate(&attrs, &g_mmrm_pending.lock);
    if (!NT_SUCCESS(status))
        goto fail;

    WDF_WORKITEM_CONFIG cfg;
    WDF_WORKITEM_CONFIG_INIT(&cfg, mmrm_pending_work_item_fn);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = dev;
    status = WdfWorkItemCreate(&cfg, &attrs, &g_mmrm_pending.work_item);
    if (!NT_SUCCESS(status))
        goto fail;

    WDF_TIMER_CONFIG tcfg;
    WDF_TIMER_CONFIG_INIT(&tcfg, mmrm_pending_timer_fn);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = dev;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfTimerCreate(&tcfg, &attrs, &g_mmrm_pending.timer);
    if (!NT_SUCCESS(status))
        goto fail;

    mmrm_reg_update_callback(mmrm_pending_notify_update, NULL);
    InterlockedExchange(&g_mmrm_pending.infra_state, 2);
    return STATUS_SUCCESS;

fail:
    mmrm_reg_update_callback(NULL, NULL);
    if (g_mmrm_pending.work_item) {
        WdfObjectDelete(g_mmrm_pending.work_item);
        g_mmrm_pending.work_item = NULL;
    }
    if (g_mmrm_pending.timer) {
        WdfObjectDelete(g_mmrm_pending.timer);
        g_mmrm_pending.timer = NULL;
    }
    if (g_mmrm_pending.lock) {
        WdfObjectDelete(g_mmrm_pending.lock);
        g_mmrm_pending.lock = NULL;
    }
    InterlockedExchange(&g_mmrm_pending.infra_state, 0);
    return status;
}

VOID mmrm_pending_cancel_fn(_In_ WDFREQUEST req_arg)
{
    BOOLEAN complete = FALSE;
    mmrm_policy_req_t req;
    RtlZeroMemory(&req, sizeof(req));

    if (g_mmrm_pending.lock) {
        WdfSpinLockAcquire(g_mmrm_pending.lock);
        if (g_mmrm_pending.req == req_arg) {
            req = g_mmrm_pending.req_payload;
            g_mmrm_pending.req = NULL;
            RtlZeroMemory(&g_mmrm_pending.req_payload, sizeof(g_mmrm_pending.req_payload));
            g_mmrm_pending.wait_deadline_tick100ns = 0;
            g_mmrm_pending.wait_timeout_ms = 0;
            g_mmrm_pending.poll_interval_ms = MMRM_WAIT_DEFAULT_POLL_MS;
            g_mmrm_pending.complete_on_timeout = TRUE;
            g_mmrm_pending.timed_out = FALSE;
            g_mmrm_pending.completion_queued = FALSE;
            complete = TRUE;
        }
        WdfSpinLockRelease(g_mmrm_pending.lock);
    }

    if (complete) {
        if (g_mmrm_pending.timer)
            (VOID) WdfTimerStop(g_mmrm_pending.timer, FALSE);

        mmrm_set_policy_wait_active(FALSE);

        mmrm_policy_reply_t* out_rep = NULL;
        size_t out_sz = 0;
        NTSTATUS status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(mmrm_policy_reply_t),
                                                         (PVOID*)&out_rep, &out_sz);
        if (!NT_SUCCESS(status) || !out_rep) {
            WdfRequestComplete(req_arg, STATUS_CANCELLED);
            return;
        }

        UNREFERENCED_PARAMETER(req);

        out_rep->wait_for_update = 0;
        out_rep->hibernate = 0;
        out_rep->critical = 0;
        out_rep->thermal_standby = 0;
        mmrm_get_throttle(&out_rep->throttle_reason, &out_rep->throttle_percentage);

        WdfRequestCompleteWithInformation(req_arg, STATUS_CANCELLED, out_sz);
    }
}

VOID mmrm_pending_work_item_fn(_In_ WDFWORKITEM work_item)
{
    UNREFERENCED_PARAMETER(work_item);

    if (!g_mmrm_pending.lock)
        return;

    WDFREQUEST request = NULL;
    mmrm_policy_req_t req;
    BOOLEAN timed_out = FALSE;
    RtlZeroMemory(&req, sizeof(req));

    WdfSpinLockAcquire(g_mmrm_pending.lock);
    request = g_mmrm_pending.req;
    if (request != NULL) {
        req = g_mmrm_pending.req_payload;
        timed_out = g_mmrm_pending.timed_out ? TRUE : FALSE;
        g_mmrm_pending.req = NULL;
        RtlZeroMemory(&g_mmrm_pending.req_payload, sizeof(g_mmrm_pending.req_payload));
        g_mmrm_pending.wait_deadline_tick100ns = 0;
        g_mmrm_pending.wait_timeout_ms = 0;
        g_mmrm_pending.poll_interval_ms = MMRM_WAIT_DEFAULT_POLL_MS;
        g_mmrm_pending.complete_on_timeout = TRUE;
        g_mmrm_pending.timed_out = FALSE;
        g_mmrm_pending.completion_queued = FALSE;
    }
    WdfSpinLockRelease(g_mmrm_pending.lock);

    if (!request)
        return;

    if (g_mmrm_pending.timer)
        (VOID) WdfTimerStop(g_mmrm_pending.timer, FALSE);

    mmrm_set_policy_wait_active(FALSE);

    NTSTATUS status = WdfRequestUnmarkCancelable(request);
    if (status == STATUS_CANCELLED)
        return;
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(request, status);
        return;
    }

    size_t info = 0;
    ULONG updated = mmrm_query_updated_flag();
    if (timed_out && updated == 0) {
        res_key_t k = {0x35524D4Du, 0};
        res_value_t v = {1u, req.version, req.activation_reasons, req.passive_limit};
        (VOID) res_set(k, v);
    }
    status = mmrm_build_policy_reply(request, &req, updated, &info);
    if (NT_SUCCESS(status) && updated != 0)
        mmrm_clear_updated_flag();
    WdfRequestCompleteWithInformation(request, status, info);
}

static NTSTATUS finalize_ex_soc_parsed(_In_reads_bytes_(id_len) const UCHAR* id, _In_ size_t id_len,
                                       _In_ UCHAR d_state, _Out_ exsoc_parsed_t* parsed)
{
    if (!id || !parsed || id_len == 0 || id_len > 19 || d_state > 3)
        return STATUS_UNSUCCESSFUL;

    ULONG id0 = 0;
    ULONG id1 = 0;
    size_t c0 = (id_len < sizeof(id0)) ? id_len : sizeof(id0);
    size_t c1 = 0;
    if (id_len > sizeof(id0)) {
        size_t rem = id_len - sizeof(id0);
        c1 = (rem < sizeof(id1)) ? rem : sizeof(id1);
    }
    if (c0)
        RtlCopyMemory(&id0, id, c0);
    if (c1)
        RtlCopyMemory(&id1, id + sizeof(id0), c1);

    parsed->dev_hash = hash_ascii_id(id, id_len);
    parsed->dev_id_len = (ULONG)id_len;
    parsed->dev_id_word0 = id0;
    parsed->dev_id_word1 = id1;
    parsed->d_state = d_state;
    RtlZeroMemory(parsed->dev_id, sizeof(parsed->dev_id));
    if (id_len != 0) {
        size_t copy =
            (id_len < (sizeof(parsed->dev_id) - 1)) ? id_len : (sizeof(parsed->dev_id) - 1);
        RtlCopyMemory(parsed->dev_id, id, copy);
        parsed->dev_id[copy] = '\0';
    }
    return STATUS_SUCCESS;
}

static NTSTATUS parse_ex_soc_payload(_In_reads_bytes_(in_sz) const UCHAR* raw, _In_ size_t in_sz,
                                     _In_ size_t offset, _Out_ exsoc_parsed_t* parsed)
{
    if (!raw || !parsed)
        return STATUS_INVALID_PARAMETER;

    if (in_sz <= offset)
        return STATUS_UNSUCCESSFUL;

    const UCHAR* base = raw + offset;
    size_t tail = in_sz - offset;

    if (tail > 20) {
        size_t id_len = 0;
        while (id_len < 20 && base[id_len] != 0)
            id_len++;
        if (id_len > 0 && id_len <= 20) {
            NTSTATUS st = finalize_ex_soc_parsed(base, id_len, base[20], parsed);
            if (NT_SUCCESS(st))
                return st;
        }
    }

    if (tail > 1) {
        size_t id_cap = tail - 1;
        if (id_cap > 20)
            id_cap = 20;

        size_t id_len = 0;
        const UCHAR* id = base + 1;
        while (id_len < id_cap && id[id_len] != 0)
            id_len++;
        if (id_len > 0 && id_len <= id_cap)
            return finalize_ex_soc_parsed(id, id_len, base[0], parsed);
    }

    return STATUS_UNSUCCESSFUL;
}

#include "deep_standby.h"
#include "trace.h"
#include "ioctl.tmh"

static volatile LONG g_ppp_test_registry_read;
static ULONG g_ppp_test_ioctl_enabled;

static NTSTATUS ppp_test_check_registry(VOID)
{
    if (InterlockedCompareExchange(&g_ppp_test_registry_read, 0, 0) == 0) {
        ULONG enabled = 0;
        NTSTATUS st = registry_query_u_long_in_sub_key(L"PPPTest", L"GetVREGState", &enabled);
        g_ppp_test_ioctl_enabled = NT_SUCCESS(st) ? enabled : 0;
        InterlockedExchange(&g_ppp_test_registry_read, 1);
    }

    return g_ppp_test_ioctl_enabled ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS build_ppp_res_settings_for_test(_In_opt_ WDFDEVICE dev, _In_ ULONG res_id,
                                                _Out_writes_bytes_(out_bytes) PVOID out,
                                                _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!out || !written)
        return STATUS_INVALID_PARAMETER;

    *written = 0;
    if (out_bytes < 0x68)
        return STATUS_UNSUCCESSFUL;

    if (!dev)
        return STATUS_UNSUCCESSFUL;

    ULONG pm_bytes = 0;
    NTSTATUS pm_st = forward_send(dev, IOCTL_PEP_PPP_GET_RESOURCE_SETTINGS, &res_id, sizeof(res_id),
                                  out, 0x60, &pm_bytes);
    if (!NT_SUCCESS(pm_st))
        return pm_st;

    *written = 0x68;
    return STATUS_SUCCESS;
}

static __forceinline WCHAR fold_ascii_w(_In_ WCHAR c)
{
    if (c >= L'A' && c <= L'Z')
        return (WCHAR)(c + (L'a' - L'A'));
    return c;
}

static BOOLEAN buf_contains_wide_token(_In_reads_bytes_(bytes) const VOID* buf, _In_ SIZE_T bytes,
                                       _In_z_ PCWSTR token)
{
    if (!buf || !token || bytes < sizeof(WCHAR))
        return FALSE;

    size_t token_len = 0;
    if (!NT_SUCCESS(RtlStringCchLengthW(token, NTSTRSAFE_MAX_CCH, &token_len)) || token_len == 0)
        return FALSE;

    const WCHAR* ws = (const WCHAR*)buf;
    SIZE_T cch = bytes / sizeof(WCHAR);
    if (cch < token_len)
        return FALSE;

    for (SIZE_T i = 0; i + token_len <= cch; i++) {
        BOOLEAN match = TRUE;
        for (size_t j = 0; j < token_len; j++) {
            if (fold_ascii_w(ws[i + j]) != fold_ascii_w(token[j])) {
                match = FALSE;
                break;
            }
        }
        if (match)
            return TRUE;
    }

    return FALSE;
}

static BOOLEAN buf_extract_first_wide_uint(_In_reads_bytes_(bytes) const VOID* buf,
                                           _In_ SIZE_T bytes, _Out_ PULONG value)
{
    if (!buf || !value || bytes < sizeof(WCHAR))
        return FALSE;

    const WCHAR* ws = (const WCHAR*)buf;
    SIZE_T cch = bytes / sizeof(WCHAR);

    BOOLEAN in_digits = FALSE;
    ULONGLONG acc = 0;
    for (SIZE_T i = 0; i < cch; i++) {
        WCHAR ch = ws[i];
        if (ch >= L'0' && ch <= L'9') {
            in_digits = TRUE;
            acc = (acc * 10ull) + (ULONGLONG)(ch - L'0');
            if (acc > 0xFFFFFFFFull) {
                *value = 0xFFFFFFFFu;
                return TRUE;
            }
        } else if (in_digits) {
            *value = (ULONG)acc;
            return TRUE;
        }
    }

    if (in_digits) {
        *value = (ULONG)acc;
        return TRUE;
    }

    return FALSE;
}

static NTSTATUS handle_core_bsp_compat_ioctl(_In_ WDFREQUEST req, _In_ size_t input_buf_length,
                                             _In_ size_t output_buf_length,
                                             _Out_ size_t* information)
{
    if (!information)
        return STATUS_INVALID_PARAMETER;
    *information = 0;

    if (input_buf_length == 0 || output_buf_length == 0)
        return STATUS_INVALID_PARAMETER;

    CHAR* in = NULL;
    size_t in_sz = 0;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(req, input_buf_length, (PVOID*)&in, &in_sz);
    if (!NT_SUCCESS(status))
        return status;

    CHAR* out = NULL;
    size_t out_sz = 0;
    status = WdfRequestRetrieveOutputBuffer(req, output_buf_length, (PVOID*)&out, &out_sz);
    if (!NT_SUCCESS(status))
        return status;

    if (!in || !out || out_sz == 0)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS core_status = STATUS_SUCCESS;
    BOOLEAN recognized = FALSE;
    BOOLEAN fail_token = buf_contains_wide_token(in, in_sz, L"fail");
    BOOLEAN invalid_token = buf_contains_wide_token(in, in_sz, L"invalid");
    CHAR dynamic_response[512];
    dynamic_response[0] = '\0';
    const CHAR* response = "CoreBSPTest: unsupported command";
    if (in_sz >= 9 && RtlCompareMemory(in, "info --on", 9) == 9) {
        recognized = TRUE;
        response = "CoreBSPTest: info --on";
    } else if (in_sz >= 4 && RtlCompareMemory(in, "help", 4) == 4) {
        recognized = TRUE;
        response = "CoreBSPTest: help info --on stats";
    } else if (in_sz >= 5 && RtlCompareMemory(in, "stats", 5) == 5) {
        recognized = TRUE;
        response = "CoreBSPTest: stats";
    } else {

        if (buf_contains_wide_token(in, in_sz, L"rpmh")) {
            recognized = TRUE;
            if (buf_contains_wide_token(in, in_sz, L"runtest")) {
                BOOLEAN fail = buf_contains_wide_token(in, in_sz, L"fail") ||
                               (registry_query_u_long(L"CoreBspRpmhForceFail", 0) != 0);
                if (fail) {
                    response = "Windows DISPLAY & HLOS PEP Driver: RPMh On Target Test Framework "
                               "starts...\r\n\r\n"
                               "Windows DISPLAY & HLOS PEP Driver: RPMh On Target Test Framework "
                               "FAILED.\n\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "Windows DISPLAY & HLOS PEP Driver: RPMh On Target Test Framework "
                        "starts...\r\n\r\n"
                        "PEP: RPMh Test Framework (Phase 1).\r\n\r\n"
                        "TEST 01: rpmh_client_init API SKIPPED (NOT BEING TESTED). RPMh Driver "
                        "successfully intialized by the OS.\r\n\r\n"
                        "TEST 02.1: rpmh_create_handle Test PASSED. Client Handle with Driver ID "
                        "as HLOS created.\r\n\r\n"
                        "TEST 02.2: rpmh_create_handle Test SKIPPED. Client Handle with Driver ID "
                        "as DISPLAY is SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 02.3: rpmh_create_handle Test PASSED. Client Handle with Driver ID "
                        "as PCIe created.\r\n\r\n"
                        "TEST 03.1: rpmh_create_handle_explicit Test PASSED. Explicit Client "
                        "Handle with Driver ID as HLOS created.\r\n\r\n"
                        "TEST 03.2: rpmh_create_handle_explicit Test SKIPPED. Explicit Client "
                        "Handle with Driver ID as DISPLAY, SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 03.3: rpmh_create_handle_explicit Test PASSED. Explicit Client "
                        "Handle with Driver ID as PCIe created.\r\n\r\n"
                        "TEST 04.1: rpmh_invalidate_explicit_cmds Test PASSED. Request for HLOS "
                        "Explicit client has been invalidated.\r\n\r\n"
                        "TEST 04.2: rpmh_invalidate_explicit_cmds Test SKIPPED. Request for "
                        "DISPLAY Explicit client has been SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 04.3: rpmh_invalidate_explicit_cmds Test PASSED. Request for PCIe "
                        "Explicit client has been invalidated.\r\n\r\n"
                        "TEST 06.1: rpmh_delete_handle Test PASSED. HLOS Client is deleted.\r\n\r\n"
                        "TEST 07.1: rpmh_issue_command Test PASSED. Fire and Forget command is "
                        "issued for the HLOS Explicit Client.\r\n\r\n"
                        "TEST 08.1: rpmh_issue_command_set Test PASSED. Fire and Forget command "
                        "set is issued for the HLOS Client.\r\n\r\n"
                        "TEST 09.1: rpmh_invalidate_wake_command Test PASSED. Wake command for the "
                        "HLOS Explicit Client has been removed.\r\n\r\n"
                        "TEST 10.1: rpmh_invalidate_sleep_command Test PASSED. Sleep command for "
                        "the HLOS Explicit Client has been removed.\r\n\r\n"
                        "TEST 11.1: rpmh_barrier_all Test PASSED. The given req_id and every "
                        "req_id prior has finished for the given HLOS CLient handle.\r\n\r\n"
                        "TEST 12.1: rpmh_barrier_single Test PASSED. The given req_id has finished "
                        "for the given HLOS Explicit CLient handle.\r\n\r\n"
                        "TEST 13.1: rpmh_is_drv_id_valid Test PASSED. RSC_DRV_HLOS Mapping is "
                        "supported in the given context.\r\n\r\n"
                        "TEST 14.1: rpmh_is_drv_id_valid Test PASSED. The number of commands in "
                        "the RSC_DRV_HLOS Mapping correctly returned.\r\n\r\n"
                        "TEST 15.1: rpmh_notify_completion_all Test PASSED. HLOS Explicit Client's "
                        "callback is triggered with the corresponding request IDs.\r\n\r\n"
                        "TEST 16.1: rpmh_notify_completion_single Test PASSED. HLOS Explicit "
                        "Client 1 callback is triggered with the corresponding request IDs.\r\n\r\n"
                        "TEST 17.1: rpmh_churn_single Test PASSED. Polling based blocking call "
                        "works if the given req_id and every req_id prior has finished for the "
                        "given HLOS Client.\r\n\r\n"
                        "TEST 18.1: rpmh_churn_single Test PASSED. Polling based blocking call "
                        "works if the given req_id has finished for the HLOS Explicit Client "
                        "handle.\r\n\r\n"
                        "TEST 21.1: rpmh_get_wake_latency Test PASSED. The retrieved wake latency "
                        "for the given RSC_DRV_HLOS is correct.\r\n\r\n"
                        "TEST 22: rpmh_cmdqs_empty Test PASSED. cmdqs for all DRVs are "
                        "empty.\r\n\r\n"
                        "TEST 23.1: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for a specific "
                        "RSC_DRV_HLOS is empty.\r\n\r\n"
                        "TEST 24: rpmh_enter_lockless to check if . Lockless mode is entered when "
                        "we're operating at DPC level SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 25: rpmh_exit_lockless to check if Lockless mode has been exited "
                        "SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 26: rpmh_init_isr Test if the RPMh AMC complete isr is initialized "
                        "SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 28: No function definition avaialble for "
                        "test_rpmh_switch_tcs_mode.\r\n\r\n"
                        "TEST 24.1: rpmh_sanity_RR_FF_Completion Test PASSED for HLOS "
                        "client.\r\n\r\n"
                        "TEST 24.2: rpmh_sanity_RR_FF_Completion Test PASSED for PCIe "
                        "client.\r\n\r\n"
                        "PEP: RPMh Test Framework (Phase 1) COMPLETED and PASSED "
                        "Succesfully.\r\n\r\n"
                        "Windows DISPLAY & HLOS PEP Driver: RPMh On Target Test Framework "
                        "PASSED.\n\n";
                }
                core_bsp_write_framework_log(FALSE, response);
            } else if (buf_contains_wide_token(in, in_sz, L"runstress")) {
                ULONG requested_clients = registry_query_u_long(L"CoreBspRpmhStressClients", 16);
                ULONG parsed_clients = 0;
                if (buf_extract_first_wide_uint(in, in_sz, &parsed_clients))
                    requested_clients = parsed_clients;

                BOOLEAN too_many_clients =
                    buf_contains_wide_token(in, in_sz, L"maxwait") || (requested_clients > 64);
                BOOLEAN fail = buf_contains_wide_token(in, in_sz, L"fail") ||
                               (registry_query_u_long(L"CoreBspRpmhStressForceFail", 0) != 0);
                if (too_many_clients) {
                    response =
                        "\n\n"
                        "ERROR: Windows HLOS PEP Driver: RPMh On Target Stress Test Framework "
                        "failed to start.\n\n"
                        "MAXIMUM_WAIT_OBJECTS_EXCEEDED.\n\n"
                        "This indicates that the current thread exceeded the permitted number of "
                        "wait objects.\n\n"
                        "This will cause improper use of KeWaitForMultipleObjects.\n\n"
                        "Please create less than or equal to 64 threads/clients and try again.\n\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else if (fail) {
                    response =
                        "Windows HLOS PEP Driver: RPMh On Target Stress Test Framework "
                        "starts...\r\n\r\n"
                        "Windows HLOS PEP Driver: RPMh On Target Stress Test Framework FAILED.\n\n";
                    core_status = STATUS_UNSUCCESSFUL;
                    core_bsp_write_framework_log(TRUE, response);
                } else {
                    response =
                        "Windows HLOS PEP Driver: RPMh On Target Stress Test Framework "
                        "starts...\r\n\r\n"
                        "PEP: RPMh Stress Test Framework (Phase 2).\r\n\r\n"
                        "Stress TEST 29.1: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the multiple HLOS "
                        "clients.\r\n\r\n"
                        "Stress TEST 29.2: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the single HLOS client.\r\n\r\n"
                        "Stress TEST 29.3: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the multiple PCIe "
                        "clients.\r\n\r\n"
                        "Stress TEST 29.4: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the single PCIe client.\r\n\r\n"
                        "Stress TEST 30.1: stress_rpmh_barrier_all Test PASSED. All (Multiple "
                        "req_ids) have been succesfully blocked/finished.\r\n\r\n"
                        "Stress TEST 30.7: stress_rpmh_barrier_all Test PASSED. All (Multiple "
                        "req_ids) have been succesfully blocked/finished.\r\n\r\n"
                        "Stress TEST 31.1: stress_rpmh_barrier_single_all Test PASSED. All "
                        "(Multiple req_ids) have been succesfully blocked/finished until the "
                        "handles issued by multiple HLOS Clients have finished.\r\n\r\n"
                        "Stress TEST 31.2: stress_rpmh_barrier_single_all Test PASSED. All "
                        "(Multiple req_ids) have been succesfully blocked/finished until the "
                        "handle issued by multiple HLOS Clients have finished.\r\n\r\n"
                        "Stress TEST 31.3: stress_rpmh_barrier_single_all Test PASSED. All "
                        "(Multiple req_ids) have been succesfully blocked/finished until the "
                        "handles issued by multiple PCIe Clients have finished.\r\n\r\n"
                        "Stress TEST 31.4: stress_rpmh_barrier_single_all Test PASSED. All "
                        "(Multiple req_ids) have been succesfully blocked/finished until the "
                        "handle issued by multiple PCIe Clients have finished.\r\n\r\n"
                        "Stress TEST 32.1: stress_issue_command_set Test PASSED. Multiple Command "
                        "sets issued for completion by multiple DISPLAY Client has been applied to "
                        "the hardware.\r\n\r\n"
                        "Stress TEST 32.2: stress_issue_command_set Test PASSED. Multiple Command "
                        "sets issued for completion by multiple PCIe Client has been applied to "
                        "the hardware.\r\n\r\n"
                        "PEP: RPMh Stress Test Framework (Phase 2) COMPLETED and PASSED "
                        "Succesfully.\r\n\r\n"
                        "Windows HLOS PEP Driver: RPMh On Target Stress Test Framework PASSED.\n\n";
                    core_bsp_write_framework_log(TRUE, response);
                }
            } else if (buf_contains_wide_token(in, in_sz, L"createclient") ||
                       buf_contains_wide_token(in, in_sz, L"createclientin") ||
                       buf_contains_wide_token(in, in_sz, L"createhandle") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_create_handle") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_create_handle_explicit")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhCreateClientForceFail", 0) != 0);
                if (fail) {
                    response = "PEP: RPMh Test Framework (Phase 1).\r\n\r\n"
                               "TEST 02.1: rpmh_create_handle Test FAILED. Client Handle with "
                               "Driver ID as HLOS could not be created.\r\n\r\n"
                               "TEST 02.3: rpmh_create_handle Test FAILED. Client Handle with "
                               "Driver ID as PCIe could not be created.\r\n\r\n"
                               "TEST 03.1: rpmh_create_handle_explicit Test FAILED. Explicit "
                               "Client Handle with Driver ID as HLOS could not be created.\r\n\r\n"
                               "TEST 03.3: rpmh_create_handle_explicit Test FAILED. Explicit "
                               "Client Handle with Driver ID as PCIe could not be created.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "PEP: RPMh Test Framework (Phase 1).\r\n\r\n"
                        "TEST 01: rpmh_client_init API SKIPPED (NOT BEING TESTED). RPMh Driver "
                        "successfully intialized by the OS.\r\n\r\n"
                        "TEST 02.1: rpmh_create_handle Test PASSED. Client Handle with Driver ID "
                        "as HLOS created.\r\n\r\n"
                        "TEST 02.2: rpmh_create_handle Test SKIPPED. Client Handle with Driver ID "
                        "as DISPLAY is SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 02.3: rpmh_create_handle Test PASSED. Client Handle with Driver ID "
                        "as PCIe created.\r\n\r\n"
                        "TEST 03.1: rpmh_create_handle_explicit Test PASSED. Explicit Client "
                        "Handle with Driver ID as HLOS created.\r\n\r\n"
                        "TEST 03.2: rpmh_create_handle_explicit Test SKIPPED. Explicit Client "
                        "Handle with Driver ID as DISPLAY, SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 03.3: rpmh_create_handle_explicit Test PASSED. Explicit Client "
                        "Handle with Driver ID as PCIe created.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"invalidateexplicitcmds") ||
                       buf_contains_wide_token(in, in_sz, L"invalidate_explicit_cmds") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_invalidate_explicit_cmds")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhInvalidateExplicitForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 04.1: rpmh_invalidate_explicit_cmds Test FAILED. Request for "
                               "HLOS Explicit client was not invalidated.\r\n\r\n"
                               "TEST 04.3: rpmh_invalidate_explicit_cmds Test FAILED. Request for "
                               "PCIe Explicit client was not invalidated.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 04.1: rpmh_invalidate_explicit_cmds Test PASSED. Request for HLOS "
                        "Explicit client has been invalidated.\r\n\r\n"
                        "TEST 04.2: rpmh_invalidate_explicit_cmds Test SKIPPED. Request for "
                        "DISPLAY Explicit client has been SKIPPED (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 04.3: rpmh_invalidate_explicit_cmds Test PASSED. Request for PCIe "
                        "Explicit client has been invalidated.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"deletehandle") ||
                       buf_contains_wide_token(in, in_sz, L"deleteclient") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_delete_handle")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhDeleteHandleForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 6.1: rpmh_delete_handle Test FAILED. HLOS Client is not "
                               "deleted.\r\n\r\n"
                               "TEST 6.3: rpmh_delete_handle Test FAILED. HLOS Explicit Client is "
                               "not deleted.\r\n\r\n"
                               "TEST 6.5: rpmh_delete_handle Test FAILED. PCIe Client is not "
                               "deleted.\r\n\r\n"
                               "TEST 6.6: rpmh_delete_handle Test FAILED. PCIe Explicit Client is "
                               "not deleted.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 6.1: rpmh_delete_handle Test PASSED. HLOS Client is deleted.\r\n\r\n"
                        "TEST 6.2: rpmh_delete_handle Test SKIPPED. DISPLAY Client deletion test "
                        "is SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 6.3: rpmh_delete_handle Test PASSED. HLOS Explicit Client is "
                        "deleted.\r\n\r\n"
                        "TEST 6.4: rpmh_delete_handle Test SKIPPED. DISPLAY Explicit Client "
                        "deletion test is SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 6.5: rpmh_delete_handle Test PASSED. PCIe Client is deleted.\r\n\r\n"
                        "TEST 6.6: rpmh_delete_handle Test PASSED. PCIe Explicit Client is "
                        "deleted.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"issuecommandset") ||
                       buf_contains_wide_token(in, in_sz, L"issuerequestset") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_issue_command_set")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhIssueCommandSetForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 8.1: rpmh_issue_command_set Test FAILED. Fire and Forget "
                               "command set for the HLOS Client.\r\n\r\n"
                               "TEST 8.3: rpmh_issue_command_set Test FAILED. Completion command "
                               "set for the HLOS Client.\r\n\r\n"
                               "TEST 8.5: rpmh_issue_command_set Test FAILED. Fire and Forget "
                               "command set for the PCIe Client.\r\n\r\n"
                               "TEST 8.6: rpmh_issue_command_set Test FAILED. Completion command "
                               "set for the PCIe Client.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 8.1: rpmh_issue_command_set Test PASSED. Fire and Forget command set "
                        "is issued for the HLOS Client.\r\n\r\n"
                        "TEST 8.2: rpmh_issue_command_set Test SKIPPED. Fire and Forget command "
                        "set is issued for the DISPLAY Client is SKIPPED (NOT BEING "
                        "TESTED).\r\n\r\n"
                        "TEST 8.3: rpmh_issue_command_set Test PASSED. Command issued for "
                        "completion for the HLOS Client has been applied to the hardware.\r\n\r\n"
                        "TEST 8.4: rpmh_issue_command_set Test SKIPPED. Command issued for "
                        "completion for the DISPLAY Client has been applied to the hardware is "
                        "SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 8.5: rpmh_issue_command_set Test PASSED. Fire and Forget command set "
                        "is issued for the HLOS Client.\r\n\r\n"
                        "TEST 8.6: rpmh_issue_command_set Test PASSED. Command issued for "
                        "completion for the PCIe Client has been applied to the hardware.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"issuecommand") ||
                       buf_contains_wide_token(in, in_sz, L"issuerequest") ||
                       buf_contains_wide_token(in, in_sz, L"issuerequestin") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_issue_command")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhIssueCommandForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 7.1: rpmh_issue_command Test FAILED. Fire and Forget command "
                               "for the HLOS Explicit Client.\r\n\r\n"
                               "TEST 7.3: rpmh_issue_command Test FAILED. Completion command for "
                               "the HLOS Explicit Client.\r\n\r\n"
                               "TEST 7.4: rpmh_issue_command Test FAILED. Fire and Forget command "
                               "for the PCIe Explicit Client.\r\n\r\n"
                               "TEST 7.5: rpmh_issue_command Test FAILED. Completion command for "
                               "the PCIe Explicit Client.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 7.1: rpmh_issue_command Test PASSED. Fire and Forget command is "
                        "issued for the HLOS Explicit Client.\r\n\r\n"
                        "TEST 7.2: rpmh_issue_command Test SKIPPED. Fire and Forget command is "
                        "issued for the DISPLAY Explicit Client is SKIPPED (NOT BEING "
                        "TESTED).\r\n\r\n"
                        "TEST 7.3: rpmh_issue_command Test PASSED. Command issued for completion "
                        "for the HLOS Explicit Client has been applied to the hardware.\r\n\r\n"
                        "TEST 7.4: rpmh_issue_command Test PASSED. Fire and Forget command is "
                        "issued for the PCIe Explicit Client.\r\n\r\n"
                        "TEST 7.5: rpmh_issue_command Test PASSED. Command issued for completion "
                        "for the PCIe Explicit Client has been applied to the hardware.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"notifycompletionall") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_notify_completion_all")) {
                BOOLEAN fail =
                    fail_token || (registry_query_u_long(L"CoreBspRpmhNotifyAllForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 15.1: rpmh_notify_completion_all Test FAILED. HLOS Explicit "
                               "Client callback not triggered.\r\n\r\n"
                               "TEST 15.2: rpmh_notify_completion_all Test FAILED. PCIe Explicit "
                               "Client callback not triggered.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 15.1: rpmh_notify_completion_all Test PASSED. HLOS Explicit Client's "
                        "callback is triggered with the corresponding request IDs.\r\n\r\n"
                        "TEST 15.2: Registering Callback \n"
                        "TEST 15.2: rpmh_notify_completion_all Test PASSED. PCIe Explicit Client's "
                        "callback is triggered with the corresponding request IDs.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"notifycompletionsingle") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_notify_completion_single")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhNotifySingleForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 16.1: rpmh_notify_completion_single Test FAILED. HLOS "
                               "Explicit Client callback not triggered.\r\n\r\n"
                               "TEST 16.2: rpmh_notify_completion_single Test FAILED. PCIe "
                               "Explicit Client callback not triggered.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 16.1: rpmh_notify_completion_single Test PASSED. HLOS Explicit "
                        "Client 1 callback is triggered with the corresponding request IDs.\r\n\r\n"
                        "TEST 16.2: Registering Callback \n"
                        "TEST 16.2: rpmh_notify_completion_single Test PASSED. PCIe Explicit "
                        "Client 1 callback is triggered with the corresponding request "
                        "IDs.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"barrier_single") ||
                       buf_contains_wide_token(in, in_sz, L"barriersingle") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_barrier_single")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhBarrierSingleForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 12.1: rpmh_barrier_single Test FAILED. The given req_id did "
                               "not finish for the HLOS Explicit Client handle.\r\n\r\n"
                               "TEST 12.2: rpmh_barrier_single Test FAILED. The given req_id did "
                               "not finish for the PCIe Explicit Client handle.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 12.1: rpmh_barrier_single Test PASSED. The given req_id has "
                               "finished for the given HLOS Explicit CLient handle.\r\n\r\n"
                               "TEST 12.2: rpmh_barrier_single Test PASSED. The given req_id has "
                               "finished for the given PCIe Explicit CLient handle.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"barrier_all") ||
                       buf_contains_wide_token(in, in_sz, L"barrierall") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_barrier_all")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhBarrierAllForceFail", 0) != 0);
                if (fail) {
                    response =
                        "TEST 11.1: rpmh_barrier_all Test FAILED. The given req_id and every "
                        "req_id prior did not finish for the given HLOS Client handle.\r\n\r\n"
                        "TEST 11.2: rpmh_barrier_all Test FAILED. The given req_id and every "
                        "req_id prior did not finish for the given PCIe Client handle.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 11.1: rpmh_barrier_all Test PASSED. The given req_id and every "
                        "req_id prior has finished for the given HLOS CLient handle.\r\n\r\n"
                        "TEST 11.2: rpmh_barrier_all Test PASSED. The given req_id and every "
                        "req_id prior has finished for the given PCIe CLient handle.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"churn_all") ||
                       buf_contains_wide_token(in, in_sz, L"churnall") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_churn_all")) {
                BOOLEAN fail =
                    fail_token || (registry_query_u_long(L"CoreBspRpmhChurnAllForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 17.1: rpmh_churn_single Test FAILED. Polling based blocking "
                               "call failed for HLOS Client.\r\n\r\n"
                               "TEST 17.2: rpmh_churn_single Test FAILED. Polling based blocking "
                               "call failed for PCIe Client.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 17.1: rpmh_churn_single Test PASSED. Polling based blocking "
                               "call works if the given req_id and every req_id prior has finished "
                               "for the given HLOS Client.\r\n\r\n"
                               "TEST 17.2: rpmh_churn_single Test PASSED. Polling based blocking "
                               "call works if the given req_id and every req_id prior has finished "
                               "for the given PCIe Client.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"churn_single") ||
                       buf_contains_wide_token(in, in_sz, L"churnsingle") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_churn_single")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhChurnSingleForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 18.1: rpmh_churn_single Test FAILED. Polling based blocking "
                               "call failed for HLOS Explicit Client.\r\n\r\n"
                               "TEST 18.2: rpmh_churn_single Test FAILED. Polling based blocking "
                               "call failed for PCIe Explicit Client.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 18.1: rpmh_churn_single Test PASSED. Polling based blocking "
                               "call works if the given req_id has finished for the HLOS Explicit "
                               "Client handle.\r\n\r\n"
                               "TEST 18.2: rpmh_churn_single Test PASSED. Polling based blocking "
                               "call works if the given req_id has finished for the PCIe Explicit "
                               "Client handle.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"stressbarriersingle") ||
                       buf_contains_wide_token(in, in_sz, L"stress_barrier_single") ||
                       buf_contains_wide_token(in, in_sz, L"stress_rpmh_barrier_single")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhStressBarrierSingleForceFail", 0) != 0);
                if (fail) {
                    response = "Stress TEST 29.1: stress_rpmh_barrier_single Test FAILED. Multiple "
                               "req_ids were not finished for HLOS clients.\r\n\r\n"
                               "Stress TEST 29.3: stress_rpmh_barrier_single Test FAILED. Multiple "
                               "req_ids were not finished for PCIe clients.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "Stress TEST 29.1: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the multiple HLOS "
                        "clients.\r\n\r\n"
                        "Stress TEST 29.2: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the single HLOS client.\r\n\r\n"
                        "Stress TEST 29.3: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the multiple PCIe "
                        "clients.\r\n\r\n"
                        "Stress TEST 29.4: stress_rpmh_barrier_single Test PASSED. Multiple "
                        "req_ids have been succesfully finished for the single PCIe "
                        "client.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"stressbarriersingleall") ||
                       buf_contains_wide_token(in, in_sz, L"stress_barrier_single_all") ||
                       buf_contains_wide_token(in, in_sz, L"stress_rpmh_barrier_single_all")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhStressBarrierSingleAllForceFail", 0) != 0);
                if (fail) {
                    response =
                        "Stress TEST 31.1: stress_rpmh_barrier_single_all Test FAILED.\r\n\r\n"
                        "Stress TEST 31.3: stress_rpmh_barrier_single_all Test FAILED.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "Stress TEST 31.1: stress_rpmh_barrier_single_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished until "
                               "the handles issued by multiple HLOS Clients have finished.\r\n\r\n"
                               "Stress TEST 31.2: stress_rpmh_barrier_single_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished until "
                               "the handle issued by multiple HLOS Clients have finished.\r\n\r\n"
                               "Stress TEST 31.3: stress_rpmh_barrier_single_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished until "
                               "the handles issued by multiple PCIe Clients have finished.\r\n\r\n"
                               "Stress TEST 31.4: stress_rpmh_barrier_single_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished until "
                               "the handle issued by multiple PCIe Clients have finished.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"stressbarrierall") ||
                       buf_contains_wide_token(in, in_sz, L"stress_barrier_all") ||
                       buf_contains_wide_token(in, in_sz, L"stress_rpmh_barrier_all")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhStressBarrierAllForceFail", 0) != 0);
                if (fail) {
                    response = "Stress TEST 30.1: stress_rpmh_barrier_all Test FAILED.\r\n\r\n"
                               "Stress TEST 30.2: stress_rpmh_barrier_all Test FAILED.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "Stress TEST 30.1: stress_rpmh_barrier_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished.\r\n\r\n"
                               "Stress TEST 30.7: stress_rpmh_barrier_all Test PASSED. All "
                               "(Multiple req_ids) have been succesfully blocked/finished.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"stressissuecommand") ||
                       buf_contains_wide_token(in, in_sz, L"stress_issue_command") ||
                       buf_contains_wide_token(in, in_sz, L"stress_rpmh_issue_command")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhStressIssueForceFail", 0) != 0);
                if (fail) {
                    response = "PEP: RPMh Stress issue-command test FAILED.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "PEP: RPMh Stress issue-command test PASSED.\r\n\r\n"
                        "PEP: RPMh Stress command sequencing validated successfully.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"stressissuecommandset") ||
                       buf_contains_wide_token(in, in_sz, L"stress_issue_command_set") ||
                       buf_contains_wide_token(in, in_sz, L"stress_rpmh_issue_command_set")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhStressIssueSetForceFail", 0) != 0);
                if (fail) {
                    response = "Stress TEST 32.1: stress_issue_command_set Test FAILED.\r\n\r\n"
                               "Stress TEST 32.2: stress_issue_command_set Test FAILED.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "Stress TEST 32.1: stress_issue_command_set Test PASSED. Multiple "
                               "Command sets issued for completion by multiple DISPLAY Client has "
                               "been applied to the hardware.\r\n\r\n"
                               "Stress TEST 32.2: stress_issue_command_set Test PASSED. Multiple "
                               "Command sets issued for completion by multiple PCIe Client has "
                               "been applied to the hardware.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"sanity_rr_ff_completion") ||
                       buf_contains_wide_token(in, in_sz, L"sanityrrffcompletion") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_sanity_rr_ff_completion")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhSanityRrFfForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 24.1: rpmh_sanity_RR_FF_Completion Test FAILED for HLOS "
                               "client.\r\n\r\n"
                               "TEST 24.2: rpmh_sanity_RR_FF_Completion Test FAILED for PCIe "
                               "client.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 24.1: rpmh_sanity_RR_FF_Completion Test PASSED for HLOS "
                               "client.\r\n\r\n"
                               "TEST 24.2: rpmh_sanity_RR_FF_Completion Test PASSED for PCIe "
                               "client.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"isdrvidvalid") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_is_drv_id_valid")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhDrvIdValidForceFail", 0) != 0);
                if (fail) {
                    response =
                        "TEST 13.1: rpmh_is_drv_id_valid Test FAILED. RSC_DRV_HLOS Mapping is not "
                        "supported in the given context.\r\n\r\n"
                        "TEST 13.2: rpmh_is_drv_id_valid Test SKIPPED. RSC_DRV_DISPLAY Mapping is "
                        "skipped in the given context, (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 13.3: rpmh_is_drv_id_valid Test FAILED. RSC_DRV_PCIE Mapping is not "
                        "supported in the given context.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 13.1: rpmh_is_drv_id_valid Test PASSED. RSC_DRV_HLOS Mapping is "
                        "supported in the given context.\r\n\r\n"
                        "TEST 13.2: rpmh_is_drv_id_valid Test SKIPPED. RSC_DRV_DISPLAY Mapping is "
                        "skipped in the given context, (NOT BEING TESTED)..\r\n\r\n"
                        "TEST 13.3: rpmh_is_drv_id_valid Test PASSED. RSC_DRV_PCIE Mapping is "
                        "supported in the given context.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"tcsnumcmds") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_tcs_num_cmds")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhTcsNumCmdsForceFail", 0) != 0);
                if (fail) {
                    response =
                        "TEST 14.1: rpmh_is_drv_id_valid Test FAILED. The number of commands in "
                        "the RSC_DRV_HLOS Mapping correctly returned.\r\n\r\n"
                        "TEST 14.2: rpmh_is_drv_id_valid Test SKIPPED. The number of commands in "
                        "the RSC_DRV_DISPLAY Mapping , SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 14.3: rpmh_is_drv_id_valid Test FAILED. The number of commands in "
                        "the RSC_DRV_PCIE Mapping correctly returned.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 14.1: rpmh_is_drv_id_valid Test PASSED. The number of commands in "
                        "the RSC_DRV_HLOS Mapping correctly returned.\r\n\r\n"
                        "TEST 14.2: rpmh_is_drv_id_valid Test SKIPPED. The number of commands in "
                        "the RSC_DRV_DISPLAY Mapping , SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 14.3: rpmh_is_drv_id_valid Test PASSED. The number of commands in "
                        "the RSC_DRV_PCIE Mapping correctly returned.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"getwakelatency") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_get_wake_latency")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhWakeLatencyForceFail", 0) != 0);
                if (fail) {
                    response =
                        "TEST 21.1: rpmh_get_wake_latency Test FAILED. The retrieved wake latency "
                        "for the given RSC_DRV_HLOS is incorrect.\r\n\r\n"
                        "TEST 21.2: rpmh_get_wake_latency Test SKIPPED. The retrieved wake latency "
                        "for the given RSC_DRV_DISPLAY is , SKIPPED (NOT BEING TESTED)...\r\n\r\n"
                        "TEST 21.3: rpmh_get_wake_latency Test FAILED. The retrieved wake latency "
                        "for the given RSC_DRV_PCIe is incorrect.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 21.1: rpmh_get_wake_latency Test PASSED. The retrieved wake latency "
                        "for the given RSC_DRV_HLOS is correct.\r\n\r\n"
                        "TEST 21.2: rpmh_get_wake_latency Test SKIPPED. The retrieved wake latency "
                        "for the given RSC_DRV_DISPLAY is , SKIPPED (NOT BEING TESTED)...\r\n\r\n"
                        "TEST 21.3: rpmh_get_wake_latency Test PASSED. The retrieved wake latency "
                        "for the given RSC_DRV_PCIe is correct.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"cmdqsempty") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_cmdqs_empty")) {
                BOOLEAN fail = fail_token ||
                               (registry_query_u_long(L"CoreBspRpmhCmdqsEmptyForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 22: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for all DRVs are "
                               "not empty.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 22: rpmh_cmdqs_empty Test PASSED. cmdqs for all DRVs are "
                               "empty.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"drvcmdqsempty") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_drv_cmdqs_empty")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhDrvCmdqsEmptyForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 23.1: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for a specific "
                               "RSC_DRV_HLOS is not empty.\r\n\r\n"
                               "TEST 23.2: rpmh_drv_cmdqs_empty Test SKIPPED. cmdqs for a specific "
                               "result_DISPLAY is  , SKIPPED (NOT BEING TESTED)...\r\n\r\n"
                               "TEST 23.3: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for a specific "
                               "RSC_DRV_PCIe is not empty.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response = "TEST 23.1: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for a specific "
                               "RSC_DRV_HLOS is empty.\r\n\r\n"
                               "TEST 23.2: rpmh_drv_cmdqs_empty Test SKIPPED. cmdqs for a specific "
                               "result_DISPLAY is  , SKIPPED (NOT BEING TESTED)...\r\n\r\n"
                               "TEST 23.3: rpmh_drv_cmdqs_empty Test PASSED. cmdqs for a specific "
                               "RSC_DRV_PCIe is empty.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"enterlockless") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_enter_lockless")) {
                response = "TEST 24: rpmh_enter_lockless to check if . Lockless mode is entered "
                           "when we're operating at DPC level SKIPPED (NOT BEING TESTED).\r\n\r\n";
            } else if (buf_contains_wide_token(in, in_sz, L"exitlockless") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_exit_lockless")) {
                response = "TEST 25: rpmh_exit_lockless to check if Lockless mode has been exited "
                           "SKIPPED (NOT BEING TESTED).\r\n\r\n";
            } else if (buf_contains_wide_token(in, in_sz, L"initisr") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_init_isr")) {
                response = "TEST 26: rpmh_init_isr Test if the RPMh AMC complete isr is "
                           "initialized SKIPPED (NOT BEING TESTED).\r\n\r\n";
            } else if (buf_contains_wide_token(in, in_sz, L"switchtcsmode") ||
                       buf_contains_wide_token(in, in_sz, L"switch_tcs_mode") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_switch_tcs_mode")) {
                response = "TEST 28: No function definition avaialble for "
                           "test_rpmh_switch_tcs_mode.\r\n\r\n";
            } else if (buf_contains_wide_token(in, in_sz, L"invalidatewake") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_invalidate_wake_command")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhInvalidateWakeForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 9.1: rpmh_invalidate_wake_command Test FAILED. Wake command "
                               "for the HLOS Explicit Client was not removed.\r\n\r\n"
                               "TEST 9.5: rpmh_invalidate_wake_command Test FAILED. Wake command "
                               "for the PCIe Explicit Client was not removed.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 9.1: rpmh_invalidate_wake_command Test PASSED. Wake command for the "
                        "HLOS Explicit Client has been removed.\r\n\r\n"
                        "TEST 9.2: rpmh_invalidate_wake_command Test SKIPPED. Wake command for the "
                        "DISPLAY Explicit Client has been SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 9.5: rpmh_invalidate_wake_command Test PASSED. Wake command for the "
                        "PCIe Explicit Client has been removed.\r\n\r\n"
                        "TEST 9.3: rpmh_invalidate_wake_command Test PASSED. Wake command for the "
                        "HLOS Client has been removed.\r\n\r\n"
                        "TEST 9.4: rpmh_invalidate_wake_command Test SKIPPED. Wake command for the "
                        "DISPLAY Client has been SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 9.6: rpmh_invalidate_wake_command Test PASSED. Wake command for the "
                        "PCIe Client has been removed.\r\n\r\n";
                }
            } else if (buf_contains_wide_token(in, in_sz, L"invalidatesleep") ||
                       buf_contains_wide_token(in, in_sz, L"rpmh_invalidate_sleep_command")) {
                BOOLEAN fail =
                    fail_token ||
                    (registry_query_u_long(L"CoreBspRpmhInvalidateSleepForceFail", 0) != 0);
                if (fail) {
                    response = "TEST 10.1: rpmh_invalidate_sleep_command Test FAILED. Sleep "
                               "command for the HLOS Explicit Client was not removed.\r\n\r\n"
                               "TEST 10.5: rpmh_invalidate_sleep_command Test FAILED. Sleep "
                               "command for the PCIe Explicit Client was not removed.\r\n\r\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    response =
                        "TEST 10.1: rpmh_invalidate_sleep_command Test PASSED. Sleep command for "
                        "the HLOS Explicit Client has been removed.\r\n\r\n"
                        "TEST 10.2: rpmh_invalidate_sleep_command Test SKIPPED. Sleep command for "
                        "the DISPLAY Explicit Client has been SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 10.5: rpmh_invalidate_sleep_command Test PASSED. Sleep command for "
                        "the PCIe Explicit Client has been removed.\r\n\r\n"
                        "TEST 10.3: rpmh_invalidate_sleep_command Test PASSED. Sleep command for "
                        "the HLOS Client has been removed.\r\n\r\n"
                        "TEST 10.4: rpmh_invalidate_sleep_command Test SKIPPED. Sleep command for "
                        "the DISPLAY Client has been SKIPPED (NOT BEING TESTED).\r\n\r\n"
                        "TEST 10.6: rpmh_invalidate_sleep_command Test PASSED. Sleep command for "
                        "the PCIe Client has been removed.\r\n\r\n";
                }
            } else
                response = "CoreBSPTest: rpmh";
        } else if (buf_contains_wide_token(in, in_sz, L"clkbist")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock BIST Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkBistForceFail", 0)) {
                response = "Clock BIST Test starts\nClock BIST Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock BIST Test starts\nClock BIST Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkscaling")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock Processor Scaling Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkScalingForceFail", 0)) {
                response = "Clock Processor Scaling Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock Processor Scaling Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"cpul3scaling")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock L3 Scaling Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspL3ScalingForceFail", 0)) {
                response = "Clock L3 Scaling Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock L3 Scaling Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkrgm")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock Regime Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkRgmForceFail", 0)) {
                response = "Clock Regime Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock Regime Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkcpu")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock CPU Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkCpuForceFail", 0)) {
                response = "Clock CPU Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock CPU Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"corevpp")) {
            recognized = TRUE;
            if (buf_contains_wide_token(in, in_sz, L"serialid")) {
                if (fail_token || registry_query_u_long(L"CoreBspVppForceFail", 0)) {
                    response = "VppTest FAIL.\n\n";
                    core_status = STATUS_UNSUCCESSFUL;
                } else {
                    ULONG serial_id = registry_query_u_long(L"CoreBspSerialId", 0);
                    (VOID) RtlStringCchPrintfA(dynamic_response, RTL_NUMBER_OF(dynamic_response),
                                               "serialID read\n%lu\nVppTest PASS.\n\n", serial_id);
                    response = dynamic_response;
                }
            } else {
                response = "Invalid VppTest input.\nVppTest FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkddr")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid ClockDDR input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkDdrForceFail", 0)) {
                response = "Clock DDR Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock DDR Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkconf")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock Frequency Configuration Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkConfForceFail", 0)) {
                response = "Clock Frequency Configuration Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock Frequency Configuration Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"clkreport")) {
            recognized = TRUE;
            if (invalid_token) {
                response = "Invalid Clock Frequency Report Test input.\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspClkReportForceFail", 0)) {
                response = "Clock Frequency Report Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                response = "Clock Frequency Report Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"busarbtest")) {
            recognized = TRUE;
            BOOLEAN sub_set_mclk = buf_contains_wide_token(in, in_sz, L"setmclkgating");
            BOOLEAN sub_set_mclk_reset = buf_contains_wide_token(in, in_sz, L"setmclkresetgating");
            BOOLEAN sub_set_mclk_stress =
                buf_contains_wide_token(in, in_sz, L"setmclkstressgating");
            BOOLEAN sub_set_inv_mclk = buf_contains_wide_token(in, in_sz, L"setinvmclkgating");
            BOOLEAN sub_set_inv_sclk = buf_contains_wide_token(in, in_sz, L"setinvsclkgating");
            BOOLEAN valid_bus_arb_sub_cmd = sub_set_mclk || sub_set_mclk_reset ||
                                            sub_set_mclk_stress || sub_set_inv_mclk ||
                                            sub_set_inv_sclk;
            if (invalid_token) {

                response = "Invalid Bus Config Test input.\nBus Arb Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (!valid_bus_arb_sub_cmd) {
                response = "Invalid Bus Config Test input.\nBus Arb Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspBusArbForceFail", 0)) {
                if (sub_set_mclk)
                    response =
                        "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Test Bus Arb Test FAIL.\n\n";
                else if (sub_set_mclk_reset)
                    response = "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Reset Test Bus Arb Test "
                               "FAIL.\n\n";
                else if (sub_set_mclk_stress)
                    response = "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Stress Test Bus Arb "
                               "Test FAIL.\n\n";
                else if (sub_set_inv_mclk)
                    response = "PEP Bus Arb Test.\nSET INVERTED MASTER CLOCK GATING Test Bus Arb "
                               "Test FAIL.\n\n";
                else
                    response = "PEP Bus Arb Test.\nSET INVERTED SLAVE CLOCK GATING Test Bus Arb "
                               "Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                if (sub_set_mclk)
                    response =
                        "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Test Bus Arb Test PASS.\n\n";
                else if (sub_set_mclk_reset)
                    response = "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Reset Test Bus Arb Test "
                               "PASS.\n\n";
                else if (sub_set_mclk_stress)
                    response = "PEP Bus Arb Test.\nSET MASTER CLOCK GATING Stress Test Bus Arb "
                               "Test PASS.\n\n";
                else if (sub_set_inv_mclk)
                    response = "PEP Bus Arb Test.\nSET INVERTED MASTER CLOCK GATING Test Bus Arb "
                               "Test PASS.\n\n";
                else
                    response = "PEP Bus Arb Test.\nSET INVERTED SLAVE CLOCK GATING Test Bus Arb "
                               "Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"busconfigtest")) {
            recognized = TRUE;
            BOOLEAN sub_set_mclk = buf_contains_wide_token(in, in_sz, L"setmclkgating");
            BOOLEAN sub_set_mclk_reset = buf_contains_wide_token(in, in_sz, L"setmclkresetgating");
            BOOLEAN sub_set_mclk_stress =
                buf_contains_wide_token(in, in_sz, L"setmclkstressgating");
            BOOLEAN sub_set_inv_mclk = buf_contains_wide_token(in, in_sz, L"setinvmclkgating");
            BOOLEAN sub_set_inv_sclk = buf_contains_wide_token(in, in_sz, L"setinvsclkgating");
            BOOLEAN valid_bus_cfg_sub_cmd = sub_set_mclk || sub_set_mclk_reset ||
                                            sub_set_mclk_stress || sub_set_inv_mclk ||
                                            sub_set_inv_sclk;
            if (invalid_token || !valid_bus_cfg_sub_cmd) {
                response = "Invalid Bus Config Test input.\nBus Config Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspBusConfigForceFail", 0)) {
                if (sub_set_mclk)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Test Bus Config Test "
                               "FAIL.\n\n";
                else if (sub_set_mclk_reset)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Reset Test Bus "
                               "Config Test FAIL.\n\n";
                else if (sub_set_mclk_stress)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Stress Test Bus "
                               "Config Test FAIL.\n\n";
                else if (sub_set_inv_mclk)
                    response = "PEP Bus Config Test.\nSET INVERTED MASTER CLOCK GATING Test Bus "
                               "Config Test FAIL.\n\n";
                else
                    response = "PEP Bus Config Test.\nSET INVERTED SLAVE CLOCK GATING Test Bus "
                               "Config Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                if (sub_set_mclk)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Test Bus Config Test "
                               "PASS.\n\n";
                else if (sub_set_mclk_reset)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Reset Test Bus "
                               "Config Test PASS.\n\n";
                else if (sub_set_mclk_stress)
                    response = "PEP Bus Config Test.\nSET MASTER CLOCK GATING Stress Test Bus "
                               "Config Test PASS.\n\n";
                else if (sub_set_inv_mclk)
                    response = "PEP Bus Config Test.\nSET INVERTED MASTER CLOCK GATING Test Bus "
                               "Config Test PASS.\n\n";
                else
                    response = "PEP Bus Config Test.\nSET INVERTED SLAVE CLOCK GATING Test Bus "
                               "Config Test PASS.\n\n";
            }
        } else if (buf_contains_wide_token(in, in_sz, L"tsens")) {
            recognized = TRUE;
            BOOLEAN sub_get_num_sensors = buf_contains_wide_token(in, in_sz, L"getnumsensors");
            BOOLEAN sub_check_calibration = buf_contains_wide_token(in, in_sz, L"checkcalibration");
            BOOLEAN sub_get_temp_range = buf_contains_wide_token(in, in_sz, L"gettemprange");
            BOOLEAN sub_get_temp = buf_contains_wide_token(in, in_sz, L"gettemp");
            BOOLEAN sub_enable_disable_threshold =
                buf_contains_wide_token(in, in_sz, L"enabledisablethreshold") ||
                buf_contains_wide_token(in, in_sz, L"disenth");
            BOOLEAN sub_set_low_threshold =
                buf_contains_wide_token(in, in_sz, L"setlowthreshold") ||
                buf_contains_wide_token(in, in_sz, L"setlowth");
            BOOLEAN sub_set_high_threshold =
                buf_contains_wide_token(in, in_sz, L"sethighthreshold") ||
                buf_contains_wide_token(in, in_sz, L"sethighth");
            BOOLEAN sub_enable_disable_set_threshold =
                buf_contains_wide_token(in, in_sz, L"disensetth");
            BOOLEAN sub_get_num_sensors_adversarial =
                buf_contains_wide_token(in, in_sz, L"getnumsensorsadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advgetnumsensors");
            BOOLEAN sub_check_calibration_adversarial =
                buf_contains_wide_token(in, in_sz, L"checkcalibrationadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advcheckcalibration");
            BOOLEAN sub_get_temp_adversarial =
                buf_contains_wide_token(in, in_sz, L"gettempadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advgettemp");
            BOOLEAN sub_get_temp_range_adversarial =
                buf_contains_wide_token(in, in_sz, L"gettemprangeadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advgettemprange");
            BOOLEAN sub_set_low_threshold_adversarial =
                buf_contains_wide_token(in, in_sz, L"setlowthresholdadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advsetlowth");
            BOOLEAN sub_set_high_threshold_adversarial =
                buf_contains_wide_token(in, in_sz, L"sethighthresholdadversarial") ||
                buf_contains_wide_token(in, in_sz, L"advsethighth");
            if (invalid_token) {
                response = "Invalid Tsens input.\nTSens Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspTsensForceFail", 0)) {
                if (sub_get_num_sensors)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_GetNumSensors FAILED.\nTSens "
                               "Test FAIL.\n\n";
                else if (sub_check_calibration)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_CheckCalibration for sensorID "
                               "= 0 FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_get_temp_range)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_GetTempRange for sensorID = 0 "
                               "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_get_temp)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_GetTemp for sensorID = 0 "
                               "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_enable_disable_threshold)
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_EnableThreshold "
                               "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_set_low_threshold)
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetLowThreshold for sensor "
                               "0 FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_set_high_threshold)
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetHighThreshold for "
                               "sensor 0 FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_enable_disable_set_threshold)
                    response =
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_EnableDisablesetLOWThreshold "
                        "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_get_num_sensors_adversarial)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_GetNumSensors did not return "
                               "STATUS_INVALID_PARAMETER_1.\nTSens Test FAIL.\n\n";
                else if (sub_check_calibration_adversarial)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_CheckCalibration returned "
                               "STATUS_SUCCESS.\nTSens Test FAIL.\n\n";
                else if (sub_get_temp_adversarial)
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_GetTempAdversarial "
                               "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_get_temp_range_adversarial)
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetTempRangeAdversarial "
                               "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_set_low_threshold_adversarial)
                    response =
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetLowerThresholdAdversarial "
                        "FAILED.\nTSens Test FAIL.\n\n";
                else if (sub_set_high_threshold_adversarial)
                    response =
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetUpperThresholdAdversarial "
                        "FAILED.\nTSens Test FAIL.\n\n";
                else
                    response = "TSens Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                ULONG sensors = registry_query_u_long(L"CoreBspTsensSensors", 1);
                ULONG temp_deci_c =
                    registry_query_u_long(L"CoreBspTsensTempDeciC", tsens_get_temp_deci_c());
                LONG min_deci_c = (LONG)registry_query_u_long(L"CoreBspTsensMinDeciC", 0);
                LONG max_deci_c = (LONG)registry_query_u_long(L"CoreBspTsensMaxDeciC", 1200);

                if (sub_get_num_sensors) {
                    (VOID) RtlStringCchPrintfA(
                        dynamic_response, RTL_NUMBER_OF(dynamic_response),
                        "PEP_CoreBSPTest -> Tsenstest: Number of Sensors Supported: %lu\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetNumSensors SUCCESSFUL.\n"
                        "TSens Test PASS.\n\n",
                        sensors);
                    response = dynamic_response;
                } else if (sub_check_calibration) {
                    response = "PEP_CoreBSPTest -> Tsenstest: Tsens_CheckCalibration for sensorID "
                               "= 0 Passed.\n"
                               "PEP_CoreBSPTest -> Tsenstest: Tsens_CheckCalibration SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_get_temp_range) {
                    (VOID) RtlStringCchPrintfA(
                        dynamic_response, RTL_NUMBER_OF(dynamic_response),
                        "PEP_CoreBSPTest -> Tsenstest: For sensorID = 0, Min Temp = %ld, Max Temp "
                        "= %ld\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetTempRange SUCCESSFUL.\n"
                        "TSens Test PASS.\n\n",
                        min_deci_c, max_deci_c);
                    response = dynamic_response;
                } else if (sub_get_temp) {
                    (VOID) RtlStringCchPrintfA(
                        dynamic_response, RTL_NUMBER_OF(dynamic_response),
                        "PEP_CoreBSPTest -> Tsenstest: Current Temp for sensorID = 0 is %lu\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetTemp SUCCESSFUL.\n"
                        "TSens Test PASS.\n\n",
                        temp_deci_c);
                    response = dynamic_response;
                } else if (sub_enable_disable_threshold) {
                    response =
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_DisableThreshold SUCCESSFUL.\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_EnableThreshold SUCCESSFUL.\n"
                        "TSens Test PASS.\n\n";
                } else if (sub_set_low_threshold) {
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetLowThreshold for sensor "
                               "0 SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_set_high_threshold) {
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetHighThreshold for "
                               "sensor 0 SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_enable_disable_set_threshold) {
                    response =
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_DisableThreshold SUCCESSFUL.\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_SetHighThreshold for sensor 0 "
                        "SUCCESSFUL.\n"
                        "PEP_CoreBSPTest -> Tsenstest: TSensTest_EnableThreshold SUCCESSFUL.\n"
                        "TSens Test PASS.\n\n";
                } else if (sub_get_num_sensors_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetNumSensorsAdversarial "
                               "SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_check_calibration_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: "
                               "TSensTest_CheckCalibrationAdversarial SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_get_temp_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: TSens_GetTempAdversarial.\n"
                               "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetTemp SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_get_temp_range_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: TSensTest_GetTempRangeAdversarial "
                               "SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_set_low_threshold_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: "
                               "TSensTest_SetLowerThresholdAdversarial SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else if (sub_set_high_threshold_adversarial) {
                    response = "PEP_CoreBSPTest -> Tsenstest: "
                               "TSensTest_SetUpperThresholdAdversarial SUCCESSFUL.\n"
                               "TSens Test PASS.\n\n";
                } else {
                    response = "TSens Test PASS.\n\n";
                }
            }
        } else if (buf_contains_wide_token(in, in_sz, L"tlmm")) {
            recognized = TRUE;
            BOOLEAN sub_config_gpio = buf_contains_wide_token(in, in_sz, L"configgpio");
            BOOLEAN sub_feedback_gpio = buf_contains_wide_token(in, in_sz, L"feedbackgpio");
            BOOLEAN sub_get_num_gpio = buf_contains_wide_token(in, in_sz, L"getnumgpio");
            BOOLEAN sub_config_gpio_grp = buf_contains_wide_token(in, in_sz, L"configgpiogrp");
            BOOLEAN valid_tlmm_sub_cmd =
                sub_config_gpio || sub_feedback_gpio || sub_get_num_gpio || sub_config_gpio_grp;
            if (invalid_token || !valid_tlmm_sub_cmd) {
                response = "Invalid TLMM Test input.\nTLMM Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else if (fail_token || registry_query_u_long(L"CoreBspTlmmForceFail", 0)) {
                if (sub_config_gpio || sub_config_gpio_grp)
                    response = "PEP TLMM Test. CONFIG GPIO TEST.\nFailed to configure the "
                               "GPIO.\nTLMM Test FAIL.\n\n";
                else if (sub_feedback_gpio)
                    response = "PEP TLMM Test. CONFIG GPIO TEST.\nGPIO Direction configuration "
                               "doesn't match.\nTLMM Test FAIL.\n\n";
                else
                    response = "PEP TLMM Test. CONFIG GPIO TEST.\nFailed to obtain GPIO "
                               "count.\nTLMM Test FAIL.\n\n";
                core_status = STATUS_UNSUCCESSFUL;
            } else {
                if (sub_config_gpio || sub_config_gpio_grp) {
                    response = "PEP TLMM Test. CONFIG GPIO TEST.\n"
                               "Successfuly configured the GPIO.\n"
                               "TLMM Test PASS.\n\n";
                } else if (sub_feedback_gpio) {
                    response = "PEP TLMM Test. CONFIG GPIO TEST.\n"
                               "GPIO configuration successfully read and verified.\n"
                               "TLMM Test PASS.\n\n";
                } else {
                    ULONG gpio_cnt = registry_query_u_long(L"CoreBspTlmmGpioCount", 0);
                    (VOID) RtlStringCchPrintfA(dynamic_response, RTL_NUMBER_OF(dynamic_response),
                                               "PEP TLMM Test. CONFIG GPIO TEST.\n"
                                               "Successfuly obtained the Total Number of GPIO for "
                                               "this target. Total = %lu\n"
                                               "TLMM Test PASS.\n\n",
                                               gpio_cnt);
                    response = dynamic_response;
                }
            }
        }
    }

    if (!recognized)
        core_status = STATUS_UNSUCCESSFUL;

    RtlZeroMemory(out, out_sz);

    size_t copied = 0;
    if (out_sz > 1) {
        size_t resp_len = 0;
        (VOID) RtlStringCchLengthA(response, NTSTRSAFE_MAX_CCH, &resp_len);
        copied = (resp_len < (out_sz - 1)) ? resp_len : (out_sz - 1);
        if (copied)
            RtlCopyMemory(out, response, copied);
    }

    out[copied] = '\0';
    *information = copied;
    return core_status;
}

VOID evt_io_dev_control(WDFQUEUE queue, WDFREQUEST req_arg, size_t output_buf_length,
                        size_t input_buf_length, ULONG io_control_code)
{
    UNREFERENCED_PARAMETER(output_buf_length);

    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t info = 0;

    WDFDEVICE device = WdfIoQueueGetDevice(queue);
    p_dev_ctx_t ctx = get_dev_ctx(device);

    if (diag_all_enabled()) {
        PVOID inb = NULL;
        size_t inb_sz = 0;
        if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(req_arg, 0, &inb, &inb_sz)))
            diag_record(io_control_code, (ULONG)input_buf_length, (ULONG)output_buf_length,
                        STATUS_PENDING, 0, inb, inb_sz);
        else
            diag_record(io_control_code, (ULONG)input_buf_length, (ULONG)output_buf_length,
                        STATUS_PENDING, 0, NULL, 0);
    }

    if (forward_enabled() && forward_is_interesting_ioctl(io_control_code) &&
        io_control_code != IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT &&
        io_control_code != IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE &&
        io_control_code != IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT &&
        io_control_code != IOCTL_PEP_GPIO_PMIC_SET_PULL &&
        io_control_code != IOCTL_PEP_GPIO_PMIC_SET_DRIVE &&
        io_control_code != IOCTL_PM_GPIO_GET_GPIO_STATE &&
        io_control_code != IOCTL_PM_GPIO_SET_GPIO_OUTPUT) {
        PVOID inb2 = NULL;
        size_t inb2_sz = 0;
        PVOID outb2 = NULL;
        size_t outb2_sz = 0;
        ULONG bytes = 0;
        (VOID) WdfRequestRetrieveInputBuffer(req_arg, 0, &inb2, &inb2_sz);
        (VOID) WdfRequestRetrieveOutputBuffer(req_arg, 0, &outb2, &outb2_sz);
        NTSTATUS fwd = forward_send(device, io_control_code, inb2, (ULONG)inb2_sz, outb2,
                                    (ULONG)outb2_sz, &bytes);
        if (NT_SUCCESS(fwd)) {
            status = fwd;
            info = bytes;
            goto done;
        }
    }

    if (is_unsupported_parity_ioctl(io_control_code)) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        info = 0;
        goto done;
    }

    if (is_compat_pmic_passthrough_family(io_control_code) &&
        io_control_code != IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE &&
        io_control_code != IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT &&
        io_control_code != IOCTL_PEP_GPIO_PMIC_SET_STATE &&
        io_control_code != IOCTL_PEP_GPIO_PMIC_SET_PULL &&
        io_control_code != IOCTL_PEP_GPIO_PMIC_SET_DRIVE &&
        io_control_code != IOCTL_PM_GPIO_GET_GPIO_STATE &&
        io_control_code != IOCTL_PM_GPIO_SET_GPIO_OUTPUT &&
        io_control_code != IOCTL_PMIC_PUBLIC_USB_OTG_CONTROL &&
        io_control_code != IOCTL_PMIC_PUBLIC_USB_OTG_ENABLE &&
        io_control_code != IOCTL_PM_3P_PUBLIC_USB_OTG_ENABLE &&
        io_control_code != IOCTL_PM_WLED_MODULE_ENABLE &&
        io_control_code != IOCTL_PM_IBBLAB_MODULE_ENABLE) {
        if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
            status = STATUS_INVALID_DEVICE_STATE;
            info = 0;
            goto done;
        }

        ULONG* in = NULL;
        size_t in_sz = 0;
        status = WdfRequestRetrieveInputBuffer(req_arg, 4, (PVOID*)&in, &in_sz);
        if (!NT_SUCCESS(status) || !in) {
            info = 0;
            goto done;
        }

        res_key_t k;
        res_value_t v;
        k.res_id = 0x30464D50u;
        k.sub_type = io_control_code;
        v.value0 = (in_sz >= sizeof(ULONG)) ? in[0] : 0;
        v.value1 = (in_sz >= sizeof(ULONG) * 2) ? in[1] : 0;
        v.value2 = (in_sz >= sizeof(ULONG) * 3) ? in[2] : 0;
        v.value3 = (in_sz >= sizeof(ULONG) * 4) ? in[3] : 0;
        (VOID) res_set(k, v);

        if (output_buf_length != 0) {
            ULONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status)) {
                if (status == STATUS_BUFFER_TOO_SMALL)
                    info = 4;
                goto done;
            }
            *out = 0;
            info = 4;
        } else {
            info = 0;
        }

        status = STATUS_SUCCESS;
        goto done;
    }

    switch (io_control_code) {
        case 9u:
            status =
                handle_core_bsp_compat_ioctl(req_arg, input_buf_length, output_buf_length, &info);
            break;

        case IOCTL_PEP_PING:
            status = STATUS_SUCCESS;
            info = 0;
            break;

        case IOCTL_PEP_GET_PORTID: {
            ioctl_portid_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ioctl_portid_response_t),
                                                    (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                out->PortId = ctx ? ctx->port_id : 0;
                info = sizeof(*out);
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_GET_HID: {
            PWCHAR out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 1, (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (ctx && ctx->hid.Buffer && ctx->hid.Length + sizeof(WCHAR) <= out_sz) {
                    RtlCopyMemory(out, ctx->hid.Buffer, ctx->hid.Length);
                    out[ctx->hid.Length / sizeof(WCHAR)] = L'\0';
                    info = ctx->hid.Length + sizeof(WCHAR);
                    status = STATUS_SUCCESS;
                } else {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = (ctx ? (ctx->hid.Length + sizeof(WCHAR)) : sizeof(WCHAR));
                }
            }
            break;
        }

        case IOCTL_PEP_GET_CONFIG: {
            ioctl_config_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ioctl_config_response_t),
                                                    (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                out->Config = g.config;
                info = sizeof(*out);
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_GET_STATS: {
            ioctl_stats_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ioctl_stats_response_t),
                                                    (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                out->IoctlCount = (ULONG)g_stats.ioctl_cnt;
                out->PoFxPowerRequired = (ULONG)g_stats.po_fx_power_required;
                out->PoFxPowerNotRequired = (ULONG)g_stats.po_fx_power_not_required;
                out->PoFxComponentActive = (ULONG)g_stats.po_fx_comp_active;
                out->PoFxComponentIdle = (ULONG)g_stats.po_fx_comp_idle;
                out->PoFxIdleState = (ULONG)g_stats.po_fx_idle_state;
                out->D0Entry = (ULONG)g_stats.d0_entry;
                out->D0Exit = (ULONG)g_stats.d0_exit;
                info = sizeof(*out);
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_GET_DIAG: {
            PVOID out = NULL;
            size_t out_sz = 0;
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(diag_dump_hdr_t), &out, &out_sz);
            if (NT_SUCCESS(status)) {
                size_t written = 0;
                status = diag_read(out, out_sz, &written);
                info = written;
            }
            break;
        }

        case IOCTL_PEP_GET_PEPLOG: {
            PVOID out = NULL;
            size_t out_sz = 0;
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(peplog_dump_hdr_t), &out, &out_sz);
            if (NT_SUCCESS(status)) {
                size_t written = 0;
                status = pep_log_dump(out, out_sz, &written);
                info = written;
            }
            break;
        }

        case IOCTL_PEP_CLEAR_PEPLOG:
            pep_log_clear();
            status = STATUS_SUCCESS;
            info = 0;
            break;

        case IOCTL_PEP_SOC_SUBSYS_COUNT: {
            ULONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ULONG), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                *out = SOC_SUBSYS_CNT;
                info = sizeof(ULONG);
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_SOC_SUBSYS_QUERY: {
            ioctl_soc_subsys_query_request_t* in = NULL;
            size_t in_sz = 0;
            ioctl_soc_subsys_query_response_t* out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(
                req_arg, sizeof(ioctl_soc_subsys_query_request_t), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_soc_subsys_query_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Index = in->Index;

            PCWSTR parent = L"";
            PCWSTR name = L"";
            ULONG meta_cnt = 0;
            BOOLEAN enabled = FALSE;
            soc_subsystem_stats_t st = {0};

            if (!soc_subsys_get_info(in->Index, &parent, &name, &meta_cnt, &enabled, &st)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            out->Enabled = enabled ? 1u : 0u;
            out->MetadataCount = meta_cnt;
            out->WakeupCount = st.wakeup_cnt;
            out->BlockedDuration100ns = st.blocked_duration100ns;
            out->SleepDuration100ns = st.sleep_duration100ns;

            (VOID) RtlStringCchCopyW(out->ParentName, ARRAYSIZE(out->ParentName),
                                     parent ? parent : L"");
            (VOID) RtlStringCchCopyW(out->Name, ARRAYSIZE(out->Name), name ? name : L"");

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_SOC_SUBSYS_RESET: {
            PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING reset;
            reset.PlatformIdleStateIndex = 0;
            reset.Flags = 0;
            if (!soc_subsys_reset_accounting(&reset))
                status = STATUS_UNSUCCESSFUL;
            else
                status = STATUS_SUCCESS;
            info = 0;
            break;
        }

        case IOCTL_PEP_SOC_SUBSYS_BLOCKTIME: {
            ioctl_soc_subsys_block_request_t* in = NULL;
            size_t in_sz = 0;
            ioctl_soc_subsys_block_response_t* out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(
                req_arg, sizeof(ioctl_soc_subsys_block_request_t), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_soc_subsys_block_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            PCWSTR parent = NULL;
            PCWSTR name = NULL;
            ULONG meta_cnt = 0;
            BOOLEAN enabled = FALSE;
            soc_subsystem_stats_t st = {0};

            if (!soc_subsys_get_info(in->Index, &parent, &name, &meta_cnt, &enabled, &st)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            out->BlockingTime100ns = st.blocked_duration100ns;
            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_SOC_SUBSYS_METADATA: {
            ioctl_soc_subsys_meta_request_t* in = NULL;
            size_t in_sz = 0;
            ioctl_soc_subsys_meta_response_t* out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ioctl_soc_subsys_meta_request_t),
                                                   (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_soc_subsys_meta_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Index = in->Index;
            out->PairCount = 4;

            PCWSTR parent = L"";
            PCWSTR name = L"";
            ULONG meta_cnt = 0;
            BOOLEAN enabled = FALSE;
            soc_subsystem_stats_t st = {0};

            if (!soc_subsys_get_info(in->Index, &parent, &name, &meta_cnt, &enabled, &st)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            (VOID)
                RtlStringCchCopyW(out->Pairs[0].Key, ARRAYSIZE(out->Pairs[0].Key), L"WakeupCount");
            (VOID) RtlStringCchPrintfW(out->Pairs[0].Value, ARRAYSIZE(out->Pairs[0].Value), L"%lu",
                                       st.wakeup_cnt);

            (VOID) RtlStringCchCopyW(out->Pairs[1].Key, ARRAYSIZE(out->Pairs[1].Key),
                                     L"BlockedDuration100ns");
            (VOID) RtlStringCchPrintfW(out->Pairs[1].Value, ARRAYSIZE(out->Pairs[1].Value), L"%llu",
                                       st.blocked_duration100ns);

            (VOID) RtlStringCchCopyW(out->Pairs[2].Key, ARRAYSIZE(out->Pairs[2].Key),
                                     L"SleepDuration100ns");
            (VOID) RtlStringCchPrintfW(out->Pairs[2].Value, ARRAYSIZE(out->Pairs[2].Value), L"%llu",
                                       st.sleep_duration100ns);

            (VOID) RtlStringCchCopyW(out->Pairs[3].Key, ARRAYSIZE(out->Pairs[3].Key), L"Enabled");
            (VOID) RtlStringCchPrintfW(out->Pairs[3].Value, ARRAYSIZE(out->Pairs[3].Value), L"%lu",
                                       enabled ? 1u : 0u);

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_REGISTER_VETO_REASON: {
            PVOID in = NULL;
            size_t in_sz = 0;
            PVOID out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(WCHAR), &in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ULONG), &out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            PCWSTR s = (PCWSTR)in;
            size_t max_cch = in_sz / sizeof(WCHAR);
            size_t i = 0;
            for (; i < max_cch; i++) {
                if (s[i] == L'\0')
                    break;
            }
            if (i == max_cch) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            ULONG id = 0;
            status = pep_reg_veto_reason(s, &id);
            if (NT_SUCCESS(status)) {
                *(ULONG*)out = id;
                info = sizeof(ULONG);
            }
            break;
        }

        case IOCTL_PEP_APPLY_VETO: {
            ioctl_veto_operation_t* op = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ioctl_veto_operation_t),
                                                   (PVOID*)&op, &in_sz);
            if (!NT_SUCCESS(status))
                break;
            status = pep_apply_veto(op->LprId, op->ModeId, op->VetoId);
            info = 0;
            break;
        }

        case IOCTL_PEP_REMOVE_VETO: {
            ioctl_veto_operation_t* op = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ioctl_veto_operation_t),
                                                   (PVOID*)&op, &in_sz);
            if (!NT_SUCCESS(status))
                break;
            status = pep_remove_veto(op->LprId, op->ModeId, op->VetoId);
            info = 0;
            break;
        }

        case IOCTL_PEP_QUERY_VETO_REFCOUNT: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            LONGLONG* out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(LONGLONG), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            *out = pep_get_veto_ref_cnt(*in);
            info = sizeof(LONGLONG);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_INTERRUPT_SNAPSHOT: {
            ioctl_interrupt_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_interrupt_snapshot_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            interrupt_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            interrupt_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->IsrCount = snap.isr_cnt;
            out->DpcCount = snap.dpc_cnt;
            out->EnableCount = snap.enable_cnt;
            out->DisableCount = snap.disable_cnt;
            out->DpcQueuedCount = snap.dpc_queued_cnt;
            out->LastMessageId = snap.last_message_id;
            out->LastIsrTick100ns = snap.last_isr_tick100ns;
            out->LastDpcTick100ns = snap.last_dpc_tick100ns;
            out->IdleScanCount = snap.idle_scan_cnt;
            out->IdleWakeSampleCount = snap.idle_wake_sample_cnt;
            out->LastIdleLevel = snap.last_idle_level;
            out->LastIdleModeIndex = snap.last_idle_mode_idx;
            out->LastWakeIrqStatus = snap.last_wake_irq_status;
            out->LastWakeGeniStatus = snap.last_wake_geni_status;
            out->LastWakeCmd0 = snap.last_wake_cmd0;
            out->LastWakeCmdCtrl = snap.last_wake_cmd_ctrl;
            out->MapCount = snap.map_cnt;
            out->MapHitCount = snap.map_hit_cnt;
            out->MapMissCount = snap.map_miss_cnt;
            out->LastLookupInterruptId = snap.last_lookup_interrupt_id;
            out->LastLookupOffset = snap.last_lookup_offset;
            out->LastLookupTriggerType = snap.last_lookup_trigger_type;
            out->LastLookupDriverId = snap.last_lookup_driver_id;
            out->WakeBitHitCount = snap.wake_bit_hit_cnt;
            out->LastWakeOffset = snap.last_wake_offset;
            out->LastWakeInterruptId = snap.last_wake_interrupt_id;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventTick100ns = snap.last_event_tick100ns;

            out->PortCount = snap.port_cnt;
            if (out->PortCount > RTL_NUMBER_OF(out->Ports))
                out->PortCount = RTL_NUMBER_OF(out->Ports);
            for (ULONG i = 0; i < out->PortCount; i++) {
                out->Ports[i].PortId = snap.ports[i].port_id;
                out->Ports[i].IsrCount = snap.ports[i].isr_cnt;
                out->Ports[i].DpcCount = snap.ports[i].dpc_cnt;
                out->Ports[i].EnableCount = snap.ports[i].enable_cnt;
                out->Ports[i].DisableCount = snap.ports[i].disable_cnt;
                out->Ports[i].DpcQueuedCount = snap.ports[i].dpc_queued_cnt;
                out->Ports[i].LastMessageId = snap.ports[i].last_message_id;
                out->Ports[i].LastIsrTick100ns = snap.ports[i].last_isr_tick100ns;
                out->Ports[i].LastDpcTick100ns = snap.ports[i].last_dpc_tick100ns;
            }

            out->WakeTopCount = snap.wake_top_cnt;
            if (out->WakeTopCount > RTL_NUMBER_OF(out->WakeTop))
                out->WakeTopCount = RTL_NUMBER_OF(out->WakeTop);
            for (ULONG i = 0; i < out->WakeTopCount; i++) {
                out->WakeTop[i].Offset = snap.wake_top[i].offset;
                out->WakeTop[i].InterruptId = snap.wake_top[i].interrupt_id;
                out->WakeTop[i].HitCount = snap.wake_top[i].hit_cnt;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_INTERRUPT_HISTORY: {
            ioctl_interrupt_history_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_interrupt_history_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            const ULONG base = FIELD_OFFSET(ioctl_interrupt_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_interrupt_event_info_t);
            if (out_sz < base + entry_size) {
                info = base + entry_size;
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            ULONG capacity = (ULONG)((out_sz - base) / entry_size);
            if (capacity > INTERRUPT_HISTORY_CAPACITY)
                capacity = INTERRUPT_HISTORY_CAPACITY;

            RtlZeroMemory(out, base);
            out->Capacity = capacity;
            out->EntrySize = entry_size;

            ULONG count = 0;
            ULONG dropped = 0;
            status =
                interrupt_get_history((interrupt_event_t*)out->Entries, capacity, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            out->Count = count;
            out->Dropped = dropped;
            info = base + ((size_t)count * entry_size);
            break;
        }

        case IOCTL_PEP_INTERRUPT_WAKECOUNTERS: {
            ioctl_interrupt_wakecounters_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_interrupt_wakecounters_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            const ULONG base = FIELD_OFFSET(ioctl_interrupt_wakecounters_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_interrupt_wake_counter_info_t);
            if (out_sz < base + entry_size) {
                info = base + entry_size;
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            ULONG capacity = (ULONG)((out_sz - base) / entry_size);
            if (capacity > INTERRUPT_MAP_CAPACITY)
                capacity = INTERRUPT_MAP_CAPACITY;

            RtlZeroMemory(out, base);
            out->Capacity = capacity;
            out->EntrySize = entry_size;

            ULONG count = 0;
            status = interrupt_get_wake_counters((interrupt_wake_counter_t*)out->Entries, capacity,
                                                 &count);
            if (!NT_SUCCESS(status))
                break;

            out->Count = count;
            info = base + ((size_t)count * entry_size);
            break;
        }

        case IOCTL_PEP_INTERRUPT_LOOKUP: {
            ioctl_interrupt_lookup_request_t* in = NULL;
            size_t in_sz = 0;
            ioctl_interrupt_lookup_response_t* out = NULL;
            size_t out_sz = 0;

            status = WdfRequestRetrieveInputBuffer(
                req_arg, sizeof(ioctl_interrupt_lookup_request_t), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, sizeof(ioctl_interrupt_lookup_response_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            LONG offset = -1;
            ULONG trig = 0;
            ULONG drv = 0;
            ULONG flags = 0;
            NTSTATUS map_status =
                interrupt_lookup_mapping(in->InterruptId, &offset, &trig, &drv, &flags);

            RtlZeroMemory(out, sizeof(*out));
            out->InterruptId = in->InterruptId;
            out->Offset = offset;
            out->TriggerType = trig;
            out->DriverId = drv;
            out->Flags = flags;
            out->Status = map_status;
            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_MMRM_SNAPSHOT: {
            ioctl_mmrm_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            mmrm_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            mmrm_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->FeatureEnabled = snap.feature_enabled;
            out->ReservedPowerMilliWatts = snap.reserved_power_milli_watts;
            out->SamplingPeriodMilliSeconds = snap.sampling_period_milli_seconds;
            out->MaxThrottlePercentage = snap.max_throttle_percentage;
            out->MitigationStartThresholdMilliAmps = snap.mitigation_start_threshold_milli_amps;
            out->MitigationStopThresholdMilliAmps = snap.mitigation_stop_threshold_milli_amps;
            out->TemperatureSensorId = snap.temperature_sensor_id;
            out->VideoEncodingState = snap.video_encoding_state;
            out->VideoDecodingState = snap.video_decoding_state;
            out->LastTotalPowerMilliWatts = snap.last_total_power_milli_watts;
            out->LastPeakMilliAmps = snap.last_peak_milli_amps;
            out->ThrottleReason = snap.throttle_reason;
            out->ThrottlePercentage = snap.throttle_percentage;
            out->Updated = snap.updated;
            out->LastPassiveLimit = snap.last_passive_limit;
            out->LastActivationReasons = snap.last_activation_reasons;
            out->LastVersion = snap.last_version;
            out->PolicyWaitActive = snap.policy_wait_active;
            out->SamplingTimerRunning = snap.sampling_timer_running;
            out->DsmFlags = snap.dsm_flags;
            out->DsmRefreshCount = snap.dsm_refresh_cnt;
            out->LastDsmStatus = snap.last_dsm_status;
            out->DsmPollIntervalMilliSeconds = snap.dsm_poll_interval_milli_seconds;
            out->LastDsmPolicyItemCount = snap.last_dsm_policy_item_cnt;
            out->LastDsmClockItemCount = snap.last_dsm_clock_item_cnt;
            out->PeakVoltageMilliVolts = snap.peak_voltage_milli_volts;
            out->GlobalDynPermille = snap.global_dyn_permille;
            out->GlobalLkgPermille = snap.global_lkg_permille;
            out->LastCameraPowerMilliWatts = snap.last_camera_power_milli_watts;
            out->LastVideoPowerMilliWatts = snap.last_video_power_milli_watts;
            out->LastDisplayPowerMilliWatts = snap.last_display_power_milli_watts;
            out->CurrentTempDeciK = snap.current_temp_deci_k;
            out->ThresholdLowDeciK = snap.threshold_low_deci_k;
            out->ThresholdHighDeciK = snap.threshold_high_deci_k;
            out->CameraTempDeciC = snap.camera_temp_deci_c;
            out->VideoTempDeciC = snap.video_temp_deci_c;
            out->DisplayTempDeciC = snap.display_temp_deci_c;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventReason = snap.last_event_reason;
            out->LastEventPercent = snap.last_event_percent;
            out->LastEventUpdated = snap.last_event_updated;
            out->LastEventPeakMilliAmps = snap.last_event_peak_milli_amps;
            out->LastEventTempDeciK = snap.last_event_temp_deci_k;
            out->LastEventLowTempDeciK = snap.last_event_low_temp_deci_k;
            out->LastEventHighTempDeciK = snap.last_event_high_temp_deci_k;
            out->LastEventTick100ns = snap.last_event_tick100ns;

            ULONG domain_cnt = snap.domain_cnt;
            if (domain_cnt > RTL_NUMBER_OF(out->Domains))
                domain_cnt = RTL_NUMBER_OF(out->Domains);
            out->DomainCount = domain_cnt;
            for (ULONG i = 0; i < domain_cnt; i++) {
                out->Domains[i].FrequencyKhz = snap.domains[i].freq_khz;
                out->Domains[i].PowerMilliWatts = snap.domains[i].power_milli_watts;
                out->Domains[i].Flags = snap.domains[i].flags;
                out->Domains[i].DynPermille = snap.domains[i].dyn_permille;
                out->Domains[i].LkgPermille = snap.domains[i].lkg_permille;
                out->Domains[i].TempBiasDeciC = snap.domains[i].temp_bias_deci_c;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_MMRM_HISTORY: {
            ioctl_mmrm_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_mmrm_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_mmrm_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_mmrm_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_mmrm_event_info_t);
                break;
            }

            mmrm_event_t* hist = (mmrm_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(mmrm_event_t), 'hRmQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(mmrm_event_t));
            status = mmrm_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_mmrm_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].Reason = hist[i].reason;
                    out->Entries[i].Percent = hist[i].percent;
                    out->Entries[i].Updated = hist[i].updated;
                    out->Entries[i].PeakMilliAmps = hist[i].peak_milli_amps;
                    out->Entries[i].TempDeciK = hist[i].temp_deci_k;
                    out->Entries[i].LowTempDeciK = hist[i].low_temp_deci_k;
                    out->Entries[i].HighTempDeciK = hist[i].high_temp_deci_k;
                    out->Entries[i].PassiveLimit = hist[i].passive_limit;
                    out->Entries[i].ActivationReasons = hist[i].activation_reasons;
                    out->Entries[i].FeatureEnabled = hist[i].feature_enabled;
                    out->Entries[i].DsmFlags = hist[i].dsm_flags;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_mmrm_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_MMRM_DOMAIN_QUERY: {
            ioctl_mmrm_domain_query_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_mmrm_domain_query_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            mmrm_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            mmrm_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Index = in->Index;
            out->DomainCount = snap.domain_cnt;
            if (in->Index >= snap.domain_cnt || in->Index >= RTL_NUMBER_OF(snap.domains)) {
                out->Status = STATUS_NOT_FOUND;
                status = STATUS_SUCCESS;
                info = sizeof(*out);
                break;
            }

            out->Domain.FrequencyKhz = snap.domains[in->Index].freq_khz;
            out->Domain.PowerMilliWatts = snap.domains[in->Index].power_milli_watts;
            out->Domain.Flags = snap.domains[in->Index].flags;
            out->Domain.DynPermille = snap.domains[in->Index].dyn_permille;
            out->Domain.LkgPermille = snap.domains[in->Index].lkg_permille;
            out->Domain.TempBiasDeciC = snap.domains[in->Index].temp_bias_deci_c;
            out->Status = STATUS_SUCCESS;
            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_TSENS_SNAPSHOT: {
            ioctl_tsens_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            tsens_snapshot_t ts;
            RtlZeroMemory(&ts, sizeof(ts));
            tsens_get_snapshot(&ts);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = ts.version;
            out->SensorCount = ts.sensor_cnt;
            out->AggregateTempDeciC = ts.aggregate_temp_deci_c;
            out->ReadCount = ts.read_cnt;
            out->DsmRefreshCount = ts.dsm_refresh_cnt;
            out->ThresholdHitCount = ts.threshold_hit_cnt;
            out->EventSequence = ts.event_sequence;
            out->HistoryCount = ts.history_cnt;
            out->HistoryDropped = ts.history_dropped;
            out->LastEventCode = ts.last_event_code;
            out->LastEventStatus = ts.last_event_status;
            out->LastSensorId = ts.last_sensor_id;
            out->LastTempDeciC = ts.last_temp_deci_c;
            out->LastLowDeciC = ts.last_low_deci_c;
            out->LastHighDeciC = ts.last_high_deci_c;
            out->LastEventTick100ns = ts.last_event_tick100ns;

            ULONG sensor_cnt = ts.sensor_cnt;
            if (sensor_cnt > RTL_NUMBER_OF(out->Sensors))
                sensor_cnt = RTL_NUMBER_OF(out->Sensors);
            out->SensorEntryCount = sensor_cnt;
            for (ULONG i = 0; i < sensor_cnt; i++) {
                out->Sensors[i].LastTempDeciC = ts.sensors[i].last_temp_deci_c;
                out->Sensors[i].MinTempDeciC = ts.sensors[i].min_temp_deci_c;
                out->Sensors[i].MaxTempDeciC = ts.sensors[i].max_temp_deci_c;
                out->Sensors[i].LowThresholdDeciC = ts.sensors[i].low_threshold_deci_c;
                out->Sensors[i].HighThresholdDeciC = ts.sensors[i].high_threshold_deci_c;
                out->Sensors[i].LowHitCount = ts.sensors[i].low_hit_cnt;
                out->Sensors[i].HighHitCount = ts.sensors[i].high_hit_cnt;
                out->Sensors[i].Flags = ts.sensors[i].flags;
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_TSENS_HISTORY: {
            ioctl_tsens_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_tsens_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_tsens_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_tsens_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_tsens_event_info_t);
                break;
            }

            tsens_event_t* hist = (tsens_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(tsens_event_t), 'hTsQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(tsens_event_t));
            status = tsens_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_tsens_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].SensorId = hist[i].sensor_id;
                    out->Entries[i].TempDeciC = hist[i].temp_deci_c;
                    out->Entries[i].LowDeciC = hist[i].low_deci_c;
                    out->Entries[i].HighDeciC = hist[i].high_deci_c;
                    out->Entries[i].Aux0 = hist[i].aux0;
                    out->Entries[i].Aux1 = hist[i].aux1;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_tsens_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_TSENS_SENSOR_RANGE: {
            ioctl_tsens_sensor_range_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_tsens_sensor_range_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->SensorId = in->SensorId;
            out->Status =
                tsens_get_temp_range(in->SensorId, &out->MinTempDeciC, &out->MaxTempDeciC);
            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_TSENS_SET_THRESHOLD: {
            ioctl_tsens_set_threshold_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_tsens_set_threshold_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            NTSTATUS set_status = tsens_set_threshold_window(
                in->SensorId, in->LowThresholdDeciC, in->HighThresholdDeciC,
                (in->Flags & 0x1u) ? TRUE : FALSE, (in->Flags & 0x2u) ? TRUE : FALSE);

            RtlZeroMemory(out, sizeof(*out));
            out->SensorId = in->SensorId;
            out->Status = set_status;
            out->LowThresholdDeciC = in->LowThresholdDeciC;
            out->HighThresholdDeciC = in->HighThresholdDeciC;
            out->Flags = in->Flags & 0x3u;
            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_THERMALREQ_SNAPSHOT: {
            ioctl_thermalreq_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            thermal_req_snapshot_t tr;
            RtlZeroMemory(&tr, sizeof(tr));
            thermal_req_get_snapshot(&tr);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = tr.version;
            out->PollMs = tr.poll_ms;
            out->ActiveCount = tr.active_cnt;
            out->LastRequestId = tr.last_req_id;
            out->TotalQueued = tr.total_queued;
            out->TotalCompleted = tr.total_completed;
            out->TotalCancelled = tr.total_cancelled;
            out->TotalTimeout = tr.total_timeout;
            out->TotalLowThresholdHits = tr.total_low_threshold_hits;
            out->TotalHighThresholdHits = tr.total_high_threshold_hits;
            out->TotalBusyRejected = tr.total_busy_rejected;
            out->TotalNoSlotRejected = tr.total_no_slot_rejected;
            out->TotalMarkCancelableFailures = tr.total_mark_cancelable_failures;
            out->EventSequence = tr.event_sequence;
            out->HistoryCount = tr.history_cnt;
            out->HistoryDropped = tr.history_dropped;
            out->LastEventCode = tr.last_event_code;
            out->LastEventStatus = tr.last_event_status;
            out->LastEventRequestId = tr.last_event_req_id;
            out->LastEventSensorId = tr.last_event_sensor_id;
            out->LastEventTempDeciK = tr.last_event_temp_deci_k;
            out->LastEventLowTempDeciK = tr.last_event_low_temp_deci_k;
            out->LastEventHighTempDeciK = tr.last_event_high_temp_deci_k;
            out->LastEventTick100ns = tr.last_event_tick100ns;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_THERMALREQ_HISTORY: {
            ioctl_thermalreq_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_thermalreq_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_thermalreq_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_thermalreq_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_thermalreq_event_info_t);
                break;
            }

            thermal_req_event_t* hist = (thermal_req_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(thermal_req_event_t), 'hTrQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(thermal_req_event_t));
            status = thermal_req_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_thermalreq_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].RequestId = hist[i].req_id;
                    out->Entries[i].SensorId = hist[i].sensor_id;
                    out->Entries[i].TempDeciK = hist[i].temp_deci_k;
                    out->Entries[i].LowTempDeciK = hist[i].low_temp_deci_k;
                    out->Entries[i].HighTempDeciK = hist[i].high_temp_deci_k;
                    out->Entries[i].ActiveCount = hist[i].active_cnt;
                    out->Entries[i].PortId = hist[i].port_id;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_thermalreq_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_DEEPSTANDBY_SNAPSHOT: {
            ioctl_deepstandby_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            deep_standby_snapshot_t ds;
            RtlZeroMemory(&ds, sizeof(ds));
            status = deep_standby_get_snapshot(&ds);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = ds.version;
            out->FeatureEnabled = ds.feature_enabled;
            out->CallbacksEnabled = ds.callbacks_enabled;
            out->ModemEnabled = ds.modem_enabled;
            out->WlanEnabled = ds.wlan_enabled;
            out->PowerSource = ds.power_src;
            out->ScreenState = ds.screen_state;
            out->UserState = ds.user_state;
            out->MsOnDcPwr = ds.ms_on_dc_pwr;
            out->DeepStandbyState = ds.deep_standby_state;
            out->TimerExpired = ds.timer_expired;
            out->BattDrainExceededLimit = ds.batt_drain_exceeded_limit;
            out->BattDrainSinceTmrStart = ds.batt_drain_since_tmr_start;
            out->TmrStartingBattPct = ds.tmr_starting_batt_pct;
            out->CurrentBattPct = ds.current_batt_pct;
            out->MaxBattDrainPct = ds.max_batt_drain_pct;
            out->TimeoutSeconds = ds.timeout_seconds;
            out->ModemReq = ds.modem_req;
            out->EvaluateCount = ds.evaluate_cnt;
            out->CallbackCount = ds.callback_cnt;
            out->StateTransitionCount = ds.state_transition_cnt;
            out->EnterCount = ds.enter_cnt;
            out->ExitCount = ds.exit_cnt;
            out->WlanIoctlErrors = ds.wlan_ioctl_errors;
            out->ModemIoctlErrors = ds.modem_ioctl_errors;
            out->LastWlanIoctlStatus = ds.last_wlan_ioctl_status;
            out->LastModemIoctlStatus = ds.last_modem_ioctl_status;
            out->HistoryCount = ds.history_cnt;
            out->HistoryDropped = ds.history_dropped;
            out->LastEventCode = ds.last_event_code;
            out->LastEventStatus = ds.last_event_status;
            out->TmrStartTime100ns = ds.tmr_start_time100ns;
            out->DeepStandbyDeadline100ns = ds.deep_standby_deadline100ns;
            out->LastEvaluateTick100ns = ds.last_evaluate_tick100ns;
            out->LastTransitionTick100ns = ds.last_transition_tick100ns;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_DEEPSTANDBY_HISTORY: {
            ioctl_deepstandby_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_deepstandby_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_deepstandby_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_deepstandby_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_deepstandby_event_info_t);
                break;
            }

            deep_standby_history_entry_t* hist = (deep_standby_history_entry_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(deep_standby_history_entry_t), 'hDsQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(deep_standby_history_entry_t));
            status = deep_standby_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_deepstandby_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].PrevMsOnDcPwr = hist[i].prev_ms_on_dc_pwr;
                    out->Entries[i].CurrMsOnDcPwr = hist[i].curr_ms_on_dc_pwr;
                    out->Entries[i].PrevDeepStandbyState = hist[i].prev_deep_standby_state;
                    out->Entries[i].CurrDeepStandbyState = hist[i].curr_deep_standby_state;
                    out->Entries[i].Aux0 = hist[i].aux0;
                    out->Entries[i].Aux1 = hist[i].aux1;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_deepstandby_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_DEEPSTANDBY_MODEM_GET: {
            ioctl_deepstandby_modem_get_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->ModemReq = deep_standby_get_modem_req();
            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_CRASHDUMP_SNAPSHOT: {
            ioctl_crashdump_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            crash_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            crash_dump_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->ActiveSource = snap.active_src;
            out->InUseSourceCount = snap.in_use_src_cnt;
            out->SourceCapacity = snap.src_capacity;
            out->CreateCount = snap.create_cnt;
            out->OpenCount = snap.open_cnt;
            out->DestroyCount = snap.destroy_cnt;
            out->WriteCount = snap.write_cnt;
            out->ReadCount = snap.read_cnt;
            out->CaptureCount = snap.capture_cnt;
            out->LiveKernelCreateCount = snap.live_kernel_create_cnt;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventSourceIndex = snap.last_event_src_idx;
            out->LastEventSize = snap.last_event_size;
            out->LastEventTick100ns = snap.last_event_tick100ns;

            ULONG src_cnt = snap.src_capacity;
            if (src_cnt > RTL_NUMBER_OF(out->Sources))
                src_cnt = RTL_NUMBER_OF(out->Sources);
            for (ULONG i = 0; i < src_cnt; i++) {
                out->Sources[i].Index = snap.sources[i].idx;
                out->Sources[i].InUse = snap.sources[i].in_use;
                out->Sources[i].GuidValid = snap.sources[i].guid_valid;
                out->Sources[i].SourceGuid = snap.sources[i].src_guid;
                out->Sources[i].ItemCount = snap.sources[i].item_cnt;
                out->Sources[i].EntrySize = snap.sources[i].entry_size;
                out->Sources[i].BlobLen = snap.sources[i].blob_len;
                out->Sources[i].BlobCapacity = snap.sources[i].blob_capacity;
                out->Sources[i].RingReadIndex = snap.sources[i].ring_read_idx;
                out->Sources[i].RingWriteIndex = snap.sources[i].ring_write_idx;
                out->Sources[i].RingItemsPresent = snap.sources[i].ring_items_present;
                out->Sources[i].LastWrite100ns = snap.sources[i].last_write100ns;
                out->Sources[i].HasCreateFileObject = snap.sources[i].has_create_file_object;
                out->Sources[i].HasOpenFileObject = snap.sources[i].has_open_file_object;
                out->Sources[i].CreateFileObjectOrphaned =
                    snap.sources[i].create_file_object_orphaned;
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_CRASHDUMP_HISTORY: {
            ioctl_crashdump_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_crashdump_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_crashdump_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_crashdump_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_crashdump_event_info_t);
                break;
            }

            crash_event_t* hist = (crash_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(crash_event_t), 'hCdQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(crash_event_t));
            status = crash_dump_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_crashdump_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].SourceIndex = hist[i].src_idx;
                    out->Entries[i].ActiveSource = hist[i].active_src;
                    out->Entries[i].Size = hist[i].size;
                    out->Entries[i].CreateCount = hist[i].create_cnt;
                    out->Entries[i].OpenCount = hist[i].open_cnt;
                    out->Entries[i].DestroyCount = hist[i].destroy_cnt;
                    out->Entries[i].WriteCount = hist[i].write_cnt;
                    out->Entries[i].ReadCount = hist[i].read_cnt;
                    out->Entries[i].CaptureCount = hist[i].capture_cnt;
                    out->Entries[i].LiveKernelCreateCount = hist[i].live_kernel_create_cnt;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_crashdump_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_CRASHDUMP_SOURCE_QUERY: {
            ioctl_crashdump_source_query_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_crashdump_source_query_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            crash_src_info_t src;
            RtlZeroMemory(&src, sizeof(src));
            NTSTATUS src_status = crash_dump_get_src_info(in->Index, &src);

            crash_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            crash_dump_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Index = in->Index;
            out->Status = src_status;
            out->ActiveSource = snap.active_src;
            out->Source.Index = src.idx;
            out->Source.InUse = src.in_use;
            out->Source.GuidValid = src.guid_valid;
            out->Source.SourceGuid = src.src_guid;
            out->Source.ItemCount = src.item_cnt;
            out->Source.EntrySize = src.entry_size;
            out->Source.BlobLen = src.blob_len;
            out->Source.BlobCapacity = src.blob_capacity;
            out->Source.RingReadIndex = src.ring_read_idx;
            out->Source.RingWriteIndex = src.ring_write_idx;
            out->Source.RingItemsPresent = src.ring_items_present;
            out->Source.LastWrite100ns = src.last_write100ns;
            out->Source.HasCreateFileObject = src.has_create_file_object;
            out->Source.HasOpenFileObject = src.has_open_file_object;
            out->Source.CreateFileObjectOrphaned = src.create_file_object_orphaned;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_CRASHDUMP_SET_ACTIVE: {
            ioctl_crashdump_set_active_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_crashdump_set_active_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            NTSTATUS set_status = crash_dump_set_active_src(in->Index);
            crash_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            crash_dump_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Index = in->Index;
            out->Status = set_status;
            out->ActiveSource = snap.active_src;
            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_BCL_SNAPSHOT: {
            ioctl_bcl_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            bcl_snapshot_t bcl;
            RtlZeroMemory(&bcl, sizeof(bcl));
            bcl_get_snapshot(&bcl);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = bcl.version;
            out->TestEnabled = bcl.test_enabled;
            out->CurrentNotificationType = bcl.current_notify_type;
            out->ParkingState = bcl.parking_state;
            out->DesiredParkedCpus = bcl.desired_parked_cpus;
            out->CurrentParkedCpus = bcl.current_parked_cpus;
            out->ThrottlePermille = bcl.throttle_permille;
            out->IsLowImaxState = bcl.is_low_imax_state;
            out->MitigationState = bcl.mitigation_state;
            out->ThresholdCount = bcl.threshold_cnt;
            out->SubmitCount = bcl.submit_cnt;
            out->SubmitAccepted = bcl.submit_accepted;
            out->SubmitRejected = bcl.submit_rejected;
            out->WorkItemRunCount = bcl.work_item_run_cnt;
            out->ThresholdOpCount = bcl.threshold_op_cnt;
            out->ThresholdDownstackSuccess = bcl.threshold_downstack_success;
            out->ThresholdDownstackFailure = bcl.threshold_downstack_failure;
            out->ThresholdCacheHits = bcl.threshold_cache_hits;
            out->ThresholdCacheMisses = bcl.threshold_cache_misses;
            out->EventSequence = bcl.event_sequence;
            out->HistoryCount = bcl.history_cnt;
            out->HistoryDropped = bcl.history_dropped;
            out->LastEventCode = bcl.last_event_code;
            out->LastEventStatus = bcl.last_event_status;
            out->LastEventTick100ns = bcl.last_event_tick100ns;
            out->LastResourceId = bcl.last_res_id;
            out->LastField = bcl.last_field;
            out->LastSubType = bcl.last_sub_type;
            out->LastValue0 = bcl.last_value0;
            out->LastValue1 = bcl.last_value1;
            out->LastFlag = bcl.last_flag;
            out->LastAux0 = bcl.last_aux0;

            ULONG thr_cnt = bcl.threshold_cnt;
            if (thr_cnt > RTL_NUMBER_OF(out->Thresholds))
                thr_cnt = RTL_NUMBER_OF(out->Thresholds);
            out->ThresholdCount = thr_cnt;
            for (ULONG i = 0; i < thr_cnt; i++) {
                out->Thresholds[i].ResourceId = bcl.thresholds[i].res_id;
                out->Thresholds[i].Field = bcl.thresholds[i].field;
                out->Thresholds[i].SubType = bcl.thresholds[i].sub_type;
                out->Thresholds[i].Value0 = bcl.thresholds[i].value0;
                out->Thresholds[i].Value1 = bcl.thresholds[i].value1;
                out->Thresholds[i].UpdateCount = bcl.thresholds[i].update_cnt;
                out->Thresholds[i].LastStatus = bcl.thresholds[i].last_status;
                out->Thresholds[i].LastTick100ns = bcl.thresholds[i].last_tick100ns;
                out->Thresholds[i].Flag = bcl.thresholds[i].flag;
                out->Thresholds[i].Reserved0 = bcl.thresholds[i].reserved0;
                out->Thresholds[i].Reserved1 = bcl.thresholds[i].reserved1;
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_BCL_HISTORY: {
            ioctl_bcl_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_bcl_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_bcl_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_bcl_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_bcl_event_info_t);
                break;
            }

            bcl_event_t* hist = (bcl_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(bcl_event_t), 'hBcQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(bcl_event_t));
            status = bcl_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_bcl_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].NotificationType = hist[i].notify_type;
                    out->Entries[i].ParkingState = hist[i].parking_state;
                    out->Entries[i].DesiredParkedCpus = hist[i].desired_parked_cpus;
                    out->Entries[i].ResourceId = hist[i].res_id;
                    out->Entries[i].Field = hist[i].field;
                    out->Entries[i].SubType = hist[i].sub_type;
                    out->Entries[i].Value0 = hist[i].value0;
                    out->Entries[i].Value1 = hist[i].value1;
                    out->Entries[i].Flag = hist[i].flag;
                    out->Entries[i].Aux0 = hist[i].aux0;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_bcl_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_BCL_THRESHOLDS: {
            ioctl_bcl_thresholds_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_bcl_thresholds_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_bcl_threshold_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_bcl_threshold_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_bcl_threshold_info_t);
                break;
            }

            bcl_snapshot_t bcl;
            RtlZeroMemory(&bcl, sizeof(bcl));
            bcl_get_snapshot(&bcl);

            ULONG count = bcl.threshold_cnt;
            if (count > cap)
                count = cap;

            out->Capacity = cap;
            out->Count = count;
            out->EntrySize = sizeof(ioctl_bcl_threshold_info_t);
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ResourceId = bcl.thresholds[i].res_id;
                out->Entries[i].Field = bcl.thresholds[i].field;
                out->Entries[i].SubType = bcl.thresholds[i].sub_type;
                out->Entries[i].Value0 = bcl.thresholds[i].value0;
                out->Entries[i].Value1 = bcl.thresholds[i].value1;
                out->Entries[i].UpdateCount = bcl.thresholds[i].update_cnt;
                out->Entries[i].LastStatus = bcl.thresholds[i].last_status;
                out->Entries[i].LastTick100ns = bcl.thresholds[i].last_tick100ns;
                out->Entries[i].Flag = bcl.thresholds[i].flag;
                out->Entries[i].Reserved0 = bcl.thresholds[i].reserved0;
                out->Entries[i].Reserved1 = bcl.thresholds[i].reserved1;
            }

            status = STATUS_SUCCESS;
            info = hdr_sz + ((size_t)count * sizeof(ioctl_bcl_threshold_info_t));
            break;
        }

        case IOCTL_PEP_BCL_SUBMIT_TEST: {
            ioctl_bcl_submit_test_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_bcl_submit_test_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            NTSTATUS submit_status = bcl_submit_test(in->NotificationType);
            bcl_snapshot_t bcl;
            RtlZeroMemory(&bcl, sizeof(bcl));
            bcl_get_snapshot(&bcl);

            RtlZeroMemory(out, sizeof(*out));
            out->NotificationType = in->NotificationType;
            out->Status = submit_status;
            out->CurrentNotificationType = bcl.current_notify_type;
            out->ParkingState = bcl.parking_state;
            out->DesiredParkedCpus = bcl.desired_parked_cpus;
            out->CurrentParkedCpus = bcl.current_parked_cpus;
            out->ThrottlePermille = bcl.throttle_permille;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_BCL_READSET_THRESHOLD: {
            ioctl_bcl_readset_threshold_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_bcl_readset_threshold_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG compat_in[5];
            ULONG compat_out[4];
            size_t written = 0;
            RtlZeroMemory(compat_in, sizeof(compat_in));
            RtlZeroMemory(compat_out, sizeof(compat_out));

            compat_in[0] = in->ResourceId;
            compat_in[1] = in->Field;
            compat_in[2] = in->Threshold;
            compat_in[3] = in->Value0;
            compat_in[4] = in->Flags;

            NTSTATUS op_status = bcl_read_set_threshold(device, compat_in, sizeof(compat_in),
                                                        compat_out, sizeof(compat_out), &written);

            RtlZeroMemory(out, sizeof(*out));
            out->ResourceId = in->ResourceId;
            out->Field = in->Field;
            out->Threshold = in->Threshold;
            out->Value0 = in->Value0;
            out->Flags = in->Flags;
            out->Status = op_status;

            if (NT_SUCCESS(op_status) && written >= sizeof(compat_out)) {
                out->ResourceId = compat_out[0];
                out->Field = compat_out[1];
                out->Threshold = compat_out[2];
                out->Value0 = compat_out[3];
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_DCVS_SNAPSHOT: {
            ioctl_dcvs_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            dcvs_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            dcvs_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->Initialized = snap.initialized;
            out->CpuCount = snap.cpu_cnt;
            out->DomainId = snap.domain_id;
            out->DomainMembers = snap.domain_members;
            out->CoordinationType = snap.coordination_type;
            out->WorstCaseTransitionLatency = snap.worst_case_transition_latency;
            out->WorstCaseTransitionOverhead = snap.worst_case_transition_overhead;
            out->EnablePerfSelection = snap.enable_perf_selection;
            out->EnableThermalPerfSelection = snap.enable_thermal_perf_selection;
            out->EnableBoost = snap.enable_boost;
            out->BoostFrequencyHz = snap.boost_freq_hz;
            out->MaxFrequencyHz = snap.max_freq_hz;
            out->MinFrequencyHz = snap.min_freq_hz;
            out->NominalFrequencyHz = snap.nominal_freq_hz;
            out->LowestNonlinearFrequencyHz = snap.lowest_nonlinear_freq_hz;
            out->ThermalHotDeciC = snap.thermal_hot_deci_c;
            out->ThermalCriticalDeciC = snap.thermal_critical_deci_c;
            out->ThermalCapHotPercent = snap.thermal_cap_hot_percent;
            out->ThermalCapCriticalPercent = snap.thermal_cap_critical_percent;
            out->CurrentThermalCapPercent = snap.current_thermal_cap_percent;
            out->PolicyUpdateCount = snap.policy_update_cnt;
            out->LastPolicyTick100ns = snap.last_policy_tick100ns;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventCpu = snap.last_event_cpu;
            out->LastEventMinPerfState = snap.last_event_min_perf_state;
            out->LastEventMaxPerfState = snap.last_event_max_perf_state;
            out->LastEventDesiredPerfState = snap.last_event_desired_perf_state;
            out->LastEventAppliedPerfLevel = snap.last_event_applied_perf_level;
            out->LastEventAppliedFrequencyHz = snap.last_event_applied_freq_hz;
            out->LastEventThermalCapPercent = snap.last_event_thermal_cap_percent;
            out->LastEventEpp = snap.last_event_epp;
            out->LastEventAux0 = snap.last_event_aux0;
            out->LastEventAux1 = snap.last_event_aux1;
            out->LastEventTick100ns = snap.last_event_tick100ns;

            ULONG perf_cnt = snap.perf_table_cnt;
            if (perf_cnt > RTL_NUMBER_OF(out->PerfTableHz))
                perf_cnt = RTL_NUMBER_OF(out->PerfTableHz);
            out->PerfTableCount = perf_cnt;
            for (ULONG i = 0; i < perf_cnt; i++)
                out->PerfTableHz[i] = snap.perf_table_hz[i];

            ULONG cpu_cnt = snap.cpu_entry_cnt;
            if (cpu_cnt > RTL_NUMBER_OF(out->Cpus))
                cpu_cnt = RTL_NUMBER_OF(out->Cpus);
            out->CpuEntryCount = cpu_cnt;
            for (ULONG i = 0; i < cpu_cnt; i++) {
                out->Cpus[i].Cpu = snap.cpus[i].cpu;
                out->Cpus[i].DomainIndex = snap.cpus[i].domain_idx;
                out->Cpus[i].CurrentPerfLevel = snap.cpus[i].current_perf_level;
                out->Cpus[i].CurrentFrequencyHz = snap.cpus[i].current_freq_hz;
                out->Cpus[i].RequestedPerfStateIndex = snap.cpus[i].requested_perf_state_idx;
                out->Cpus[i].MinPerf = snap.cpus[i].min_perf;
                out->Cpus[i].MaxPerf = snap.cpus[i].max_perf;
                out->Cpus[i].DesiredPerf = snap.cpus[i].desired_perf;
                out->Cpus[i].Epp = snap.cpus[i].epp;
                out->Cpus[i].EnterTime100ns = snap.cpus[i].enter_time100ns;
                out->Cpus[i].Residency100ns = snap.cpus[i].residency100ns;
                out->Cpus[i].TransitionCount = snap.cpus[i].transition_cnt;
                out->Cpus[i].NormalizedPerformance = snap.cpus[i].normalized_performance;
                out->Cpus[i].Reserved = snap.cpus[i].reserved;
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_DCVS_HISTORY: {
            ioctl_dcvs_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_dcvs_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_dcvs_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_dcvs_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_dcvs_event_info_t);
                break;
            }

            dcvs_event_t* hist = (dcvs_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(dcvs_event_t), 'hDcQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(dcvs_event_t));
            status = dcvs_get_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_dcvs_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].Cpu = hist[i].cpu;
                    out->Entries[i].MinPerfState = hist[i].min_perf_state;
                    out->Entries[i].MaxPerfState = hist[i].max_perf_state;
                    out->Entries[i].DesiredPerfState = hist[i].desired_perf_state;
                    out->Entries[i].AppliedPerfLevel = hist[i].applied_perf_level;
                    out->Entries[i].AppliedFrequencyHz = hist[i].applied_freq_hz;
                    out->Entries[i].ThermalCapPercent = hist[i].thermal_cap_percent;
                    out->Entries[i].Epp = hist[i].epp;
                    out->Entries[i].Aux0 = hist[i].aux0;
                    out->Entries[i].Aux1 = hist[i].aux1;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_dcvs_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_DCVS_PERF_TABLE: {
            ioctl_dcvs_perf_table_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_dcvs_perf_table_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + sizeof(ULONG), (PVOID*)&out,
                                                    &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ULONG));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ULONG);
                break;
            }

            ULONG* perf =
                (ULONG*)ExAllocatePool2(POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(ULONG), 'tDcQ');
            if (!perf) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            RtlZeroMemory(perf, (size_t)cap * sizeof(ULONG));
            status = dcvs_get_perf_table(perf, cap, &count);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->EntrySize = sizeof(ULONG);
                for (ULONG i = 0; i < count; i++)
                    out->Entries[i] = perf[i];
                info = hdr_sz + ((size_t)count * sizeof(ULONG));
            }

            ExFreePool(perf);
            break;
        }

        case IOCTL_PEP_DCVS_SET_POLICY: {
            ioctl_dcvs_set_policy_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_dcvs_set_policy_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            NTSTATUS set_status =
                dcvs_set_policy(in->Flags, in->EnablePerfSelection, in->EnableThermalPerfSelection,
                                in->ThermalHotDeciC, in->ThermalCriticalDeciC,
                                in->ThermalCapHotPercent, in->ThermalCapCriticalPercent);

            dcvs_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            dcvs_get_snapshot(&snap);

            RtlZeroMemory(out, sizeof(*out));
            out->Flags = in->Flags;
            out->Status = set_status;
            out->AppliedEnablePerfSelection = snap.enable_perf_selection;
            out->AppliedEnableThermalPerfSelection = snap.enable_thermal_perf_selection;
            out->AppliedThermalHotDeciC = snap.thermal_hot_deci_c;
            out->AppliedThermalCriticalDeciC = snap.thermal_critical_deci_c;
            out->AppliedThermalCapHotPercent = snap.thermal_cap_hot_percent;
            out->AppliedThermalCapCriticalPercent = snap.thermal_cap_critical_percent;
            out->CurrentThermalCapPercent = snap.current_thermal_cap_percent;
            out->PolicyUpdateCount = snap.policy_update_cnt;
            out->LastPolicyTick100ns = snap.last_policy_tick100ns;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_PARKSEL_SNAPSHOT: {
            ioctl_parksel_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            parksel_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = pep_get_park_selection_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->CpuCount = snap.cpu_cnt;
            out->ActiveCpuCount = snap.active_cpu_cnt;
            out->LastAdditionalUnparked = snap.last_additional_unparked;
            out->LastEvaluationType = snap.last_evaluation_type;
            out->LastEvaluationTime = snap.last_evaluation_time;
            out->TimerThreshold0Sec = snap.timer_threshold0_sec;
            out->TimerThreshold1Sec = snap.timer_threshold1_sec;
            out->CoreRotationStat0 = snap.core_rotation_stat0;
            out->CoreRotationStat1 = snap.core_rotation_stat1;
            out->Cluster0Unparked = snap.cluster0_unparked;
            out->Cluster1Unparked = snap.cluster1_unparked;
            out->PrefRailId = snap.pref_rail_id;
            out->TimerRailId = snap.timer_rail_id;
            out->NumUnparkRailId = snap.num_unpark_rail_id;
            out->CoreRotationRailId = snap.core_rotation_rail_id;
            out->LastCluster0EvaluationTime = snap.last_cluster0_evaluation_time;
            out->LastCluster1EvaluationTime = snap.last_cluster1_evaluation_time;
            out->ToggleState = snap.toggle_state;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventAdditionalUnparked = snap.last_event_additional_unparked;
            out->LastEventRemainingUnparked = snap.last_event_remaining_unparked;
            out->LastEventTick100ns = snap.last_event_tick100ns;
            out->ThermalRankEnabled = snap.thermal_rank_enabled;
            out->ThermalRankHysteresisDeciC = snap.thermal_rank_hysteresis_deci_c;
            out->ThermalRankUpdateIntervalMs = snap.thermal_rank_update_interval_ms;
            out->ThermalSensorBaseIndex = snap.thermal_sensor_base_idx;
            out->ThermalRefreshCount = snap.thermal_refresh_cnt;
            out->ThermalApplyCount = snap.thermal_apply_cnt;
            out->LastThermalStatus = snap.last_thermal_status;

            ULONG rank_cnt = snap.active_cpu_cnt;
            if (rank_cnt == 0 || rank_cnt > RTL_NUMBER_OF(out->RankPreference))
                rank_cnt = snap.cpu_cnt;
            if (rank_cnt > RTL_NUMBER_OF(out->RankPreference))
                rank_cnt = RTL_NUMBER_OF(out->RankPreference);
            out->RankEntryCount = rank_cnt;
            for (ULONG i = 0; i < rank_cnt; i++) {
                out->RankPreference[i] = snap.rank_preference[i];
                out->PreviousRankPreference[i] = snap.previous_rank_preference[i];
                out->RankCpuTempDeciC[i] = snap.rank_cpu_temp_deci_c[i];
            }

            ULONG cpu_cnt = snap.cpu_cnt;
            if (cpu_cnt > RTL_NUMBER_OF(out->Cpus))
                cpu_cnt = RTL_NUMBER_OF(out->Cpus);
            out->CpuEntryCount = cpu_cnt;
            for (ULONG i = 0; i < cpu_cnt; i++) {
                ULONG rank = snap.current_rank[i];
                LONG temp_deci_c = 0;
                if (rank < rank_cnt)
                    temp_deci_c = snap.rank_cpu_temp_deci_c[rank];

                out->Cpus[i].Cpu = i;
                out->Cpus[i].CurrentParkState = snap.current_park_state[i];
                out->Cpus[i].PreviousParkState = snap.previous_park_state[i];
                out->Cpus[i].CurrentPreference = snap.current_preference[i];
                out->Cpus[i].CurrentRank = snap.current_rank[i];
                out->Cpus[i].TransitionCount = snap.transition_cnt[i];
                out->Cpus[i].TemperatureDeciC = temp_deci_c;
            }

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_PARKSEL_HISTORY: {
            ioctl_parksel_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_parksel_history_response_t, Entries);
            status = WdfRequestRetrieveOutputBuffer(
                req_arg, hdr_sz + sizeof(ioctl_parksel_event_info_t), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / sizeof(ioctl_parksel_event_info_t));
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + sizeof(ioctl_parksel_event_info_t);
                break;
            }

            parksel_event_t* hist = (parksel_event_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, (size_t)cap * sizeof(parksel_event_t), 'hPkQ');
            if (!hist) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, (size_t)cap * sizeof(parksel_event_t));
            status = pep_get_park_selection_history(hist, cap, &count, &dropped);
            if (NT_SUCCESS(status)) {
                out->Capacity = cap;
                out->Count = count;
                out->Dropped = dropped;
                out->EntrySize = sizeof(ioctl_parksel_event_info_t);
                for (ULONG i = 0; i < count; i++) {
                    out->Entries[i].Tick100ns = hist[i].tick100ns;
                    out->Entries[i].Sequence = hist[i].sequence;
                    out->Entries[i].EventCode = hist[i].event_code;
                    out->Entries[i].Status = hist[i].status;
                    out->Entries[i].Cpu = hist[i].cpu;
                    out->Entries[i].CpuCount = hist[i].cpu_cnt;
                    out->Entries[i].AdditionalUnparked = hist[i].additional_unparked;
                    out->Entries[i].RemainingUnparked = hist[i].remaining_unparked;
                    out->Entries[i].EvaluationType = hist[i].evaluation_type;
                    out->Entries[i].EvaluationTime = hist[i].evaluation_time;
                    out->Entries[i].Cluster0Unparked = hist[i].cluster0_unparked;
                    out->Entries[i].Cluster1Unparked = hist[i].cluster1_unparked;
                    out->Entries[i].Aux0 = hist[i].aux0;
                    out->Entries[i].Aux1 = hist[i].aux1;
                }
                info = hdr_sz + ((size_t)count * sizeof(ioctl_parksel_event_info_t));
            }

            ExFreePool(hist);
            break;
        }

        case IOCTL_PEP_PARKSEL_GET_POLICY: {
            ioctl_parksel_policy_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            parksel_policy_t policy;
            RtlZeroMemory(&policy, sizeof(policy));
            NTSTATUS pol_status = pep_get_park_selection_policy(&policy);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = policy.version;
            out->Status = pol_status;
            out->AppliedFlags = policy.flags;
            out->ThermalRankEnabled = policy.thermal_rank_enabled;
            out->ThermalRankHysteresisDeciC = policy.thermal_rank_hysteresis_deci_c;
            out->ThermalRankUpdateIntervalMs = policy.thermal_rank_update_interval_ms;
            out->ThermalSensorBaseIndex = policy.thermal_sensor_base_idx;
            out->TimerThreshold0Sec = policy.timer_threshold0_sec;
            out->TimerThreshold1Sec = policy.timer_threshold1_sec;
            out->ThermalRefreshCount = policy.thermal_refresh_cnt;
            out->ThermalApplyCount = policy.thermal_apply_cnt;
            out->LastThermalStatus = policy.last_thermal_status;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_PARKSEL_SET_POLICY: {
            ioctl_parksel_policy_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_parksel_policy_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            parksel_policy_t req;
            RtlZeroMemory(&req, sizeof(req));
            req.version = in->Version;
            req.flags = in->Flags;
            req.thermal_rank_enabled = in->ThermalRankEnabled;
            req.thermal_rank_hysteresis_deci_c = in->ThermalRankHysteresisDeciC;
            req.thermal_rank_update_interval_ms = in->ThermalRankUpdateIntervalMs;
            req.thermal_sensor_base_idx = in->ThermalSensorBaseIndex;
            req.timer_threshold0_sec = in->TimerThreshold0Sec;
            req.timer_threshold1_sec = in->TimerThreshold1Sec;

            NTSTATUS set_status = pep_set_park_selection_policy(&req);

            parksel_policy_t applied;
            RtlZeroMemory(&applied, sizeof(applied));
            NTSTATUS get_status = pep_get_park_selection_policy(&applied);

            RtlZeroMemory(out, sizeof(*out));
            out->Version = applied.version;
            out->Status = NT_SUCCESS(set_status) ? get_status : set_status;
            out->AppliedFlags = in->Flags;
            out->ThermalRankEnabled = applied.thermal_rank_enabled;
            out->ThermalRankHysteresisDeciC = applied.thermal_rank_hysteresis_deci_c;
            out->ThermalRankUpdateIntervalMs = applied.thermal_rank_update_interval_ms;
            out->ThermalSensorBaseIndex = applied.thermal_sensor_base_idx;
            out->TimerThreshold0Sec = applied.timer_threshold0_sec;
            out->TimerThreshold1Sec = applied.timer_threshold1_sec;
            out->ThermalRefreshCount = applied.thermal_refresh_cnt;
            out->ThermalApplyCount = applied.thermal_apply_cnt;
            out->LastThermalStatus = applied.last_thermal_status;

            status = STATUS_SUCCESS;
            info = sizeof(*out);
            break;
        }

        case IOCTL_PEP_RES_SNAPSHOT: {
            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 64, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                size_t written = 0;
                status = res_snapshot(out, out_sz, &written);
                info = written;
            }
            break;
        }

        case IOCTL_PEP_ARM_WAKE:
            status = WdfDeviceStopIdle(device, TRUE);
            info = 0;
            break;

        case IOCTL_PEP_DISARM_WAKE:
            WdfDeviceResumeIdle(device);
            status = STATUS_SUCCESS;
            info = 0;
            break;

        case IOCTL_PEP_TSENS_GET_TEMPS: {
            PVOID out = NULL;
            size_t out_sz = 0;
            size_t min_out = sizeof(USHORT) * ((size_t)tsens_get_cnt() + 1);
            status = WdfRequestRetrieveOutputBuffer(req_arg, min_out, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                size_t written = 0;
                status = tsens_get_temperatures(out, out_sz, &written);
                info = written;
            }
            break;
        }

        case IOCTL_PEP_TSENS_GET_TEMP_FOR_IDX: {
            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 2, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                PVOID in = NULL;
                size_t in_sz = 0;
                status = WdfRequestRetrieveInputBuffer(req_arg, 2, &in, &in_sz);
                if (NT_SUCCESS(status)) {
                    size_t written = 0;
                    status = tsens_get_temp_for_idx(in, in_sz, out, out_sz, &written);
                    info = written;
                }
            }
            break;
        }

        case IOCTL_PEP_TSENS_STABLE_REQ: {
            status = tsens_process_stable_req(req_arg, device, &info);
            break;
        }

        case IOCTL_PEP_MMRM_POLICY_NOTSUP: {

            if (output_buf_length != 0 && input_buf_length < (sizeof(ULONG) * 3)) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            if (input_buf_length >= (sizeof(ULONG) * 3)) {
                ULONG* in = NULL;
                size_t in_sz = 0;
                status =
                    WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG) * 3, (PVOID*)&in, &in_sz);
                if (!NT_SUCCESS(status) || !in) {
                    info = 0;
                    break;
                }

                ULONG timeout = in[0];
                LONG low = (LONG)in[1];
                LONG high = (LONG)in[2];
                ULONG sensor_id = registry_query_u_long(L"ThermalSensorIndex", 0);
                if (sensor_id >= tsens_get_cnt())
                    sensor_id = 0;

                if (timeout != 0) {
                    p_dev_ctx_t dev_ctx = get_dev_ctx(device);
                    if (dev_ctx) {
                        dev_ctx->thermal_req_id = (ULONG)InterlockedIncrement(&g_thermal_req_seq);
                        ULONGLONG now_ticks = KeQueryInterruptTime();
                        ULONGLONG delta_ticks = (ULONGLONG)timeout * 10000ull;
                        ULONGLONG expire_at = now_ticks + delta_ticks;
                        if (expire_at < now_ticks)
                            expire_at = (ULONGLONG)-1;

                        dev_ctx->thermal_low_temp_deci_k = low;
                        dev_ctx->thermal_high_temp_deci_k = high;
                        dev_ctx->thermal_limit_expire_tick100ns = expire_at;
                    }
                    tsens_add_limit(sensor_id, device);
                }

                tsens_threshold_data_t thresholds;
                thresholds.low_temp = low;
                thresholds.high_temp = high;
                thresholds.high_temp_owner_obj = (WDFOBJECT)device;
                thresholds.tsens_temp10_c = 0;
                tsens_bracket(sensor_id, &thresholds);

                res_key_t k = {0x30524854u, io_control_code};
                res_value_t v = {timeout, (ULONG)thresholds.low_temp, (ULONG)thresholds.high_temp,
                                 (ULONG)thresholds.tsens_temp10_c};
                (VOID) res_set(k, v);

                LONG t = thresholds.tsens_temp10_c;
                ULONG temp_deci_k = (t >= 0) ? (ULONG)(t + 2730) : (ULONG)(2730 - (-t));
                if (timeout != 0) {
                    mmrm_signal_policy_update_from_temp(thresholds.low_temp, thresholds.high_temp,
                                                        t);
                    if (output_buf_length == 0) {
                        tsens_remove_limit(sensor_id, device);
                        status = STATUS_SUCCESS;
                        info = 0;
                        break;
                    }

                    ULONG* out = NULL;
                    size_t out_sz = 0;
                    status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ULONG), (PVOID*)&out,
                                                            &out_sz);
                    if (!NT_SUCCESS(status)) {
                        tsens_remove_limit(sensor_id, device);
                        if (status == STATUS_BUFFER_TOO_SMALL)
                            info = 0;
                        else
                            info = 0;
                        break;
                    }
                    if (out_sz != sizeof(ULONG)) {
                        tsens_remove_limit(sensor_id, device);
                        status = STATUS_INVALID_PARAMETER;
                        info = 0;
                        break;
                    }

                    status = thermal_req_queue(req_arg, device, sensor_id, thresholds.low_temp,
                                               thresholds.high_temp, timeout);
                    if (status != STATUS_PENDING)
                        tsens_remove_limit(sensor_id, device);
                    info = 0;
                    break;
                }

                if (output_buf_length == 0) {
                    status = STATUS_SUCCESS;
                    info = 0;
                    break;
                }

                ULONG* out = NULL;
                size_t out_sz = 0;
                status =
                    WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ULONG), (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {

                    if (out_sz != sizeof(ULONG)) {
                        status = STATUS_INVALID_PARAMETER;
                        info = 0;
                        break;
                    }

                    *out = temp_deci_k;
                    info = sizeof(ULONG);
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {

                    info = 0;
                }
            } else {

                status = STATUS_NOT_SUPPORTED;
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_MMRM_POLICY_READ: {

            if (input_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            if (input_buf_length >= sizeof(mmrm_policy_req_t)) {
                mmrm_policy_req_t* in_req = NULL;
                size_t in_sz = 0;
                status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(mmrm_policy_req_t),
                                                       (PVOID*)&in_req, &in_sz);
                if (NT_SUCCESS(status) && in_req) {
                    res_key_t k = {0x30524D4Du, io_control_code};
                    res_value_t v = {
                        in_req->version, in_req->activation_reasons, in_req->passive_limit,
                        (ULONG)in_req->wait_for_update | ((ULONG)in_req->hibernate << 8) |
                            ((ULONG)in_req->critical << 16) |
                            ((ULONG)in_req->thermal_standby << 24)};
                    (VOID) res_set(k, v);
                    mmrm_capture_policy_req(in_req);

                    if (in_req->wait_for_update != 0) {
                        status = mmrm_ensure_pending_infra(device);
                        if (!NT_SUCCESS(status)) {
                            info = 0;
                            break;
                        }

                        WdfSpinLockAcquire(g_mmrm_pending.lock);
                        if (g_mmrm_pending.req != NULL) {
                            WdfSpinLockRelease(g_mmrm_pending.lock);
                            status = STATUS_DEVICE_BUSY;
                            info = 0;
                            break;
                        }
                        ULONG timeout_ms = mmrm_read_wait_timeout_ms();
                        ULONG poll_ms = mmrm_read_wait_poll_ms();
                        BOOLEAN complete_on_timeout =
                            registry_query_u_long(L"MmrmPolicyWaitCompleteOnTimeout", 1) ? TRUE
                                                                                         : FALSE;
                        g_mmrm_pending.req = req_arg;
                        g_mmrm_pending.req_payload = *in_req;
                        g_mmrm_pending.wait_timeout_ms = timeout_ms;
                        g_mmrm_pending.poll_interval_ms = poll_ms;
                        g_mmrm_pending.wait_deadline_tick100ns =
                            mmrm_compute_deadline_tick100ns(timeout_ms);
                        g_mmrm_pending.complete_on_timeout = complete_on_timeout;
                        g_mmrm_pending.timed_out = FALSE;
                        g_mmrm_pending.completion_queued = FALSE;
                        WdfSpinLockRelease(g_mmrm_pending.lock);

                        status = WdfRequestMarkCancelableEx(req_arg, mmrm_pending_cancel_fn);
                        if (!NT_SUCCESS(status)) {
                            WdfSpinLockAcquire(g_mmrm_pending.lock);
                            if (g_mmrm_pending.req == req_arg) {
                                g_mmrm_pending.req = NULL;
                                RtlZeroMemory(&g_mmrm_pending.req_payload,
                                              sizeof(g_mmrm_pending.req_payload));
                                g_mmrm_pending.wait_deadline_tick100ns = 0;
                                g_mmrm_pending.wait_timeout_ms = 0;
                                g_mmrm_pending.poll_interval_ms = MMRM_WAIT_DEFAULT_POLL_MS;
                                g_mmrm_pending.complete_on_timeout = TRUE;
                                g_mmrm_pending.timed_out = FALSE;
                                g_mmrm_pending.completion_queued = FALSE;
                            }
                            WdfSpinLockRelease(g_mmrm_pending.lock);
                            mmrm_set_policy_wait_active(FALSE);
                            info = 0;
                            break;
                        }

                        mmrm_set_policy_wait_active(TRUE);

                        if (g_mmrm_pending.timer)
                            (VOID) WdfTimerStart(g_mmrm_pending.timer, WDF_REL_TIMEOUT_IN_MS(1));

                        if ((registry_query_u_long(L"MmrmWaitImmediateComplete", 0) ||
                             mmrm_query_updated_flag() != 0) &&
                            g_mmrm_pending.work_item)
                            mmrm_pending_notify_update(NULL);
                        status = STATUS_PENDING;
                        info = 0;
                    } else {
                        ULONG updated = mmrm_query_updated_flag();
                        status = mmrm_build_policy_reply(req_arg, in_req, updated, &info);
                        if (NT_SUCCESS(status) && updated != 0)
                            mmrm_clear_updated_flag();
                    }
                }
                break;
            }

            if (input_buf_length >= sizeof(THERMAL_POLICY) &&
                output_buf_length >= sizeof(THERMAL_POLICY)) {
                THERMAL_POLICY* in_pol = NULL;
                size_t in_sz = 0;
                status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(THERMAL_POLICY),
                                                       (PVOID*)&in_pol, &in_sz);
                if (NT_SUCCESS(status)) {
                    THERMAL_POLICY* out_pol = NULL;
                    size_t out_sz = 0;
                    status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(THERMAL_POLICY),
                                                            (PVOID*)&out_pol, &out_sz);
                    if (NT_SUCCESS(status)) {
                        *out_pol = *in_pol;
                        info = sizeof(THERMAL_POLICY);
                        status = STATUS_SUCCESS;
                    } else if (status == STATUS_BUFFER_TOO_SMALL) {
                        info = sizeof(THERMAL_POLICY);
                    }
                }
                break;
            }

            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG), &in, &in_sz);
            if (NT_SUCCESS(status)) {
                PVOID out = NULL;
                size_t out_sz = 0;
                if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(req_arg, 1, &out, &out_sz)) && out &&
                    out_sz) {
                    RtlZeroMemory(out, out_sz);
                    info = out_sz;
                } else {
                    info = 0;
                }
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_BRANCHTRACK_QUERY: {

            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            if (in[0] == 1u || in[0] == 2u) {
                UCHAR* out = NULL;
                size_t out_sz = 0;
                size_t written = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 1, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    status = (in[0] == 1u)
                                 ? res_build_branch_track_status_buf(out, out_sz, &written)
                                 : res_build_branch_track_details_buf(out, out_sz, &written);
                    if (NT_SUCCESS(status)) {
                        info = written;
                    } else if (status == STATUS_BUFFER_TOO_SMALL) {

                        info = 0;
                    }
                } else if (status == STATUS_BUFFER_TOO_SMALL) {

                    info = 0;
                }
                break;
            }

            status = STATUS_NOT_SUPPORTED;
            info = 0;
            break;
        }

        case IOCTL_PEP_EXSOC_SNAPSHOT: {
            ioctl_exsoc_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            exsoc_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = ex_soc_get_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->DeviceCount = snap.dev_cnt;
            out->ApplyCount = snap.apply_cnt;
            out->SuccessCount = snap.success_cnt;
            out->FailureCount = snap.failure_cnt;
            out->AdspStateIoctlCount = snap.adsp_state_ioctl_cnt;
            out->AdspCtrlIoctlCount = snap.adsp_ctrl_ioctl_cnt;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastIoctlCode = snap.last_ioctl_code;
            out->LastStatus = snap.last_status;
            out->LastDeviceHash = snap.last_dev_hash;
            out->LastDState = snap.last_d_state;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG dev_cnt = snap.dev_cnt;
            if (dev_cnt > RTL_NUMBER_OF(out->Devices))
                dev_cnt = RTL_NUMBER_OF(out->Devices);
            out->DeviceEntryCount = dev_cnt;
            for (ULONG i = 0; i < dev_cnt; i++) {
                out->Devices[i].DeviceHash = snap.devices[i].dev_hash;
                out->Devices[i].DeviceIdLength = snap.devices[i].dev_id_length;
                out->Devices[i].LastDState = snap.devices[i].last_d_state;
                out->Devices[i].LastIoctlCode = snap.devices[i].last_ioctl_code;
                out->Devices[i].ApplyCount = snap.devices[i].apply_cnt;
                out->Devices[i].SuccessCount = snap.devices[i].success_cnt;
                out->Devices[i].FailureCount = snap.devices[i].failure_cnt;
                out->Devices[i].LastTick100ns = snap.devices[i].last_tick100ns;
                RtlCopyMemory(out->Devices[i].DeviceId, snap.devices[i].dev_id,
                              RTL_NUMBER_OF(out->Devices[i].DeviceId));
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_EXSOC_HISTORY: {
            ioctl_exsoc_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_exsoc_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_exsoc_event_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_EXSOC_HISTORY_INFO_CAPACITY)
                cap = IOCTL_EXSOC_HISTORY_INFO_CAPACITY;

            exsoc_event_t hist[IOCTL_EXSOC_HISTORY_INFO_CAPACITY];
            RtlZeroMemory(hist, sizeof(hist));

            ULONG count = 0;
            ULONG dropped = 0;
            status = ex_soc_get_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Tick100ns = hist[i].tick100ns;
                out->Entries[i].Sequence = hist[i].sequence;
                out->Entries[i].IoctlCode = hist[i].ioctl_code;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].DeviceHash = hist[i].dev_hash;
                out->Entries[i].DState = hist[i].d_state;
                out->Entries[i].RequestedOutputBytes = hist[i].requested_output_bytes;
                out->Entries[i].ReturnedInformation = hist[i].returned_information;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            break;
        }

        case IOCTL_PEP_RSC_SNAPSHOT: {
            ioctl_rsc_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->NodeCount = snap.node_cnt;
            out->ActiveMask = snap.active_mask;
            out->Reserved = snap.reserved;
            out->EventSequence = snap.event_sequence;
            out->HistoryCount = snap.history_cnt;
            out->HistoryDropped = snap.history_dropped;
            out->LastEventSubsystemId = snap.last_event_subsystem_id;
            out->LastEventCode = snap.last_event_code;
            out->LastEventStatus = snap.last_event_status;
            out->LastEventTick100ns = snap.last_event_tick100ns;
            out->LastIoctlCode = snap.last_ioctl_code;
            out->LastTag = snap.last_tag;
            out->LastValue0 = snap.last_value0;
            out->LastValue1 = snap.last_value1;
            out->LastValue2 = snap.last_value2;
            out->LastValue3 = snap.last_value3;
            out->LastFlags = snap.last_flags;
            out->BatchApplyCount = snap.batch_apply_cnt;
            out->BatchCommandCount = snap.batch_command_cnt;
            out->BatchErrorCount = snap.batch_error_cnt;
            out->LastBatchCount = snap.last_batch_cnt;
            out->LastBatchStatus = snap.last_batch_status;
            out->LastBatchTick100ns = snap.last_batch_tick100ns;
            out->ScenarioRunCount = snap.scenario_run_cnt;
            out->ScenarioFailureCount = snap.scenario_failure_cnt;
            out->LastScenarioId = snap.last_scenario_id;
            out->LastScenarioStatus = snap.last_scenario_status;
            out->LastScenarioIterations = snap.last_scenario_iterations;
            out->LastScenarioTick100ns = snap.last_scenario_tick100ns;

            ULONG node_cnt = snap.node_cnt;
            if (node_cnt > IOCTL_RSC_NODE_INFO_CAPACITY)
                node_cnt = IOCTL_RSC_NODE_INFO_CAPACITY;
            out->NodeEntryCount = node_cnt;
            for (ULONG i = 0; i < node_cnt; i++) {
                out->Nodes[i].SubsystemId = snap.nodes[i].subsystem_id;
                RtlCopyMemory(out->Nodes[i].Name, snap.nodes[i].name, sizeof(out->Nodes[i].Name));
                out->Nodes[i].Present = snap.nodes[i].present;
                out->Nodes[i].State = snap.nodes[i].state;
                out->Nodes[i].ClockKhz = snap.nodes[i].clock_khz;
                out->Nodes[i].Flags = snap.nodes[i].flags;
                out->Nodes[i].LastIoctlCode = snap.nodes[i].last_ioctl_code;
                out->Nodes[i].LastTag = snap.nodes[i].last_tag;
                out->Nodes[i].LastValue0 = snap.nodes[i].last_value0;
                out->Nodes[i].LastValue1 = snap.nodes[i].last_value1;
                out->Nodes[i].LastValue2 = snap.nodes[i].last_value2;
                out->Nodes[i].LastValue3 = snap.nodes[i].last_value3;
                out->Nodes[i].RequestCount = snap.nodes[i].req_cnt;
                out->Nodes[i].SuccessCount = snap.nodes[i].success_cnt;
                out->Nodes[i].FailureCount = snap.nodes[i].failure_cnt;
                out->Nodes[i].VoteCount = snap.nodes[i].vote_cnt;
                out->Nodes[i].CommandCount = snap.nodes[i].command_cnt;
                out->Nodes[i].PendingCount = snap.nodes[i].pending_cnt;
                out->Nodes[i].LastStatus = snap.nodes[i].last_status;
                out->Nodes[i].LastTick100ns = snap.nodes[i].last_tick100ns;
                out->Nodes[i].ActivityAccumulator = snap.nodes[i].activity_accumulator;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_HISTORY: {
            ioctl_rsc_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_event_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_HISTORY_INFO_CAPACITY;

            rscsw_event_t hist[IOCTL_RSC_HISTORY_INFO_CAPACITY];
            RtlZeroMemory(hist, sizeof(hist));

            ULONG count = 0;
            ULONG dropped = 0;
            status = rsc_sw_get_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Tick100ns = hist[i].tick100ns;
                out->Entries[i].Sequence = hist[i].sequence;
                out->Entries[i].SubsystemId = hist[i].subsystem_id;
                out->Entries[i].EventCode = hist[i].event_code;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].IoctlCode = hist[i].ioctl_code;
                out->Entries[i].Tag = hist[i].tag;
                out->Entries[i].Value0 = hist[i].value0;
                out->Entries[i].Value1 = hist[i].value1;
                out->Entries[i].Value2 = hist[i].value2;
                out->Entries[i].Value3 = hist[i].value3;
                out->Entries[i].Flags = hist[i].flags;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            break;
        }

        case IOCTL_PEP_RSC_SET_NODE: {
            ioctl_rsc_set_node_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            status = rsc_sw_apply_node(in->SubsystemId, in->State, in->ClockKhz, in->Flags, in->Tag,
                                       in->Value0, in->Value1, in->Value2, in->Value3,
                                       in->IoctlCode ? in->IoctlCode : io_control_code);
            if (!NT_SUCCESS(status))
                break;

            ioctl_rsc_set_node_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                rscsw_node_state_t node;
                RtlZeroMemory(&node, sizeof(node));
                status = rsc_sw_query_node(in->SubsystemId, &node);
                if (NT_SUCCESS(status)) {
                    RtlZeroMemory(out, sizeof(*out));
                    out->Status = STATUS_SUCCESS;
                    out->Node.SubsystemId = node.subsystem_id;
                    RtlCopyMemory(out->Node.Name, node.name, sizeof(out->Node.Name));
                    out->Node.Present = node.present;
                    out->Node.State = node.state;
                    out->Node.ClockKhz = node.clock_khz;
                    out->Node.Flags = node.flags;
                    out->Node.LastIoctlCode = node.last_ioctl_code;
                    out->Node.LastTag = node.last_tag;
                    out->Node.LastValue0 = node.last_value0;
                    out->Node.LastValue1 = node.last_value1;
                    out->Node.LastValue2 = node.last_value2;
                    out->Node.LastValue3 = node.last_value3;
                    out->Node.RequestCount = node.req_cnt;
                    out->Node.SuccessCount = node.success_cnt;
                    out->Node.FailureCount = node.failure_cnt;
                    out->Node.VoteCount = node.vote_cnt;
                    out->Node.CommandCount = node.command_cnt;
                    out->Node.PendingCount = node.pending_cnt;
                    out->Node.LastStatus = node.last_status;
                    out->Node.LastTick100ns = node.last_tick100ns;
                    out->Node.ActivityAccumulator = node.activity_accumulator;
                    info = sizeof(*out);
                    status = STATUS_SUCCESS;
                }
            } else if (status == STATUS_BUFFER_TOO_SMALL) {

                status = STATUS_SUCCESS;
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_QUERY_NODE: {
            ioctl_rsc_query_node_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            ioctl_rsc_query_node_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_node_state_t node;
            RtlZeroMemory(&node, sizeof(node));
            status = rsc_sw_query_node(in->SubsystemId, &node);
            RtlZeroMemory(out, sizeof(*out));
            out->Status = status;
            if (NT_SUCCESS(status)) {
                out->Node.SubsystemId = node.subsystem_id;
                RtlCopyMemory(out->Node.Name, node.name, sizeof(out->Node.Name));
                out->Node.Present = node.present;
                out->Node.State = node.state;
                out->Node.ClockKhz = node.clock_khz;
                out->Node.Flags = node.flags;
                out->Node.LastIoctlCode = node.last_ioctl_code;
                out->Node.LastTag = node.last_tag;
                out->Node.LastValue0 = node.last_value0;
                out->Node.LastValue1 = node.last_value1;
                out->Node.LastValue2 = node.last_value2;
                out->Node.LastValue3 = node.last_value3;
                out->Node.RequestCount = node.req_cnt;
                out->Node.SuccessCount = node.success_cnt;
                out->Node.FailureCount = node.failure_cnt;
                out->Node.VoteCount = node.vote_cnt;
                out->Node.CommandCount = node.command_cnt;
                out->Node.PendingCount = node.pending_cnt;
                out->Node.LastStatus = node.last_status;
                out->Node.LastTick100ns = node.last_tick100ns;
                out->Node.ActivityAccumulator = node.activity_accumulator;
                info = sizeof(*out);
                status = STATUS_SUCCESS;
            } else {
                info = sizeof(*out);
            }
            break;
        }

        case IOCTL_PEP_RSC_APPLY_BATCH: {
            ioctl_rsc_apply_batch_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            if (in->Count == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            size_t req_bytes = FIELD_OFFSET(ioctl_rsc_apply_batch_request_t, Entries) +
                               ((size_t)in->Count * sizeof(ioctl_rsc_batch_entry_request_t));
            if (in_sz < req_bytes) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            ULONG applied = 0;
            ULONG failed = 0;
            NTSTATUS first_failure = STATUS_SUCCESS;
            status = rsc_sw_apply_batch((const rscsw_batch_entry_t*)in->Entries, in->Count,
                                        in->IoctlCode ? in->IoctlCode : io_control_code, &applied,
                                        &failed, &first_failure);

            ioctl_rsc_apply_batch_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->RequestedCount = in->Count;
                out->AppliedCount = applied;
                out->FailedCount = failed;
                out->FirstFailureStatus = first_failure;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_GET_CATALOG: {
            ioctl_rsc_catalog_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_catalog_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_catalog_entry_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_CATALOG_INFO_CAPACITY)
                cap = IOCTL_RSC_CATALOG_INFO_CAPACITY;

            rscsw_catalog_entry_t cat[IOCTL_RSC_CATALOG_INFO_CAPACITY];
            RtlZeroMemory(cat, sizeof(cat));
            ULONG count = 0;
            status = rsc_sw_get_catalog(cat, cap, &count);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->EntrySize = entry_size;
            out->Reserved = 0;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].SubsystemId = cat[i].subsystem_id;
                RtlCopyMemory(out->Entries[i].Name, cat[i].name, sizeof(out->Entries[i].Name));
                out->Entries[i].CapabilityMask = cat[i].cap_mask;
                out->Entries[i].DefaultState = cat[i].default_state;
                out->Entries[i].DefaultClockKhz = cat[i].default_clock_khz;
                out->Entries[i].DefaultFlags = cat[i].default_flags;
                out->Entries[i].ResourceTag = cat[i].res_tag;
                out->Entries[i].SuggestedIoctl = cat[i].suggested_ioctl;
                out->Entries[i].Reserved0 = cat[i].reserved0;
                out->Entries[i].Reserved1 = cat[i].reserved1;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_RUN_SCENARIO: {
            ioctl_rsc_run_scenario_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_scenario_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_scenario(in->ScenarioId, in->Iterations, in->Flags, &result);

            ioctl_rsc_run_scenario_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ScenarioId = result.scenario_id;
                out->Result.Iterations = result.iterations;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.AppliedNodes = result.applied_nodes;
                out->Result.FailedNodes = result.failed_nodes;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_SCENARIO_HISTORY: {
            ioctl_rsc_scenario_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_scenario_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_scenario_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_SCENARIO_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_SCENARIO_HISTORY_INFO_CAPACITY;

            rscsw_scenario_result_t hist[IOCTL_RSC_SCENARIO_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_scenario_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ScenarioId = hist[i].scenario_id;
                out->Entries[i].Iterations = hist[i].iterations;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].AppliedNodes = hist[i].applied_nodes;
                out->Entries[i].FailedNodes = hist[i].failed_nodes;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_DIAG_SNAPSHOT: {
            ioctl_rsc_diag_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_diag_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_diagnostics(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->EntryCount = snap.entry_cnt;
            out->MatrixRunCount = snap.matrix_run_cnt;
            out->MatrixFailureCount = snap.matrix_failure_cnt;
            out->LastMatrixStatus = snap.last_matrix_status;
            out->LastMatrixTick100ns = snap.last_matrix_tick100ns;
            out->LastMatrixFlags = snap.last_matrix_flags;
            out->LastMatrixIterations = snap.last_matrix_iterations;
            out->LastMatrixFailures = snap.last_matrix_failures;
            out->Reserved = snap.reserved;

            ULONG take = snap.entry_cnt;
            if (take > IOCTL_RSC_DIAG_INFO_CAPACITY)
                take = IOCTL_RSC_DIAG_INFO_CAPACITY;
            for (ULONG i = 0; i < take; i++) {
                out->Entries[i].SubsystemId = snap.entries[i].subsystem_id;
                RtlCopyMemory(out->Entries[i].Name, snap.entries[i].name,
                              sizeof(out->Entries[i].Name));
                out->Entries[i].DriverMapId = snap.entries[i].driver_map_id;
                out->Entries[i].PppResourceType = snap.entries[i].ppp_res_type;
                out->Entries[i].GlinkEventSeed = snap.entries[i].glink_event_seed;
                out->Entries[i].IcbErrorCode = snap.entries[i].icb_error_code;
                out->Entries[i].HealthScore = snap.entries[i].health_score;
                out->Entries[i].ConfidenceScore = snap.entries[i].confidence_score;
                out->Entries[i].ActivityScore = snap.entries[i].activity_score;
                out->Entries[i].CapabilityMask = snap.entries[i].cap_mask;
                out->Entries[i].LastScenarioStatus = snap.entries[i].last_scenario_status;
                out->Entries[i].LastNodeStatus = snap.entries[i].last_node_status;
                out->Entries[i].Reserved0 = snap.entries[i].reserved0;
                out->Entries[i].Reserved1 = snap.entries[i].reserved1;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_DIAG_RUN_MATRIX: {
            ioctl_rsc_diag_run_matrix_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_diag_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_diagnostic_matrix(in->Iterations, in->Flags, &result);

            ioctl_rsc_diag_run_matrix_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Iterations = result.iterations;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.ScenarioRuns = result.scenario_runs;
                out->Result.ScenarioFailures = result.scenario_failures;
                out->Result.LastScenarioId = result.last_scenario_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_DIAG_HISTORY: {
            ioctl_rsc_diag_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_diag_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_diag_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_DIAG_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_DIAG_HISTORY_INFO_CAPACITY;

            rscsw_diag_result_t hist[IOCTL_RSC_DIAG_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_diagnostic_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Iterations = hist[i].iterations;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].ScenarioRuns = hist[i].scenario_runs;
                out->Entries[i].ScenarioFailures = hist[i].scenario_failures;
                out->Entries[i].LastScenarioId = hist[i].last_scenario_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_PROFILE_CATALOG: {
            ioctl_rsc_profile_catalog_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_profile_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_profile_catalog(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->ProfileCount = snap.profile_cnt;
            out->ProfileRunCount = snap.profile_run_cnt;
            out->ProfileFailureCount = snap.profile_failure_cnt;
            out->LastProfileId = snap.last_profile_id;
            out->LastProfileStatus = snap.last_profile_status;
            out->LastProfileIterations = snap.last_profile_iterations;
            out->LastProfileFlags = snap.last_profile_flags;
            out->LastProfileTick100ns = snap.last_profile_tick100ns;

            ULONG take = snap.profile_cnt;
            if (take > IOCTL_RSC_PROFILE_INFO_CAPACITY)
                take = IOCTL_RSC_PROFILE_INFO_CAPACITY;
            for (ULONG i = 0; i < take; i++) {
                out->Profiles[i].ProfileId = snap.profiles[i].profile_id;
                RtlCopyMemory(out->Profiles[i].Name, snap.profiles[i].name,
                              sizeof(out->Profiles[i].Name));
                out->Profiles[i].SubsystemMask = snap.profiles[i].subsystem_mask;
                out->Profiles[i].ScenarioFlags = snap.profiles[i].scenario_flags;
                out->Profiles[i].MatrixFlags = snap.profiles[i].matrix_flags;
                out->Profiles[i].DefaultIterations = snap.profiles[i].default_iterations;
                out->Profiles[i].Priority = snap.profiles[i].priority;
                out->Profiles[i].Reserved = snap.profiles[i].reserved;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_PROFILE_RUN: {
            ioctl_rsc_profile_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_profile_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_profile(in->ProfileId, in->Iterations, in->Flags, &result);

            ioctl_rsc_profile_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ProfileId = result.profile_id;
                out->Result.Iterations = result.iterations;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.ScenarioRuns = result.scenario_runs;
                out->Result.ScenarioFailures = result.scenario_failures;
                out->Result.MatrixRuns = result.matrix_runs;
                out->Result.MatrixFailures = result.matrix_failures;
                out->Result.LastScenarioId = result.last_scenario_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_PROFILE_HISTORY: {
            ioctl_rsc_profile_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_profile_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_profile_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_PROFILE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_PROFILE_HISTORY_INFO_CAPACITY;

            rscsw_profile_result_t hist[IOCTL_RSC_PROFILE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_profile_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ProfileId = hist[i].profile_id;
                out->Entries[i].Iterations = hist[i].iterations;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].ScenarioRuns = hist[i].scenario_runs;
                out->Entries[i].ScenarioFailures = hist[i].scenario_failures;
                out->Entries[i].MatrixRuns = hist[i].matrix_runs;
                out->Entries[i].MatrixFailures = hist[i].matrix_failures;
                out->Entries[i].LastScenarioId = hist[i].last_scenario_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_GRAPH_SNAPSHOT: {
            ioctl_rsc_graph_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_graph_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_dependency_graph(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->NodeCount = snap.node_cnt;
            out->LinkCount = snap.link_cnt;
            out->ActiveMask = snap.active_mask;
            out->LastSequenceStatus = snap.last_action_sequence_status;
            out->LastSequenceStepCount = snap.last_action_sequence_step_cnt;
            out->LastSequenceFailureCount = snap.last_action_sequence_failure_cnt;
            out->LastSequenceTick100ns = snap.last_action_sequence_tick100ns;

            ULONG take = snap.link_cnt;
            if (take > IOCTL_RSC_GRAPH_LINK_INFO_CAPACITY)
                take = IOCTL_RSC_GRAPH_LINK_INFO_CAPACITY;
            for (ULONG i = 0; i < take; i++) {
                out->Links[i].FromSubsystemId = snap.links[i].from_subsystem_id;
                out->Links[i].ToSubsystemId = snap.links[i].to_subsystem_id;
                out->Links[i].Weight = snap.links[i].weight;
                out->Links[i].LinkType = snap.links[i].link_type;
                out->Links[i].Flags = snap.links[i].flags;
                out->Links[i].Reserved0 = snap.links[i].reserved0;
                out->Links[i].Reserved1 = snap.links[i].reserved1;
                out->Links[i].Reserved2 = snap.links[i].reserved2;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_ACTION_SEQUENCE_EXECUTE: {
            ioctl_rsc_action_sequence_execute_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            if (in->StepCount == 0u ||
                in->StepCount > IOCTL_RSC_ACTION_SEQUENCE_STEP_INFO_CAPACITY) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            size_t req_bytes =
                FIELD_OFFSET(ioctl_rsc_action_sequence_execute_request_t, Steps) +
                ((size_t)in->StepCount * sizeof(ioctl_rsc_action_sequence_step_request_t));
            if (in_sz < req_bytes) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            rscsw_action_sequence_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_execute_action_sequence((const rscsw_action_sequence_step_t*)in->Steps,
                                                    in->StepCount, in->Flags, &result);

            ioctl_rsc_action_sequence_execute_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.RequestedSteps = result.requested_steps;
                out->Result.ExecutedSteps = result.executed_steps;
                out->Result.FailedSteps = result.failed_steps;
                out->Result.Status = result.status;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_ACTION_SEQUENCE_HISTORY: {
            ioctl_rsc_action_sequence_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz =
                FIELD_OFFSET(ioctl_rsc_action_sequence_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_action_sequence_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_ACTION_SEQUENCE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_ACTION_SEQUENCE_HISTORY_INFO_CAPACITY;

            rscsw_action_sequence_result_t hist[IOCTL_RSC_ACTION_SEQUENCE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_action_sequence_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;
            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].RequestedSteps = hist[i].requested_steps;
                out->Entries[i].ExecutedSteps = hist[i].executed_steps;
                out->Entries[i].FailedSteps = hist[i].failed_steps;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_SNAPSHOT: {
            ioctl_rsc_action_sequence_preview_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_action_sequence_preview_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_action_sequence_preview_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastRequestedSteps = snap.last_requested_steps;
            out->LastExpandedSteps = snap.last_expanded_steps;
            out->LastInvalidSteps = snap.last_invalid_steps;
            out->LastDuplicateSteps = snap.last_duplicate_steps;
            out->LastDependencyEdges = snap.last_dependency_edges;
            out->LastReorderedSteps = snap.last_reordered_steps;
            out->LastActiveMaskBefore = snap.last_active_mask_before;
            out->LastActiveMaskAfter = snap.last_active_mask_after;
            out->LastFlags = snap.last_flags;
            out->LastTick100ns = snap.last_tick100ns;
            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_EXECUTE: {
            ioctl_rsc_action_sequence_preview_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            if (in->StepCount == 0u ||
                in->StepCount > IOCTL_RSC_ACTION_SEQUENCE_STEP_INFO_CAPACITY) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            size_t req_bytes =
                FIELD_OFFSET(ioctl_rsc_action_sequence_preview_request_t, Steps) +
                ((size_t)in->StepCount * sizeof(ioctl_rsc_action_sequence_step_request_t));
            if (in_sz < req_bytes) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            rscsw_action_sequence_preview_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_preview_action_sequence((const rscsw_action_sequence_step_t*)in->Steps,
                                                    in->StepCount, in->Flags, &result);

            ioctl_rsc_action_sequence_preview_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.RequestedSteps = result.requested_steps;
                out->Result.Flags = result.flags;
                out->Result.ExpandedSteps = result.expanded_steps;
                out->Result.InvalidSteps = result.invalid_steps;
                out->Result.DuplicateSteps = result.duplicate_steps;
                out->Result.DependencyEdges = result.dependency_edges;
                out->Result.ReorderedSteps = result.reordered_steps;
                out->Result.ActiveMaskBefore = result.active_mask_before;
                out->Result.ActiveMaskAfter = result.active_mask_after;
                out->Result.Status = result.status;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY: {
            ioctl_rsc_action_sequence_preview_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz =
                FIELD_OFFSET(ioctl_rsc_action_sequence_preview_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_action_sequence_preview_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY_INFO_CAPACITY;

            rscsw_action_sequence_preview_result_t
                hist[IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_action_sequence_preview_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].RequestedSteps = hist[i].requested_steps;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].ExpandedSteps = hist[i].expanded_steps;
                out->Entries[i].InvalidSteps = hist[i].invalid_steps;
                out->Entries[i].DuplicateSteps = hist[i].duplicate_steps;
                out->Entries[i].DependencyEdges = hist[i].dependency_edges;
                out->Entries[i].ReorderedSteps = hist[i].reordered_steps;
                out->Entries[i].ActiveMaskBefore = hist[i].active_mask_before;
                out->Entries[i].ActiveMaskAfter = hist[i].active_mask_after;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_CHECKPOINT_SNAPSHOT: {
            ioctl_rsc_checkpoint_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_checkpoint_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_checkpoint_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->SlotCount = snap.slot_cnt;
            out->ValidCount = snap.valid_cnt;
            out->CaptureCount = snap.capture_cnt;
            out->ApplyCount = snap.apply_cnt;
            out->LastOperationStatus = snap.last_operation_status;
            out->LastOperationSlotId = snap.last_operation_slot_id;
            out->LastOperationTick100ns = snap.last_operation_tick100ns;

            ULONG take = snap.slot_cnt;
            if (take > IOCTL_RSC_CHECKPOINT_INFO_CAPACITY)
                take = IOCTL_RSC_CHECKPOINT_INFO_CAPACITY;
            for (ULONG i = 0; i < take; i++) {
                out->Slots[i].SlotId = snap.slots[i].slot_id;
                out->Slots[i].Valid = snap.slots[i].valid;
                RtlCopyMemory(out->Slots[i].Name, snap.slots[i].name, sizeof(out->Slots[i].Name));
                out->Slots[i].CaptureCount = snap.slots[i].capture_cnt;
                out->Slots[i].ApplyCount = snap.slots[i].apply_cnt;
                out->Slots[i].LastStatus = snap.slots[i].last_status;
                out->Slots[i].NodeCount = snap.slots[i].node_cnt;
                out->Slots[i].ActiveMask = snap.slots[i].active_mask;
                out->Slots[i].LastTick100ns = snap.slots[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_CHECKPOINT_CAPTURE: {
            ioctl_rsc_checkpoint_capture_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            CHAR name[RTL_NUMBER_OF(in->Name) + 1];
            RtlZeroMemory(name, sizeof(name));
            RtlCopyMemory(name, in->Name, RTL_NUMBER_OF(in->Name));

            rscsw_checkpoint_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            result.operation = IOCTL_RSC_CHECKPOINT_OP_CAPTURE;
            result.slot_id = in->SlotId;
            result.flags = in->Flags;
            result.status = STATUS_UNSUCCESSFUL;

            status =
                rsc_sw_capture_checkpoint(in->SlotId, name[0] ? name : NULL, in->Flags, &result);

            if (!NT_SUCCESS(status))
                result.status = status;

            ioctl_rsc_checkpoint_capture_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Operation = result.operation;
                out->Result.SlotId = result.slot_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.NodeCount = result.node_cnt;
                out->Result.SuccessCount = result.success_cnt;
                out->Result.FailureCount = result.failure_cnt;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_CHECKPOINT_APPLY: {
            ioctl_rsc_checkpoint_apply_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_checkpoint_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            result.operation = IOCTL_RSC_CHECKPOINT_OP_APPLY;
            result.slot_id = in->SlotId;
            result.flags = in->Flags;
            result.status = STATUS_UNSUCCESSFUL;

            status = rsc_sw_apply_checkpoint(in->SlotId, in->Flags, &result);
            if (!NT_SUCCESS(status))
                result.status = status;

            ioctl_rsc_checkpoint_apply_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Operation = result.operation;
                out->Result.SlotId = result.slot_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.NodeCount = result.node_cnt;
                out->Result.SuccessCount = result.success_cnt;
                out->Result.FailureCount = result.failure_cnt;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_CHECKPOINT_HISTORY: {
            ioctl_rsc_checkpoint_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_checkpoint_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_checkpoint_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_CHECKPOINT_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_CHECKPOINT_HISTORY_INFO_CAPACITY;

            rscsw_checkpoint_result_t hist[IOCTL_RSC_CHECKPOINT_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_checkpoint_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Operation = hist[i].operation;
                out->Entries[i].SlotId = hist[i].slot_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].NodeCount = hist[i].node_cnt;
                out->Entries[i].SuccessCount = hist[i].success_cnt;
                out->Entries[i].FailureCount = hist[i].failure_cnt;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_COVERAGE_SNAPSHOT: {
            ioctl_rsc_coverage_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_coverage_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_coverage_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->NodeCount = snap.node_cnt;
            out->ActiveMask = snap.active_mask;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastHealthScore = snap.last_health_score;
            out->LastActivityScore = snap.last_activity_score;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastThresholdPermille = snap.last_threshold_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.node_cnt;
            if (row_cnt > IOCTL_RSC_COVERAGE_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_COVERAGE_INFO_CAPACITY;
            out->RowCount = row_cnt;
            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].Present = snap.rows[i].present;
                out->Rows[i].Active = snap.rows[i].active;
                out->Rows[i].RequestCount = snap.rows[i].req_cnt;
                out->Rows[i].SuccessCount = snap.rows[i].success_cnt;
                out->Rows[i].FailureCount = snap.rows[i].failure_cnt;
                out->Rows[i].SuccessPermille = snap.rows[i].success_permille;
                out->Rows[i].DependencyIn = snap.rows[i].dependency_in;
                out->Rows[i].DependencyOut = snap.rows[i].dependency_out;
                out->Rows[i].HealthScore = snap.rows[i].health_score;
                out->Rows[i].ActivityScore = snap.rows[i].activity_score;
                out->Rows[i].Flags = snap.rows[i].flags;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_COVERAGE_ANALYZE: {
            ioctl_rsc_coverage_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_coverage_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_analyze_coverage(in->Flags, &result);

            ioctl_rsc_coverage_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.NodeCount = result.node_cnt;
                out->Result.Status = result.status;
                out->Result.HealthScore = result.health_score;
                out->Result.ActivityScore = result.activity_score;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.BelowThresholdCount = result.below_threshold_cnt;
                out->Result.ThresholdPermille = result.threshold_permille;
                out->Result.ActiveMaskBefore = result.active_mask_before;
                out->Result.ActiveMaskAfter = result.active_mask_after;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_COVERAGE_HISTORY: {
            ioctl_rsc_coverage_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_coverage_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_coverage_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_COVERAGE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_COVERAGE_HISTORY_INFO_CAPACITY;

            rscsw_coverage_result_t hist[IOCTL_RSC_COVERAGE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_coverage_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].NodeCount = hist[i].node_cnt;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].HealthScore = hist[i].health_score;
                out->Entries[i].ActivityScore = hist[i].activity_score;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].BelowThresholdCount = hist[i].below_threshold_cnt;
                out->Entries[i].ThresholdPermille = hist[i].threshold_permille;
                out->Entries[i].ActiveMaskBefore = hist[i].active_mask_before;
                out->Entries[i].ActiveMaskAfter = hist[i].active_mask_after;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_REMEDIATION_SNAPSHOT: {
            ioctl_rsc_remediation_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_remediation_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_remediation_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastRecommendedCount = snap.last_recommended_cnt;
            out->LastExecutedCount = snap.last_executed_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastCoverageThresholdPermille = snap.last_coverage_threshold_permille;
            out->LastCoverageHealthScore = snap.last_coverage_health_score;
            out->LastCoverageActivityScore = snap.last_coverage_activity_score;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastWorstSubsystemId = snap.last_worst_subsystem_id;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG action_cnt = snap.action_cnt;
            if (action_cnt > IOCTL_RSC_REMEDIATION_INFO_CAPACITY)
                action_cnt = IOCTL_RSC_REMEDIATION_INFO_CAPACITY;
            out->ActionCount = action_cnt;
            for (ULONG i = 0; i < action_cnt; i++) {
                out->Actions[i].SubsystemId = snap.actions[i].subsystem_id;
                RtlCopyMemory(out->Actions[i].Name, snap.actions[i].name,
                              sizeof(out->Actions[i].Name));
                out->Actions[i].Priority = snap.actions[i].priority;
                out->Actions[i].ReasonFlags = snap.actions[i].reason_flags;
                out->Actions[i].CurrentState = snap.actions[i].current_state;
                out->Actions[i].RecommendedState = snap.actions[i].recommended_state;
                out->Actions[i].CurrentClockKhz = snap.actions[i].current_clock_khz;
                out->Actions[i].RecommendedClockKhz = snap.actions[i].recommended_clock_khz;
                out->Actions[i].CurrentFlags = snap.actions[i].current_flags;
                out->Actions[i].RecommendedFlags = snap.actions[i].recommended_flags;
                out->Actions[i].HealthScore = snap.actions[i].health_score;
                out->Actions[i].ActivityScore = snap.actions[i].activity_score;
                out->Actions[i].SuccessPermille = snap.actions[i].success_permille;
                out->Actions[i].DependencyIn = snap.actions[i].dependency_in;
                out->Actions[i].DependencyOut = snap.actions[i].dependency_out;
                out->Actions[i].LastStatus = snap.actions[i].last_status;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_REMEDIATION_RUN: {
            ioctl_rsc_remediation_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_remediation_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_remediation(in->Flags, &result);

            ioctl_rsc_remediation_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.RecommendedCount = result.recommended_cnt;
                out->Result.ExecutedCount = result.executed_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.CoverageThresholdPermille = result.coverage_threshold_permille;
                out->Result.CoverageHealthScore = result.coverage_health_score;
                out->Result.CoverageActivityScore = result.coverage_activity_score;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.WorstSubsystemId = result.worst_subsystem_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_REMEDIATION_HISTORY: {
            ioctl_rsc_remediation_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_remediation_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_remediation_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_REMEDIATION_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_REMEDIATION_HISTORY_INFO_CAPACITY;

            rscsw_remediation_result_t hist[IOCTL_RSC_REMEDIATION_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_remediation_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].RecommendedCount = hist[i].recommended_cnt;
                out->Entries[i].ExecutedCount = hist[i].executed_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].CoverageThresholdPermille = hist[i].coverage_threshold_permille;
                out->Entries[i].CoverageHealthScore = hist[i].coverage_health_score;
                out->Entries[i].CoverageActivityScore = hist[i].coverage_activity_score;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].WorstSubsystemId = hist[i].worst_subsystem_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_STABILIZATION_SNAPSHOT: {
            ioctl_rsc_stabilization_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_stabilization_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_stabilization_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastLaneCount = snap.last_lane_cnt;
            out->LastDegradedLaneCount = snap.last_degraded_lane_cnt;
            out->LastRemediationStatus = snap.last_remediation_status;
            out->LastCoverageHealthScore = snap.last_coverage_health_score;
            out->LastCoverageActivityScore = snap.last_coverage_activity_score;
            out->LastCoverageWarningCount = snap.last_coverage_warning_cnt;
            out->LastCoverageErrorCount = snap.last_coverage_error_cnt;
            out->LastProfileRuns = snap.last_profile_runs;
            out->LastProfileFailures = snap.last_profile_failures;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG lane_cnt = snap.lane_cnt;
            if (lane_cnt > IOCTL_RSC_STABILIZATION_LANE_INFO_CAPACITY)
                lane_cnt = IOCTL_RSC_STABILIZATION_LANE_INFO_CAPACITY;
            out->LaneCount = lane_cnt;
            for (ULONG i = 0; i < lane_cnt; i++) {
                out->Lanes[i].LaneId = snap.lanes[i].lane_id;
                RtlCopyMemory(out->Lanes[i].Name, snap.lanes[i].name, sizeof(out->Lanes[i].Name));
                out->Lanes[i].SubsystemMask = snap.lanes[i].subsystem_mask;
                out->Lanes[i].NodeCount = snap.lanes[i].node_cnt;
                out->Lanes[i].DegradedCount = snap.lanes[i].degraded_cnt;
                out->Lanes[i].HealthScore = snap.lanes[i].health_score;
                out->Lanes[i].ActivityScore = snap.lanes[i].activity_score;
                out->Lanes[i].WarningCount = snap.lanes[i].warning_cnt;
                out->Lanes[i].ErrorCount = snap.lanes[i].error_cnt;
                out->Lanes[i].LastStatus = snap.lanes[i].last_status;
                out->Lanes[i].LastTick100ns = snap.lanes[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_STABILIZATION_RUN: {
            ioctl_rsc_stabilization_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_stabilization_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_stabilization(in->Flags, &result);

            ioctl_rsc_stabilization_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.LaneCount = result.lane_cnt;
                out->Result.DegradedLaneCount = result.degraded_lane_cnt;
                out->Result.RemediationStatus = result.remediation_status;
                out->Result.CoverageHealthScore = result.coverage_health_score;
                out->Result.CoverageActivityScore = result.coverage_activity_score;
                out->Result.CoverageWarningCount = result.coverage_warning_cnt;
                out->Result.CoverageErrorCount = result.coverage_error_cnt;
                out->Result.ProfileRuns = result.profile_runs;
                out->Result.ProfileFailures = result.profile_failures;
                out->Result.LastLaneId = result.last_lane_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_STABILIZATION_HISTORY: {
            ioctl_rsc_stabilization_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_stabilization_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_stabilization_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_STABILIZATION_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_STABILIZATION_HISTORY_INFO_CAPACITY;

            rscsw_stabilization_result_t hist[IOCTL_RSC_STABILIZATION_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_stabilization_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].LaneCount = hist[i].lane_cnt;
                out->Entries[i].DegradedLaneCount = hist[i].degraded_lane_cnt;
                out->Entries[i].RemediationStatus = hist[i].remediation_status;
                out->Entries[i].CoverageHealthScore = hist[i].coverage_health_score;
                out->Entries[i].CoverageActivityScore = hist[i].coverage_activity_score;
                out->Entries[i].CoverageWarningCount = hist[i].coverage_warning_cnt;
                out->Entries[i].CoverageErrorCount = hist[i].coverage_error_cnt;
                out->Entries[i].ProfileRuns = hist[i].profile_runs;
                out->Entries[i].ProfileFailures = hist[i].profile_failures;
                out->Entries[i].LastLaneId = hist[i].last_lane_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_CONFORMANCE_SNAPSHOT: {
            ioctl_rsc_conformance_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_conformance_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_conformance_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastThresholdPermille = snap.last_threshold_permille;
            out->LastPassingCount = snap.last_passing_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastWorstSubsystemId = snap.last_worst_subsystem_id;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_CONFORMANCE_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_CONFORMANCE_INFO_CAPACITY;
            out->RowCount = row_cnt;
            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].GlinkEventSeed = snap.rows[i].glink_event_seed;
                out->Rows[i].CapabilityMask = snap.rows[i].cap_mask;
                out->Rows[i].ResourceTag = snap.rows[i].res_tag;
                out->Rows[i].SuggestedIoctl = snap.rows[i].suggested_ioctl;
                out->Rows[i].DependencyIn = snap.rows[i].dependency_in;
                out->Rows[i].DependencyOut = snap.rows[i].dependency_out;
                out->Rows[i].Active = snap.rows[i].active;
                out->Rows[i].SuccessPermille = snap.rows[i].success_permille;
                out->Rows[i].ActivityScore = snap.rows[i].activity_score;
                out->Rows[i].ConformanceScorePermille = snap.rows[i].conformance_score_permille;
                out->Rows[i].ReasonFlags = snap.rows[i].reason_flags;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_CONFORMANCE_RUN: {
            ioctl_rsc_conformance_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_conformance_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_conformance(in->Flags, &result);

            ioctl_rsc_conformance_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.NodeCount = result.node_cnt;
                out->Result.PassingCount = result.passing_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.ThresholdPermille = result.threshold_permille;
                out->Result.WorstSubsystemId = result.worst_subsystem_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_CONFORMANCE_HISTORY: {
            ioctl_rsc_conformance_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_conformance_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_conformance_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_CONFORMANCE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_CONFORMANCE_HISTORY_INFO_CAPACITY;

            rscsw_conformance_result_t hist[IOCTL_RSC_CONFORMANCE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_conformance_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].NodeCount = hist[i].node_cnt;
                out->Entries[i].PassingCount = hist[i].passing_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].ThresholdPermille = hist[i].threshold_permille;
                out->Entries[i].WorstSubsystemId = hist[i].worst_subsystem_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_ORCHESTRATION_SNAPSHOT: {
            ioctl_rsc_orchestration_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_orchestration_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_orchestration_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastStageCount = snap.last_stage_cnt;
            out->LastSuccessfulStages = snap.last_successful_stages;
            out->LastWarningStages = snap.last_warning_stages;
            out->LastFailedStages = snap.last_failed_stages;
            out->LastStageId = snap.last_stage_id;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG stage_cnt = snap.stage_cnt;
            if (stage_cnt > IOCTL_RSC_ORCHESTRATION_STAGE_INFO_CAPACITY)
                stage_cnt = IOCTL_RSC_ORCHESTRATION_STAGE_INFO_CAPACITY;
            out->StageCount = stage_cnt;
            for (ULONG i = 0; i < stage_cnt; i++) {
                out->Stages[i].StageId = snap.stages[i].stage_id;
                RtlCopyMemory(out->Stages[i].StageName, snap.stages[i].stage_name,
                              sizeof(out->Stages[i].StageName));
                out->Stages[i].Enabled = snap.stages[i].enabled;
                out->Stages[i].Status = snap.stages[i].status;
                out->Stages[i].WarningCount = snap.stages[i].warning_cnt;
                out->Stages[i].ErrorCount = snap.stages[i].error_cnt;
                out->Stages[i].ScorePermille = snap.stages[i].score_permille;
                out->Stages[i].Detail0 = snap.stages[i].detail0;
                out->Stages[i].Detail1 = snap.stages[i].detail1;
                out->Stages[i].Detail2 = snap.stages[i].detail2;
                out->Stages[i].Detail3 = snap.stages[i].detail3;
                out->Stages[i].Tick100ns = snap.stages[i].tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_ORCHESTRATION_RUN: {
            ioctl_rsc_orchestration_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_orchestration_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_orchestration(in->Flags, &result);

            ioctl_rsc_orchestration_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.StageCount = result.stage_cnt;
                out->Result.SuccessfulStages = result.successful_stages;
                out->Result.WarningStages = result.warning_stages;
                out->Result.FailedStages = result.failed_stages;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.LastStageId = result.last_stage_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_ORCHESTRATION_HISTORY: {
            ioctl_rsc_orchestration_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_orchestration_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_orchestration_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_ORCHESTRATION_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_ORCHESTRATION_HISTORY_INFO_CAPACITY;

            rscsw_orchestration_result_t hist[IOCTL_RSC_ORCHESTRATION_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_orchestration_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].StageCount = hist[i].stage_cnt;
                out->Entries[i].SuccessfulStages = hist[i].successful_stages;
                out->Entries[i].WarningStages = hist[i].warning_stages;
                out->Entries[i].FailedStages = hist[i].failed_stages;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].LastStageId = hist[i].last_stage_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_POLICY_SNAPSHOT: {
            ioctl_rsc_policy_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_policy_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_policy_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->ResourceCount = snap.res_cnt;
            out->ActiveResourceCount = snap.active_res_cnt;
            out->EventSequence = snap.event_sequence;
            out->VoteCount = snap.vote_cnt;
            out->RemoveCount = snap.remove_cnt;
            out->PropagationCount = snap.propagation_cnt;
            out->LastResourceId = snap.last_res_id;
            out->LastSubsystemId = snap.last_subsystem_id;
            out->LastParentSubsystemId = snap.last_parent_subsystem_id;
            out->LastIoctlCode = snap.last_ioctl_code;
            out->LastStatus = snap.last_status;
            out->LastFlags = snap.last_flags;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG res_cnt = snap.res_entry_cnt;
            if (res_cnt > IOCTL_RSC_POLICY_RESOURCE_INFO_CAPACITY)
                res_cnt = IOCTL_RSC_POLICY_RESOURCE_INFO_CAPACITY;
            out->ResourceEntryCount = res_cnt;
            for (ULONG i = 0; i < res_cnt; i++) {
                out->Resources[i].ResourceId = snap.resources[i].res_id;
                out->Resources[i].ParentResourceId = snap.resources[i].parent_res_id;
                out->Resources[i].SubsystemId = snap.resources[i].subsystem_id;
                out->Resources[i].ParentSubsystemId = snap.resources[i].parent_subsystem_id;
                out->Resources[i].ResourceType = snap.resources[i].res_type;
                out->Resources[i].ActiveVoteCount = snap.resources[i].active_vote_cnt;
                out->Resources[i].SetMask = snap.resources[i].set_mask;
                out->Resources[i].DriverMapId = snap.resources[i].driver_map_id;
                out->Resources[i].Flags = snap.resources[i].flags;
                out->Resources[i].LastStatus = snap.resources[i].last_status;
                out->Resources[i].LastTag = snap.resources[i].last_tag;
                out->Resources[i].LastValue0 = snap.resources[i].last_value0;
                out->Resources[i].LastValue1 = snap.resources[i].last_value1;
                out->Resources[i].LastValue2 = snap.resources[i].last_value2;
                out->Resources[i].LastValue3 = snap.resources[i].last_value3;
                out->Resources[i].LastTick100ns = snap.resources[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_POLICY_HISTORY: {
            ioctl_rsc_policy_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_policy_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_policy_event_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_POLICY_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_POLICY_HISTORY_INFO_CAPACITY;

            rscsw_policy_event_t hist[IOCTL_RSC_POLICY_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_policy_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Sequence = hist[i].sequence;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].ResourceId = hist[i].res_id;
                out->Entries[i].ParentResourceId = hist[i].parent_res_id;
                out->Entries[i].SubsystemId = hist[i].subsystem_id;
                out->Entries[i].ParentSubsystemId = hist[i].parent_subsystem_id;
                out->Entries[i].ResourceType = hist[i].res_type;
                out->Entries[i].ActiveVoteCount = hist[i].active_vote_cnt;
                out->Entries[i].SetMask = hist[i].set_mask;
                out->Entries[i].DriverMapId = hist[i].driver_map_id;
                out->Entries[i].Tag = hist[i].tag;
                out->Entries[i].Value0 = hist[i].value0;
                out->Entries[i].Value1 = hist[i].value1;
                out->Entries[i].Value2 = hist[i].value2;
                out->Entries[i].Value3 = hist[i].value3;
                out->Entries[i].IoctlCode = hist[i].ioctl_code;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_FABRIC_SNAPSHOT: {
            ioctl_rsc_fabric_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_fabric_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_fabric_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastLaneCount = snap.last_lane_cnt;
            out->LastActiveLaneCount = snap.last_active_lane_cnt;
            out->LastPolicyActiveLaneCount = snap.last_policy_active_lane_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastOverallHealthScore = snap.last_overall_health_score;
            out->LastOverallActivityScore = snap.last_overall_activity_score;
            out->LastThermalMaxDeciC = snap.last_thermal_max_deci_c;
            out->LastThermalMinDeciC = snap.last_thermal_min_deci_c;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG lane_cnt = snap.lane_cnt;
            if (lane_cnt > IOCTL_RSC_FABRIC_LANE_INFO_CAPACITY)
                lane_cnt = IOCTL_RSC_FABRIC_LANE_INFO_CAPACITY;
            out->LaneCount = lane_cnt;

            for (ULONG i = 0; i < lane_cnt; i++) {
                out->Lanes[i].SubsystemId = snap.lanes[i].subsystem_id;
                RtlCopyMemory(out->Lanes[i].Name, snap.lanes[i].name, sizeof(out->Lanes[i].Name));
                out->Lanes[i].Present = snap.lanes[i].present;
                out->Lanes[i].Active = snap.lanes[i].active;
                out->Lanes[i].State = snap.lanes[i].state;
                out->Lanes[i].ClockKhz = snap.lanes[i].clock_khz;
                out->Lanes[i].DriverMapId = snap.lanes[i].driver_map_id;
                out->Lanes[i].PppResourceType = snap.lanes[i].ppp_res_type;
                out->Lanes[i].PolicyResourceCount = snap.lanes[i].policy_res_cnt;
                out->Lanes[i].PolicyActiveVoteCount = snap.lanes[i].policy_active_vote_cnt;
                out->Lanes[i].DependencyIn = snap.lanes[i].dependency_in;
                out->Lanes[i].DependencyOut = snap.lanes[i].dependency_out;
                out->Lanes[i].RequestCount = snap.lanes[i].req_cnt;
                out->Lanes[i].SuccessCount = snap.lanes[i].success_cnt;
                out->Lanes[i].FailureCount = snap.lanes[i].failure_cnt;
                out->Lanes[i].SuccessPermille = snap.lanes[i].success_permille;
                out->Lanes[i].ActivityScore = snap.lanes[i].activity_score;
                out->Lanes[i].HealthScore = snap.lanes[i].health_score;
                out->Lanes[i].ThermalDeciC = snap.lanes[i].thermal_deci_c;
                out->Lanes[i].Flags = snap.lanes[i].flags;
                out->Lanes[i].LastStatus = snap.lanes[i].last_status;
                out->Lanes[i].LastTick100ns = snap.lanes[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_FABRIC_RUN: {
            ioctl_rsc_fabric_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_fabric_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_fabric(in->Flags, &result);

            ioctl_rsc_fabric_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.LaneCount = result.lane_cnt;
                out->Result.ActiveLaneCount = result.active_lane_cnt;
                out->Result.PolicyActiveLaneCount = result.policy_active_lane_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.OverallHealthScore = result.overall_health_score;
                out->Result.OverallActivityScore = result.overall_activity_score;
                out->Result.ThermalMaxDeciC = result.thermal_max_deci_c;
                out->Result.ThermalMinDeciC = result.thermal_min_deci_c;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_FABRIC_HISTORY: {
            ioctl_rsc_fabric_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_fabric_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_fabric_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_FABRIC_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_FABRIC_HISTORY_INFO_CAPACITY;

            rscsw_fabric_result_t hist[IOCTL_RSC_FABRIC_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_fabric_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].LaneCount = hist[i].lane_cnt;
                out->Entries[i].ActiveLaneCount = hist[i].active_lane_cnt;
                out->Entries[i].PolicyActiveLaneCount = hist[i].policy_active_lane_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].OverallHealthScore = hist[i].overall_health_score;
                out->Entries[i].OverallActivityScore = hist[i].overall_activity_score;
                out->Entries[i].ThermalMaxDeciC = hist[i].thermal_max_deci_c;
                out->Entries[i].ThermalMinDeciC = hist[i].thermal_min_deci_c;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_DOMAIN_SNAPSHOT: {
            ioctl_rsc_domain_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_domain_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_domain_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastDomainCount = snap.last_domain_cnt;
            out->LastHealthyDomainCount = snap.last_healthy_domain_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastOverallHealthScore = snap.last_overall_health_score;
            out->LastOverallActivityScore = snap.last_overall_activity_score;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG domain_cnt = snap.domain_cnt;
            if (domain_cnt > IOCTL_RSC_DOMAIN_INFO_CAPACITY)
                domain_cnt = IOCTL_RSC_DOMAIN_INFO_CAPACITY;
            out->DomainCount = domain_cnt;

            for (ULONG i = 0; i < domain_cnt; i++) {
                out->Domains[i].DomainId = snap.domains[i].domain_id;
                RtlCopyMemory(out->Domains[i].Name, snap.domains[i].name,
                              sizeof(out->Domains[i].Name));
                out->Domains[i].SubsystemMask = snap.domains[i].subsystem_mask;
                out->Domains[i].LaneCount = snap.domains[i].lane_cnt;
                out->Domains[i].ActiveLaneCount = snap.domains[i].active_lane_cnt;
                out->Domains[i].PolicyActiveLaneCount = snap.domains[i].policy_active_lane_cnt;
                out->Domains[i].WarningCount = snap.domains[i].warning_cnt;
                out->Domains[i].ErrorCount = snap.domains[i].error_cnt;
                out->Domains[i].AvgHealthScore = snap.domains[i].avg_health_score;
                out->Domains[i].AvgActivityScore = snap.domains[i].avg_activity_score;
                out->Domains[i].ThermalMaxDeciC = snap.domains[i].thermal_max_deci_c;
                out->Domains[i].ThermalMinDeciC = snap.domains[i].thermal_min_deci_c;
                out->Domains[i].LastSubsystemId = snap.domains[i].last_subsystem_id;
                out->Domains[i].LastStatus = snap.domains[i].last_status;
                out->Domains[i].Flags = snap.domains[i].flags;
                out->Domains[i].LastTick100ns = snap.domains[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_DOMAIN_RUN: {
            ioctl_rsc_domain_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_domain_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_domain(in->Flags, &result);

            ioctl_rsc_domain_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.DomainCount = result.domain_cnt;
                out->Result.HealthyDomainCount = result.healthy_domain_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.OverallHealthScore = result.overall_health_score;
                out->Result.OverallActivityScore = result.overall_activity_score;
                out->Result.LastDomainId = result.last_domain_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_DOMAIN_HISTORY: {
            ioctl_rsc_domain_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_domain_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_domain_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_DOMAIN_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_DOMAIN_HISTORY_INFO_CAPACITY;

            rscsw_domain_result_t hist[IOCTL_RSC_DOMAIN_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_domain_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].DomainCount = hist[i].domain_cnt;
                out->Entries[i].HealthyDomainCount = hist[i].healthy_domain_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].OverallHealthScore = hist[i].overall_health_score;
                out->Entries[i].OverallActivityScore = hist[i].overall_activity_score;
                out->Entries[i].LastDomainId = hist[i].last_domain_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_INTERCONNECT_SNAPSHOT: {
            ioctl_rsc_interconnect_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_interconnect_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_interconnect_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->LastFlags = snap.last_flags;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastRouteCount = snap.last_route_cnt;
            out->LastActiveRouteCount = snap.last_active_route_cnt;
            out->LastPolicyLinkedRouteCount = snap.last_policy_linked_route_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastAvgUtilizationPermille = snap.last_avg_utilization_permille;
            out->LastPeakUtilizationPermille = snap.last_peak_utilization_permille;
            out->LastTotalRequiredBandwidthMb = snap.last_total_required_bandwidth_mb;
            out->LastTotalGrantedBandwidthMb = snap.last_total_granted_bandwidth_mb;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG route_cnt = snap.route_cnt;
            if (route_cnt > IOCTL_RSC_INTERCONNECT_ROUTE_INFO_CAPACITY)
                route_cnt = IOCTL_RSC_INTERCONNECT_ROUTE_INFO_CAPACITY;
            out->RouteCount = route_cnt;

            for (ULONG i = 0; i < route_cnt; i++) {
                out->Routes[i].RouteId = snap.routes[i].route_id;
                RtlCopyMemory(out->Routes[i].Name, snap.routes[i].name,
                              sizeof(out->Routes[i].Name));
                out->Routes[i].SubsystemId = snap.routes[i].subsystem_id;
                RtlCopyMemory(out->Routes[i].SubsystemName, snap.routes[i].subsystem_name,
                              sizeof(out->Routes[i].SubsystemName));
                out->Routes[i].MasterId = snap.routes[i].master_id;
                RtlCopyMemory(out->Routes[i].MasterName, snap.routes[i].master_name,
                              sizeof(out->Routes[i].MasterName));
                out->Routes[i].SlaveId = snap.routes[i].slave_id;
                RtlCopyMemory(out->Routes[i].SlaveName, snap.routes[i].slave_name,
                              sizeof(out->Routes[i].SlaveName));
                out->Routes[i].Active = snap.routes[i].active;
                out->Routes[i].PolicyLinked = snap.routes[i].policy_linked;
                out->Routes[i].RequiredBandwidthMb = snap.routes[i].required_bandwidth_mb;
                out->Routes[i].GrantedBandwidthMb = snap.routes[i].granted_bandwidth_mb;
                out->Routes[i].UtilizationPermille = snap.routes[i].utilization_permille;
                out->Routes[i].VoteCount = snap.routes[i].vote_cnt;
                out->Routes[i].DependencyIn = snap.routes[i].dependency_in;
                out->Routes[i].DependencyOut = snap.routes[i].dependency_out;
                out->Routes[i].ThermalDeciC = snap.routes[i].thermal_deci_c;
                out->Routes[i].Flags = snap.routes[i].flags;
                out->Routes[i].LastStatus = snap.routes[i].last_status;
                out->Routes[i].LastTick100ns = snap.routes[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_INTERCONNECT_RUN: {
            ioctl_rsc_interconnect_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_interconnect_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_interconnect(in->Flags, &result);

            ioctl_rsc_interconnect_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.RouteCount = result.route_cnt;
                out->Result.ActiveRouteCount = result.active_route_cnt;
                out->Result.PolicyLinkedRouteCount = result.policy_linked_route_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.AvgUtilizationPermille = result.avg_utilization_permille;
                out->Result.PeakUtilizationPermille = result.peak_utilization_permille;
                out->Result.TotalRequiredBandwidthMb = result.total_required_bandwidth_mb;
                out->Result.TotalGrantedBandwidthMb = result.total_granted_bandwidth_mb;
                out->Result.LastRouteId = result.last_route_id;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_INTERCONNECT_HISTORY: {
            ioctl_rsc_interconnect_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_interconnect_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_interconnect_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_INTERCONNECT_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_INTERCONNECT_HISTORY_INFO_CAPACITY;

            rscsw_interconnect_result_t hist[IOCTL_RSC_INTERCONNECT_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_interconnect_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].RouteCount = hist[i].route_cnt;
                out->Entries[i].ActiveRouteCount = hist[i].active_route_cnt;
                out->Entries[i].PolicyLinkedRouteCount = hist[i].policy_linked_route_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].AvgUtilizationPermille = hist[i].avg_utilization_permille;
                out->Entries[i].PeakUtilizationPermille = hist[i].peak_utilization_permille;
                out->Entries[i].TotalRequiredBandwidthMb = hist[i].total_required_bandwidth_mb;
                out->Entries[i].TotalGrantedBandwidthMb = hist[i].total_granted_bandwidth_mb;
                out->Entries[i].LastRouteId = hist[i].last_route_id;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_TOPOLOGY_SNAPSHOT: {
            ioctl_rsc_topology_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_topology_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_topology_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->NodeCount = snap.node_cnt;
            out->RouteCount = snap.route_cnt;
            out->DriverMapCount = snap.driver_map_cnt;
            out->PppResourceNameCount = snap.ppp_res_name_cnt;
            out->IcbMasterNameCount = snap.icb_master_name_cnt;
            out->IcbSlaveNameCount = snap.icb_slave_name_cnt;
            out->Reserved = 0u;

            ULONG node_cnt = snap.node_cnt;
            if (node_cnt > IOCTL_RSC_TOPOLOGY_NODE_INFO_CAPACITY)
                node_cnt = IOCTL_RSC_TOPOLOGY_NODE_INFO_CAPACITY;
            out->NodeCount = node_cnt;

            for (ULONG i = 0; i < node_cnt; i++) {
                out->Nodes[i].SubsystemId = snap.nodes[i].subsystem_id;
                RtlCopyMemory(out->Nodes[i].Name, snap.nodes[i].name, sizeof(out->Nodes[i].Name));
                out->Nodes[i].DriverMapId = snap.nodes[i].driver_map_id;
                out->Nodes[i].PppResourceType = snap.nodes[i].ppp_res_type;
                out->Nodes[i].GlinkEventSeed = snap.nodes[i].glink_event_seed;
                out->Nodes[i].IcbErrorCode = snap.nodes[i].icb_error_code;
                out->Nodes[i].RepresentativeMasterId = snap.nodes[i].representative_master_id;
                RtlCopyMemory(out->Nodes[i].RepresentativeMasterName,
                              snap.nodes[i].representative_master_name,
                              sizeof(out->Nodes[i].RepresentativeMasterName));
                out->Nodes[i].RepresentativeSlaveId = snap.nodes[i].representative_slave_id;
                RtlCopyMemory(out->Nodes[i].RepresentativeSlaveName,
                              snap.nodes[i].representative_slave_name,
                              sizeof(out->Nodes[i].RepresentativeSlaveName));
                out->Nodes[i].DomainMask = snap.nodes[i].domain_mask;
                out->Nodes[i].ScenarioId = snap.nodes[i].scenario_id;
                out->Nodes[i].ProfileMask = snap.nodes[i].profile_mask;
                out->Nodes[i].CapabilityMask = snap.nodes[i].cap_mask;
                out->Nodes[i].ResourceTag = snap.nodes[i].res_tag;
                out->Nodes[i].SuggestedIoctl = snap.nodes[i].suggested_ioctl;
            }

            ULONG route_cnt = snap.route_cnt;
            if (route_cnt > IOCTL_RSC_TOPOLOGY_ROUTE_INFO_CAPACITY)
                route_cnt = IOCTL_RSC_TOPOLOGY_ROUTE_INFO_CAPACITY;
            out->RouteCount = route_cnt;

            for (ULONG i = 0; i < route_cnt; i++) {
                out->Routes[i].RouteId = snap.routes[i].route_id;
                RtlCopyMemory(out->Routes[i].Name, snap.routes[i].name,
                              sizeof(out->Routes[i].Name));
                out->Routes[i].SubsystemId = snap.routes[i].subsystem_id;
                RtlCopyMemory(out->Routes[i].SubsystemName, snap.routes[i].subsystem_name,
                              sizeof(out->Routes[i].SubsystemName));
                out->Routes[i].DomainId = snap.routes[i].domain_id;
                out->Routes[i].MasterId = snap.routes[i].master_id;
                RtlCopyMemory(out->Routes[i].MasterName, snap.routes[i].master_name,
                              sizeof(out->Routes[i].MasterName));
                out->Routes[i].SlaveId = snap.routes[i].slave_id;
                RtlCopyMemory(out->Routes[i].SlaveName, snap.routes[i].slave_name,
                              sizeof(out->Routes[i].SlaveName));
                out->Routes[i].BaseBandwidthMb = snap.routes[i].base_bandwidth_mb;
                out->Routes[i].PeakBandwidthMb = snap.routes[i].peak_bandwidth_mb;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_CATALOG: {
            ioctl_rsc_subsystem_catalog_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_catalog_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_catalog_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->SubsystemCount = snap.subsystem_cnt;
            out->AopSubsystemCount = snap.aop_subsystem_cnt;
            out->ArcResourceCount = snap.arc_res_cnt;
            out->CmdDbSlvCount = snap.cmd_db_slv_cnt;
            out->PdcDrvCount = snap.pdc_drv_cnt;
            out->PdcTriggerCount = snap.pdc_trigger_cnt;
            out->RpmhSetCount = snap.rpmh_set_cnt;
            out->NpaClientCount = snap.npa_client_cnt;
            out->GlinkEventCount = snap.glink_event_cnt;
            out->TlmmDriveCount = snap.tlmm_drive_cnt;
            out->TsensThresholdCount = snap.tsens_threshold_cnt;
            out->ClockDriverCount = snap.clock_driver_cnt;
            out->CmdDbQueryCount = snap.cmd_db_query_cnt;

            ULONG subsystem_cnt = snap.subsystem_cnt;
            if (subsystem_cnt > IOCTL_RSC_SUBSYSTEM_INFO_CAPACITY)
                subsystem_cnt = IOCTL_RSC_SUBSYSTEM_INFO_CAPACITY;
            out->SubsystemCount = subsystem_cnt;
            for (ULONG i = 0; i < subsystem_cnt; i++) {
                out->Subsystems[i].SubsystemId = snap.subsystems[i].subsystem_id;
                RtlCopyMemory(out->Subsystems[i].Name, snap.subsystems[i].name,
                              sizeof(out->Subsystems[i].Name));
                out->Subsystems[i].DriverMapId = snap.subsystems[i].driver_map_id;
                out->Subsystems[i].PppResourceType = snap.subsystems[i].ppp_res_type;
                out->Subsystems[i].GlinkEventType = snap.subsystems[i].glink_event_type;
                out->Subsystems[i].ArcResourceType = snap.subsystems[i].arc_res_type;
                out->Subsystems[i].CmdDbSlaveId = snap.subsystems[i].cmd_db_slave_id;
                out->Subsystems[i].CmdDbQueryType = snap.subsystems[i].cmd_db_query_type;
                out->Subsystems[i].PdcDrvType = snap.subsystems[i].pdc_drv_type;
                out->Subsystems[i].PdcTriggerType = snap.subsystems[i].pdc_trigger_type;
                out->Subsystems[i].RpmhSet = snap.subsystems[i].rpmh_set;
                out->Subsystems[i].NpaClientType = snap.subsystems[i].npa_client_type;
                out->Subsystems[i].TlmmDriveType = snap.subsystems[i].tlmm_drive_type;
                out->Subsystems[i].TsensThresholdType = snap.subsystems[i].tsens_threshold_type;
                out->Subsystems[i].ClockDriverType = snap.subsystems[i].clock_driver_type;
                out->Subsystems[i].AopSubsystemType = snap.subsystems[i].aop_subsystem_type;
            }

            ULONG enum_cnt = snap.aop_subsystem_cnt;
            if (enum_cnt > IOCTL_RSC_AOP_SUBSYSTEM_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_AOP_SUBSYSTEM_INFO_CAPACITY;
            out->AopSubsystemCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->AopSubsystems[i].Value = snap.aop_subsystems[i].value;
                RtlCopyMemory(out->AopSubsystems[i].Name, snap.aop_subsystems[i].name,
                              sizeof(out->AopSubsystems[i].Name));
            }

            enum_cnt = snap.arc_res_cnt;
            if (enum_cnt > IOCTL_RSC_ARC_RESOURCE_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_ARC_RESOURCE_INFO_CAPACITY;
            out->ArcResourceCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->ArcResources[i].Value = snap.arc_resources[i].value;
                RtlCopyMemory(out->ArcResources[i].Name, snap.arc_resources[i].name,
                              sizeof(out->ArcResources[i].Name));
            }

            enum_cnt = snap.cmd_db_slv_cnt;
            if (enum_cnt > IOCTL_RSC_CMDDB_SLV_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_CMDDB_SLV_INFO_CAPACITY;
            out->CmdDbSlvCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->CmdDbSlvIds[i].Value = snap.cmd_db_slv_ids[i].value;
                RtlCopyMemory(out->CmdDbSlvIds[i].Name, snap.cmd_db_slv_ids[i].name,
                              sizeof(out->CmdDbSlvIds[i].Name));
            }

            enum_cnt = snap.pdc_drv_cnt;
            if (enum_cnt > IOCTL_RSC_PDC_DRV_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_PDC_DRV_INFO_CAPACITY;
            out->PdcDrvCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->PdcDrvTypes[i].Value = snap.pdc_drv_types[i].value;
                RtlCopyMemory(out->PdcDrvTypes[i].Name, snap.pdc_drv_types[i].name,
                              sizeof(out->PdcDrvTypes[i].Name));
            }

            enum_cnt = snap.pdc_trigger_cnt;
            if (enum_cnt > IOCTL_RSC_PDC_TRIGGER_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_PDC_TRIGGER_INFO_CAPACITY;
            out->PdcTriggerCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->PdcTriggers[i].Value = snap.pdc_triggers[i].value;
                RtlCopyMemory(out->PdcTriggers[i].Name, snap.pdc_triggers[i].name,
                              sizeof(out->PdcTriggers[i].Name));
            }

            enum_cnt = snap.rpmh_set_cnt;
            if (enum_cnt > IOCTL_RSC_RPMH_SET_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_RPMH_SET_INFO_CAPACITY;
            out->RpmhSetCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->RpmhSets[i].Value = snap.rpmh_sets[i].value;
                RtlCopyMemory(out->RpmhSets[i].Name, snap.rpmh_sets[i].name,
                              sizeof(out->RpmhSets[i].Name));
            }

            enum_cnt = snap.npa_client_cnt;
            if (enum_cnt > IOCTL_RSC_NPA_CLIENT_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_NPA_CLIENT_INFO_CAPACITY;
            out->NpaClientCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->NpaClientTypes[i].Value = snap.npa_client_types[i].value;
                RtlCopyMemory(out->NpaClientTypes[i].Name, snap.npa_client_types[i].name,
                              sizeof(out->NpaClientTypes[i].Name));
            }

            enum_cnt = snap.glink_event_cnt;
            if (enum_cnt > IOCTL_RSC_GLINK_EVENT_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_GLINK_EVENT_INFO_CAPACITY;
            out->GlinkEventCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->GlinkEvents[i].Value = snap.glink_events[i].value;
                RtlCopyMemory(out->GlinkEvents[i].Name, snap.glink_events[i].name,
                              sizeof(out->GlinkEvents[i].Name));
            }

            enum_cnt = snap.tlmm_drive_cnt;
            if (enum_cnt > IOCTL_RSC_TLMM_DRIVE_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_TLMM_DRIVE_INFO_CAPACITY;
            out->TlmmDriveCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->TlmmDriveTypes[i].Value = snap.tlmm_drive_types[i].value;
                RtlCopyMemory(out->TlmmDriveTypes[i].Name, snap.tlmm_drive_types[i].name,
                              sizeof(out->TlmmDriveTypes[i].Name));
            }

            enum_cnt = snap.tsens_threshold_cnt;
            if (enum_cnt > IOCTL_RSC_TSENS_THRESHOLD_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_TSENS_THRESHOLD_INFO_CAPACITY;
            out->TsensThresholdCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->TsensThresholds[i].Value = snap.tsens_thresholds[i].value;
                RtlCopyMemory(out->TsensThresholds[i].Name, snap.tsens_thresholds[i].name,
                              sizeof(out->TsensThresholds[i].Name));
            }

            enum_cnt = snap.clock_driver_cnt;
            if (enum_cnt > IOCTL_RSC_CLOCK_DRIVER_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_CLOCK_DRIVER_INFO_CAPACITY;
            out->ClockDriverCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->ClockDriverTypes[i].Value = snap.clock_driver_types[i].value;
                RtlCopyMemory(out->ClockDriverTypes[i].Name, snap.clock_driver_types[i].name,
                              sizeof(out->ClockDriverTypes[i].Name));
            }

            enum_cnt = snap.cmd_db_query_cnt;
            if (enum_cnt > IOCTL_RSC_CMDDB_QUERY_INFO_CAPACITY)
                enum_cnt = IOCTL_RSC_CMDDB_QUERY_INFO_CAPACITY;
            out->CmdDbQueryCount = enum_cnt;
            for (ULONG i = 0; i < enum_cnt; i++) {
                out->CmdDbQueryTypes[i].Value = snap.cmd_db_query_types[i].value;
                RtlCopyMemory(out->CmdDbQueryTypes[i].Name, snap.cmd_db_query_types[i].name,
                              sizeof(out->CmdDbQueryTypes[i].Name));
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SYNC_SNAPSHOT: {
            ioctl_rsc_sync_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_sync_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_sync_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastProfileId = snap.last_profile_id;
            out->LastFlags = snap.last_flags;
            out->LastIterations = snap.last_iterations;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG lane_cnt = snap.lane_cnt;
            if (lane_cnt > IOCTL_RSC_SYNC_LANE_INFO_CAPACITY)
                lane_cnt = IOCTL_RSC_SYNC_LANE_INFO_CAPACITY;
            out->LaneCount = lane_cnt;

            for (ULONG i = 0; i < lane_cnt; i++) {
                out->Lanes[i].SubsystemId = snap.lanes[i].subsystem_id;
                RtlCopyMemory(out->Lanes[i].Name, snap.lanes[i].name, sizeof(out->Lanes[i].Name));
                out->Lanes[i].ProfileId = snap.lanes[i].profile_id;
                out->Lanes[i].DriverMapId = snap.lanes[i].driver_map_id;
                out->Lanes[i].ArcResourceType = snap.lanes[i].arc_res_type;
                out->Lanes[i].CmdDbSlaveId = snap.lanes[i].cmd_db_slave_id;
                out->Lanes[i].RpmhSet = snap.lanes[i].rpmh_set;
                out->Lanes[i].NpaClientType = snap.lanes[i].npa_client_type;
                out->Lanes[i].GlinkEventType = snap.lanes[i].glink_event_type;
                out->Lanes[i].TargetState = snap.lanes[i].target_state;
                out->Lanes[i].TargetClockKhz = snap.lanes[i].target_clock_khz;
                out->Lanes[i].TargetFlags = snap.lanes[i].target_flags;
                out->Lanes[i].ReasonFlags = snap.lanes[i].reason_flags;
                out->Lanes[i].LastStatus = snap.lanes[i].last_status;
                out->Lanes[i].LastTick100ns = snap.lanes[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SYNC_RUN: {
            ioctl_rsc_sync_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_sync_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_sync(in->ProfileId, in->Iterations, in->Flags, &result);

            ioctl_rsc_sync_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ProfileId = result.profile_id;
                out->Result.Iterations = result.iterations;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_SYNC_HISTORY: {
            ioctl_rsc_sync_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_sync_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_sync_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_SYNC_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_SYNC_HISTORY_INFO_CAPACITY;

            rscsw_sync_result_t hist[IOCTL_RSC_SYNC_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_sync_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ProfileId = hist[i].profile_id;
                out->Entries[i].Iterations = hist[i].iterations;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_VALIDATE_SNAPSHOT: {
            ioctl_rsc_validate_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_validate_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_validate_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastCheckedCount = snap.last_checked_cnt;
            out->LastRepairedCount = snap.last_repaired_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_VALIDATE_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_VALIDATE_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].IssueMask = snap.rows[i].issue_mask;
                out->Rows[i].WarningMask = snap.rows[i].warning_mask;
                out->Rows[i].ScorePermille = snap.rows[i].score_permille;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].CmdDbQueryType = snap.rows[i].cmd_db_query_type;
                out->Rows[i].PdcDrvType = snap.rows[i].pdc_drv_type;
                out->Rows[i].PdcTriggerType = snap.rows[i].pdc_trigger_type;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].TlmmDriveType = snap.rows[i].tlmm_drive_type;
                out->Rows[i].TsensThresholdType = snap.rows[i].tsens_threshold_type;
                out->Rows[i].ClockDriverType = snap.rows[i].clock_driver_type;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_VALIDATE_RUN: {
            ioctl_rsc_validate_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_validate_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_validate(in->ModeId, in->Flags, &result);

            ioctl_rsc_validate_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.CheckedCount = result.checked_cnt;
                out->Result.RepairedCount = result.repaired_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastIssueMask = result.last_issue_mask;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_VALIDATE_HISTORY: {
            ioctl_rsc_validate_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_validate_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_validate_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_VALIDATE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_VALIDATE_HISTORY_INFO_CAPACITY;

            rscsw_validate_result_t hist[IOCTL_RSC_VALIDATE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_validate_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].CheckedCount = hist[i].checked_cnt;
                out->Entries[i].RepairedCount = hist[i].repaired_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastIssueMask = hist[i].last_issue_mask;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SEQUENCE_SNAPSHOT: {
            ioctl_rsc_sequence_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_sequence_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_sequence_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPhaseMask = snap.last_phase_mask;
            out->LastStepCount = snap.last_step_cnt;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastPolicyVoteCount = snap.last_policy_vote_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_SEQUENCE_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_SEQUENCE_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].PrimaryPhase = snap.rows[i].primary_phase;
                out->Rows[i].PhaseMask = snap.rows[i].phase_mask;
                out->Rows[i].ActiveState = snap.rows[i].active_state;
                out->Rows[i].SleepState = snap.rows[i].sleep_state;
                out->Rows[i].WakeState = snap.rows[i].wake_state;
                out->Rows[i].ActiveClockKhz = snap.rows[i].active_clock_khz;
                out->Rows[i].SleepClockKhz = snap.rows[i].sleep_clock_khz;
                out->Rows[i].WakeClockKhz = snap.rows[i].wake_clock_khz;
                out->Rows[i].ActiveFlags = snap.rows[i].active_flags;
                out->Rows[i].SleepFlags = snap.rows[i].sleep_flags;
                out->Rows[i].WakeFlags = snap.rows[i].wake_flags;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastPhase = snap.rows[i].last_phase;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SEQUENCE_RUN: {
            ioctl_rsc_sequence_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_sequence_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_sequence(in->ModeId, in->Flags, &result);

            ioctl_rsc_sequence_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.ExecutedPhaseMask = result.executed_phase_mask;
                out->Result.StepCount = result.step_cnt;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.PolicyVoteCount = result.policy_vote_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastPhase = result.last_phase;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_SEQUENCE_HISTORY: {
            ioctl_rsc_sequence_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_sequence_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_sequence_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_SEQUENCE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_SEQUENCE_HISTORY_INFO_CAPACITY;

            rscsw_sequence_result_t hist[IOCTL_RSC_SEQUENCE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_sequence_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].ExecutedPhaseMask = hist[i].executed_phase_mask;
                out->Entries[i].StepCount = hist[i].step_cnt;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].PolicyVoteCount = hist[i].policy_vote_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastPhase = hist[i].last_phase;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_LOOKUP_SNAPSHOT: {
            ioctl_rsc_lookup_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_lookup_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_lookup_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastResult.QueryType = snap.last_result.query_type;
            out->LastResult.QueryId = snap.last_result.query_id;
            out->LastResult.Flags = snap.last_result.flags;
            out->LastResult.Status = snap.last_result.status;
            out->LastResult.MatchCount = snap.last_result.match_cnt;
            out->LastResult.OverflowCount = snap.last_result.overflow_cnt;
            out->LastResult.PatternHash = snap.last_result.pattern_hash;
            out->LastResult.LastKind = snap.last_result.last_kind;
            out->LastResult.LastId = snap.last_result.last_id;
            out->LastResult.LastSubsystemMask = snap.last_result.last_subsystem_mask;
            out->LastResult.LastScorePermille = snap.last_result.last_score_permille;
            out->LastResult.LastAux0 = snap.last_result.last_aux0;
            out->LastResult.LastAux1 = snap.last_result.last_aux1;
            out->LastResult.Tick100ns = snap.last_result.tick100ns;

            ULONG match_cnt = snap.last_match_cnt;
            if (match_cnt > IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY)
                match_cnt = IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY;
            out->LastMatchCount = match_cnt;

            for (ULONG i = 0; i < match_cnt; i++) {
                out->LastMatches[i].Kind = snap.last_matches[i].kind;
                out->LastMatches[i].Id = snap.last_matches[i].id;
                RtlCopyMemory(out->LastMatches[i].Name, snap.last_matches[i].name,
                              sizeof(out->LastMatches[i].Name));
                out->LastMatches[i].SubsystemMask = snap.last_matches[i].subsystem_mask;
                out->LastMatches[i].ScorePermille = snap.last_matches[i].score_permille;
                out->LastMatches[i].Aux0 = snap.last_matches[i].aux0;
                out->LastMatches[i].Aux1 = snap.last_matches[i].aux1;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_LOOKUP_RUN: {
            ioctl_rsc_lookup_run_request_t* in = NULL;
            size_t in_sz = 0;
            CHAR query_text[65];
            const CHAR* query_text_ptr = NULL;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(query_text, sizeof(query_text));
            RtlCopyMemory(query_text, in->QueryText, sizeof(in->QueryText));
            query_text[RTL_NUMBER_OF(query_text) - 1u] = '\0';
            if (query_text[0] != '\0')
                query_text_ptr = query_text;

            rscsw_lookup_execution_t execution;
            RtlZeroMemory(&execution, sizeof(execution));
            status = rsc_sw_run_lookup(in->QueryType, in->QueryId, in->Flags, query_text_ptr,
                                       &execution);

            ioctl_rsc_lookup_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                ULONG match_cnt = execution.match_cnt;

                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.QueryType = execution.result.query_type;
                out->Result.QueryId = execution.result.query_id;
                out->Result.Flags = execution.result.flags;
                out->Result.Status = execution.result.status;
                out->Result.MatchCount = execution.result.match_cnt;
                out->Result.OverflowCount = execution.result.overflow_cnt;
                out->Result.PatternHash = execution.result.pattern_hash;
                out->Result.LastKind = execution.result.last_kind;
                out->Result.LastId = execution.result.last_id;
                out->Result.LastSubsystemMask = execution.result.last_subsystem_mask;
                out->Result.LastScorePermille = execution.result.last_score_permille;
                out->Result.LastAux0 = execution.result.last_aux0;
                out->Result.LastAux1 = execution.result.last_aux1;
                out->Result.Tick100ns = execution.result.tick100ns;
                out->OverflowCount = execution.overflow_cnt;
                if (match_cnt > IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY)
                    match_cnt = IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY;
                out->MatchCount = match_cnt;

                for (ULONG i = 0; i < match_cnt; i++) {
                    out->Matches[i].Kind = execution.matches[i].kind;
                    out->Matches[i].Id = execution.matches[i].id;
                    RtlCopyMemory(out->Matches[i].Name, execution.matches[i].name,
                                  sizeof(out->Matches[i].Name));
                    out->Matches[i].SubsystemMask = execution.matches[i].subsystem_mask;
                    out->Matches[i].ScorePermille = execution.matches[i].score_permille;
                    out->Matches[i].Aux0 = execution.matches[i].aux0;
                    out->Matches[i].Aux1 = execution.matches[i].aux1;
                }

                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_LOOKUP_HISTORY: {
            ioctl_rsc_lookup_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_lookup_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_lookup_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_LOOKUP_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_LOOKUP_HISTORY_INFO_CAPACITY;

            rscsw_lookup_result_t hist[IOCTL_RSC_LOOKUP_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_lookup_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].QueryType = hist[i].query_type;
                out->Entries[i].QueryId = hist[i].query_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].MatchCount = hist[i].match_cnt;
                out->Entries[i].OverflowCount = hist[i].overflow_cnt;
                out->Entries[i].PatternHash = hist[i].pattern_hash;
                out->Entries[i].LastKind = hist[i].last_kind;
                out->Entries[i].LastId = hist[i].last_id;
                out->Entries[i].LastSubsystemMask = hist[i].last_subsystem_mask;
                out->Entries[i].LastScorePermille = hist[i].last_score_permille;
                out->Entries[i].LastAux0 = hist[i].last_aux0;
                out->Entries[i].LastAux1 = hist[i].last_aux1;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_PROVISION_SNAPSHOT: {
            ioctl_rsc_provision_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_provision_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_provision_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPreparedCount = snap.last_prepared_cnt;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastPolicyVoteCount = snap.last_policy_vote_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastAvgConfidencePermille = snap.last_avg_confidence_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_PROVISION_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_PROVISION_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].ClockDriverType = snap.rows[i].clock_driver_type;
                out->Rows[i].TlmmDriveType = snap.rows[i].tlmm_drive_type;
                out->Rows[i].TsensThresholdType = snap.rows[i].tsens_threshold_type;
                out->Rows[i].TargetState = snap.rows[i].target_state;
                out->Rows[i].TargetClockKhz = snap.rows[i].target_clock_khz;
                out->Rows[i].TargetFlags = snap.rows[i].target_flags;
                out->Rows[i].PolicyFlags = snap.rows[i].policy_flags;
                out->Rows[i].ConfidencePermille = snap.rows[i].confidence_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_PROVISION_RUN: {
            ioctl_rsc_provision_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_provision_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_provision(in->ModeId, in->Flags, &result);

            ioctl_rsc_provision_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.PreparedCount = result.prepared_cnt;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.PolicyVoteCount = result.policy_vote_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.AvgConfidencePermille = result.avg_confidence_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_PROVISION_HISTORY: {
            ioctl_rsc_provision_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_provision_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_provision_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_PROVISION_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_PROVISION_HISTORY_INFO_CAPACITY;

            rscsw_provision_result_t hist[IOCTL_RSC_PROVISION_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_provision_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].PreparedCount = hist[i].prepared_cnt;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].PolicyVoteCount = hist[i].policy_vote_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].AvgConfidencePermille = hist[i].avg_confidence_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_RAIL_SNAPSHOT: {
            ioctl_rsc_rail_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_rail_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_rail_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPreparedCount = snap.last_prepared_cnt;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastRailVoteCount = snap.last_rail_vote_cnt;
            out->LastBandwidthVoteCount = snap.last_bandwidth_vote_cnt;
            out->LastPolicyVoteCount = snap.last_policy_vote_cnt;
            out->LastGlinkEventCount = snap.last_glink_event_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastAvgConfidencePermille = snap.last_avg_confidence_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_RAIL_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_RAIL_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].ClockDriverType = snap.rows[i].clock_driver_type;
                out->Rows[i].TlmmDriveType = snap.rows[i].tlmm_drive_type;
                out->Rows[i].TsensThresholdType = snap.rows[i].tsens_threshold_type;
                out->Rows[i].PdcTriggerType = snap.rows[i].pdc_trigger_type;
                out->Rows[i].TargetState = snap.rows[i].target_state;
                out->Rows[i].TargetClockKhz = snap.rows[i].target_clock_khz;
                out->Rows[i].TargetFlags = snap.rows[i].target_flags;
                out->Rows[i].RailMicrovolts = snap.rows[i].rail_microvolts;
                out->Rows[i].BandwidthIb = snap.rows[i].bandwidth_ib;
                out->Rows[i].BandwidthAb = snap.rows[i].bandwidth_ab;
                out->Rows[i].PolicyFlags = snap.rows[i].policy_flags;
                out->Rows[i].ConfidencePermille = snap.rows[i].confidence_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_RAIL_RUN: {
            ioctl_rsc_rail_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_rail_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_rail(in->ModeId, in->Flags, &result);

            ioctl_rsc_rail_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.PreparedCount = result.prepared_cnt;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.RailVoteCount = result.rail_vote_cnt;
                out->Result.BandwidthVoteCount = result.bandwidth_vote_cnt;
                out->Result.PolicyVoteCount = result.policy_vote_cnt;
                out->Result.GlinkEventCount = result.glink_event_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.AvgConfidencePermille = result.avg_confidence_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_RAIL_HISTORY: {
            ioctl_rsc_rail_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_rail_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_rail_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_RAIL_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_RAIL_HISTORY_INFO_CAPACITY;

            rscsw_rail_result_t hist[IOCTL_RSC_RAIL_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_rail_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].PreparedCount = hist[i].prepared_cnt;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].RailVoteCount = hist[i].rail_vote_cnt;
                out->Entries[i].BandwidthVoteCount = hist[i].bandwidth_vote_cnt;
                out->Entries[i].PolicyVoteCount = hist[i].policy_vote_cnt;
                out->Entries[i].GlinkEventCount = hist[i].glink_event_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].AvgConfidencePermille = hist[i].avg_confidence_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_TCS_SNAPSHOT: {
            ioctl_rsc_tcs_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_tcs_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_tcs_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPreparedCount = snap.last_prepared_cnt;
            out->LastProgrammedCount = snap.last_programmed_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastPolicyVoteCount = snap.last_policy_vote_cnt;
            out->LastQueuedGlinkCount = snap.last_queued_glink_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastAvgConfidencePermille = snap.last_avg_confidence_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_TCS_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_TCS_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].CmdDbQueryType = snap.rows[i].cmd_db_query_type;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].RpmhPriority = snap.rows[i].rpmh_priority;
                out->Rows[i].TcsCount = snap.rows[i].tcs_cnt;
                out->Rows[i].CommandAddress = snap.rows[i].command_address;
                out->Rows[i].CommandData = snap.rows[i].command_data;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].PdcTriggerType = snap.rows[i].pdc_trigger_type;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].ClockDriverType = snap.rows[i].clock_driver_type;
                out->Rows[i].TsensThresholdType = snap.rows[i].tsens_threshold_type;
                out->Rows[i].TargetState = snap.rows[i].target_state;
                out->Rows[i].TargetClockKhz = snap.rows[i].target_clock_khz;
                out->Rows[i].TargetFlags = snap.rows[i].target_flags;
                out->Rows[i].PolicyFlags = snap.rows[i].policy_flags;
                out->Rows[i].CommandFlags = snap.rows[i].command_flags;
                out->Rows[i].ConfidencePermille = snap.rows[i].confidence_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_TCS_RUN: {
            ioctl_rsc_tcs_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_tcs_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_tcs(in->ModeId, in->Flags, &result);

            ioctl_rsc_tcs_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.PreparedCount = result.prepared_cnt;
                out->Result.ProgrammedCount = result.programmed_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.PolicyVoteCount = result.policy_vote_cnt;
                out->Result.QueuedGlinkCount = result.queued_glink_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.AvgConfidencePermille = result.avg_confidence_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_TCS_HISTORY: {
            ioctl_rsc_tcs_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_tcs_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_tcs_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_TCS_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_TCS_HISTORY_INFO_CAPACITY;

            rscsw_tcs_result_t hist[IOCTL_RSC_TCS_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_tcs_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].PreparedCount = hist[i].prepared_cnt;
                out->Entries[i].ProgrammedCount = hist[i].programmed_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].PolicyVoteCount = hist[i].policy_vote_cnt;
                out->Entries[i].QueuedGlinkCount = hist[i].queued_glink_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].AvgConfidencePermille = hist[i].avg_confidence_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_THERMAL_SNAPSHOT: {
            ioctl_rsc_thermal_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_thermal_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_thermal_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPreparedCount = snap.last_prepared_cnt;
            out->LastSampledCount = snap.last_sampled_cnt;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastMitigatedCount = snap.last_mitigated_cnt;
            out->LastRecoveredCount = snap.last_recovered_cnt;
            out->LastPolicyVoteCount = snap.last_policy_vote_cnt;
            out->LastGlinkEventCount = snap.last_glink_event_cnt;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastAvgThermalPressurePermille = snap.last_avg_thermal_pressure_permille;
            out->LastAvgConfidencePermille = snap.last_avg_confidence_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_THERMAL_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_THERMAL_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].CmdDbQueryType = snap.rows[i].cmd_db_query_type;
                out->Rows[i].RpmhSet = snap.rows[i].rpmh_set;
                out->Rows[i].NpaClientType = snap.rows[i].npa_client_type;
                out->Rows[i].GlinkEventType = snap.rows[i].glink_event_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].TsensThresholdType = snap.rows[i].tsens_threshold_type;
                out->Rows[i].TlmmDriveType = snap.rows[i].tlmm_drive_type;
                out->Rows[i].ClockDriverType = snap.rows[i].clock_driver_type;
                out->Rows[i].PdcTriggerType = snap.rows[i].pdc_trigger_type;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].SensorIndex = snap.rows[i].sensor_idx;
                out->Rows[i].SampleTempDeciC = snap.rows[i].sample_temp_deci_c;
                out->Rows[i].LowThresholdDeciC = snap.rows[i].low_threshold_deci_c;
                out->Rows[i].HighThresholdDeciC = snap.rows[i].high_threshold_deci_c;
                out->Rows[i].ThermalPressurePermille = snap.rows[i].thermal_pressure_permille;
                out->Rows[i].TargetState = snap.rows[i].target_state;
                out->Rows[i].TargetClockKhz = snap.rows[i].target_clock_khz;
                out->Rows[i].TargetFlags = snap.rows[i].target_flags;
                out->Rows[i].PolicyFlags = snap.rows[i].policy_flags;
                out->Rows[i].ActionFlags = snap.rows[i].action_flags;
                out->Rows[i].ConfidencePermille = snap.rows[i].confidence_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_THERMAL_RUN: {
            ioctl_rsc_thermal_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_thermal_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_thermal(in->ModeId, in->Flags, &result);

            ioctl_rsc_thermal_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.PreparedCount = result.prepared_cnt;
                out->Result.SampledCount = result.sampled_cnt;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.MitigatedCount = result.mitigated_cnt;
                out->Result.RecoveredCount = result.recovered_cnt;
                out->Result.PolicyVoteCount = result.policy_vote_cnt;
                out->Result.GlinkEventCount = result.glink_event_cnt;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.AvgThermalPressurePermille = result.avg_thermal_pressure_permille;
                out->Result.AvgConfidencePermille = result.avg_confidence_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_THERMAL_HISTORY: {
            ioctl_rsc_thermal_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_thermal_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_thermal_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_THERMAL_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_THERMAL_HISTORY_INFO_CAPACITY;

            rscsw_thermal_result_t hist[IOCTL_RSC_THERMAL_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_thermal_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].PreparedCount = hist[i].prepared_cnt;
                out->Entries[i].SampledCount = hist[i].sampled_cnt;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].MitigatedCount = hist[i].mitigated_cnt;
                out->Entries[i].RecoveredCount = hist[i].recovered_cnt;
                out->Entries[i].PolicyVoteCount = hist[i].policy_vote_cnt;
                out->Entries[i].GlinkEventCount = hist[i].glink_event_cnt;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].AvgThermalPressurePermille = hist[i].avg_thermal_pressure_permille;
                out->Entries[i].AvgConfidencePermille = hist[i].avg_confidence_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_COORD_SNAPSHOT: {
            ioctl_rsc_coord_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_coord_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_coord_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastPreparedCount = snap.last_prepared_cnt;
            out->LastAppliedCount = snap.last_applied_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastExecutedPhaseMask = snap.last_executed_phase_mask;
            out->LastFailedPhaseMask = snap.last_failed_phase_mask;
            out->LastAvgConfidencePermille = snap.last_avg_confidence_permille;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_COORD_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_COORD_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].StateBefore = snap.rows[i].state_before;
                out->Rows[i].ClockBeforeKhz = snap.rows[i].clock_before_khz;
                out->Rows[i].FlagsBefore = snap.rows[i].flags_before;
                out->Rows[i].StateAfter = snap.rows[i].state_after;
                out->Rows[i].ClockAfterKhz = snap.rows[i].clock_after_khz;
                out->Rows[i].FlagsAfter = snap.rows[i].flags_after;
                out->Rows[i].PhaseStatusMask = snap.rows[i].phase_status_mask;
                out->Rows[i].FailureMask = snap.rows[i].failure_mask;
                out->Rows[i].ConfidencePermille = snap.rows[i].confidence_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_COORD_RUN: {
            ioctl_rsc_coord_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_coord_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_coord(in->ModeId, in->Flags, &result);

            ioctl_rsc_coord_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.PreparedCount = result.prepared_cnt;
                out->Result.AppliedCount = result.applied_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.ExecutedPhaseMask = result.executed_phase_mask;
                out->Result.FailedPhaseMask = result.failed_phase_mask;
                out->Result.SyncStatus = result.sync_status;
                out->Result.ValidateStatus = result.validate_status;
                out->Result.SequenceStatus = result.sequence_status;
                out->Result.ProvisionStatus = result.provision_status;
                out->Result.RailStatus = result.rail_status;
                out->Result.TcsStatus = result.tcs_status;
                out->Result.ThermalStatus = result.thermal_status;
                out->Result.FabricStatus = result.fabric_status;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.AvgConfidencePermille = result.avg_confidence_permille;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_COORD_HISTORY: {
            ioctl_rsc_coord_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_coord_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_coord_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_COORD_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_COORD_HISTORY_INFO_CAPACITY;

            rscsw_coord_result_t hist[IOCTL_RSC_COORD_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_coord_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].PreparedCount = hist[i].prepared_cnt;
                out->Entries[i].AppliedCount = hist[i].applied_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].ExecutedPhaseMask = hist[i].executed_phase_mask;
                out->Entries[i].FailedPhaseMask = hist[i].failed_phase_mask;
                out->Entries[i].SyncStatus = hist[i].sync_status;
                out->Entries[i].ValidateStatus = hist[i].validate_status;
                out->Entries[i].SequenceStatus = hist[i].sequence_status;
                out->Entries[i].ProvisionStatus = hist[i].provision_status;
                out->Entries[i].RailStatus = hist[i].rail_status;
                out->Entries[i].TcsStatus = hist[i].tcs_status;
                out->Entries[i].ThermalStatus = hist[i].thermal_status;
                out->Entries[i].FabricStatus = hist[i].fabric_status;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].AvgConfidencePermille = hist[i].avg_confidence_permille;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_AUDIT_SNAPSHOT: {
            ioctl_rsc_audit_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_audit_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_audit_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastCheckedCount = snap.last_checked_cnt;
            out->LastRepairedCount = snap.last_repaired_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastValidateStatus = snap.last_validate_status;
            out->LastCoordStatus = snap.last_coord_status;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG row_cnt = snap.row_cnt;
            if (row_cnt > IOCTL_RSC_AUDIT_ROW_INFO_CAPACITY)
                row_cnt = IOCTL_RSC_AUDIT_ROW_INFO_CAPACITY;
            out->RowCount = row_cnt;

            for (ULONG i = 0; i < row_cnt; i++) {
                out->Rows[i].SubsystemId = snap.rows[i].subsystem_id;
                RtlCopyMemory(out->Rows[i].Name, snap.rows[i].name, sizeof(out->Rows[i].Name));
                out->Rows[i].ModeId = snap.rows[i].mode_id;
                out->Rows[i].DriverMapId = snap.rows[i].driver_map_id;
                out->Rows[i].ArcResourceType = snap.rows[i].arc_res_type;
                out->Rows[i].CmdDbSlaveId = snap.rows[i].cmd_db_slave_id;
                out->Rows[i].CmdDbQueryType = snap.rows[i].cmd_db_query_type;
                out->Rows[i].PppResourceType = snap.rows[i].ppp_res_type;
                out->Rows[i].IcbMasterId = snap.rows[i].icb_master_id;
                out->Rows[i].IcbSlaveId = snap.rows[i].icb_slave_id;
                out->Rows[i].AopSubsystemType = snap.rows[i].aop_subsystem_type;
                out->Rows[i].DomainId = snap.rows[i].domain_id;
                out->Rows[i].DomainCount = snap.rows[i].domain_cnt;
                out->Rows[i].RouteIdPrimary = snap.rows[i].route_id_primary;
                out->Rows[i].RouteCount = snap.rows[i].route_cnt;
                out->Rows[i].RoutePeakBandwidthMb = snap.rows[i].route_peak_bandwidth_mb;
                out->Rows[i].DriverMapFanout = snap.rows[i].driver_map_fanout;
                out->Rows[i].IntegrityPenaltyPermille = snap.rows[i].integrity_penalty_permille;
                out->Rows[i].IntegrityFlags = snap.rows[i].integrity_flags;
                out->Rows[i].DependencyIn = snap.rows[i].dependency_in;
                out->Rows[i].DependencyOut = snap.rows[i].dependency_out;
                out->Rows[i].DependencyRiskPermille = snap.rows[i].dependency_risk_permille;
                out->Rows[i].DomainCoveragePermille = snap.rows[i].domain_coverage_permille;
                out->Rows[i].RouteCoveragePermille = snap.rows[i].route_coverage_permille;
                out->Rows[i].TopologyPressurePermille = snap.rows[i].topology_pressure_permille;
                out->Rows[i].SemanticCoveragePermille = snap.rows[i].semantic_coverage_permille;
                out->Rows[i].ProtocolDriftPermille = snap.rows[i].protocol_drift_permille;
                out->Rows[i].StateBefore = snap.rows[i].state_before;
                out->Rows[i].ClockBeforeKhz = snap.rows[i].clock_before_khz;
                out->Rows[i].FlagsBefore = snap.rows[i].flags_before;
                out->Rows[i].StateAfter = snap.rows[i].state_after;
                out->Rows[i].ClockAfterKhz = snap.rows[i].clock_after_khz;
                out->Rows[i].FlagsAfter = snap.rows[i].flags_after;
                out->Rows[i].IssueMask = snap.rows[i].issue_mask;
                out->Rows[i].WarningMask = snap.rows[i].warning_mask;
                out->Rows[i].ScorePermille = snap.rows[i].score_permille;
                out->Rows[i].LastStatus = snap.rows[i].last_status;
                out->Rows[i].LastTick100ns = snap.rows[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_AUDIT_RUN: {
            ioctl_rsc_audit_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_audit_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_audit(in->ModeId, in->Flags, &result);

            ioctl_rsc_audit_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.Status = result.status;
                out->Result.CheckedCount = result.checked_cnt;
                out->Result.RepairedCount = result.repaired_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.ValidateStatus = result.validate_status;
                out->Result.CoordStatus = result.coord_status;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.LastSubsystemId = result.last_subsystem_id;
                out->Result.LastIssueMask = result.last_issue_mask;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_AUDIT_HISTORY: {
            ioctl_rsc_audit_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_audit_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_audit_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_AUDIT_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_AUDIT_HISTORY_INFO_CAPACITY;

            rscsw_audit_result_t hist[IOCTL_RSC_AUDIT_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_audit_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].CheckedCount = hist[i].checked_cnt;
                out->Entries[i].RepairedCount = hist[i].repaired_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].ValidateStatus = hist[i].validate_status;
                out->Entries[i].CoordStatus = hist[i].coord_status;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].LastSubsystemId = hist[i].last_subsystem_id;
                out->Entries[i].LastIssueMask = hist[i].last_issue_mask;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SUPERVISOR_SNAPSHOT: {
            ioctl_rsc_supervisor_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_supervisor_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_supervisor_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastCyclesRequested = snap.last_cycles_requested;
            out->LastCyclesCompleted = snap.last_cycles_completed;
            out->LastFailureCount = snap.last_failure_cnt;
            out->LastCheckedCount = snap.last_checked_cnt;
            out->LastRepairedCount = snap.last_repaired_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastAuditStatus = snap.last_audit_status;
            out->LastCoordStatus = snap.last_coord_status;
            out->LastThermalStatus = snap.last_thermal_status;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG module_cnt = snap.module_cnt;
            if (module_cnt > IOCTL_RSC_SUPERVISOR_MODULE_INFO_CAPACITY)
                module_cnt = IOCTL_RSC_SUPERVISOR_MODULE_INFO_CAPACITY;
            out->ModuleCount = module_cnt;

            for (ULONG i = 0; i < module_cnt; i++) {
                out->Modules[i].ModuleId = snap.modules[i].module_id;
                RtlCopyMemory(out->Modules[i].Name, snap.modules[i].name,
                              sizeof(out->Modules[i].Name));
                out->Modules[i].IssueCount = snap.modules[i].issue_cnt;
                out->Modules[i].WarningCount = snap.modules[i].warning_cnt;
                out->Modules[i].RepairCount = snap.modules[i].repair_cnt;
                out->Modules[i].SampleCount = snap.modules[i].sample_cnt;
                out->Modules[i].ScorePermille = snap.modules[i].score_permille;
                out->Modules[i].IntegrityPenaltyPermilleAvg =
                    snap.modules[i].integrity_penalty_permille_avg;
                out->Modules[i].DependencyGapCount = snap.modules[i].dependency_gap_cnt;
                out->Modules[i].DomainGapCount = snap.modules[i].domain_gap_cnt;
                out->Modules[i].RouteGapCount = snap.modules[i].route_gap_cnt;
                out->Modules[i].FanoutViolationCount = snap.modules[i].fanout_violation_cnt;
                out->Modules[i].CoverageScorePermilleAvg =
                    snap.modules[i].coverage_score_permille_avg;
                out->Modules[i].TopologyPressurePermilleAvg =
                    snap.modules[i].topology_pressure_permille_avg;
                out->Modules[i].SemanticCoveragePermilleAvg =
                    snap.modules[i].semantic_coverage_permille_avg;
                out->Modules[i].ProtocolDriftPermilleAvg =
                    snap.modules[i].protocol_drift_permille_avg;
                out->Modules[i].LastStatus = snap.modules[i].last_status;
                out->Modules[i].LastTick100ns = snap.modules[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_SUPERVISOR_RUN: {
            ioctl_rsc_supervisor_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_supervisor_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_supervisor(in->ModeId, in->Flags, in->Cycles, &result);

            ioctl_rsc_supervisor_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.CyclesRequested = result.cycles_requested;
                out->Result.CyclesCompleted = result.cycles_completed;
                out->Result.Status = result.status;
                out->Result.FailureCount = result.failure_cnt;
                out->Result.CheckedCount = result.checked_cnt;
                out->Result.RepairedCount = result.repaired_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.AuditStatus = result.audit_status;
                out->Result.CoordStatus = result.coord_status;
                out->Result.ThermalStatus = result.thermal_status;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.LastModuleId = result.last_module_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_SUPERVISOR_HISTORY: {
            ioctl_rsc_supervisor_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_supervisor_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_supervisor_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_SUPERVISOR_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_SUPERVISOR_HISTORY_INFO_CAPACITY;

            rscsw_supervisor_result_t hist[IOCTL_RSC_SUPERVISOR_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_supervisor_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].CyclesRequested = hist[i].cycles_requested;
                out->Entries[i].CyclesCompleted = hist[i].cycles_completed;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].FailureCount = hist[i].failure_cnt;
                out->Entries[i].CheckedCount = hist[i].checked_cnt;
                out->Entries[i].RepairedCount = hist[i].repaired_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].AuditStatus = hist[i].audit_status;
                out->Entries[i].CoordStatus = hist[i].coord_status;
                out->Entries[i].ThermalStatus = hist[i].thermal_status;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].LastModuleId = hist[i].last_module_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_AUTOTUNE_SNAPSHOT: {
            ioctl_rsc_autotune_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_autotune_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_autotune_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastCyclesRequested = snap.last_cycles_requested;
            out->LastCyclesCompleted = snap.last_cycles_completed;
            out->LastCheckedCount = snap.last_checked_cnt;
            out->LastAdjustedCount = snap.last_adjusted_cnt;
            out->LastFailedAdjustCount = snap.last_failed_adjust_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastSupervisorStatus = snap.last_supervisor_status;
            out->LastThermalStatus = snap.last_thermal_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG module_cnt = snap.module_cnt;
            if (module_cnt > IOCTL_RSC_AUTOTUNE_MODULE_INFO_CAPACITY)
                module_cnt = IOCTL_RSC_AUTOTUNE_MODULE_INFO_CAPACITY;
            out->ModuleCount = module_cnt;

            for (ULONG i = 0; i < module_cnt; i++) {
                out->Modules[i].ModuleId = snap.modules[i].module_id;
                RtlCopyMemory(out->Modules[i].Name, snap.modules[i].name,
                              sizeof(out->Modules[i].Name));
                out->Modules[i].TargetScorePermille = snap.modules[i].target_score_permille;
                out->Modules[i].EffectiveTargetScorePermille =
                    snap.modules[i].effective_target_score_permille;
                out->Modules[i].ObservedScorePermille = snap.modules[i].observed_score_permille;
                out->Modules[i].DeltaPermille = snap.modules[i].delta_permille;
                out->Modules[i].RiskScorePermille = snap.modules[i].risk_score_permille;
                out->Modules[i].IntegrityPenaltyPermille =
                    snap.modules[i].integrity_penalty_permille;
                out->Modules[i].DependencyRiskPermille = snap.modules[i].dependency_risk_permille;
                out->Modules[i].DomainGapCount = snap.modules[i].domain_gap_cnt;
                out->Modules[i].RouteGapCount = snap.modules[i].route_gap_cnt;
                out->Modules[i].FanoutViolationCount = snap.modules[i].fanout_violation_cnt;
                out->Modules[i].ThermalPressurePermille = snap.modules[i].thermal_pressure_permille;
                out->Modules[i].CoverageScorePermille = snap.modules[i].coverage_score_permille;
                out->Modules[i].TopologyPressurePermille =
                    snap.modules[i].topology_pressure_permille;
                out->Modules[i].SemanticCoveragePermille =
                    snap.modules[i].semantic_coverage_permille;
                out->Modules[i].ProtocolDriftPermille = snap.modules[i].protocol_drift_permille;
                out->Modules[i].ActionMask = snap.modules[i].action_mask;
                out->Modules[i].LastStatus = snap.modules[i].last_status;
                out->Modules[i].LastTick100ns = snap.modules[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_AUTOTUNE_RUN: {
            ioctl_rsc_autotune_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_autotune_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_autotune(in->ModeId, in->Flags, in->Cycles, &result);

            ioctl_rsc_autotune_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.CyclesRequested = result.cycles_requested;
                out->Result.CyclesCompleted = result.cycles_completed;
                out->Result.Status = result.status;
                out->Result.CheckedCount = result.checked_cnt;
                out->Result.AdjustedCount = result.adjusted_cnt;
                out->Result.FailedAdjustCount = result.failed_adjust_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.SupervisorStatus = result.supervisor_status;
                out->Result.ThermalStatus = result.thermal_status;
                out->Result.LastModuleId = result.last_module_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_AUTOTUNE_HISTORY: {
            ioctl_rsc_autotune_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_autotune_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_autotune_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_AUTOTUNE_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_AUTOTUNE_HISTORY_INFO_CAPACITY;

            rscsw_autotune_result_t hist[IOCTL_RSC_AUTOTUNE_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_autotune_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].CyclesRequested = hist[i].cycles_requested;
                out->Entries[i].CyclesCompleted = hist[i].cycles_completed;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].CheckedCount = hist[i].checked_cnt;
                out->Entries[i].AdjustedCount = hist[i].adjusted_cnt;
                out->Entries[i].FailedAdjustCount = hist[i].failed_adjust_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].SupervisorStatus = hist[i].supervisor_status;
                out->Entries[i].ThermalStatus = hist[i].thermal_status;
                out->Entries[i].LastModuleId = hist[i].last_module_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_HARDEN_SNAPSHOT: {
            ioctl_rsc_harden_snapshot_response_t* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_harden_snapshot_t snap;
            RtlZeroMemory(&snap, sizeof(snap));
            status = rsc_sw_get_harden_snapshot(&snap);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, sizeof(*out));
            out->Version = snap.version;
            out->RunCount = snap.run_cnt;
            out->FailureCount = snap.failure_cnt;
            out->LastStatus = snap.last_status;
            out->LastModeId = snap.last_mode_id;
            out->LastFlags = snap.last_flags;
            out->LastCyclesRequested = snap.last_cycles_requested;
            out->LastCyclesCompleted = snap.last_cycles_completed;
            out->LastCheckedCount = snap.last_checked_cnt;
            out->LastHardenedCount = snap.last_hardened_cnt;
            out->LastFailedCount = snap.last_failed_cnt;
            out->LastWarningCount = snap.last_warning_cnt;
            out->LastErrorCount = snap.last_error_cnt;
            out->LastOverallScorePermille = snap.last_overall_score_permille;
            out->LastAutotuneStatus = snap.last_autotune_status;
            out->LastSupervisorStatus = snap.last_supervisor_status;
            out->LastAuditStatus = snap.last_audit_status;
            out->LastCoordStatus = snap.last_coord_status;
            out->LastThermalStatus = snap.last_thermal_status;
            out->LastInterconnectStatus = snap.last_interconnect_status;
            out->LastDomainStatus = snap.last_domain_status;
            out->LastTick100ns = snap.last_tick100ns;

            ULONG module_cnt = snap.module_cnt;
            if (module_cnt > IOCTL_RSC_HARDEN_MODULE_INFO_CAPACITY)
                module_cnt = IOCTL_RSC_HARDEN_MODULE_INFO_CAPACITY;
            out->ModuleCount = module_cnt;

            for (ULONG i = 0; i < module_cnt; i++) {
                out->Modules[i].ModuleId = snap.modules[i].module_id;
                RtlCopyMemory(out->Modules[i].Name, snap.modules[i].name,
                              sizeof(out->Modules[i].Name));
                out->Modules[i].HealthScorePermille = snap.modules[i].health_score_permille;
                out->Modules[i].RiskScorePermille = snap.modules[i].risk_score_permille;
                out->Modules[i].RecoveryScorePermille = snap.modules[i].recovery_score_permille;
                out->Modules[i].IntegrityPenaltyPermille =
                    snap.modules[i].integrity_penalty_permille;
                out->Modules[i].DependencyRiskPermille = snap.modules[i].dependency_risk_permille;
                out->Modules[i].DomainGapCount = snap.modules[i].domain_gap_cnt;
                out->Modules[i].RouteGapCount = snap.modules[i].route_gap_cnt;
                out->Modules[i].FanoutViolationCount = snap.modules[i].fanout_violation_cnt;
                out->Modules[i].CoverageScorePermille = snap.modules[i].coverage_score_permille;
                out->Modules[i].TopologyPressurePermille =
                    snap.modules[i].topology_pressure_permille;
                out->Modules[i].SemanticCoveragePermille =
                    snap.modules[i].semantic_coverage_permille;
                out->Modules[i].ProtocolDriftPermille = snap.modules[i].protocol_drift_permille;
                out->Modules[i].ActionMask = snap.modules[i].action_mask;
                out->Modules[i].LastStatus = snap.modules[i].last_status;
                out->Modules[i].LastTick100ns = snap.modules[i].last_tick100ns;
            }

            info = sizeof(*out);
            status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_PEP_RSC_HARDEN_RUN: {
            ioctl_rsc_harden_run_request_t* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(*in), (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status))
                break;

            rscsw_harden_result_t result;
            RtlZeroMemory(&result, sizeof(result));
            status = rsc_sw_run_harden(in->ModeId, in->Flags, in->Cycles, &result);

            ioctl_rsc_harden_run_response_t* out = NULL;
            size_t out_sz = 0;
            NTSTATUS out_status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(*out), (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(out_status)) {
                RtlZeroMemory(out, sizeof(*out));
                out->Status = status;
                out->Result.ModeId = result.mode_id;
                out->Result.Flags = result.flags;
                out->Result.CyclesRequested = result.cycles_requested;
                out->Result.CyclesCompleted = result.cycles_completed;
                out->Result.Status = result.status;
                out->Result.CheckedCount = result.checked_cnt;
                out->Result.HardenedCount = result.hardened_cnt;
                out->Result.FailedCount = result.failed_cnt;
                out->Result.WarningCount = result.warning_cnt;
                out->Result.ErrorCount = result.error_cnt;
                out->Result.OverallScorePermille = result.overall_score_permille;
                out->Result.AutotuneStatus = result.autotune_status;
                out->Result.SupervisorStatus = result.supervisor_status;
                out->Result.AuditStatus = result.audit_status;
                out->Result.CoordStatus = result.coord_status;
                out->Result.ThermalStatus = result.thermal_status;
                out->Result.InterconnectStatus = result.interconnect_status;
                out->Result.DomainStatus = result.domain_status;
                out->Result.LastModuleId = result.last_module_id;
                out->Result.LastTag = result.last_tag;
                out->Result.LastValue0 = result.last_value0;
                out->Result.LastValue1 = result.last_value1;
                out->Result.LastValue2 = result.last_value2;
                out->Result.LastValue3 = result.last_value3;
                out->Result.Tick100ns = result.tick100ns;
                info = sizeof(*out);
            } else if (out_status == STATUS_BUFFER_TOO_SMALL) {
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_RSC_HARDEN_HISTORY: {
            ioctl_rsc_harden_history_response_t* out = NULL;
            size_t out_sz = 0;
            const size_t hdr_sz = FIELD_OFFSET(ioctl_rsc_harden_history_response_t, Entries);
            const ULONG entry_size = sizeof(ioctl_rsc_harden_result_info_t);
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, hdr_sz + entry_size, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status))
                break;

            ULONG cap = 0;
            if (out_sz > hdr_sz)
                cap = (ULONG)((out_sz - hdr_sz) / entry_size);
            if (cap == 0u) {
                status = STATUS_BUFFER_TOO_SMALL;
                info = hdr_sz + entry_size;
                break;
            }
            if (cap > IOCTL_RSC_HARDEN_HISTORY_INFO_CAPACITY)
                cap = IOCTL_RSC_HARDEN_HISTORY_INFO_CAPACITY;

            rscsw_harden_result_t hist[IOCTL_RSC_HARDEN_HISTORY_INFO_CAPACITY];
            ULONG count = 0;
            ULONG dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));

            status = rsc_sw_get_harden_history(hist, cap, &count, &dropped);
            if (!NT_SUCCESS(status))
                break;

            RtlZeroMemory(out, hdr_sz);
            out->Capacity = cap;
            out->Count = count;
            out->Dropped = dropped;
            out->EntrySize = entry_size;

            for (ULONG i = 0; i < count; i++) {
                out->Entries[i].ModeId = hist[i].mode_id;
                out->Entries[i].Flags = hist[i].flags;
                out->Entries[i].CyclesRequested = hist[i].cycles_requested;
                out->Entries[i].CyclesCompleted = hist[i].cycles_completed;
                out->Entries[i].Status = hist[i].status;
                out->Entries[i].CheckedCount = hist[i].checked_cnt;
                out->Entries[i].HardenedCount = hist[i].hardened_cnt;
                out->Entries[i].FailedCount = hist[i].failed_cnt;
                out->Entries[i].WarningCount = hist[i].warning_cnt;
                out->Entries[i].ErrorCount = hist[i].error_cnt;
                out->Entries[i].OverallScorePermille = hist[i].overall_score_permille;
                out->Entries[i].AutotuneStatus = hist[i].autotune_status;
                out->Entries[i].SupervisorStatus = hist[i].supervisor_status;
                out->Entries[i].AuditStatus = hist[i].audit_status;
                out->Entries[i].CoordStatus = hist[i].coord_status;
                out->Entries[i].ThermalStatus = hist[i].thermal_status;
                out->Entries[i].InterconnectStatus = hist[i].interconnect_status;
                out->Entries[i].DomainStatus = hist[i].domain_status;
                out->Entries[i].LastModuleId = hist[i].last_module_id;
                out->Entries[i].LastTag = hist[i].last_tag;
                out->Entries[i].LastValue0 = hist[i].last_value0;
                out->Entries[i].LastValue1 = hist[i].last_value1;
                out->Entries[i].LastValue2 = hist[i].last_value2;
                out->Entries[i].LastValue3 = hist[i].last_value3;
                out->Entries[i].Tick100ns = hist[i].tick100ns;
            }

            info = hdr_sz + ((size_t)count * entry_size);
            status = STATUS_SUCCESS;
            break;
        }

            /* ---------------- crash_dump / LiveKernelDump IOCTLs (DMF interfaces)
             * ----------------. */

        case 0x22A000: /* crash_dump: DataSourceCreateFromRequest. */
        case 0x22A004: /* crash_dump: DataSourceDestroyAuxiliary. */
        case 0x22A008: /* crash_dump: DataSourceWriteFromRequest. */
        case 0x226010: /* crash_dump: DataSourceReadFromRequest. */
        case 0x226014: /* crash_dump: DataSourceOpenFromRequest. */
        case 0x226018: /* crash_dump: DataSourceCaptureFromRequest. */
        case 0x22CB00: {
            /* crashdump sources belong to per-file-object slots. */

            if (io_control_code != 0x22CB00 && io_control_code != 0x22A008 &&
                !WdfRequestGetFileObject(req_arg)) {
                status = STATUS_INVALID_ADDRESS;
                info = 0;
                break;
            }

            if (io_control_code == 0x22CB00 && input_buf_length < 37) {

                status = STATUS_BUFFER_TOO_SMALL;
                info = 0;
                break;
            }

            PVOID in = NULL, out = NULL;
            size_t in_sz = 0, out_sz = 0;
            PVOID file_object = WdfRequestGetFileObject(req_arg);

            (VOID) WdfRequestRetrieveInputBuffer(req_arg, 0, &in, &in_sz);
            (VOID) WdfRequestRetrieveOutputBuffer(req_arg, 0, &out, &out_sz);

            size_t written = 0;
            status = crash_dump_handle_ioctl(io_control_code, file_object, in, in_sz, out, out_sz,
                                             &written);
            info = written;
            break;
        }

        case IOCTL_PEP_DMF_DEVICEINTERFACE_SEND: {

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 0x1C, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status) && in && in_sz >= 0x1C) {
                GUID g;
                RtlCopyMemory(&g, in, sizeof(GUID));
                ULONG f0 = *(ULONG*)(in + 16);
                ULONG f1 = *(ULONG*)(in + 20);
                ULONG f2 = *(ULONG*)(in + 24);
                ULONG f3 = (in_sz >= 0x20) ? *(ULONG*)(in + 28) : 0;

                ULONG port_id = 0;
                ULONG code1 = f0;
                ULONG code2 = f1;
                ULONG sz = f2;

                if (in_sz >= 0x20 && (f0 & 0xFFFF0000u) == 0 && f1 >= 0x80000000u &&
                    f2 >= 0x80000000u && f3 > 0 && f3 <= 0x00100000u) {
                    port_id = f0 & 0xFFFFu;
                    code1 = f1;
                    code2 = f2;
                    sz = f3;
                }

                res_key_t k = {0x444D4630u, code1};
                res_value_t v = {code2, sz, (ULONG)in_sz, port_id};
                (VOID) res_set(k, v);

                res_key_t k2 = {0x31464D44u, code2};
                res_value_t v2 = {code1, port_id, (ULONG)g.Data1,
                                  (ULONG)g.Data2 | ((ULONG)g.Data3 << 16)};
                (VOID) res_set(k2, v2);

                if (sz && forward_enabled()) {
                    ULONG probe_out = sz;
                    if (probe_out > 0x00010000u)
                        probe_out = 0x00010000u;

                    UCHAR* out_tmp =
                        (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, probe_out, POOL_TAG);
                    if (out_tmp) {
                        RtlZeroMemory(out_tmp, probe_out);
                        ULONG br = 0;
                        (VOID)
                            forward_send_guid(device, &g, code1, NULL, 0, out_tmp, probe_out, &br);
                        if (code2 && code2 != code1) {
                            ULONG br2 = 0;
                            (VOID) forward_send_guid(device, &g, code2, NULL, 0, out_tmp, probe_out,
                                                     &br2);
                        }
                        ExFreePool(out_tmp);
                    }
                }

                status = STATUS_SUCCESS;
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_C3502FF8: {

            info = input_buf_length;
            PVOID in = NULL;
            size_t in_sz = 0;
            if (input_buf_length != 0) {
                status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
                if (!NT_SUCCESS(status)) {
                    info = 0;
                    break;
                }
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            if (output_buf_length != 0) {
                status = WdfRequestRetrieveOutputBuffer(req_arg, output_buf_length, &out, &out_sz);
                if (!NT_SUCCESS(status)) {
                    info = 0;
                    break;
                }
            }

            if (!in || !out) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            UNREFERENCED_PARAMETER(in_sz);
            UNREFERENCED_PARAMETER(out_sz);
            status = STATUS_UNSUCCESSFUL;
            break;
        }

        case IOCTL_PEP_THERMAL_PMIC_ALARM_QUERY: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 4, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 0x14, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONG channel = *in;
                    ULONG temp_k10 =
                        registry_query_u_long(L"PmicTempDeciK", tsens_get_temp_deci_c() + 2730);
                    ULONG hi_k10 = registry_query_u_long(L"PmicTempHighDeciK", temp_k10 + 100);
                    ULONG lo_k10 = registry_query_u_long(L"PmicTempLowDeciK", temp_k10 - 100);
                    ULONG flags = registry_query_u_long(L"PmicTempFlags", 0);

                    out[0] = channel;
                    out[1] = temp_k10;
                    out[2] = hi_k10;
                    out[3] = lo_k10;
                    out[4] = flags;

                    info = 0x14;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 0x14;
                }
            }
            break;
        }

        case IOCTL_PEP_THERMAL_PMIC_ALARM_STATE: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 4, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 8, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONG channel = *in;
                    ULONG state = registry_query_u_long(L"PmicTempAlarmState", 0);

                    out[0] = channel;
                    out[1] = state;

                    info = 8;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 8;
                }
            }
            break;
        }

        case IOCTL_PEP_PMIC_HWINFO: {
            /* PMIC hardware readiness uses a 0xA4-byte pm_send_msg response.
             */
            UCHAR* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 0xA4, (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                RtlZeroMemory(out, out_sz);

                WDFDEVICE dev = WdfIoQueueGetDevice(queue);
                p_dev_ctx_t ctx = get_dev_ctx(dev);
                chipinfo_t ci = chip_info_get();

                ((ULONG*)out)[0] = ctx ? ctx->port_id : 0;
                ((ULONG*)out)[1] = ci.chip_id;
                ((ULONG*)out)[2] = ci.chip_version;
                ((ULONG*)out)[3] = ci.chip_family;
                ((ULONG*)out)[4] = registry_query_u_long(L"PmicIoctlsEnable", 1);

                info = 0xA4;
                status = STATUS_SUCCESS;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 0xA4;
            }
            break;
        }

        case IOCTL_PEP_PMIC_CLX_EVENTCOUNT: {

            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 8, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONGLONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 8, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    UNREFERENCED_PARAMETER(in);

                    ULONG cnt = registry_query_u_long(L"ClxEventCount", (ULONG)g_stats.ioctl_cnt);

                    *out = (ULONGLONG)cnt;

                    info = 8;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 8;
                }
            }
            break;
        }

        case IOCTL_PEP_BMS_SET_THRESHOLDS: {

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 0x14, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {

                ULONG a = *(ULONG*)&in[0];
                ULONG b = *(ULONG*)&in[4];
                ULONG c = *(ULONG*)&in[8];
                ULONG d = *(ULONG*)&in[12];
                ULONG e = *(ULONG*)&in[16];

                res_key_t k = {0x424D5330u, a};
                res_value_t v = {b, c, d, e};
                (VOID) res_set(k, v);

                status = STATUS_SUCCESS;
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_BMS_QUERY: {

            ULONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 0x10, (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                ULONG temp_k10 =
                    registry_query_u_long(L"BmsTempDeciK", tsens_get_temp_deci_c() + 2730);
                ULONG hi_k10 = registry_query_u_long(L"BmsHiDeciK", temp_k10 + 50);
                ULONG lo_k10 = registry_query_u_long(L"BmsLoDeciK", temp_k10 - 50);
                ULONG flags = registry_query_u_long(L"BmsFlags", 0);

                out[0] = 1;
                out[1] = temp_k10;
                out[2] = hi_k10;
                out[3] = (lo_k10 & 0xFFFF) | (flags << 16);

                info = 0x10;
                status = STATUS_SUCCESS;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 0x10;
            }
            break;
        }

        case IOCTL_PEP_DEEPSTANDBY_MODEM_SET: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 4, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                deep_standby_set_modem_req(*in);
                status = STATUS_SUCCESS;
                info = 0;
            }
            break;
        }

        case IOCTL_PEP_PPP_GET_RPMH_COMMAND_SET: {

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 0x68, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                UCHAR* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 0x60, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    RtlZeroMemory(out, out_sz);

                    WDFDEVICE dev = WdfIoQueueGetDevice(queue);
                    p_dev_ctx_t ctx = get_dev_ctx(dev);

                    ((ULONG*)out)[0] = ctx ? ctx->port_id : 0;
                    ((ULONG*)out)[1] = registry_query_u_long(L"PppEnable", 1);
                    ((ULONG*)out)[2] = 0;
                    ((ULONG*)out)[3] = 1;

                    size_t copy = (in_sz < 0x20) ? in_sz : 0x20;
                    if (copy && out_sz > 0x10)
                        RtlCopyMemory(out + 0x10, in, copy);

                    info = 0x60;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 0x60;
                }
            }
            break;
        }

        case IOCTL_PEP_PPP_RESOURCE_VOTE: {

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 0x60, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {

                if (in_sz >= 20) {
                    ULONG set_type = *(ULONG*)(in + 16);
                    if (set_type < 3u || set_type > 4u) {
                        status = STATUS_INVALID_PARAMETER_2;
                        info = 0;
                        break;
                    }
                }
                if (in_sz >= 12) {
                    ULONG res_id = *(ULONG*)(in + 8);
                    if (res_id > 0x16Du) {
                        status = STATUS_INVALID_PARAMETER;
                        info = 0;
                        break;
                    }
                }

                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONGLONG voter_id = 0;
                    if (in_sz >= sizeof(ULONGLONG))
                        voter_id = *(ULONGLONG*)(in + 0);
                    else if (in_sz >= sizeof(ULONG))
                        voter_id = *(ULONG*)(in + 0);
                    ULONG res_id = (in_sz >= 12) ? *(ULONG*)(in + 8) : 0;
                    ULONG set_type = (in_sz >= 20) ? *(ULONG*)(in + 16) : 0;
                    ULONG drv_id = (in_sz >= 0x60) ? *(ULONG*)(in + 0x5C) : 0;

                    *out = 0;
                    info = 4;
                    status = STATUS_SUCCESS;

                    UNREFERENCED_PARAMETER(set_type);
                    ppp_vote_record(voter_id, res_id, 0u, drv_id);
                    ppp_vote_record(voter_id, res_id, 1u, drv_id);
                    (VOID) rsc_sw_apply_ppp_policy_vote(res_id, set_type, drv_id, voter_id, FALSE,
                                                        io_control_code, status);
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 4;
                }

                if (in_sz >= 16) {
                    ULONG a = *(ULONG*)&in[0];
                    ULONG b = *(ULONG*)&in[4];
                    ULONG c = *(ULONG*)&in[8];
                    ULONG d = *(ULONG*)&in[12];
                    res_key_t k = {0x50505030u, a};
                    res_value_t v = {b, c, d, (ULONG)in_sz};
                    (VOID) res_set(k, v);
                }
            }
            break;
        }

        case IOCTL_PEP_PPP_GET_RESOURCE_SETTINGS: {

            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 4, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                if (*in > 0x16Du) {
                    status = STATUS_INVALID_PARAMETER;
                    info = 0;
                    break;
                }

                UCHAR* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 0x60, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    RtlZeroMemory(out, out_sz);

                    ULONG port = 0;
                    WDFDEVICE dev = WdfIoQueueGetDevice(queue);
                    p_dev_ctx_t ctx = get_dev_ctx(dev);
                    if (ctx)
                        port = ctx->port_id;

                    ((ULONG*)out)[0] = port;
                    ((ULONG*)out)[1] = *in;
                    ((ULONG*)out)[2] = registry_query_u_long(L"PppEnable", 1);
                    ((ULONG*)out)[3] = 1;

                    ULONGLONG voter = 0;
                    ULONG set_mask = 0;
                    ULONG drv_id = 0;
                    if (ppp_vote_query_res(*in, &voter, &set_mask, &drv_id)) {
                        ((ULONG*)out)[4] = set_mask;
                        ((ULONG*)out)[5] = drv_id;
                        ((ULONG*)out)[6] = (ULONG)(voter & 0xFFFFFFFFu);
                        ((ULONG*)out)[7] = (ULONG)(voter >> 32);
                    }

                    info = 0x60;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 0x60;
                }
            }
            break;
        }

        case IOCTL_PEP_PPP_RESOURCE_ACTION: {

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 0x60, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONGLONG voter_id = 0;
                if (in_sz >= sizeof(ULONGLONG))
                    voter_id = *(ULONGLONG*)(in + 0);
                else if (in_sz >= sizeof(ULONG))
                    voter_id = *(ULONG*)(in + 0);
                ULONG res_id = (in_sz >= 12) ? *(ULONG*)(in + 8) : 0;
                ULONG drv_id = (in_sz >= 0x60) ? *(ULONG*)(in + 0x5C) : 0;

                if (in_sz >= 12) {
                    if (res_id > 0x16Du) {
                        status = STATUS_INVALID_PARAMETER;
                        info = 0;
                        break;
                    }
                }

                if (!ppp_vote_remove(voter_id, res_id, drv_id)) {
                    status = STATUS_UNSUCCESSFUL;
                    info = 0;
                    break;
                }

                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    UNREFERENCED_PARAMETER(in);
                    *out = 0;
                    info = 4;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    if (output_buf_length == 0) {

                        status = STATUS_SUCCESS;
                        info = 0;
                    } else {
                        info = 4;
                    }
                }

                (VOID) rsc_sw_apply_ppp_policy_vote(res_id, 0u, drv_id, voter_id, TRUE,
                                                    io_control_code, status);
            }
            break;
        }

        case IOCTL_PMIC_PUBLIC_USB_OTG_CONTROL:
        case IOCTL_PMIC_PUBLIC_USB_OTG_ENABLE:
        case IOCTL_PM_3P_PUBLIC_USB_OTG_ENABLE:
        case IOCTL_PM_WLED_MODULE_ENABLE:
        case IOCTL_PM_IBBLAB_MODULE_ENABLE: {
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }

            ULONG req_in = 4u;
            if (io_control_code == IOCTL_PM_WLED_MODULE_ENABLE)
                req_in = 8u;
            else if (io_control_code == IOCTL_PM_IBBLAB_MODULE_ENABLE)
                req_in = 12u;

            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, req_in, (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            ULONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
            if (!NT_SUCCESS(status)) {
                if (status == STATUS_BUFFER_TOO_SMALL)
                    info = 4;
                break;
            }

            res_key_t k;
            res_value_t v;
            k.res_id = 0x304D4350u;
            k.sub_type = io_control_code;
            v.value0 = (in_sz >= sizeof(ULONG)) ? in[0] : 0;
            v.value1 = (in_sz >= sizeof(ULONG) * 2) ? in[1] : 0;
            v.value2 = (in_sz >= sizeof(ULONG) * 3) ? in[2] : 0;
            v.value3 = 0;
            (VOID) res_set(k, v);

            *out = 0;
            status = STATUS_SUCCESS;
            info = 4;
            break;
        }

        case IOCTL_PEP_GPIO_CONFIG:
        case IOCTL_PEP_GPIO_REQUEST: {
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }

            ULONG req_in = (io_control_code == IOCTL_PEP_GPIO_CONFIG) ? 28u : 12u;
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, req_in, (PVOID*)&in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            res_key_t k;
            res_value_t v;

            k.res_id = (io_control_code == IOCTL_PEP_GPIO_CONFIG) ? 0x30434750u : 0x30524750u;
            k.sub_type = (in_sz >= sizeof(ULONG)) ? in[0] : 0;

            v.value0 = (in_sz >= sizeof(ULONG) * 2) ? in[1] : 0;
            v.value1 = (in_sz >= sizeof(ULONG) * 3) ? in[2] : 0;
            v.value2 = (in_sz >= sizeof(ULONG) * 4) ? in[3] : 0;
            v.value3 = (ULONG)io_control_code;
            (VOID) res_set(k, v);

            status = STATUS_SUCCESS;
            info = 0;
            break;
        }

        case IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT: {
            /* qualcomm's actual PMIC GPIO provider owns these pins. Never claim success just
             * because the request was stored in the resource cache. */

            PVOID raw = NULL;
            size_t raw_bytes = 0;
            PVOID result_buf = NULL;
            size_t result_bytes = 0;
            pm_gpio_digital_output_t decoded;
            ULONG returned = 0;
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }
            status = WdfRequestRetrieveInputBuffer(req_arg, PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES,
                                                   &raw, &raw_bytes);
            if (!NT_SUCCESS(status)) {
                info = 0;
                break;
            }
            if (raw_bytes != PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES ||
                !pm_gpio_decode_digital_output(raw, raw_bytes, &decoded)) {
                status = STATUS_INVALID_BUFFER_SIZE;
                info = 0;
                break;
            }
            status =
                WdfRequestRetrieveOutputBuffer(req_arg, sizeof(ULONG), &result_buf, &result_bytes);
            if (!NT_SUCCESS(status)) {
                info = 0;
                break;
            }
            if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
                status = STATUS_NOT_SUPPORTED;
                info = 0;
                break;
            }
            status =
                pmic_gpio_send(device, io_control_code, raw, PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES,
                               result_buf, sizeof(ULONG), &returned);
            if (!NT_SUCCESS(status) || returned != sizeof(ULONG)) {
                if (NT_SUCCESS(status))
                    status = STATUS_DEVICE_PROTOCOL_ERROR;
                info = 0;
                break;
            }

            if (*(ULONG*)result_buf != 0u) {
                status = STATUS_DEVICE_PROTOCOL_ERROR;
                info = 0;
                break;
            }
            info = sizeof(ULONG);
            break;
        }

        case IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE:
        case IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT:
        case IOCTL_PEP_GPIO_PMIC_SET_PULL:
        case IOCTL_PEP_GPIO_PMIC_SET_DRIVE:
        case IOCTL_PM_GPIO_GET_GPIO_STATE:
        case IOCTL_PM_GPIO_SET_GPIO_OUTPUT: {
            /* forward the SC8280 0x8002 GPIO family to the PMIC provider and preserve its result.
             */

            ULONG req_in = (io_control_code == IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT) ? 20u : 12u;
            ULONG req_out = (io_control_code == IOCTL_PM_GPIO_GET_GPIO_STATE) ? 8u : 4u;
            PVOID input = NULL, output = NULL;
            size_t input_size = 0, output_size = 0;
            ULONG bytes = 0;
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }
            status = WdfRequestRetrieveInputBuffer(req_arg, req_in, &input, &input_size);
            if (!NT_SUCCESS(status)) {
                info = 0;
                break;
            }
            if (input_size != req_in) {
                status = STATUS_INVALID_BUFFER_SIZE;
                info = 0;
                break;
            }
            status = WdfRequestRetrieveOutputBuffer(req_arg, req_out, &output, &output_size);
            if (!NT_SUCCESS(status)) {
                info = 0;
                break;
            }
            if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
                status = STATUS_NOT_SUPPORTED;
                info = 0;
                break;
            }
            if (io_control_code == IOCTL_PM_GPIO_GET_GPIO_STATE) {
                pm_gpio_state_query_t query;
                pm_gpio_state_raw_t raw;
                if (!pm_gpio_decode_state_query(input, input_size, &query)) {
                    status = STATUS_INVALID_BUFFER_SIZE;
                    info = 0;
                    break;
                }
                /* the provider reply is eight opaque bytes; it cannot supply the complete 28-byte
                 * PMIC output configuration for D0/D3. */

                status = pmic_gpio_query_raw_state(device, &query, &raw);
                if (!NT_SUCCESS(status)) {
                    info = 0;
                    break;
                }
                RtlCopyMemory(output, raw.bytes, sizeof(raw.bytes));
                info = sizeof(raw.bytes);
                break;
            }
            status =
                pmic_gpio_send(device, io_control_code, input, req_in, output, req_out, &bytes);
            if (!NT_SUCCESS(status) || bytes != req_out) {
                if (NT_SUCCESS(status))
                    status = STATUS_DEVICE_PROTOCOL_ERROR;
                info = 0;
                break;
            }
            if (io_control_code != IOCTL_PM_GPIO_GET_GPIO_STATE && *(ULONG*)output != 0u) {
                status = STATUS_DEVICE_PROTOCOL_ERROR;
                info = 0;
                break;
            }
            info = req_out;
            break;
        }

        case IOCTL_PM_MPP_CONFIG_DIGITAL_INPUT:
        case IOCTL_PM_MPP_CONFIG_DIGITAL_OUTPUT:
        case IOCTL_PM_MPP_CONFIG_DIGITAL_INOUT:
        case IOCTL_PM_MPP_CONFIG_ANALOG_INPUT:
        case IOCTL_PM_MPP_CONFIG_ANALOG_OUTPUT:
        case IOCTL_PM_MPP_CONFIG_I_SINK:
        case IOCTL_PM_MPP_CONFIG_ATEST:
        case IOCTL_PM_MPP_SET_OUTPUT_STATE: {
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }

            ULONG req_in = 12;
            switch (io_control_code) {
                case IOCTL_PM_MPP_CONFIG_DIGITAL_INPUT:
                    req_in = 20;
                    break;
                case IOCTL_PM_MPP_CONFIG_DIGITAL_INOUT:
                    req_in = 16;
                    break;
                case IOCTL_PM_MPP_CONFIG_ANALOG_OUTPUT:
                    req_in = 16;
                    break;
                case IOCTL_PM_MPP_CONFIG_I_SINK:
                    req_in = 16;
                    break;
                default:
                    req_in = 12;
                    break;
            }

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, req_in, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONG id = (in_sz >= 4) ? *(ULONG*)in : 0;
                    ULONG a = (in_sz >= 8) ? *(ULONG*)(in + 4) : 0;
                    ULONG b = (in_sz >= 12) ? *(ULONG*)(in + 8) : 0;
                    ULONG c = (in_sz >= 16) ? *(ULONG*)(in + 12) : 0;

                    res_key_t k = {0x4D505030u, id};
                    res_value_t v = {a, b, c, io_control_code};
                    (VOID) res_set(k, v);

                    *out = 0;
                    info = 4;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 4;
                }
            }
            break;
        }

        case IOCTL_PM_MPP_GET_INPUT_STATE: {
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 12, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONGLONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 8, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONG id = (in_sz >= 4) ? *(ULONG*)in : 0;
                    ULONG st = registry_query_u_long(L"MppInputState", 0) & 1u;
                    *out = ((ULONGLONG)id << 32) | (ULONGLONG)st;

                    info = 8;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 8;
                }
            }
            break;
        }

        case IOCTL_PM_LPG_PATTERN_CONFIG:
        case IOCTL_PM_LPG_PWM_SIZE:
        case IOCTL_PM_LPG_PWM_CLK:
        case IOCTL_PM_LPG_LUT_CFG:
        case IOCTL_PM_LPG_PWM_ENABLE:
        case IOCTL_PM_LPG_PWM_SET:
        case IOCTL_PM_LPG_GET_STATUS:
        case IOCTL_PM_LPG_SET_RAMP:
        case IOCTL_PM_LPG_SET_PAUSE:
        case IOCTL_PM_LPG_SET_LUT:
        case IOCTL_PM_LPG_SET_DTEST:
        case IOCTL_PM_LPG_ATEST: {
            if (!registry_query_u_long(L"PmicIoctlsEnable", 1)) {
                status = STATUS_INVALID_DEVICE_STATE;
                info = 0;
                break;
            }

            ULONG req_in = 12;
            switch (io_control_code) {
                case IOCTL_PM_LPG_PATTERN_CONFIG:
                    req_in = 28;
                    break;
                case IOCTL_PM_LPG_PWM_SIZE:
                    req_in = 16;
                    break;
                case IOCTL_PM_LPG_PWM_CLK:
                    req_in = 24;
                    break;
                case IOCTL_PM_LPG_LUT_CFG:
                    req_in = 28;
                    break;
                case IOCTL_PM_LPG_PWM_ENABLE:
                    req_in = 16;
                    break;
                case IOCTL_PM_LPG_PWM_SET:
                    req_in = 36;
                    break;
                case IOCTL_PM_LPG_GET_STATUS:
                    req_in = 12;
                    break;
                case IOCTL_PM_LPG_SET_RAMP:
                    req_in = 12;
                    break;
                case IOCTL_PM_LPG_SET_PAUSE:
                    req_in = 12;
                    break;
                case IOCTL_PM_LPG_SET_LUT:
                    req_in = 16;
                    break;
                case IOCTL_PM_LPG_SET_DTEST:
                    req_in = 24;
                    break;
                case IOCTL_PM_LPG_ATEST:
                    req_in = 8;
                    break;
                default:
                    req_in = 12;
                    break;
            }

            UCHAR* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, req_in, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONG* out = NULL;
                size_t out_sz = 0;
                status = WdfRequestRetrieveOutputBuffer(req_arg, 4, (PVOID*)&out, &out_sz);
                if (NT_SUCCESS(status)) {
                    ULONG id = (in_sz >= 4) ? *(ULONG*)in : 0;
                    ULONG a = (in_sz >= 8) ? *(ULONG*)(in + 4) : 0;
                    ULONG b = (in_sz >= 12) ? *(ULONG*)(in + 8) : 0;
                    ULONG c = (in_sz >= 16) ? *(ULONG*)(in + 12) : 0;

                    res_key_t k = {0x4C504730u, id};
                    res_value_t v = {a, b, c, io_control_code};
                    (VOID) res_set(k, v);

                    if (io_control_code == IOCTL_PM_LPG_GET_STATUS)
                        *out = registry_query_u_long(L"LpgStatus", 0);
                    else
                        *out = 0;

                    info = 4;
                    status = STATUS_SUCCESS;
                } else if (status == STATUS_BUFFER_TOO_SMALL) {
                    info = 4;
                }
            }
            break;
        }

        case IOCTL_PEP_SUBSYS_ADSP_STATE: {

            ULONG* in = NULL;
            size_t in_sz = 0;
            exsoc_parsed_t ex;
            BOOLEAN ex_valid = FALSE;
            ULONG required = 0;
            RtlZeroMemory(&ex, sizeof(ex));

            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG), (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status) && in) {
                required = in[0];
                status = parse_ex_soc_payload((const UCHAR*)in, in_sz, 0, &ex);
                if (!NT_SUCCESS(status)) {
                    status = STATUS_UNSUCCESSFUL;
                    info = 0;
                } else {
                    ex_valid = TRUE;

                    status = pep_apply_ex_soc_d_state(ex.dev_id, ex.d_state);
                    if (NT_SUCCESS(status)) {
                        PVOID out = NULL;
                        size_t out_sz = 0;
                        status = WdfRequestRetrieveOutputBuffer(req_arg, required, &out, &out_sz);
                        if (NT_SUCCESS(status) && out && out_sz >= required)
                            info = required;
                        else
                            info = 0;

                        if (NT_SUCCESS(status)) {
                            res_key_t k0 = {0x30535845u, ex.dev_hash};
                            res_value_t v0 = {(ULONG)ex.d_state, ex.dev_id_len, ex.dev_id_word0,
                                              ex.dev_id_word1};
                            (VOID) res_set(k0, v0);

                            res_key_t k1 = {0x4F535845u, io_control_code};
                            res_value_t v1 = {ex.dev_hash, (ULONG)ex.d_state, required, 0};
                            (VOID) res_set(k1, v1);
                        }
                    } else {
                        info = 0;
                    }
                }
            }

            ex_soc_record_apply(io_control_code, ex_valid ? ex.dev_id : "",
                                ex_valid ? ex.dev_hash : 0, ex_valid ? (ULONG)ex.d_state : 0,
                                status, required, (ULONG)((info > MAXULONG) ? MAXULONG : info));
            rsc_sw_set_node_state(RSC_SW_SUBSYSTEM_AOP, ex_valid ? (ULONG)ex.d_state : 0, required,
                                  ex_valid ? ex.dev_hash : 0);
            rsc_sw_record_event(RSC_SW_SUBSYSTEM_AOP, RSC_SW_EVENT_TRANSITION, status,
                                io_control_code, ex_valid ? ex.dev_hash : 0,
                                ex_valid ? (ULONG)ex.d_state : 0, required,
                                (ULONG)((info > MAXULONG) ? MAXULONG : info),
                                ex_valid ? ex.dev_id_word0 : 0, ex_valid ? ex.dev_id_word1 : 0);
            break;
        }

        case IOCTL_PEP_SUBSYS_ADSP_CTRL: {

            ULONG* in = NULL;
            size_t in_sz = 0;
            exsoc_parsed_t ex;
            BOOLEAN ex_valid = FALSE;
            RtlZeroMemory(&ex, sizeof(ex));
            status = WdfRequestRetrieveInputBuffer(req_arg, sizeof(ULONG) * 2, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status) && in) {
                status = parse_ex_soc_payload((const UCHAR*)in, in_sz, 0, &ex);
                if (!NT_SUCCESS(status)) {
                    status = STATUS_UNSUCCESSFUL;
                    info = 0;
                } else {
                    ex_valid = TRUE;

                    status = pep_apply_ex_soc_d_state(ex.dev_id, ex.d_state);
                    if (NT_SUCCESS(status)) {
                        res_key_t k0 = {0x30535845u, ex.dev_hash};
                        res_value_t v0 = {(ULONG)ex.d_state, ex.dev_id_len, ex.dev_id_word0,
                                          ex.dev_id_word1};
                        (VOID) res_set(k0, v0);

                        res_key_t k1 = {0x4F535845u, io_control_code};
                        res_value_t v1 = {ex.dev_hash, (ULONG)ex.d_state, ex.dev_id_word0,
                                          ex.dev_id_word1};
                        (VOID) res_set(k1, v1);

                        status = STATUS_SUCCESS;
                        info = 0;
                    } else {
                        info = 0;
                    }
                }
            }

            ex_soc_record_apply(io_control_code, ex_valid ? ex.dev_id : "",
                                ex_valid ? ex.dev_hash : 0, ex_valid ? (ULONG)ex.d_state : 0,
                                status, 0, (ULONG)((info > MAXULONG) ? MAXULONG : info));
            rsc_sw_set_node_state(RSC_SW_SUBSYSTEM_GLINK, ex_valid ? (ULONG)ex.d_state : 0, 0,
                                  ex_valid ? ex.dev_hash : 0);
            rsc_sw_record_event(RSC_SW_SUBSYSTEM_GLINK, RSC_SW_EVENT_TRANSITION, status,
                                io_control_code, ex_valid ? ex.dev_hash : 0,
                                ex_valid ? (ULONG)ex.d_state : 0, ex_valid ? ex.dev_id_word0 : 0,
                                ex_valid ? ex.dev_id_word1 : 0,
                                (ULONG)((info > MAXULONG) ? MAXULONG : info), 0);
            break;
        }

        case IOCTL_PEP_SUBSYS_CDSP_STATE: {

            status = ppp_test_check_registry();
            if (!NT_SUCCESS(status)) {
                info = 0;
                break;
            }

            PVOID in = NULL;
            size_t in_sz = 0;
            if (input_buf_length != 0) {
                status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
                if (!NT_SUCCESS(status)) {
                    info = 0;
                    break;
                }
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            if (output_buf_length != 0) {
                status = WdfRequestRetrieveOutputBuffer(req_arg, output_buf_length, &out, &out_sz);
                if (!NT_SUCCESS(status)) {
                    info = 0;
                    break;
                }
            }

            if (!in || !out) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            ULONG rid = 0;
            if (in_sz >= sizeof(ULONG))
                rid = *(ULONG*)in;

            size_t written = 0;
            status = build_ppp_res_settings_for_test(device, rid, out, out_sz, &written);
            info = NT_SUCCESS(status) ? written : 0;
            rsc_sw_set_node_state(RSC_SW_SUBSYSTEM_CMD_DB, NT_SUCCESS(status) ? 1u : 0u,
                                  (ULONG)written, rid);
            rsc_sw_record_event(RSC_SW_SUBSYSTEM_CMD_DB, RSC_SW_EVENT_QUERY, status,
                                io_control_code, rid, (ULONG)in_sz, (ULONG)out_sz, (ULONG)written,
                                0, 0);
            break;
        }

        case IOCTL_PEP_VENDOR_88882000: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            PVOID inb = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &inb, &in_sz);
            if (!NT_SUCCESS(status) || !inb) {
                info = 0;
                break;
            }

            PVOID buf = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 120, &buf, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 120) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 120;
                    break;
                }

                LONG req = 0;
                if (in_sz >= sizeof(LONG))
                    req = *(LONG*)inb;

                xo_test_handle_req(req);

                *(LONG*)buf = req;

                status = build_pep_stats(ctx, buf, 120);
                info = 120;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 120;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_8888204C: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 320, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 320) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 320;
                    break;
                }
                RtlZeroMemory(out, 320);
                status = build_subsystem_status(ctx, out, 320);
                info = 320;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 320;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882058: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 3844, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 3844) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 3844;
                    break;
                }
                RtlZeroMemory(out, 3844);
                status = build_xo_clock_status(ctx, out, 3844);
                info = 3844;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 3844;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_8888205C: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            ULONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 16, (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 16) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 16;
                    break;
                }
                RtlZeroMemory(out, 16);

                chipinfo_t ci = chip_info_get();

                out[0] = ci.chip_id;
                out[1] = ci.chip_version;
                out[2] = ci.chip_family;
                out[3] = 0;

                info = 16;
                status = STATUS_SUCCESS;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 16;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882080: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 15376, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 15376) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 15376;
                    break;
                }
                RtlZeroMemory(out, 15376);
                status = build_dev_info(ctx, out, 15376);
                info = 15376;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 15376;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882084: /* pwcal rundown: 51212 bytes. */
        {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 51212, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                if (out_sz < 51212) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    info = 51212;
                    break;
                }
                RtlZeroMemory(out, 51212);
                status = build_pw_cal_rundown(ctx, out, 51212);
                info = 51212;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 51212;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882088: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }
            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            PVOID out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 16, &out, &out_sz);
            if (NT_SUCCESS(status)) {
                ULONG written = 0;
                status = build_clock_debug_list(ctx, out, (ULONG)out_sz, &written);
                UNREFERENCED_PARAMETER(written);
                info = (status == STATUS_INVALID_PARAMETER) ? input_buf_length : 83984;
            } else if (status == STATUS_BUFFER_TOO_SMALL) {
                info = 83984;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_8888208C:
        case IOCTL_PEP_VENDOR_88884084: {
            /* processor idle diagnostic IOCTL: METHOD_BUFFERED in/out using the same system buf. */
            PVOID buf = NULL;
            size_t in_sz = 0;

            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &buf, &in_sz);
            if (NT_SUCCESS(status) && buf) {

                status = ppm_test_handle(buf, in_sz);
                if (NT_SUCCESS(status))
                    info = input_buf_length;
                else if (status == STATUS_BUFFER_TOO_SMALL)
                    info = 0;
                else
                    info = 0;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882094: {
            if (input_buf_length == 0 || output_buf_length == 0) {
                status = STATUS_INVALID_PARAMETER;
                info = 0;
                break;
            }

            PVOID in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, input_buf_length, &in, &in_sz);
            if (!NT_SUCCESS(status) || !in) {
                info = 0;
                break;
            }

            ULONGLONG* out = NULL;
            size_t out_sz = 0;
            status = WdfRequestRetrieveOutputBuffer(req_arg, 0, (PVOID*)&out, &out_sz);
            if (NT_SUCCESS(status)) {
                ULONGLONG ticks = KeQueryInterruptTime();
                if (out && out_sz != 0) {
                    size_t copy = (out_sz < sizeof(ticks)) ? out_sz : sizeof(ticks);
                    RtlCopyMemory(out, &ticks, copy);
                }
                info = input_buf_length;
                status = STATUS_SUCCESS;
            }
            break;
        }

        case IOCTL_PEP_VENDOR_88882098:

            status = STATUS_NOT_SUPPORTED;
            info = 0;
            break;

        case IOCTL_PEP_VENDOR_8888209C: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 8, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                ULONG shadow_in[4];
                ULONG compat_out[4];
                RtlZeroMemory(shadow_in, sizeof(shadow_in));
                RtlZeroMemory(compat_out, sizeof(compat_out));
                if (in && in_sz) {
                    size_t copy = (in_sz < sizeof(shadow_in)) ? in_sz : sizeof(shadow_in);
                    RtlCopyMemory(shadow_in, in, copy);
                }

                size_t written = 0;
                status = bcl_read_set_threshold(device, shadow_in, sizeof(shadow_in), compat_out,
                                                sizeof(compat_out), &written);
                if (NT_SUCCESS(status)) {
                    ULONG result_sub_type = shadow_in[2];
                    if (written >= sizeof(compat_out))
                        result_sub_type = compat_out[2];

                    ULONG* out = NULL;
                    size_t out_sz = 0;
                    status = WdfRequestRetrieveOutputBuffer(req_arg, 0, (PVOID*)&out, &out_sz);
                    if (out) {
                        out[0] = shadow_in[0];
                        out[1] = result_sub_type;
                    }
                    info = 16;
                }
            }
            break;
        }

        case IOCTL_PEP_VENDOR_888820A4: {
            ULONG* in = NULL;
            size_t in_sz = 0;
            status = WdfRequestRetrieveInputBuffer(req_arg, 8, (PVOID*)&in, &in_sz);
            if (NT_SUCCESS(status)) {
                status = bcl_submit_test(*in);
                info = 0;
            }
            break;
        }

        default:
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_IO,
                        "ioctl: unsupported code=%08X input=%Iu output=%Iu", io_control_code,
                        input_buf_length, output_buf_length);

            PVOID inb = NULL;
            size_t inb_sz = 0;
            if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(req_arg, 0, &inb, &inb_sz)))
                diag_record(io_control_code, (ULONG)input_buf_length, (ULONG)output_buf_length,
                            STATUS_INVALID_DEVICE_REQUEST, 0, inb, inb_sz);
            else
                diag_record(io_control_code, (ULONG)input_buf_length, (ULONG)output_buf_length,
                            STATUS_INVALID_DEVICE_REQUEST, 0, NULL, 0);

            status = STATUS_INVALID_DEVICE_REQUEST;
            info = 0;
            break;
    }

done: {
    ULONG sub = rsc_sw_subsystem_for_ioctl(io_control_code);
    if (sub < RSC_SW_SUBSYSTEM_MAX) {
        ULONG in_len = (input_buf_length > MAXULONG) ? MAXULONG : (ULONG)input_buf_length;
        ULONG out_len = (output_buf_length > MAXULONG) ? MAXULONG : (ULONG)output_buf_length;
        ULONG ret_len = (info > MAXULONG) ? MAXULONG : (ULONG)info;
        ULONG flags = NT_SUCCESS(status) ? 1u : 0u;

        rsc_sw_record_event(sub, RSC_SW_EVENT_IOCTL, status, io_control_code, ret_len, in_len,
                            out_len, ret_len, (ULONG)status, flags);
    }
}
    ioctl_complete(req_arg, io_control_code, status, (ULONG)input_buf_length, info);
}
