#include "driver.h"
#include <pepfx.h>
#include "dispatch/limits.h"
#include "dcvs.h"
#include "init_gate.h"
#include "interrupt.h"
#include "peplog.h"
#include "resource_cache.h"
#include "soc_subsystem.h"
#include "dispatch/idle_types.h"
#include "dispatch/dump_priv.h"
#include "dispatch/idle_priv.h"
#include "dispatch/main_priv.h"
#if defined(_M_ARM64) || defined(_ARM64_) || defined(ARM64)
#include <arm64intr.h>
#endif

VOID pep_veto_reason_init_if_needed(VOID);
static __forceinline const WCHAR* pep_lookup_veto_reason_text(_In_ ULONG veto_id);
static __forceinline VOID pep_pending_veto_init_if_needed(VOID);
VOID pep_queue_pending_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id,
                            _In_ UCHAR increment);
VOID pep_query_platform_state_residencies(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_veto_reasons(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_veto_reason(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_query_enumerate_boot_vetoes(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
static __forceinline VOID idle_free_unicode(_Inout_ UNICODE_STRING* u);
static NTSTATUS idle_alloc_unicode_from_range(_Out_ UNICODE_STRING* out, _In_reads_(cch) PCWSTR s,
                                              _In_ SIZE_T cch);
static NTSTATUS idle_extract_lpr_name(_In_opt_ PCWSTR state_name, _Out_ UNICODE_STRING* lpr_name);
static NTSTATUS idle_extract_mode_name(_In_opt_ PCWSTR state_name, _Out_ UNICODE_STRING* mode_name);
VOID idle_acpi_ensure_init(VOID);
static ULONG idle_fill_lpr_array(_In_ idle_ctx_t* ctx, _In_ const UNICODE_STRING* lpr_name);
VOID idle_acpi_free(VOID);
VOID idle_internal_query_state_name(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num,
                                    _Inout_ pep_ppm_query_state_name_t* state_name);
VOID idle_internal_get_proc_state_data(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num, _In_ ULONG cnt,
                                       _Out_writes_(cnt) pep_proc_idle_state_v2_t* idle_states);
VOID idle_internal_get_coordinated_state_dependency(
    _In_ idle_ctx_t* cntxt, _Inout_ pep_ppm_query_coordinated_dependency_t* dep);
BOOLEAN pep_send_idle_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id,
                           _In_ UCHAR increment);
VOID pep_flush_pending_vetoes(VOID);
static ULONG pep_query_registry_veto_feature(_In_ ULONG feature_id);
VOID pep_init_and_apply_registry_vetoes_if_needed(VOID);
static __forceinline ULONG pep_boot_veto_effective_id(VOID);
static BOOLEAN pep_get_drips_lpr_mode(_Out_ ULONG* lpr_id, _Out_ ULONG* mode_id);
VOID pep_remove_boot_veto_if_applied(VOID);
VOID pep_apply_boot_veto_if_possible(VOID);
static ULONGLONG pep_get_drips_residency_now100ns(VOID);
static VOID hw_drips_timer_expiry(_In_ WDFTIMER timer);
static VOID idle_start_hw_drips_test_timer(VOID);
static VOID idle_stop_hw_drips_test_timer(VOID);
VOID idle_unmap(VOID);
static VOID idle_enumerate_unmasked_irqs(_In_ ULONG level, _In_ ULONG coordinated_mode_idx);
static VOID idle_turn_off_suppressible_resources(_In_ ULONG coordinated_mode_idx);
static VOID idle_turn_on_suppressible_resources(_In_ ULONG coordinated_mode_idx);
static VOID idle_transition_debugger(_In_ UCHAR transition, _In_ ULONG coordinated_mode_idx);
NTSTATUS idle_rpmh_sync_enter(_In_ ULONG coordinated_mode_idx);
VOID idle_rpmh_sync_exit(_In_ ULONG coordinated_mode_idx);
static VOID idle_wake_reason_mon(_In_ ULONG coordinated_mode_idx);
static BOOLEAN idle_match_pattern(_In_opt_ PCWSTR text, _In_z_ PCWSTR pattern);
VOID idle_ensure_init(VOID);
static NTSTATUS idle_clock_sleep(_In_ ULONG mode, _In_ ULONG clock_flags);
static NTSTATUS idle_clock_restore(_In_ ULONG mode, _In_ ULONG clock_flags);
static NTSTATUS idle_cx_pre_execute(_In_ ULONG mode_idx);
static NTSTATUS idle_cx_complete(_In_ ULONG mode_idx);
VOID pep_override_lpi_support(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_update_lpi_proc_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_update_lpi_coordinated_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_idle_lpi_pre_execute(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_idle_lpi_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_ppm_enter_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);
VOID pep_ppm_from_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

pep_veto_ctx_t g_veto;
FAST_MUTEX g_veto_reason_lock;
volatile LONG g_veto_reason_init;
UNICODE_STRING g_veto_reason_names[MAX_VETO_REASONS + 1];
volatile LONG g_veto_reason_cnt;
pep_residency_ctx_t g_resid;
static pep_mode_residency_ctx_t* g_platform_mode_resid;
static ULONG g_platform_mode_resid_cnt;
ULONG g_drips_coordinated_mode_idx;
ULONG g_boot_veto_enabled = 1;
ULONG g_boot_veto_id = 1;
ULONG g_registry_veto_id = 1;
volatile LONG g_registry_veto_applied;
volatile LONG g_boot_veto_cleared;
static volatile LONG g_boot_veto_applied;
static ULONG g_boot_veto_lpr_id = 0xFFFFFFFFu;
static ULONG g_boot_veto_mode_id = 0xFFFFFFFFu;
const WCHAR g_veto_reason_boot_drips[] = L"Cold boot or Hibernate in Progress";
static const WCHAR g_veto_reason_unknown[] =
    L"Unknown veto reason (registered after OS enumeration of vetoes)";
static const WCHAR g_veto_reason_reserved[] = L"Reserved veto reason";
static const idle_registry_veto_entry_t g_registry_veto_map[] = {
    {L"K*", L"C2", 21u, 0x000000FFu, 1u},        {L"K*", L"C3", 20u, 0x000000FFu, 0u},
    {L"K*", L"C4", 20u, 0x0000FF00u, 0u},        {L"L3Cluster", L"D2", 25u, 0x00000001u, 1u},
    {L"L3Cluster", L"D4", 25u, 0x00000002u, 0u}, {L"plat*", L"DRIPS", 18u, 0x00000001u, 0u},
};
static SLIST_HEADER g_pending_veto_s_list;
static volatile LONG g_pending_veto_init;
static volatile LONG g_pending_veto_cnt;
volatile LONG g_vetoes_enumerated;
idle_engine_t g_idle;
idle_ctx_t g_idle_acpi;
static volatile LONG g_idle_acpi_init;

extern __forceinline VOID pep_veto_reason_init_if_needed(VOID)
{
    /* publishing 1 is not sufficient: concurrent callers must not touch an uninitialized
     * mutex. */

    if (InterlockedCompareExchange(&g_veto_reason_init, 1, 0) == 0) {
        ExInitializeFastMutex(&g_veto_reason_lock);
        RtlZeroMemory(g_veto_reason_names, sizeof(g_veto_reason_names));
        g_veto_reason_cnt = 0;
        InterlockedExchange(&g_veto_reason_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_veto_reason_init, 2, 2) != 2) {
            /* the initializer cannot fail and performs no blocking I/O. */
            YieldProcessor();
        }
    }
}

static __forceinline const WCHAR* pep_lookup_veto_reason_text(_In_ ULONG veto_id)
{
    pep_veto_reason_init_if_needed();

    if (veto_id <= MAX_VETO_REASONS && g_veto_reason_names[veto_id].Buffer)
        return g_veto_reason_names[veto_id].Buffer;

    if (veto_id == g_boot_veto_id)
        return g_veto_reason_boot_drips;

    if (veto_id == MAX_VETO_REASONS)
        return g_veto_reason_unknown;

    if (veto_id >= 1 && veto_id <= MAX_VETO_REASONS)
        return g_veto_reason_reserved;

    return g_veto_reason_unknown;
}

static __forceinline VOID pep_pending_veto_init_if_needed(VOID)
{

    if (InterlockedCompareExchange(&g_pending_veto_init, 1, 0) == 0) {
        InitializeSListHead(&g_pending_veto_s_list);
        g_pending_veto_cnt = 0;
        InterlockedExchange(&g_pending_veto_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_pending_veto_init, 2, 2) != 2)
            YieldProcessor();
    }
}

extern __forceinline VOID pep_queue_pending_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id,
                                                 _In_ ULONG veto_id, _In_ UCHAR increment)
{
    pep_pending_veto_init_if_needed();

    LONG c = InterlockedIncrement(&g_pending_veto_cnt);
    if ((ULONG)c > MAX_PENDING_VETO) {
        InterlockedDecrement(&g_pending_veto_cnt);
        return;
    }

    pep_pending_veto_t* e =
        (pep_pending_veto_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*e), POOL_TAG);
    if (!e) {
        InterlockedDecrement(&g_pending_veto_cnt);
        return;
    }

    RtlZeroMemory(e, sizeof(*e));
    e->lpr_id = lpr_id;
    e->mode_id = mode_id;
    e->veto_id = veto_id;
    e->increment = increment;

    InterlockedPushEntrySList(&g_pending_veto_s_list, &e->link);

    pep_log_write(PEPLOG_EVT_IDLE, 0x60, STATUS_PENDING, lpr_id, mode_id, veto_id,
                  increment ? 1u : 0u);

    pep_req_kernel_work();
}

