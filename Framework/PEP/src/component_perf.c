#include "component_perf.h"
#include "dcvs.h"
#include "resource_cache.h"
#include "hw_backend.h"

typedef struct comp_perf_set_state {
    ULONG cnt;
    pep_perf_state_t* states;
    volatile ULONG current_idx;
} comp_perf_set_state_t;

typedef struct comp_perf_set_range {
    ULONGLONG minimum;
    ULONGLONG maximum;
    volatile LONG64 current_value;
} comp_perf_set_range_t;

typedef struct comp_perf_set {
    UNICODE_STRING name;
    ULONGLONG flags;
    pep_perf_state_unit_t unit;
    pep_perf_state_type_t type;
    res_key_t res_key;
    ULONG mirror_res;
    comp_perf_set_state_t discrete;
    comp_perf_set_range_t range;
} comp_perf_set_t;

typedef struct comp_perf_comp {
    ULONG set_cnt;
    comp_perf_set_t* sets;
} comp_perf_comp_t;

typedef struct comp_perf_dev {
    ULONG comp_cnt;
    comp_perf_comp_t* components;
} comp_perf_dev_t;

/* component transitions require an admitted hardware backend with verified completion. */

BOOLEAN comp_perf_hardware_backend_ready(VOID)
{
    /* fail closed unless the verified production backend is registered. */
    if (!hw_backend_ready())
        return FALSE;
    return TRUE;
}

static volatile LONG g_comp_perf_init;
static ULONG g_comp_perf_enabled;
static ULONG g_comp_perf_set_cnt;
static ULONG g_comp_perf_state_cnt_override;
static ULONG g_comp_perf_mirror_resources;

static __forceinline VOID comp_perf_init_globals_if_needed(VOID)
{
    if (InterlockedCompareExchange(&g_comp_perf_init, 1, 0) != 0)
        return;

    g_comp_perf_enabled = registry_query_u_long(L"EnableComponentPerf", 0);
    g_comp_perf_set_cnt = registry_query_u_long(L"ComponentPerfSetCount", 1);
    if (g_comp_perf_set_cnt == 0 || g_comp_perf_set_cnt > 8)
        g_comp_perf_set_cnt = 1;

    g_comp_perf_state_cnt_override = registry_query_u_long(L"ComponentPerfStateCount", 0);
    if (g_comp_perf_state_cnt_override > 256)
        g_comp_perf_state_cnt_override = 0;

    g_comp_perf_mirror_resources = registry_query_u_long(L"ComponentPerfMirrorResources", 1);
}

static __forceinline ULONG comp_perf_get_state_cnt(VOID)
{
    comp_perf_init_globals_if_needed();
    if (!g_comp_perf_enabled)
        return 0;

    if (g_comp_perf_state_cnt_override)
        return g_comp_perf_state_cnt_override;

    ULONG c = dcvs_get_perf_level_cnt(0);
    if (c == 0 || c > 256)
        c = 0;
    return c;
}

static __forceinline VOID comp_perf_free_set(_Inout_ comp_perf_set_t* s)
{
    if (!s)
        return;
    if (s->discrete.states)
        ExFreePoolWithTag(s->discrete.states, POOL_TAG);
    s->discrete.states = NULL;
    s->discrete.cnt = 0;
    if (s->name.Buffer)
        ExFreePoolWithTag(s->name.Buffer, POOL_TAG);
    s->name.Buffer = NULL;
    s->name.Length = 0;
    s->name.MaximumLength = 0;
    s->range.minimum = 0;
    s->range.maximum = 0;
    s->range.current_value = 0;
}

