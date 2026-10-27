#include "power_control.h"
#include "peplog.h"
#include "resource_cache.h"

typedef struct selextsource_in {
    CHAR power_domain[128];
    ULONG src_id;
    ULONG arg0;
    ULONG arg1;
    ULONG arg2;
    ULONG arg3;
    ULONG rail_voltage_level;
} selextsource_in_t;

typedef struct extsrc_entry {
    ULONG domain_key;
    ULONG src_id;
    ULONG arg0;
    ULONG arg1;
    ULONG arg2;
    ULONG arg3;
    ULONG rail_voltage_level;
} extsrc_entry_t;

static KSPIN_LOCK g_ext_src_lock;
static extsrc_entry_t g_ext_src[32];
static ULONG g_ext_src_cnt;
static volatile LONG g_ext_src_init;

static __forceinline UCHAR ascii_lower(_In_ UCHAR c)
{
    if (c >= 'A' && c <= 'Z')
        return (UCHAR)(c + 32);
    return c;
}

static ULONG fnv1a32_ansi(_In_reads_bytes_(len) const CHAR* s, _In_ size_t len)
{
    ULONG h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        UCHAR c = (UCHAR)s[i];
        if (c == 0)
            break;
        c = ascii_lower(c);
        h ^= (ULONG)c;
        h *= 16777619u;
    }
    return h;
}

static size_t strnlen_a(_In_reads_bytes_(max_val) const CHAR* s, _In_ size_t max_val)
{
    if (!s)
        return 0;
    for (size_t i = 0; i < max_val; i++) {
        if (s[i] == 0)
            return i;
    }
    return max_val;
}

static extsrc_entry_t* ext_src_find_locked(_In_ ULONG key)
{
    for (ULONG i = 0; i < g_ext_src_cnt; i++) {
        if (g_ext_src[i].domain_key == key)
            return &g_ext_src[i];
    }
    return NULL;
}

static VOID ext_src_record(_In_ ULONG domain_key, _In_ ULONG src_id, _In_ ULONG arg0,
                           _In_ ULONG arg1, _In_ ULONG arg2, _In_ ULONG arg3,
                           _In_ ULONG rail_voltage_level)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_ext_src_lock, &irql);

    extsrc_entry_t* e = ext_src_find_locked(domain_key);
    if (!e) {
        if (g_ext_src_cnt < RTL_NUMBER_OF(g_ext_src))
            e = &g_ext_src[g_ext_src_cnt++];
    }

    if (e) {
        e->domain_key = domain_key;
        e->src_id = src_id;
        e->arg0 = arg0;
        e->arg1 = arg1;
        e->arg2 = arg2;
        e->arg3 = arg3;
        e->rail_voltage_level = rail_voltage_level;
    }

    KeReleaseSpinLock(&g_ext_src_lock, irql);
}

NTSTATUS power_ctrl_init(VOID)
{
    LONG previous = InterlockedCompareExchange(&g_ext_src_init, 1, 0);
    if (previous == 2)
        return STATUS_SUCCESS;
    if (previous == 1) {
        while (InterlockedCompareExchange(&g_ext_src_init, 2, 2) != 2)
            YieldProcessor();
        return STATUS_SUCCESS;
    }
    KeInitializeSpinLock(&g_ext_src_lock);
    g_ext_src_cnt = 0;
    RtlZeroMemory(g_ext_src, sizeof(g_ext_src));
    InterlockedExchange(&g_ext_src_init, 2);
    return STATUS_SUCCESS;
}

VOID power_ctrl_deinit(VOID)
{
    g_ext_src_cnt = 0;
    RtlZeroMemory(g_ext_src, sizeof(g_ext_src));
    InterlockedExchange(&g_ext_src_init, 0);
}

NTSTATUS power_ctrl_write_simple_status(_In_ NTSTATUS status,
                                        _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                        _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (out_buf && out_bytes >= 12) {
        ULONG* out = (ULONG*)out_buf;
        out[0] = 3;
        out[1] = (ULONG)status;
        out[2] = 0;
        *written = 12;
        return status;
    }

    if (out_buf && out_bytes > 0) {
        *written = 12;
        return STATUS_BUFFER_TOO_SMALL;
    }

    return status;
}

NTSTATUS power_ctrl_handle_log_command(_In_opt_ const VOID* in_buf, _In_ size_t in_bytes,
                                       _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                       _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    /* preserve the 12-byte response ABI, but NEVER turn an invalid. */

    size_t tmp_written = 0;
    NTSTATUS status = pep_log_handle_command(in_buf, in_bytes, NULL, 0, &tmp_written);
    return power_ctrl_write_simple_status(status, out_buf, out_bytes, written);
}

NTSTATUS power_ctrl_handle_select_external_src(_In_ ULONG dev_id, _In_opt_ const VOID* in_buf,
                                               _In_ size_t in_bytes,
                                               _Out_writes_bytes_opt_(out_bytes) VOID* out_buf,
                                               _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!in_buf || in_bytes < sizeof(selextsource_in_t))
        return power_ctrl_write_simple_status(STATUS_INVALID_PARAMETER, out_buf, out_bytes,
                                              written);

    const selextsource_in_t* in = (const selextsource_in_t*)in_buf;
    size_t name_len = strnlen_a(in->power_domain, sizeof(in->power_domain));
    if (name_len == 0)
        return power_ctrl_write_simple_status(STATUS_INVALID_PARAMETER, out_buf, out_bytes,
                                              written);

    ULONG domain_key = fnv1a32_ansi(in->power_domain, name_len);

    ext_src_record(domain_key, in->src_id, in->arg0, in->arg1, in->arg2, in->arg3,
                   in->rail_voltage_level);

    res_key_t k;
    k.res_id = 0x54584553u;
    k.sub_type = domain_key;
    res_value_t v;
    v.value0 = in->src_id;
    v.value1 = in->arg0;
    v.value2 = in->arg1;
    v.value3 = in->rail_voltage_level;
    (VOID) res_set(k, v);

    res_key_t kx;
    kx.res_id = 0x32584553u;
    kx.sub_type = domain_key;
    res_value_t vx;
    vx.value0 = in->arg2;
    vx.value1 = in->arg3;
    vx.value2 = in->rail_voltage_level;
    vx.value3 = in->src_id;
    (VOID) res_set(kx, vx);

    res_key_t kd;
    kd.res_id = 0x54584553u;
    kd.sub_type = 0x80000000u | (dev_id & 0x7FFFFFFFu);
    res_value_t vd;
    vd.value0 = domain_key;
    vd.value1 = in->src_id;
    vd.value2 = in->rail_voltage_level;
    vd.value3 = in->arg3;
    (VOID) res_set(kd, vd);

    /* never tell the caller the hardware src has been selected. */

    pep_log_write(PEPLOG_EVT_POWER_CONTROL, 0x53455854u, STATUS_NOT_SUPPORTED, domain_key,
                  in->src_id, in->arg0, in->arg1);

    return power_ctrl_write_simple_status(STATUS_NOT_SUPPORTED, out_buf, out_bytes, written);
}