VOID pep_query_platform_state_residencies(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    __try {
        pep_ppm_platform_state_residencies_t* r = (pep_ppm_platform_state_residencies_t*)data;
        if (!r->states || r->cnt == 0)
            return;

        ULONG n = r->cnt;

        pep_ppm_platform_state_residency_t* s = r->states;
        ULONGLONG now = pep_now100ns();

        for (ULONG i = 0; i < n; i++) {
            ULONGLONG resid = 0;
            ULONGLONG trans = 0;

            if (g_platform_mode_resid && i < g_platform_mode_resid_cnt) {
                pep_mode_residency_ctx_t* pr = &g_platform_mode_resid[i];
                resid = (ULONGLONG)pr->residency100ns;
                trans = (ULONGLONG)pr->transition_cnt;
                if (pr->in_state) {
                    ULONGLONG start = pr->enter_time100ns;
                    if (now > start)
                        resid += (now - start);
                }
            }

            if (i == g_drips_coordinated_mode_idx) {
                resid = (ULONGLONG)g_resid.drips_residency100ns;
                trans = (ULONGLONG)g_resid.drips_transition_cnt;
                if (g_resid.in_drips) {
                    ULONGLONG start = g_resid.drips_enter_time100ns;
                    if (now > start)
                        resid += (now - start);
                }
            }

            s[i].residency = resid;
            s[i].transition_cnt = trans;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_query_veto_reasons(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    __try {
        *(ULONG*)data = MAX_VETO_REASONS;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_query_veto_reason(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    __try {
        pep_ppm_query_veto_reason_t* q = (pep_ppm_query_veto_reason_t*)data;
        ULONG id = q->veto_reason;

        if (id != MAX_VETO_REASONS) {
            if (id < 1 || id > MAX_VETO_REASONS)
                return;

            pep_veto_reason_init_if_needed();
            if (!g_veto_reason_names[id].Buffer)
                return;
        }

        const WCHAR* txt = pep_lookup_veto_reason_text(q->veto_reason);
        SIZE_T cch = 0;
        while (txt[cch])
            cch++;
        USHORT need_chars = (USHORT)(cch + 1u);

        if (!q->name) {
            q->name_size = need_chars;
            return;
        }

        if (!q->name_size)
            return;

        if (((SIZE_T)q->name_size * sizeof(WCHAR)) < (cch * sizeof(WCHAR)))
            return;

        RtlCopyMemory(q->name, txt, cch * sizeof(WCHAR));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

VOID pep_query_enumerate_boot_vetoes(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(pep_handle);

    if (!g_boot_veto_enabled || g_boot_veto_cleared)
        return;

    (VOID) InterlockedExchange(&g_vetoes_enumerated, 1);
    (VOID) InterlockedExchange(&g_veto.boot_veto_enumerated, 1);

    pep_apply_boot_veto_if_possible();
    pep_flush_pending_vetoes();
}

static __forceinline VOID idle_free_unicode(_Inout_ UNICODE_STRING* u)
{
    if (!u)
        return;
    if (u->Buffer)
        ExFreePool(u->Buffer);
    u->Buffer = NULL;
    u->Length = 0;
    u->MaximumLength = 0;
}

static NTSTATUS idle_alloc_unicode_from_range(_Out_ UNICODE_STRING* out, _In_reads_(cch) PCWSTR s,
                                              _In_ SIZE_T cch)
{
    if (!out)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(out, sizeof(*out));
    if (!s)
        return STATUS_INVALID_PARAMETER;

    if (cch > 0x7FFF)
        cch = 0x7FFF;

    USHORT bytes = (USHORT)((cch + 1) * sizeof(WCHAR));
    PWSTR buf = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(buf, bytes);
    if (cch)
        RtlCopyMemory(buf, s, cch * sizeof(WCHAR));

    out->Buffer = buf;
    out->Length = (USHORT)(cch * sizeof(WCHAR));
    out->MaximumLength = bytes;
    return STATUS_SUCCESS;
}

static NTSTATUS idle_extract_lpr_name(_In_opt_ PCWSTR state_name, _Out_ UNICODE_STRING* lpr_name)
{
    if (!state_name || !lpr_name)
        return STATUS_INVALID_PARAMETER;
    SIZE_T i = 0;
    while (state_name[i] && state_name[i] != L'.')
        i++;
    if (i == 0)
        return STATUS_INVALID_PARAMETER;
    return idle_alloc_unicode_from_range(lpr_name, state_name, i);
}

static NTSTATUS idle_extract_mode_name(_In_opt_ PCWSTR state_name, _Out_ UNICODE_STRING* mode_name)
{
    if (!state_name || !mode_name)
        return STATUS_INVALID_PARAMETER;
    SIZE_T i = 0;
    while (state_name[i] && state_name[i] != L'.')
        i++;
    if (!state_name[i])
        return idle_alloc_unicode_from_range(mode_name, L"", 0);
    i++;
    SIZE_T j = i;
    while (state_name[j])
        j++;
    return idle_alloc_unicode_from_range(mode_name, state_name + i, (j > i) ? (j - i) : 0);
}

VOID idle_acpi_ensure_init(VOID)
{
    /* allocating ACPI idle arrays at such IRQL is not allowed, and status 1 is not a published
     * graph. A concurrent callback must observe only state 2. */

    if (init_gate_ready(&g_idle_acpi_init) || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    if (init_gate_begin(&g_idle_acpi_init) != 1)
        return;

    RtlZeroMemory(&g_idle_acpi, sizeof(g_idle_acpi));

    ULONG cpu = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (!cpu)
        cpu = 1;
    g_idle_acpi.cpu_cnt = cpu;
    g_idle_acpi.lpr_array_size = cpu + 16;

    g_idle_acpi.lpr_array = (idle_lpr_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(idle_lpr_t) * g_idle_acpi.lpr_array_size, POOL_TAG);
    if (!g_idle_acpi.lpr_array) {
        RtlZeroMemory(&g_idle_acpi, sizeof(g_idle_acpi));
        InterlockedExchange(&g_idle_acpi_init, 0);
        return;
    }
    RtlZeroMemory(g_idle_acpi.lpr_array, sizeof(idle_lpr_t) * g_idle_acpi.lpr_array_size);
    init_gate_publish(&g_idle_acpi_init);
}

static ULONG idle_fill_lpr_array(_In_ idle_ctx_t* ctx, _In_ const UNICODE_STRING* lpr_name)
{
    if (!ctx || !ctx->lpr_array || !lpr_name || !lpr_name->Buffer)
        return 0;

    for (ULONG i = 0; i < ctx->lpr_array_size; i++) {
        if (ctx->lpr_array[i].lpr_name.Buffer) {
            if (RtlCompareUnicodeString(&ctx->lpr_array[i].lpr_name, (UNICODE_STRING*)lpr_name,
                                        TRUE) == 0)
                return i;
        }
    }

    for (ULONG i = ctx->cpu_cnt; i < ctx->lpr_array_size; i++) {
        if (!ctx->lpr_array[i].lpr_name.Buffer) {
            NTSTATUS st = idle_alloc_unicode_from_range(
                &ctx->lpr_array[i].lpr_name, lpr_name->Buffer, lpr_name->Length / sizeof(WCHAR));
            if (!NT_SUCCESS(st))
                return 0;
            ctx->lpr_array[i].lpr_id = i;
            ctx->lpr_array[i].core_mask = 0xFFFFFFFFu;
            ctx->lpr_array[i].is_avail = 1;
            return i;
        }
    }

    return 0;
}

VOID idle_acpi_free(VOID)
{
    if (!init_gate_ready(&g_idle_acpi_init))
        return;

    if (g_idle_acpi.coordinated_modes_array) {
        for (ULONG i = 0; i < g_idle_acpi.coordinated_modes_array_size; i++) {
            idle_mode_t* m = g_idle_acpi.coordinated_modes_array[i];
            if (!m)
                continue;
            idle_free_unicode(&m->mode_name);
            if (m->dependencies) {
                for (ULONG j = 0; j < m->dependency_cnt; j++) {
                    if (m->dependencies[j].options)
                        ExFreePool(m->dependencies[j].options);
                }
                ExFreePool(m->dependencies);
            }
            ExFreePool(m);
            g_idle_acpi.coordinated_modes_array[i] = NULL;
        }
        ExFreePool(g_idle_acpi.coordinated_modes_array);
        g_idle_acpi.coordinated_modes_array = NULL;
    }

    if (g_idle_acpi.lpr_array) {
        for (ULONG i = 0; i < g_idle_acpi.lpr_array_size; i++) {
            idle_free_unicode(&g_idle_acpi.lpr_array[i].lpr_name);
            if (g_idle_acpi.lpr_array[i].mode_array) {
                for (ULONG j = 0; j < g_idle_acpi.lpr_array[i].mode_cnt; j++)
                    idle_free_unicode(&g_idle_acpi.lpr_array[i].mode_array[j].mode_name);
                ExFreePool(g_idle_acpi.lpr_array[i].mode_array);
            }
            g_idle_acpi.lpr_array[i].mode_array = NULL;
            g_idle_acpi.lpr_array[i].mode_cnt = 0;
        }
        ExFreePool(g_idle_acpi.lpr_array);
        g_idle_acpi.lpr_array = NULL;
    }

    RtlZeroMemory(&g_idle_acpi, sizeof(g_idle_acpi));
    InterlockedExchange(&g_idle_acpi_init, 0);

    if (g_platform_mode_resid) {
        ExFreePool(g_platform_mode_resid);
        g_platform_mode_resid = NULL;
        g_platform_mode_resid_cnt = 0;
    }
}

VOID idle_internal_query_state_name(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num,
                                    _Inout_ pep_ppm_query_state_name_t* state_name)
{
    if (!cntxt || !state_name || !cntxt->lpr_array)
        return;

    idle_mode_t* mode = NULL;
    idle_mode_t* mode_for_name = NULL;
    ULONG idx = state_name->state_idx;

    if (proc_num == 0xFFFFFFFFu) {
        if (idx < cntxt->coordinated_modes_array_size) {
            mode = cntxt->coordinated_modes_array[idx];
            mode_for_name = mode;
        }
    } else if (proc_num < cntxt->lpr_array_size) {
        if (cntxt->lpr_array[proc_num].mode_array && idx < cntxt->lpr_array[proc_num].mode_cnt) {
            mode = &cntxt->lpr_array[proc_num].mode_array[idx];
            mode_for_name = mode;
        }
    }

    if (!mode_for_name)
        return;

    if (!state_name->name) {
        USHORT cch = (USHORT)((mode_for_name->mode_name.Length + 4 +
                               cntxt->lpr_array[mode_for_name->lpr_id].lpr_name.Length) /
                              sizeof(WCHAR));
        state_name->name_size = cch;
        return;
    }

    if (!state_name->name_size)
        return;

    (VOID) RtlStringCchPrintfW(state_name->name, state_name->name_size, L"%ws.%ws",
                               cntxt->lpr_array[mode_for_name->lpr_id].lpr_name.Buffer
                                   ? cntxt->lpr_array[mode_for_name->lpr_id].lpr_name.Buffer
                                   : L"",
                               mode_for_name->mode_name.Buffer ? mode_for_name->mode_name.Buffer
                                                               : L"");
}

VOID idle_internal_get_proc_state_data(_In_ idle_ctx_t* cntxt, _In_ ULONG proc_num, _In_ ULONG cnt,
                                       _Out_writes_(cnt) pep_proc_idle_state_v2_t* idle_states)
{
    if (!cntxt || !idle_states || !cntxt->lpr_array)
        return;
    if (proc_num >= cntxt->lpr_array_size)
        return;
    if (!cntxt->lpr_array[proc_num].mode_array)
        return;

    ULONG mode_cnt = cntxt->lpr_array[proc_num].mode_cnt;
    if (cnt > mode_cnt)
        cnt = mode_cnt;

    for (ULONG i = 0; i < cnt; i++) {
        idle_states[i].flags = 0;
        idle_states[i].latency = cntxt->lpr_array[proc_num].mode_array[i].attr.latency;
        idle_states[i].break_even_duration =
            cntxt->lpr_array[proc_num].mode_array[i].attr.break_even;
    }
}

VOID idle_internal_get_coordinated_state_dependency(
    _In_ idle_ctx_t* cntxt, _Inout_ pep_ppm_query_coordinated_dependency_t* dep)
{
    if (!cntxt || !dep)
        return;

    dep->dependency_size_used = 0;
    if (dep->state_idx >= cntxt->coordinated_modes_array_size)
        return;

    idle_mode_t* mode = cntxt->coordinated_modes_array[dep->state_idx];
    if (!mode || !mode->dependencies)
        return;

    if (dep->dependency_idx >= mode->dependency_cnt)
        return;

    idle_dependency_t* d = &mode->dependencies[dep->dependency_idx];
    if (!d->cnt)
        return;

    SIZE_T needed = (SIZE_T)d->cnt * sizeof(pep_coordinated_dependency_option_t);
    dep->dependency_size_used = (ULONG)needed;

    if (dep->dependency_size < needed)
        return;

    for (ULONG i = 0; i < d->cnt; i++) {
        UCHAR sidx = 0;
        if (d->options) {
            ULONG v = 0;
            __try {
                v = d->options[i];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                v = 0;
            }
            if (v > 0xFF)
                v = 0;
            sidx = (UCHAR)v;
        }
        dep->options[i].expected_state_idx = sidx;
        dep->options[i].loose_dependency = 0;
        dep->options[i].initiating_state = 0;
        dep->options[i].dependent_state = 0;
    }
}

BOOLEAN pep_send_idle_veto(_In_ ULONG lpr_id, _In_ ULONG mode_id, _In_ ULONG veto_id,
                           _In_ UCHAR increment)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    if (!g_vetoes_enumerated)
        return FALSE;
    if (!g_kernel_info.ProcessorIdleVeto || !g_kernel_info.PlatformIdleVeto)
        return FALSE;

    idle_acpi_ensure_init();

    if (!g_idle_acpi.lpr_array || lpr_id >= g_idle_acpi.lpr_array_size)
        return FALSE;

    idle_lpr_t* lpr = &g_idle_acpi.lpr_array[lpr_id];
    if (!lpr->mode_array || mode_id >= lpr->mode_cnt)
        return FALSE;

    idle_mode_t* mode = &lpr->mode_array[mode_id];

    if (lpr->core_mask == 0xFFFFFFFFu) {
        if (!lpr->is_avail)
            return FALSE;

        ULONG coordinated = lpr->mode_array[mode_id].coordinated_mode_id;
        ULONG cpu_now = pep_get_current_cpu_num();
        PVOID h = pep_get_kernel_handle_from_cpu_num(cpu_now);
        if (!h)
            return FALSE;

        pfn_idle_veto_t fn = (pfn_idle_veto_t)g_kernel_info.PlatformIdleVeto;
        int st = fn(h, coordinated, veto_id, increment);
        if (st >= 0) {
            if (increment)
                InterlockedIncrement(&mode->veto_reference_cnt);
            else
                pep_dec_ref32(&mode->veto_reference_cnt);
        }
        pep_log_write(PEPLOG_EVT_IDLE, 0x61, (NTSTATUS)st, cpu_now, coordinated, veto_id,
                      increment ? 1u : 0u);
        return TRUE;
    }

    ULONG cpu = lpr->core_mask;
    PVOID h = pep_get_kernel_handle_from_cpu_num(cpu);
    if (!h)
        return FALSE;

    pfn_idle_veto_t fn = (pfn_idle_veto_t)g_kernel_info.ProcessorIdleVeto;
    int st = fn(h, mode_id, veto_id, increment);
    if (st >= 0) {
        if (increment)
            InterlockedIncrement(&mode->veto_reference_cnt);
        else
            pep_dec_ref32(&mode->veto_reference_cnt);
    }
    pep_log_write(PEPLOG_EVT_IDLE, 0x61, (NTSTATUS)st, cpu, mode_id, veto_id, increment ? 1u : 0u);
    return TRUE;
}

VOID pep_flush_pending_vetoes(VOID)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    if (!g_vetoes_enumerated)
        return;
    if (!g_kernel_info.ProcessorIdleVeto || !g_kernel_info.PlatformIdleVeto)
        return;
    if (!g_pending_veto_init)
        return;

    for (ULONG i = 0; i < 1024; i++) {
        PSLIST_ENTRY le = InterlockedPopEntrySList(&g_pending_veto_s_list);
        if (!le)
            break;

        InterlockedDecrement(&g_pending_veto_cnt);

        pep_pending_veto_t* e = CONTAINING_RECORD(le, pep_pending_veto_t, link);

        if (pep_send_idle_veto(e->lpr_id, e->mode_id, e->veto_id, e->increment)) {
            ExFreePool(e);
            continue;
        }

        InterlockedIncrement(&g_pending_veto_cnt);
        InterlockedPushEntrySList(&g_pending_veto_s_list, &e->link);
        break;
    }
}

static ULONG pep_query_registry_veto_feature(_In_ ULONG feature_id)
{
    WCHAR feature_name[48];
    if (!NT_SUCCESS(RtlStringCchPrintfW(feature_name, RTL_NUMBER_OF(feature_name),
                                        L"RegistryVetoFeature%u", feature_id)))
        return 0;
    return registry_query_u_long(feature_name, 0);
}

VOID pep_init_and_apply_registry_vetoes_if_needed(VOID)
{
    if (InterlockedCompareExchange(&g_registry_veto_applied, 1, 0) != 0)
        return;

    if (g_registry_veto_id < 1 || g_registry_veto_id > MAX_VETO_REASONS)
        return;

    idle_acpi_ensure_init();
    if (!g_idle_acpi.lpr_array || !g_idle_acpi.lpr_array_size)
        return;

    for (ULONG i = 0; i < g_idle_acpi.lpr_array_size; i++) {
        idle_lpr_t* lpr = &g_idle_acpi.lpr_array[i];
        if (!lpr->lpr_name.Buffer || !lpr->mode_array || !lpr->mode_cnt)
            continue;

        for (ULONG j = 0; j < lpr->mode_cnt; j++) {
            idle_mode_t* mode = &lpr->mode_array[j];
            if (!mode->mode_name.Buffer)
                continue;

            for (ULONG k = 0; k < RTL_NUMBER_OF(g_registry_veto_map); k++) {
                const idle_registry_veto_entry_t* e = &g_registry_veto_map[k];
                if (!idle_match_pattern(lpr->lpr_name.Buffer, e->lpr_pattern))
                    continue;
                if (!idle_match_pattern(mode->mode_name.Buffer, e->mode_pattern))
                    continue;

                ULONG output = pep_query_registry_veto_feature(e->registry_feature_id);
                output &= e->mask;

                BOOLEAN apply = FALSE;
                if (e->force_disable) {
                    apply = TRUE;
                } else if (output) {
                    if (lpr->core_mask == 0xFFFFFFFFu) {
                        apply = TRUE;
                    } else if (lpr->core_mask < 32u && ((output & (1u << lpr->core_mask)) != 0)) {
                        apply = TRUE;
                    } else if (e->registry_feature_id == 20u && lpr->core_mask < 32u) {
                        ULONG cpu_cnt = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
                        if (cpu_cnt < 32u) {
                            ULONG shifted = output >> cpu_cnt;
                            if ((shifted & (1u << lpr->core_mask)) != 0)
                                apply = TRUE;
                        }
                    }
                }

                if (apply)
                    (VOID) pep_apply_veto(lpr->lpr_id, mode->mode_id, g_registry_veto_id);
            }
        }
    }
}

static __forceinline ULONG pep_boot_veto_effective_id(VOID)
{
    ULONG id = g_boot_veto_id;
    if (id < 1 || id > MAX_VETO_REASONS)
        id = MAX_VETO_REASONS;
    return id;
}

static BOOLEAN pep_get_drips_lpr_mode(_Out_ ULONG* lpr_id, _Out_ ULONG* mode_id)
{
    if (!lpr_id || !mode_id)
        return FALSE;

    idle_acpi_ensure_init();

    if (!g_idle_acpi.coordinated_modes_array ||
        g_drips_coordinated_mode_idx >= g_idle_acpi.coordinated_modes_array_size)
        return FALSE;

    idle_mode_t* m = g_idle_acpi.coordinated_modes_array[g_drips_coordinated_mode_idx];
    if (!m)
        return FALSE;

    *lpr_id = m->lpr_id;
    *mode_id = m->mode_id;
    return TRUE;
}

VOID pep_remove_boot_veto_if_applied(VOID)
{
    if (InterlockedCompareExchange(&g_boot_veto_applied, 0, 1) != 1)
        return;

    ULONG lpr = g_boot_veto_lpr_id;
    ULONG mode = g_boot_veto_mode_id;
    g_boot_veto_lpr_id = 0xFFFFFFFFu;
    g_boot_veto_mode_id = 0xFFFFFFFFu;

    if (lpr == 0xFFFFFFFFu || mode == 0xFFFFFFFFu)
        return;

    (VOID) pep_remove_veto(lpr, mode, pep_boot_veto_effective_id());
}

VOID pep_apply_boot_veto_if_possible(VOID)
{
    if (!g_boot_veto_enabled || g_boot_veto_cleared)
        return;

    ULONG lpr = 0xFFFFFFFFu;
    ULONG mode = 0xFFFFFFFFu;

    if (!pep_get_drips_lpr_mode(&lpr, &mode))
        return;

    if (g_boot_veto_applied && g_boot_veto_lpr_id == lpr && g_boot_veto_mode_id == mode)
        return;

    if (g_boot_veto_applied)
        pep_remove_boot_veto_if_applied();

    (VOID) pep_apply_veto(lpr, mode, pep_boot_veto_effective_id());
    g_boot_veto_lpr_id = lpr;
    g_boot_veto_mode_id = mode;
    InterlockedExchange(&g_boot_veto_applied, 1);
}

/* idle diagnostics have no authorized PDC/QUP owner and perform no physical MMIO probes. */
static ULONGLONG pep_get_drips_residency_now100ns(VOID)
{
    ULONGLONG resid = (ULONGLONG)g_resid.drips_residency100ns;
    if (g_resid.in_drips) {
        ULONGLONG now = pep_now100ns();
        ULONGLONG start = g_resid.drips_enter_time100ns;
        if (now > start)
            resid += (now - start);
    }
    return resid;
}

static VOID hw_drips_timer_expiry(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);

    if (!g_idle.hw_drips_test_timeout_seconds)
        return;

    if (InterlockedCompareExchange(&g_idle.hw_drips_active, 0, 1) != 1)
        return;

    ULONGLONG now_resid = pep_get_drips_residency_now100ns();
    ULONGLONG now_trans = (ULONGLONG)g_resid.drips_transition_cnt;

    if (now_resid == g_idle.hw_drips_start_residency100ns &&
        now_trans == g_idle.hw_drips_start_transition_cnt) {
        pep_log_write(PEPLOG_EVT_IDLE, 0x3D, STATUS_TIMEOUT, (ULONG)(now_resid >> 32),
                      (ULONG)now_resid, (ULONG)now_trans, 0);

        if (g_idle.hw_drips_bugcheck_on_failure && pep_is_bugcheck_enabled(0x10u))
            KeBugCheckEx(0x14E, 0x48574452, (ULONG_PTR)g_idle.hw_drips_start_residency100ns,
                         (ULONG_PTR)now_resid, (ULONG_PTR)now_trans);
    } else {
        pep_log_write(PEPLOG_EVT_IDLE, 0x3C, STATUS_SUCCESS, (ULONG)(now_resid >> 32),
                      (ULONG)now_resid, (ULONG)now_trans, 0);
    }
}

static VOID idle_start_hw_drips_test_timer(VOID)
{
    if (!g_idle.hw_drips_test_timeout_seconds || !g_idle.hw_drips_timer)
        return;

    g_idle.hw_drips_start_residency100ns = pep_get_drips_residency_now100ns();
    g_idle.hw_drips_start_transition_cnt = (ULONGLONG)g_resid.drips_transition_cnt;
    g_idle.hw_drips_start_time100ns = pep_now100ns();

    InterlockedExchange(&g_idle.hw_drips_active, 1);
    WdfTimerStart(g_idle.hw_drips_timer,
                  WDF_REL_TIMEOUT_IN_SEC(g_idle.hw_drips_test_timeout_seconds));
}

static VOID idle_stop_hw_drips_test_timer(VOID)
{
    if (!g_idle.hw_drips_test_timeout_seconds || !g_idle.hw_drips_timer)
        return;

    (VOID) WdfTimerStop(g_idle.hw_drips_timer, FALSE);
    InterlockedExchange(&g_idle.hw_drips_active, 0);
}

VOID idle_unmap(VOID)
{
    if (g_idle.hw_drips_timer) {
        (VOID) WdfTimerStop(g_idle.hw_drips_timer, TRUE);
    }

    /* no physical window may be mapped by this unowned diagnostic path. */
    RtlZeroMemory(&g_idle.qup, sizeof(g_idle.qup));
    g_idle.wake_pdc_map_base = NULL;
    g_idle.wake_pdc_map_bytes = 0;
    g_idle.wake_pdc_status_map = NULL;
    g_idle.wake_pdc_word_cnt = 0;

    InterlockedExchange(&g_idle.initialized, 0);
    g_idle.enabled = 0;
    g_idle.clock_flags = 0;
    g_idle.map_qup_registers = 0;
}

static VOID idle_enumerate_unmasked_irqs(_In_ ULONG level, _In_ ULONG coordinated_mode_idx)
{
    UNREFERENCED_PARAMETER(level);
    if (!g_idle.enabled)
        return;
    if (!g_idle.wake_reason_enabled)
        return;
    interrupt_record_idle_scan(level, coordinated_mode_idx);
    pep_log_write(PEPLOG_EVT_IDLE, 0x18, STATUS_SUCCESS, coordinated_mode_idx, level,
                  pep_get_current_cpu_number(), 0);
}

static VOID idle_turn_off_suppressible_resources(_In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled)
        return;
    pep_log_write(PEPLOG_EVT_IDLE, 0x19, STATUS_SUCCESS, coordinated_mode_idx, 0, 0, 0);
}

static VOID idle_turn_on_suppressible_resources(_In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled)
        return;
    pep_log_write(PEPLOG_EVT_IDLE, 0x1A, STATUS_SUCCESS, coordinated_mode_idx, 0, 0, 0);
}

static VOID idle_transition_debugger(_In_ UCHAR transition, _In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled)
        return;

    pep_transition_debugger_critical_res(transition);

    if (!g_idle.transition_debugger_enabled)
        return;
    pep_log_write(PEPLOG_EVT_IDLE, 0x1B, STATUS_SUCCESS, coordinated_mode_idx, transition, 0, 0);
}

NTSTATUS idle_rpmh_sync_enter(_In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled || !g_idle.rpmh_enabled)
        return STATUS_SUCCESS;

    if (InterlockedCompareExchange(&g_idle.in_rpmh, 1, 0) != 0)
        return STATUS_SUCCESS;

    NTSTATUS st = STATUS_SUCCESS;
    if (!g_idle.rpmh_assume_cmdq_empty)
        st = STATUS_UNSUCCESSFUL;

    if (!NT_SUCCESS(st)) {
        InterlockedIncrement(&g_idle.rpmh_enter_failures);
        InterlockedExchange(&g_idle.in_rpmh, 0);
    }

    pep_log_write(PEPLOG_EVT_IDLE, 0x1C, st, coordinated_mode_idx, g_idle.rpmh_assume_cmdq_empty, 0,
                  0);
    return st;
}

VOID idle_rpmh_sync_exit(_In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled || !g_idle.rpmh_enabled)
        return;

    if (InterlockedCompareExchange(&g_idle.in_rpmh, 0, 1) == 1)
        pep_log_write(PEPLOG_EVT_IDLE, 0x1D, STATUS_SUCCESS, coordinated_mode_idx, 0, 0, 0);
}

