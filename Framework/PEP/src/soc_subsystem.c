#include "driver.h"
#include "soc_subsystem.h"
#include "resource_cache.h"
#include "stats.h"

#define SOCSUBSYS_FLAG_ENABLED 0x00000001u
#define SOCSUBSYS_FLAG_ISLAND_CAPABLE 0x00000002u
#define SOCSUBSYS_FLAG_ISLAND_ENABLED 0x00000004u

static volatile LONG g_soc_init_state;
static volatile LONG g_soc_trace_init;

static socsubsys_ctx_t g_soc_subsys;
static KSPIN_LOCK g_soc_trace_lock;

static volatile LONG g_drips_blocked;
static ULONGLONG g_drips_blocked_start100ns;
static ULONG g_drips_blocked_reason;

static ULONGLONG g_drips_blocked_boot100ns;
static ULONGLONG g_drips_blocked_usb100ns;
static ULONGLONG g_drips_blocked_dbg100ns;
static ULONGLONG g_drips_blocked_other100ns;
static ULONG g_last_blocked_reason;

static ULONGLONG g_aoss_sleep_count100ns;
static ULONGLONG g_ddr_sleep_count100ns;
static ULONG g_island_mode_enabled_mask;

static ULONGLONG g_island_entry_cnt[SOC_SUBSYS_CNT];
static ULONGLONG g_island_entry_time100ns[SOC_SUBSYS_CNT];
static ULONGLONG g_island_exit_time100ns[SOC_SUBSYS_CNT];
static ULONGLONG g_island_accum_duration100ns[SOC_SUBSYS_CNT];

static ULONGLONG g_soc_event_sequence;
static ULONG g_soc_history_head;
static ULONG g_soc_history_cnt;
static ULONG g_soc_history_dropped;
static socsubsys_event_t g_soc_history[SOCSUBSYS_HISTORY_CAPACITY];

static ULONG g_soc_last_event_code;
static LONG g_soc_last_event_status;
static ULONG g_soc_last_event_subsystem_idx;
static ULONG g_soc_last_event_reason;
static ULONGLONG g_soc_last_event_tick100ns;

static const wchar_t g_soc_subsystem_name0[] = L"Application Subsystem";
static const wchar_t g_soc_subsystem_name1[] = L"Modem Subsystem";
static const wchar_t g_soc_subsystem_name2[] = L"Low Power Audio (LPA) Subsystem";
static const wchar_t g_soc_subsystem_name3[] = L"Wireless Connectivity (WCN) Subsystem";
static const wchar_t g_soc_subsystem_name4[] = L"Sensor Island (SSC) Subsystem";
static const wchar_t g_soc_subsystem_name5[] = L"Compute DSP Subsystem";
static const wchar_t g_soc_subsystem_name6[] = L"Graphics Subsystem";
static const wchar_t g_soc_subsystem_name7[] = L"Display proc";
static const wchar_t g_soc_subsystem_name8[] = L"Secure proc";
static const wchar_t g_soc_subsystem_name9[] = L"SOC";

static const wchar_t g_soc_parent_name[] = L"SOC";
static const wchar_t g_qcom_soc_parent_name[] = L"QCOM_SOC";

static const UNICODE_STRING g_key_aoss_sleep_cnt = RTL_CONSTANT_STRING(L"AOSS Sleep Count");
static const UNICODE_STRING g_key_ddr_sleep_cnt = RTL_CONSTANT_STRING(L"DDR Sleep Count");
static const UNICODE_STRING g_key_island_entry_time =
    RTL_CONSTANT_STRING(L"Island mode Entry Time");
static const UNICODE_STRING g_key_island_accum_duration =
    RTL_CONSTANT_STRING(L"Duration in Island Mode");
static const UNICODE_STRING g_key_island_exit_time = RTL_CONSTANT_STRING(L"Island mode Exit Time");
static const UNICODE_STRING g_key_island_entry_cnt =
    RTL_CONSTANT_STRING(L"Island mode Entry Count");

static __forceinline BOOLEAN soc_subsys_is_enabled(_In_ ULONG idx)
{
    if (idx >= SOC_SUBSYS_CNT)
        return FALSE;

    if (idx == SOC_SUBSYS_SOC)
        return TRUE;

    return (g_soc_subsys.enabled_mask & (1u << idx)) != 0;
}

static __forceinline BOOLEAN soc_subsys_is_island_capable(_In_ ULONG idx)
{
    return (idx == SOC_SUBSYS_SSC);
}