static __forceinline NTSTATUS comp_perf_alloc_set(_Inout_ comp_perf_set_t* s, _In_ ULONG comp,
                                                  _In_ ULONG set_idx, _In_ ULONG state_cnt)
{
    WCHAR sub[128];
    ULONG v = 0;
    ULONG state_cnt_local = state_cnt;

    RtlZeroMemory(s, sizeof(*s));
    s->flags = 0;
    s->unit = PEP_PERF_STATE_UNIT_FREQ;
    s->type = PEP_PERF_STATE_TYPE_DISCRETE;
    s->discrete.current_idx = 0;
    s->range.minimum = 0;
    s->range.maximum = 0;
    s->range.current_value = 0;

    (VOID) RtlStringCchPrintfW(sub, RTL_NUMBER_OF(sub), L"ComponentPerf\\Comp%u\\Set%u",
                               (unsigned int)comp, (unsigned int)set_idx);
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Flags", &v)))
        s->flags = (ULONGLONG)v;
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Unit", &v)) &&
        v <= (ULONG)PEP_PERF_STATE_UNIT_BANDWIDTH)
        s->unit = (pep_perf_state_unit_t)v;
    if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"Type", &v)) &&
        v <= (ULONG)PEP_PERF_STATE_TYPE_RANGE)
        s->type = (pep_perf_state_type_t)v;

    s->mirror_res = g_comp_perf_mirror_resources;
    s->res_key.res_id = 0xC0000000u | (((comp & 0xFFu) << 8) | (set_idx & 0xFFu));
    s->res_key.sub_type = 0;
    if (s->mirror_res) {
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"MirrorRes", &v)))
            s->mirror_res = v ? 1 : 0;
        v = 0;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"ResourceId", &v)))
            s->res_key.res_id = v;
        v = 0;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"ResourceSubType", &v)))
            s->res_key.sub_type = v;
        if (s->res_key.res_id == 0)
            s->mirror_res = 0;
    }

    WCHAR name[64];
    (VOID) RtlStringCchPrintfW(name, RTL_NUMBER_OF(name), L"COMP%u_SET%u", (unsigned int)comp,
                               (unsigned int)set_idx);
    size_t cch = 0;
    (VOID) RtlStringCchLengthW(name, RTL_NUMBER_OF(name), &cch);
    USHORT bytes = (USHORT)((cch + 1) * sizeof(WCHAR));
    PWSTR buf = (PWSTR)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(buf, name, bytes);
    s->name.Buffer = buf;
    s->name.Length = bytes - sizeof(WCHAR);
    s->name.MaximumLength = bytes;

    if (s->type == PEP_PERF_STATE_TYPE_RANGE) {
        ULONGLONG min_v = 0;
        ULONGLONG max_v = 0;
        if (state_cnt_local != 0) {
            min_v = ~0ull;
            for (ULONG i = 0; i < state_cnt_local; ++i) {
                ULONGLONG hz = (ULONGLONG)dcvs_get_perf_freq_hz_by_perf_state_idx(0, i);
                if (hz < min_v)
                    min_v = hz;
                if (hz > max_v)
                    max_v = hz;
            }
            if (min_v == ~0ull)
                min_v = 0;
        }
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"RangeMinimum", &v)))
            min_v = (ULONGLONG)v;
        if (NT_SUCCESS(registry_query_u_long_in_sub_key(sub, L"RangeMaximum", &v)))
            max_v = (ULONGLONG)v;
        if (max_v < min_v)
            max_v = min_v;
        s->range.minimum = min_v;
        s->range.maximum = max_v;
        s->range.current_value = (LONG64)min_v;
        s->discrete.cnt = 0;
        s->discrete.states = NULL;
        return STATUS_SUCCESS;
    }

    if (state_cnt_local == 0)
        return STATUS_SUCCESS;

    SIZE_T st_bytes = sizeof(pep_perf_state_t) * (SIZE_T)state_cnt_local;
    pep_perf_state_t* st =
        (pep_perf_state_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, st_bytes, POOL_TAG);
    if (!st) {
        comp_perf_free_set(s);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(st, st_bytes);
    for (ULONG i = 0; i < state_cnt_local; ++i) {
        ULONG hz = dcvs_get_perf_freq_hz_by_perf_state_idx(0, i);
        st[i].value = (ULONGLONG)hz;
        st[i].ctx = (PVOID)(ULONG_PTR)(((comp & 0xFFFFu) << 16) | (i & 0xFFFFu));
    }
    s->discrete.cnt = state_cnt_local;
    s->discrete.states = st;
    return STATUS_SUCCESS;
}

/* allocation failure consequently exposed an incomplete tree and reused the public
 * deinitializer for its rollback. */

