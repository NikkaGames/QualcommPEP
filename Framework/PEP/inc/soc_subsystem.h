#pragma once
#include <ntddk.h>

#define SOC_SUBSYS_CNT 10
#define SOCSUBSYS_HISTORY_CAPACITY 128u

typedef enum soc_subsys_id {
    SOC_SUBSYS_APPS = 0,
    SOC_SUBSYS_MODEM = 1,
    SOC_SUBSYS_LPA = 2,
    SOC_SUBSYS_WCN = 3,
    SOC_SUBSYS_SSC = 4,
    SOC_SUBSYS_CDSP = 5,
    SOC_SUBSYS_GRAPHICS = 6,
    SOC_SUBSYS_DISPLAY = 7,
    SOC_SUBSYS_SECURE = 8,
    SOC_SUBSYS_SOC = 9,
} soc_subsys_id_t;

typedef struct soc_subsystem_stats {
    ULONG wakeup_cnt;
    ULONGLONG blocked_duration100ns;
    ULONGLONG sleep_duration100ns;
} soc_subsystem_stats_t;

typedef enum socsubsys_event_code {
    SOC_SUBSYS_EVENT_INIT = 1u,
    SOC_SUBSYS_EVENT_DRIPS_ENTRY = 2u,
    SOC_SUBSYS_EVENT_DRIPS_EXIT = 3u,
    SOC_SUBSYS_EVENT_BLOCKED_START = 4u,
    SOC_SUBSYS_EVENT_BLOCKED_END = 5u,
    SOC_SUBSYS_EVENT_RESET_ACCOUNTING = 6u
} socsubsys_event_code_t;

typedef struct socsubsys_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG subsystem_idx;
    ULONG reason;
    ULONG in_drips;
    ULONG wakeup_cnt;
    ULONGLONG blocked_duration100ns;
    ULONGLONG sleep_duration100ns;
    ULONGLONG aux100ns;
} socsubsys_event_t;

typedef struct socsubsys_entry_snapshot {
    soc_subsystem_stats_t stats;
    ULONGLONG last_entered_at100ns;
    ULONGLONG last_exited_at100ns;
    ULONG metadata_cnt;
    ULONG flags;
    ULONGLONG island_entry_cnt;
    ULONGLONG island_entry_time100ns;
    ULONGLONG island_exit_time100ns;
    ULONGLONG island_accum_duration100ns;
} socsubsys_entry_snapshot_t;

typedef struct socsubsys_snapshot {
    ULONG version;
    ULONG drips_state_id;
    ULONG enabled_mask;
    ULONG subsystem_cnt;
    ULONG in_drips;
    ULONG drips_blocked;
    ULONG drips_blocked_reason;
    ULONG last_blocked_reason;
    ULONGLONG current_blocked_duration100ns;
    ULONGLONG blocked_boot100ns;
    ULONGLONG blocked_usb100ns;
    ULONGLONG blocked_dbg100ns;
    ULONGLONG blocked_other100ns;
    ULONGLONG aoss_sleep_count100ns;
    ULONGLONG ddr_sleep_count100ns;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_subsystem_idx;
    ULONG last_event_reason;
    ULONGLONG last_event_tick100ns;
    socsubsys_entry_snapshot_t entries[SOC_SUBSYS_CNT];
} socsubsys_snapshot_t;

typedef struct soc_subsystem_entry {
    ULONG subsystem_idx;
    UNICODE_STRING parent_name;
    UNICODE_STRING name;
    ULONG metadata_cnt;
    soc_subsystem_stats_t stats;
    ULONGLONG last_entered_at100ns;
    ULONGLONG last_exited_at100ns;
} soc_subsystem_entry_t;

typedef struct socsubsys_ctx {
    volatile LONG initialized;
    volatile LONG in_drips;
    ULONG drips_state_id;
    ULONG enabled_mask;
    UNICODE_STRING parent_soc_name;
    soc_subsystem_entry_t entries[SOC_SUBSYS_CNT];
} socsubsys_ctx_t;

/* already included either framework header must use the kit definitions. */

#if !defined(PEPFX) && !defined(PEPLEGACY)

typedef struct _PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING {
    ULONG PlatformIdleStateIndex;
    ULONG Flags;
} PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING;

typedef struct _PEP_QUERY_SOC_SUBSYSTEM_COUNT {
    ULONG PlatformIdleStateIndex;
    ULONG SubsystemCount;
    ULONG Flags;
} PEP_QUERY_SOC_SUBSYSTEM_COUNT;

typedef struct _PEP_QUERY_SOC_SUBSYSTEM_BLOCKING_TIME {
    ULONG PlatformIdleStateIndex;
    PVOID SubsystemHandle;
    const UNICODE_STRING* SubsystemName;
    ULONGLONG BlockingTime;
    ULONG Flags;
} PEP_QUERY_SOC_SUBSYSTEM_BLOCKING_TIME;

typedef struct _PEP_QUERY_SOC_SUBSYSTEM {
    ULONG PlatformIdleStateIndex;
    ULONG SubsystemIndex;
    PVOID SubsystemHandle;
    UNICODE_STRING ParentName;
    UNICODE_STRING SubsystemName;
    ULONG MetadataCount;
    ULONG Flags;
} PEP_QUERY_SOC_SUBSYSTEM;

typedef struct _PEP_SOC_SUBSYSTEM_METADATA {
    UNICODE_STRING Key;
    UNICODE_STRING Value;
} PEP_SOC_SUBSYSTEM_METADATA;

typedef struct _PEP_QUERY_SOC_SUBSYSTEM_METADATA {
    ULONG PlatformIdleStateIndex;
    PVOID SubsystemHandle;
    const UNICODE_STRING* SubsystemName;
    ULONG Flags;
    ULONG MetadataCount;
    PEP_SOC_SUBSYSTEM_METADATA* Metadata[1];
} PEP_QUERY_SOC_SUBSYSTEM_METADATA;
#endif

VOID soc_subsys_init_if_needed(VOID);
VOID soc_subsys_set_drips_state_id(_In_ ULONG platform_idle_state_idx);
VOID soc_subsys_on_drips_entry(_In_ ULONGLONG now100ns);
VOID soc_subsys_on_drips_exit(_In_ ULONGLONG now100ns);
VOID soc_subsys_on_drips_blocked_start(_In_ ULONGLONG now100ns, _In_ ULONG reason);
VOID soc_subsys_on_drips_blocked_end(_In_ ULONGLONG now100ns);

BOOLEAN soc_subsys_query_cnt(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_COUNT* query);
BOOLEAN soc_subsys_query_subsystem(_Inout_ PEP_QUERY_SOC_SUBSYSTEM* query);
BOOLEAN soc_subsys_reset_accounting(_In_ PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING* reset);
BOOLEAN soc_subsys_query_blocking_time(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_BLOCKING_TIME* query);
BOOLEAN soc_subsys_query_metadata(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_METADATA* query);
VOID soc_subsys_get_snapshot(_Out_ socsubsys_snapshot_t* snapshot);
NTSTATUS soc_subsys_get_history(_Out_writes_to_(capacity, *cnt) socsubsys_event_t* events,
                                _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);

BOOLEAN soc_subsys_get_info(_In_ ULONG idx, _Out_opt_ PCWSTR* parent, _Out_opt_ PCWSTR* name,
                            _Out_opt_ ULONG* metadata_cnt, _Out_opt_ BOOLEAN* enabled,
                            _Out_opt_ soc_subsystem_stats_t* stats);