static VOID idle_wake_reason_mon(_In_ ULONG coordinated_mode_idx)
{
    if (!g_idle.enabled || !g_idle.wake_reason_enabled)
        return;

    /* without an independently owned QUP/PDC res, zero-filled hardware observations are
     * misleading: zero does not establish that the registers or wake bitmap were read. */

    InterlockedIncrement(&g_idle.wake_reason_events);
    pep_log_write(PEPLOG_EVT_IDLE, 0x1E, STATUS_NOT_SUPPORTED, coordinated_mode_idx,
                  pep_get_current_cpu_number(), 0, 0);
}

static BOOLEAN idle_match_pattern(_In_opt_ PCWSTR text, _In_z_ PCWSTR pattern)
{
    if (!text || !pattern)
        return FALSE;

    while (*pattern) {
        if (*pattern == L'*') {
            while (*pattern == L'*')
                pattern++;

            if (!*pattern)
                return TRUE;

            while (*text) {
                if (idle_match_pattern(text, pattern))
                    return TRUE;
                text++;
            }

            return FALSE;
        }

        if (!*text)
            return FALSE;

        if (upcase_w(*text) != upcase_w(*pattern))
            return FALSE;

        text++;
        pattern++;
    }

    return (*text == L'\0');
}

VOID idle_ensure_init(VOID)
{
    /* this is a two-phase publication gate, not a callback rundown lock. */

    if (init_gate_ready(&g_idle.initialized) || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;
    if (init_gate_begin(&g_idle.initialized) != 1)
        return;

    /* no verified Molokai RPMh/clock idle backend exists. Keep diagnostic collection opt-in
     * and never make it evidence of LPI readiness. */

    g_idle.enabled = registry_query_u_long(L"IdleFeatureEnabled", 0);
    g_idle.clock_flags = registry_query_u_long(L"IdleClockFlags", 0x10);
    /* registry values and physical addresses do not establish ownership. These MMIO diagnostics
     * require authorized PnP/peer providers. */

    g_idle.map_qup_registers = 0;
    g_idle.rpmh_enabled = registry_query_u_long(L"IdleRpmhEnabled", 0);
    g_idle.rpmh_assume_cmdq_empty = registry_query_u_long(L"IdleRpmhAssumeCmdqEmpty", 0);
    g_idle.wake_reason_enabled = registry_query_u_long(L"IdleWakeReasonEnabled", 0);
    g_idle.wake_pdc_bitmap_enabled = 0;
    g_idle.wake_pdc_status_phys = 0;
    g_idle.wake_pdc_total_interrupts = 0;
    g_idle.wake_pdc_word_cnt = 0;
    g_idle.wake_pdc_map_base = NULL;
    g_idle.wake_pdc_map_bytes = 0;
    g_idle.wake_pdc_status_map = NULL;
    g_idle.transition_debugger_enabled = registry_query_u_long(L"IdleTransitionDebuggerEnabled", 0);

    g_idle.hw_drips_test_timeout_seconds = registry_query_u_long(L"HWDripsTestTimeoutSeconds", 0);
    g_idle.hw_drips_bugcheck_on_failure = registry_query_u_long(L"HWDripsBugcheckOnFailure", 0);
    g_idle.hw_drips_timer = NULL;
    g_idle.hw_drips_active = 0;
    g_idle.hw_drips_start_residency100ns = 0;
    g_idle.hw_drips_start_transition_cnt = 0;
    g_idle.hw_drips_start_time100ns = 0;

    g_idle.in_rpmh = 0;
    g_idle.rpmh_enter_failures = 0;
    g_idle.wake_reason_events = 0;

    g_idle.sleep_cnt = 0;
    g_idle.restore_cnt = 0;

    if (g_idle.hw_drips_test_timeout_seconds && g.driver) {
        WDF_TIMER_CONFIG cfg;
        WDF_OBJECT_ATTRIBUTES attr;
        WDF_TIMER_CONFIG_INIT(&cfg, hw_drips_timer_expiry);
        cfg.TolerableDelay = 3000;
        WDF_OBJECT_ATTRIBUTES_INIT(&attr);
        attr.ExecutionLevel = WdfExecutionLevelDispatch;
        attr.SynchronizationScope = WdfSynchronizationScopeNone;
        attr.ParentObject = g.driver;
        if (!NT_SUCCESS(WdfTimerCreate(&cfg, &attr, &g_idle.hw_drips_timer)))
            g_idle.hw_drips_timer = NULL;
    }

    init_gate_publish(&g_idle.initialized);
}

static NTSTATUS idle_clock_sleep(_In_ ULONG mode, _In_ ULONG clock_flags)
{
    if (!g_idle.enabled || !clock_flags)
        return STATUS_SUCCESS;

    InterlockedIncrement(&g_idle.sleep_cnt);
    pep_log_write(PEPLOG_EVT_IDLE, 0x10, STATUS_SUCCESS, mode, clock_flags,
                  (ULONG)KeGetCurrentIrql(), 0);
    return STATUS_SUCCESS;
}

static NTSTATUS idle_clock_restore(_In_ ULONG mode, _In_ ULONG clock_flags)
{
    if (!g_idle.enabled || !clock_flags)
        return STATUS_SUCCESS;

    InterlockedIncrement(&g_idle.restore_cnt);
    pep_log_write(PEPLOG_EVT_IDLE, 0x11, STATUS_SUCCESS, mode, clock_flags,
                  (ULONG)KeGetCurrentIrql(), 0);
    return STATUS_SUCCESS;
}

static NTSTATUS idle_cx_pre_execute(_In_ ULONG mode_idx)
{
    idle_ensure_init();
    if (!init_gate_ready(&g_idle.initialized) || !g_idle.enabled ||
        !pep_is_molokai_idle_execution_ready())
        return STATUS_NOT_SUPPORTED;

    idle_mode_t* mode = NULL;
    if (init_gate_ready(&g_idle_acpi_init) && g_idle_acpi.coordinated_modes_array &&
        mode_idx < g_idle_acpi.coordinated_modes_array_size)
        mode = g_idle_acpi.coordinated_modes_array[mode_idx];

    if (mode && (mode->mode_flags & 0x20u))
        idle_transition_debugger(0, mode_idx);

    idle_enumerate_unmasked_irqs(3, mode_idx);
    idle_turn_off_suppressible_resources(mode_idx);

    NTSTATUS st = idle_clock_sleep(1, g_idle.clock_flags);
    if (NT_SUCCESS(st))
        st = idle_rpmh_sync_enter(mode_idx);

    pep_log_write(PEPLOG_EVT_IDLE, 0x20, st, mode_idx, mode ? mode->mode_flags : 0, 0, 0);
    return st;
}

static NTSTATUS idle_cx_complete(_In_ ULONG mode_idx)
{
    idle_ensure_init();
    if (!init_gate_ready(&g_idle.initialized) || !g_idle.enabled ||
        !pep_is_molokai_idle_execution_ready())
        return STATUS_NOT_SUPPORTED;

    idle_mode_t* mode = NULL;
    if (init_gate_ready(&g_idle_acpi_init) && g_idle_acpi.coordinated_modes_array &&
        mode_idx < g_idle_acpi.coordinated_modes_array_size)
        mode = g_idle_acpi.coordinated_modes_array[mode_idx];

    idle_rpmh_sync_exit(mode_idx);

    NTSTATUS st = idle_clock_restore(1, g_idle.clock_flags);

    idle_turn_on_suppressible_resources(mode_idx);
    idle_wake_reason_mon(mode_idx);

    if (mode && (mode->mode_flags & 0x20u))
        idle_transition_debugger(1, mode_idx);

    pep_log_write(PEPLOG_EVT_IDLE, 0x21, st, mode_idx, mode ? mode->mode_flags : 0, 0, 0);
    return st;
}

VOID pep_override_lpi_support(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);

    ULONG disable = g_molokai_disable_lpi_support;

    UCHAR v = (disable == 0 && pep_is_molokai_idle_execution_ready()) ? 1 : 0;

    if (data)
        *(UCHAR*)data = v;

    pep_log_write(PEPLOG_EVT_IDLE, 0x30, STATUS_SUCCESS, v, disable, (ULONG)g_idle_acpi_init, 0);
}

