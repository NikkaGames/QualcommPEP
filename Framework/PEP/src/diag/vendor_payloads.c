#include "vendor_payloads.h"
#include "stats.h"
#include "resource_cache.h"
#include "soc_subsystem.h"
#include "deep_standby.h"
#include "thermal_request.h"
#include "tsens.h"
#include "mmrm.h"
#include "bcl.h"
#include "interrupt.h"
#include "dcvs.h"
#include "crash_dump.h"
#include "resource_diag.h"
#include <ntstrsafe.h>
#include <stdarg.h>
#include <string.h>

#pragma pack(push, 1)

typedef struct state_result_st {
    int pep_status;
    int status;
    unsigned int idx;
} state_result_st_t;

typedef struct sleep_stats_type {
    unsigned int stat_type;
    unsigned int cnt;
    unsigned __int64 last_entered_at;
    unsigned __int64 last_exited_at;
    unsigned __int64 accumulated_duration;
} sleep_stats_type_t;

typedef struct pep_stats_result_st1 {
    state_result_st_t base;
    unsigned int debugger_info;
    unsigned __int8 xo_clock_logging_enable;
    unsigned __int8 pad[3];
    sleep_stats_type_t stats_blob[3];
} pep_stats_result_st1_t;

typedef struct sys_master_stats_t {
    unsigned int sys_sleep_version;
    unsigned int sys_sleep_cnt;
    unsigned __int64 sys_sleep_entered_at;
    unsigned __int64 sys_sleep_exited_at;
    unsigned __int64 sys_sleep_accumulated_duration;
} sys_master_stats_t_t;

typedef struct pep_stats_dev_type {
    char dev_name[32];
    int current_d_state;
    int current_f_state;
    int current_p_state[10];
} pep_stats_dev_type_t;

typedef struct pep_dev_info {
    state_result_st_t base;
    unsigned __int8 num_of_dev;
    unsigned __int8 pad[3];
    pep_stats_dev_type_t dev_info[192];
} pep_dev_info_t;

typedef struct pep_stats_each_clock_type {
    pep_stats_dev_type_t base;
    char clock_name[32];
    unsigned int pep_clock_value;
    unsigned int hw_clock_value;
} pep_stats_each_clock_type_t;

typedef struct pep_stats_clock_st {
    unsigned int num_clocks;
    pep_stats_each_clock_type_t each_clock[32];
} pep_stats_clock_st_t;

typedef struct xo_blocking_clocks {
    pep_stats_clock_st_t clock_data;
} xo_blocking_clocks_t;

#pragma pack(pop)

#pragma pack(push, 1)

typedef struct clockdbg_entry {
    unsigned __int8 unk0[12];
    char clock_name[128];
    unsigned __int8 unk140;
    unsigned __int8 b_on;
    unsigned __int8 unk142[2];
    int l_hz;
} clockdbg_entry_t;

typedef struct clockdbg_list {
    unsigned int dword0;
    unsigned int status;
    unsigned int dword2;
    unsigned int cnt;
    clockdbg_entry_t entries[512];
    unsigned __int8 scratch[8192];
} clockdbg_list_t;

#pragma pack(pop)

#pragma pack(push, 1)

typedef struct res_snapshot_hdr {
    ULONG magic;
    ULONG version;
    ULONG cnt;
    ULONG entry_size;
} res_snapshot_hdr_t;

typedef struct res_snapshot_entry {
    ULONG res_id;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
} res_snapshot_entry_t;

#pragma pack(pop)

static VOID init_state_result(_Out_ state_result_st_t* s)
{
    s->pep_status = 0;
    s->status = 0;
    s->idx = 0;
}

static unsigned __int64 now_ticks(void)
{
    return KeQueryInterruptTime();
}

static CHAR printable_ascii(_In_ UCHAR ch)
{
    if (ch >= 0x20 && ch <= 0x7E)
        return (CHAR)ch;
    return '_';
}

static VOID format_res_name_a(_In_ ULONG res_id, _In_ ULONG sub_type,
                              _Out_writes_z_(name_cch) CHAR* name, _In_ size_t name_cch)
{
    CHAR tag[5];
    tag[0] = printable_ascii((UCHAR)(res_id & 0xFF));
    tag[1] = printable_ascii((UCHAR)((res_id >> 8) & 0xFF));
    tag[2] = printable_ascii((UCHAR)((res_id >> 16) & 0xFF));
    tag[3] = printable_ascii((UCHAR)((res_id >> 24) & 0xFF));
    tag[4] = '\0';

    (VOID) RtlStringCchPrintfA(name, name_cch, "%s_%04X", tag, (unsigned)(sub_type & 0xFFFFu));
}

static ULONG snapshot_cnt_for_buf(_In_ const res_snapshot_hdr_t* hdr, _In_ size_t bytes,
                                  _In_ ULONG max_cnt_arg)
{
    if (!hdr)
        return 0;

    if (bytes < sizeof(*hdr))
        return 0;

    if (hdr->magic != 0x50515152u || hdr->entry_size != sizeof(res_snapshot_entry_t))
        return 0;

    ULONG cnt = hdr->cnt;
    size_t max_cnt = (bytes - sizeof(*hdr)) / sizeof(res_snapshot_entry_t);
    if ((size_t)cnt > max_cnt)
        cnt = (ULONG)max_cnt;
    if (cnt > max_cnt_arg)
        cnt = max_cnt_arg;
    return cnt;
}

static VOID append_fmt_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                         _Inout_ size_t* offset, _In_z_ _Printf_format_string_ PCSTR fmt, ...)
{
    if (!buf || !offset || !fmt || *offset >= capacity)
        return;

    size_t remain = capacity - *offset;
    va_list args;
    va_start(args, fmt);
    NTSTATUS st = RtlStringCbVPrintfA(buf + *offset, remain, fmt, args);
    va_end(args);

    if (!NT_SUCCESS(st)) {
        buf[capacity - 1] = '\0';
        *offset = capacity - 1;
        return;
    }

    size_t appended = 0;
    if (NT_SUCCESS(RtlStringCbLengthA(buf + *offset, remain, &appended)))
        *offset += appended;
    else
        *offset = capacity - 1;
}

static VOID fill_subsystem_stats_blob(_Inout_ sleep_stats_type_t* out, _In_ ULONG subsystem_idx)
{
    if (!out)
        return;

    RtlZeroMemory(out, sizeof(*out));
    out->stat_type = subsystem_idx;

    soc_subsystem_stats_t st;
    RtlZeroMemory(&st, sizeof(st));
    if (soc_subsys_get_info(subsystem_idx, NULL, NULL, NULL, NULL, &st)) {
        out->cnt = st.wakeup_cnt;
        out->last_entered_at = st.blocked_duration100ns;
        out->last_exited_at = st.sleep_duration100ns;
        out->accumulated_duration = st.blocked_duration100ns + st.sleep_duration100ns;
    }
}

static const CHAR* ds_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case DEEP_STANDBY_EVENT_INIT:
            return "init";
        case DEEP_STANDBY_EVENT_ATTACH:
            return "attach";
        case DEEP_STANDBY_EVENT_DETACH:
            return "detach";
        case DEEP_STANDBY_EVENT_CALLBACK_SCREEN:
            return "cb_screen";
        case DEEP_STANDBY_EVENT_CALLBACK_USER:
            return "cb_user";
        case DEEP_STANDBY_EVENT_CALLBACK_POWER:
            return "cb_power";
        case DEEP_STANDBY_EVENT_CALLBACK_BATTERY:
            return "cb_batt";
        case DEEP_STANDBY_EVENT_EVALUATE:
            return "evaluate";
        case DEEP_STANDBY_EVENT_MS_ON_DC_CHANGE:
            return "ms_on_dc";
        case DEEP_STANDBY_EVENT_TIMER_START:
            return "tmr_start";
        case DEEP_STANDBY_EVENT_TIMER_EXPIRY:
            return "tmr_expiry";
        case DEEP_STANDBY_EVENT_STATE_CHANGE:
            return "state";
        case DEEP_STANDBY_EVENT_WLAN_IOCTL:
            return "wlan_ioctl";
        case DEEP_STANDBY_EVENT_MODEM_IOCTL:
            return "modem_ioctl";
        case DEEP_STANDBY_EVENT_MANUAL_MODEM_REQ:
            return "manual_req";
        default:
            return "unknown";
    }
}

static const CHAR* soc_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case SOC_SUBSYS_EVENT_INIT:
            return "init";
        case SOC_SUBSYS_EVENT_DRIPS_ENTRY:
            return "drips_entry";
        case SOC_SUBSYS_EVENT_DRIPS_EXIT:
            return "drips_exit";
        case SOC_SUBSYS_EVENT_BLOCKED_START:
            return "blocked_start";
        case SOC_SUBSYS_EVENT_BLOCKED_END:
            return "blocked_end";
        case SOC_SUBSYS_EVENT_RESET_ACCOUNTING:
            return "reset";
        default:
            return "unknown";
    }
}

static const CHAR* bcl_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case BCL_EVENT_INIT:
            return "init";
        case BCL_EVENT_DEINIT:
            return "deinit";
        case BCL_EVENT_SUBMIT_ACCEPTED:
            return "submit_ok";
        case BCL_EVENT_SUBMIT_REJECTED:
            return "submit_reject";
        case BCL_EVENT_WORK_ITEM_RUN:
            return "workitem";
        case BCL_EVENT_THRESHOLD_DOWNSTACK:
            return "thz_ds";
        case BCL_EVENT_THRESHOLD_UPDATE:
            return "thz_upd";
        default:
            return "unknown";
    }
}

static const CHAR* mmrm_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case MMRM_EVENT_INIT:
            return "init";
        case MMRM_EVENT_ATTACH_DEV:
            return "attach";
        case MMRM_EVENT_DETACH_DEV:
            return "detach";
        case MMRM_EVENT_POLICY_REQ:
            return "policy";
        case MMRM_EVENT_THERMAL_SAMPLE:
            return "thermal";
        case MMRM_EVENT_THROTTLE_CHANGED:
            return "throttle";
        case MMRM_EVENT_POLICY_WAIT_STATE:
            return "wait";
        case MMRM_EVENT_DSM_POLL:
            return "dsm";
        default:
            return "unknown";
    }
}

static const CHAR* interrupt_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case INTERRUPT_EVENT_INIT:
            return "init";
        case INTERRUPT_EVENT_ENABLE:
            return "enable";
        case INTERRUPT_EVENT_DISABLE:
            return "disable";
        case INTERRUPT_EVENT_ISR:
            return "isr";
        case INTERRUPT_EVENT_DPC_QUEUED:
            return "dpc_q";
        case INTERRUPT_EVENT_DPC_RUN:
            return "dpc";
        case INTERRUPT_EVENT_IDLE_SCAN:
            return "idle_scan";
        case INTERRUPT_EVENT_IDLE_WAKE_SNAPSHOT:
            return "idle_wake";
        case INTERRUPT_EVENT_MAP_LOOKUP_HIT:
            return "map_hit";
        case INTERRUPT_EVENT_MAP_LOOKUP_MISS:
            return "map_miss";
        case INTERRUPT_EVENT_DEINIT:
            return "deinit";
        case INTERRUPT_EVENT_WAKE_OFFSET:
            return "wake_off";
        default:
            return "unknown";
    }
}

static const CHAR* dcvs_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case DCVS_EVENT_INIT:
            return "init";
        case DCVS_EVENT_DEINIT:
            return "deinit";
        case DCVS_EVENT_PERF_SET:
            return "perf_set";
        case DCVS_EVENT_RESET_FEEDBACK:
            return "reset_fb";
        case DCVS_EVENT_POLICY_UPDATE:
            return "policy";
        default:
            return "unknown";
    }
}

static const CHAR* crash_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case CRASH_EVENT_CREATE:
            return "create";
        case CRASH_EVENT_OPEN:
            return "open";
        case CRASH_EVENT_DESTROY:
            return "destroy";
        case CRASH_EVENT_WRITE:
            return "write";
        case CRASH_EVENT_READ:
            return "read";
        case CRASH_EVENT_CAPTURE:
            return "capture";
        case CRASH_EVENT_LIVE_KERNEL_DUMP:
            return "lkd";
        case CRASH_EVENT_FILE_CLOSE:
            return "file_close";
        case CRASH_EVENT_SET_ACTIVE:
            return "set_active";
        case CRASH_EVENT_NOT_SUPPORTED:
            return "notsup";
        default:
            return "unknown";
    }
}

static const CHAR* rsc_sw_event_name(_In_ ULONG event_code)
{
    switch (event_code) {
        case RSC_SW_EVENT_INIT:
            return "init";
        case RSC_SW_EVENT_DEINIT:
            return "deinit";
        case RSC_SW_EVENT_IOCTL:
            return "ioctl";
        case RSC_SW_EVENT_VOTE:
            return "vote";
        case RSC_SW_EVENT_COMMAND:
            return "cmd";
        case RSC_SW_EVENT_QUERY:
            return "query";
        case RSC_SW_EVENT_TRANSITION:
            return "transition";
        case RSC_SW_EVENT_REFRESH:
            return "refresh";
        case RSC_SW_EVENT_TEST:
            return "test";
        default:
            return "unknown";
    }
}

static const CHAR* rsc_sw_scenario_name(_In_ ULONG scenario_id)
{
    switch (scenario_id) {
        case RSC_SW_SCENARIO_AOP:
            return "aop";
        case RSC_SW_SCENARIO_ARC:
            return "arc";
        case RSC_SW_SCENARIO_CLOCK:
            return "clock";
        case RSC_SW_SCENARIO_CMD_DB:
            return "cmddb";
        case RSC_SW_SCENARIO_DAL:
            return "dal";
        case RSC_SW_SCENARIO_GLINK:
            return "glink";
        case RSC_SW_SCENARIO_ICB:
            return "icb";
        case RSC_SW_SCENARIO_NPA:
            return "npa";
        case RSC_SW_SCENARIO_PCU:
            return "pcu";
        case RSC_SW_SCENARIO_PDC:
            return "pdc";
        case RSC_SW_SCENARIO_PPP:
            return "ppp";
        case RSC_SW_SCENARIO_RPMH:
            return "rpmh";
        case RSC_SW_SCENARIO_RSC:
            return "rsc";
        case RSC_SW_SCENARIO_TLMM:
            return "tlmm";
        case RSC_SW_SCENARIO_TSENS:
            return "tsens";
        case RSC_SW_SCENARIO_FULL_SWEEP:
            return "full_sweep";
        default:
            return "unknown";
    }
}

static VOID append_diag_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_DIAG_FLAG_INCLUDE_FULL_SWEEP) ? "full_sweep " : "",
                 (flags & IOCTL_RSC_DIAG_FLAG_TOGGLE_STATE) ? "toggle " : "",
                 (flags & IOCTL_RSC_DIAG_FLAG_VERIFY_QUERY) ? "verify " : "",
                 (flags & IOCTL_RSC_DIAG_FLAG_INCLUDE_ARCMGR) ? "arcmgr " : "",
                 (flags & IOCTL_RSC_DIAG_FLAG_REFRESH_EACH) ? "refresh " : "");
}

static const CHAR* rsc_sw_profile_name(_In_ ULONG profile_id)
{
    switch (profile_id) {
        case 1u:
            return "bringup";
        case 2u:
            return "performance";
        case 3u:
            return "interconnect";
        case 4u:
            return "thermal";
        case 5u:
            return "full_fabric";
        default:
            return "unknown";
    }
}

static VOID append_profile_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                   _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_PROFILE_FLAG_RUN_MATRIX) ? "matrix " : "",
                 (flags & IOCTL_RSC_PROFILE_FLAG_INCLUDE_FULL_SWEEP) ? "full_sweep " : "",
                 (flags & IOCTL_RSC_PROFILE_FLAG_FORCE_VERIFY) ? "verify " : "",
                 (flags & IOCTL_RSC_PROFILE_FLAG_FORCE_TOGGLE) ? "toggle " : "");
}

static VOID append_coverage_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                    _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_COVERAGE_FLAG_REQUIRE_ACTIVE) ? "active " : "",
                 (flags & IOCTL_RSC_COVERAGE_FLAG_REQUIRE_SUCCESS_THRESHOLD) ? "success " : "",
                 (flags & IOCTL_RSC_COVERAGE_FLAG_REQUIRE_RECENT_ACTIVITY) ? "recent " : "",
                 (flags & IOCTL_RSC_COVERAGE_FLAG_INCLUDE_DEPENDENCY_LOAD) ? "deps " : "",
                 (flags & IOCTL_RSC_COVERAGE_FLAG_STRICT_THRESHOLD) ? "strict " : "");
}

static VOID append_remediation_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                       _In_ size_t capacity, _Inout_ size_t* offset,
                                       _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_APPLY_RECOMMENDATIONS) ? "apply " : "",
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_TARGET_INACTIVE) ? "inactive " : "",
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_TARGET_LOW_SUCCESS) ? "success " : "",
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_TARGET_STALE_ACTIVITY) ? "stale " : "",
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_INCLUDE_DEPENDENCY_LOAD) ? "deps " : "",
                 (flags & IOCTL_RSC_REMEDIATION_FLAG_STRICT_THRESHOLD) ? "strict " : "");
}

static VOID append_remediation_reason_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                              _In_ size_t capacity, _Inout_ size_t* offset,
                                              _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "reasons=0x%08X{%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_REMEDIATION_REASON_INACTIVE) ? "inactive " : "",
                 (flags & IOCTL_RSC_REMEDIATION_REASON_LOW_SUCCESS) ? "low_success " : "",
                 (flags & IOCTL_RSC_REMEDIATION_REASON_STALE_ACTIVITY) ? "stale " : "",
                 (flags & IOCTL_RSC_REMEDIATION_REASON_DEPENDENCY_PRESSURE) ? "dep_pressure " : "",
                 (flags & IOCTL_RSC_REMEDIATION_REASON_LAST_STATUS_FAIL) ? "last_fail " : "",
                 (flags & IOCTL_RSC_REMEDIATION_REASON_HIGH_FAILURE_RATE) ? "high_fail " : "");
}

static VOID append_stabilization_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                         _In_ size_t capacity, _Inout_ size_t* offset,
                                         _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_CONTROL_LANE) ? "control " : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_INTERCONNECT_LANE) ? "interconnect "
                                                                                  : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_THERMAL_LANE) ? "thermal " : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_RUN_PROFILES) ? "profiles " : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_EXECUTE_REMEDIATION) ? "remediate " : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_VERIFY_POST) ? "verify " : "",
                 (flags & IOCTL_RSC_STABILIZATION_FLAG_STRICT_COVERAGE) ? "strict " : "");
}

static VOID append_conformance_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                       _In_ size_t capacity, _Inout_ size_t* offset,
                                       _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_ACTIVE) ? "active " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_SUCCESS_THRESHOLD) ? "success " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_RECENT_ACTIVITY) ? "recent " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_DEPENDENCY_SHAPE) ? "dep_shape " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_CATALOG_MATCH) ? "catalog " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_STRICT_THRESHOLD) ? "strict " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_FLAG_INCLUDE_ARCMGR) ? "arcmgr " : "");
}

static VOID append_conformance_reason_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                              _In_ size_t capacity, _Inout_ size_t* offset,
                                              _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "reasons=0x%08X{%s%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_INACTIVE) ? "inactive " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_LOW_SUCCESS) ? "low_success " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_LOW_ACTIVITY) ? "low_activity " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_DEPENDENCY_GAP) ? "dep_gap " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_STATUS_FAIL) ? "status_fail " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_CATALOG_MISMATCH) ? "catalog_mismatch " : "",
                 (flags & IOCTL_RSC_CONFORMANCE_REASON_HIGH_FAILURE_RATE) ? "high_fail " : "");
}

static const CHAR* rsc_sw_orchestration_stage_name(_In_ ULONG stage_id)
{
    switch (stage_id) {
        case IOCTL_RSC_ORCHESTRATION_STAGE_COVERAGE:
            return "coverage";
        case IOCTL_RSC_ORCHESTRATION_STAGE_REMEDIATION:
            return "remediation";
        case IOCTL_RSC_ORCHESTRATION_STAGE_STABILIZATION:
            return "stabilization";
        case IOCTL_RSC_ORCHESTRATION_STAGE_CONFORMANCE:
            return "conformance";
        case IOCTL_RSC_ORCHESTRATION_STAGE_PROFILES:
            return "profiles";
        default:
            return "unknown";
    }
}

static VOID append_orchestration_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                         _In_ size_t capacity, _Inout_ size_t* offset,
                                         _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_RUN_COVERAGE) ? "coverage " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_RUN_REMEDIATION) ? "remediation " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_RUN_STABILIZATION) ? "stabilization " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_RUN_CONFORMANCE) ? "conformance " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_RUN_PROFILES) ? "profiles " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_STRICT) ? "strict " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_INCLUDE_ARCMGR) ? "arcmgr " : "",
                 (flags & IOCTL_RSC_ORCHESTRATION_FLAG_REFRESH_EACH_STAGE) ? "refresh " : "");
}

static VOID append_policy_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                  _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_POLICY_FLAG_VOTE) ? "vote " : "",
                 (flags & IOCTL_RSC_POLICY_FLAG_REMOVE) ? "remove " : "",
                 (flags & IOCTL_RSC_POLICY_FLAG_PROPAGATED) ? "propagated " : "",
                 (flags & IOCTL_RSC_POLICY_FLAG_PARENT) ? "parent " : "",
                 (flags & IOCTL_RSC_POLICY_FLAG_ACTIVE) ? "active " : "");
}

static VOID append_fabric_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                  _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_FABRIC_FLAG_REQUIRE_ACTIVE) ? "active " : "",
                 (flags & IOCTL_RSC_FABRIC_FLAG_REQUIRE_POLICY_ACTIVITY) ? "policy " : "",
                 (flags & IOCTL_RSC_FABRIC_FLAG_REQUIRE_DEPENDENCY_HEALTH) ? "deps " : "",
                 (flags & IOCTL_RSC_FABRIC_FLAG_INCLUDE_THERMAL_PRESSURE) ? "thermal " : "",
                 (flags & IOCTL_RSC_FABRIC_FLAG_STRICT) ? "strict " : "");
}

static VOID append_domain_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                  _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_DOMAIN_FLAG_REQUIRE_ACTIVE) ? "active " : "",
                 (flags & IOCTL_RSC_DOMAIN_FLAG_REQUIRE_POLICY_ACTIVITY) ? "policy " : "",
                 (flags & IOCTL_RSC_DOMAIN_FLAG_REQUIRE_DEPENDENCY_HEALTH) ? "deps " : "",
                 (flags & IOCTL_RSC_DOMAIN_FLAG_INCLUDE_THERMAL_PRESSURE) ? "thermal " : "",
                 (flags & IOCTL_RSC_DOMAIN_FLAG_STRICT) ? "strict " : "");
}

static VOID append_interconnect_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                        _In_ size_t capacity, _Inout_ size_t* offset,
                                        _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_ACTIVE_ROUTES) ? "active " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_BANDWIDTH_HEADROOM) ? "headroom "
                                                                                  : "",
                 (flags & IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_POLICY_LINK) ? "policy " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_FLAG_INCLUDE_THERMAL_PRESSURE) ? "thermal " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_FLAG_STRICT) ? "strict " : "");
}

static VOID append_interconnect_route_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                              _In_ size_t capacity, _Inout_ size_t* offset,
                                              _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s}", flags,
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_ACTIVE) ? "active " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_POLICY_LINKED) ? "policy " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_THERMAL_HIGH) ? "thermal " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_STATUS_FAIL) ? "status " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_UNDER_PROVISIONED) ? "under " : "",
                 (flags & IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_OVER_UTILIZED) ? "over " : "");
}

static const CHAR* rsc_sw_sync_profile_name(_In_ ULONG profile_id)
{
    switch (profile_id) {
        case RSCSW_SYNC_PROFILE_BOOT:
            return "boot";
        case RSCSW_SYNC_PROFILE_BALANCED:
            return "balanced";
        case RSCSW_SYNC_PROFILE_PERFORMANCE:
            return "performance";
        case RSCSW_SYNC_PROFILE_INTERCONNECT:
            return "interconnect";
        case RSCSW_SYNC_PROFILE_THERMAL:
            return "thermal";
        default:
            return "unknown";
    }
}

static VOID append_sync_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & RSCSW_SYNC_FLAG_FORCE_ENABLE) ? "force " : "",
                 (flags & RSCSW_SYNC_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_SYNC_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_SYNC_FLAG_THERMAL_GUARD) ? "thermal_guard " : "",
                 (flags & RSCSW_SYNC_FLAG_VERIFY_QUERY) ? "verify " : "");
}

static const CHAR* rsc_sw_validate_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_VALIDATE_MODE_BASELINE:
            return "baseline";
        case RSCSW_VALIDATE_MODE_STRICT:
            return "strict";
        case RSCSW_VALIDATE_MODE_THERMAL:
            return "thermal";
        default:
            return "unknown";
    }
}

static VOID append_validate_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                    _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s}", flags,
                 (flags & RSCSW_VALIDATE_FLAG_REPAIR_STATE) ? "repair_state " : "",
                 (flags & RSCSW_VALIDATE_FLAG_REPAIR_CLOCK) ? "repair_clock " : "",
                 (flags & RSCSW_VALIDATE_FLAG_REPAIR_POLICY) ? "repair_policy " : "",
                 (flags & RSCSW_VALIDATE_FLAG_VERIFY_ICB) ? "verify_icb " : "",
                 (flags & RSCSW_VALIDATE_FLAG_VERIFY_ENUM_NAMES) ? "verify_names " : "");
}

static const CHAR* rsc_sw_sequence_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_SEQUENCE_MODE_DEFAULT:
            return "default";
        case RSCSW_SEQUENCE_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_SEQUENCE_MODE_THERMAL:
            return "thermal";
        default:
            return "unknown";
    }
}

static VOID append_sequence_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                    _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_SEQUENCE_FLAG_INCLUDE_ACTIVE) ? "active " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_INCLUDE_SLEEP) ? "sleep " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_INCLUDE_WAKE) ? "wake " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_FORCE_POLICY) ? "policy " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_SEQUENCE_FLAG_VERIFY_QUERY) ? "verify " : "");
}