static __forceinline BOOLEAN soc_subsys_is_island_stats_enabled(_In_ ULONG idx)
{
    if (!soc_subsys_is_island_capable(idx))
        return FALSE;

    return (g_island_mode_enabled_mask & (1u << idx)) != 0;
}

static VOID soc_subsys_ensure_trace_infra(VOID)
{
    if (InterlockedCompareExchange(&g_soc_trace_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_soc_trace_lock);
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_soc_trace_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_soc_trace_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static ULONG soc_subsys_entry_flags(_In_ ULONG idx)
{
    ULONG flags = 0;

    if (soc_subsys_is_enabled(idx))
        flags |= SOCSUBSYS_FLAG_ENABLED;
    if (soc_subsys_is_island_capable(idx))
        flags |= SOCSUBSYS_FLAG_ISLAND_CAPABLE;
    if (soc_subsys_is_island_stats_enabled(idx))
        flags |= SOCSUBSYS_FLAG_ISLAND_ENABLED;

    return flags;
}

static VOID soc_subsys_publish_summary(VOID)
{
    res_key_t k;
    res_value_t v;

    ULONG in_drips = 0;
    ULONG drips_blocked = 0;
    ULONG drips_reason = 0;
    ULONG dropped = 0;
    ULONGLONG boot = 0;
    ULONGLONG usb = 0;
    ULONGLONG dbg = 0;
    ULONGLONG other = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);
    in_drips = (ULONG)g_soc_subsys.in_drips;
    drips_blocked = (ULONG)g_drips_blocked;
    drips_reason = g_drips_blocked_reason;
    dropped = g_soc_history_dropped;
    boot = g_drips_blocked_boot100ns;
    usb = g_drips_blocked_usb100ns;
    dbg = g_drips_blocked_dbg100ns;
    other = g_drips_blocked_other100ns;
    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    k.res_id = 0x30515353u;
    k.sub_type = 0;
    v.value0 = in_drips;
    v.value1 = drips_blocked;
    v.value2 = drips_reason;
    v.value3 = dropped;
    (VOID) res_set(k, v);

    k.res_id = 0x34515353u;
    k.sub_type = 0;
    v.value0 = (ULONG)boot;
    v.value1 = (ULONG)usb;
    v.value2 = (ULONG)dbg;
    v.value3 = (ULONG)other;
    (VOID) res_set(k, v);
}

static VOID soc_subsys_publish_entry(_In_ ULONG idx)
{
    if (idx >= SOC_SUBSYS_CNT)
        return;

    res_key_t k;
    res_value_t v;

    ULONG wake = 0;
    ULONGLONG blocked = 0;
    ULONGLONG sleep = 0;
    ULONGLONG entered = 0;
    ULONGLONG exited = 0;
    ULONGLONG island_cnt = 0;
    ULONG metadata_cnt = 0;
    ULONG flags = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);
    wake = g_soc_subsys.entries[idx].stats.wakeup_cnt;
    blocked = g_soc_subsys.entries[idx].stats.blocked_duration100ns;
    sleep = g_soc_subsys.entries[idx].stats.sleep_duration100ns;
    entered = g_soc_subsys.entries[idx].last_entered_at100ns;
    exited = g_soc_subsys.entries[idx].last_exited_at100ns;
    island_cnt = g_island_entry_cnt[idx];
    metadata_cnt = g_soc_subsys.entries[idx].metadata_cnt;
    flags = soc_subsys_entry_flags(idx);
    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    k.res_id = 0x32515353u;
    k.sub_type = idx;
    v.value0 = wake;
    v.value1 = (ULONG)blocked;
    v.value2 = (ULONG)sleep;
    v.value3 = flags;
    (VOID) res_set(k, v);

    k.res_id = 0x33515353u;
    k.sub_type = idx;
    v.value0 = (ULONG)entered;
    v.value1 = (ULONG)exited;
    v.value2 = (ULONG)island_cnt;
    v.value3 = metadata_cnt;
    (VOID) res_set(k, v);
}

static VOID soc_subsys_publish_event(_In_ ULONG event_code, _In_ NTSTATUS status,
                                     _In_ ULONG subsystem_idx, _In_ ULONG reason,
                                     _In_ ULONGLONG sequence)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x31515353u;
    k.sub_type = event_code;
    v.value0 = (ULONG)status;
    v.value1 = subsystem_idx;
    v.value2 = reason;
    v.value3 = (ULONG)sequence;
    (VOID) res_set(k, v);
}