VOID pep_update_lpi_proc_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    idle_ensure_init();
    idle_acpi_ensure_init();
    if (!data || !g_idle_acpi.lpr_array)
        return;

    ULONG proc = get_cpu_num_from_pep_handle(pep_handle);
    if (proc >= g_idle_acpi.cpu_cnt)
        proc = 0;

    pep_ppm_lpi_proc_states_t* s = (pep_ppm_lpi_proc_states_t*)data;
    ULONG cnt = s->cnt;
    if (!cnt)
        return;
    if (cnt > 64)
        cnt = 64;

    idle_lpr_t* lpr = &g_idle_acpi.lpr_array[proc];

    if (lpr->mode_array) {
        for (ULONG i = 0; i < lpr->mode_cnt; i++)
            idle_free_unicode(&lpr->mode_array[i].mode_name);
        ExFreePool(lpr->mode_array);
        lpr->mode_array = NULL;
        lpr->mode_cnt = 0;
    }

    if (lpr->lpr_name.Buffer)
        idle_free_unicode(&lpr->lpr_name);

    (VOID) idle_extract_lpr_name(s->lpi_states[0].state_name, &lpr->lpr_name);
    lpr->lpr_id = proc;
    lpr->core_mask = proc;

    lpr->mode_array =
        (idle_mode_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(idle_mode_t) * cnt, POOL_TAG);
    if (!lpr->mode_array)
        return;
    RtlZeroMemory(lpr->mode_array, sizeof(idle_mode_t) * cnt);
    lpr->mode_cnt = cnt;
    lpr->is_avail = 1;

    for (ULONG i = 0; i < cnt; i++) {
        lpr->mode_array[i].lpr_id = proc;
        lpr->mode_array[i].mode_id = i;
        lpr->mode_array[i].coordinated_mode_id = (ULONG)-1;
        lpr->mode_array[i].mode_flags = s->lpi_states[i].arch_ctx_flags |
                                        (16u * (((s->lpi_states[i].flags & 1u) == 0u) ? 1u : 0u));
        lpr->mode_array[i].psci_adder =
            (ULONGLONG)(ULONG)s->lpi_states[i].entry_method.address.LowPart;
        lpr->mode_array[i].attr.latency = s->lpi_states[i].wakeup_latency;
        lpr->mode_array[i].attr.break_even = s->lpi_states[i].min_residency;
        (VOID) idle_extract_mode_name(s->lpi_states[i].state_name, &lpr->mode_array[i].mode_name);
    }

    if (g_kernel_info.UpdateProcessorIdleState && pep_handle) {
        typedef struct pep_proc_idle_state_update {
            ULONG version;
            ULONG latency;
            ULONG break_even_duration;
        } pep_proc_idle_state_update_t;

        typedef int(__fastcall * pfn_update_proc_idle_state_t)(
            PVOID kernel_handle, unsigned int mode_id, pep_proc_idle_state_update_t* update);

        pfn_update_proc_idle_state_t fn =
            (pfn_update_proc_idle_state_t)g_kernel_info.UpdateProcessorIdleState;

        for (ULONG i = 0; i < cnt; i++) {
            pep_proc_idle_state_update_t u;
            u.version = 1;
            u.latency = lpr->mode_array[i].attr.latency;
            u.break_even_duration = lpr->mode_array[i].attr.break_even;
            (VOID) fn(pep_handle, i, &u);
        }
    }

    pep_log_write(PEPLOG_EVT_IDLE, 0x31, STATUS_SUCCESS, cnt, proc, 0, 0);
}