static const CHAR* rsc_sw_lookup_query_name(_In_ ULONG query_type)
{
    switch (query_type) {
        case RSCSW_LOOKUP_QUERY_SUBSYSTEM:
            return "subsystem";
        case RSCSW_LOOKUP_QUERY_PPP_ID:
            return "ppp";
        case RSCSW_LOOKUP_QUERY_ICB_MASTER_ID:
            return "icb_master";
        case RSCSW_LOOKUP_QUERY_ICB_SLAVE_ID:
            return "icb_slave";
        case RSCSW_LOOKUP_QUERY_TEXT:
            return "text";
        case RSCSW_LOOKUP_QUERY_AOP_SUBSYSTEM_ID:
            return "aop_subsystem";
        case RSCSW_LOOKUP_QUERY_ARC_RES_ID:
            return "arc_resource";
        case RSCSW_LOOKUP_QUERY_CMDDB_SLV_ID:
            return "cmddb_slv";
        case RSCSW_LOOKUP_QUERY_CMDDB_QUERY_TYPE:
            return "cmddb_query";
        case RSCSW_LOOKUP_QUERY_PDC_DRV_ID:
            return "pdc_drv";
        case RSCSW_LOOKUP_QUERY_PDC_TRIGGER_ID:
            return "pdc_trigger";
        case RSCSW_LOOKUP_QUERY_RPMH_SET_ID:
            return "rpmh_set";
        case RSCSW_LOOKUP_QUERY_NPA_CLIENT_TYPE:
            return "npa_client";
        case RSCSW_LOOKUP_QUERY_GLINK_EVENT_ID:
            return "glink_event";
        case RSCSW_LOOKUP_QUERY_TLMM_DRIVE_TYPE:
            return "tlmm_drive";
        case RSCSW_LOOKUP_QUERY_TSENS_THRESHOLD_ID:
            return "tsens_threshold";
        case RSCSW_LOOKUP_QUERY_CLOCK_DRIVER_ID:
            return "clock_driver";
        case RSCSW_LOOKUP_QUERY_DRIVER_MAP_ID:
            return "driver_map";
        case RSCSW_LOOKUP_QUERY_DOMAIN_ID:
            return "domain";
        case RSCSW_LOOKUP_QUERY_INTERCONNECT_ROUTE_ID:
            return "route";
        case RSCSW_LOOKUP_QUERY_CLOCK_FREQ_MATCH_ID:
            return "clock_freq";
        case RSCSW_LOOKUP_QUERY_CLOCK_RES_TYPE_ID:
            return "clock_resource";
        case RSCSW_LOOKUP_QUERY_CLOCK_CONFIG_PARAM_ID:
            return "clock_config";
        case RSCSW_LOOKUP_QUERY_GLINK_STATUS_ID:
            return "glink_status";
        case RSCSW_LOOKUP_QUERY_GLINK_XPORT_STATE_ID:
            return "glink_xport";
        case RSCSW_LOOKUP_QUERY_GLINK_LINK_STATE_ID:
            return "glink_link";
        case RSCSW_LOOKUP_QUERY_ICB_ERROR_ID:
            return "icb_error";
        case RSCSW_LOOKUP_QUERY_ICB_QUERY_TYPE:
            return "icb_query";
        case RSCSW_LOOKUP_QUERY_ICB_REQ_TYPE:
            return "icb_request";
        case RSCSW_LOOKUP_QUERY_DAL_OBJECT_TYPE:
            return "dal_object";
        case RSCSW_LOOKUP_QUERY_AOP_STATE_ID:
            return "aop_state";
        case RSCSW_LOOKUP_QUERY_AOP_TX_ERROR_ID:
            return "aop_tx_error";
        case RSCSW_LOOKUP_QUERY_PDC_STATUS_ID:
            return "pdc_status";
        case RSCSW_LOOKUP_QUERY_NPA_QUERY_TYPE_ID:
            return "npa_query";
        case RSCSW_LOOKUP_QUERY_GLINK_ERROR_ID:
            return "glink_error";
        case RSCSW_LOOKUP_QUERY_GLINK_REMOTE_STATE_ID:
            return "glink_remote";
        case RSCSW_LOOKUP_QUERY_ICB_BUS_TYPE_ID:
            return "icb_bus";
        case RSCSW_LOOKUP_QUERY_ICB_NOTIFY_THRESHOLD_ID:
            return "icb_notify_threshold";
        case RSCSW_LOOKUP_QUERY_ICB_NODE_TYPE_ID:
            return "icb_node";
        case RSCSW_LOOKUP_QUERY_ICB_INTERFACE_TYPE_ID:
            return "icb_interface";
        case RSCSW_LOOKUP_QUERY_PDC_FLAGS_ID:
            return "pdc_flags";
        case RSCSW_LOOKUP_QUERY_PDC_PIN_TYPE_ID:
            return "pdc_pin";
        case RSCSW_LOOKUP_QUERY_TLMM_DIR_TYPE_ID:
            return "tlmm_dir";
        case RSCSW_LOOKUP_QUERY_TLMM_PULL_TYPE_ID:
            return "tlmm_pull";
        case RSCSW_LOOKUP_QUERY_TLMM_VALUE_TYPE_ID:
            return "tlmm_value";
        case RSCSW_LOOKUP_QUERY_TSENS_SROT_STATE_ID:
            return "tsens_srot";
        case RSCSW_LOOKUP_QUERY_TSENS_RESULT_TYPE_ID:
            return "tsens_result";
        case RSCSW_LOOKUP_QUERY_GLINK_LOCAL_STATE_ID:
            return "glink_local";
        case RSCSW_LOOKUP_QUERY_GLINK_CHANNEL_EVENT_ID:
            return "glink_channel";
        case RSCSW_LOOKUP_QUERY_GLINK_XPORT_PRIORITY_ID:
            return "glink_priority";
        case RSCSW_LOOKUP_QUERY_ICB_NOTIFY_CROSSING_ID:
            return "icb_crossing";
        case RSCSW_LOOKUP_QUERY_ICB_REQ_KIND_ID:
            return "icb_request_kind";
        case RSCSW_LOOKUP_QUERY_ICB_HW_NODE_KIND_ID:
            return "icb_hw_node";
        case RSCSW_LOOKUP_QUERY_ICB_SW_STATE_ID:
            return "icb_sw_state";
        case RSCSW_LOOKUP_QUERY_AOP_STAT_IDX_ID:
            return "aop_stat_index";
        case RSCSW_LOOKUP_QUERY_AOP_SETUP_ERROR_ID:
            return "aop_setup_error";
        case RSCSW_LOOKUP_QUERY_CMDDB_ERROR_ID:
            return "cmddb_error";
        case RSCSW_LOOKUP_QUERY_NPA_REQ_ATTR_ID:
            return "npa_request_attr";
        case RSCSW_LOOKUP_QUERY_NPA_NODE_ATTR_ID:
            return "npa_node_attr";
        case RSCSW_LOOKUP_QUERY_NPA_RES_ATTR_ID:
            return "npa_resource_attr";
        case RSCSW_LOOKUP_QUERY_NPA_STATUS_ID:
            return "npa_status";
        case RSCSW_LOOKUP_QUERY_ICB_CLIENT_ATTR_ID:
            return "icb_client_attr";
        case RSCSW_LOOKUP_QUERY_CLOCK_ACTION_ID:
            return "clock_action";
        case RSCSW_LOOKUP_QUERY_CLOCK_SLEEP_MODE_ID:
            return "clock_sleep_mode";
        case RSCSW_LOOKUP_QUERY_CLOCK_RESET_ID:
            return "clock_reset";
        case RSCSW_LOOKUP_QUERY_CLOCK_REQ_DATA_ID:
            return "clock_request_data";
        case RSCSW_LOOKUP_QUERY_PCU_RES_STATE_ID:
            return "pcu_resource_state";
        case RSCSW_LOOKUP_QUERY_PCU_RES_TYPE_ID:
            return "pcu_resource_type";
        case RSCSW_LOOKUP_QUERY_TSENS_CLIENT_TYPE_ID:
            return "tsens_client";
        case RSCSW_LOOKUP_QUERY_TSENS_TEMP_ALARM_ID:
            return "tsens_temp_alarm";
        case RSCSW_LOOKUP_QUERY_PPP_RES_RANGE_ID:
            return "ppp_range";
        case RSCSW_LOOKUP_QUERY_POWERDOMAIN_ACTION_ID:
            return "powerdomain_action";
        case RSCSW_LOOKUP_QUERY_SUPPRESSIBLE_STATE_ID:
            return "suppressible_state";
        case RSCSW_LOOKUP_QUERY_PM_RES_IF_ID:
            return "pm_resource_if";
        case RSCSW_LOOKUP_QUERY_CLOCK_QDSS_EVENT_ID:
            return "clock_qdss_event";
        case RSCSW_LOOKUP_QUERY_CLOCK_TIMER_DEADLINE_ID:
            return "clock_timer_deadline";
        case RSCSW_LOOKUP_QUERY_TLMM_IO_TYPE_ID:
            return "tlmm_io_type";
        case RSCSW_LOOKUP_QUERY_TSENS_LIMIT_TYPE_ID:
            return "tsens_limit";
        case RSCSW_LOOKUP_QUERY_PPP_VOTE_SET_TYPE_ID:
            return "ppp_vote_set";
        case RSCSW_LOOKUP_QUERY_PPP_PMIC_ID:
            return "ppp_pmic";
        case RSCSW_LOOKUP_QUERY_PPP_MVS_SUBTYPE_ID:
            return "ppp_mvs_subtype";
        case RSCSW_LOOKUP_QUERY_PPP_CXOBUF_SUBTYPE_ID:
            return "ppp_cxobuf_subtype";
        case RSCSW_LOOKUP_QUERY_PPP_RWAY_SUBTYPE_ID:
            return "ppp_rway_subtype";
        case RSCSW_LOOKUP_QUERY_RSC_REQ_TYPE_ID:
            return "rsc_request_type";
        case RSCSW_LOOKUP_QUERY_PM_VRM_VOTE_OFFSET_ID:
            return "pm_vrm_vote_offset";
        case RSCSW_LOOKUP_QUERY_CLOCK_SRC_CONFIG_ID:
            return "clock_source_config";
        case RSCSW_LOOKUP_QUERY_CLOCK_SRC_CONFIG_MODE_ID:
            return "clock_source_config_mode";
        case RSCSW_LOOKUP_QUERY_NPA_TRANSACTION_ATTR_ID:
            return "npa_transaction_attr";
        case RSCSW_LOOKUP_QUERY_NPA_RES_TYPE_ID:
            return "npa_resource_type";
        case RSCSW_LOOKUP_QUERY_NPA_EVENT_TRIGGER_ID:
            return "npa_event_trigger";
        case RSCSW_LOOKUP_QUERY_DAL_MUTEX_ATTR_ID:
            return "dal_mutex_attr";
        case RSCSW_LOOKUP_QUERY_DAL_MUTEX_LOCK_TYPE_ID:
            return "dal_mutex_lock_type";
        case RSCSW_LOOKUP_QUERY_GLINK_LOG_EVENT_ID:
            return "glink_log_event";
        case RSCSW_LOOKUP_QUERY_GLINK_PKT_RESULT_ID:
            return "glink_pkt_result";
        case RSCSW_LOOKUP_QUERY_GLINK_XPORT_LOG_MASK_ID:
            return "glink_xport_log_mask";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_STATE_ID:
            return "rsc_tcs_state";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_MDSS_CFG_ID:
            return "rsc_tcs_mdss_cfg";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_PCIE_CFG_ID:
            return "rsc_tcs_pcie_cfg";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_APSS_CFG_ID:
            return "rsc_tcs_apss_cfg";
        case RSCSW_LOOKUP_QUERY_AOP_LPM_MONITOR_STATE_ID:
            return "aop_lpm_monitor_state";
        case RSCSW_LOOKUP_QUERY_ARC_VOTE_CLASS_ID:
            return "arc_vote_class";
        case RSCSW_LOOKUP_QUERY_CLOCK_AGG_DRIVER_ID:
            return "clock_agg_driver";
        case RSCSW_LOOKUP_QUERY_CMDDB_ACCESS_PROFILE_ID:
            return "cmddb_access_profile";
        case RSCSW_LOOKUP_QUERY_DAL_EVENT_CTRL_ID:
            return "dal_event_ctrl";
        case RSCSW_LOOKUP_QUERY_GLINK_SSR_EVENT_ID:
            return "glink_ssr_event";
        case RSCSW_LOOKUP_QUERY_ICB_QUERY_PATH_ID:
            return "icb_query_path";
        case RSCSW_LOOKUP_QUERY_NPA_AGG_DRIVER_ID:
            return "npa_agg_driver";
        case RSCSW_LOOKUP_QUERY_PDC_PIN_STATE_ID:
            return "pdc_pin_state";
        case RSCSW_LOOKUP_QUERY_PPP_COMPARATOR_TYPE_ID:
            return "ppp_comparator_type";
        case RSCSW_LOOKUP_QUERY_RPMH_CMDQ_STAGE_ID:
            return "rpmh_cmdq_stage";
        case RSCSW_LOOKUP_QUERY_RSC_TRIGGER_REASON_ID:
            return "rsc_trigger_reason";
        case RSCSW_LOOKUP_QUERY_TLMM_GPIO_ACTION_ID:
            return "tlmm_gpio_action";
        case RSCSW_LOOKUP_QUERY_TSENS_CALLBACK_TYPE_ID:
            return "tsens_callback_type";
        case RSCSW_LOOKUP_QUERY_AOP_ADDRESS_MODE_ID:
            return "aop_address_mode";
        case RSCSW_LOOKUP_QUERY_ARC_OPERATING_POINT_ID:
            return "arc_operating_point";
        case RSCSW_LOOKUP_QUERY_CLOCK_NPA_QUERY_ID:
            return "clock_npa_query";
        case RSCSW_LOOKUP_QUERY_CMDDB_LOOKUP_STATUS_ID:
            return "cmddb_lookup_status";
        case RSCSW_LOOKUP_QUERY_DAL_SYNC_PRIMITIVE_ID:
            return "dal_sync_primitive";
        case RSCSW_LOOKUP_QUERY_GLINK_QOS_EVENT_ID:
            return "glink_qos_event";
        case RSCSW_LOOKUP_QUERY_ICB_QUERY_STAGE_ID:
            return "icb_query_stage";
        case RSCSW_LOOKUP_QUERY_NPA_RES_DRIVER_ID:
            return "npa_resource_driver";
        case RSCSW_LOOKUP_QUERY_PCU_VOTE_REASON_ID:
            return "pcu_vote_reason";
        case RSCSW_LOOKUP_QUERY_PDC_TRIGGER_CONFIG_ID:
            return "pdc_trigger_config";
        case RSCSW_LOOKUP_QUERY_PPP_RES_SERVICE_ID:
            return "ppp_resource_service";
        case RSCSW_LOOKUP_QUERY_RPMH_AMC_PHASE_ID:
            return "rpmh_amc_phase";
        case RSCSW_LOOKUP_QUERY_RSC_CMD_ACTION_ID:
            return "rsc_cmd_action";
        case RSCSW_LOOKUP_QUERY_TLMM_CONFIG_PHASE_ID:
            return "tlmm_config_phase";
        case RSCSW_LOOKUP_QUERY_TSENS_CONTROLLER_EVENT_ID:
            return "tsens_controller_event";
        case RSCSW_LOOKUP_QUERY_AOP_IPCC_TARGET_ID:
            return "aop_ipcc_target";
        case RSCSW_LOOKUP_QUERY_ARC_VOTE_BUCKET_ID:
            return "arc_vote_bucket";
        case RSCSW_LOOKUP_QUERY_CLOCK_AGG_QUERY_ID:
            return "clock_agg_query";
        case RSCSW_LOOKUP_QUERY_CMDDB_QUERY_SRC_ID:
            return "cmddb_query_source";
        case RSCSW_LOOKUP_QUERY_DAL_EVENT_TYPE_ID:
            return "dal_event_type";
        case RSCSW_LOOKUP_QUERY_GLINK_XPORT_CMD_ID:
            return "glink_xport_cmd";
        case RSCSW_LOOKUP_QUERY_ICB_ROUTE_CLASS_ID:
            return "icb_route_class";
        case RSCSW_LOOKUP_QUERY_NPA_CLIENT_LIFECYCLE_ID:
            return "npa_client_lifecycle";
        case RSCSW_LOOKUP_QUERY_PCU_TRANSITION_PHASE_ID:
            return "pcu_transition_phase";
        case RSCSW_LOOKUP_QUERY_PDC_SEQUENCE_PHASE_ID:
            return "pdc_sequence_phase";
        case RSCSW_LOOKUP_QUERY_PPP_REQ_OPCODE_ID:
            return "ppp_request_opcode";
        case RSCSW_LOOKUP_QUERY_RPMH_BARRIER_TYPE_ID:
            return "rpmh_barrier_type";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_MODE_ID:
            return "rsc_tcs_mode";
        case RSCSW_LOOKUP_QUERY_TLMM_TILE_SELECTOR_ID:
            return "tlmm_tile_selector";
        case RSCSW_LOOKUP_QUERY_TSENS_INTERRUPT_THRESHOLD_ID:
            return "tsens_interrupt_threshold";
        case RSCSW_LOOKUP_QUERY_AOP_SERVICE_CLASS_ID:
            return "aop_service_class";
        case RSCSW_LOOKUP_QUERY_ARC_RM_ACTION_ID:
            return "arc_rm_action";
        case RSCSW_LOOKUP_QUERY_CLOCK_DOMAIN_POLICY_ID:
            return "clock_domain_policy";
        case RSCSW_LOOKUP_QUERY_CMDDB_ACCESS_PATH_ID:
            return "cmddb_access_path";
        case RSCSW_LOOKUP_QUERY_DAL_SYNC_EVENT_ID:
            return "dal_sync_event";
        case RSCSW_LOOKUP_QUERY_GLINK_CHANNEL_ROLE_ID:
            return "glink_channel_role";
        case RSCSW_LOOKUP_QUERY_ICB_BW_CLASS_ID:
            return "icb_bw_class";
        case RSCSW_LOOKUP_QUERY_NPA_EVENT_LIFECYCLE_ID:
            return "npa_event_lifecycle";
        case RSCSW_LOOKUP_QUERY_PCU_POLICY_REASON_ID:
            return "pcu_policy_reason";
        case RSCSW_LOOKUP_QUERY_PDC_WAKE_SRC_ID:
            return "pdc_wake_source";
        case RSCSW_LOOKUP_QUERY_PPP_VOTE_PATH_ID:
            return "ppp_vote_path";
        case RSCSW_LOOKUP_QUERY_RPMH_COMPLETION_STATE_ID:
            return "rpmh_completion_state";
        case RSCSW_LOOKUP_QUERY_RSC_TCS_PRIORITY_ID:
            return "rsc_tcs_priority";
        case RSCSW_LOOKUP_QUERY_TLMM_DRIVE_STRENGTH_ID:
            return "tlmm_drive_strength";
        case RSCSW_LOOKUP_QUERY_TSENS_TRIP_CLASS_ID:
            return "tsens_trip_class";
        case RSCSW_LOOKUP_QUERY_AOP_MAILBOX_STATE_ID:
            return "aop_mailbox_state";
        case RSCSW_LOOKUP_QUERY_ARC_POLICY_PHASE_ID:
            return "arc_policy_phase";
        case RSCSW_LOOKUP_QUERY_CLOCK_CONFIG_PHASE_ID:
            return "clock_plan_stage";
        case RSCSW_LOOKUP_QUERY_CMDDB_SLAVE_REGION_ID:
            return "cmddb_slave_region";
        case RSCSW_LOOKUP_QUERY_DAL_WORKLOOP_EVENT_ID:
            return "dal_workloop_event";
        case RSCSW_LOOKUP_QUERY_GLINK_RX_INTENT_STATE_ID:
            return "glink_rx_intent_state";
        case RSCSW_LOOKUP_QUERY_ICB_LATENCY_CLASS_ID:
            return "icb_latency_class";
        case RSCSW_LOOKUP_QUERY_NPA_REQ_PHASE_ID:
            return "npa_request_phase";
        case RSCSW_LOOKUP_QUERY_PCU_FREQ_FLOOR_STATE_ID:
            return "pcu_freq_floor_state";
        case RSCSW_LOOKUP_QUERY_PDC_LATENCY_MODE_ID:
            return "pdc_latency_mode";
        case RSCSW_LOOKUP_QUERY_PPP_RAIL_STATE_ID:
            return "ppp_rail_state";
        case RSCSW_LOOKUP_QUERY_RPMH_WAKESET_TYPE_ID:
            return "rpmh_wakeset_type";
        case RSCSW_LOOKUP_QUERY_RSC_SEQ_PHASE_ID:
            return "rsc_seq_phase";
        case RSCSW_LOOKUP_QUERY_TLMM_OWNER_DOMAIN_ID:
            return "tlmm_owner_domain";
        case RSCSW_LOOKUP_QUERY_TSENS_FILTER_MODE_ID:
            return "tsens_filter_mode";
        case RSCSW_LOOKUP_QUERY_AOP_IPCC_PROTOCOL_ID:
            return "aop_ipcc_protocol";
        case RSCSW_LOOKUP_QUERY_ARC_RAIL_VOLTAGE_LEVEL_ID:
            return "arc_rail_voltage_level";
        case RSCSW_LOOKUP_QUERY_CLOCK_HAL_API_TYPE_ID:
            return "clock_hal_api_type";
        case RSCSW_LOOKUP_QUERY_CMDDB_AUX_CTRL_TYPE_ID:
            return "cmddb_aux_ctrl_type";
        case RSCSW_LOOKUP_QUERY_DAL_ULOG_LOCK_TYPE_ID:
            return "dal_ulog_lock_type";
        case RSCSW_LOOKUP_QUERY_GLINK_TRACER_EVENT_ID:
            return "glink_tracer_event";
        case RSCSW_LOOKUP_QUERY_ICB_PEP_PERF_STATE_ID:
            return "icb_pep_perf_state";
        case RSCSW_LOOKUP_QUERY_NPA_EVENT_TYPE_ID:
            return "npa_event_type";
        case RSCSW_LOOKUP_QUERY_PCU_POWER_DOMAIN_API_ID:
            return "pcu_power_domain_api";
        case RSCSW_LOOKUP_QUERY_PDC_DEEPSTANDBY_MODE_ID:
            return "pdc_deepstandby_mode";
        case RSCSW_LOOKUP_QUERY_PPP_PARENT_CHILD_PARSE_STATE_ID:
            return "ppp_parent_child_parse_state";
        case RSCSW_LOOKUP_QUERY_RPMH_ISR_TYPE_ID:
            return "rpmh_isr_type";
        case RSCSW_LOOKUP_QUERY_RSC_RC_DIRTY_STATE_ID:
            return "rsc_rc_dirty_state";
        case RSCSW_LOOKUP_QUERY_TLMM_DISCRETE_GPIO_PARSE_STATE_ID:
            return "tlmm_discrete_gpio_parse_state";
        case RSCSW_LOOKUP_QUERY_TSENS_INTERRUPT_TYPE_ID:
            return "tsens_interrupt_type";
        case RSCSW_LOOKUP_QUERY_AOP_SLEEP_STATE_ID:
            return "aop_sleep_state";
        case RSCSW_LOOKUP_QUERY_RPMH_RC_PRIORITY_ID:
            return "rpmh_rc_priority";
        case RSCSW_LOOKUP_QUERY_CLOCK_CONFIG_API_ID:
            return "clock_config_api";
        case RSCSW_LOOKUP_QUERY_DAL_ULOG_MEMORY_CONFIG_ID:
            return "dal_ulog_memory_config";
        case RSCSW_LOOKUP_QUERY_DAL_ULOG_ALLOC_ID:
            return "dal_ulog_alloc";
        case RSCSW_LOOKUP_QUERY_GLINK_QDSS_TRACE_STREAM_ID:
            return "glink_qdss_trace_stream";
        case RSCSW_LOOKUP_QUERY_GLINK_QDSS_SPEED_ID:
            return "glink_qdss_speed";
        case RSCSW_LOOKUP_QUERY_DAL_ULOG_MEMORY_USAGE_ID:
            return "dal_ulog_memory_usage";
        case RSCSW_LOOKUP_QUERY_CLOCK_SRC_DISABLE_MODE_ID:
            return "clock_source_disable_mode";
        case RSCSW_LOOKUP_QUERY_PDC_IDLE_CALLBACK_TYPE_ID:
            return "pdc_idle_callback_type";
        case RSCSW_LOOKUP_QUERY_PPP_PMIC_GPIO_PARSE_STATE_ID:
            return "ppp_pmic_gpio_parse_state";
        case RSCSW_LOOKUP_QUERY_RSC_RC_STATE_ID:
            return "rsc_rc_state";
        case RSCSW_LOOKUP_QUERY_CLOCK_PLL_FRAC_MODE_ID:
            return "clock_pll_frac_mode";
        case RSCSW_LOOKUP_QUERY_TLMM_LED_GPIO_PARSE_STATE_ID:
            return "tlmm_led_gpio_parse_state";
        case RSCSW_LOOKUP_QUERY_TSENS_DEV_STATE_ID:
            return "tsens_device_state";
        default:
            return "unknown";
    }
}

static const CHAR* rsc_sw_lookup_kind_name(_In_ ULONG kind)
{
    switch (kind) {
        case RSCSW_LOOKUP_KIND_SUBSYSTEM:
            return "subsystem";
        case RSCSW_LOOKUP_KIND_PPP_RES:
            return "ppp";
        case RSCSW_LOOKUP_KIND_ICB_MASTER:
            return "icb_master";
        case RSCSW_LOOKUP_KIND_ICB_SLAVE:
            return "icb_slave";
        case RSCSW_LOOKUP_KIND_AOP_SUBSYSTEM:
            return "aop_subsystem";
        case RSCSW_LOOKUP_KIND_ARC_RES:
            return "arc_resource";
        case RSCSW_LOOKUP_KIND_CMDDB_SLV:
            return "cmddb_slv";
        case RSCSW_LOOKUP_KIND_CMDDB_QUERY:
            return "cmddb_query";
        case RSCSW_LOOKUP_KIND_PDC_DRV:
            return "pdc_drv";
        case RSCSW_LOOKUP_KIND_PDC_TRIGGER:
            return "pdc_trigger";
        case RSCSW_LOOKUP_KIND_RPMH_SET:
            return "rpmh_set";
        case RSCSW_LOOKUP_KIND_NPA_CLIENT:
            return "npa_client";
        case RSCSW_LOOKUP_KIND_GLINK_EVENT:
            return "glink_event";
        case RSCSW_LOOKUP_KIND_TLMM_DRIVE:
            return "tlmm_drive";
        case RSCSW_LOOKUP_KIND_TSENS_THRESHOLD:
            return "tsens_threshold";
        case RSCSW_LOOKUP_KIND_CLOCK_DRIVER:
            return "clock_driver";
        case RSCSW_LOOKUP_KIND_DRIVER_MAP:
            return "driver_map";
        case RSCSW_LOOKUP_KIND_DOMAIN:
            return "domain";
        case RSCSW_LOOKUP_KIND_INTERCONNECT_ROUTE:
            return "route";
        case RSCSW_LOOKUP_KIND_CLOCK_FREQ_MATCH:
            return "clock_freq";
        case RSCSW_LOOKUP_KIND_CLOCK_RES:
            return "clock_resource";
        case RSCSW_LOOKUP_KIND_CLOCK_CONFIG_PARAM:
            return "clock_config";
        case RSCSW_LOOKUP_KIND_GLINK_STATUS:
            return "glink_status";
        case RSCSW_LOOKUP_KIND_GLINK_XPORT_STATE:
            return "glink_xport";
        case RSCSW_LOOKUP_KIND_GLINK_LINK_STATE:
            return "glink_link";
        case RSCSW_LOOKUP_KIND_ICB_ERROR:
            return "icb_error";
        case RSCSW_LOOKUP_KIND_ICB_QUERY:
            return "icb_query";
        case RSCSW_LOOKUP_KIND_ICB_REQ:
            return "icb_request";
        case RSCSW_LOOKUP_KIND_DAL_OBJECT:
            return "dal_object";
        case RSCSW_LOOKUP_KIND_AOP_STATE:
            return "aop_state";
        case RSCSW_LOOKUP_KIND_AOP_TX_ERROR:
            return "aop_tx_error";
        case RSCSW_LOOKUP_KIND_PDC_STATUS:
            return "pdc_status";
        case RSCSW_LOOKUP_KIND_NPA_QUERY:
            return "npa_query";
        case RSCSW_LOOKUP_KIND_GLINK_ERROR:
            return "glink_error";
        case RSCSW_LOOKUP_KIND_GLINK_REMOTE_STATE:
            return "glink_remote";
        case RSCSW_LOOKUP_KIND_ICB_BUS:
            return "icb_bus";
        case RSCSW_LOOKUP_KIND_ICB_NOTIFY_THRESHOLD:
            return "icb_notify_threshold";
        case RSCSW_LOOKUP_KIND_ICB_NODE:
            return "icb_node";
        case RSCSW_LOOKUP_KIND_ICB_INTERFACE:
            return "icb_interface";
        case RSCSW_LOOKUP_KIND_PDC_FLAGS:
            return "pdc_flags";
        case RSCSW_LOOKUP_KIND_PDC_PIN_TYPE:
            return "pdc_pin";
        case RSCSW_LOOKUP_KIND_TLMM_DIR:
            return "tlmm_dir";
        case RSCSW_LOOKUP_KIND_TLMM_PULL:
            return "tlmm_pull";
        case RSCSW_LOOKUP_KIND_TLMM_VALUE:
            return "tlmm_value";
        case RSCSW_LOOKUP_KIND_TSENS_SROT_STATE:
            return "tsens_srot";
        case RSCSW_LOOKUP_KIND_TSENS_RESULT_TYPE:
            return "tsens_result";
        case RSCSW_LOOKUP_KIND_GLINK_LOCAL_STATE:
            return "glink_local";
        case RSCSW_LOOKUP_KIND_GLINK_CHANNEL_EVENT:
            return "glink_channel";
        case RSCSW_LOOKUP_KIND_GLINK_XPORT_PRIORITY:
            return "glink_priority";
        case RSCSW_LOOKUP_KIND_ICB_NOTIFY_CROSSING:
            return "icb_crossing";
        case RSCSW_LOOKUP_KIND_ICB_REQ_KIND:
            return "icb_request_kind";
        case RSCSW_LOOKUP_KIND_ICB_HW_NODE_KIND:
            return "icb_hw_node";
        case RSCSW_LOOKUP_KIND_ICB_SW_STATE:
            return "icb_sw_state";
        case RSCSW_LOOKUP_KIND_AOP_STAT_IDX:
            return "aop_stat_index";
        case RSCSW_LOOKUP_KIND_AOP_SETUP_ERROR:
            return "aop_setup_error";
        case RSCSW_LOOKUP_KIND_CMDDB_ERROR:
            return "cmddb_error";
        case RSCSW_LOOKUP_KIND_NPA_REQ_ATTR:
            return "npa_request_attr";
        case RSCSW_LOOKUP_KIND_NPA_NODE_ATTR:
            return "npa_node_attr";
        case RSCSW_LOOKUP_KIND_NPA_RES_ATTR:
            return "npa_resource_attr";
        case RSCSW_LOOKUP_KIND_NPA_STATUS:
            return "npa_status";
        case RSCSW_LOOKUP_KIND_ICB_CLIENT_ATTR:
            return "icb_client_attr";
        case RSCSW_LOOKUP_KIND_CLOCK_ACTION:
            return "clock_action";
        case RSCSW_LOOKUP_KIND_CLOCK_SLEEP_MODE:
            return "clock_sleep_mode";
        case RSCSW_LOOKUP_KIND_CLOCK_RESET:
            return "clock_reset";
        case RSCSW_LOOKUP_KIND_CLOCK_REQ_DATA:
            return "clock_request_data";
        case RSCSW_LOOKUP_KIND_PCU_RES_STATE:
            return "pcu_resource_state";
        case RSCSW_LOOKUP_KIND_PCU_RES_TYPE:
            return "pcu_resource_type";
        case RSCSW_LOOKUP_KIND_TSENS_CLIENT_TYPE:
            return "tsens_client";
        case RSCSW_LOOKUP_KIND_TSENS_TEMP_ALARM:
            return "tsens_temp_alarm";
        case RSCSW_LOOKUP_KIND_PPP_RES_RANGE:
            return "ppp_range";
        case RSCSW_LOOKUP_KIND_POWERDOMAIN_ACTION:
            return "powerdomain_action";
        case RSCSW_LOOKUP_KIND_SUPPRESSIBLE_STATE:
            return "suppressible_state";
        case RSCSW_LOOKUP_KIND_PM_RES_IF:
            return "pm_resource_if";
        case RSCSW_LOOKUP_KIND_CLOCK_QDSS_EVENT:
            return "clock_qdss_event";
        case RSCSW_LOOKUP_KIND_CLOCK_TIMER_DEADLINE:
            return "clock_timer_deadline";
        case RSCSW_LOOKUP_KIND_TLMM_IO_TYPE:
            return "tlmm_io_type";
        case RSCSW_LOOKUP_KIND_TSENS_LIMIT_TYPE:
            return "tsens_limit";
        case RSCSW_LOOKUP_KIND_PPP_VOTE_SET_TYPE:
            return "ppp_vote_set";
        case RSCSW_LOOKUP_KIND_PPP_PMIC_ID:
            return "ppp_pmic";
        case RSCSW_LOOKUP_KIND_PPP_MVS_SUBTYPE:
            return "ppp_mvs_subtype";
        case RSCSW_LOOKUP_KIND_PPP_CXOBUF_SUBTYPE:
            return "ppp_cxobuf_subtype";
        case RSCSW_LOOKUP_KIND_PPP_RWAY_SUBTYPE:
            return "ppp_rway_subtype";
        case RSCSW_LOOKUP_KIND_RSC_REQ_TYPE:
            return "rsc_request_type";
        case RSCSW_LOOKUP_KIND_PM_VRM_VOTE_OFFSET:
            return "pm_vrm_vote_offset";
        case RSCSW_LOOKUP_KIND_CLOCK_SRC_CONFIG:
            return "clock_source_config";
        case RSCSW_LOOKUP_KIND_CLOCK_SRC_CONFIG_MODE:
            return "clock_source_config_mode";
        case RSCSW_LOOKUP_KIND_NPA_TRANSACTION_ATTR:
            return "npa_transaction_attr";
        case RSCSW_LOOKUP_KIND_NPA_RES_TYPE:
            return "npa_resource_type";
        case RSCSW_LOOKUP_KIND_NPA_EVENT_TRIGGER:
            return "npa_event_trigger";
        case RSCSW_LOOKUP_KIND_DAL_MUTEX_ATTR:
            return "dal_mutex_attr";
        case RSCSW_LOOKUP_KIND_DAL_MUTEX_LOCK_TYPE:
            return "dal_mutex_lock_type";
        case RSCSW_LOOKUP_KIND_GLINK_LOG_EVENT:
            return "glink_log_event";
        case RSCSW_LOOKUP_KIND_GLINK_PKT_RESULT:
            return "glink_pkt_result";
        case RSCSW_LOOKUP_KIND_GLINK_XPORT_LOG_MASK:
            return "glink_xport_log_mask";
        case RSCSW_LOOKUP_KIND_RSC_TCS_STATE:
            return "rsc_tcs_state";
        case RSCSW_LOOKUP_KIND_RSC_TCS_MDSS_CFG:
            return "rsc_tcs_mdss_cfg";
        case RSCSW_LOOKUP_KIND_RSC_TCS_PCIE_CFG:
            return "rsc_tcs_pcie_cfg";
        case RSCSW_LOOKUP_KIND_RSC_TCS_APSS_CFG:
            return "rsc_tcs_apss_cfg";
        case RSCSW_LOOKUP_KIND_AOP_LPM_MONITOR_STATE:
            return "aop_lpm_monitor_state";
        case RSCSW_LOOKUP_KIND_ARC_VOTE_CLASS:
            return "arc_vote_class";
        case RSCSW_LOOKUP_KIND_CLOCK_AGG_DRIVER:
            return "clock_agg_driver";
        case RSCSW_LOOKUP_KIND_CMDDB_ACCESS_PROFILE:
            return "cmddb_access_profile";
        case RSCSW_LOOKUP_KIND_DAL_EVENT_CTRL:
            return "dal_event_ctrl";
        case RSCSW_LOOKUP_KIND_GLINK_SSR_EVENT:
            return "glink_ssr_event";
        case RSCSW_LOOKUP_KIND_ICB_QUERY_PATH:
            return "icb_query_path";
        case RSCSW_LOOKUP_KIND_NPA_AGG_DRIVER:
            return "npa_agg_driver";
        case RSCSW_LOOKUP_KIND_PDC_PIN_STATE:
            return "pdc_pin_state";
        case RSCSW_LOOKUP_KIND_PPP_COMPARATOR_TYPE:
            return "ppp_comparator_type";
        case RSCSW_LOOKUP_KIND_RPMH_CMDQ_STAGE:
            return "rpmh_cmdq_stage";
        case RSCSW_LOOKUP_KIND_RSC_TRIGGER_REASON:
            return "rsc_trigger_reason";
        case RSCSW_LOOKUP_KIND_TLMM_GPIO_ACTION:
            return "tlmm_gpio_action";
        case RSCSW_LOOKUP_KIND_TSENS_CALLBACK_TYPE:
            return "tsens_callback_type";
        case RSCSW_LOOKUP_KIND_AOP_ADDRESS_MODE:
            return "aop_address_mode";
        case RSCSW_LOOKUP_KIND_ARC_OPERATING_POINT:
            return "arc_operating_point";
        case RSCSW_LOOKUP_KIND_CLOCK_NPA_QUERY:
            return "clock_npa_query";
        case RSCSW_LOOKUP_KIND_CMDDB_LOOKUP_STATUS:
            return "cmddb_lookup_status";
        case RSCSW_LOOKUP_KIND_DAL_SYNC_PRIMITIVE:
            return "dal_sync_primitive";
        case RSCSW_LOOKUP_KIND_GLINK_QOS_EVENT:
            return "glink_qos_event";
        case RSCSW_LOOKUP_KIND_ICB_QUERY_STAGE:
            return "icb_query_stage";
        case RSCSW_LOOKUP_KIND_NPA_RES_DRIVER:
            return "npa_resource_driver";
        case RSCSW_LOOKUP_KIND_PCU_VOTE_REASON:
            return "pcu_vote_reason";
        case RSCSW_LOOKUP_KIND_PDC_TRIGGER_CONFIG:
            return "pdc_trigger_config";
        case RSCSW_LOOKUP_KIND_PPP_RES_SERVICE:
            return "ppp_resource_service";
        case RSCSW_LOOKUP_KIND_RPMH_AMC_PHASE:
            return "rpmh_amc_phase";
        case RSCSW_LOOKUP_KIND_RSC_CMD_ACTION:
            return "rsc_cmd_action";
        case RSCSW_LOOKUP_KIND_TLMM_CONFIG_PHASE:
            return "tlmm_config_phase";
        case RSCSW_LOOKUP_KIND_TSENS_CONTROLLER_EVENT:
            return "tsens_controller_event";
        case RSCSW_LOOKUP_KIND_AOP_IPCC_TARGET:
            return "aop_ipcc_target";
        case RSCSW_LOOKUP_KIND_ARC_VOTE_BUCKET:
            return "arc_vote_bucket";
        case RSCSW_LOOKUP_KIND_CLOCK_AGG_QUERY:
            return "clock_agg_query";
        case RSCSW_LOOKUP_KIND_CMDDB_QUERY_SRC:
            return "cmddb_query_source";
        case RSCSW_LOOKUP_KIND_DAL_EVENT_TYPE:
            return "dal_event_type";
        case RSCSW_LOOKUP_KIND_GLINK_XPORT_CMD:
            return "glink_xport_cmd";
        case RSCSW_LOOKUP_KIND_ICB_ROUTE_CLASS:
            return "icb_route_class";
        case RSCSW_LOOKUP_KIND_NPA_CLIENT_LIFECYCLE:
            return "npa_client_lifecycle";
        case RSCSW_LOOKUP_KIND_PCU_TRANSITION_PHASE:
            return "pcu_transition_phase";
        case RSCSW_LOOKUP_KIND_PDC_SEQUENCE_PHASE:
            return "pdc_sequence_phase";
        case RSCSW_LOOKUP_KIND_PPP_REQ_OPCODE:
            return "ppp_request_opcode";
        case RSCSW_LOOKUP_KIND_RPMH_BARRIER_TYPE:
            return "rpmh_barrier_type";
        case RSCSW_LOOKUP_KIND_RSC_TCS_MODE:
            return "rsc_tcs_mode";
        case RSCSW_LOOKUP_KIND_TLMM_TILE_SELECTOR:
            return "tlmm_tile_selector";
        case RSCSW_LOOKUP_KIND_TSENS_INTERRUPT_THRESHOLD:
            return "tsens_interrupt_threshold";
        case RSCSW_LOOKUP_KIND_AOP_SERVICE_CLASS:
            return "aop_service_class";
        case RSCSW_LOOKUP_KIND_ARC_RM_ACTION:
            return "arc_rm_action";
        case RSCSW_LOOKUP_KIND_CLOCK_DOMAIN_POLICY:
            return "clock_domain_policy";
        case RSCSW_LOOKUP_KIND_CMDDB_ACCESS_PATH:
            return "cmddb_access_path";
        case RSCSW_LOOKUP_KIND_DAL_SYNC_EVENT:
            return "dal_sync_event";
        case RSCSW_LOOKUP_KIND_GLINK_CHANNEL_ROLE:
            return "glink_channel_role";
        case RSCSW_LOOKUP_KIND_ICB_BW_CLASS:
            return "icb_bw_class";
        case RSCSW_LOOKUP_KIND_NPA_EVENT_LIFECYCLE:
            return "npa_event_lifecycle";
        case RSCSW_LOOKUP_KIND_PCU_POLICY_REASON:
            return "pcu_policy_reason";
        case RSCSW_LOOKUP_KIND_PDC_WAKE_SRC:
            return "pdc_wake_source";
        case RSCSW_LOOKUP_KIND_PPP_VOTE_PATH:
            return "ppp_vote_path";
        case RSCSW_LOOKUP_KIND_RPMH_COMPLETION_STATE:
            return "rpmh_completion_state";
        case RSCSW_LOOKUP_KIND_RSC_TCS_PRIORITY:
            return "rsc_tcs_priority";
        case RSCSW_LOOKUP_KIND_TLMM_DRIVE_STRENGTH:
            return "tlmm_drive_strength";
        case RSCSW_LOOKUP_KIND_TSENS_TRIP_CLASS:
            return "tsens_trip_class";
        case RSCSW_LOOKUP_KIND_AOP_MAILBOX_STATE:
            return "aop_mailbox_state";
        case RSCSW_LOOKUP_KIND_ARC_POLICY_PHASE:
            return "arc_policy_phase";
        case RSCSW_LOOKUP_KIND_CLOCK_CONFIG_PHASE:
            return "clock_plan_stage";
        case RSCSW_LOOKUP_KIND_CMDDB_SLAVE_REGION:
            return "cmddb_slave_region";
        case RSCSW_LOOKUP_KIND_DAL_WORKLOOP_EVENT:
            return "dal_workloop_event";
        case RSCSW_LOOKUP_KIND_GLINK_RX_INTENT_STATE:
            return "glink_rx_intent_state";
        case RSCSW_LOOKUP_KIND_ICB_LATENCY_CLASS:
            return "icb_latency_class";
        case RSCSW_LOOKUP_KIND_NPA_REQ_PHASE:
            return "npa_request_phase";
        case RSCSW_LOOKUP_KIND_PCU_FREQ_FLOOR_STATE:
            return "pcu_freq_floor_state";
        case RSCSW_LOOKUP_KIND_PDC_LATENCY_MODE:
            return "pdc_latency_mode";
        case RSCSW_LOOKUP_KIND_PPP_RAIL_STATE:
            return "ppp_rail_state";
        case RSCSW_LOOKUP_KIND_RPMH_WAKESET_TYPE:
            return "rpmh_wakeset_type";
        case RSCSW_LOOKUP_KIND_RSC_SEQ_PHASE:
            return "rsc_seq_phase";
        case RSCSW_LOOKUP_KIND_TLMM_OWNER_DOMAIN:
            return "tlmm_owner_domain";
        case RSCSW_LOOKUP_KIND_TSENS_FILTER_MODE:
            return "tsens_filter_mode";
        case RSCSW_LOOKUP_KIND_AOP_IPCC_PROTOCOL:
            return "aop_ipcc_protocol";
        case RSCSW_LOOKUP_KIND_ARC_RAIL_VOLTAGE_LEVEL:
            return "arc_rail_voltage_level";
        case RSCSW_LOOKUP_KIND_CLOCK_HAL_API_TYPE:
            return "clock_hal_api_type";
        case RSCSW_LOOKUP_KIND_CMDDB_AUX_CTRL_TYPE:
            return "cmddb_aux_ctrl_type";
        case RSCSW_LOOKUP_KIND_DAL_ULOG_LOCK_TYPE:
            return "dal_ulog_lock_type";
        case RSCSW_LOOKUP_KIND_GLINK_TRACER_EVENT:
            return "glink_tracer_event";
        case RSCSW_LOOKUP_KIND_ICB_PEP_PERF_STATE:
            return "icb_pep_perf_state";
        case RSCSW_LOOKUP_KIND_NPA_EVENT_TYPE:
            return "npa_event_type";
        case RSCSW_LOOKUP_KIND_PCU_POWER_DOMAIN_API:
            return "pcu_power_domain_api";
        case RSCSW_LOOKUP_KIND_PDC_DEEPSTANDBY_MODE:
            return "pdc_deepstandby_mode";
        case RSCSW_LOOKUP_KIND_PPP_PARENT_CHILD_PARSE_STATE:
            return "ppp_parent_child_parse_state";
        case RSCSW_LOOKUP_KIND_RPMH_ISR_TYPE:
            return "rpmh_isr_type";
        case RSCSW_LOOKUP_KIND_RSC_RC_DIRTY_STATE:
            return "rsc_rc_dirty_state";
        case RSCSW_LOOKUP_KIND_TLMM_DISCRETE_GPIO_PARSE_STATE:
            return "tlmm_discrete_gpio_parse_state";
        case RSCSW_LOOKUP_KIND_TSENS_INTERRUPT_TYPE:
            return "tsens_interrupt_type";
        case RSCSW_LOOKUP_KIND_AOP_SLEEP_STATE:
            return "aop_sleep_state";
        case RSCSW_LOOKUP_KIND_RPMH_RC_PRIORITY:
            return "rpmh_rc_priority";
        case RSCSW_LOOKUP_KIND_CLOCK_CONFIG_API:
            return "clock_config_api";
        case RSCSW_LOOKUP_KIND_DAL_ULOG_MEMORY_CONFIG:
            return "dal_ulog_memory_config";
        case RSCSW_LOOKUP_KIND_DAL_ULOG_ALLOC:
            return "dal_ulog_alloc";
        case RSCSW_LOOKUP_KIND_GLINK_QDSS_TRACE_STREAM:
            return "glink_qdss_trace_stream";
        case RSCSW_LOOKUP_KIND_GLINK_QDSS_SPEED:
            return "glink_qdss_speed";
        case RSCSW_LOOKUP_KIND_DAL_ULOG_MEMORY_USAGE:
            return "dal_ulog_memory_usage";
        case RSCSW_LOOKUP_KIND_CLOCK_SRC_DISABLE_MODE:
            return "clock_source_disable_mode";
        case RSCSW_LOOKUP_KIND_PDC_IDLE_CALLBACK_TYPE:
            return "pdc_idle_callback_type";
        case RSCSW_LOOKUP_KIND_PPP_PMIC_GPIO_PARSE_STATE:
            return "ppp_pmic_gpio_parse_state";
        case RSCSW_LOOKUP_KIND_RSC_RC_STATE:
            return "rsc_rc_state";
        case RSCSW_LOOKUP_KIND_CLOCK_PLL_FRAC_MODE:
            return "clock_pll_frac_mode";
        case RSCSW_LOOKUP_KIND_TLMM_LED_GPIO_PARSE_STATE:
            return "tlmm_led_gpio_parse_state";
        case RSCSW_LOOKUP_KIND_TSENS_DEV_STATE:
            return "tsens_device_state";
        default:
            return "unknown";
    }
}