static VOID soc_subsys_record_event(_In_ ULONG event_code, _In_ NTSTATUS status,
                                    _In_ ULONG subsystem_idx, _In_ ULONG reason,
                                    _In_ ULONGLONG aux100ns)
{
    ULONGLONG tick = KeQueryInterruptTime();
    ULONGLONG seq = 0;

    soc_subsys_ensure_trace_infra();

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    seq = ++g_soc_event_sequence;

    ULONG idx = g_soc_history_head % SOCSUBSYS_HISTORY_CAPACITY;
    socsubsys_event_t* e = &g_soc_history[idx];

    e->tick100ns = tick;
    e->sequence = seq;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->subsystem_idx = subsystem_idx;
    e->reason = reason;
    e->in_drips = (ULONG)g_soc_subsys.in_drips;
    e->wakeup_cnt = 0;
    e->blocked_duration100ns = 0;
    e->sleep_duration100ns = 0;
    e->aux100ns = aux100ns;

    if (subsystem_idx < SOC_SUBSYS_CNT) {
        soc_subsystem_entry_t* sub = &g_soc_subsys.entries[subsystem_idx];
        e->wakeup_cnt = sub->stats.wakeup_cnt;
        e->blocked_duration100ns = sub->stats.blocked_duration100ns;
        e->sleep_duration100ns = sub->stats.sleep_duration100ns;
    }

    g_soc_history_head = (idx + 1u) % SOCSUBSYS_HISTORY_CAPACITY;
    if (g_soc_history_cnt < SOCSUBSYS_HISTORY_CAPACITY)
        g_soc_history_cnt++;
    else
        g_soc_history_dropped++;

    g_soc_last_event_code = event_code;
    g_soc_last_event_status = (LONG)status;
    g_soc_last_event_subsystem_idx = subsystem_idx;
    g_soc_last_event_reason = reason;
    g_soc_last_event_tick100ns = tick;

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    stats_record_event(0x700u + event_code, status, subsystem_idx, reason, (ULONG)aux100ns,
                       (ULONG)(aux100ns >> 32));

    soc_subsys_publish_summary();
    if (subsystem_idx < SOC_SUBSYS_CNT)
        soc_subsys_publish_entry(subsystem_idx);
    soc_subsys_publish_event(event_code, status, subsystem_idx, reason, seq);
}