static VOID comp_perf_free_dev(_Inout_opt_ comp_perf_dev_t* d)
{
    if (!d)
        return;
    if (d->components) {
        for (ULONG c = 0; c < d->comp_cnt; ++c) {
            comp_perf_comp_t* comp = &d->components[c];
            if (comp->sets) {
                for (ULONG s = 0; s < comp->set_cnt; ++s)
                    comp_perf_free_set(&comp->sets[s]);
                ExFreePoolWithTag(comp->sets, POOL_TAG);
            }
        }
        ExFreePoolWithTag(d->components, POOL_TAG);
    }
    ExFreePoolWithTag(d, POOL_TAG);
}

NTSTATUS comp_perf_init_for_dev(_Inout_ PVOID pep_dev_handle, _In_ ULONG comp_cnt)
{
    comp_perf_dev_t* d;
    ULONG state_cnt;
    ULONG set_cnt;
    NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

    comp_perf_init_globals_if_needed();
    if (!pep_dev_handle)
        return STATUS_INVALID_PARAMETER;
    if (!g_comp_perf_enabled)
        return STATUS_SUCCESS;
    if (!comp_perf_hardware_backend_ready())
        return STATUS_NOT_SUPPORTED;
    if (comp_cnt == 0 || comp_cnt > 128)
        return STATUS_INVALID_PARAMETER;
    /* reinitialization cannot silently replace an already owned context. */
    if (*((PVOID*)pep_dev_handle) != NULL)
        return STATUS_INVALID_DEVICE_STATE;

    d = (comp_perf_dev_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*d), POOL_TAG);
    if (!d)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(d, sizeof(*d));
    d->comp_cnt = comp_cnt;

    d->components = (comp_perf_comp_t*)ExAllocatePool2(
        POOL_FLAG_NON_PAGED, sizeof(comp_perf_comp_t) * (SIZE_T)comp_cnt, POOL_TAG);
    if (!d->components)
        goto rollback;
    RtlZeroMemory(d->components, sizeof(comp_perf_comp_t) * (SIZE_T)comp_cnt);

    state_cnt = comp_perf_get_state_cnt();
    set_cnt = g_comp_perf_set_cnt;
    for (ULONG c = 0; c < comp_cnt; ++c) {
        comp_perf_comp_t* comp = &d->components[c];
        comp->set_cnt = set_cnt;
        comp->sets = (comp_perf_set_t*)ExAllocatePool2(
            POOL_FLAG_NON_PAGED, sizeof(comp_perf_set_t) * (SIZE_T)set_cnt, POOL_TAG);
        if (!comp->sets) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            goto rollback;
        }
        RtlZeroMemory(comp->sets, sizeof(comp_perf_set_t) * (SIZE_T)set_cnt);
        for (ULONG s = 0; s < set_cnt; ++s) {
            status = comp_perf_alloc_set(&comp->sets[s], c, s, state_cnt);
            if (!NT_SUCCESS(status))
                goto rollback;
        }
    }

    *((PVOID*)pep_dev_handle) = d;
    return STATUS_SUCCESS;
rollback:
    comp_perf_free_dev(d);
    return status;
}

VOID comp_perf_deinit_for_dev(_Inout_ PVOID pep_dev_handle)
{
    comp_perf_dev_t* d;
    if (!pep_dev_handle)
        return;
    d = (comp_perf_dev_t*)(*((PVOID*)pep_dev_handle));
    if (!d)
        return;
    /* dev lifetime serialization is provided by the caller; ownership transfer to the local
     * pointer prevents repeated destruction. */

    *((PVOID*)pep_dev_handle) = NULL;
    comp_perf_free_dev(d);
}

static __forceinline comp_perf_dev_t* comp_perf_get_dev(_In_ PVOID pep_dev_handle)
{
    if (!pep_dev_handle)
        return NULL;
    return (comp_perf_dev_t*)(*((PVOID*)pep_dev_handle));
}

ULONG comp_perf_get_set_cnt(_In_ PVOID pep_dev_handle, _In_ ULONG comp)
{
    comp_perf_init_globals_if_needed();
    if (!g_comp_perf_enabled || !comp_perf_hardware_backend_ready())
        return 0;
    comp_perf_dev_t* d = comp_perf_get_dev(pep_dev_handle);
    if (!d || comp >= d->comp_cnt)
        return 0;
    return d->components[comp].set_cnt;
}