VOID pep_update_lpi_coordinated_states(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    idle_ensure_init();
    idle_acpi_ensure_init();
    if (!data || !g_idle_acpi.lpr_array)
        return;

    pep_ppm_lpi_coordinated_states_t* s = (pep_ppm_lpi_coordinated_states_t*)data;
    ULONG cnt = s->cnt;
    if (!cnt)
        return;
    if (cnt > 256)
        cnt = 256;

    if (g_platform_mode_resid) {
        ExFreePool(g_platform_mode_resid);
        g_platform_mode_resid = NULL;
        g_platform_mode_resid_cnt = 0;
    }
    g_drips_coordinated_mode_idx = 0xFFFFFFFFu;

    if (g_idle_acpi.coordinated_modes_array) {
        for (ULONG i = 0; i < g_idle_acpi.coordinated_modes_array_size; i++) {
            idle_mode_t* m = g_idle_acpi.coordinated_modes_array[i];
            if (!m)
                continue;
            idle_free_unicode(&m->mode_name);
            if (m->dependencies) {
                for (ULONG j = 0; j < m->dependency_cnt; j++)
                    if (m->dependencies[j].options)
                        ExFreePool(m->dependencies[j].options);
                ExFreePool(m->dependencies);
            }
            ExFreePool(m);
        }
        ExFreePool(g_idle_acpi.coordinated_modes_array);
        g_idle_acpi.coordinated_modes_array = NULL;
        g_idle_acpi.coordinated_modes_array_size = 0;
    }

    for (ULONG i = g_idle_acpi.cpu_cnt; i < g_idle_acpi.lpr_array_size; i++) {
        if (g_idle_acpi.lpr_array[i].mode_array) {
            for (ULONG j = 0; j < g_idle_acpi.lpr_array[i].mode_cnt; j++)
                idle_free_unicode(&g_idle_acpi.lpr_array[i].mode_array[j].mode_name);
            ExFreePool(g_idle_acpi.lpr_array[i].mode_array);
        }
        idle_free_unicode(&g_idle_acpi.lpr_array[i].lpr_name);
        RtlZeroMemory(&g_idle_acpi.lpr_array[i], sizeof(idle_lpr_t));
    }

    g_idle_acpi.coordinated_modes_array =
        (idle_mode_t**)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(idle_mode_t*) * cnt, POOL_TAG);
    if (!g_idle_acpi.coordinated_modes_array)
        return;
    RtlZeroMemory(g_idle_acpi.coordinated_modes_array, sizeof(idle_mode_t*) * cnt);
    g_idle_acpi.coordinated_modes_array_size = cnt;

    g_platform_mode_resid = (pep_mode_residency_ctx_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(pep_mode_residency_ctx_t) * cnt, POOL_TAG);
    if (g_platform_mode_resid) {
        RtlZeroMemory(g_platform_mode_resid, sizeof(pep_mode_residency_ctx_t) * cnt);
        g_platform_mode_resid_cnt = cnt;
    }

    ULONG last_lpr = 0xFFFFFFFFu;
    ULONG per_lpr = 0;

    for (ULONG i = 0; i < cnt; i++) {
        idle_mode_t* m =
            (idle_mode_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(idle_mode_t), POOL_TAG);
        if (!m)
            break;
        RtlZeroMemory(m, sizeof(*m));

        UNICODE_STRING lpr_name;
        RtlZeroMemory(&lpr_name, sizeof(lpr_name));
        if (!NT_SUCCESS(idle_extract_lpr_name(s->lpi_states[i].state_name, &lpr_name)))
            idle_alloc_unicode_from_range(&lpr_name, L"PLATFORM", 8);

        ULONG lpr_id = idle_fill_lpr_array(&g_idle_acpi, &lpr_name);
        idle_free_unicode(&lpr_name);

        m->lpr_id = lpr_id;
        if (lpr_id == last_lpr)
            per_lpr++;
        else {
            last_lpr = lpr_id;
            per_lpr = 0;
        }
        m->mode_id = per_lpr;
        m->coordinated_mode_id = i;
        m->psci_adder = s->lpi_states[i].entry_method;
        m->mode_flags = s->lpi_states[i].arch_ctx_flags |
                        (16u * (((s->lpi_states[i].flags & 1u) == 0u) ? 1u : 0u));
        m->attr.latency = s->lpi_states[i].wakeup_latency;
        m->attr.break_even = s->lpi_states[i].min_residency;
        (VOID) idle_extract_mode_name(s->lpi_states[i].state_name, &m->mode_name);

        ULONG dep_cnt = s->lpi_states[i].dependency_cnt;
        if (dep_cnt > 32)
            dep_cnt = 32;
        m->dependency_cnt = dep_cnt;
        if (dep_cnt && s->lpi_states[i].dependencies) {
            m->dependencies = (idle_dependency_t*)ExAllocatePool2(
                POOL_FLAG_NON_PAGED, sizeof(idle_dependency_t) * dep_cnt, POOL_TAG);
            if (m->dependencies)
                RtlZeroMemory(m->dependencies, sizeof(idle_dependency_t) * dep_cnt);
            for (ULONG j = 0; j < dep_cnt && m->dependencies; j++) {
                ULONG opt_cnt = 0;
                __try {
                    opt_cnt = s->lpi_states[i].dependencies[j].option_cnt;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    opt_cnt = 0;
                }
                if (opt_cnt > 32)
                    opt_cnt = 32;
                m->dependencies[j].cnt = opt_cnt;
                if (s->lpi_states[i].dependencies[j].u.target_proc)
                    m->dependencies[j].lpr_id =
                        get_cpu_num_from_pep_handle(s->lpi_states[i].dependencies[j].u.target_proc);
                else
                    m->dependencies[j].lpr_id = 0xFFFFFFFFu;

                if (opt_cnt && s->lpi_states[i].dependencies[j].options) {
                    m->dependencies[j].options = (ULONG*)ExAllocatePool2(
                        POOL_FLAG_NON_PAGED, sizeof(ULONG) * opt_cnt, POOL_TAG);
                    if (m->dependencies[j].options) {
                        for (ULONG k = 0; k < opt_cnt; k++) {
                            ULONG v = 0;
                            __try {
                                v = s->lpi_states[i].dependencies[j].options[k];
                            } __except (EXCEPTION_EXECUTE_HANDLER) {
                                v = 0;
                            }
                            m->dependencies[j].options[k] = v;
                        }
                    }
                }
            }
        }

        g_idle_acpi.coordinated_modes_array[i] = m;

        if (g_drips_coordinated_mode_idx == 0xFFFFFFFFu) {
            __try {
                if (s->lpi_states[i].state_name &&
                    wcs_contains_insensitive(s->lpi_states[i].state_name, L"DRIPS"))
                    g_drips_coordinated_mode_idx = i;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }

    if (g_drips_coordinated_mode_idx == 0xFFFFFFFFu)
        g_drips_coordinated_mode_idx = 0;
    soc_subsys_set_drips_state_id(g_drips_coordinated_mode_idx);
    if (InterlockedCompareExchange(&g_debugger_present, 0, 0) && g_debugger_break_even_offset_us) {
        ULONG off = g_debugger_break_even_offset_us;
        ULONG affect_all = g_debugger_break_even_affects_all_states;
        for (ULONG i = 0; i < cnt; i++) {
            if (!affect_all && i != g_drips_coordinated_mode_idx)
                continue;
            idle_mode_t* m = g_idle_acpi.coordinated_modes_array[i];
            if (!m)
                continue;
            ULONG be = m->attr.break_even;
            if (be <= 0xFFFFFFFFu - off)
                m->attr.break_even = be + off;
            else
                m->attr.break_even = 0xFFFFFFFFu;
        }
        pep_log_write(PEPLOG_EVT_IDLE, 0x34, STATUS_SUCCESS, g_drips_coordinated_mode_idx, off,
                      affect_all, 0);
    }

    for (ULONG i = 0; i < cnt; i++) {
        idle_mode_t* m = g_idle_acpi.coordinated_modes_array[i];
        if (!m)
            continue;
        ULONG lpr_id = m->lpr_id;
        if (lpr_id >= g_idle_acpi.lpr_array_size)
            continue;
        idle_lpr_t* lpr = &g_idle_acpi.lpr_array[lpr_id];
        if (!lpr->mode_array) {
            ULONG need = m->mode_id + 1;
            if (need < 1)
                need = 1;
            lpr->mode_array = (idle_mode_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                                            sizeof(idle_mode_t) * need, POOL_TAG);
            if (!lpr->mode_array)
                continue;
            RtlZeroMemory(lpr->mode_array, sizeof(idle_mode_t) * need);
            lpr->mode_cnt = need;
        }
        if (m->mode_id < lpr->mode_cnt) {
            idle_mode_t* dst = &lpr->mode_array[m->mode_id];
            dst->lpr_id = lpr_id;
            dst->mode_id = m->mode_id;
            dst->coordinated_mode_id = i;
            dst->psci_adder = m->psci_adder;
            dst->mode_flags = m->mode_flags;
            dst->attr = m->attr;
            if (!dst->mode_name.Buffer && m->mode_name.Buffer)
                (VOID) idle_alloc_unicode_from_range(&dst->mode_name, m->mode_name.Buffer,
                                                     m->mode_name.Length / sizeof(WCHAR));
        }
    }

    if (g_kernel_info.UpdatePlatformIdleState) {
        typedef struct pep_platform_idle_state_update {
            ULONG version;
            ULONG latency;
            ULONG break_even_duration;
        } pep_platform_idle_state_update_t;

        typedef int(__fastcall * pfn_update_plat_idle_state_t)(
            PVOID kernel_handle, unsigned int coordinated_mode_id,
            pep_platform_idle_state_update_t* update);

        pfn_update_plat_idle_state_t fn =
            (pfn_update_plat_idle_state_t)g_kernel_info.UpdatePlatformIdleState;
        PVOID h =
            pep_handle ? pep_handle : pep_get_kernel_handle_from_cpu_num(pep_get_current_cpu_num());

        if (h) {
            for (ULONG i = 0; i < cnt; i++) {
                idle_mode_t* m = g_idle_acpi.coordinated_modes_array
                                     ? g_idle_acpi.coordinated_modes_array[i]
                                     : NULL;
                if (!m)
                    continue;

                pep_platform_idle_state_update_t u;
                u.version = 1;
                u.latency = m->attr.latency;
                u.break_even_duration = m->attr.break_even;
                (VOID) fn(h, i, &u);
            }
        }
    }

    pep_init_and_apply_registry_vetoes_if_needed();
    pep_apply_boot_veto_if_possible();
    if (g_vetoes_enumerated)
        pep_flush_pending_vetoes();

    pep_log_write(PEPLOG_EVT_IDLE, 0x32, STATUS_SUCCESS, cnt, 0, 0, 0);
}

VOID pep_idle_lpi_pre_execute(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    idle_ensure_init();
    if (!data)
        return;

    pep_lpi_list_t* l = (pep_lpi_list_t*)data;
    NTSTATUS st = STATUS_SUCCESS;

    ULONG cnt = l->cnt;
    if (cnt > 0x1000)
        cnt = 0;

    ULONG* idx = (ULONG*)(ULONG_PTR)l->indices;
    if (!idx || !cnt) {
        l->status = STATUS_SUCCESS;
        return;
    }

    ULONGLONG now = pep_now100ns();
    BOOLEAN drips_in_list = FALSE;

    if (g_drips_coordinated_mode_idx < g_idle_acpi.coordinated_modes_array_size) {
        for (ULONG i = 0; i < cnt; i++) {
            if (idx[i] == g_drips_coordinated_mode_idx) {
                drips_in_list = TRUE;
                break;
            }
        }
    }

    ULONG enforce_boot_veto = registry_query_u_long(L"EnforceBootVeto", 0);
    ULONG enforce_usb_veto = registry_query_u_long(L"EnforceXhciSubDxVeto", 0);
    ULONG required_usb_state = registry_query_u_long(L"XhciSubDxRequiredState", 1);

    if (drips_in_list && enforce_boot_veto && g_boot_veto_enabled && !g_boot_veto_cleared) {
        pep_apply_boot_veto_if_possible();
        soc_subsys_on_drips_blocked_start(now, 1);
        st = STATUS_DEVICE_BUSY;
    } else if (drips_in_list && enforce_usb_veto &&
               pep_any_usb_sub_dx_mismatch(required_usb_state)) {
        soc_subsys_on_drips_blocked_start(now, 2);
        st = STATUS_DEVICE_BUSY;
    }

    if (NT_SUCCESS(st)) {
        if (drips_in_list)
            soc_subsys_on_drips_blocked_end(now);

        pep_log_write(PEPLOG_EVT_IDLE, 0x33, STATUS_SUCCESS, cnt, (ULONG)drips_in_list, 0, 0);

        for (ULONG i = 0; i < cnt; i++) {
            ULONG mode_idx = idx[i];

            if (g_platform_mode_resid && mode_idx < g_platform_mode_resid_cnt) {
                pep_mode_residency_ctx_t* pr = &g_platform_mode_resid[mode_idx];
                if (InterlockedCompareExchange(&pr->in_state, 1, 0) == 0)
                    pr->enter_time100ns = now;
            }

            NTSTATUS s = idle_cx_pre_execute(mode_idx);
            if (!NT_SUCCESS(s))
                st = s;
        }
    }

    if (drips_in_list && NT_SUCCESS(st)) {
        if (InterlockedCompareExchange(&g_resid.in_drips, 1, 0) == 0) {
            g_resid.drips_enter_time100ns = now;
            soc_subsys_on_drips_entry(now);
            idle_start_hw_drips_test_timer();

            res_key_t k;
            k.res_id = 0x50495244u;
            k.sub_type = 0;
            res_value_t v;
            v.value0 = 1;
            v.value1 = (ULONG)g_resid.drips_transition_cnt;
            v.value2 = (ULONG)g_resid.drips_residency100ns;
            v.value3 = (ULONG)((ULONGLONG)g_resid.drips_residency100ns >> 32);
            (VOID) res_set(k, v);
        }
    }

    l->status = st;
}

VOID pep_idle_lpi_complete(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    idle_ensure_init();

    if (!data)
        return;

    ULONG cnt = *(ULONG*)data;
    ULONG_PTR ptr = 0;

    if (cnt > 0x1000) {
        pep_lpi_list_t* l = (pep_lpi_list_t*)data;
        cnt = l->cnt;
        ptr = l->indices;
    } else {
        ptr = *(ULONG_PTR*)((PUCHAR)data + 8);
    }

    ULONG* idx = (ULONG*)(ULONG_PTR)ptr;
    if (!idx || !cnt)
        return;

    ULONGLONG now = pep_now100ns();
    BOOLEAN drips_in_list = FALSE;

    if (g_drips_coordinated_mode_idx < g_idle_acpi.coordinated_modes_array_size) {
        for (ULONG i = 0; i < cnt; i++) {
            if (idx[i] == g_drips_coordinated_mode_idx) {
                drips_in_list = TRUE;
                break;
            }
        }
    }

    pep_log_write(PEPLOG_EVT_IDLE, 0x34, STATUS_SUCCESS, cnt, (ULONG)drips_in_list, 0, 0);

    for (ULONG i = 0; i < cnt; i++) {
        ULONG mode_idx = idx[i];
        (VOID) idle_cx_complete(mode_idx);

        if (g_platform_mode_resid && mode_idx < g_platform_mode_resid_cnt) {
            pep_mode_residency_ctx_t* pr = &g_platform_mode_resid[mode_idx];
            if (InterlockedCompareExchange(&pr->in_state, 0, 1) == 1) {
                ULONGLONG start = pr->enter_time100ns;
                if (now > start)
                    InterlockedAdd64(&pr->residency100ns, (LONG64)(now - start));
                InterlockedIncrement64(&pr->transition_cnt);
            }
        }
    }

    if (drips_in_list && InterlockedCompareExchange(&g_resid.in_drips, 0, 1) == 1) {
        idle_stop_hw_drips_test_timer();
        soc_subsys_on_drips_exit(now);
        ULONGLONG start = g_resid.drips_enter_time100ns;
        if (now > start)
            InterlockedAdd64(&g_resid.drips_residency100ns, (LONG64)(now - start));
        InterlockedIncrement64(&g_resid.drips_transition_cnt);

        res_key_t k;
        k.res_id = 0x50495244u;
        k.sub_type = 0;
        ULONGLONG resid = (ULONGLONG)g_resid.drips_residency100ns;
        res_value_t v;
        v.value0 = 0;
        v.value1 = (ULONG)g_resid.drips_transition_cnt;
        v.value2 = (ULONG)resid;
        v.value3 = (ULONG)(resid >> 32);
        (VOID) res_set(k, v);
    }
}

VOID pep_ppm_enter_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    ULONG state = *(ULONG*)data;

    if (state == 5) {
        pep_log_write(PEPLOG_EVT_IDLE, 0x35, STATUS_SUCCESS, state, 0, 0, 0);
    }
}

VOID pep_ppm_from_system_state(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle)
{
    UNREFERENCED_PARAMETER(pep_handle);
    if (!data)
        return;

    ULONG state = *(ULONG*)data;
    if (state == 5) {
        dcvs_reset_feedback_counters();
        pep_log_write(PEPLOG_EVT_IDLE, 0x36, STATUS_SUCCESS, state, 0, 0, 0);
    }
}