static BOOLEAN soc_subsys_try_resolve_handle(_In_opt_ PVOID handle, _Out_ ULONG* idx)
{
    if (!idx)
        return FALSE;

    *idx = 0;

    if (!handle)
        return FALSE;

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        if (handle == &g_soc_subsys.entries[i]) {
            *idx = i;
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN soc_subsys_try_resolve_name(_In_opt_ const UNICODE_STRING* name, _Out_ ULONG* idx)
{
    if (!idx)
        return FALSE;

    *idx = 0;

    if (!name || !name->Buffer || name->Length == 0)
        return FALSE;

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        if (RtlCompareUnicodeString(name, &g_soc_subsys.entries[i].name, TRUE) == 0) {
            *idx = i;
            return TRUE;
        }
    }

    return FALSE;
}

static __forceinline BOOLEAN soc_subsys_platform_ok(_In_ ULONG platform_idle_state_idx)
{
    soc_subsys_init_if_needed();
    return (platform_idle_state_idx == g_soc_subsys.drips_state_id);
}

static BOOLEAN copy_key_value(_In_ PEP_SOC_SUBSYSTEM_METADATA* md, _In_ const UNICODE_STRING* key,
                              _In_ const UNICODE_STRING* value)
{
    if (!md || !key || !value)
        return FALSE;

    if (md->Key.Buffer && md->Key.MaximumLength)
        RtlCopyUnicodeString(&md->Key, key);
    else
        md->Key = *key;

    if (md->Value.Buffer && md->Value.MaximumLength)
        RtlCopyUnicodeString(&md->Value, value);
    else
        md->Value = *value;

    return TRUE;
}

static VOID u64_to_unicode(_In_ ULONGLONG value, _Out_ UNICODE_STRING* out,
                           _Out_writes_(cch) wchar_t* buf, _In_ USHORT cch)
{
    out->Buffer = buf;
    out->Length = 0;
    out->MaximumLength = (USHORT)(cch * sizeof(wchar_t));
    if (cch)
        buf[0] = 0;
    (VOID) RtlInt64ToUnicodeString((LONGLONG)value, 10, out);
}

VOID soc_subsys_init_if_needed(VOID)
{
    if (InterlockedCompareExchange(&g_soc_init_state, 1, 0) != 0)
        return;

    RtlZeroMemory(&g_soc_subsys, sizeof(g_soc_subsys));

    g_soc_subsys.initialized = 1;
    g_soc_subsys.drips_state_id = 0;
    g_soc_subsys.enabled_mask = registry_query_u_long(L"SocSubsystemEnabledMask", 0x3FFu);

    g_island_mode_enabled_mask =
        registry_query_u_long(L"SocSubsystemIslandMask", (1u << SOC_SUBSYS_SSC));

    RtlInitUnicodeString(&g_soc_subsys.parent_soc_name, g_soc_parent_name);

    static const wchar_t* const names[SOC_SUBSYS_CNT] = {
        g_soc_subsystem_name0, g_soc_subsystem_name1, g_soc_subsystem_name2, g_soc_subsystem_name3,
        g_soc_subsystem_name4, g_soc_subsystem_name5, g_soc_subsystem_name6, g_soc_subsystem_name7,
        g_soc_subsystem_name8, g_soc_subsystem_name9,
    };

    soc_subsys_ensure_trace_infra();

    InterlockedExchange(&g_drips_blocked, 0);
    g_drips_blocked_start100ns = 0;
    g_drips_blocked_reason = 0;

    g_drips_blocked_boot100ns = 0;
    g_drips_blocked_usb100ns = 0;
    g_drips_blocked_dbg100ns = 0;
    g_drips_blocked_other100ns = 0;
    g_last_blocked_reason = 0;

    g_aoss_sleep_count100ns = 0;
    g_ddr_sleep_count100ns = 0;

    RtlZeroMemory(g_island_entry_cnt, sizeof(g_island_entry_cnt));
    RtlZeroMemory(g_island_entry_time100ns, sizeof(g_island_entry_time100ns));
    RtlZeroMemory(g_island_exit_time100ns, sizeof(g_island_exit_time100ns));
    RtlZeroMemory(g_island_accum_duration100ns, sizeof(g_island_accum_duration100ns));

    g_soc_event_sequence = 0;
    g_soc_history_head = 0;
    g_soc_history_cnt = 0;
    g_soc_history_dropped = 0;
    RtlZeroMemory(g_soc_history, sizeof(g_soc_history));

    g_soc_last_event_code = 0;
    g_soc_last_event_status = STATUS_SUCCESS;
    g_soc_last_event_subsystem_idx = SOC_SUBSYS_SOC;
    g_soc_last_event_reason = 0;
    g_soc_last_event_tick100ns = 0;

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        soc_subsystem_entry_t* e = &g_soc_subsys.entries[i];

        e->subsystem_idx = i;
        e->last_entered_at100ns = 0;
        e->last_exited_at100ns = 0;
        e->stats.wakeup_cnt = 0;
        e->stats.blocked_duration100ns = 0;
        e->stats.sleep_duration100ns = 0;

        if (i == SOC_SUBSYS_SOC)
            RtlInitUnicodeString(&e->parent_name, g_qcom_soc_parent_name);
        else
            e->parent_name = g_soc_subsys.parent_soc_name;

        if (i == SOC_SUBSYS_SOC)
            e->metadata_cnt = 2;
        else if (soc_subsys_is_island_stats_enabled(i))
            e->metadata_cnt = 4;
        else
            e->metadata_cnt = 0;

        RtlInitUnicodeString(&e->name, names[i]);
    }

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++)
        soc_subsys_publish_entry(i);

    soc_subsys_publish_summary();
    soc_subsys_record_event(SOC_SUBSYS_EVENT_INIT, STATUS_SUCCESS, SOC_SUBSYS_SOC, 0, 0);
}

VOID soc_subsys_set_drips_state_id(_In_ ULONG platform_idle_state_idx)
{
    soc_subsys_init_if_needed();

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);
    g_soc_subsys.drips_state_id = platform_idle_state_idx;
    KeReleaseSpinLock(&g_soc_trace_lock, irql);
}