BOOLEAN comp_perf_query_set(_In_ PVOID pep_dev_handle, _In_ ULONG comp,
                            _Inout_ pep_query_comp_perf_set_t* q)
{
    if (!q)
        return FALSE;
    if (!comp_perf_hardware_backend_ready())
        return FALSE;
    comp_perf_dev_t* d = comp_perf_get_dev(pep_dev_handle);
    if (!d || comp >= d->comp_cnt)
        return FALSE;
    comp_perf_comp_t* c = &d->components[comp];
    if (q->set >= c->set_cnt)
        return FALSE;
    comp_perf_set_t* s = &c->sets[q->set];
    q->flags = s->flags;
    q->unit = s->unit;
    q->type = s->type;
    if (s->type == PEP_PERF_STATE_TYPE_DISCRETE)
        q->u.discrete.cnt = s->discrete.cnt;
    else {
        q->u.range.minimum = s->range.minimum;
        q->u.range.maximum = s->range.maximum;
    }
    return TRUE;
}

BOOLEAN comp_perf_query_set_name(_In_ PVOID pep_dev_handle,
                                 _Inout_ pep_query_comp_perf_set_name_t* q)
{
    if (!q)
        return FALSE;
    if (!comp_perf_hardware_backend_ready())
        return FALSE;
    comp_perf_dev_t* d = comp_perf_get_dev(pep_dev_handle);
    if (!d || q->comp >= d->comp_cnt)
        return FALSE;
    comp_perf_comp_t* c = &d->components[q->comp];
    if (q->set >= c->set_cnt)
        return FALSE;
    comp_perf_set_t* s = &c->sets[q->set];
    USHORT bytes = s->name.MaximumLength;
    if (!q->name) {
        q->name_size = bytes;
        return TRUE;
    }
    if (q->name_size < bytes) {
        q->name_size = bytes;
        return FALSE;
    }
    RtlCopyMemory(q->name, s->name.Buffer, bytes);
    q->name_size = bytes;
    return TRUE;
}

BOOLEAN comp_perf_query_states(_In_ PVOID pep_dev_handle, _Inout_ pep_query_comp_perf_states_t* q)
{
    if (!q)
        return FALSE;
    if (!comp_perf_hardware_backend_ready())
        return FALSE;
    comp_perf_dev_t* d = comp_perf_get_dev(pep_dev_handle);
    if (!d || q->comp >= d->comp_cnt)
        return FALSE;
    comp_perf_comp_t* c = &d->components[q->comp];
    if (q->set >= c->set_cnt)
        return FALSE;
    comp_perf_set_t* s = &c->sets[q->set];
    if (s->type != PEP_PERF_STATE_TYPE_DISCRETE)
        return FALSE;
    if (!q->states)
        return FALSE;
    SIZE_T bytes = sizeof(pep_perf_state_t) * (SIZE_T)s->discrete.cnt;
    RtlCopyMemory(q->states, s->discrete.states, bytes);
    return TRUE;
}

/* a PEP caller could then observe a mixed set table that never represented one successful
 * reg. */

#define COMP_PERF_MAX_REG_SETS 8u