static VOID append_lookup_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                  _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset,
                 "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_LOOKUP_FLAG_PARTIAL_MATCH) ? "partial " : "",
                 (flags & RSCSW_LOOKUP_FLAG_CASE_INSENSITIVE) ? "nocase " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_PPP) ? "ppp " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_ICB_MASTER) ? "icb_master " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_ICB_SLAVE) ? "icb_slave " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_AOP) ? "aop " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_ARC) ? "arc " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_CMDDB) ? "cmddb " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_PDC) ? "pdc " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_RPMH) ? "rpmh " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_NPA) ? "npa " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_GLINK) ? "glink " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_TLMM) ? "tlmm " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_TSENS) ? "tsens " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_CLOCK) ? "clock " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_DRIVER_MAP) ? "driver_map " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_ROUTE) ? "route " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_DAL) ? "dal " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_PCU) ? "pcu " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_RSC) ? "rsc " : "",
                 (flags & RSCSW_LOOKUP_FLAG_INCLUDE_ARCMGR) ? "arcmgr " : "");
}

static const CHAR* rsc_sw_provision_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_PROVISION_MODE_DEFAULT:
            return "default";
        case RSCSW_PROVISION_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_PROVISION_MODE_THERMAL:
            return "thermal";
        case RSCSW_PROVISION_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_provision_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                     _In_ size_t capacity, _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_PROVISION_FLAG_APPLY_STATE) ? "apply_state " : "",
                 (flags & RSCSW_PROVISION_FLAG_APPLY_POLICY) ? "apply_policy " : "",
                 (flags & RSCSW_PROVISION_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_PROVISION_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_PROVISION_FLAG_VERIFY_QUERY) ? "verify " : "",
                 (flags & RSCSW_PROVISION_FLAG_THERMAL_GUARD) ? "thermal_guard " : "");
}

static const CHAR* rsc_sw_rail_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_RAIL_MODE_BALANCED:
            return "balanced";
        case RSCSW_RAIL_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_RAIL_MODE_THERMAL:
            return "thermal";
        case RSCSW_RAIL_MODE_RETENTION:
            return "retention";
        default:
            return "unknown";
    }
}

static VOID append_rail_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_RAIL_FLAG_APPLY_ARC) ? "apply_arc " : "",
                 (flags & RSCSW_RAIL_FLAG_APPLY_CLOCK) ? "apply_clock " : "",
                 (flags & RSCSW_RAIL_FLAG_APPLY_POLICY) ? "apply_policy " : "",
                 (flags & RSCSW_RAIL_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_RAIL_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_RAIL_FLAG_VERIFY_QUERY) ? "verify " : "",
                 (flags & RSCSW_RAIL_FLAG_ISSUE_GLINK) ? "glink " : "",
                 (flags & RSCSW_RAIL_FLAG_THERMAL_GUARD) ? "thermal_guard " : "");
}

static const CHAR* rsc_sw_tcs_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_TCS_MODE_ACTIVE:
            return "active";
        case RSCSW_TCS_MODE_SLEEP:
            return "sleep";
        case RSCSW_TCS_MODE_WAKE:
            return "wake";
        case RSCSW_TCS_MODE_HIBERNATE:
            return "hibernate";
        default:
            return "unknown";
    }
}

static VOID append_tcs_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                               _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_TCS_FLAG_PROGRAM_TCS) ? "program_tcs " : "",
                 (flags & RSCSW_TCS_FLAG_VOTE_POLICY) ? "vote_policy " : "",
                 (flags & RSCSW_TCS_FLAG_QUEUE_GLINK) ? "queue_glink " : "",
                 (flags & RSCSW_TCS_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_TCS_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_TCS_FLAG_VERIFY_QUERY) ? "verify " : "",
                 (flags & RSCSW_TCS_FLAG_THERMAL_GUARD) ? "thermal_guard " : "",
                 (flags & RSCSW_TCS_FLAG_STRICT_TCS) ? "strict_tcs " : "");
}

static const CHAR* rsc_sw_thermal_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_THERMAL_MODE_MONITOR:
            return "monitor";
        case RSCSW_THERMAL_MODE_MITIGATE:
            return "mitigate";
        case RSCSW_THERMAL_MODE_RECOVER:
            return "recover";
        case RSCSW_THERMAL_MODE_CALIBRATE:
            return "calibrate";
        default:
            return "unknown";
    }
}

static VOID append_thermal_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                   _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_THERMAL_FLAG_APPLY_STATE) ? "apply_state " : "",
                 (flags & RSCSW_THERMAL_FLAG_APPLY_THRESHOLDS) ? "apply_thresholds " : "",
                 (flags & RSCSW_THERMAL_FLAG_APPLY_POLICY) ? "apply_policy " : "",
                 (flags & RSCSW_THERMAL_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_THERMAL_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_THERMAL_FLAG_VERIFY_QUERY) ? "verify " : "",
                 (flags & RSCSW_THERMAL_FLAG_FORCE_COOLING) ? "force_cooling " : "",
                 (flags & RSCSW_THERMAL_FLAG_ISSUE_GLINK) ? "glink " : "");
}

static const CHAR* rsc_sw_coord_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_COORD_MODE_BASELINE:
            return "baseline";
        case RSCSW_COORD_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_COORD_MODE_THERMAL:
            return "thermal";
        case RSCSW_COORD_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_coord_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                 _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_COORD_FLAG_RUN_SYNC) ? "sync " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_VALIDATE) ? "validate " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_SEQUENCE) ? "sequence " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_PROVISION) ? "provision " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_RAIL) ? "rail " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_TCS) ? "tcs " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_THERMAL) ? "thermal " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_FABRIC) ? "fabric " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_COORD_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_COORD_FLAG_STRICT_VERIFY) ? "strict " : "");
}

static const CHAR* rsc_sw_audit_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_AUDIT_MODE_BASELINE:
            return "baseline";
        case RSCSW_AUDIT_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_AUDIT_MODE_THERMAL:
            return "thermal";
        case RSCSW_AUDIT_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_audit_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                 _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_AUDIT_FLAG_REPAIR_STATE) ? "repair_state " : "",
                 (flags & RSCSW_AUDIT_FLAG_REPAIR_CLOCK) ? "repair_clock " : "",
                 (flags & RSCSW_AUDIT_FLAG_REPAIR_POLICY) ? "repair_policy " : "",
                 (flags & RSCSW_AUDIT_FLAG_VERIFY_ICB) ? "verify_icb " : "",
                 (flags & RSCSW_AUDIT_FLAG_VERIFY_ENUM_NAMES) ? "verify_names " : "",
                 (flags & RSCSW_AUDIT_FLAG_RUN_COORD) ? "coord " : "",
                 (flags & RSCSW_AUDIT_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_AUDIT_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_AUDIT_FLAG_STRICT) ? "strict " : "",
                 (flags & RSCSW_AUDIT_FLAG_VERIFY_DRIVER_MAP) ? "verify_drvmap " : "",
                 (flags & RSCSW_AUDIT_FLAG_VERIFY_DOMAIN_MAP) ? "verify_domain " : "",
                 (flags & RSCSW_AUDIT_FLAG_VERIFY_ROUTE_MAP) ? "verify_route " : "");
}

static const CHAR* rsc_sw_supervisor_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_SUPERVISOR_MODE_BASELINE:
            return "baseline";
        case RSCSW_SUPERVISOR_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_SUPERVISOR_MODE_THERMAL:
            return "thermal";
        case RSCSW_SUPERVISOR_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_supervisor_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf,
                                      _In_ size_t capacity, _Inout_ size_t* offset,
                                      _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_SUPERVISOR_FLAG_RUN_AUDIT) ? "audit " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_RUN_COORD) ? "coord " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_RUN_THERMAL) ? "thermal " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_STRICT) ? "strict " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_REPAIR_STATE) ? "repair_state " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_REPAIR_CLOCK) ? "repair_clock " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_REPAIR_POLICY) ? "repair_policy " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_VERIFY_ICB) ? "verify_icb " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_VERIFY_ENUM_NAMES) ? "verify_names " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_TRACK_MODULE_MATRIX) ? "track_matrix " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_VERIFY_DRIVER_MAP) ? "verify_drvmap " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_VERIFY_DOMAIN_MAP) ? "verify_domain " : "",
                 (flags & RSCSW_SUPERVISOR_FLAG_VERIFY_ROUTE_MAP) ? "verify_route " : "");
}

static const CHAR* rsc_sw_autotune_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_AUTOTUNE_MODE_BASELINE:
            return "baseline";
        case RSCSW_AUTOTUNE_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_AUTOTUNE_MODE_THERMAL:
            return "thermal";
        case RSCSW_AUTOTUNE_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_autotune_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                    _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_AUTOTUNE_FLAG_RUN_SUPERVISOR) ? "supervisor " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_RUN_THERMAL) ? "thermal " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_APPLY_STATE) ? "apply_state " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_APPLY_CLOCK) ? "apply_clock " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_APPLY_POLICY) ? "apply_policy " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_STRICT) ? "strict " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_AGGRESSIVE) ? "aggressive " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_VERIFY_QUERY) ? "verify_query " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_TRACK_HISTORY) ? "track_history " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_DEPENDENCY_GUARD) ? "dependency_guard " : "",
                 (flags & RSCSW_AUTOTUNE_FLAG_PROFILE_POLICY) ? "profile_policy " : "");
}

static const CHAR* rsc_sw_harden_mode_name(_In_ ULONG mode_id)
{
    switch (mode_id) {
        case RSCSW_HARDEN_MODE_BASELINE:
            return "baseline";
        case RSCSW_HARDEN_MODE_PERFORMANCE:
            return "performance";
        case RSCSW_HARDEN_MODE_THERMAL:
            return "thermal";
        case RSCSW_HARDEN_MODE_RECOVERY:
            return "recovery";
        default:
            return "unknown";
    }
}

static VOID append_harden_flags_a(_Inout_updates_bytes_(capacity) PCHAR buf, _In_ size_t capacity,
                                  _Inout_ size_t* offset, _In_ ULONG flags)
{
    append_fmt_a(buf, capacity, offset, "flags=0x%08X{%s%s%s%s%s%s%s%s%s%s}", flags,
                 (flags & RSCSW_HARDEN_FLAG_RUN_AUTOTUNE) ? "autotune " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_SUPERVISOR) ? "supervisor " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_AUDIT) ? "audit " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_COORD) ? "coord " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_THERMAL) ? "thermal " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_INTERCONNECT) ? "interconnect " : "",
                 (flags & RSCSW_HARDEN_FLAG_RUN_DOMAIN) ? "domain " : "",
                 (flags & RSCSW_HARDEN_FLAG_STRICT) ? "strict " : "",
                 (flags & RSCSW_HARDEN_FLAG_VERIFY_QUERY) ? "verify_query " : "",
                 (flags & RSCSW_HARDEN_FLAG_TRACK_HISTORY) ? "track_history " : "");
}

static const CHAR* rsc_sw_policy_type_name(_In_ ULONG res_type)
{
    switch (res_type) {
        case 0x0u:
            return "ldo";
        case 0x1u:
            return "smps";
        case 0x6u:
            return "cxo";
        case 0x7u:
            return "boost";
        case 0xAu:
            return "tlmm_gpio";
        case 0xDu:
            return "div_clk";
        case 0xFu:
            return "buck_boost";
        case 0x10u:
            return "fixed_vreg";
        default:
            return "unknown";
    }
}

static const CHAR* rsc_sw_link_type_name(_In_ ULONG link_type)
{
    switch (link_type) {
        case 1u:
            return "ctrl";
        case 2u:
            return "clock";
        case 3u:
            return "data";
        case 4u:
            return "thermal";
        default:
            return "unk";
    }
}

static const CHAR* rsc_sw_checkpoint_op_name(_In_ ULONG operation)
{
    switch (operation) {
        case IOCTL_RSC_CHECKPOINT_OP_CAPTURE:
            return "capture";
        case IOCTL_RSC_CHECKPOINT_OP_APPLY:
            return "apply";
        default:
            return "unknown";
    }
}

NTSTATUS build_pep_stats(_In_ p_dev_ctx_t ctx, _Inout_updates_bytes_(120) PVOID buf,
                         _In_ ULONG size)
{
    UNREFERENCED_PARAMETER(ctx);
    if (size < 120)
        return STATUS_BUFFER_TOO_SMALL;

    pep_stats_result_st1_t* r = (pep_stats_result_st1_t*)buf;
    int req = r->base.pep_status;

    RtlZeroMemory(buf, 120);
    init_state_result(&r->base);

    r->debugger_info = registry_query_u_long(L"DebuggerTransportSelected", 0);
    r->xo_clock_logging_enable = (unsigned __int8)registry_query_u_long(
        L"XoClockLoggingEnable", (g.config.BranchTrackEnabled ? 1u : 0u));

    if (req > 0)
        r->debugger_info |= 1u;

    if (req == -1)
        r->debugger_info = registry_query_u_long(L"DebuggerTransportSelected", 0);

    fill_subsystem_stats_blob(&r->stats_blob[0], SOC_SUBSYS_SOC);
    fill_subsystem_stats_blob(&r->stats_blob[1], SOC_SUBSYS_APPS);
    fill_subsystem_stats_blob(&r->stats_blob[2], SOC_SUBSYS_MODEM);

    return STATUS_SUCCESS;
}

NTSTATUS build_subsystem_status(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(320) PVOID buf,
                                _In_ ULONG size)
{
    UNREFERENCED_PARAMETER(ctx);
    if (size < 320)
        return STATUS_BUFFER_TOO_SMALL;

    sys_master_stats_t_t* s = (sys_master_stats_t_t*)buf;
    RtlZeroMemory(buf, 320);

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        soc_subsystem_stats_t st;
        BOOLEAN enabled = FALSE;
        RtlZeroMemory(&st, sizeof(st));
        (VOID) soc_subsys_get_info(i, NULL, NULL, NULL, &enabled, &st);

        s[i].sys_sleep_version = enabled ? 1u : 0u;
        s[i].sys_sleep_cnt = st.wakeup_cnt;
        s[i].sys_sleep_entered_at = st.blocked_duration100ns;
        s[i].sys_sleep_exited_at = st.sleep_duration100ns;
        s[i].sys_sleep_accumulated_duration = st.blocked_duration100ns + st.sleep_duration100ns;
    }

    return STATUS_SUCCESS;
}

static VOID fill_dev_type(_Out_ pep_stats_dev_type_t* d, _In_z_ const char* name, _In_ int dstate)
{
    RtlZeroMemory(d, sizeof(*d));
    (VOID) RtlStringCchCopyA(d->dev_name, RTL_NUMBER_OF(d->dev_name), name);
    d->current_d_state = dstate;
    d->current_f_state = 0;
    for (int i = 0; i < 8; i++)
        d->current_p_state[i] = 0;
}

static ULONG dev_info_find_or_add(_Inout_ pep_dev_info_t* d,
                                  _Inout_updates_(max_val) ULONG* dev_ids, _Inout_ PULONG dev_cnt,
                                  _In_ ULONG max_val, _In_ ULONG id16)
{
    if (!d || !dev_ids || !dev_cnt)
        return 0xFFFFFFFFu;

    for (ULONG i = 0; i < *dev_cnt; i++)
        if (dev_ids[i] == id16)
            return i;

    if (*dev_cnt >= max_val)
        return 0xFFFFFFFFu;

    char name[32];
    RtlZeroMemory(name, sizeof(name));
    (VOID) RtlStringCchPrintfA(name, RTL_NUMBER_OF(name), "DEV%04X", (unsigned)id16);

    fill_dev_type(&d->dev_info[*dev_cnt], name, 0);
    dev_ids[*dev_cnt] = id16;
    (*dev_cnt)++;
    return (*dev_cnt) - 1;
}