VOID soc_subsys_on_drips_entry(_In_ ULONGLONG now100ns)
{
    ULONG changed[SOC_SUBSYS_CNT];
    ULONG changed_cnt = 0;

    soc_subsys_init_if_needed();
    soc_subsys_on_drips_blocked_end(now100ns);

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    if (g_soc_subsys.in_drips != 0) {
        KeReleaseSpinLock(&g_soc_trace_lock, irql);
        return;
    }

    g_soc_subsys.in_drips = 1;
    g_drips_blocked_start100ns = 0;
    g_drips_blocked_reason = 0;
    InterlockedExchange(&g_drips_blocked, 0);

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        if (!soc_subsys_is_enabled(i))
            continue;

        g_soc_subsys.entries[i].last_entered_at100ns = now100ns;

        if (soc_subsys_is_island_stats_enabled(i)) {
            g_island_entry_time100ns[i] = now100ns;
            g_island_entry_cnt[i]++;
        }

        changed[changed_cnt++] = i;
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    for (ULONG i = 0; i < changed_cnt; i++)
        soc_subsys_publish_entry(changed[i]);

    soc_subsys_publish_summary();
    soc_subsys_record_event(SOC_SUBSYS_EVENT_DRIPS_ENTRY, STATUS_SUCCESS, SOC_SUBSYS_SOC,
                            changed_cnt, now100ns);
}

VOID soc_subsys_on_drips_exit(_In_ ULONGLONG now100ns)
{
    ULONG changed[SOC_SUBSYS_CNT];
    ULONG changed_cnt = 0;
    ULONGLONG soc_delta = 0;

    soc_subsys_init_if_needed();

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    if (g_soc_subsys.in_drips != 1) {
        KeReleaseSpinLock(&g_soc_trace_lock, irql);
        return;
    }

    g_soc_subsys.in_drips = 0;
    g_drips_blocked_start100ns = 0;
    g_drips_blocked_reason = 0;
    InterlockedExchange(&g_drips_blocked, 0);

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        if (!soc_subsys_is_enabled(i))
            continue;

        soc_subsystem_entry_t* e = &g_soc_subsys.entries[i];
        ULONGLONG start = e->last_entered_at100ns;

        e->last_exited_at100ns = now100ns;
        if (now100ns > start && start != 0) {
            ULONGLONG delta = now100ns - start;
            e->stats.sleep_duration100ns += delta;

            if (i == SOC_SUBSYS_SOC) {
                soc_delta = delta;
                g_aoss_sleep_count100ns += delta;
                g_ddr_sleep_count100ns += delta;
            }

            if (soc_subsys_is_island_stats_enabled(i)) {
                g_island_exit_time100ns[i] = now100ns;
                g_island_accum_duration100ns[i] += delta;
            }
        }

        e->stats.wakeup_cnt++;
        changed[changed_cnt++] = i;
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    for (ULONG i = 0; i < changed_cnt; i++)
        soc_subsys_publish_entry(changed[i]);

    soc_subsys_publish_summary();
    soc_subsys_record_event(SOC_SUBSYS_EVENT_DRIPS_EXIT, STATUS_SUCCESS, SOC_SUBSYS_SOC,
                            changed_cnt, soc_delta);
}

VOID soc_subsys_on_drips_blocked_start(_In_ ULONGLONG now100ns, _In_ ULONG reason)
{
    BOOLEAN changed = FALSE;

    soc_subsys_init_if_needed();
    if (!now100ns)
        return;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    if (g_drips_blocked == 0) {
        g_drips_blocked = 1;
        g_drips_blocked_start100ns = now100ns;
        changed = TRUE;
    }

    g_drips_blocked_reason = reason;

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    if (changed) {
        soc_subsys_publish_summary();
        soc_subsys_record_event(SOC_SUBSYS_EVENT_BLOCKED_START, STATUS_SUCCESS, SOC_SUBSYS_SOC,
                                reason, now100ns);
    }
}