BOOLEAN comp_perf_reg_states(_In_ PVOID pep_dev_handle, _Inout_ pep_reg_comp_perf_states_t* r)
{
    comp_perf_dev_t* d;
    comp_perf_comp_t* c;
    pep_perf_state_t* staged[COMP_PERF_MAX_REG_SETS] = {0};
    ULONGLONG min_values[COMP_PERF_MAX_REG_SETS] = {0};
    ULONGLONG max_values[COMP_PERF_MAX_REG_SETS] = {0};
    ULONG s;
    BOOLEAN accepted = FALSE;

    if (!r || !r->perf_state_info || !comp_perf_hardware_backend_ready())
        return FALSE;
    d = comp_perf_get_dev(pep_dev_handle);
    if (!d || r->comp >= d->comp_cnt)
        return FALSE;
    c = &d->components[r->comp];
    if (!c->sets || !c->set_cnt || c->set_cnt > COMP_PERF_MAX_REG_SETS ||
        r->perf_state_info->set_cnt != c->set_cnt)
        return FALSE;

    for (s = 0; s < c->set_cnt; ++s) {
        const pep_comp_perf_set_t* in = &r->perf_state_info->perf_state_sets[s];
        comp_perf_set_t* out = &c->sets[s];
        if (in->type != out->type)
            goto cleanup;
        if (in->type == PEP_PERF_STATE_TYPE_DISCRETE) {
            SIZE_T bytes;
            ULONG cnt = in->u.discrete.cnt;
            if (cnt == 0 || cnt > 256u || !in->u.discrete.states)
                goto cleanup;
            bytes = sizeof(pep_perf_state_t) * (SIZE_T)cnt;
            staged[s] = (pep_perf_state_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
            if (!staged[s])
                goto cleanup;
            RtlCopyMemory(staged[s], in->u.discrete.states, bytes);
        } else if (in->type == PEP_PERF_STATE_TYPE_RANGE) {
            if (in->u.range.maximum < in->u.range.minimum)
                goto cleanup;
            min_values[s] = in->u.range.minimum;
            max_values[s] = in->u.range.maximum;
        } else
            goto cleanup;
    }

    /* the PEP device reg/lifetime lock must cover readers. */

    for (s = 0; s < c->set_cnt; ++s) {
        comp_perf_set_t* out = &c->sets[s];
        if (out->type == PEP_PERF_STATE_TYPE_DISCRETE) {
            pep_perf_state_t* old = out->discrete.states;
            out->discrete.states = staged[s];
            out->discrete.cnt = r->perf_state_info->perf_state_sets[s].u.discrete.cnt;
            staged[s] = NULL;
            if (out->discrete.current_idx >= out->discrete.cnt)
                out->discrete.current_idx = 0;
            if (old)
                ExFreePoolWithTag(old, POOL_TAG);
        } else {
            ULONGLONG cur;
            out->range.minimum = min_values[s];
            out->range.maximum = max_values[s];
            cur = (ULONGLONG)InterlockedCompareExchange64(&out->range.current_value, 0, 0);
            if (cur < min_values[s])
                cur = min_values[s];
            if (cur > max_values[s])
                cur = max_values[s];
            InterlockedExchange64(&out->range.current_value, (LONG64)cur);
        }
    }
    accepted = TRUE;

cleanup:
    for (s = 0; s < COMP_PERF_MAX_REG_SETS; ++s)
        if (staged[s])
            ExFreePoolWithTag(staged[s], POOL_TAG);
    return accepted;
}

BOOLEAN comp_perf_req_state(_In_ PVOID pep_dev_handle, _Inout_ pep_req_comp_perf_state_t* r)
{
    UNREFERENCED_PARAMETER(pep_dev_handle);
    if (!r)
        return FALSE;

    /* no authenticated Windows Qualcomm F/P-state provider exists here. The old code updated
     * current_idx/current_value, mirrored an in-memory res and returned succeeded=1 WITHOUT
     * touching device hardware. */

    r->completed = 1;
    r->succeeded = 0;
    return TRUE;
}

BOOLEAN comp_perf_query_current_state(_In_ PVOID pep_dev_handle,
                                      _Inout_ pep_query_current_comp_perf_state_t* q)
{
    if (!q)
        return FALSE;
    if (!comp_perf_hardware_backend_ready())
        return FALSE;
    comp_perf_dev_t* d = comp_perf_get_dev(pep_dev_handle);
    if (!d || q->comp >= d->comp_cnt)
        return FALSE;
    comp_perf_comp_t* c = &d->components[q->comp];
    if (q->set >= c->set_cnt)
        return FALSE;
    comp_perf_set_t* s = &c->sets[q->set];
    if (s->type == PEP_PERF_STATE_TYPE_DISCRETE) {
        ULONG idx =
            (ULONG)InterlockedCompareExchange((volatile LONG*)&s->discrete.current_idx, 0, 0);
        if (idx >= s->discrete.cnt)
            idx = 0;
        q->u.state_idx = idx;
        return TRUE;
    }
    if (s->type == PEP_PERF_STATE_TYPE_RANGE) {
        ULONGLONG v = (ULONGLONG)InterlockedCompareExchange64(&s->range.current_value, 0, 0);
        if (v < s->range.minimum)
            v = s->range.minimum;
        if (v > s->range.maximum)
            v = s->range.maximum;
        q->u.state_value = v;
        return TRUE;
    }
    return FALSE;
}