NTSTATUS build_xo_clock_status(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(3844) PVOID buf,
                               _In_ ULONG size)
{
    UNREFERENCED_PARAMETER(ctx);
    if (size < 3844)
        return STATUS_BUFFER_TOO_SMALL;

    xo_blocking_clocks_t* x = (xo_blocking_clocks_t*)buf;
    RtlZeroMemory(buf, 3844);

    x->clock_data.num_clocks = 0;

    const size_t snap_cap = 16384;
    size_t snap_bytes = 0;
    PVOID snap = ExAllocatePool2(POOL_FLAG_NON_PAGED, snap_cap, 'xCpq');
    if (snap) {
        NTSTATUS st_snap = res_snapshot(snap, snap_cap, &snap_bytes);
        if (NT_SUCCESS(st_snap)) {
            res_snapshot_hdr_t* h = (res_snapshot_hdr_t*)snap;
            ULONG cnt =
                snapshot_cnt_for_buf(h, snap_bytes, RTL_NUMBER_OF(x->clock_data.each_clock));
            if (cnt != 0) {
                res_snapshot_entry_t* e = (res_snapshot_entry_t*)((PUCHAR)snap + sizeof(*h));
                for (ULONG i = 0; i < cnt; i++) {
                    pep_stats_each_clock_type_t* c = &x->clock_data.each_clock[i];
                    fill_dev_type(&c->base, "clock", (int)e[i].sub_type);
                    format_res_name_a(e[i].res_id, e[i].sub_type, c->clock_name,
                                      RTL_NUMBER_OF(c->clock_name));
                    c->pep_clock_value = e[i].value0;
                    c->hw_clock_value = e[i].value1;
                    c->base.current_p_state[0] = (int)e[i].value2;
                    c->base.current_p_state[1] = (int)e[i].value3;
                }
                x->clock_data.num_clocks = cnt;
            }
        }

        ExFreePool(snap);
    }

    if (x->clock_data.num_clocks == 0) {
        x->clock_data.num_clocks = 8;
        const char* names[8] = {"xo", "gcc", "cpu", "mem", "gpu", "ufs", "pcie", "disp"};
        int freqs[8] = {19200000, 0, 0, 0, 0, 0, 0, 0};

        for (int i = 0; i < 8; i++) {
            pep_stats_each_clock_type_t* c = &x->clock_data.each_clock[i];
            fill_dev_type(&c->base, "clock", 0);
            (VOID) RtlStringCchCopyA(c->clock_name, RTL_NUMBER_OF(c->clock_name), names[i]);
            c->pep_clock_value = (unsigned int)freqs[i];
            c->hw_clock_value = (unsigned int)freqs[i];
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS build_dev_info(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(15376) PVOID buf, _In_ ULONG size)
{
    UNREFERENCED_PARAMETER(ctx);

    if (size < 15376)
        return STATUS_BUFFER_TOO_SMALL;

    pep_dev_info_t* d = (pep_dev_info_t*)buf;
    RtlZeroMemory(buf, 15376);
    init_state_result(&d->base);

    d->num_of_dev = 192;

    const ULONG rid_dsta = 0x41545344u;
    const ULONG rid_fsta = 0x41545346u;
    const ULONG rid_pst0 = 0x30545350u;
    const ULONG rid_sdc1 = 0x31434453u;
    const ULONG rid_sdc2 = 0x32434453u;

    for (int i = 0; i < 192; i++)
        fill_dev_type(&d->dev_info[i], "dev", 0);

    ULONG dev_ids[188];
    ULONG dev_cnt = 0;
    RtlZeroMemory(dev_ids, sizeof(dev_ids));

    size_t snap_bytes = 0;
    const size_t snap_cap = 16384;
    PVOID snap = ExAllocatePool2(POOL_FLAG_NON_PAGED, snap_cap, 'iDpq');
    if (snap) {
        NTSTATUS st_snap = res_snapshot(snap, snap_cap, &snap_bytes);
        if (NT_SUCCESS(st_snap)) {
            res_snapshot_hdr_t* h = (res_snapshot_hdr_t*)snap;
            ULONG cnt = snapshot_cnt_for_buf(h, snap_bytes, 0xFFFFFFFFu);
            if (cnt != 0) {
                res_snapshot_entry_t* e = (res_snapshot_entry_t*)((PUCHAR)snap + sizeof(*h));

                for (ULONG i = 0; i < cnt; i++) {
                    res_snapshot_entry_t* x = &e[i];

                    if (x->res_id == rid_sdc1 || x->res_id == rid_sdc2) {
                        if (dev_cnt < 188) {
                            char nm[32];
                            RtlZeroMemory(nm, sizeof(nm));
                            (VOID) RtlStringCchPrintfA(nm, RTL_NUMBER_OF(nm), "%s",
                                                       (x->res_id == rid_sdc1) ? "SDC1" : "SDC2");
                            fill_dev_type(&d->dev_info[dev_cnt], nm, 0);
                            d->dev_info[dev_cnt].current_p_state[0] = (int)x->value0;
                            dev_cnt++;
                        }
                        continue;
                    }

                    if (x->res_id == rid_dsta) {
                        ULONG id16 = x->sub_type & 0xFFFFu;
                        ULONG slot = dev_info_find_or_add(d, dev_ids, &dev_cnt, 188, id16);
                        if (slot != 0xFFFFFFFFu)
                            d->dev_info[slot].current_d_state = (int)x->value0;
                        continue;
                    }

                    if (x->res_id == rid_fsta) {
                        ULONG id16 = (x->sub_type >> 16) & 0xFFFFu;
                        ULONG comp = x->sub_type & 0xFFFFu;
                        ULONG slot = dev_info_find_or_add(d, dev_ids, &dev_cnt, 188, id16);
                        if (slot != 0xFFFFFFFFu) {
                            if (comp == 0)
                                d->dev_info[slot].current_f_state = (int)x->value0;
                        }
                        continue;
                    }

                    if (x->res_id == rid_pst0) {
                        ULONG id16 = (x->sub_type >> 16) & 0xFFFFu;
                        ULONG comp = x->sub_type & 0xFFFFu;
                        ULONG slot = dev_info_find_or_add(d, dev_ids, &dev_cnt, 188, id16);
                        if (slot != 0xFFFFFFFFu) {
                            if (comp < 10)
                                d->dev_info[slot].current_p_state[comp] = (int)x->value0;
                        }
                        continue;
                    }
                }
            }
        }

        ExFreePool(snap);
    }

    for (int i = 0; i < 4; i++) {
        pep_stats_dev_type_t* e = &d->dev_info[188 + i];
        fill_dev_type(e, "driver_stats", 0);
        e->current_d_state = (int)g_stats.d0_entry;
        e->current_f_state = (int)g_stats.d0_exit;
        e->current_p_state[0] = (int)g_stats.po_fx_idle_state;
        e->current_p_state[1] = (int)g_stats.ioctl_cnt;
        e->current_p_state[2] = (int)g_stats.d0_entry;
        e->current_p_state[3] = (int)g_stats.d0_exit;
        e->current_p_state[4] = (int)g_stats.dev_attached;
    }

    return STATUS_SUCCESS;
}

NTSTATUS build_pw_cal_rundown(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(51212) PVOID buf,
                              _In_ ULONG size)
{
    UNREFERENCED_PARAMETER(ctx);
    if (size < 51212)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(buf, 51212);

    PCHAR text = (PCHAR)buf;
    const size_t text_cap = 32768;
    size_t off = 0;

    append_fmt_a(text, text_cap, &off, "power controller state\n");
    append_fmt_a(text, text_cap, &off, "info --on\n");
    append_fmt_a(text, text_cap, &off,
                 "ticks=%llu ioctl=%ld d0e=%ld d0x=%ld pwr_req=%ld pwr_not=%ld comp_act=%ld "
                 "comp_idle=%ld idle_state=%ld\n",
                 now_ticks(), g_stats.ioctl_cnt, g_stats.d0_entry, g_stats.d0_exit,
                 g_stats.po_fx_power_required, g_stats.po_fx_power_not_required,
                 g_stats.po_fx_comp_active, g_stats.po_fx_comp_idle, g_stats.po_fx_idle_state);
    append_fmt_a(text, text_cap, &off, "attached=%ld\n", g_stats.dev_attached);

    {
        stats_snapshot_t st;
        RtlZeroMemory(&st, sizeof(st));
        if (NT_SUCCESS(stats_get_snapshot(&st))) {
            append_fmt_a(text, text_cap, &off,
                         "stats: events=%llu errors=%llu ioctl_errors=%llu hist=%lu drop=%lu "
                         "last_ev=%lu/0x%08X args=%lu,%lu,%lu,%lu t=%llu\n",
                         st.event_cnt, st.error_cnt, st.ioctl_error_cnt, st.history_cnt,
                         st.history_dropped, st.last_event_code, (ULONG)st.last_event_status,
                         st.last_event_arg0, st.last_event_arg1, st.last_event_arg2,
                         st.last_event_arg3, st.last_event_tick100ns);
            append_fmt_a(text, text_cap, &off,
                         "stats_ioctl: code=0x%08X st=0x%08X in=%lu out=%lu\n", st.last_ioctl_code,
                         (ULONG)st.last_ioctl_status, st.last_ioctl_in_len, st.last_ioctl_out_len);
        }

        stats_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(stats_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "stats_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  t=%llu ev=%lu st=0x%08X args=%lu,%lu,%lu,%lu\n", hist[i].tick100ns,
                             hist[i].event_code, (ULONG)hist[i].status, hist[i].arg0, hist[i].arg1,
                             hist[i].arg2, hist[i].arg3);
            }
        }
    }

    {
        tsens_snapshot_t ts;
        RtlZeroMemory(&ts, sizeof(ts));
        tsens_get_snapshot(&ts);
        append_fmt_a(
            text, text_cap, &off,
            "tsens: sensors=%lu agg=%ld read=%llu dsm=%llu hits=%llu seq=%llu hist=%lu drop=%lu "
            "last_ev=%lu/0x%08X sensor=%lu temp=%ld low=%ld high=%ld t=%llu\n",
            ts.sensor_cnt, ts.aggregate_temp_deci_c, ts.read_cnt, ts.dsm_refresh_cnt,
            ts.threshold_hit_cnt, ts.event_sequence, ts.history_cnt, ts.history_dropped,
            ts.last_event_code, (ULONG)ts.last_event_status, ts.last_sensor_id, ts.last_temp_deci_c,
            ts.last_low_deci_c, ts.last_high_deci_c, ts.last_event_tick100ns);

        ULONG sensor_limit = ts.sensor_cnt;
        if (sensor_limit > 6)
            sensor_limit = 6;
        for (ULONG i = 0; i < sensor_limit; i++) {
            append_fmt_a(
                text, text_cap, &off,
                "  tsens[%lu]: t=%ld min=%ld max=%ld low=%ld high=%ld hits=%lu/%lu flags=0x%08X\n",
                i, ts.sensors[i].last_temp_deci_c, ts.sensors[i].min_temp_deci_c,
                ts.sensors[i].max_temp_deci_c, ts.sensors[i].low_threshold_deci_c,
                ts.sensors[i].high_threshold_deci_c, ts.sensors[i].low_hit_cnt,
                ts.sensors[i].high_hit_cnt, ts.sensors[i].flags);
        }

        tsens_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(tsens_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "tsens_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  seq=%llu t=%llu ev=%lu st=0x%08X sensor=%lu temp=%ld low=%ld "
                             "high=%ld aux=%lu,%lu\n",
                             hist[i].sequence, hist[i].tick100ns, hist[i].event_code,
                             (ULONG)hist[i].status, hist[i].sensor_id, hist[i].temp_deci_c,
                             hist[i].low_deci_c, hist[i].high_deci_c, hist[i].aux0, hist[i].aux1);
            }
        }
    }

    {
        mmrm_snapshot_t mmrm;
        RtlZeroMemory(&mmrm, sizeof(mmrm));
        mmrm_get_snapshot(&mmrm);
        append_fmt_a(
            text, text_cap, &off,
            "mmrm: ver=%lu feat=%lu reason=%lu pct=%lu upd=%lu wait=%lu timer=%lu "
            "reserve=%lu peak=%lu total=%lu dsm=0x%08X dsm_ref=%lu dsm_st=0x%08X dsm_poll=%lu "
            "policy_items=%lu clock_items=%lu volt_mv=%lu permille=%lu/%lu domain=%lu "
            "temp=%ld low=%ld high=%ld cam=%ld vid=%ld disp=%ld "
            "ev_seq=%llu hist=%lu drop=%lu last_ev=%s(%lu)/0x%08X "
            "last_reason=%lu last_pct=%lu last_peak=%lu last_temp=%ld last_low=%ld last_high=%ld "
            "t=%llu\n",
            mmrm.version, mmrm.feature_enabled, mmrm.throttle_reason, mmrm.throttle_percentage,
            mmrm.updated, mmrm.policy_wait_active, mmrm.sampling_timer_running,
            mmrm.reserved_power_milli_watts, mmrm.last_peak_milli_amps,
            mmrm.last_total_power_milli_watts, mmrm.dsm_flags, mmrm.dsm_refresh_cnt,
            mmrm.last_dsm_status, mmrm.dsm_poll_interval_milli_seconds,
            mmrm.last_dsm_policy_item_cnt, mmrm.last_dsm_clock_item_cnt,
            mmrm.peak_voltage_milli_volts, mmrm.global_dyn_permille, mmrm.global_lkg_permille,
            mmrm.domain_cnt, mmrm.current_temp_deci_k, mmrm.threshold_low_deci_k,
            mmrm.threshold_high_deci_k, mmrm.camera_temp_deci_c, mmrm.video_temp_deci_c,
            mmrm.display_temp_deci_c, mmrm.event_sequence, mmrm.history_cnt, mmrm.history_dropped,
            mmrm_event_name(mmrm.last_event_code), mmrm.last_event_code,
            (ULONG)mmrm.last_event_status, mmrm.last_event_reason, mmrm.last_event_percent,
            mmrm.last_event_peak_milli_amps, mmrm.last_event_temp_deci_k,
            mmrm.last_event_low_temp_deci_k, mmrm.last_event_high_temp_deci_k,
            mmrm.last_event_tick100ns);

        ULONG domain_limit = mmrm.domain_cnt;
        if (domain_limit > MMRM_MAX_CLOCK_DOMAINS)
            domain_limit = MMRM_MAX_CLOCK_DOMAINS;
        if (domain_limit > 12)
            domain_limit = 12;
        for (ULONG i = 0; i < domain_limit; i++) {
            append_fmt_a(
                text, text_cap, &off,
                "  mmrm_domain[%lu]: freq=%lu pwr=%lu flags=0x%08X dyn=%lu lkg=%lu bias=%ld\n", i,
                mmrm.domains[i].freq_khz, mmrm.domains[i].power_milli_watts, mmrm.domains[i].flags,
                mmrm.domains[i].dyn_permille, mmrm.domains[i].lkg_permille,
                mmrm.domains[i].temp_bias_deci_c);
        }

        mmrm_event_t history[12];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                mmrm_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "mmrm_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X reason=%lu pct=%lu upd=%lu peak=%lu "
                    "temp=%ld low=%ld high=%ld passive=%lu act=0x%08X feat=%lu dsm=0x%08X\n",
                    history[i].sequence, history[i].tick100ns,
                    mmrm_event_name(history[i].event_code), history[i].event_code,
                    (ULONG)history[i].status, history[i].reason, history[i].percent,
                    history[i].updated, history[i].peak_milli_amps, history[i].temp_deci_k,
                    history[i].low_temp_deci_k, history[i].high_temp_deci_k,
                    history[i].passive_limit, history[i].activation_reasons,
                    history[i].feature_enabled, history[i].dsm_flags);
            }
        }
    }

    {
        thermal_req_snapshot_t tr;
        RtlZeroMemory(&tr, sizeof(tr));
        thermal_req_get_snapshot(&tr);
        append_fmt_a(text, text_cap, &off,
                     "thermalreq: poll=%lu active=%lu last_req=%lu queued=%llu completed=%llu "
                     "cancelled=%llu "
                     "timeout=%llu low=%llu high=%llu busy=%llu noslot=%llu markfail=%llu seq=%llu "
                     "hist=%lu drop=%lu "
                     "last_ev=%lu/0x%08X req=%lu sensor=%lu temp=%ld low=%ld high=%ld t=%llu\n",
                     tr.poll_ms, tr.active_cnt, tr.last_req_id, tr.total_queued, tr.total_completed,
                     tr.total_cancelled, tr.total_timeout, tr.total_low_threshold_hits,
                     tr.total_high_threshold_hits, tr.total_busy_rejected,
                     tr.total_no_slot_rejected, tr.total_mark_cancelable_failures,
                     tr.event_sequence, tr.history_cnt, tr.history_dropped, tr.last_event_code,
                     (ULONG)tr.last_event_status, tr.last_event_req_id, tr.last_event_sensor_id,
                     tr.last_event_temp_deci_k, tr.last_event_low_temp_deci_k,
                     tr.last_event_high_temp_deci_k, tr.last_event_tick100ns);

        thermal_req_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(
                thermal_req_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "thermalreq_history: count=%lu dropped=%lu\n",
                         hist_cnt, hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  seq=%llu t=%llu ev=%lu st=0x%08X req=%lu sensor=%lu temp=%ld "
                             "low=%ld high=%ld active=%lu port=%lu\n",
                             hist[i].sequence, hist[i].tick100ns, hist[i].event_code,
                             (ULONG)hist[i].status, hist[i].req_id, hist[i].sensor_id,
                             hist[i].temp_deci_k, hist[i].low_temp_deci_k, hist[i].high_temp_deci_k,
                             hist[i].active_cnt, hist[i].port_id);
            }
        }
    }

    {
        bcl_snapshot_t bcl;
        RtlZeroMemory(&bcl, sizeof(bcl));
        bcl_get_snapshot(&bcl);

        append_fmt_a(
            text, text_cap, &off,
            "bcl: ver=%lu enabled=%lu notif=%lu park_state=%lu desired=%lu current=%lu "
            "throttle=%lu "
            "low_imax=%lu mitigation=%lu submit=%llu ok=%llu reject=%llu work=%llu th_op=%llu "
            "th_ds_ok=%llu th_ds_fail=%llu cache=%llu/%llu seq=%llu hist=%lu drop=%lu "
            "last_ev=%s(%lu)/0x%08X t=%llu last_rid=0x%08X field=%lu sub=%lu v0=%lu v1=%lu "
            "flag=%lu aux=%lu "
            "th_count=%lu\n",
            bcl.version, bcl.test_enabled, bcl.current_notify_type, bcl.parking_state,
            bcl.desired_parked_cpus, bcl.current_parked_cpus, bcl.throttle_permille,
            bcl.is_low_imax_state, bcl.mitigation_state, bcl.submit_cnt, bcl.submit_accepted,
            bcl.submit_rejected, bcl.work_item_run_cnt, bcl.threshold_op_cnt,
            bcl.threshold_downstack_success, bcl.threshold_downstack_failure,
            bcl.threshold_cache_hits, bcl.threshold_cache_misses, bcl.event_sequence,
            bcl.history_cnt, bcl.history_dropped, bcl_event_name(bcl.last_event_code),
            bcl.last_event_code, (ULONG)bcl.last_event_status, bcl.last_event_tick100ns,
            bcl.last_res_id, bcl.last_field, bcl.last_sub_type, bcl.last_value0, bcl.last_value1,
            bcl.last_flag, bcl.last_aux0, bcl.threshold_cnt);

        ULONG th_limit = bcl.threshold_cnt;
        if (th_limit > 6)
            th_limit = 6;
        for (ULONG i = 0; i < th_limit; i++) {
            append_fmt_a(text, text_cap, &off,
                         "  bcl_th[%lu]: rid=0x%08X field=%lu sub=%lu v0=%lu v1=%lu upd=%lu "
                         "st=0x%08X t=%llu flag=%lu\n",
                         i, bcl.thresholds[i].res_id, bcl.thresholds[i].field,
                         bcl.thresholds[i].sub_type, bcl.thresholds[i].value0,
                         bcl.thresholds[i].value1, bcl.thresholds[i].update_cnt,
                         (ULONG)bcl.thresholds[i].last_status, bcl.thresholds[i].last_tick100ns,
                         (ULONG)bcl.thresholds[i].flag);
        }

        bcl_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(bcl_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "bcl_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X notif=%lu park=%lu "
                             "desired=%lu rid=0x%08X "
                             "field=%lu sub=%lu v0=%lu v1=%lu flag=%lu aux=%lu\n",
                             hist[i].sequence, hist[i].tick100ns,
                             bcl_event_name(hist[i].event_code), hist[i].event_code,
                             (ULONG)hist[i].status, hist[i].notify_type, hist[i].parking_state,
                             hist[i].desired_parked_cpus, hist[i].res_id, hist[i].field,
                             hist[i].sub_type, hist[i].value0, hist[i].value1, hist[i].flag,
                             hist[i].aux0);
            }
        }
    }

    {
        parksel_snapshot_t park;
        RtlZeroMemory(&park, sizeof(park));
        if (NT_SUCCESS(pep_get_park_selection_snapshot(&park))) {
            append_fmt_a(
                text, text_cap, &off,
                "parksel: ver=%lu cpus=%lu active=%lu last_add=%lu eval_type=%lu eval_time=%llu "
                "cluster_eval0=%llu cluster_eval1=%llu timer0=%lu timer1=%lu rot0=%lu rot1=%lu "
                "toggle=%lu c0_unpark=%lu c1_unpark=%lu rails(pref=%lu timer=%lu num=%lu rot=%lu) "
                "seq=%llu hist=%lu drop=%lu last_ev=%lu/0x%08X add=%lu rem=%lu t=%llu "
                "thermal(en=%lu hyst_dC=%ld upd_ms=%lu sensor_base=%lu refresh=%lu apply=%lu "
                "st=0x%08X)\n",
                park.version, park.cpu_cnt, park.active_cpu_cnt, park.last_additional_unparked,
                park.last_evaluation_type, park.last_evaluation_time,
                park.last_cluster0_evaluation_time, park.last_cluster1_evaluation_time,
                park.timer_threshold0_sec, park.timer_threshold1_sec, park.core_rotation_stat0,
                park.core_rotation_stat1, park.toggle_state, park.cluster0_unparked,
                park.cluster1_unparked, park.pref_rail_id, park.timer_rail_id,
                park.num_unpark_rail_id, park.core_rotation_rail_id, park.event_sequence,
                park.history_cnt, park.history_dropped, park.last_event_code,
                (ULONG)park.last_event_status, park.last_event_additional_unparked,
                park.last_event_remaining_unparked, park.last_event_tick100ns,
                park.thermal_rank_enabled, park.thermal_rank_hysteresis_deci_c,
                park.thermal_rank_update_interval_ms, park.thermal_sensor_base_idx,
                park.thermal_refresh_cnt, park.thermal_apply_cnt, (ULONG)park.last_thermal_status);

            ULONG cpu_limit = park.cpu_cnt;
            if (cpu_limit > PARKSEL_MAX_CPU_SNAPSHOT)
                cpu_limit = PARKSEL_MAX_CPU_SNAPSHOT;

            ULONG rank_limit = park.active_cpu_cnt;
            if (rank_limit == 0 || rank_limit > PARKSEL_MAX_CPU_SNAPSHOT)
                rank_limit = cpu_limit;
            for (ULONG i = 0; i < rank_limit; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  parksel_rank[%lu]: pref=%u prev_pref=%u temp_dC=%ld\n", i,
                             (ULONG)park.rank_preference[i],
                             (ULONG)park.previous_rank_preference[i], park.rank_cpu_temp_deci_c[i]);
            }

            for (ULONG i = 0; i < cpu_limit; i++) {
                ULONG rank = park.current_rank[i];
                LONG temp_deci_c = 0;
                if (rank < PARKSEL_MAX_CPU_SNAPSHOT)
                    temp_deci_c = park.rank_cpu_temp_deci_c[rank];

                append_fmt_a(
                    text, text_cap, &off,
                    "  parksel_cpu[%lu]: park=%u prev=%u pref=%u rank=%u trans=%lu temp_dC=%ld\n",
                    i, (ULONG)park.current_park_state[i], (ULONG)park.previous_park_state[i],
                    (ULONG)park.current_preference[i], (ULONG)park.current_rank[i],
                    park.transition_cnt[i], temp_deci_c);
            }

            parksel_event_t hist[8];
            ULONG hist_cnt = 0;
            ULONG hist_dropped = 0;
            RtlZeroMemory(hist, sizeof(hist));
            if (NT_SUCCESS(pep_get_park_selection_history(hist, RTL_NUMBER_OF(hist), &hist_cnt,
                                                          &hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "parksel_history: count=%lu dropped=%lu\n",
                             hist_cnt, hist_dropped);
                for (ULONG i = 0; i < hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  seq=%llu t=%llu ev=%lu st=0x%08X cpu=%lu cnt=%lu add=%lu "
                                 "rem=%lu eval=%lu "
                                 "eval_t=%llu c0=%lu c1=%lu aux=%lu/%lu\n",
                                 hist[i].sequence, hist[i].tick100ns, hist[i].event_code,
                                 (ULONG)hist[i].status, hist[i].cpu, hist[i].cpu_cnt,
                                 hist[i].additional_unparked, hist[i].remaining_unparked,
                                 hist[i].evaluation_type, hist[i].evaluation_time,
                                 hist[i].cluster0_unparked, hist[i].cluster1_unparked, hist[i].aux0,
                                 hist[i].aux1);
                }
            }
        }
    }

    {
        interrupt_snapshot_t it;
        RtlZeroMemory(&it, sizeof(it));
        interrupt_get_snapshot(&it);

        append_fmt_a(
            text, text_cap, &off,
            "interrupt: ver=%lu isr=%lu dpc=%lu en=%lu dis=%lu dq=%lu last_msg=%lu "
            "last_isr=%llu last_dpc=%llu idle_scan=%lu idle_wake=%lu idle_lvl=%lu idle_mode=%lu "
            "wake=%08X/%08X cmd=%08X/%08X map=%lu hit=%lu miss=%lu lookup=%lu->%ld trig=%lu "
            "drv=%lu "
            "wake_bits=%llu last_wake=%lu irq=%lu seq=%llu hist=%lu drop=%lu "
            "last_ev=%s(%lu)/0x%08X t=%llu ports=%lu wake_top=%lu\n",
            it.version, it.isr_cnt, it.dpc_cnt, it.enable_cnt, it.disable_cnt, it.dpc_queued_cnt,
            it.last_message_id, it.last_isr_tick100ns, it.last_dpc_tick100ns, it.idle_scan_cnt,
            it.idle_wake_sample_cnt, it.last_idle_level, it.last_idle_mode_idx,
            it.last_wake_irq_status, it.last_wake_geni_status, it.last_wake_cmd0,
            it.last_wake_cmd_ctrl, it.map_cnt, it.map_hit_cnt, it.map_miss_cnt,
            it.last_lookup_interrupt_id, it.last_lookup_offset, it.last_lookup_trigger_type,
            it.last_lookup_driver_id, it.wake_bit_hit_cnt, it.last_wake_offset,
            it.last_wake_interrupt_id, it.event_sequence, it.history_cnt, it.history_dropped,
            interrupt_event_name(it.last_event_code), it.last_event_code,
            (ULONG)it.last_event_status, it.last_event_tick100ns, it.port_cnt, it.wake_top_cnt);

        ULONG port_limit = it.port_cnt;
        if (port_limit > 8)
            port_limit = 8;

        for (ULONG i = 0; i < port_limit; i++) {
            append_fmt_a(text, text_cap, &off,
                         "  intr_port[%lu]: id=%lu isr=%lu dpc=%lu en=%lu dis=%lu dq=%lu msg=%lu "
                         "isr_t=%llu dpc_t=%llu\n",
                         i, it.ports[i].port_id, it.ports[i].isr_cnt, it.ports[i].dpc_cnt,
                         it.ports[i].enable_cnt, it.ports[i].disable_cnt,
                         it.ports[i].dpc_queued_cnt, it.ports[i].last_message_id,
                         it.ports[i].last_isr_tick100ns, it.ports[i].last_dpc_tick100ns);
        }

        ULONG wake_top_limit = it.wake_top_cnt;
        if (wake_top_limit > 12)
            wake_top_limit = 12;
        for (ULONG i = 0; i < wake_top_limit; i++) {
            append_fmt_a(text, text_cap, &off, "  intr_wake_top[%lu]: off=%lu irq=%lu hits=%lu\n",
                         i, it.wake_top[i].offset, it.wake_top[i].interrupt_id,
                         it.wake_top[i].hit_cnt);
        }

        interrupt_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(
                interrupt_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "interrupt_history: count=%lu dropped=%lu\n",
                         hist_cnt, hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X port=%lu msg=%lu irq=%lu map=%ld "
                    "v=%lu,%lu,%lu,%lu\n",
                    hist[i].sequence, hist[i].tick100ns, interrupt_event_name(hist[i].event_code),
                    hist[i].event_code, (ULONG)hist[i].status, hist[i].port_id, hist[i].message_id,
                    hist[i].interrupt_id, hist[i].map_offset, hist[i].value0, hist[i].value1,
                    hist[i].value2, hist[i].value3);
            }
        }

        interrupt_wake_counter_t wake_counters[16];
        ULONG wake_counter_cnt = 0;
        RtlZeroMemory(wake_counters, sizeof(wake_counters));
        if (NT_SUCCESS(interrupt_get_wake_counters(wake_counters, RTL_NUMBER_OF(wake_counters),
                                                   &wake_counter_cnt))) {
            append_fmt_a(text, text_cap, &off, "interrupt_wake_counters: count=%lu\n",
                         wake_counter_cnt);
            for (ULONG i = 0; i < wake_counter_cnt; i++) {
                append_fmt_a(text, text_cap, &off, "  off=%lu irq=%lu hits=%lu\n",
                             wake_counters[i].offset, wake_counters[i].interrupt_id,
                             wake_counters[i].hit_cnt);
            }
        }
    }

    append_fmt_a(text, text_cap, &off, "subsystems:\n");
    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        soc_subsystem_stats_t st;
        BOOLEAN enabled = FALSE;
        RtlZeroMemory(&st, sizeof(st));
        (VOID) soc_subsys_get_info(i, NULL, NULL, NULL, &enabled, &st);
        append_fmt_a(text, text_cap, &off,
                     "  idx=%lu enabled=%lu wake=%lu blocked=%llu sleep=%llu total=%llu\n", i,
                     enabled ? 1u : 0u, st.wakeup_cnt, st.blocked_duration100ns,
                     st.sleep_duration100ns, st.blocked_duration100ns + st.sleep_duration100ns);
    }

    {
        socsubsys_snapshot_t ss;
        RtlZeroMemory(&ss, sizeof(ss));
        soc_subsys_get_snapshot(&ss);

        append_fmt_a(text, text_cap, &off,
                     "socsubsys: ver=%lu drips_state=%lu mask=0x%08X count=%lu in_drips=%lu "
                     "blocked=%lu reason=%lu "
                     "last_reason=%lu cur_block=%llu boot=%llu usb=%llu dbg=%llu other=%llu "
                     "aoss=%llu ddr=%llu "
                     "seq=%llu hist=%lu drop=%lu last_ev=%s(%lu)/0x%08X idx=%lu rsn=%lu t=%llu\n",
                     ss.version, ss.drips_state_id, ss.enabled_mask, ss.subsystem_cnt, ss.in_drips,
                     ss.drips_blocked, ss.drips_blocked_reason, ss.last_blocked_reason,
                     ss.current_blocked_duration100ns, ss.blocked_boot100ns, ss.blocked_usb100ns,
                     ss.blocked_dbg100ns, ss.blocked_other100ns, ss.aoss_sleep_count100ns,
                     ss.ddr_sleep_count100ns, ss.event_sequence, ss.history_cnt, ss.history_dropped,
                     soc_event_name(ss.last_event_code), ss.last_event_code,
                     (ULONG)ss.last_event_status, ss.last_event_subsystem_idx, ss.last_event_reason,
                     ss.last_event_tick100ns);

        ULONG entry_limit = ss.subsystem_cnt;
        if (entry_limit > SOC_SUBSYS_CNT)
            entry_limit = SOC_SUBSYS_CNT;
        if (entry_limit > 8)
            entry_limit = 8;

        for (ULONG i = 0; i < entry_limit; i++) {
            append_fmt_a(
                text, text_cap, &off,
                "  soc[%lu]: wake=%lu blocked=%llu sleep=%llu enter=%llu exit=%llu md=%lu "
                "flags=0x%08X "
                "island_count=%llu island_enter=%llu island_exit=%llu island_dur=%llu\n",
                i, ss.entries[i].stats.wakeup_cnt, ss.entries[i].stats.blocked_duration100ns,
                ss.entries[i].stats.sleep_duration100ns, ss.entries[i].last_entered_at100ns,
                ss.entries[i].last_exited_at100ns, ss.entries[i].metadata_cnt, ss.entries[i].flags,
                ss.entries[i].island_entry_cnt, ss.entries[i].island_entry_time100ns,
                ss.entries[i].island_exit_time100ns, ss.entries[i].island_accum_duration100ns);
        }

        socsubsys_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(
                soc_subsys_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "socsubsys_history: count=%lu dropped=%lu\n",
                         hist_cnt, hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X idx=%lu reason=%lu "
                             "in_drips=%lu wake=%lu "
                             "blocked=%llu sleep=%llu aux=%llu\n",
                             hist[i].sequence, hist[i].tick100ns,
                             soc_event_name(hist[i].event_code), hist[i].event_code,
                             (ULONG)hist[i].status, hist[i].subsystem_idx, hist[i].reason,
                             hist[i].in_drips, hist[i].wakeup_cnt, hist[i].blocked_duration100ns,
                             hist[i].sleep_duration100ns, hist[i].aux100ns);
            }
        }
    }

    {
        deep_standby_snapshot_t ds;
        RtlZeroMemory(&ds, sizeof(ds));
        if (NT_SUCCESS(deep_standby_get_snapshot(&ds))) {
            append_fmt_a(
                text, text_cap, &off,
                "deepstandby: feat=%lu cb=%lu modem=%lu wlan=%lu pwr=%lu scr=%lu usr=%lu msdc=%lu "
                "ds=%lu "
                "tmr_exp=%lu batt_lim=%lu batt_drop=%lu batt_cur=%lu timeout=%lu modem_req=%lu\n",
                ds.feature_enabled, ds.callbacks_enabled, ds.modem_enabled, ds.wlan_enabled,
                ds.power_src, ds.screen_state, ds.user_state, ds.ms_on_dc_pwr,
                ds.deep_standby_state, ds.timer_expired, ds.batt_drain_exceeded_limit,
                ds.batt_drain_since_tmr_start, ds.current_batt_pct, ds.timeout_seconds,
                ds.modem_req);
            append_fmt_a(text, text_cap, &off,
                         "deepstandby_stats: eval=%lu cb=%lu trans=%lu enter=%lu exit=%lu "
                         "wlan_err=%lu modem_err=%lu last_ev=%lu/0x%08X hist=%lu drop=%lu\n",
                         ds.evaluate_cnt, ds.callback_cnt, ds.state_transition_cnt, ds.enter_cnt,
                         ds.exit_cnt, ds.wlan_ioctl_errors, ds.modem_ioctl_errors,
                         ds.last_event_code, ds.last_event_status, ds.history_cnt,
                         ds.history_dropped);
        }

        deep_standby_history_entry_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(
                deep_standby_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "deepstandby_history: count=%lu dropped=%lu\n",
                         hist_cnt, hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                const deep_standby_history_entry_t* e = &hist[i];
                append_fmt_a(text, text_cap, &off,
                             "  t=%llu ev=%s(%lu) st=0x%08X ms=%lu->%lu ds=%lu->%lu aux=%lu,%lu\n",
                             e->tick100ns, ds_event_name(e->event_code), e->event_code, e->status,
                             e->prev_ms_on_dc_pwr, e->curr_ms_on_dc_pwr, e->prev_deep_standby_state,
                             e->curr_deep_standby_state, e->aux0, e->aux1);
            }
        }
    }

    {
        dcvs_snapshot_t dcvs;
        RtlZeroMemory(&dcvs, sizeof(dcvs));
        dcvs_get_snapshot(&dcvs);

        append_fmt_a(text, text_cap, &off,
                     "dcvs: ver=%lu init=%lu cpu=%lu domain=%lu members=0x%08X coord=%lu "
                     "lat=%lu ovh=%lu perf_sel=%lu thermal_sel=%lu boost=%lu boost_hz=%lu "
                     "max=%lu min=%lu nom=%lu lnl=%lu thermal=%lu/%lu cap=%lu/%lu cur_cap=%lu "
                     "policy_updates=%lu policy_t=%llu seq=%llu hist=%lu drop=%lu "
                     "last_ev=%s(%lu)/0x%08X cpu=%lu min=%lu max=%lu desired=%lu level=%lu hz=%lu "
                     "cap=%lu epp=%lu "
                     "aux=%lu,%lu t=%llu\n",
                     dcvs.version, dcvs.initialized, dcvs.cpu_cnt, dcvs.domain_id,
                     dcvs.domain_members, dcvs.coordination_type,
                     dcvs.worst_case_transition_latency, dcvs.worst_case_transition_overhead,
                     dcvs.enable_perf_selection, dcvs.enable_thermal_perf_selection,
                     dcvs.enable_boost, dcvs.boost_freq_hz, dcvs.max_freq_hz, dcvs.min_freq_hz,
                     dcvs.nominal_freq_hz, dcvs.lowest_nonlinear_freq_hz, dcvs.thermal_hot_deci_c,
                     dcvs.thermal_critical_deci_c, dcvs.thermal_cap_hot_percent,
                     dcvs.thermal_cap_critical_percent, dcvs.current_thermal_cap_percent,
                     dcvs.policy_update_cnt, dcvs.last_policy_tick100ns, dcvs.event_sequence,
                     dcvs.history_cnt, dcvs.history_dropped, dcvs_event_name(dcvs.last_event_code),
                     dcvs.last_event_code, (ULONG)dcvs.last_event_status, dcvs.last_event_cpu,
                     dcvs.last_event_min_perf_state, dcvs.last_event_max_perf_state,
                     dcvs.last_event_desired_perf_state, dcvs.last_event_applied_perf_level,
                     dcvs.last_event_applied_freq_hz, dcvs.last_event_thermal_cap_percent,
                     dcvs.last_event_epp, dcvs.last_event_aux0, dcvs.last_event_aux1,
                     dcvs.last_event_tick100ns);

        ULONG perf_limit = dcvs.perf_table_cnt;
        if (perf_limit > 16)
            perf_limit = 16;
        for (ULONG i = 0; i < perf_limit; i++) {
            append_fmt_a(text, text_cap, &off, "  dcvs_perf[%lu]=%lu\n", i, dcvs.perf_table_hz[i]);
        }

        ULONG cpu_limit = dcvs.cpu_entry_cnt;
        if (cpu_limit > 8)
            cpu_limit = 8;
        for (ULONG i = 0; i < cpu_limit; i++) {
            append_fmt_a(
                text, text_cap, &off,
                "  dcvs_cpu[%lu]: dom=%lu level=%lu hz=%lu req=%lu min=%lu max=%lu des=%lu epp=%lu "
                "enter=%llu resid=%llu trans=%llu perf=%lu\n",
                dcvs.cpus[i].cpu, dcvs.cpus[i].domain_idx, dcvs.cpus[i].current_perf_level,
                dcvs.cpus[i].current_freq_hz, dcvs.cpus[i].requested_perf_state_idx,
                dcvs.cpus[i].min_perf, dcvs.cpus[i].max_perf, dcvs.cpus[i].desired_perf,
                dcvs.cpus[i].epp, dcvs.cpus[i].enter_time100ns, dcvs.cpus[i].residency100ns,
                dcvs.cpus[i].transition_cnt, dcvs.cpus[i].normalized_performance);
        }

        dcvs_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(dcvs_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "dcvs_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X cpu=%lu min=%lu max=%lu des=%lu "
                    "level=%lu hz=%lu cap=%lu epp=%lu aux=%lu,%lu\n",
                    hist[i].sequence, hist[i].tick100ns, dcvs_event_name(hist[i].event_code),
                    hist[i].event_code, (ULONG)hist[i].status, hist[i].cpu, hist[i].min_perf_state,
                    hist[i].max_perf_state, hist[i].desired_perf_state, hist[i].applied_perf_level,
                    hist[i].applied_freq_hz, hist[i].thermal_cap_percent, hist[i].epp, hist[i].aux0,
                    hist[i].aux1);
            }
        }
    }

    {
        crash_snapshot_t crash;
        RtlZeroMemory(&crash, sizeof(crash));
        crash_dump_get_snapshot(&crash);

        append_fmt_a(
            text, text_cap, &off,
            "crashdump: ver=%lu active=%lu in_use=%lu src_cap=%lu create=%lu open=%lu destroy=%lu "
            "write=%lu read=%lu capture=%lu lkd=%lu seq=%llu hist=%lu drop=%lu "
            "last_ev=%s(%lu)/0x%08X src=%lu size=%lu t=%llu\n",
            crash.version, crash.active_src, crash.in_use_src_cnt, crash.src_capacity,
            crash.create_cnt, crash.open_cnt, crash.destroy_cnt, crash.write_cnt, crash.read_cnt,
            crash.capture_cnt, crash.live_kernel_create_cnt, crash.event_sequence,
            crash.history_cnt, crash.history_dropped, crash_event_name(crash.last_event_code),
            crash.last_event_code, (ULONG)crash.last_event_status, crash.last_event_src_idx,
            crash.last_event_size, crash.last_event_tick100ns);

        ULONG src_limit = crash.src_capacity;
        if (src_limit > CRASH_SRC_CAPACITY)
            src_limit = CRASH_SRC_CAPACITY;
        for (ULONG i = 0; i < src_limit; i++) {
            append_fmt_a(text, text_cap, &off,
                         "  crash_src[%lu]: in_use=%lu guid=%lu items=%lu entry=%lu blob=%lu/%lu "
                         "ring=%lu/%lu/%lu "
                         "last_wr=%llu create_fo=%lu open_fo=%lu orphan=%lu\n",
                         crash.sources[i].idx, crash.sources[i].in_use, crash.sources[i].guid_valid,
                         crash.sources[i].item_cnt, crash.sources[i].entry_size,
                         crash.sources[i].blob_len, crash.sources[i].blob_capacity,
                         crash.sources[i].ring_read_idx, crash.sources[i].ring_write_idx,
                         crash.sources[i].ring_items_present, crash.sources[i].last_write100ns,
                         crash.sources[i].has_create_file_object,
                         crash.sources[i].has_open_file_object,
                         crash.sources[i].create_file_object_orphaned);
        }

        crash_event_t hist[8];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(
                crash_dump_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "crashdump_history: count=%lu dropped=%lu\n",
                         hist_cnt, hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  seq=%llu t=%llu ev=%s(%lu) st=0x%08X src=%lu active=%lu size=%lu "
                             "cnt=%lu/%lu/%lu/%lu/%lu/%lu/%lu\n",
                             hist[i].sequence, hist[i].tick100ns,
                             crash_event_name(hist[i].event_code), hist[i].event_code,
                             (ULONG)hist[i].status, hist[i].src_idx, hist[i].active_src,
                             hist[i].size, hist[i].create_cnt, hist[i].open_cnt,
                             hist[i].destroy_cnt, hist[i].write_cnt, hist[i].read_cnt,
                             hist[i].capture_cnt, hist[i].live_kernel_create_cnt);
            }
        }
    }

    {
        res_branchtrack_snapshot_t bt;
        RtlZeroMemory(&bt, sizeof(bt));
        NTSTATUS st_bt = res_get_branch_track_snapshot(&bt);
        if (NT_SUCCESS(st_bt)) {
            append_fmt_a(
                text, text_cap, &off,
                "branchtrack: ver=%lu entries=%lu active=%lu seq=%llu hist=%lu drop=%lu "
                "last={rid=%08X st=%08X v=%08X,%08X,%08X,%08X active=%lu upd=%lu t=%llu}\n",
                bt.version, bt.entry_cnt, bt.active_cnt, bt.event_sequence, bt.history_cnt,
                bt.history_dropped, bt.last_event_res_id, bt.last_event_sub_type,
                bt.last_event_value0, bt.last_event_value1, bt.last_event_value2,
                bt.last_event_value3, bt.last_event_active, bt.last_event_update_cnt,
                bt.last_event_tick100ns);

            res_branchtrack_event_t history[8];
            ULONG hist_cnt = 0;
            ULONG hist_dropped = 0;
            RtlZeroMemory(history, sizeof(history));
            if (NT_SUCCESS(res_get_branch_track_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                                        &hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "branchtrack_history: count=%lu dropped=%lu\n",
                             hist_cnt, hist_dropped);
                for (ULONG i = 0; i < hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  seq=%llu t=%llu rid=%08X st=%08X v=%08X,%08X,%08X,%08X "
                                 "active=%lu upd=%lu\n",
                                 history[i].sequence, history[i].tick100ns, history[i].res_id,
                                 history[i].sub_type, history[i].value0, history[i].value1,
                                 history[i].value2, history[i].value3, history[i].active,
                                 history[i].update_cnt);
                }
            }
        } else {
            append_fmt_a(text, text_cap, &off, "branchtrack: snapshot_status=0x%08X\n",
                         (ULONG)st_bt);
        }
    }

    {
        rscsw_snapshot_t rs;
        RtlZeroMemory(&rs, sizeof(rs));
        NTSTATUS st = rsc_sw_get_snapshot(&rs);
        if (NT_SUCCESS(st)) {
            append_fmt_a(
                text, text_cap, &off,
                "rscsw: ver=%lu nodes=%lu active=0x%08X seq=%llu hist=%lu drop=%lu "
                "batch={apply=%lu cmds=%lu err=%lu last_count=%lu last_st=0x%08X last_t=%llu} "
                "scenario={run=%lu fail=%lu last_id=%lu(%s) last_st=0x%08X last_iter=%lu "
                "last_t=%llu} "
                "last={sub=%lu ev=%s(%lu) st=0x%08X io=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu "
                "flags=0x%08X t=%llu}\n",
                rs.version, rs.node_cnt, rs.active_mask, rs.event_sequence, rs.history_cnt,
                rs.history_dropped, rs.batch_apply_cnt, rs.batch_command_cnt, rs.batch_error_cnt,
                rs.last_batch_cnt, (ULONG)rs.last_batch_status, rs.last_batch_tick100ns,
                rs.scenario_run_cnt, rs.scenario_failure_cnt, rs.last_scenario_id,
                rsc_sw_scenario_name(rs.last_scenario_id), (ULONG)rs.last_scenario_status,
                rs.last_scenario_iterations, rs.last_scenario_tick100ns, rs.last_event_subsystem_id,
                rsc_sw_event_name(rs.last_event_code), rs.last_event_code,
                (ULONG)rs.last_event_status, rs.last_ioctl_code, rs.last_tag, rs.last_value0,
                rs.last_value1, rs.last_value2, rs.last_value3, rs.last_flags,
                rs.last_event_tick100ns);

            ULONG node_limit = rs.node_cnt;
            if (node_limit > RSCSW_NODE_CAPACITY)
                node_limit = RSCSW_NODE_CAPACITY;
            for (ULONG i = 0; i < node_limit; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  rscsw[%lu:%s]: present=%lu state=%lu clk=%lu flags=0x%08X req=%lu ok=%lu "
                    "fail=%lu vote=%lu cmd=%lu pend=%lu "
                    "last={st=0x%08X io=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu t=%llu} acc=%llu\n",
                    rs.nodes[i].subsystem_id, rs.nodes[i].name, rs.nodes[i].present,
                    rs.nodes[i].state, rs.nodes[i].clock_khz, rs.nodes[i].flags,
                    rs.nodes[i].req_cnt, rs.nodes[i].success_cnt, rs.nodes[i].failure_cnt,
                    rs.nodes[i].vote_cnt, rs.nodes[i].command_cnt, rs.nodes[i].pending_cnt,
                    (ULONG)rs.nodes[i].last_status, rs.nodes[i].last_ioctl_code,
                    rs.nodes[i].last_tag, rs.nodes[i].last_value0, rs.nodes[i].last_value1,
                    rs.nodes[i].last_value2, rs.nodes[i].last_value3, rs.nodes[i].last_tick100ns,
                    rs.nodes[i].activity_accumulator);
            }
        } else {
            append_fmt_a(text, text_cap, &off, "rscsw: snapshot_status=0x%08X\n", (ULONG)st);
        }

        rscsw_event_t hist[16];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(hist, sizeof(hist));
        if (NT_SUCCESS(rsc_sw_get_history(hist, RTL_NUMBER_OF(hist), &hist_cnt, &hist_dropped))) {
            append_fmt_a(text, text_cap, &off, "rscsw_history: count=%lu dropped=%lu\n", hist_cnt,
                         hist_dropped);
            for (ULONG i = 0; i < hist_cnt; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  seq=%llu t=%llu sub=%lu(%s) ev=%s(%lu) st=0x%08X io=0x%08X tag=0x%08X "
                    "v=%lu,%lu,%lu,%lu flags=0x%08X\n",
                    hist[i].sequence, hist[i].tick100ns, hist[i].subsystem_id,
                    rsc_sw_get_subsystem_name_a(hist[i].subsystem_id),
                    rsc_sw_event_name(hist[i].event_code), hist[i].event_code,
                    (ULONG)hist[i].status, hist[i].ioctl_code, hist[i].tag, hist[i].value0,
                    hist[i].value1, hist[i].value2, hist[i].value3, hist[i].flags);
            }
        }

        rscsw_catalog_entry_t cat[16];
        ULONG cat_cnt = 0;
        RtlZeroMemory(cat, sizeof(cat));
        if (NT_SUCCESS(rsc_sw_get_catalog(cat, RTL_NUMBER_OF(cat), &cat_cnt))) {
            append_fmt_a(text, text_cap, &off, "rscsw_catalog: count=%lu\n", cat_cnt);
            for (ULONG i = 0; i < cat_cnt; i++) {
                append_fmt_a(text, text_cap, &off,
                             "  cat[%lu]: sub=%lu name=%s cap=0x%08X def_state=%lu def_clk=%lu "
                             "def_flags=0x%08X tag=0x%08X ioctl=0x%08X\n",
                             i, cat[i].subsystem_id, cat[i].name, cat[i].cap_mask,
                             cat[i].default_state, cat[i].default_clock_khz, cat[i].default_flags,
                             cat[i].res_tag, cat[i].suggested_ioctl);
            }
        }

        rscsw_scenario_result_t scenarios[12];
        ULONG scenario_cnt = 0;
        ULONG scenario_dropped = 0;
        RtlZeroMemory(scenarios, sizeof(scenarios));
        if (NT_SUCCESS(rsc_sw_get_scenario_history(scenarios, RTL_NUMBER_OF(scenarios),
                                                   &scenario_cnt, &scenario_dropped))) {
            append_fmt_a(text, text_cap, &off, "rscsw_scenario_history: count=%lu dropped=%lu\n",
                         scenario_cnt, scenario_dropped);

            for (ULONG i = 0; i < scenario_cnt; i++) {
                append_fmt_a(
                    text, text_cap, &off,
                    "  t=%llu id=%lu(%s) iter=%lu flags=0x%08X st=0x%08X applied=%lu failed=%lu "
                    "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu\n",
                    scenarios[i].tick100ns, scenarios[i].scenario_id,
                    rsc_sw_scenario_name(scenarios[i].scenario_id), scenarios[i].iterations,
                    scenarios[i].flags, (ULONG)scenarios[i].status, scenarios[i].applied_nodes,
                    scenarios[i].failed_nodes, scenarios[i].last_subsystem_id,
                    rsc_sw_get_subsystem_name_a(scenarios[i].last_subsystem_id),
                    scenarios[i].last_tag, scenarios[i].last_value0, scenarios[i].last_value1,
                    scenarios[i].last_value2, scenarios[i].last_value3);
            }
        }

        {
            rscsw_diag_snapshot_t diag;
            RtlZeroMemory(&diag, sizeof(diag));
            if (NT_SUCCESS(rsc_sw_get_diagnostics(&diag))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_diag: ver=%lu entries=%lu matrix={runs=%lu fails=%lu "
                             "last_st=0x%08X last_t=%llu last_iter=%lu last_fail=%lu ",
                             diag.version, diag.entry_cnt, diag.matrix_run_cnt,
                             diag.matrix_failure_cnt, (ULONG)diag.last_matrix_status,
                             diag.last_matrix_tick100ns, diag.last_matrix_iterations,
                             diag.last_matrix_failures);
                append_diag_flags_a(text, text_cap, &off, diag.last_matrix_flags);
                append_fmt_a(text, text_cap, &off, "}\n");

                ULONG diag_cnt = diag.entry_cnt;
                if (diag_cnt > RSCSW_NODE_CAPACITY)
                    diag_cnt = RSCSW_NODE_CAPACITY;
                for (ULONG i = 0; i < diag_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  diag[%lu:%s]: drv=%lu ppp_type=%lu glink_seed=%lu icb_err=%lu "
                                 "health=%lu conf=%lu activity=%lu cap=0x%08X "
                                 "last_scn=0x%08X last_node=0x%08X\n",
                                 diag.entries[i].subsystem_id, diag.entries[i].name,
                                 diag.entries[i].driver_map_id, diag.entries[i].ppp_res_type,
                                 diag.entries[i].glink_event_seed, diag.entries[i].icb_error_code,
                                 diag.entries[i].health_score, diag.entries[i].confidence_score,
                                 diag.entries[i].activity_score, diag.entries[i].cap_mask,
                                 (ULONG)diag.entries[i].last_scenario_status,
                                 (ULONG)diag.entries[i].last_node_status);
                }
            }
        }

        {
            rscsw_diag_result_t diag_hist[12];
            ULONG diag_hist_cnt = 0;
            ULONG diag_hist_dropped = 0;
            RtlZeroMemory(diag_hist, sizeof(diag_hist));
            if (NT_SUCCESS(rsc_sw_get_diagnostic_history(diag_hist, RTL_NUMBER_OF(diag_hist),
                                                         &diag_hist_cnt, &diag_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_diag_history: count=%lu dropped=%lu\n",
                             diag_hist_cnt, diag_hist_dropped);
                for (ULONG i = 0; i < diag_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu iter=%lu st=0x%08X runs=%lu fails=%lu last_scn=%lu(%s) "
                                 "last_sub=%lu(%s) tag=0x%08X ",
                                 diag_hist[i].tick100ns, diag_hist[i].iterations,
                                 (ULONG)diag_hist[i].status, diag_hist[i].scenario_runs,
                                 diag_hist[i].scenario_failures, diag_hist[i].last_scenario_id,
                                 rsc_sw_scenario_name(diag_hist[i].last_scenario_id),
                                 diag_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(diag_hist[i].last_subsystem_id),
                                 diag_hist[i].last_tag);
                    append_diag_flags_a(text, text_cap, &off, diag_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_profile_snapshot_t profile;
            RtlZeroMemory(&profile, sizeof(profile));
            if (NT_SUCCESS(rsc_sw_get_profile_catalog(&profile))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_profile: ver=%lu count=%lu runs=%lu fails=%lu last={id=%lu(%s) "
                             "st=0x%08X iter=%lu ",
                             profile.version, profile.profile_cnt, profile.profile_run_cnt,
                             profile.profile_failure_cnt, profile.last_profile_id,
                             rsc_sw_profile_name(profile.last_profile_id),
                             (ULONG)profile.last_profile_status, profile.last_profile_iterations);
                append_profile_flags_a(text, text_cap, &off, profile.last_profile_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", profile.last_profile_tick100ns);

                ULONG profile_cnt = profile.profile_cnt;
                if (profile_cnt > RSCSW_NODE_CAPACITY)
                    profile_cnt = RSCSW_NODE_CAPACITY;
                for (ULONG i = 0; i < profile_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  profile[%lu:%s]: mask=0x%08X scen_flags=0x%08X diag_flags=0x%08X "
                        "def_iter=%lu pri=%lu\n",
                        profile.profiles[i].profile_id, profile.profiles[i].name,
                        profile.profiles[i].subsystem_mask, profile.profiles[i].scenario_flags,
                        profile.profiles[i].matrix_flags, profile.profiles[i].default_iterations,
                        profile.profiles[i].priority);
                }
            }
        }

        {
            rscsw_profile_result_t profile_hist[12];
            ULONG profile_hist_cnt = 0;
            ULONG profile_hist_dropped = 0;
            RtlZeroMemory(profile_hist, sizeof(profile_hist));
            if (NT_SUCCESS(rsc_sw_get_profile_history(profile_hist, RTL_NUMBER_OF(profile_hist),
                                                      &profile_hist_cnt, &profile_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_profile_history: count=%lu dropped=%lu\n",
                             profile_hist_cnt, profile_hist_dropped);
                for (ULONG i = 0; i < profile_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu id=%lu(%s) iter=%lu st=0x%08X scen={runs=%lu fails=%lu "
                                 "last_id=%lu(%s) last_sub=%lu(%s) tag=0x%08X} "
                                 "matrix={runs=%lu fails=%lu} ",
                                 profile_hist[i].tick100ns, profile_hist[i].profile_id,
                                 rsc_sw_profile_name(profile_hist[i].profile_id),
                                 profile_hist[i].iterations, (ULONG)profile_hist[i].status,
                                 profile_hist[i].scenario_runs, profile_hist[i].scenario_failures,
                                 profile_hist[i].last_scenario_id,
                                 rsc_sw_scenario_name(profile_hist[i].last_scenario_id),
                                 profile_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(profile_hist[i].last_subsystem_id),
                                 profile_hist[i].last_tag, profile_hist[i].matrix_runs,
                                 profile_hist[i].matrix_failures);
                    append_profile_flags_a(text, text_cap, &off, profile_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_graph_snapshot_t graph;
            RtlZeroMemory(&graph, sizeof(graph));
            if (NT_SUCCESS(rsc_sw_get_dependency_graph(&graph))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_graph: ver=%lu nodes=%lu links=%lu active=0x%08X "
                             "last_plan={st=0x%08X steps=%lu fail=%lu t=%llu}\n",
                             graph.version, graph.node_cnt, graph.link_cnt, graph.active_mask,
                             graph.last_action_sequence_status, graph.last_action_sequence_step_cnt,
                             graph.last_action_sequence_failure_cnt,
                             graph.last_action_sequence_tick100ns);

                ULONG link_cnt = graph.link_cnt;
                if (link_cnt > 24u)
                    link_cnt = 24u;
                for (ULONG i = 0; i < link_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  link[%lu]: %lu(%s) -> %lu(%s) w=%lu type=%s flags=0x%08X\n", i,
                                 graph.links[i].from_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(graph.links[i].from_subsystem_id),
                                 graph.links[i].to_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(graph.links[i].to_subsystem_id),
                                 graph.links[i].weight,
                                 rsc_sw_link_type_name(graph.links[i].link_type),
                                 graph.links[i].flags);
                }
            }
        }

        {
            rscsw_action_sequence_result_t action_sequence_hist[12];
            ULONG action_sequence_hist_cnt = 0;
            ULONG action_sequence_hist_dropped = 0;
            RtlZeroMemory(action_sequence_hist, sizeof(action_sequence_hist));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_history(
                    action_sequence_hist, RTL_NUMBER_OF(action_sequence_hist),
                    &action_sequence_hist_cnt, &action_sequence_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_plan_history: count=%lu dropped=%lu\n",
                             action_sequence_hist_cnt, action_sequence_hist_dropped);
                for (ULONG i = 0; i < action_sequence_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu req=%lu exec=%lu fail=%lu st=0x%08X last_sub=%lu(%s) tag=0x%08X "
                        "v=%lu,%lu,%lu,%lu\n",
                        action_sequence_hist[i].tick100ns, action_sequence_hist[i].requested_steps,
                        action_sequence_hist[i].executed_steps,
                        action_sequence_hist[i].failed_steps, (ULONG)action_sequence_hist[i].status,
                        action_sequence_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(action_sequence_hist[i].last_subsystem_id),
                        action_sequence_hist[i].last_tag, action_sequence_hist[i].last_value0,
                        action_sequence_hist[i].last_value1, action_sequence_hist[i].last_value2,
                        action_sequence_hist[i].last_value3);
                }
            }
        }

        {
            rscsw_action_sequence_preview_snapshot_t preview;
            RtlZeroMemory(&preview, sizeof(preview));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_preview_snapshot(&preview))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_plan_preview: ver=%lu run=%lu fail=%lu last={st=0x%08X req=%lu "
                             "exp=%lu invalid=%lu dup=%lu dep=%lu reorder=%lu "
                             "mask=%08X->%08X ",
                             preview.version, preview.run_cnt, preview.failure_cnt,
                             (ULONG)preview.last_status, preview.last_requested_steps,
                             preview.last_expanded_steps, preview.last_invalid_steps,
                             preview.last_duplicate_steps, preview.last_dependency_edges,
                             preview.last_reordered_steps, preview.last_active_mask_before,
                             preview.last_active_mask_after);
                append_fmt_a(text, text_cap, &off, "flags=0x%08X t=%llu}\n", preview.last_flags,
                             preview.last_tick100ns);
            }
        }

        {
            rscsw_action_sequence_preview_result_t preview_hist[12];
            ULONG preview_hist_cnt = 0;
            ULONG preview_hist_dropped = 0;
            RtlZeroMemory(preview_hist, sizeof(preview_hist));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_preview_history(
                    preview_hist, RTL_NUMBER_OF(preview_hist), &preview_hist_cnt,
                    &preview_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_plan_preview_history: count=%lu dropped=%lu\n",
                             preview_hist_cnt, preview_hist_dropped);
                for (ULONG i = 0; i < preview_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu req=%lu exp=%lu invalid=%lu dup=%lu dep=%lu reorder=%lu "
                                 "mask=%08X->%08X st=0x%08X "
                                 "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu ",
                                 preview_hist[i].tick100ns, preview_hist[i].requested_steps,
                                 preview_hist[i].expanded_steps, preview_hist[i].invalid_steps,
                                 preview_hist[i].duplicate_steps, preview_hist[i].dependency_edges,
                                 preview_hist[i].reordered_steps,
                                 preview_hist[i].active_mask_before,
                                 preview_hist[i].active_mask_after, (ULONG)preview_hist[i].status,
                                 preview_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(preview_hist[i].last_subsystem_id),
                                 preview_hist[i].last_tag, preview_hist[i].last_value0,
                                 preview_hist[i].last_value1, preview_hist[i].last_value2,
                                 preview_hist[i].last_value3);
                    append_fmt_a(text, text_cap, &off, "flags=0x%08X\n", preview_hist[i].flags);
                }
            }
        }

        {
            rscsw_coverage_snapshot_t coverage;
            RtlZeroMemory(&coverage, sizeof(coverage));
            if (NT_SUCCESS(rsc_sw_get_coverage_snapshot(&coverage))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_coverage: ver=%lu nodes=%lu active=0x%08X run=%lu fail=%lu "
                             "last={st=0x%08X health=%lu activity=%lu warn=%lu err=%lu thr=%lu ",
                             coverage.version, coverage.node_cnt, coverage.active_mask,
                             coverage.run_cnt, coverage.failure_cnt, (ULONG)coverage.last_status,
                             coverage.last_health_score, coverage.last_activity_score,
                             coverage.last_warning_cnt, coverage.last_error_cnt,
                             coverage.last_threshold_permille);
                append_coverage_flags_a(text, text_cap, &off, coverage.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", coverage.last_tick100ns);

                ULONG coverage_cnt = coverage.node_cnt;
                if (coverage_cnt > RSCSW_NODE_CAPACITY)
                    coverage_cnt = RSCSW_NODE_CAPACITY;
                for (ULONG i = 0; i < coverage_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  cov[%lu:%s]: present=%lu active=%lu req=%lu ok=%lu fail=%lu "
                        "succ_permille=%lu dep_in=%lu dep_out=%lu "
                        "health=%lu activity=%lu flags=0x%08X last_st=0x%08X last_t=%llu\n",
                        coverage.rows[i].subsystem_id, coverage.rows[i].name,
                        coverage.rows[i].present, coverage.rows[i].active, coverage.rows[i].req_cnt,
                        coverage.rows[i].success_cnt, coverage.rows[i].failure_cnt,
                        coverage.rows[i].success_permille, coverage.rows[i].dependency_in,
                        coverage.rows[i].dependency_out, coverage.rows[i].health_score,
                        coverage.rows[i].activity_score, coverage.rows[i].flags,
                        (ULONG)coverage.rows[i].last_status, coverage.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_coverage_result_t coverage_hist[12];
            ULONG coverage_hist_cnt = 0;
            ULONG coverage_hist_dropped = 0;
            RtlZeroMemory(coverage_hist, sizeof(coverage_hist));
            if (NT_SUCCESS(rsc_sw_get_coverage_history(coverage_hist, RTL_NUMBER_OF(coverage_hist),
                                                       &coverage_hist_cnt,
                                                       &coverage_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_coverage_history: count=%lu dropped=%lu\n", coverage_hist_cnt,
                             coverage_hist_dropped);
                for (ULONG i = 0; i < coverage_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu nodes=%lu st=0x%08X health=%lu activity=%lu warn=%lu err=%lu "
                        "below=%lu thr=%lu mask=%08X->%08X "
                        "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        coverage_hist[i].tick100ns, coverage_hist[i].node_cnt,
                        (ULONG)coverage_hist[i].status, coverage_hist[i].health_score,
                        coverage_hist[i].activity_score, coverage_hist[i].warning_cnt,
                        coverage_hist[i].error_cnt, coverage_hist[i].below_threshold_cnt,
                        coverage_hist[i].threshold_permille, coverage_hist[i].active_mask_before,
                        coverage_hist[i].active_mask_after, coverage_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(coverage_hist[i].last_subsystem_id),
                        coverage_hist[i].last_tag, coverage_hist[i].last_value0,
                        coverage_hist[i].last_value1, coverage_hist[i].last_value2,
                        coverage_hist[i].last_value3);
                    append_coverage_flags_a(text, text_cap, &off, coverage_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_remediation_snapshot_t remediation;
            RtlZeroMemory(&remediation, sizeof(remediation));
            if (NT_SUCCESS(rsc_sw_get_remediation_snapshot(&remediation))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rscsw_remediation: ver=%lu run=%lu fail=%lu last={st=0x%08X rec=%lu exec=%lu "
                    "apply_fail=%lu "
                    "cov={thr=%lu health=%lu activity=%lu warn=%lu err=%lu worst=%lu(%s)} ",
                    remediation.version, remediation.run_cnt, remediation.failure_cnt,
                    (ULONG)remediation.last_status, remediation.last_recommended_cnt,
                    remediation.last_executed_cnt, remediation.last_failed_cnt,
                    remediation.last_coverage_threshold_permille,
                    remediation.last_coverage_health_score,
                    remediation.last_coverage_activity_score, remediation.last_warning_cnt,
                    remediation.last_error_cnt, remediation.last_worst_subsystem_id,
                    rsc_sw_get_subsystem_name_a(remediation.last_worst_subsystem_id));
                append_remediation_flags_a(text, text_cap, &off, remediation.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", remediation.last_tick100ns);

                ULONG action_cnt = remediation.action_cnt;
                if (action_cnt > 24u)
                    action_cnt = 24u;
                for (ULONG i = 0; i < action_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  remediate[%lu:%s]: pri=%lu state=%lu->%lu clk=%lu->%lu "
                        "flags=0x%08X->0x%08X "
                        "health=%lu activity=%lu succ_permille=%lu dep=%lu/%lu last_st=0x%08X ",
                        remediation.actions[i].subsystem_id, remediation.actions[i].name,
                        remediation.actions[i].priority, remediation.actions[i].current_state,
                        remediation.actions[i].recommended_state,
                        remediation.actions[i].current_clock_khz,
                        remediation.actions[i].recommended_clock_khz,
                        remediation.actions[i].current_flags,
                        remediation.actions[i].recommended_flags,
                        remediation.actions[i].health_score, remediation.actions[i].activity_score,
                        remediation.actions[i].success_permille,
                        remediation.actions[i].dependency_in, remediation.actions[i].dependency_out,
                        (ULONG)remediation.actions[i].last_status);
                    append_remediation_reason_flags_a(text, text_cap, &off,
                                                      remediation.actions[i].reason_flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_remediation_result_t remediation_hist[12];
            ULONG remediation_hist_cnt = 0;
            ULONG remediation_hist_dropped = 0;
            RtlZeroMemory(remediation_hist, sizeof(remediation_hist));
            if (NT_SUCCESS(rsc_sw_get_remediation_history(
                    remediation_hist, RTL_NUMBER_OF(remediation_hist), &remediation_hist_cnt,
                    &remediation_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_remediation_history: count=%lu dropped=%lu\n",
                             remediation_hist_cnt, remediation_hist_dropped);
                for (ULONG i = 0; i < remediation_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X rec=%lu exec=%lu apply_fail=%lu cov={thr=%lu "
                        "health=%lu activity=%lu warn=%lu err=%lu "
                        "worst=%lu(%s)} last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        remediation_hist[i].tick100ns, (ULONG)remediation_hist[i].status,
                        remediation_hist[i].recommended_cnt, remediation_hist[i].executed_cnt,
                        remediation_hist[i].failed_cnt,
                        remediation_hist[i].coverage_threshold_permille,
                        remediation_hist[i].coverage_health_score,
                        remediation_hist[i].coverage_activity_score,
                        remediation_hist[i].warning_cnt, remediation_hist[i].error_cnt,
                        remediation_hist[i].worst_subsystem_id,
                        rsc_sw_get_subsystem_name_a(remediation_hist[i].worst_subsystem_id),
                        remediation_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(remediation_hist[i].last_subsystem_id),
                        remediation_hist[i].last_tag, remediation_hist[i].last_value0,
                        remediation_hist[i].last_value1, remediation_hist[i].last_value2,
                        remediation_hist[i].last_value3);
                    append_remediation_flags_a(text, text_cap, &off, remediation_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_stabilization_snapshot_t stabilization;
            RtlZeroMemory(&stabilization, sizeof(stabilization));
            if (NT_SUCCESS(rsc_sw_get_stabilization_snapshot(&stabilization))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rscsw_stabilization: ver=%lu run=%lu fail=%lu last={st=0x%08X lanes=%lu "
                    "degraded=%lu rem_st=0x%08X "
                    "cov={health=%lu activity=%lu warn=%lu err=%lu} profiles={run=%lu fail=%lu} ",
                    stabilization.version, stabilization.run_cnt, stabilization.failure_cnt,
                    (ULONG)stabilization.last_status, stabilization.last_lane_cnt,
                    stabilization.last_degraded_lane_cnt,
                    (ULONG)stabilization.last_remediation_status,
                    stabilization.last_coverage_health_score,
                    stabilization.last_coverage_activity_score,
                    stabilization.last_coverage_warning_cnt, stabilization.last_coverage_error_cnt,
                    stabilization.last_profile_runs, stabilization.last_profile_failures);
                append_stabilization_flags_a(text, text_cap, &off, stabilization.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", stabilization.last_tick100ns);

                ULONG lane_cnt = stabilization.lane_cnt;
                if (lane_cnt > RSCSW_STABILIZATION_LANE_CAPACITY)
                    lane_cnt = RSCSW_STABILIZATION_LANE_CAPACITY;
                for (ULONG i = 0; i < lane_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  lane[%lu:%s]: mask=0x%08X nodes=%lu degraded=%lu health=%lu "
                        "activity=%lu warn=%lu err=%lu "
                        "st=0x%08X t=%llu\n",
                        stabilization.lanes[i].lane_id, stabilization.lanes[i].name,
                        stabilization.lanes[i].subsystem_mask, stabilization.lanes[i].node_cnt,
                        stabilization.lanes[i].degraded_cnt, stabilization.lanes[i].health_score,
                        stabilization.lanes[i].activity_score, stabilization.lanes[i].warning_cnt,
                        stabilization.lanes[i].error_cnt, (ULONG)stabilization.lanes[i].last_status,
                        stabilization.lanes[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_stabilization_result_t stabilization_hist[12];
            ULONG stabilization_hist_cnt = 0;
            ULONG stabilization_hist_dropped = 0;
            RtlZeroMemory(stabilization_hist, sizeof(stabilization_hist));
            if (NT_SUCCESS(rsc_sw_get_stabilization_history(
                    stabilization_hist, RTL_NUMBER_OF(stabilization_hist), &stabilization_hist_cnt,
                    &stabilization_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_stabilization_history: count=%lu dropped=%lu\n",
                             stabilization_hist_cnt, stabilization_hist_dropped);
                for (ULONG i = 0; i < stabilization_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X lanes=%lu degraded=%lu rem_st=0x%08X cov={health=%lu "
                        "activity=%lu warn=%lu err=%lu} "
                        "profiles={run=%lu fail=%lu} last_lane=%lu last_sub=%lu(%s) tag=0x%08X "
                        "v=%lu,%lu,%lu,%lu ",
                        stabilization_hist[i].tick100ns, (ULONG)stabilization_hist[i].status,
                        stabilization_hist[i].lane_cnt, stabilization_hist[i].degraded_lane_cnt,
                        (ULONG)stabilization_hist[i].remediation_status,
                        stabilization_hist[i].coverage_health_score,
                        stabilization_hist[i].coverage_activity_score,
                        stabilization_hist[i].coverage_warning_cnt,
                        stabilization_hist[i].coverage_error_cnt,
                        stabilization_hist[i].profile_runs, stabilization_hist[i].profile_failures,
                        stabilization_hist[i].last_lane_id, stabilization_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(stabilization_hist[i].last_subsystem_id),
                        stabilization_hist[i].last_tag, stabilization_hist[i].last_value0,
                        stabilization_hist[i].last_value1, stabilization_hist[i].last_value2,
                        stabilization_hist[i].last_value3);
                    append_stabilization_flags_a(text, text_cap, &off, stabilization_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_conformance_snapshot_t conformance;
            RtlZeroMemory(&conformance, sizeof(conformance));
            if (NT_SUCCESS(rsc_sw_get_conformance_snapshot(&conformance))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_conformance: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "score=%lu thr=%lu pass=%lu warn=%lu err=%lu worst=%lu(%s) ",
                             conformance.version, conformance.run_cnt, conformance.failure_cnt,
                             (ULONG)conformance.last_status,
                             conformance.last_overall_score_permille,
                             conformance.last_threshold_permille, conformance.last_passing_cnt,
                             conformance.last_warning_cnt, conformance.last_error_cnt,
                             conformance.last_worst_subsystem_id,
                             rsc_sw_get_subsystem_name_a(conformance.last_worst_subsystem_id));
                append_conformance_flags_a(text, text_cap, &off, conformance.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", conformance.last_tick100ns);

                ULONG row_cnt = conformance.row_cnt;
                if (row_cnt > RSCSW_NODE_CAPACITY)
                    row_cnt = RSCSW_NODE_CAPACITY;
                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  conf[%lu:%s]: drv=%lu ppp_type=%lu glink_seed=%lu cap=0x%08X tag=0x%08X "
                        "ioctl=0x%08X "
                        "dep=%lu/%lu active=%lu succ=%lu activity=%lu score=%lu last_st=0x%08X "
                        "last_t=%llu ",
                        conformance.rows[i].subsystem_id, conformance.rows[i].name,
                        conformance.rows[i].driver_map_id, conformance.rows[i].ppp_res_type,
                        conformance.rows[i].glink_event_seed, conformance.rows[i].cap_mask,
                        conformance.rows[i].res_tag, conformance.rows[i].suggested_ioctl,
                        conformance.rows[i].dependency_in, conformance.rows[i].dependency_out,
                        conformance.rows[i].active, conformance.rows[i].success_permille,
                        conformance.rows[i].activity_score,
                        conformance.rows[i].conformance_score_permille,
                        (ULONG)conformance.rows[i].last_status, conformance.rows[i].last_tick100ns);
                    append_conformance_reason_flags_a(text, text_cap, &off,
                                                      conformance.rows[i].reason_flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_conformance_result_t conformance_hist[12];
            ULONG conformance_hist_cnt = 0;
            ULONG conformance_hist_dropped = 0;
            RtlZeroMemory(conformance_hist, sizeof(conformance_hist));
            if (NT_SUCCESS(rsc_sw_get_conformance_history(
                    conformance_hist, RTL_NUMBER_OF(conformance_hist), &conformance_hist_cnt,
                    &conformance_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_conformance_history: count=%lu dropped=%lu\n",
                             conformance_hist_cnt, conformance_hist_dropped);
                for (ULONG i = 0; i < conformance_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X nodes=%lu pass=%lu warn=%lu err=%lu score=%lu thr=%lu "
                        "worst=%lu(%s) "
                        "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        conformance_hist[i].tick100ns, (ULONG)conformance_hist[i].status,
                        conformance_hist[i].node_cnt, conformance_hist[i].passing_cnt,
                        conformance_hist[i].warning_cnt, conformance_hist[i].error_cnt,
                        conformance_hist[i].overall_score_permille,
                        conformance_hist[i].threshold_permille,
                        conformance_hist[i].worst_subsystem_id,
                        rsc_sw_get_subsystem_name_a(conformance_hist[i].worst_subsystem_id),
                        conformance_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(conformance_hist[i].last_subsystem_id),
                        conformance_hist[i].last_tag, conformance_hist[i].last_value0,
                        conformance_hist[i].last_value1, conformance_hist[i].last_value2,
                        conformance_hist[i].last_value3);
                    append_conformance_flags_a(text, text_cap, &off, conformance_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_orchestration_snapshot_t orchestration;
            RtlZeroMemory(&orchestration, sizeof(orchestration));
            if (NT_SUCCESS(rsc_sw_get_orchestration_snapshot(&orchestration))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_orchestration: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "score=%lu stage={count=%lu ok=%lu warn=%lu fail=%lu last=%lu(%s)} ",
                             orchestration.version, orchestration.run_cnt,
                             orchestration.failure_cnt, (ULONG)orchestration.last_status,
                             orchestration.last_overall_score_permille,
                             orchestration.last_stage_cnt, orchestration.last_successful_stages,
                             orchestration.last_warning_stages, orchestration.last_failed_stages,
                             orchestration.last_stage_id,
                             rsc_sw_orchestration_stage_name(orchestration.last_stage_id));
                append_orchestration_flags_a(text, text_cap, &off, orchestration.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", orchestration.last_tick100ns);

                ULONG stage_cnt = orchestration.stage_cnt;
                if (stage_cnt > RSCSW_ORCHESTRATION_STAGE_CAPACITY)
                    stage_cnt = RSCSW_ORCHESTRATION_STAGE_CAPACITY;
                for (ULONG i = 0; i < stage_cnt; i++) {
                    const CHAR* stage_name = orchestration.stages[i].stage_name;
                    if (!stage_name || stage_name[0] == '\0')
                        stage_name =
                            rsc_sw_orchestration_stage_name(orchestration.stages[i].stage_id);
                    append_fmt_a(
                        text, text_cap, &off,
                        "  orch_stage[%lu:%s]: enabled=%lu st=0x%08X warn=%lu err=%lu score=%lu "
                        "detail=%lu,%lu,%lu,%lu t=%llu\n",
                        orchestration.stages[i].stage_id, stage_name,
                        orchestration.stages[i].enabled, (ULONG)orchestration.stages[i].status,
                        orchestration.stages[i].warning_cnt, orchestration.stages[i].error_cnt,
                        orchestration.stages[i].score_permille, orchestration.stages[i].detail0,
                        orchestration.stages[i].detail1, orchestration.stages[i].detail2,
                        orchestration.stages[i].detail3, orchestration.stages[i].tick100ns);
                }
            }
        }

        {
            rscsw_orchestration_result_t orchestration_hist[12];
            ULONG orchestration_hist_cnt = 0;
            ULONG orchestration_hist_dropped = 0;
            RtlZeroMemory(orchestration_hist, sizeof(orchestration_hist));
            if (NT_SUCCESS(rsc_sw_get_orchestration_history(
                    orchestration_hist, RTL_NUMBER_OF(orchestration_hist), &orchestration_hist_cnt,
                    &orchestration_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_orchestration_history: count=%lu dropped=%lu\n",
                             orchestration_hist_cnt, orchestration_hist_dropped);
                for (ULONG i = 0; i < orchestration_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X stages={count=%lu ok=%lu warn=%lu fail=%lu "
                        "last=%lu(%s)} score=%lu "
                        "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        orchestration_hist[i].tick100ns, (ULONG)orchestration_hist[i].status,
                        orchestration_hist[i].stage_cnt, orchestration_hist[i].successful_stages,
                        orchestration_hist[i].warning_stages, orchestration_hist[i].failed_stages,
                        orchestration_hist[i].last_stage_id,
                        rsc_sw_orchestration_stage_name(orchestration_hist[i].last_stage_id),
                        orchestration_hist[i].overall_score_permille,
                        orchestration_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(orchestration_hist[i].last_subsystem_id),
                        orchestration_hist[i].last_tag, orchestration_hist[i].last_value0,
                        orchestration_hist[i].last_value1, orchestration_hist[i].last_value2,
                        orchestration_hist[i].last_value3);
                    append_orchestration_flags_a(text, text_cap, &off, orchestration_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_policy_snapshot_t policy;
            RtlZeroMemory(&policy, sizeof(policy));
            if (NT_SUCCESS(rsc_sw_get_policy_snapshot(&policy))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_policy: ver=%lu resources=%lu active=%lu seq=%llu "
                             "updates={vote=%lu remove=%lu propagated=%lu} "
                             "last={rid=0x%08X sub=%lu(%s) parent=%lu(%s) io=0x%08X st=0x%08X ",
                             policy.version, policy.res_cnt, policy.active_res_cnt,
                             policy.event_sequence, policy.vote_cnt, policy.remove_cnt,
                             policy.propagation_cnt, policy.last_res_id, policy.last_subsystem_id,
                             rsc_sw_get_subsystem_name_a(policy.last_subsystem_id),
                             policy.last_parent_subsystem_id,
                             rsc_sw_get_subsystem_name_a(policy.last_parent_subsystem_id),
                             policy.last_ioctl_code, (ULONG)policy.last_status);
                append_policy_flags_a(text, text_cap, &off, policy.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", policy.last_tick100ns);

                ULONG policy_res_cnt = policy.res_entry_cnt;
                if (policy_res_cnt > RSCSW_POLICY_RES_CAPACITY)
                    policy_res_cnt = RSCSW_POLICY_RES_CAPACITY;
                for (ULONG i = 0; i < policy_res_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  policy_res[0x%08X(%s)->0x%08X(%s)]: sub=%lu(%s)->%lu(%s) type=%lu(%s) "
                        "votes=%lu set=0x%08X drv=%lu "
                        "st=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        policy.resources[i].res_id,
                        rsc_sw_get_policy_res_name(policy.resources[i].res_id),
                        policy.resources[i].parent_res_id,
                        rsc_sw_get_policy_res_name(policy.resources[i].parent_res_id),
                        policy.resources[i].subsystem_id,
                        rsc_sw_get_subsystem_name_a(policy.resources[i].subsystem_id),
                        policy.resources[i].parent_subsystem_id,
                        rsc_sw_get_subsystem_name_a(policy.resources[i].parent_subsystem_id),
                        policy.resources[i].res_type,
                        rsc_sw_policy_type_name(policy.resources[i].res_type),
                        policy.resources[i].active_vote_cnt, policy.resources[i].set_mask,
                        policy.resources[i].driver_map_id, (ULONG)policy.resources[i].last_status,
                        policy.resources[i].last_tag, policy.resources[i].last_value0,
                        policy.resources[i].last_value1, policy.resources[i].last_value2,
                        policy.resources[i].last_value3);
                    append_policy_flags_a(text, text_cap, &off, policy.resources[i].flags);
                    append_fmt_a(text, text_cap, &off, " t=%llu\n",
                                 policy.resources[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_policy_event_t policy_hist[24];
            ULONG policy_hist_cnt = 0;
            ULONG policy_hist_dropped = 0;
            RtlZeroMemory(policy_hist, sizeof(policy_hist));
            if (NT_SUCCESS(rsc_sw_get_policy_history(policy_hist, RTL_NUMBER_OF(policy_hist),
                                                     &policy_hist_cnt, &policy_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_policy_history: count=%lu dropped=%lu\n",
                             policy_hist_cnt, policy_hist_dropped);
                for (ULONG i = 0; i < policy_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  seq=%llu t=%llu rid=0x%08X(%s)->0x%08X(%s) sub=%lu(%s)->%lu(%s) "
                        "type=%lu(%s) votes=%lu set=0x%08X drv=%lu "
                        "st=0x%08X io=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu ",
                        policy_hist[i].sequence, policy_hist[i].tick100ns, policy_hist[i].res_id,
                        rsc_sw_get_policy_res_name(policy_hist[i].res_id),
                        policy_hist[i].parent_res_id,
                        rsc_sw_get_policy_res_name(policy_hist[i].parent_res_id),
                        policy_hist[i].subsystem_id,
                        rsc_sw_get_subsystem_name_a(policy_hist[i].subsystem_id),
                        policy_hist[i].parent_subsystem_id,
                        rsc_sw_get_subsystem_name_a(policy_hist[i].parent_subsystem_id),
                        policy_hist[i].res_type, rsc_sw_policy_type_name(policy_hist[i].res_type),
                        policy_hist[i].active_vote_cnt, policy_hist[i].set_mask,
                        policy_hist[i].driver_map_id, (ULONG)policy_hist[i].status,
                        policy_hist[i].ioctl_code, policy_hist[i].tag, policy_hist[i].value0,
                        policy_hist[i].value1, policy_hist[i].value2, policy_hist[i].value3);
                    append_policy_flags_a(text, text_cap, &off, policy_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_fabric_snapshot_t fabric;
            RtlZeroMemory(&fabric, sizeof(fabric));
            if (NT_SUCCESS(rsc_sw_get_fabric_snapshot(&fabric))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_fabric: ver=%lu run=%lu fail=%lu last={st=0x%08X lanes=%lu "
                             "active=%lu policy_active=%lu warn=%lu err=%lu "
                             "health=%lu activity=%lu thermal=[%ld,%ld] ",
                             fabric.version, fabric.run_cnt, fabric.failure_cnt,
                             (ULONG)fabric.last_status, fabric.last_lane_cnt,
                             fabric.last_active_lane_cnt, fabric.last_policy_active_lane_cnt,
                             fabric.last_warning_cnt, fabric.last_error_cnt,
                             fabric.last_overall_health_score, fabric.last_overall_activity_score,
                             fabric.last_thermal_min_deci_c, fabric.last_thermal_max_deci_c);
                append_fabric_flags_a(text, text_cap, &off, fabric.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", fabric.last_tick100ns);

                ULONG lane_cnt = fabric.lane_cnt;
                if (lane_cnt > RSCSW_NODE_CAPACITY)
                    lane_cnt = RSCSW_NODE_CAPACITY;
                for (ULONG i = 0; i < lane_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  fabric_lane[%lu:%s]: present=%lu active=%lu state=%lu clk=%lu drv=%lu "
                        "ppp=%lu policy={res=%lu votes=%lu} "
                        "deps={in=%lu out=%lu} req={all=%lu ok=%lu fail=%lu} perf={succ=%lu "
                        "act=%lu health=%lu} "
                        "thermal=%ld st=0x%08X lane_flags=0x%08X t=%llu\n",
                        fabric.lanes[i].subsystem_id, fabric.lanes[i].name, fabric.lanes[i].present,
                        fabric.lanes[i].active, fabric.lanes[i].state, fabric.lanes[i].clock_khz,
                        fabric.lanes[i].driver_map_id, fabric.lanes[i].ppp_res_type,
                        fabric.lanes[i].policy_res_cnt, fabric.lanes[i].policy_active_vote_cnt,
                        fabric.lanes[i].dependency_in, fabric.lanes[i].dependency_out,
                        fabric.lanes[i].req_cnt, fabric.lanes[i].success_cnt,
                        fabric.lanes[i].failure_cnt, fabric.lanes[i].success_permille,
                        fabric.lanes[i].activity_score, fabric.lanes[i].health_score,
                        fabric.lanes[i].thermal_deci_c, (ULONG)fabric.lanes[i].last_status,
                        fabric.lanes[i].flags, fabric.lanes[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_fabric_result_t fabric_hist[24];
            ULONG fabric_hist_cnt = 0;
            ULONG fabric_hist_dropped = 0;
            RtlZeroMemory(fabric_hist, sizeof(fabric_hist));
            if (NT_SUCCESS(rsc_sw_get_fabric_history(fabric_hist, RTL_NUMBER_OF(fabric_hist),
                                                     &fabric_hist_cnt, &fabric_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_fabric_history: count=%lu dropped=%lu\n",
                             fabric_hist_cnt, fabric_hist_dropped);
                for (ULONG i = 0; i < fabric_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X lanes={count=%lu active=%lu policy=%lu warn=%lu "
                        "err=%lu} "
                        "score={health=%lu activity=%lu} thermal=[%ld,%ld] "
                        "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        fabric_hist[i].tick100ns, (ULONG)fabric_hist[i].status,
                        fabric_hist[i].lane_cnt, fabric_hist[i].active_lane_cnt,
                        fabric_hist[i].policy_active_lane_cnt, fabric_hist[i].warning_cnt,
                        fabric_hist[i].error_cnt, fabric_hist[i].overall_health_score,
                        fabric_hist[i].overall_activity_score, fabric_hist[i].thermal_min_deci_c,
                        fabric_hist[i].thermal_max_deci_c, fabric_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(fabric_hist[i].last_subsystem_id),
                        fabric_hist[i].last_tag, fabric_hist[i].last_value0,
                        fabric_hist[i].last_value1, fabric_hist[i].last_value2,
                        fabric_hist[i].last_value3);
                    append_fabric_flags_a(text, text_cap, &off, fabric_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_domain_snapshot_t domain;
            RtlZeroMemory(&domain, sizeof(domain));
            if (NT_SUCCESS(rsc_sw_get_domain_snapshot(&domain))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_domain: ver=%lu run=%lu fail=%lu last={st=0x%08X domains=%lu "
                             "healthy=%lu warn=%lu err=%lu "
                             "score={health=%lu activity=%lu} ",
                             domain.version, domain.run_cnt, domain.failure_cnt,
                             (ULONG)domain.last_status, domain.last_domain_cnt,
                             domain.last_healthy_domain_cnt, domain.last_warning_cnt,
                             domain.last_error_cnt, domain.last_overall_health_score,
                             domain.last_overall_activity_score);
                append_domain_flags_a(text, text_cap, &off, domain.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", domain.last_tick100ns);

                ULONG domain_cnt = domain.domain_cnt;
                if (domain_cnt > RSCSW_DOMAIN_CAPACITY)
                    domain_cnt = RSCSW_DOMAIN_CAPACITY;
                for (ULONG i = 0; i < domain_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  domain[%lu:%s]: mask=0x%08X lanes={count=%lu active=%lu policy=%lu "
                        "warn=%lu err=%lu} "
                        "score={health=%lu activity=%lu} thermal=[%ld,%ld] last={sub=%lu(%s) "
                        "st=0x%08X} ",
                        domain.domains[i].domain_id, domain.domains[i].name,
                        domain.domains[i].subsystem_mask, domain.domains[i].lane_cnt,
                        domain.domains[i].active_lane_cnt, domain.domains[i].policy_active_lane_cnt,
                        domain.domains[i].warning_cnt, domain.domains[i].error_cnt,
                        domain.domains[i].avg_health_score, domain.domains[i].avg_activity_score,
                        domain.domains[i].thermal_min_deci_c, domain.domains[i].thermal_max_deci_c,
                        domain.domains[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(domain.domains[i].last_subsystem_id),
                        (ULONG)domain.domains[i].last_status);
                    append_domain_flags_a(text, text_cap, &off, domain.domains[i].flags);
                    append_fmt_a(text, text_cap, &off, " t=%llu\n",
                                 domain.domains[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_domain_result_t domain_hist[24];
            ULONG domain_hist_cnt = 0;
            ULONG domain_hist_dropped = 0;
            RtlZeroMemory(domain_hist, sizeof(domain_hist));
            if (NT_SUCCESS(rsc_sw_get_domain_history(domain_hist, RTL_NUMBER_OF(domain_hist),
                                                     &domain_hist_cnt, &domain_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rscsw_domain_history: count=%lu dropped=%lu\n",
                             domain_hist_cnt, domain_hist_dropped);
                for (ULONG i = 0; i < domain_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X domains={count=%lu healthy=%lu warn=%lu err=%lu} "
                        "score={health=%lu activity=%lu} last={dom=%lu sub=%lu(%s) tag=0x%08X "
                        "v=%lu,%lu,%lu,%lu} ",
                        domain_hist[i].tick100ns, (ULONG)domain_hist[i].status,
                        domain_hist[i].domain_cnt, domain_hist[i].healthy_domain_cnt,
                        domain_hist[i].warning_cnt, domain_hist[i].error_cnt,
                        domain_hist[i].overall_health_score, domain_hist[i].overall_activity_score,
                        domain_hist[i].last_domain_id, domain_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(domain_hist[i].last_subsystem_id),
                        domain_hist[i].last_tag, domain_hist[i].last_value0,
                        domain_hist[i].last_value1, domain_hist[i].last_value2,
                        domain_hist[i].last_value3);
                    append_domain_flags_a(text, text_cap, &off, domain_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_interconnect_snapshot_t interconnect;
            RtlZeroMemory(&interconnect, sizeof(interconnect));
            if (NT_SUCCESS(rsc_sw_get_interconnect_snapshot(&interconnect))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_interconnect: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "routes=%lu active=%lu policy=%lu warn=%lu err=%lu "
                             "util={avg=%lu peak=%lu} bw={req=%lu grant=%lu} ",
                             interconnect.version, interconnect.run_cnt, interconnect.failure_cnt,
                             (ULONG)interconnect.last_status, interconnect.last_route_cnt,
                             interconnect.last_active_route_cnt,
                             interconnect.last_policy_linked_route_cnt,
                             interconnect.last_warning_cnt, interconnect.last_error_cnt,
                             interconnect.last_avg_utilization_permille,
                             interconnect.last_peak_utilization_permille,
                             interconnect.last_total_required_bandwidth_mb,
                             interconnect.last_total_granted_bandwidth_mb);
                append_interconnect_flags_a(text, text_cap, &off, interconnect.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", interconnect.last_tick100ns);

                ULONG route_cnt = interconnect.route_cnt;
                if (route_cnt > RSCSW_INTERCONNECT_ROUTE_CAPACITY)
                    route_cnt = RSCSW_INTERCONNECT_ROUTE_CAPACITY;
                if (route_cnt > 24u)
                    route_cnt = 24u;

                for (ULONG i = 0; i < route_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  interconnect_route[%lu:%s]: sub=%lu(%s) m=0x%03X(%s) s=0x%03X(%s) "
                        "active=%lu policy=%lu "
                        "bw={req=%lu grant=%lu util=%lu} votes=%lu deps={in=%lu out=%lu} "
                        "thermal=%ld st=0x%08X ",
                        interconnect.routes[i].route_id, interconnect.routes[i].name,
                        interconnect.routes[i].subsystem_id, interconnect.routes[i].subsystem_name,
                        interconnect.routes[i].master_id, interconnect.routes[i].master_name,
                        interconnect.routes[i].slave_id, interconnect.routes[i].slave_name,
                        interconnect.routes[i].active, interconnect.routes[i].policy_linked,
                        interconnect.routes[i].required_bandwidth_mb,
                        interconnect.routes[i].granted_bandwidth_mb,
                        interconnect.routes[i].utilization_permille,
                        interconnect.routes[i].vote_cnt, interconnect.routes[i].dependency_in,
                        interconnect.routes[i].dependency_out,
                        interconnect.routes[i].thermal_deci_c,
                        (ULONG)interconnect.routes[i].last_status);
                    append_interconnect_route_flags_a(text, text_cap, &off,
                                                      interconnect.routes[i].flags);
                    append_fmt_a(text, text_cap, &off, " t=%llu\n",
                                 interconnect.routes[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_interconnect_result_t interconnect_hist[24];
            ULONG interconnect_hist_cnt = 0;
            ULONG interconnect_hist_dropped = 0;
            RtlZeroMemory(interconnect_hist, sizeof(interconnect_hist));
            if (NT_SUCCESS(rsc_sw_get_interconnect_history(
                    interconnect_hist, RTL_NUMBER_OF(interconnect_hist), &interconnect_hist_cnt,
                    &interconnect_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_interconnect_history: count=%lu dropped=%lu\n",
                             interconnect_hist_cnt, interconnect_hist_dropped);
                for (ULONG i = 0; i < interconnect_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X routes={count=%lu active=%lu policy=%lu warn=%lu "
                        "err=%lu} util={avg=%lu peak=%lu} "
                        "bw={req=%lu grant=%lu} last={route=%lu sub=%lu(%s) tag=0x%08X "
                        "v=%lu,%lu,%lu,%lu} ",
                        interconnect_hist[i].tick100ns, (ULONG)interconnect_hist[i].status,
                        interconnect_hist[i].route_cnt, interconnect_hist[i].active_route_cnt,
                        interconnect_hist[i].policy_linked_route_cnt,
                        interconnect_hist[i].warning_cnt, interconnect_hist[i].error_cnt,
                        interconnect_hist[i].avg_utilization_permille,
                        interconnect_hist[i].peak_utilization_permille,
                        interconnect_hist[i].total_required_bandwidth_mb,
                        interconnect_hist[i].total_granted_bandwidth_mb,
                        interconnect_hist[i].last_route_id, interconnect_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(interconnect_hist[i].last_subsystem_id),
                        interconnect_hist[i].last_tag, interconnect_hist[i].last_value0,
                        interconnect_hist[i].last_value1, interconnect_hist[i].last_value2,
                        interconnect_hist[i].last_value3);
                    append_interconnect_flags_a(text, text_cap, &off, interconnect_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_topology_snapshot_t topology;
            RtlZeroMemory(&topology, sizeof(topology));
            if (NT_SUCCESS(rsc_sw_get_topology_snapshot(&topology))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_topology: ver=%lu nodes=%lu routes=%lu driver_maps=%lu "
                             "ppp_names=%lu icb_masters=%lu icb_slaves=%lu\n",
                             topology.version, topology.node_cnt, topology.route_cnt,
                             topology.driver_map_cnt, topology.ppp_res_name_cnt,
                             topology.icb_master_name_cnt, topology.icb_slave_name_cnt);

                ULONG node_cnt = topology.node_cnt;
                if (node_cnt > RSCSW_TOPOLOGY_NODE_CAPACITY)
                    node_cnt = RSCSW_TOPOLOGY_NODE_CAPACITY;
                for (ULONG i = 0; i < node_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  topo_node[%lu:%s]: drv=%lu ppp=%lu(%s) glink=%lu icb_err=%lu dom=0x%08X "
                        "scenario=%lu(%s) profiles=0x%08X "
                        "cap=0x%08X tag=0x%08X ioctl=0x%08X rep={m=0x%03X(%s) s=0x%03X(%s)}\n",
                        topology.nodes[i].subsystem_id, topology.nodes[i].name,
                        topology.nodes[i].driver_map_id, topology.nodes[i].ppp_res_type,
                        rsc_sw_policy_type_name(topology.nodes[i].ppp_res_type),
                        topology.nodes[i].glink_event_seed, topology.nodes[i].icb_error_code,
                        topology.nodes[i].domain_mask, topology.nodes[i].scenario_id,
                        rsc_sw_scenario_name(topology.nodes[i].scenario_id),
                        topology.nodes[i].profile_mask, topology.nodes[i].cap_mask,
                        topology.nodes[i].res_tag, topology.nodes[i].suggested_ioctl,
                        topology.nodes[i].representative_master_id,
                        topology.nodes[i].representative_master_name,
                        topology.nodes[i].representative_slave_id,
                        topology.nodes[i].representative_slave_name);
                }

                ULONG route_cnt = topology.route_cnt;
                if (route_cnt > RSCSW_TOPOLOGY_ROUTE_CAPACITY)
                    route_cnt = RSCSW_TOPOLOGY_ROUTE_CAPACITY;
                if (route_cnt > 24u)
                    route_cnt = 24u;
                for (ULONG i = 0; i < route_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  topo_route[%lu:%s]: sub=%lu(%s) dom=%lu m=0x%03X(%s) "
                                 "s=0x%03X(%s) bw_base=%lu bw_peak=%lu\n",
                                 topology.routes[i].route_id, topology.routes[i].name,
                                 topology.routes[i].subsystem_id, topology.routes[i].subsystem_name,
                                 topology.routes[i].domain_id, topology.routes[i].master_id,
                                 topology.routes[i].master_name, topology.routes[i].slave_id,
                                 topology.routes[i].slave_name,
                                 topology.routes[i].base_bandwidth_mb,
                                 topology.routes[i].peak_bandwidth_mb);
                }
            }
        }

        {
            rscsw_catalog_snapshot_t resource_catalog;
            RtlZeroMemory(&resource_catalog, sizeof(resource_catalog));
            if (NT_SUCCESS(rsc_sw_get_catalog_snapshot(&resource_catalog))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_catalog: ver=%lu subsystems=%lu aop=%lu arc=%lu cmddb=%lu "
                    "pdc_drv=%lu pdc_trig=%lu "
                    "rpmh=%lu npa=%lu glink=%lu tlmm=%lu tsens=%lu clk=%lu cmddb_query=%lu\n",
                    resource_catalog.version, resource_catalog.subsystem_cnt,
                    resource_catalog.aop_subsystem_cnt, resource_catalog.arc_res_cnt,
                    resource_catalog.cmd_db_slv_cnt, resource_catalog.pdc_drv_cnt,
                    resource_catalog.pdc_trigger_cnt, resource_catalog.rpmh_set_cnt,
                    resource_catalog.npa_client_cnt, resource_catalog.glink_event_cnt,
                    resource_catalog.tlmm_drive_cnt, resource_catalog.tsens_threshold_cnt,
                    resource_catalog.clock_driver_cnt, resource_catalog.cmd_db_query_cnt);

                ULONG subsystem_cnt = resource_catalog.subsystem_cnt;
                if (subsystem_cnt > RSCSW_SUBSYSTEM_CAPACITY)
                    subsystem_cnt = RSCSW_SUBSYSTEM_CAPACITY;
                for (ULONG i = 0; i < subsystem_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_sub[%lu:%s]: drv=%lu ppp=%lu glink=%lu arc=%lu cmddb={slv=%lu "
                        "q=%lu} "
                        "pdc={drv=%lu trig=%lu} rpmh=%lu npa=0x%X tlmm=%lu tsens=%lu clk=%lu "
                        "aop=%ld\n",
                        resource_catalog.subsystems[i].subsystem_id,
                        resource_catalog.subsystems[i].name,
                        resource_catalog.subsystems[i].driver_map_id,
                        resource_catalog.subsystems[i].ppp_res_type,
                        resource_catalog.subsystems[i].glink_event_type,
                        resource_catalog.subsystems[i].arc_res_type,
                        resource_catalog.subsystems[i].cmd_db_slave_id,
                        resource_catalog.subsystems[i].cmd_db_query_type,
                        resource_catalog.subsystems[i].pdc_drv_type,
                        resource_catalog.subsystems[i].pdc_trigger_type,
                        resource_catalog.subsystems[i].rpmh_set,
                        resource_catalog.subsystems[i].npa_client_type,
                        resource_catalog.subsystems[i].tlmm_drive_type,
                        resource_catalog.subsystems[i].tsens_threshold_type,
                        resource_catalog.subsystems[i].clock_driver_type,
                        (LONG)resource_catalog.subsystems[i].aop_subsystem_type);
                }

                ULONG show = resource_catalog.aop_subsystem_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  aop_enum[%lu]=0x%X %s\n", i,
                                 resource_catalog.aop_subsystems[i].value,
                                 resource_catalog.aop_subsystems[i].name);
                }

                show = resource_catalog.arc_res_cnt;
                if (show > 12u)
                    show = 12u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  arc_enum[%lu]=0x%X %s\n", i,
                                 resource_catalog.arc_resources[i].value,
                                 resource_catalog.arc_resources[i].name);
                }

                show = resource_catalog.cmd_db_slv_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  cmddb_slv[%lu]=0x%X %s\n", i,
                                 resource_catalog.cmd_db_slv_ids[i].value,
                                 resource_catalog.cmd_db_slv_ids[i].name);
                }

                show = resource_catalog.pdc_drv_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  pdc_drv[%lu]=0x%X %s\n", i,
                                 resource_catalog.pdc_drv_types[i].value,
                                 resource_catalog.pdc_drv_types[i].name);
                }

                show = resource_catalog.pdc_trigger_cnt;
                if (show > 12u)
                    show = 12u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  pdc_trig[%lu]=0x%X %s\n", i,
                                 resource_catalog.pdc_triggers[i].value,
                                 resource_catalog.pdc_triggers[i].name);
                }

                show = resource_catalog.rpmh_set_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  rpmh_set[%lu]=0x%X %s\n", i,
                                 resource_catalog.rpmh_sets[i].value,
                                 resource_catalog.rpmh_sets[i].name);
                }

                show = resource_catalog.npa_client_cnt;
                if (show > 16u)
                    show = 16u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  npa_client[%lu]=0x%X %s\n", i,
                                 resource_catalog.npa_client_types[i].value,
                                 resource_catalog.npa_client_types[i].name);
                }

                show = resource_catalog.glink_event_cnt;
                if (show > 16u)
                    show = 16u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  glink_evt[%lu]=0x%X %s\n", i,
                                 resource_catalog.glink_events[i].value,
                                 resource_catalog.glink_events[i].name);
                }

                show = resource_catalog.tlmm_drive_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  tlmm_drive[%lu]=0x%X %s\n", i,
                                 resource_catalog.tlmm_drive_types[i].value,
                                 resource_catalog.tlmm_drive_types[i].name);
                }

                show = resource_catalog.tsens_threshold_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  tsens_th[%lu]=0x%X %s\n", i,
                                 resource_catalog.tsens_thresholds[i].value,
                                 resource_catalog.tsens_thresholds[i].name);
                }

                show = resource_catalog.clock_driver_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  clk_driver[%lu]=0x%X %s\n", i,
                                 resource_catalog.clock_driver_types[i].value,
                                 resource_catalog.clock_driver_types[i].name);
                }

                show = resource_catalog.cmd_db_query_cnt;
                if (show > 8u)
                    show = 8u;
                for (ULONG i = 0; i < show; i++) {
                    append_fmt_a(text, text_cap, &off, "  cmddb_query[%lu]=0x%X %s\n", i,
                                 resource_catalog.cmd_db_query_types[i].value,
                                 resource_catalog.cmd_db_query_types[i].name);
                }
            }
        }

        {
            rscsw_sync_snapshot_t sync;
            RtlZeroMemory(&sync, sizeof(sync));
            if (NT_SUCCESS(rsc_sw_get_sync_snapshot(&sync))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_sync: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "profile=%s(%lu) iter=%lu apply=%lu fail=%lu "
                             "interconnect=0x%08X domain=0x%08X ",
                             sync.version, sync.run_cnt, sync.failure_cnt, (ULONG)sync.last_status,
                             rsc_sw_sync_profile_name(sync.last_profile_id), sync.last_profile_id,
                             sync.last_iterations, sync.last_applied_cnt, sync.last_failed_cnt,
                             (ULONG)sync.last_interconnect_status, (ULONG)sync.last_domain_status);
                append_sync_flags_a(text, text_cap, &off, sync.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", sync.last_tick100ns);

                ULONG lane_cnt = sync.lane_cnt;
                if (lane_cnt > RSCSW_SYNC_LANE_CAPACITY)
                    lane_cnt = RSCSW_SYNC_LANE_CAPACITY;
                if (lane_cnt > 24u)
                    lane_cnt = 24u;

                for (ULONG i = 0; i < lane_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  rsc_sync_lane[%lu:%s]: profile=%s(%lu) drv=%lu arc=%lu "
                                 "cmddb=%lu rpmh=%lu npa=0x%X glink=%lu "
                                 "target={state=%lu clk_khz=%lu flags=0x%08X reason=0x%08X} "
                                 "last={st=0x%08X t=%llu}\n",
                                 sync.lanes[i].subsystem_id, sync.lanes[i].name,
                                 rsc_sw_sync_profile_name(sync.lanes[i].profile_id),
                                 sync.lanes[i].profile_id, sync.lanes[i].driver_map_id,
                                 sync.lanes[i].arc_res_type, sync.lanes[i].cmd_db_slave_id,
                                 sync.lanes[i].rpmh_set, sync.lanes[i].npa_client_type,
                                 sync.lanes[i].glink_event_type, sync.lanes[i].target_state,
                                 sync.lanes[i].target_clock_khz, sync.lanes[i].target_flags,
                                 sync.lanes[i].reason_flags, (ULONG)sync.lanes[i].last_status,
                                 sync.lanes[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_sync_result_t sync_hist[24];
            ULONG sync_hist_cnt = 0;
            ULONG sync_hist_dropped = 0;
            RtlZeroMemory(sync_hist, sizeof(sync_hist));
            if (NT_SUCCESS(rsc_sw_get_sync_history(sync_hist, RTL_NUMBER_OF(sync_hist),
                                                   &sync_hist_cnt, &sync_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_sync_history: count=%lu dropped=%lu\n",
                             sync_hist_cnt, sync_hist_dropped);
                for (ULONG i = 0; i < sync_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X profile=%s(%lu) iter=%lu applied=%lu failed=%lu "
                        "interconnect=0x%08X domain=0x%08X "
                        "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        sync_hist[i].tick100ns, (ULONG)sync_hist[i].status,
                        rsc_sw_sync_profile_name(sync_hist[i].profile_id), sync_hist[i].profile_id,
                        sync_hist[i].iterations, sync_hist[i].applied_cnt, sync_hist[i].failed_cnt,
                        (ULONG)sync_hist[i].interconnect_status, (ULONG)sync_hist[i].domain_status,
                        sync_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(sync_hist[i].last_subsystem_id),
                        sync_hist[i].last_tag, sync_hist[i].last_value0, sync_hist[i].last_value1,
                        sync_hist[i].last_value2, sync_hist[i].last_value3);
                    append_sync_flags_a(text, text_cap, &off, sync_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_validate_snapshot_t validate;
            RtlZeroMemory(&validate, sizeof(validate));
            if (NT_SUCCESS(rsc_sw_get_validate_snapshot(&validate))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_validate: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "mode=%s(%lu) checked=%lu repaired=%lu "
                             "errors=%lu warnings=%lu score=%lu ",
                             validate.version, validate.run_cnt, validate.failure_cnt,
                             (ULONG)validate.last_status,
                             rsc_sw_validate_mode_name(validate.last_mode_id),
                             validate.last_mode_id, validate.last_checked_cnt,
                             validate.last_repaired_cnt, validate.last_error_cnt,
                             validate.last_warning_cnt, validate.last_overall_score_permille);
                append_validate_flags_a(text, text_cap, &off, validate.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", validate.last_tick100ns);

                ULONG row_cnt = validate.row_cnt;
                if (row_cnt > RSCSW_VALIDATE_ROW_CAPACITY)
                    row_cnt = RSCSW_VALIDATE_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_validate_row[%lu:%s]: mode=%s(%lu) drv=%lu issue=0x%08X "
                        "warn=0x%08X score=%lu "
                        "arc=%lu cmddb={slv=%lu q=%lu} pdc={drv=%lu trig=%lu} rpmh=%lu npa=0x%X "
                        "glink=%lu ppp=%lu "
                        "tlmm=%lu tsens=%lu clk=%lu aop=%lu icb={m=0x%X s=0x%X} last={st=0x%08X "
                        "t=%llu}\n",
                        validate.rows[i].subsystem_id, validate.rows[i].name,
                        rsc_sw_validate_mode_name(validate.rows[i].mode_id),
                        validate.rows[i].mode_id, validate.rows[i].driver_map_id,
                        validate.rows[i].issue_mask, validate.rows[i].warning_mask,
                        validate.rows[i].score_permille, validate.rows[i].arc_res_type,
                        validate.rows[i].cmd_db_slave_id, validate.rows[i].cmd_db_query_type,
                        validate.rows[i].pdc_drv_type, validate.rows[i].pdc_trigger_type,
                        validate.rows[i].rpmh_set, validate.rows[i].npa_client_type,
                        validate.rows[i].glink_event_type, validate.rows[i].ppp_res_type,
                        validate.rows[i].tlmm_drive_type, validate.rows[i].tsens_threshold_type,
                        validate.rows[i].clock_driver_type, validate.rows[i].aop_subsystem_type,
                        validate.rows[i].icb_master_id, validate.rows[i].icb_slave_id,
                        (ULONG)validate.rows[i].last_status, validate.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_validate_result_t validate_hist[24];
            ULONG validate_hist_cnt = 0;
            ULONG validate_hist_dropped = 0;
            RtlZeroMemory(validate_hist, sizeof(validate_hist));
            if (NT_SUCCESS(rsc_sw_get_validate_history(validate_hist, RTL_NUMBER_OF(validate_hist),
                                                       &validate_hist_cnt,
                                                       &validate_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_validate_history: count=%lu dropped=%lu\n",
                             validate_hist_cnt, validate_hist_dropped);
                for (ULONG i = 0; i < validate_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu st=0x%08X mode=%s(%lu) checked=%lu repaired=%lu "
                                 "errors=%lu warnings=%lu score=%lu "
                                 "last={sub=%lu(%s) issue=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                                 validate_hist[i].tick100ns, (ULONG)validate_hist[i].status,
                                 rsc_sw_validate_mode_name(validate_hist[i].mode_id),
                                 validate_hist[i].mode_id, validate_hist[i].checked_cnt,
                                 validate_hist[i].repaired_cnt, validate_hist[i].error_cnt,
                                 validate_hist[i].warning_cnt,
                                 validate_hist[i].overall_score_permille,
                                 validate_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(validate_hist[i].last_subsystem_id),
                                 validate_hist[i].last_issue_mask, validate_hist[i].last_tag,
                                 validate_hist[i].last_value0, validate_hist[i].last_value1,
                                 validate_hist[i].last_value2, validate_hist[i].last_value3);
                    append_validate_flags_a(text, text_cap, &off, validate_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_sequence_snapshot_t sequence;
            RtlZeroMemory(&sequence, sizeof(sequence));
            if (NT_SUCCESS(rsc_sw_get_sequence_snapshot(&sequence))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_sequence: ver=%lu run=%lu fail=%lu last={st=0x%08X mode=%s(%lu) "
                    "phases=0x%X steps=%lu "
                    "applied=%lu failed=%lu policy=%lu interconnect=0x%08X domain=0x%08X ",
                    sequence.version, sequence.run_cnt, sequence.failure_cnt,
                    (ULONG)sequence.last_status, rsc_sw_sequence_mode_name(sequence.last_mode_id),
                    sequence.last_mode_id, sequence.last_phase_mask, sequence.last_step_cnt,
                    sequence.last_applied_cnt, sequence.last_failed_cnt,
                    sequence.last_policy_vote_cnt, (ULONG)sequence.last_interconnect_status,
                    (ULONG)sequence.last_domain_status);
                append_sequence_flags_a(text, text_cap, &off, sequence.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", sequence.last_tick100ns);

                ULONG row_cnt = sequence.row_cnt;
                if (row_cnt > RSCSW_SEQUENCE_ROW_CAPACITY)
                    row_cnt = RSCSW_SEQUENCE_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_seq_row[%lu:%s]: mode=%s(%lu) drv=%lu primary=0x%X mask=0x%X "
                        "active={st=%lu clk=%lu fl=0x%08X} sleep={st=%lu clk=%lu fl=0x%08X} "
                        "wake={st=%lu clk=%lu fl=0x%08X} "
                        "cmddb=%lu rpmh=%lu npa=0x%X glink=%lu ppp=%lu last={phase=0x%X st=0x%08X "
                        "t=%llu}\n",
                        sequence.rows[i].subsystem_id, sequence.rows[i].name,
                        rsc_sw_sequence_mode_name(sequence.rows[i].mode_id),
                        sequence.rows[i].mode_id, sequence.rows[i].driver_map_id,
                        sequence.rows[i].primary_phase, sequence.rows[i].phase_mask,
                        sequence.rows[i].active_state, sequence.rows[i].active_clock_khz,
                        sequence.rows[i].active_flags, sequence.rows[i].sleep_state,
                        sequence.rows[i].sleep_clock_khz, sequence.rows[i].sleep_flags,
                        sequence.rows[i].wake_state, sequence.rows[i].wake_clock_khz,
                        sequence.rows[i].wake_flags, sequence.rows[i].cmd_db_slave_id,
                        sequence.rows[i].rpmh_set, sequence.rows[i].npa_client_type,
                        sequence.rows[i].glink_event_type, sequence.rows[i].ppp_res_type,
                        sequence.rows[i].last_phase, (ULONG)sequence.rows[i].last_status,
                        sequence.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_sequence_result_t sequence_hist[24];
            ULONG sequence_hist_cnt = 0;
            ULONG sequence_hist_dropped = 0;
            RtlZeroMemory(sequence_hist, sizeof(sequence_hist));
            if (NT_SUCCESS(rsc_sw_get_sequence_history(sequence_hist, RTL_NUMBER_OF(sequence_hist),
                                                       &sequence_hist_cnt,
                                                       &sequence_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_sequence_history: count=%lu dropped=%lu\n",
                             sequence_hist_cnt, sequence_hist_dropped);
                for (ULONG i = 0; i < sequence_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu st=0x%08X mode=%s(%lu) phases=0x%X steps=%lu "
                                 "applied=%lu failed=%lu policy=%lu "
                                 "interconnect=0x%08X domain=0x%08X last={sub=%lu(%s) phase=0x%X "
                                 "tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                                 sequence_hist[i].tick100ns, (ULONG)sequence_hist[i].status,
                                 rsc_sw_sequence_mode_name(sequence_hist[i].mode_id),
                                 sequence_hist[i].mode_id, sequence_hist[i].executed_phase_mask,
                                 sequence_hist[i].step_cnt, sequence_hist[i].applied_cnt,
                                 sequence_hist[i].failed_cnt, sequence_hist[i].policy_vote_cnt,
                                 (ULONG)sequence_hist[i].interconnect_status,
                                 (ULONG)sequence_hist[i].domain_status,
                                 sequence_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(sequence_hist[i].last_subsystem_id),
                                 sequence_hist[i].last_phase, sequence_hist[i].last_tag,
                                 sequence_hist[i].last_value0, sequence_hist[i].last_value1,
                                 sequence_hist[i].last_value2, sequence_hist[i].last_value3);
                    append_sequence_flags_a(text, text_cap, &off, sequence_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_lookup_snapshot_t lookup;
            RtlZeroMemory(&lookup, sizeof(lookup));
            if (NT_SUCCESS(rsc_sw_get_lookup_snapshot(&lookup))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_lookup: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "query=%s(%lu) qid=%lu match=%lu overflow=%lu "
                             "pattern=0x%08X last_match={kind=%s(%lu) id=%lu mask=0x%08X score=%lu "
                             "aux=%lu,%lu} ",
                             lookup.version, lookup.run_cnt, lookup.failure_cnt,
                             (ULONG)lookup.last_result.status,
                             rsc_sw_lookup_query_name(lookup.last_result.query_type),
                             lookup.last_result.query_type, lookup.last_result.query_id,
                             lookup.last_result.match_cnt, lookup.last_result.overflow_cnt,
                             lookup.last_result.pattern_hash,
                             rsc_sw_lookup_kind_name(lookup.last_result.last_kind),
                             lookup.last_result.last_kind, lookup.last_result.last_id,
                             lookup.last_result.last_subsystem_mask,
                             lookup.last_result.last_score_permille, lookup.last_result.last_aux0,
                             lookup.last_result.last_aux1);
                append_lookup_flags_a(text, text_cap, &off, lookup.last_result.flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", lookup.last_result.tick100ns);

                ULONG match_cnt = lookup.last_match_cnt;
                if (match_cnt > RSCSW_LOOKUP_MATCH_CAPACITY)
                    match_cnt = RSCSW_LOOKUP_MATCH_CAPACITY;
                if (match_cnt > 24u)
                    match_cnt = 24u;

                for (ULONG i = 0; i < match_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  rsc_lookup_match[%lu]: kind=%s(%lu) id=%lu name=%s "
                                 "mask=0x%08X score=%lu aux=%lu,%lu\n",
                                 i, rsc_sw_lookup_kind_name(lookup.last_matches[i].kind),
                                 lookup.last_matches[i].kind, lookup.last_matches[i].id,
                                 lookup.last_matches[i].name, lookup.last_matches[i].subsystem_mask,
                                 lookup.last_matches[i].score_permille, lookup.last_matches[i].aux0,
                                 lookup.last_matches[i].aux1);
                }
            }
        }

        {
            rscsw_lookup_result_t lookup_hist[24];
            ULONG lookup_hist_cnt = 0;
            ULONG lookup_hist_dropped = 0;
            RtlZeroMemory(lookup_hist, sizeof(lookup_hist));
            if (NT_SUCCESS(rsc_sw_get_lookup_history(lookup_hist, RTL_NUMBER_OF(lookup_hist),
                                                     &lookup_hist_cnt, &lookup_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_lookup_history: count=%lu dropped=%lu\n",
                             lookup_hist_cnt, lookup_hist_dropped);
                for (ULONG i = 0; i < lookup_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu st=0x%08X query=%s(%lu) qid=%lu match=%lu overflow=%lu "
                                 "pattern=0x%08X "
                                 "last={kind=%s(%lu) id=%lu mask=0x%08X score=%lu aux=%lu,%lu} ",
                                 lookup_hist[i].tick100ns, (ULONG)lookup_hist[i].status,
                                 rsc_sw_lookup_query_name(lookup_hist[i].query_type),
                                 lookup_hist[i].query_type, lookup_hist[i].query_id,
                                 lookup_hist[i].match_cnt, lookup_hist[i].overflow_cnt,
                                 lookup_hist[i].pattern_hash,
                                 rsc_sw_lookup_kind_name(lookup_hist[i].last_kind),
                                 lookup_hist[i].last_kind, lookup_hist[i].last_id,
                                 lookup_hist[i].last_subsystem_mask,
                                 lookup_hist[i].last_score_permille, lookup_hist[i].last_aux0,
                                 lookup_hist[i].last_aux1);
                    append_lookup_flags_a(text, text_cap, &off, lookup_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_provision_snapshot_t provision;
            RtlZeroMemory(&provision, sizeof(provision));
            if (NT_SUCCESS(rsc_sw_get_provision_snapshot(&provision))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_provision: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                    "mode=%s(%lu) planned=%lu applied=%lu "
                    "failed=%lu policy=%lu interconnect=0x%08X domain=0x%08X confidence=%lu ",
                    provision.version, provision.run_cnt, provision.failure_cnt,
                    (ULONG)provision.last_status,
                    rsc_sw_provision_mode_name(provision.last_mode_id), provision.last_mode_id,
                    provision.last_prepared_cnt, provision.last_applied_cnt,
                    provision.last_failed_cnt, provision.last_policy_vote_cnt,
                    (ULONG)provision.last_interconnect_status, (ULONG)provision.last_domain_status,
                    provision.last_avg_confidence_permille);
                append_provision_flags_a(text, text_cap, &off, provision.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", provision.last_tick100ns);

                ULONG row_cnt = provision.row_cnt;
                if (row_cnt > RSCSW_PROVISION_ROW_CAPACITY)
                    row_cnt = RSCSW_PROVISION_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_provision_row[%lu:%s]: mode=%s(%lu) drv=%lu arc=%lu cmddb=%lu "
                        "rpmh=%lu npa=0x%X glink=%lu "
                        "ppp=%lu icb={m=0x%X s=0x%X} aop=%lu clk=%lu tlmm=%lu tsens=%lu "
                        "target={state=%lu clk=%lu flags=0x%08X policy=0x%08X} confidence=%lu "
                        "last={st=0x%08X t=%llu}\n",
                        provision.rows[i].subsystem_id, provision.rows[i].name,
                        rsc_sw_provision_mode_name(provision.rows[i].mode_id),
                        provision.rows[i].mode_id, provision.rows[i].driver_map_id,
                        provision.rows[i].arc_res_type, provision.rows[i].cmd_db_slave_id,
                        provision.rows[i].rpmh_set, provision.rows[i].npa_client_type,
                        provision.rows[i].glink_event_type, provision.rows[i].ppp_res_type,
                        provision.rows[i].icb_master_id, provision.rows[i].icb_slave_id,
                        provision.rows[i].aop_subsystem_type, provision.rows[i].clock_driver_type,
                        provision.rows[i].tlmm_drive_type, provision.rows[i].tsens_threshold_type,
                        provision.rows[i].target_state, provision.rows[i].target_clock_khz,
                        provision.rows[i].target_flags, provision.rows[i].policy_flags,
                        provision.rows[i].confidence_permille, (ULONG)provision.rows[i].last_status,
                        provision.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_provision_result_t provision_hist[24];
            ULONG provision_hist_cnt = 0;
            ULONG provision_hist_dropped = 0;
            RtlZeroMemory(provision_hist, sizeof(provision_hist));
            if (NT_SUCCESS(
                    rsc_sw_get_provision_history(provision_hist, RTL_NUMBER_OF(provision_hist),
                                                 &provision_hist_cnt, &provision_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_provision_history: count=%lu dropped=%lu\n",
                             provision_hist_cnt, provision_hist_dropped);
                for (ULONG i = 0; i < provision_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu st=0x%08X mode=%s(%lu) planned=%lu applied=%lu "
                                 "failed=%lu policy=%lu "
                                 "interconnect=0x%08X domain=0x%08X confidence=%lu "
                                 "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                                 provision_hist[i].tick100ns, (ULONG)provision_hist[i].status,
                                 rsc_sw_provision_mode_name(provision_hist[i].mode_id),
                                 provision_hist[i].mode_id, provision_hist[i].prepared_cnt,
                                 provision_hist[i].applied_cnt, provision_hist[i].failed_cnt,
                                 provision_hist[i].policy_vote_cnt,
                                 (ULONG)provision_hist[i].interconnect_status,
                                 (ULONG)provision_hist[i].domain_status,
                                 provision_hist[i].avg_confidence_permille,
                                 provision_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(provision_hist[i].last_subsystem_id),
                                 provision_hist[i].last_tag, provision_hist[i].last_value0,
                                 provision_hist[i].last_value1, provision_hist[i].last_value2,
                                 provision_hist[i].last_value3);
                    append_provision_flags_a(text, text_cap, &off, provision_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_rail_snapshot_t rail;
            RtlZeroMemory(&rail, sizeof(rail));
            if (NT_SUCCESS(rsc_sw_get_rail_snapshot(&rail))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_rail: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "mode=%s(%lu) planned=%lu applied=%lu "
                             "failed=%lu rail=%lu bw=%lu policy=%lu glink=%lu interconnect=0x%08X "
                             "domain=0x%08X confidence=%lu ",
                             rail.version, rail.run_cnt, rail.failure_cnt, (ULONG)rail.last_status,
                             rsc_sw_rail_mode_name(rail.last_mode_id), rail.last_mode_id,
                             rail.last_prepared_cnt, rail.last_applied_cnt, rail.last_failed_cnt,
                             rail.last_rail_vote_cnt, rail.last_bandwidth_vote_cnt,
                             rail.last_policy_vote_cnt, rail.last_glink_event_cnt,
                             (ULONG)rail.last_interconnect_status, (ULONG)rail.last_domain_status,
                             rail.last_avg_confidence_permille);
                append_rail_flags_a(text, text_cap, &off, rail.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", rail.last_tick100ns);

                ULONG row_cnt = rail.row_cnt;
                if (row_cnt > RSCSW_RAIL_ROW_CAPACITY)
                    row_cnt = RSCSW_RAIL_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  rsc_rail_row[%lu:%s]: mode=%s(%lu) drv=%lu arc=%lu "
                                 "cmddb=%lu rpmh=%lu npa=0x%X glink=%lu "
                                 "ppp=%lu icb={m=0x%X s=0x%X} aop=%lu clk=%lu tlmm=%lu tsens=%lu "
                                 "pdc_trig=%lu "
                                 "target={state=%lu clk=%lu flags=0x%08X rail_uv=%lu ib=%lu ab=%lu "
                                 "policy=0x%08X} confidence=%lu "
                                 "last={st=0x%08X t=%llu}\n",
                                 rail.rows[i].subsystem_id, rail.rows[i].name,
                                 rsc_sw_rail_mode_name(rail.rows[i].mode_id), rail.rows[i].mode_id,
                                 rail.rows[i].driver_map_id, rail.rows[i].arc_res_type,
                                 rail.rows[i].cmd_db_slave_id, rail.rows[i].rpmh_set,
                                 rail.rows[i].npa_client_type, rail.rows[i].glink_event_type,
                                 rail.rows[i].ppp_res_type, rail.rows[i].icb_master_id,
                                 rail.rows[i].icb_slave_id, rail.rows[i].aop_subsystem_type,
                                 rail.rows[i].clock_driver_type, rail.rows[i].tlmm_drive_type,
                                 rail.rows[i].tsens_threshold_type, rail.rows[i].pdc_trigger_type,
                                 rail.rows[i].target_state, rail.rows[i].target_clock_khz,
                                 rail.rows[i].target_flags, rail.rows[i].rail_microvolts,
                                 rail.rows[i].bandwidth_ib, rail.rows[i].bandwidth_ab,
                                 rail.rows[i].policy_flags, rail.rows[i].confidence_permille,
                                 (ULONG)rail.rows[i].last_status, rail.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_rail_result_t rail_hist[24];
            ULONG rail_hist_cnt = 0;
            ULONG rail_hist_dropped = 0;
            RtlZeroMemory(rail_hist, sizeof(rail_hist));
            if (NT_SUCCESS(rsc_sw_get_rail_history(rail_hist, RTL_NUMBER_OF(rail_hist),
                                                   &rail_hist_cnt, &rail_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_rail_history: count=%lu dropped=%lu\n",
                             rail_hist_cnt, rail_hist_dropped);
                for (ULONG i = 0; i < rail_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) planned=%lu applied=%lu failed=%lu "
                        "rail=%lu bw=%lu policy=%lu "
                        "glink=%lu interconnect=0x%08X domain=0x%08X confidence=%lu "
                        "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        rail_hist[i].tick100ns, (ULONG)rail_hist[i].status,
                        rsc_sw_rail_mode_name(rail_hist[i].mode_id), rail_hist[i].mode_id,
                        rail_hist[i].prepared_cnt, rail_hist[i].applied_cnt,
                        rail_hist[i].failed_cnt, rail_hist[i].rail_vote_cnt,
                        rail_hist[i].bandwidth_vote_cnt, rail_hist[i].policy_vote_cnt,
                        rail_hist[i].glink_event_cnt, (ULONG)rail_hist[i].interconnect_status,
                        (ULONG)rail_hist[i].domain_status, rail_hist[i].avg_confidence_permille,
                        rail_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(rail_hist[i].last_subsystem_id),
                        rail_hist[i].last_tag, rail_hist[i].last_value0, rail_hist[i].last_value1,
                        rail_hist[i].last_value2, rail_hist[i].last_value3);
                    append_rail_flags_a(text, text_cap, &off, rail_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_tcs_snapshot_t tcs;
            RtlZeroMemory(&tcs, sizeof(tcs));
            if (NT_SUCCESS(rsc_sw_get_tcs_snapshot(&tcs))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_tcs: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "mode=%s(%lu) planned=%lu programmed=%lu "
                             "failed=%lu policy=%lu glink=%lu interconnect=0x%08X domain=0x%08X "
                             "confidence=%lu ",
                             tcs.version, tcs.run_cnt, tcs.failure_cnt, (ULONG)tcs.last_status,
                             rsc_sw_tcs_mode_name(tcs.last_mode_id), tcs.last_mode_id,
                             tcs.last_prepared_cnt, tcs.last_programmed_cnt, tcs.last_failed_cnt,
                             tcs.last_policy_vote_cnt, tcs.last_queued_glink_cnt,
                             (ULONG)tcs.last_interconnect_status, (ULONG)tcs.last_domain_status,
                             tcs.last_avg_confidence_permille);
                append_tcs_flags_a(text, text_cap, &off, tcs.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", tcs.last_tick100ns);

                ULONG row_cnt = tcs.row_cnt;
                if (row_cnt > RSCSW_TCS_ROW_CAPACITY)
                    row_cnt = RSCSW_TCS_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_tcs_row[%lu:%s]: mode=%s(%lu) drv=%lu arc=%lu cmddb={slv=%lu "
                        "q=%lu} rpmh={set=%lu pri=%lu tcs=%lu} "
                        "cmd={addr=0x%08X data=0x%08X flags=0x%08X} npa=0x%X glink=%lu ppp=%lu "
                        "icb={m=0x%X s=0x%X} pdc_trig=%lu aop=%lu clk=%lu tsens=%lu "
                        "target={state=%lu clk=%lu flags=0x%08X policy=0x%08X} confidence=%lu "
                        "last={st=0x%08X t=%llu}\n",
                        tcs.rows[i].subsystem_id, tcs.rows[i].name,
                        rsc_sw_tcs_mode_name(tcs.rows[i].mode_id), tcs.rows[i].mode_id,
                        tcs.rows[i].driver_map_id, tcs.rows[i].arc_res_type,
                        tcs.rows[i].cmd_db_slave_id, tcs.rows[i].cmd_db_query_type,
                        tcs.rows[i].rpmh_set, tcs.rows[i].rpmh_priority, tcs.rows[i].tcs_cnt,
                        tcs.rows[i].command_address, tcs.rows[i].command_data,
                        tcs.rows[i].command_flags, tcs.rows[i].npa_client_type,
                        tcs.rows[i].glink_event_type, tcs.rows[i].ppp_res_type,
                        tcs.rows[i].icb_master_id, tcs.rows[i].icb_slave_id,
                        tcs.rows[i].pdc_trigger_type, tcs.rows[i].aop_subsystem_type,
                        tcs.rows[i].clock_driver_type, tcs.rows[i].tsens_threshold_type,
                        tcs.rows[i].target_state, tcs.rows[i].target_clock_khz,
                        tcs.rows[i].target_flags, tcs.rows[i].policy_flags,
                        tcs.rows[i].confidence_permille, (ULONG)tcs.rows[i].last_status,
                        tcs.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_tcs_result_t tcs_hist[24];
            ULONG tcs_hist_cnt = 0;
            ULONG tcs_hist_dropped = 0;
            RtlZeroMemory(tcs_hist, sizeof(tcs_hist));
            if (NT_SUCCESS(rsc_sw_get_tcs_history(tcs_hist, RTL_NUMBER_OF(tcs_hist), &tcs_hist_cnt,
                                                  &tcs_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_tcs_history: count=%lu dropped=%lu\n",
                             tcs_hist_cnt, tcs_hist_dropped);
                for (ULONG i = 0; i < tcs_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) planned=%lu programmed=%lu failed=%lu "
                        "policy=%lu glink=%lu "
                        "interconnect=0x%08X domain=0x%08X confidence=%lu last={sub=%lu(%s) "
                        "tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        tcs_hist[i].tick100ns, (ULONG)tcs_hist[i].status,
                        rsc_sw_tcs_mode_name(tcs_hist[i].mode_id), tcs_hist[i].mode_id,
                        tcs_hist[i].prepared_cnt, tcs_hist[i].programmed_cnt,
                        tcs_hist[i].failed_cnt, tcs_hist[i].policy_vote_cnt,
                        tcs_hist[i].queued_glink_cnt, (ULONG)tcs_hist[i].interconnect_status,
                        (ULONG)tcs_hist[i].domain_status, tcs_hist[i].avg_confidence_permille,
                        tcs_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(tcs_hist[i].last_subsystem_id),
                        tcs_hist[i].last_tag, tcs_hist[i].last_value0, tcs_hist[i].last_value1,
                        tcs_hist[i].last_value2, tcs_hist[i].last_value3);
                    append_tcs_flags_a(text, text_cap, &off, tcs_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_thermal_snapshot_t thermal;
            RtlZeroMemory(&thermal, sizeof(thermal));
            if (NT_SUCCESS(rsc_sw_get_thermal_snapshot(&thermal))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_thermal: ver=%lu run=%lu fail=%lu last={st=0x%08X mode=%s(%lu) "
                    "planned=%lu sampled=%lu "
                    "applied=%lu failed=%lu mitigated=%lu recovered=%lu policy=%lu glink=%lu "
                    "interconnect=0x%08X "
                    "domain=0x%08X pressure=%lu confidence=%lu ",
                    thermal.version, thermal.run_cnt, thermal.failure_cnt,
                    (ULONG)thermal.last_status, rsc_sw_thermal_mode_name(thermal.last_mode_id),
                    thermal.last_mode_id, thermal.last_prepared_cnt, thermal.last_sampled_cnt,
                    thermal.last_applied_cnt, thermal.last_failed_cnt, thermal.last_mitigated_cnt,
                    thermal.last_recovered_cnt, thermal.last_policy_vote_cnt,
                    thermal.last_glink_event_cnt, (ULONG)thermal.last_interconnect_status,
                    (ULONG)thermal.last_domain_status, thermal.last_avg_thermal_pressure_permille,
                    thermal.last_avg_confidence_permille);
                append_thermal_flags_a(text, text_cap, &off, thermal.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", thermal.last_tick100ns);

                ULONG row_cnt = thermal.row_cnt;
                if (row_cnt > RSCSW_THERMAL_ROW_CAPACITY)
                    row_cnt = RSCSW_THERMAL_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_thermal_row[%lu:%s]: mode=%s(%lu) drv=%lu arc=%lu "
                        "cmddb={slv=%lu q=%lu} rpmh=%lu npa=0x%X "
                        "glink=%lu ppp=%lu icb={m=0x%X s=0x%X} tsens=%lu tlmm=%lu clk=%lu "
                        "pdc_trig=%lu aop=%lu sensor=%lu "
                        "sample=%ld low=%ld high=%ld pressure=%lu target={state=%lu clk=%lu "
                        "flags=0x%08X policy=0x%08X "
                        "action=0x%08X} confidence=%lu last={st=0x%08X t=%llu}\n",
                        thermal.rows[i].subsystem_id, thermal.rows[i].name,
                        rsc_sw_thermal_mode_name(thermal.rows[i].mode_id), thermal.rows[i].mode_id,
                        thermal.rows[i].driver_map_id, thermal.rows[i].arc_res_type,
                        thermal.rows[i].cmd_db_slave_id, thermal.rows[i].cmd_db_query_type,
                        thermal.rows[i].rpmh_set, thermal.rows[i].npa_client_type,
                        thermal.rows[i].glink_event_type, thermal.rows[i].ppp_res_type,
                        thermal.rows[i].icb_master_id, thermal.rows[i].icb_slave_id,
                        thermal.rows[i].tsens_threshold_type, thermal.rows[i].tlmm_drive_type,
                        thermal.rows[i].clock_driver_type, thermal.rows[i].pdc_trigger_type,
                        thermal.rows[i].aop_subsystem_type, thermal.rows[i].sensor_idx,
                        thermal.rows[i].sample_temp_deci_c, thermal.rows[i].low_threshold_deci_c,
                        thermal.rows[i].high_threshold_deci_c,
                        thermal.rows[i].thermal_pressure_permille, thermal.rows[i].target_state,
                        thermal.rows[i].target_clock_khz, thermal.rows[i].target_flags,
                        thermal.rows[i].policy_flags, thermal.rows[i].action_flags,
                        thermal.rows[i].confidence_permille, (ULONG)thermal.rows[i].last_status,
                        thermal.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_thermal_result_t thermal_hist[24];
            ULONG thermal_hist_cnt = 0;
            ULONG thermal_hist_dropped = 0;
            RtlZeroMemory(thermal_hist, sizeof(thermal_hist));
            if (NT_SUCCESS(rsc_sw_get_thermal_history(thermal_hist, RTL_NUMBER_OF(thermal_hist),
                                                      &thermal_hist_cnt, &thermal_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_thermal_history: count=%lu dropped=%lu\n",
                             thermal_hist_cnt, thermal_hist_dropped);
                for (ULONG i = 0; i < thermal_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) planned=%lu sampled=%lu applied=%lu "
                        "failed=%lu mitigated=%lu "
                        "recovered=%lu policy=%lu glink=%lu interconnect=0x%08X domain=0x%08X "
                        "pressure=%lu confidence=%lu "
                        "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        thermal_hist[i].tick100ns, (ULONG)thermal_hist[i].status,
                        rsc_sw_thermal_mode_name(thermal_hist[i].mode_id), thermal_hist[i].mode_id,
                        thermal_hist[i].prepared_cnt, thermal_hist[i].sampled_cnt,
                        thermal_hist[i].applied_cnt, thermal_hist[i].failed_cnt,
                        thermal_hist[i].mitigated_cnt, thermal_hist[i].recovered_cnt,
                        thermal_hist[i].policy_vote_cnt, thermal_hist[i].glink_event_cnt,
                        (ULONG)thermal_hist[i].interconnect_status,
                        (ULONG)thermal_hist[i].domain_status,
                        thermal_hist[i].avg_thermal_pressure_permille,
                        thermal_hist[i].avg_confidence_permille, thermal_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(thermal_hist[i].last_subsystem_id),
                        thermal_hist[i].last_tag, thermal_hist[i].last_value0,
                        thermal_hist[i].last_value1, thermal_hist[i].last_value2,
                        thermal_hist[i].last_value3);
                    append_thermal_flags_a(text, text_cap, &off, thermal_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_coord_snapshot_t coord;
            RtlZeroMemory(&coord, sizeof(coord));
            if (NT_SUCCESS(rsc_sw_get_coord_snapshot(&coord))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_coord: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "mode=%s(%lu) planned=%lu applied=%lu failed=%lu "
                             "phases={exec=0x%08X fail=0x%08X} confidence=%lu ",
                             coord.version, coord.run_cnt, coord.failure_cnt,
                             (ULONG)coord.last_status, rsc_sw_coord_mode_name(coord.last_mode_id),
                             coord.last_mode_id, coord.last_prepared_cnt, coord.last_applied_cnt,
                             coord.last_failed_cnt, coord.last_executed_phase_mask,
                             coord.last_failed_phase_mask, coord.last_avg_confidence_permille);
                append_coord_flags_a(text, text_cap, &off, coord.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", coord.last_tick100ns);

                ULONG row_cnt = coord.row_cnt;
                if (row_cnt > RSCSW_COORD_ROW_CAPACITY)
                    row_cnt = RSCSW_COORD_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_coord_row[%lu:%s]: mode=%s(%lu) drv=%lu ppp=%lu icb={m=0x%X "
                        "s=0x%X} "
                        "before={st=%lu clk=%lu flags=0x%08X} after={st=%lu clk=%lu flags=0x%08X} "
                        "phases=0x%08X fail=0x%08X confidence=%lu last={st=0x%08X t=%llu}\n",
                        coord.rows[i].subsystem_id, coord.rows[i].name,
                        rsc_sw_coord_mode_name(coord.rows[i].mode_id), coord.rows[i].mode_id,
                        coord.rows[i].driver_map_id, coord.rows[i].ppp_res_type,
                        coord.rows[i].icb_master_id, coord.rows[i].icb_slave_id,
                        coord.rows[i].state_before, coord.rows[i].clock_before_khz,
                        coord.rows[i].flags_before, coord.rows[i].state_after,
                        coord.rows[i].clock_after_khz, coord.rows[i].flags_after,
                        coord.rows[i].phase_status_mask, coord.rows[i].failure_mask,
                        coord.rows[i].confidence_permille, (ULONG)coord.rows[i].last_status,
                        coord.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_coord_result_t coord_hist[24];
            ULONG coord_hist_cnt = 0;
            ULONG coord_hist_dropped = 0;
            RtlZeroMemory(coord_hist, sizeof(coord_hist));
            if (NT_SUCCESS(rsc_sw_get_coord_history(coord_hist, RTL_NUMBER_OF(coord_hist),
                                                    &coord_hist_cnt, &coord_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_coord_history: count=%lu dropped=%lu\n",
                             coord_hist_cnt, coord_hist_dropped);
                for (ULONG i = 0; i < coord_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) planned=%lu applied=%lu failed=%lu "
                        "phases={exec=0x%08X fail=0x%08X} "
                        "status={sync=0x%08X validate=0x%08X sequence=0x%08X provision=0x%08X "
                        "rail=0x%08X tcs=0x%08X "
                        "thermal=0x%08X fabric=0x%08X interconnect=0x%08X domain=0x%08X} "
                        "confidence=%lu "
                        "last={sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        coord_hist[i].tick100ns, (ULONG)coord_hist[i].status,
                        rsc_sw_coord_mode_name(coord_hist[i].mode_id), coord_hist[i].mode_id,
                        coord_hist[i].prepared_cnt, coord_hist[i].applied_cnt,
                        coord_hist[i].failed_cnt, coord_hist[i].executed_phase_mask,
                        coord_hist[i].failed_phase_mask, (ULONG)coord_hist[i].sync_status,
                        (ULONG)coord_hist[i].validate_status, (ULONG)coord_hist[i].sequence_status,
                        (ULONG)coord_hist[i].provision_status, (ULONG)coord_hist[i].rail_status,
                        (ULONG)coord_hist[i].tcs_status, (ULONG)coord_hist[i].thermal_status,
                        (ULONG)coord_hist[i].fabric_status,
                        (ULONG)coord_hist[i].interconnect_status,
                        (ULONG)coord_hist[i].domain_status, coord_hist[i].avg_confidence_permille,
                        coord_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(coord_hist[i].last_subsystem_id),
                        coord_hist[i].last_tag, coord_hist[i].last_value0,
                        coord_hist[i].last_value1, coord_hist[i].last_value2,
                        coord_hist[i].last_value3);
                    append_coord_flags_a(text, text_cap, &off, coord_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_audit_snapshot_t audit;
            RtlZeroMemory(&audit, sizeof(audit));
            if (NT_SUCCESS(rsc_sw_get_audit_snapshot(&audit))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_audit: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                             "mode=%s(%lu) checked=%lu repaired=%lu "
                             "error=%lu warning=%lu score=%lu status={validate=0x%08X coord=0x%08X "
                             "interconnect=0x%08X domain=0x%08X} ",
                             audit.version, audit.run_cnt, audit.failure_cnt,
                             (ULONG)audit.last_status, rsc_sw_audit_mode_name(audit.last_mode_id),
                             audit.last_mode_id, audit.last_checked_cnt, audit.last_repaired_cnt,
                             audit.last_error_cnt, audit.last_warning_cnt,
                             audit.last_overall_score_permille, (ULONG)audit.last_validate_status,
                             (ULONG)audit.last_coord_status, (ULONG)audit.last_interconnect_status,
                             (ULONG)audit.last_domain_status);
                append_audit_flags_a(text, text_cap, &off, audit.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", audit.last_tick100ns);

                ULONG row_cnt = audit.row_cnt;
                if (row_cnt > RSCSW_AUDIT_ROW_CAPACITY)
                    row_cnt = RSCSW_AUDIT_ROW_CAPACITY;
                if (row_cnt > 24u)
                    row_cnt = 24u;

                for (ULONG i = 0; i < row_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_audit_row[%lu:%s]: mode=%s(%lu) drv=%lu arc=%lu cmddb={slv=0x%X "
                        "qry=0x%X} ppp=%lu "
                        "icb={m=0x%X s=0x%X} aop=%lu domain={id=%lu count=%lu} route={primary=%lu "
                        "count=%lu peak=%lu} "
                        "fanout=%lu dependency={in=%lu out=%lu risk=%lu} integrity={penalty=%lu "
                        "flags=0x%08X} "
                        "coverage={domain=%lu route=%lu} topology=%lu semantic=%lu drift=%lu "
                        "before={st=%lu clk=%lu flags=0x%08X} after={st=%lu clk=%lu flags=0x%08X} "
                        "issues=0x%08X warnings=0x%08X score=%lu last={st=0x%08X t=%llu}\n",
                        audit.rows[i].subsystem_id, audit.rows[i].name,
                        rsc_sw_audit_mode_name(audit.rows[i].mode_id), audit.rows[i].mode_id,
                        audit.rows[i].driver_map_id, audit.rows[i].arc_res_type,
                        audit.rows[i].cmd_db_slave_id, audit.rows[i].cmd_db_query_type,
                        audit.rows[i].ppp_res_type, audit.rows[i].icb_master_id,
                        audit.rows[i].icb_slave_id, audit.rows[i].aop_subsystem_type,
                        audit.rows[i].domain_id, audit.rows[i].domain_cnt,
                        audit.rows[i].route_id_primary, audit.rows[i].route_cnt,
                        audit.rows[i].route_peak_bandwidth_mb, audit.rows[i].driver_map_fanout,
                        audit.rows[i].dependency_in, audit.rows[i].dependency_out,
                        audit.rows[i].dependency_risk_permille,
                        audit.rows[i].integrity_penalty_permille, audit.rows[i].integrity_flags,
                        audit.rows[i].domain_coverage_permille,
                        audit.rows[i].route_coverage_permille,
                        audit.rows[i].topology_pressure_permille,
                        audit.rows[i].semantic_coverage_permille,
                        audit.rows[i].protocol_drift_permille, audit.rows[i].state_before,
                        audit.rows[i].clock_before_khz, audit.rows[i].flags_before,
                        audit.rows[i].state_after, audit.rows[i].clock_after_khz,
                        audit.rows[i].flags_after, audit.rows[i].issue_mask,
                        audit.rows[i].warning_mask, audit.rows[i].score_permille,
                        (ULONG)audit.rows[i].last_status, audit.rows[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_audit_result_t audit_hist[24];
            ULONG audit_hist_cnt = 0;
            ULONG audit_hist_dropped = 0;
            RtlZeroMemory(audit_hist, sizeof(audit_hist));
            if (NT_SUCCESS(rsc_sw_get_audit_history(audit_hist, RTL_NUMBER_OF(audit_hist),
                                                    &audit_hist_cnt, &audit_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_audit_history: count=%lu dropped=%lu\n",
                             audit_hist_cnt, audit_hist_dropped);
                for (ULONG i = 0; i < audit_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) checked=%lu repaired=%lu error=%lu "
                        "warning=%lu score=%lu "
                        "status={validate=0x%08X coord=0x%08X interconnect=0x%08X domain=0x%08X} "
                        "last={sub=%lu(%s) issue=0x%08X tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        audit_hist[i].tick100ns, (ULONG)audit_hist[i].status,
                        rsc_sw_audit_mode_name(audit_hist[i].mode_id), audit_hist[i].mode_id,
                        audit_hist[i].checked_cnt, audit_hist[i].repaired_cnt,
                        audit_hist[i].error_cnt, audit_hist[i].warning_cnt,
                        audit_hist[i].overall_score_permille, (ULONG)audit_hist[i].validate_status,
                        (ULONG)audit_hist[i].coord_status, (ULONG)audit_hist[i].interconnect_status,
                        (ULONG)audit_hist[i].domain_status, audit_hist[i].last_subsystem_id,
                        rsc_sw_get_subsystem_name_a(audit_hist[i].last_subsystem_id),
                        audit_hist[i].last_issue_mask, audit_hist[i].last_tag,
                        audit_hist[i].last_value0, audit_hist[i].last_value1,
                        audit_hist[i].last_value2, audit_hist[i].last_value3);
                    append_audit_flags_a(text, text_cap, &off, audit_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_supervisor_snapshot_t supervisor;
            RtlZeroMemory(&supervisor, sizeof(supervisor));
            if (NT_SUCCESS(rsc_sw_get_supervisor_snapshot(&supervisor))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_supervisor: ver=%lu run=%lu fail=%lu last={st=0x%08X "
                    "mode=%s(%lu) cycles=%lu/%lu fail_count=%lu "
                    "checked=%lu repaired=%lu error=%lu warning=%lu score=%lu "
                    "status={audit=0x%08X coord=0x%08X thermal=0x%08X interconnect=0x%08X "
                    "domain=0x%08X} ",
                    supervisor.version, supervisor.run_cnt, supervisor.failure_cnt,
                    (ULONG)supervisor.last_status,
                    rsc_sw_supervisor_mode_name(supervisor.last_mode_id), supervisor.last_mode_id,
                    supervisor.last_cycles_completed, supervisor.last_cycles_requested,
                    supervisor.last_failure_cnt, supervisor.last_checked_cnt,
                    supervisor.last_repaired_cnt, supervisor.last_error_cnt,
                    supervisor.last_warning_cnt, supervisor.last_overall_score_permille,
                    (ULONG)supervisor.last_audit_status, (ULONG)supervisor.last_coord_status,
                    (ULONG)supervisor.last_thermal_status,
                    (ULONG)supervisor.last_interconnect_status,
                    (ULONG)supervisor.last_domain_status);
                append_supervisor_flags_a(text, text_cap, &off, supervisor.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", supervisor.last_tick100ns);

                ULONG module_cnt = supervisor.module_cnt;
                if (module_cnt > RSCSW_SUPERVISOR_MODULE_CAPACITY)
                    module_cnt = RSCSW_SUPERVISOR_MODULE_CAPACITY;
                if (module_cnt > 24u)
                    module_cnt = 24u;

                for (ULONG i = 0; i < module_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  rsc_supervisor_module[%lu:%s]: issue=%lu warning=%lu "
                                 "repair=%lu sample=%lu score=%lu "
                                 "integrity_penalty=%lu gaps={dep=%lu domain=%lu route=%lu "
                                 "fanout=%lu} coverage=%lu topology=%lu semantic=%lu drift=%lu "
                                 "last={st=0x%08X t=%llu}\n",
                                 supervisor.modules[i].module_id, supervisor.modules[i].name,
                                 supervisor.modules[i].issue_cnt, supervisor.modules[i].warning_cnt,
                                 supervisor.modules[i].repair_cnt, supervisor.modules[i].sample_cnt,
                                 supervisor.modules[i].score_permille,
                                 supervisor.modules[i].integrity_penalty_permille_avg,
                                 supervisor.modules[i].dependency_gap_cnt,
                                 supervisor.modules[i].domain_gap_cnt,
                                 supervisor.modules[i].route_gap_cnt,
                                 supervisor.modules[i].fanout_violation_cnt,
                                 supervisor.modules[i].coverage_score_permille_avg,
                                 supervisor.modules[i].topology_pressure_permille_avg,
                                 supervisor.modules[i].semantic_coverage_permille_avg,
                                 supervisor.modules[i].protocol_drift_permille_avg,
                                 (ULONG)supervisor.modules[i].last_status,
                                 supervisor.modules[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_supervisor_result_t supervisor_hist[24];
            ULONG supervisor_hist_cnt = 0;
            ULONG supervisor_hist_dropped = 0;
            RtlZeroMemory(supervisor_hist, sizeof(supervisor_hist));
            if (NT_SUCCESS(rsc_sw_get_supervisor_history(
                    supervisor_hist, RTL_NUMBER_OF(supervisor_hist), &supervisor_hist_cnt,
                    &supervisor_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rsc_supervisor_history: count=%lu dropped=%lu\n", supervisor_hist_cnt,
                             supervisor_hist_dropped);
                for (ULONG i = 0; i < supervisor_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) cycles=%lu/%lu fail_count=%lu checked=%lu "
                        "repaired=%lu error=%lu "
                        "warning=%lu score=%lu status={audit=0x%08X coord=0x%08X thermal=0x%08X "
                        "interconnect=0x%08X domain=0x%08X} "
                        "last={module=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        supervisor_hist[i].tick100ns, (ULONG)supervisor_hist[i].status,
                        rsc_sw_supervisor_mode_name(supervisor_hist[i].mode_id),
                        supervisor_hist[i].mode_id, supervisor_hist[i].cycles_completed,
                        supervisor_hist[i].cycles_requested, supervisor_hist[i].failure_cnt,
                        supervisor_hist[i].checked_cnt, supervisor_hist[i].repaired_cnt,
                        supervisor_hist[i].error_cnt, supervisor_hist[i].warning_cnt,
                        supervisor_hist[i].overall_score_permille,
                        (ULONG)supervisor_hist[i].audit_status,
                        (ULONG)supervisor_hist[i].coord_status,
                        (ULONG)supervisor_hist[i].thermal_status,
                        (ULONG)supervisor_hist[i].interconnect_status,
                        (ULONG)supervisor_hist[i].domain_status, supervisor_hist[i].last_module_id,
                        (supervisor_hist[i].last_module_id > 0u &&
                         supervisor_hist[i].last_module_id <= RSCSW_NODE_CAPACITY)
                            ? rsc_sw_get_subsystem_name_a(supervisor_hist[i].last_module_id - 1u)
                            : "UNKNOWN",
                        supervisor_hist[i].last_tag, supervisor_hist[i].last_value0,
                        supervisor_hist[i].last_value1, supervisor_hist[i].last_value2,
                        supervisor_hist[i].last_value3);
                    append_supervisor_flags_a(text, text_cap, &off, supervisor_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_autotune_snapshot_t autotune;
            RtlZeroMemory(&autotune, sizeof(autotune));
            if (NT_SUCCESS(rsc_sw_get_autotune_snapshot(&autotune))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_autotune: ver=%lu run=%lu fail=%lu last={st=0x%08X mode=%s(%lu) "
                    "cycles=%lu/%lu checked=%lu "
                    "adjusted=%lu failed_adjust=%lu warning=%lu error=%lu score=%lu "
                    "status={supervisor=0x%08X thermal=0x%08X} ",
                    autotune.version, autotune.run_cnt, autotune.failure_cnt,
                    (ULONG)autotune.last_status, rsc_sw_autotune_mode_name(autotune.last_mode_id),
                    autotune.last_mode_id, autotune.last_cycles_completed,
                    autotune.last_cycles_requested, autotune.last_checked_cnt,
                    autotune.last_adjusted_cnt, autotune.last_failed_adjust_cnt,
                    autotune.last_warning_cnt, autotune.last_error_cnt,
                    autotune.last_overall_score_permille, (ULONG)autotune.last_supervisor_status,
                    (ULONG)autotune.last_thermal_status);
                append_autotune_flags_a(text, text_cap, &off, autotune.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", autotune.last_tick100ns);

                ULONG module_cnt = autotune.module_cnt;
                if (module_cnt > RSCSW_AUTOTUNE_MODULE_CAPACITY)
                    module_cnt = RSCSW_AUTOTUNE_MODULE_CAPACITY;
                if (module_cnt > 24u)
                    module_cnt = 24u;

                for (ULONG i = 0; i < module_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_autotune_module[%lu:%s]: target=%lu effective=%lu observed=%lu "
                        "delta=%lu "
                        "risk=%lu integrity=%lu dependency=%lu gaps={domain=%lu route=%lu "
                        "fanout=%lu} thermal_pressure=%lu action=0x%08X "
                        "coverage=%lu topology=%lu semantic=%lu drift=%lu "
                        "last={st=0x%08X t=%llu}\n",
                        autotune.modules[i].module_id, autotune.modules[i].name,
                        autotune.modules[i].target_score_permille,
                        autotune.modules[i].effective_target_score_permille,
                        autotune.modules[i].observed_score_permille,
                        autotune.modules[i].delta_permille, autotune.modules[i].risk_score_permille,
                        autotune.modules[i].integrity_penalty_permille,
                        autotune.modules[i].dependency_risk_permille,
                        autotune.modules[i].domain_gap_cnt, autotune.modules[i].route_gap_cnt,
                        autotune.modules[i].fanout_violation_cnt,
                        autotune.modules[i].thermal_pressure_permille,
                        autotune.modules[i].action_mask,
                        autotune.modules[i].coverage_score_permille,
                        autotune.modules[i].topology_pressure_permille,
                        autotune.modules[i].semantic_coverage_permille,
                        autotune.modules[i].protocol_drift_permille,
                        (ULONG)autotune.modules[i].last_status, autotune.modules[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_autotune_result_t autotune_hist[24];
            ULONG autotune_hist_cnt = 0;
            ULONG autotune_hist_dropped = 0;
            RtlZeroMemory(autotune_hist, sizeof(autotune_hist));
            if (NT_SUCCESS(rsc_sw_get_autotune_history(autotune_hist, RTL_NUMBER_OF(autotune_hist),
                                                       &autotune_hist_cnt,
                                                       &autotune_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_autotune_history: count=%lu dropped=%lu\n",
                             autotune_hist_cnt, autotune_hist_dropped);
                for (ULONG i = 0; i < autotune_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) cycles=%lu/%lu checked=%lu adjusted=%lu "
                        "failed_adjust=%lu "
                        "warning=%lu error=%lu score=%lu status={supervisor=0x%08X thermal=0x%08X} "
                        "last={module=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        autotune_hist[i].tick100ns, (ULONG)autotune_hist[i].status,
                        rsc_sw_autotune_mode_name(autotune_hist[i].mode_id),
                        autotune_hist[i].mode_id, autotune_hist[i].cycles_completed,
                        autotune_hist[i].cycles_requested, autotune_hist[i].checked_cnt,
                        autotune_hist[i].adjusted_cnt, autotune_hist[i].failed_adjust_cnt,
                        autotune_hist[i].warning_cnt, autotune_hist[i].error_cnt,
                        autotune_hist[i].overall_score_permille,
                        (ULONG)autotune_hist[i].supervisor_status,
                        (ULONG)autotune_hist[i].thermal_status, autotune_hist[i].last_module_id,
                        (autotune_hist[i].last_module_id > 0u &&
                         autotune_hist[i].last_module_id <= RSCSW_NODE_CAPACITY)
                            ? rsc_sw_get_subsystem_name_a(autotune_hist[i].last_module_id - 1u)
                            : "UNKNOWN",
                        autotune_hist[i].last_tag, autotune_hist[i].last_value0,
                        autotune_hist[i].last_value1, autotune_hist[i].last_value2,
                        autotune_hist[i].last_value3);
                    append_autotune_flags_a(text, text_cap, &off, autotune_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_harden_snapshot_t harden;
            RtlZeroMemory(&harden, sizeof(harden));
            if (NT_SUCCESS(rsc_sw_get_harden_snapshot(&harden))) {
                append_fmt_a(
                    text, text_cap, &off,
                    "rsc_harden: ver=%lu run=%lu fail=%lu last={st=0x%08X mode=%s(%lu) "
                    "cycles=%lu/%lu checked=%lu "
                    "hardened=%lu failed=%lu warning=%lu error=%lu score=%lu "
                    "status={autotune=0x%08X supervisor=0x%08X audit=0x%08X coord=0x%08X "
                    "thermal=0x%08X interconnect=0x%08X domain=0x%08X} ",
                    harden.version, harden.run_cnt, harden.failure_cnt, (ULONG)harden.last_status,
                    rsc_sw_harden_mode_name(harden.last_mode_id), harden.last_mode_id,
                    harden.last_cycles_completed, harden.last_cycles_requested,
                    harden.last_checked_cnt, harden.last_hardened_cnt, harden.last_failed_cnt,
                    harden.last_warning_cnt, harden.last_error_cnt,
                    harden.last_overall_score_permille, (ULONG)harden.last_autotune_status,
                    (ULONG)harden.last_supervisor_status, (ULONG)harden.last_audit_status,
                    (ULONG)harden.last_coord_status, (ULONG)harden.last_thermal_status,
                    (ULONG)harden.last_interconnect_status, (ULONG)harden.last_domain_status);
                append_harden_flags_a(text, text_cap, &off, harden.last_flags);
                append_fmt_a(text, text_cap, &off, " t=%llu}\n", harden.last_tick100ns);

                ULONG module_cnt = harden.module_cnt;
                if (module_cnt > RSCSW_HARDEN_MODULE_CAPACITY)
                    module_cnt = RSCSW_HARDEN_MODULE_CAPACITY;
                if (module_cnt > 24u)
                    module_cnt = 24u;

                for (ULONG i = 0; i < module_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  rsc_harden_module[%lu:%s]: health=%lu risk=%lu recovery=%lu "
                        "integrity=%lu dependency=%lu gaps={domain=%lu route=%lu fanout=%lu} "
                        "coverage=%lu topology=%lu semantic=%lu drift=%lu action=0x%08X "
                        "last={st=0x%08X t=%llu}\n",
                        harden.modules[i].module_id, harden.modules[i].name,
                        harden.modules[i].health_score_permille,
                        harden.modules[i].risk_score_permille,
                        harden.modules[i].recovery_score_permille,
                        harden.modules[i].integrity_penalty_permille,
                        harden.modules[i].dependency_risk_permille,
                        harden.modules[i].domain_gap_cnt, harden.modules[i].route_gap_cnt,
                        harden.modules[i].fanout_violation_cnt,
                        harden.modules[i].coverage_score_permille,
                        harden.modules[i].topology_pressure_permille,
                        harden.modules[i].semantic_coverage_permille,
                        harden.modules[i].protocol_drift_permille, harden.modules[i].action_mask,
                        (ULONG)harden.modules[i].last_status, harden.modules[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_harden_result_t harden_hist[24];
            ULONG harden_hist_cnt = 0;
            ULONG harden_hist_dropped = 0;
            RtlZeroMemory(harden_hist, sizeof(harden_hist));
            if (NT_SUCCESS(rsc_sw_get_harden_history(harden_hist, RTL_NUMBER_OF(harden_hist),
                                                     &harden_hist_cnt, &harden_hist_dropped))) {
                append_fmt_a(text, text_cap, &off, "rsc_harden_history: count=%lu dropped=%lu\n",
                             harden_hist_cnt, harden_hist_dropped);
                for (ULONG i = 0; i < harden_hist_cnt; i++) {
                    append_fmt_a(
                        text, text_cap, &off,
                        "  t=%llu st=0x%08X mode=%s(%lu) cycles=%lu/%lu checked=%lu hardened=%lu "
                        "failed=%lu warning=%lu "
                        "error=%lu score=%lu status={autotune=0x%08X supervisor=0x%08X "
                        "audit=0x%08X coord=0x%08X thermal=0x%08X interconnect=0x%08X "
                        "domain=0x%08X} "
                        "last={module=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu} ",
                        harden_hist[i].tick100ns, (ULONG)harden_hist[i].status,
                        rsc_sw_harden_mode_name(harden_hist[i].mode_id), harden_hist[i].mode_id,
                        harden_hist[i].cycles_completed, harden_hist[i].cycles_requested,
                        harden_hist[i].checked_cnt, harden_hist[i].hardened_cnt,
                        harden_hist[i].failed_cnt, harden_hist[i].warning_cnt,
                        harden_hist[i].error_cnt, harden_hist[i].overall_score_permille,
                        (ULONG)harden_hist[i].autotune_status,
                        (ULONG)harden_hist[i].supervisor_status, (ULONG)harden_hist[i].audit_status,
                        (ULONG)harden_hist[i].coord_status, (ULONG)harden_hist[i].thermal_status,
                        (ULONG)harden_hist[i].interconnect_status,
                        (ULONG)harden_hist[i].domain_status, harden_hist[i].last_module_id,
                        (harden_hist[i].last_module_id > 0u &&
                         harden_hist[i].last_module_id <= RSCSW_NODE_CAPACITY)
                            ? rsc_sw_get_subsystem_name_a(harden_hist[i].last_module_id - 1u)
                            : "UNKNOWN",
                        harden_hist[i].last_tag, harden_hist[i].last_value0,
                        harden_hist[i].last_value1, harden_hist[i].last_value2,
                        harden_hist[i].last_value3);
                    append_harden_flags_a(text, text_cap, &off, harden_hist[i].flags);
                    append_fmt_a(text, text_cap, &off, "\n");
                }
            }
        }

        {
            rscsw_checkpoint_snapshot_t checkpoint;
            RtlZeroMemory(&checkpoint, sizeof(checkpoint));
            if (NT_SUCCESS(rsc_sw_get_checkpoint_snapshot(&checkpoint))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_checkpoint: ver=%lu slots=%lu valid=%lu capture=%lu apply=%lu "
                             "last={st=0x%08X slot=%lu t=%llu}\n",
                             checkpoint.version, checkpoint.slot_cnt, checkpoint.valid_cnt,
                             checkpoint.capture_cnt, checkpoint.apply_cnt,
                             (ULONG)checkpoint.last_operation_status,
                             checkpoint.last_operation_slot_id,
                             checkpoint.last_operation_tick100ns);

                ULONG slot_cnt = checkpoint.slot_cnt;
                if (slot_cnt > RSCSW_CHECKPOINT_SLOT_CAPACITY)
                    slot_cnt = RSCSW_CHECKPOINT_SLOT_CAPACITY;
                for (ULONG i = 0; i < slot_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  cp[%lu:%s]: valid=%lu cap=%lu apply=%lu st=0x%08X nodes=%lu "
                                 "mask=0x%08X t=%llu\n",
                                 checkpoint.slots[i].slot_id, checkpoint.slots[i].name,
                                 checkpoint.slots[i].valid, checkpoint.slots[i].capture_cnt,
                                 checkpoint.slots[i].apply_cnt,
                                 (ULONG)checkpoint.slots[i].last_status,
                                 checkpoint.slots[i].node_cnt, checkpoint.slots[i].active_mask,
                                 checkpoint.slots[i].last_tick100ns);
                }
            }
        }

        {
            rscsw_checkpoint_result_t checkpoint_hist[12];
            ULONG checkpoint_hist_cnt = 0;
            ULONG checkpoint_hist_dropped = 0;
            RtlZeroMemory(checkpoint_hist, sizeof(checkpoint_hist));
            if (NT_SUCCESS(rsc_sw_get_checkpoint_history(
                    checkpoint_hist, RTL_NUMBER_OF(checkpoint_hist), &checkpoint_hist_cnt,
                    &checkpoint_hist_dropped))) {
                append_fmt_a(text, text_cap, &off,
                             "rscsw_checkpoint_history: count=%lu dropped=%lu\n",
                             checkpoint_hist_cnt, checkpoint_hist_dropped);
                for (ULONG i = 0; i < checkpoint_hist_cnt; i++) {
                    append_fmt_a(text, text_cap, &off,
                                 "  t=%llu op=%s(%lu) slot=%lu flags=0x%08X st=0x%08X nodes=%lu "
                                 "ok=%lu fail=%lu "
                                 "last_sub=%lu(%s) tag=0x%08X v=%lu,%lu,%lu,%lu\n",
                                 checkpoint_hist[i].tick100ns,
                                 rsc_sw_checkpoint_op_name(checkpoint_hist[i].operation),
                                 checkpoint_hist[i].operation, checkpoint_hist[i].slot_id,
                                 checkpoint_hist[i].flags, (ULONG)checkpoint_hist[i].status,
                                 checkpoint_hist[i].node_cnt, checkpoint_hist[i].success_cnt,
                                 checkpoint_hist[i].failure_cnt,
                                 checkpoint_hist[i].last_subsystem_id,
                                 rsc_sw_get_subsystem_name_a(checkpoint_hist[i].last_subsystem_id),
                                 checkpoint_hist[i].last_tag, checkpoint_hist[i].last_value0,
                                 checkpoint_hist[i].last_value1, checkpoint_hist[i].last_value2,
                                 checkpoint_hist[i].last_value3);
                }
            }
        }
    }

    const size_t snap_cap = 16384;
    size_t snap_bytes = 0;
    PVOID snap = ExAllocatePool2(POOL_FLAG_NON_PAGED, snap_cap, 'wRpq');
    if (snap) {
        NTSTATUS st_snap = res_snapshot(snap, snap_cap, &snap_bytes);
        if (NT_SUCCESS(st_snap)) {
            res_snapshot_hdr_t* h = (res_snapshot_hdr_t*)snap;
            ULONG cnt = snapshot_cnt_for_buf(h, snap_bytes, 48);
            append_fmt_a(text, text_cap, &off, "resources: count=%lu snapshotBytes=%Iu\n", cnt,
                         snap_bytes);

            res_snapshot_entry_t* e = (res_snapshot_entry_t*)((PUCHAR)snap + sizeof(*h));
            for (ULONG i = 0; i < cnt; i++) {
                append_fmt_a(text, text_cap, &off, "  rid=%08X st=%08X v=%u,%u,%u,%u\n",
                             e[i].res_id, e[i].sub_type, e[i].value0, e[i].value1, e[i].value2,
                             e[i].value3);
            }
        } else {
            append_fmt_a(text, text_cap, &off, "resources: snapshot_status=0x%08X\n",
                         (ULONG)st_snap);
        }
        ExFreePool(snap);
    } else {
        append_fmt_a(text, text_cap, &off, "resources: alloc_failed\n");
    }

    if (51212 > 4096 + 64) {
        ULONG* p = (ULONG*)((PUCHAR)buf + 4096);
        p[0] = (ULONG)g_stats.ioctl_cnt;
        p[1] = (ULONG)g_stats.d0_entry;
        p[2] = (ULONG)g_stats.d0_exit;
        p[3] = (ULONG)g_stats.po_fx_power_required;
        p[4] = (ULONG)g_stats.po_fx_power_not_required;
    }

    return STATUS_SUCCESS;
}

NTSTATUS build_clock_debug_list(_In_ p_dev_ctx_t ctx, _Out_writes_bytes_(83984) PVOID buf,
                                _In_ ULONG size, _Out_ PULONG bytes_written)
{
    if (!bytes_written)
        return STATUS_INVALID_PARAMETER;
    *bytes_written = 0;

    if (size < sizeof(clockdbg_list_t)) {
        *bytes_written = sizeof(clockdbg_list_t);
        return STATUS_BUFFER_TOO_SMALL;
    }

    clockdbg_list_t* l = (clockdbg_list_t*)buf;

    RtlZeroMemory(buf, sizeof(clockdbg_list_t));

    l->dword0 = 3;
    l->status = 0;
    l->dword2 = 0;

    ULONG target_cnt = registry_query_u_long(L"ClockDebugCount", 0);
    if (target_cnt > 512)
        return STATUS_INVALID_PARAMETER;

    ULONG filled = 0;
    const size_t snap_cap = 16384;
    size_t snap_bytes = 0;
    PVOID snap = ExAllocatePool2(POOL_FLAG_NON_PAGED, snap_cap, 'dCpq');
    if (snap) {
        NTSTATUS st_snap = res_snapshot(snap, snap_cap, &snap_bytes);
        if (NT_SUCCESS(st_snap)) {
            res_snapshot_hdr_t* h = (res_snapshot_hdr_t*)snap;
            ULONG cap = (target_cnt != 0) ? target_cnt : 512u;
            ULONG cnt = snapshot_cnt_for_buf(h, snap_bytes, cap);
            if (cnt != 0) {
                res_snapshot_entry_t* e = (res_snapshot_entry_t*)((PUCHAR)snap + sizeof(*h));
                for (ULONG i = 0; i < cnt; i++) {
                    clockdbg_entry_t* ce = &l->entries[i];
                    format_res_name_a(e[i].res_id, e[i].sub_type, ce->clock_name,
                                      RTL_NUMBER_OF(ce->clock_name));
                    ce->b_on = (unsigned __int8)((e[i].value0 | e[i].value1 | e[i].value2 |
                                                  e[i].value3) != 0);
                    ce->l_hz = (int)e[i].value0;
                    ce->unk0[0] = (unsigned __int8)(e[i].sub_type & 0xFFu);
                    ce->unk0[1] = (unsigned __int8)((e[i].sub_type >> 8) & 0xFFu);
                    ce->unk0[2] = (unsigned __int8)((e[i].sub_type >> 16) & 0xFFu);
                    ce->unk0[3] = (unsigned __int8)((e[i].sub_type >> 24) & 0xFFu);
                    ce->unk0[4] = (unsigned __int8)(e[i].value1 & 0xFFu);
                    ce->unk0[5] = (unsigned __int8)((e[i].value1 >> 8) & 0xFFu);
                    ce->unk0[6] = (unsigned __int8)((e[i].value1 >> 16) & 0xFFu);
                    ce->unk0[7] = (unsigned __int8)((e[i].value1 >> 24) & 0xFFu);
                    ce->unk0[8] = (unsigned __int8)(e[i].value2 & 0xFFu);
                    ce->unk0[9] = (unsigned __int8)(e[i].value3 & 0xFFu);
                    if (ctx && i < 16)
                        ce->unk0[10] = (unsigned __int8)(ctx->port_id & 0xFF);
                }
                filled = cnt;
            }
        }
        ExFreePool(snap);
    }

    if (target_cnt == 0)
        target_cnt = (filled != 0) ? filled : 512u;
    l->cnt = target_cnt;

    const char* names[8] = {"xo", "gcc", "cpu", "mem", "gpu", "ufs", "pcie", "disp"};
    int freqs[8] = {19200000, 0, 0, 0, 0, 0, 0, 0};

    for (ULONG i = filled; i < l->cnt; i++) {
        clockdbg_entry_t* e = &l->entries[i];
        (VOID) RtlStringCchCopyA(e->clock_name, RTL_NUMBER_OF(e->clock_name), names[i % 8]);
        e->b_on = (unsigned __int8)((i % 3) == 0);
        e->l_hz = freqs[i % 8];
        e->unk0[0] = (unsigned __int8)i;
        if (ctx && i < 16)
            e->unk0[1] = (unsigned __int8)(ctx->port_id & 0xFF);
    }

    *bytes_written = sizeof(clockdbg_list_t);
    return STATUS_SUCCESS;
}