VOID soc_subsys_on_drips_blocked_end(_In_ ULONGLONG now100ns)
{
    BOOLEAN had_block = FALSE;
    ULONGLONG delta = 0;
    ULONG reason = 0;

    soc_subsys_init_if_needed();

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    if (g_drips_blocked != 0) {
        ULONGLONG start = g_drips_blocked_start100ns;
        reason = g_drips_blocked_reason;

        g_drips_blocked = 0;
        g_drips_blocked_start100ns = 0;
        g_drips_blocked_reason = 0;
        had_block = TRUE;

        if (start && now100ns > start) {
            delta = now100ns - start;
            g_soc_subsys.entries[SOC_SUBSYS_SOC].stats.blocked_duration100ns += delta;

            if (reason == 1)
                g_drips_blocked_boot100ns += delta;
            else if (reason == 2)
                g_drips_blocked_usb100ns += delta;
            else if (reason == 3)
                g_drips_blocked_dbg100ns += delta;
            else
                g_drips_blocked_other100ns += delta;

            g_last_blocked_reason = reason;
        }
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    if (!had_block)
        return;

    soc_subsys_publish_summary();
    soc_subsys_publish_entry(SOC_SUBSYS_SOC);
    soc_subsys_record_event(SOC_SUBSYS_EVENT_BLOCKED_END, STATUS_SUCCESS, SOC_SUBSYS_SOC, reason,
                            delta);
}

BOOLEAN soc_subsys_query_cnt(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_COUNT* query)
{
    if (!query)
        return FALSE;
    if (!soc_subsys_platform_ok(query->PlatformIdleStateIndex))
        return FALSE;

    query->SubsystemCount = SOC_SUBSYS_CNT;
    return TRUE;
}

BOOLEAN soc_subsys_query_subsystem(_Inout_ PEP_QUERY_SOC_SUBSYSTEM* query)
{
    if (!query)
        return FALSE;
    if (!soc_subsys_platform_ok(query->PlatformIdleStateIndex))
        return FALSE;
    if (query->SubsystemIndex >= SOC_SUBSYS_CNT)
        return FALSE;

    soc_subsystem_entry_t* e = &g_soc_subsys.entries[query->SubsystemIndex];

    query->SubsystemHandle = e;
    query->MetadataCount = e->metadata_cnt;

    if (query->ParentName.Buffer && query->ParentName.MaximumLength)
        RtlCopyUnicodeString(&query->ParentName, &e->parent_name);
    else
        query->ParentName = e->parent_name;

    if (query->SubsystemName.Buffer && query->SubsystemName.MaximumLength)
        RtlCopyUnicodeString(&query->SubsystemName, &e->name);
    else
        query->SubsystemName = e->name;

    return TRUE;
}

BOOLEAN soc_subsys_reset_accounting(_In_ PEP_RESET_SOC_SUBSYSTEM_ACCOUNTING* reset)
{
    if (!reset)
        return FALSE;
    if (!soc_subsys_platform_ok(reset->PlatformIdleStateIndex))
        return FALSE;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    g_soc_subsys.in_drips = 0;

    g_drips_blocked = 0;
    g_drips_blocked_start100ns = 0;
    g_drips_blocked_reason = 0;

    g_drips_blocked_boot100ns = 0;
    g_drips_blocked_usb100ns = 0;
    g_drips_blocked_dbg100ns = 0;
    g_drips_blocked_other100ns = 0;
    g_last_blocked_reason = 0;

    g_aoss_sleep_count100ns = 0;
    g_ddr_sleep_count100ns = 0;

    RtlZeroMemory(g_island_entry_cnt, sizeof(g_island_entry_cnt));
    RtlZeroMemory(g_island_entry_time100ns, sizeof(g_island_entry_time100ns));
    RtlZeroMemory(g_island_exit_time100ns, sizeof(g_island_exit_time100ns));
    RtlZeroMemory(g_island_accum_duration100ns, sizeof(g_island_accum_duration100ns));

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        g_soc_subsys.entries[i].stats.wakeup_cnt = 0;
        g_soc_subsys.entries[i].stats.blocked_duration100ns = 0;
        g_soc_subsys.entries[i].stats.sleep_duration100ns = 0;
        g_soc_subsys.entries[i].last_entered_at100ns = 0;
        g_soc_subsys.entries[i].last_exited_at100ns = 0;
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++)
        soc_subsys_publish_entry(i);

    soc_subsys_publish_summary();
    soc_subsys_record_event(SOC_SUBSYS_EVENT_RESET_ACCOUNTING, STATUS_SUCCESS, SOC_SUBSYS_SOC, 0,
                            0);

    return TRUE;
}

BOOLEAN soc_subsys_query_blocking_time(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_BLOCKING_TIME* query)
{
    if (!query)
        return FALSE;
    if (!soc_subsys_platform_ok(query->PlatformIdleStateIndex))
        return FALSE;

    ULONG idx = 0;
    if (!soc_subsys_try_resolve_handle(query->SubsystemHandle, &idx)) {
        if (!soc_subsys_try_resolve_name(query->SubsystemName, &idx))
            return FALSE;
    }

    ULONGLONG blocking = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);
    blocking = g_soc_subsys.entries[idx].stats.blocked_duration100ns;

    if (idx == SOC_SUBSYS_SOC && g_drips_blocked != 0 && g_drips_blocked_start100ns != 0) {
        ULONGLONG now = KeQueryInterruptTime();
        if (now > g_drips_blocked_start100ns)
            blocking += (now - g_drips_blocked_start100ns);
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    query->BlockingTime = blocking;
    return TRUE;
}

