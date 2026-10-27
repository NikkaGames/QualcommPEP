#include "ppm_diag.h"

typedef struct pep_ppm_query_state_name {
    ULONG state_idx;
    USHORT name_size;
    PWSTR name;
} pep_ppm_query_state_name_t;

typedef struct pep_ppm_query_idle_states_v2 {
    ULONG cnt;
    UCHAR payload[1];
} pep_ppm_query_idle_states_v2_t;

typedef struct pep_ppm_query_coordinated_dependency {
    ULONG state_idx;
    ULONG dependency_idx;
    ULONG dependency_size;
    ULONG dependency_size_used;
    ULONGLONG u4;
    UCHAR options[1];
} pep_ppm_query_coordinated_dependency_t;

typedef struct pep_ppm_query_veto_reason {
    ULONG veto_reason;
    USHORT name_size;
    PWSTR name;
} pep_ppm_query_veto_reason_t;

static __forceinline PVOID ppm_get_pep_handle(_In_ ppmtest_idle_req_header_t* h)
{
    if (!h)
        return NULL;
    ULONGLONG v = 0;
    __try {
        v = *(ULONGLONG*)(((PUCHAR)h) + 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = 0;
    }
    return (PVOID)(ULONG_PTR)v;
}

static __forceinline ULONG ppm_get_veto_id(VOID)
{
    return pep_get_registry_test_veto_id();
}

static NTSTATUS ensure_min(_In_ SIZE_T sz, _In_ SIZE_T need)
{
    return (sz >= need) ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static NTSTATUS ppm_query_caps(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    if (!pep)
        return STATUS_INVALID_PARAMETER;

    ULONG idle_state_cnt = 0;
    (VOID) pep_get_proc_idle_state_cnt(pep, &idle_state_cnt);

    *(ULONG*)(((PUCHAR)h) + 0x14) = idle_state_cnt;

    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_idle_states_v2(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    if (!pep)
        return STATUS_INVALID_PARAMETER;

    pep_query_proc_idle_states(((PUCHAR)h) + 0x10, pep);
    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_platform_residencies(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    pep_query_platform_states_acpi_lpi_pep(((PUCHAR)h) + 0x10, pep);
    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_veto_reasons(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    pep_query_veto_reasons_compat(((PUCHAR)h) + 0x10, pep);

    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_veto_reason(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    pep_query_veto_reason_compat(((PUCHAR)h) + 0x10, pep);

    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_state_name(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz,
                                     _In_ BOOLEAN coordinated)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);

    if (coordinated)
        pep_query_coordinated_state_name(((PUCHAR)h) + 0x10, pep);
    else
        pep_query_proc_state_name(((PUCHAR)h) + 0x10, pep);

    return STATUS_SUCCESS;
}

static NTSTATUS ppm_query_coordinated_dependency(_Inout_ ppmtest_idle_req_header_t* h,
                                                 _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x38);
    if (!NT_SUCCESS(st))
        return st;

    PVOID pep = ppm_get_pep_handle(h);
    pep_query_coordinated_idle_dependency(((PUCHAR)h) + 0x10, pep);
    return STATUS_SUCCESS;
}

static NTSTATUS ppm_internal_veto(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz,
                                  _In_ ULONG type)
{
    NTSTATUS st = ensure_min(sz, 0x14);
    if (!NT_SUCCESS(st))
        return st;

    ULONG selector = h->u.dummy;
    ULONG mode_or_idx = 0;
    __try {
        mode_or_idx = *(ULONG*)(((PUCHAR)h) + sizeof(*h));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mode_or_idx = 0;
    }

    ULONG lpr_id = 0xFFFFFFFFu;
    ULONG mode_id = 0xFFFFFFFFu;
    st = pep_resolve_idle_veto_target(selector, mode_or_idx, &lpr_id, &mode_id);
    if (!NT_SUCCESS(st))
        return st;

    ULONG veto_id = ppm_get_veto_id();

    if (type == PPMTEST_INTERNAL_ADD_VETO) {
        st = pep_apply_veto(lpr_id, mode_id, veto_id);
    } else if (type == PPMTEST_INTERNAL_REMOVE_VETO) {
        st = pep_remove_veto(lpr_id, mode_id, veto_id);
    } else {
        st = STATUS_SUCCESS;
    }

    ULONG mode_ref = 0;
    (VOID) pep_get_mode_veto_ref_cnt(lpr_id, mode_id, &mode_ref);
    *(ULONG*)(((PUCHAR)h) + 0x10) = mode_ref;
    return st;
}

static NTSTATUS ppm_internal_get_cpu_handles(_Inout_ ppmtest_idle_req_header_t* h, _In_ SIZE_T sz)
{
    NTSTATUS st = ensure_min(sz, 0x20);
    if (!NT_SUCCESS(st))
        return st;

    ULONG cpu = h->u.dummy;
    PVOID pep_handle = NULL;
    PVOID kernel_handle = NULL;
    if (!pep_get_cpu_handles(cpu, &pep_handle, &kernel_handle))
        return STATUS_ARRAY_BOUNDS_EXCEEDED;

    ULONGLONG* o0 = (ULONGLONG*)(((PUCHAR)h) + 0x10);
    ULONGLONG* o1 = (ULONGLONG*)(((PUCHAR)h) + 0x18);
    *o0 = (ULONGLONG)(ULONG_PTR)pep_handle;
    *o1 = (ULONGLONG)(ULONG_PTR)kernel_handle;

    return STATUS_SUCCESS;
}

typedef NTSTATUS (*ppmtest_handler_fn_t)(_Inout_ ppmtest_idle_req_header_t* header,
                                         _In_ SIZE_T buf_size, _In_ ULONG req_type);

typedef struct ppmtest_handler_pair {
    ULONG type;
    ppmtest_handler_fn_t handler;
} ppmtest_handler_pair_t;

static NTSTATUS ppm_req_query_caps(_Inout_ ppmtest_idle_req_header_t* header, _In_ SIZE_T buf_size,
                                   _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_caps(header, buf_size);
}

static NTSTATUS ppm_req_query_idle_states_v2(_Inout_ ppmtest_idle_req_header_t* header,
                                             _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_idle_states_v2(header, buf_size);
}

static NTSTATUS ppm_req_query_platform_residencies(_Inout_ ppmtest_idle_req_header_t* header,
                                                   _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_platform_residencies(header, buf_size);
}

static NTSTATUS ppm_req_query_veto_reasons(_Inout_ ppmtest_idle_req_header_t* header,
                                           _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_veto_reasons(header, buf_size);
}

static NTSTATUS ppm_req_query_veto_reason(_Inout_ ppmtest_idle_req_header_t* header,
                                          _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_veto_reason(header, buf_size);
}

static NTSTATUS ppm_req_query_state_name(_Inout_ ppmtest_idle_req_header_t* header,
                                         _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_state_name(header, buf_size, FALSE);
}

static NTSTATUS ppm_req_query_coordinated_states(_Inout_ ppmtest_idle_req_header_t* header,
                                                 _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_state_name(header, buf_size, TRUE);
}

static NTSTATUS ppm_req_query_coordinated_dependency(_Inout_ ppmtest_idle_req_header_t* header,
                                                     _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_query_coordinated_dependency(header, buf_size);
}

static NTSTATUS ppm_req_internal_veto(_Inout_ ppmtest_idle_req_header_t* header,
                                      _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    return ppm_internal_veto(header, buf_size, req_type);
}

static NTSTATUS ppm_req_internal_get_cpu_handles(_Inout_ ppmtest_idle_req_header_t* header,
                                                 _In_ SIZE_T buf_size, _In_ ULONG req_type)
{
    UNREFERENCED_PARAMETER(req_type);
    return ppm_internal_get_cpu_handles(header, buf_size);
}

static const ppmtest_handler_pair_t g_ppm_req_handlers[] = {
    {PPMTEST_QUERY_CAPS, ppm_req_query_caps},
    {PPMTEST_QUERY_IDLE_STATES_V2, ppm_req_query_idle_states_v2},

    {PPMTEST_QUERY_PLATFORM_STATES, ppm_req_query_platform_residencies},
    {PPMTEST_QUERY_PLATFORM_STATE_RESIDENCIES, ppm_req_query_platform_residencies},
    {PPMTEST_QUERY_VETO_REASONS, ppm_req_query_veto_reasons},
    {PPMTEST_QUERY_VETO_REASON, ppm_req_query_veto_reason},
    {PPMTEST_QUERY_COORDINATED_STATE_NAME, ppm_req_query_state_name},
    {PPMTEST_QUERY_PROC_STATE_NAME, ppm_req_query_state_name},
    {PPMTEST_QUERY_COORDINATED_STATES, ppm_req_query_coordinated_states},
    {PPMTEST_QUERY_COORDINATED_DEPENDENCY, ppm_req_query_coordinated_dependency},
    {PPMTEST_INTERNAL_QUERY_MODE_VETO_REFCOUNT, ppm_req_internal_veto},
    {PPMTEST_INTERNAL_ADD_VETO, ppm_req_internal_veto},
    {PPMTEST_INTERNAL_REMOVE_VETO, ppm_req_internal_veto},
    {PPMTEST_INTERNAL_GET_CPU_HANDLES, ppm_req_internal_get_cpu_handles},
};

NTSTATUS ppm_test_handle(_Inout_updates_bytes_(buf_size) VOID* buf, _In_ SIZE_T buf_size)
{
    if (!buf)
        return STATUS_INVALID_PARAMETER;

    if (buf_size < 0x0C)
        return STATUS_BUFFER_TOO_SMALL;

    ppmtest_idle_req_header_t* h = (ppmtest_idle_req_header_t*)buf;

    if (h->magic_number != 0x1D1E7E57u)
        return STATUS_INVALID_PARAMETER;

    ULONG t = h->req_type;
    if (t > PPMTEST_INTERNAL_GET_CPU_HANDLES)
        return STATUS_INVALID_PARAMETER;

    const ppmtest_handler_pair_t* p = &g_ppm_req_handlers[t];
    if (p->type != t)
        return STATUS_INTERNAL_ERROR;
    if (!p->handler)
        return STATUS_NOT_SUPPORTED;
    return p->handler(h, buf_size, t);
}