BOOLEAN soc_subsys_query_metadata(_Inout_ PEP_QUERY_SOC_SUBSYSTEM_METADATA* query)
{
    if (!query)
        return FALSE;
    if (!soc_subsys_platform_ok(query->PlatformIdleStateIndex))
        return FALSE;

    ULONG idx = 0;
    if (!soc_subsys_try_resolve_handle(query->SubsystemHandle, &idx)) {
        if (!soc_subsys_try_resolve_name(query->SubsystemName, &idx))
            return FALSE;
    }

    ULONG need = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);
    need = g_soc_subsys.entries[idx].metadata_cnt;
    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    if (query->MetadataCount < need) {
        query->MetadataCount = need;
        return FALSE;
    }

    query->MetadataCount = need;

    if (need == 0)
        return TRUE;

    for (ULONG i = 0; i < need; i++) {
        if (!query->Metadata[i])
            return FALSE;
    }

    wchar_t tmp0[64], tmp1[64], tmp2[64], tmp3[64];
    UNICODE_STRING v0, v1, v2, v3;

    if (idx == SOC_SUBSYS_SOC) {
        ULONGLONG aoss = 0;
        ULONGLONG ddr = 0;

        KeAcquireSpinLock(&g_soc_trace_lock, &irql);
        aoss = g_aoss_sleep_count100ns;
        ddr = g_ddr_sleep_count100ns;
        KeReleaseSpinLock(&g_soc_trace_lock, irql);

        u64_to_unicode(aoss, &v0, tmp0, RTL_NUMBER_OF(tmp0));
        u64_to_unicode(ddr, &v1, tmp1, RTL_NUMBER_OF(tmp1));

        if (!copy_key_value(query->Metadata[0], &g_key_aoss_sleep_cnt, &v0))
            return FALSE;
        if (!copy_key_value(query->Metadata[1], &g_key_ddr_sleep_cnt, &v1))
            return FALSE;

        return TRUE;
    }

    if (soc_subsys_is_island_stats_enabled(idx)) {
        ULONGLONG entry = 0;
        ULONGLONG duration = 0;
        ULONGLONG exit_tick = 0;
        ULONGLONG cnt = 0;

        KeAcquireSpinLock(&g_soc_trace_lock, &irql);
        entry = g_island_entry_time100ns[idx];
        duration = g_island_accum_duration100ns[idx];
        exit_tick = g_island_exit_time100ns[idx];
        cnt = g_island_entry_cnt[idx];
        KeReleaseSpinLock(&g_soc_trace_lock, irql);

        u64_to_unicode(entry, &v0, tmp0, RTL_NUMBER_OF(tmp0));
        u64_to_unicode(duration, &v1, tmp1, RTL_NUMBER_OF(tmp1));
        u64_to_unicode(exit_tick, &v2, tmp2, RTL_NUMBER_OF(tmp2));
        u64_to_unicode(cnt, &v3, tmp3, RTL_NUMBER_OF(tmp3));

        if (!copy_key_value(query->Metadata[0], &g_key_island_entry_time, &v0))
            return FALSE;
        if (!copy_key_value(query->Metadata[1], &g_key_island_accum_duration, &v1))
            return FALSE;
        if (!copy_key_value(query->Metadata[2], &g_key_island_exit_time, &v2))
            return FALSE;
        if (!copy_key_value(query->Metadata[3], &g_key_island_entry_cnt, &v3))
            return FALSE;

        return TRUE;
    }

    return FALSE;
}

VOID soc_subsys_get_snapshot(_Out_ socsubsys_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    soc_subsys_init_if_needed();
    RtlZeroMemory(snapshot, sizeof(*snapshot));

    snapshot->version = 2;
    snapshot->subsystem_cnt = SOC_SUBSYS_CNT;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    snapshot->drips_state_id = g_soc_subsys.drips_state_id;
    snapshot->enabled_mask = g_soc_subsys.enabled_mask;
    snapshot->in_drips = (ULONG)g_soc_subsys.in_drips;
    snapshot->drips_blocked = (ULONG)g_drips_blocked;
    snapshot->drips_blocked_reason = g_drips_blocked_reason;
    snapshot->last_blocked_reason = g_last_blocked_reason;

    snapshot->blocked_boot100ns = g_drips_blocked_boot100ns;
    snapshot->blocked_usb100ns = g_drips_blocked_usb100ns;
    snapshot->blocked_dbg100ns = g_drips_blocked_dbg100ns;
    snapshot->blocked_other100ns = g_drips_blocked_other100ns;

    snapshot->aoss_sleep_count100ns = g_aoss_sleep_count100ns;
    snapshot->ddr_sleep_count100ns = g_ddr_sleep_count100ns;

    snapshot->event_sequence = g_soc_event_sequence;
    snapshot->history_cnt = g_soc_history_cnt;
    snapshot->history_dropped = g_soc_history_dropped;
    snapshot->last_event_code = g_soc_last_event_code;
    snapshot->last_event_status = g_soc_last_event_status;
    snapshot->last_event_subsystem_idx = g_soc_last_event_subsystem_idx;
    snapshot->last_event_reason = g_soc_last_event_reason;
    snapshot->last_event_tick100ns = g_soc_last_event_tick100ns;

    snapshot->current_blocked_duration100ns =
        g_soc_subsys.entries[SOC_SUBSYS_SOC].stats.blocked_duration100ns;
    if (g_drips_blocked != 0 && g_drips_blocked_start100ns != 0) {
        ULONGLONG now = KeQueryInterruptTime();
        if (now > g_drips_blocked_start100ns)
            snapshot->current_blocked_duration100ns += (now - g_drips_blocked_start100ns);
    }

    for (ULONG i = 0; i < SOC_SUBSYS_CNT; i++) {
        snapshot->entries[i].stats = g_soc_subsys.entries[i].stats;
        snapshot->entries[i].last_entered_at100ns = g_soc_subsys.entries[i].last_entered_at100ns;
        snapshot->entries[i].last_exited_at100ns = g_soc_subsys.entries[i].last_exited_at100ns;
        snapshot->entries[i].metadata_cnt = g_soc_subsys.entries[i].metadata_cnt;
        snapshot->entries[i].flags = soc_subsys_entry_flags(i);
        snapshot->entries[i].island_entry_cnt = g_island_entry_cnt[i];
        snapshot->entries[i].island_entry_time100ns = g_island_entry_time100ns[i];
        snapshot->entries[i].island_exit_time100ns = g_island_exit_time100ns[i];
        snapshot->entries[i].island_accum_duration100ns = g_island_accum_duration100ns[i];
    }

    KeReleaseSpinLock(&g_soc_trace_lock, irql);
}

NTSTATUS soc_subsys_get_history(_Out_writes_to_(capacity, *cnt) socsubsys_event_t* events,
                                _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    soc_subsys_init_if_needed();

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    ULONG avail = g_soc_history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;

    ULONG start = (g_soc_history_head + SOCSUBSYS_HISTORY_CAPACITY - avail + skip) %
                  SOCSUBSYS_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % SOCSUBSYS_HISTORY_CAPACITY;
        events[i] = g_soc_history[idx];
    }

    *cnt = take;
    *dropped = g_soc_history_dropped + skip;

    KeReleaseSpinLock(&g_soc_trace_lock, irql);
    return STATUS_SUCCESS;
}

BOOLEAN soc_subsys_get_info(_In_ ULONG idx, _Out_opt_ PCWSTR* parent_arg,
                            _Out_opt_ PCWSTR* name_arg, _Out_opt_ ULONG* metadata_cnt,
                            _Out_opt_ BOOLEAN* enabled_arg,
                            _Out_opt_ soc_subsystem_stats_t* stats_arg)
{
    soc_subsys_init_if_needed();
    if (idx >= SOC_SUBSYS_CNT)
        return FALSE;

    KIRQL irql;
    KeAcquireSpinLock(&g_soc_trace_lock, &irql);

    soc_subsystem_entry_t* e = &g_soc_subsys.entries[idx];
    PCWSTR parent = e->parent_name.Buffer ? e->parent_name.Buffer : L"";
    PCWSTR name = e->name.Buffer ? e->name.Buffer : L"";
    ULONG metadata = e->metadata_cnt;
    BOOLEAN enabled = soc_subsys_is_enabled(idx);
    soc_subsystem_stats_t stats = e->stats;

    KeReleaseSpinLock(&g_soc_trace_lock, irql);

    if (parent_arg)
        *parent_arg = parent;
    if (name_arg)
        *name_arg = name;
    if (metadata_cnt)
        *metadata_cnt = metadata;
    if (enabled_arg)
        *enabled_arg = enabled;
    if (stats_arg)
        *stats_arg = stats;

    return TRUE;
}
