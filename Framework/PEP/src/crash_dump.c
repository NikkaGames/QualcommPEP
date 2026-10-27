#include "crash_dump.h"
#include "resource_cache.h"

#define CRASH_MAX (256 * 1024)
#define CRASH_SRC_MAX CRASH_SRC_CAPACITY
#define CRASH_IDX_AUTO 0xFFFFFFFFu
#define CRASH_FILE_ORPHAN ((PVOID)(ULONG_PTR) - 1)

static WDFSPINLOCK g_crash_lock;

typedef struct crash_src {
    BOOLEAN in_use;
    BOOLEAN guid_valid;
    ULONG idx;
    ULONG item_cnt;
    ULONG entry_size;
    GUID src_guid;
    UCHAR* blob;
    ULONG blob_len;
    ULONG blob_capacity;
    ULONG ring_read_idx;
    ULONG ring_write_idx;
    ULONG ring_items_present;
    ULONGLONG last_write100ns;
    PVOID create_file_object;
    PVOID open_file_object;
} crash_src_t;

static crash_src_t g_crash_sources[CRASH_SRC_MAX];
static ULONG g_active_src;
static volatile LONG g_crash_create_cnt;
static volatile LONG g_crash_open_cnt;
static volatile LONG g_crash_destroy_cnt;
static volatile LONG g_crash_write_cnt;
static volatile LONG g_crash_read_cnt;
static volatile LONG g_crash_capture_cnt;
static volatile LONG g_live_kernel_create_cnt;
static crash_event_t g_crash_history[CRASH_HISTORY_CAPACITY];
static ULONGLONG g_crash_event_sequence;
static ULONG g_crash_last_event_code;
static LONG g_crash_last_event_status;
static ULONG g_crash_last_event_src_idx;
static ULONG g_crash_last_event_size;
static ULONGLONG g_crash_last_event_tick100ns;

static VOID crash_publish_state(_In_ ULONG event_code, _In_ ULONG src_idx, _In_ NTSTATUS status,
                                _In_ ULONG size)
{
    ULONGLONG seq = ++g_crash_event_sequence;
    ULONGLONG idx = (seq - 1ull) % CRASH_HISTORY_CAPACITY;
    crash_event_t* e = &g_crash_history[idx];
    ULONGLONG tick = KeQueryInterruptTime();

    e->sequence = seq;
    e->tick100ns = tick;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->src_idx = src_idx;
    e->active_src = g_active_src;
    e->size = size;
    e->create_cnt = (ULONG)g_crash_create_cnt;
    e->open_cnt = (ULONG)g_crash_open_cnt;
    e->destroy_cnt = (ULONG)g_crash_destroy_cnt;
    e->write_cnt = (ULONG)g_crash_write_cnt;
    e->read_cnt = (ULONG)g_crash_read_cnt;
    e->capture_cnt = (ULONG)g_crash_capture_cnt;
    e->live_kernel_create_cnt = (ULONG)g_live_kernel_create_cnt;

    g_crash_last_event_code = event_code;
    g_crash_last_event_status = (LONG)status;
    g_crash_last_event_src_idx = src_idx;
    g_crash_last_event_size = size;
    g_crash_last_event_tick100ns = tick;

    res_key_t k;
    res_value_t v;

    k.res_id = 0x30444352u;
    k.sub_type = src_idx;
    v.value0 = event_code;
    v.value1 = (ULONG)status;
    v.value2 = size;
    v.value3 = g_active_src;
    (VOID) res_set(k, v);

    k.res_id = 0x31444352u;
    k.sub_type = src_idx;
    v.value0 = (ULONG)g_crash_create_cnt;
    v.value1 = (ULONG)g_crash_open_cnt;
    v.value2 = (ULONG)g_crash_write_cnt;
    v.value3 = (ULONG)g_crash_read_cnt;
    (VOID) res_set(k, v);

    k.res_id = 0x32444352u;
    k.sub_type = src_idx;
    v.value0 = (ULONG)g_crash_destroy_cnt;
    v.value1 = (ULONG)g_crash_capture_cnt;
    v.value2 = (ULONG)g_live_kernel_create_cnt;
    v.value3 = (ULONG)seq;
    (VOID) res_set(k, v);

    k.res_id = 0x33444352u;
    k.sub_type = event_code;
    v.value0 = (ULONG)status;
    v.value1 = src_idx;
    v.value2 = size;
    v.value3 = (ULONG)(tick & 0xFFFFFFFFu);
    (VOID) res_set(k, v);
}

#pragma pack(push, 1)

typedef struct lkd_create_in {
    ULONG bug_check_code;
    ULONGLONG bug_check_param;
    UCHAR exclude_dmf_data;
    ULONG number_of_client_structures;
    ULONGLONG array_of_client_structures;
    ULONG size_of_secondary_data;
    ULONGLONG secondary_data_buf;
} lkd_create_in_t;

#pragma pack(pop)

static VOID crash_free_src(_Inout_ crash_src_t* s)
{
    if (!s)
        return;
    if (s->blob) {
        ExFreePool(s->blob);
        s->blob = NULL;
    }
    s->blob_len = 0;
    s->blob_capacity = 0;
    s->ring_read_idx = 0;
    s->ring_write_idx = 0;
    s->ring_items_present = 0;
    s->last_write100ns = 0;
}

static VOID crash_reset_src(_Inout_ crash_src_t* s, _In_ ULONG idx)
{
    if (!s)
        return;
    crash_free_src(s);
    RtlZeroMemory(s, sizeof(*s));
    s->idx = idx;
}

static crash_src_t* crash_find_src(_In_ ULONG idx)
{
    if (idx >= CRASH_SRC_MAX)
        return NULL;
    if (!g_crash_sources[idx].in_use)
        return NULL;
    return &g_crash_sources[idx];
}

static BOOLEAN crash_guid_equal(_In_ const GUID* a, _In_ const GUID* b)
{
    if (!a || !b)
        return FALSE;
    return (RtlCompareMemory(a, b, sizeof(GUID)) == sizeof(GUID));
}

static BOOLEAN crash_guid_is_zero(_In_ const GUID* g)
{
    if (!g)
        return TRUE;
    static const GUID zero_guid = {0};
    return crash_guid_equal(g, &zero_guid);
}

static crash_src_t* crash_find_src_by_guid(_In_ const GUID* src_guid)
{
    if (!src_guid)
        return NULL;

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (s->in_use && s->guid_valid && crash_guid_equal(&s->src_guid, src_guid))
            return s;
    }

    return NULL;
}

static crash_src_t* crash_find_src_by_guid_from_idx(_In_ const GUID* src_guid, _In_ ULONG start_idx)
{
    if (!src_guid || start_idx >= CRASH_SRC_MAX)
        return NULL;

    for (ULONG i = start_idx; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (s->in_use && s->guid_valid && crash_guid_equal(&s->src_guid, src_guid))
            return s;
    }

    return NULL;
}

static crash_src_t* crash_find_src_for_file(_In_opt_ PVOID file_object)
{
    if (!file_object)
        return NULL;

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (!s->in_use)
            continue;
        if (s->create_file_object == file_object || s->open_file_object == file_object)
            return s;
    }

    return NULL;
}

static crash_src_t* crash_find_src_by_open_file(_In_opt_ PVOID file_object)
{
    if (!file_object)
        return NULL;

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (!s->in_use)
            continue;
        if (s->open_file_object == file_object)
            return s;
    }

    return NULL;
}

static crash_src_t* crash_find_src_by_create_file(_In_opt_ PVOID file_object)
{
    if (!file_object)
        return NULL;

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (!s->in_use)
            continue;
        if (s->create_file_object == file_object)
            return s;
    }

    return NULL;
}

static crash_src_t* crash_alloc_src(_In_ ULONG requested_idx)
{
    if (requested_idx != CRASH_IDX_AUTO) {
        if (requested_idx >= CRASH_SRC_MAX)
            return NULL;

        crash_src_t* s = &g_crash_sources[requested_idx];
        if (!s->in_use) {
            crash_reset_src(s, requested_idx);
            s->in_use = TRUE;
        }
        return s;
    }

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        if (!g_crash_sources[i].in_use) {
            crash_reset_src(&g_crash_sources[i], i);
            g_crash_sources[i].in_use = TRUE;
            return &g_crash_sources[i];
        }
    }

    return NULL;
}

static crash_src_t* crash_alloc_aux_src(_In_ ULONG requested_idx)
{
    /* auxiliary crashdump slots are file-object bound and start at idx 1. */

    if (requested_idx != CRASH_IDX_AUTO) {
        if (requested_idx == 0 || requested_idx >= CRASH_SRC_MAX)
            return NULL;

        crash_src_t* s = &g_crash_sources[requested_idx];
        if (!s->in_use) {
            crash_reset_src(s, requested_idx);
            s->in_use = TRUE;
        }
        return s;
    }

    for (ULONG i = 1; i < CRASH_SRC_MAX; i++) {
        if (!g_crash_sources[i].in_use) {
            crash_reset_src(&g_crash_sources[i], i);
            g_crash_sources[i].in_use = TRUE;
            return &g_crash_sources[i];
        }
    }

    return NULL;
}

static crash_src_t* crash_get_active(_In_ BOOLEAN create_default)
{
    crash_src_t* s = crash_find_src(g_active_src);
    if (s)
        return s;

    if (!create_default)
        return NULL;

    s = crash_alloc_src(0);
    if (s)
        g_active_src = s->idx;
    return s;
}

NTSTATUS crash_dump_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);

    NTSTATUS status = WdfSpinLockCreate(WDF_NO_OBJECT_ATTRIBUTES, &g_crash_lock);
    if (!NT_SUCCESS(status))
        return status;

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++)
        crash_reset_src(&g_crash_sources[i], i);

    g_active_src = 0;
    g_crash_create_cnt = 0;
    g_crash_open_cnt = 0;
    g_crash_destroy_cnt = 0;
    g_crash_write_cnt = 0;
    g_crash_read_cnt = 0;
    g_crash_capture_cnt = 0;
    g_live_kernel_create_cnt = 0;
    g_crash_event_sequence = 0;
    g_crash_last_event_code = 0;
    g_crash_last_event_status = STATUS_SUCCESS;
    g_crash_last_event_src_idx = 0;
    g_crash_last_event_size = 0;
    g_crash_last_event_tick100ns = 0;
    RtlZeroMemory(g_crash_history, sizeof(g_crash_history));

    return STATUS_SUCCESS;
}

VOID crash_dump_deinit(VOID)
{
    for (ULONG i = 0; i < CRASH_SRC_MAX; i++)
        crash_reset_src(&g_crash_sources[i], i);

    g_active_src = 0;
    g_crash_event_sequence = 0;
    g_crash_last_event_code = 0;
    g_crash_last_event_status = STATUS_SUCCESS;
    g_crash_last_event_src_idx = 0;
    g_crash_last_event_size = 0;
    g_crash_last_event_tick100ns = 0;
    RtlZeroMemory(g_crash_history, sizeof(g_crash_history));

    if (g_crash_lock) {
        WdfObjectDelete(g_crash_lock);
        g_crash_lock = NULL;
    }
}

VOID crash_dump_on_file_close(_In_opt_ PVOID file_object)
{
    if (!g_crash_lock || !file_object)
        return;

    WdfSpinLockAcquire(g_crash_lock);

    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_src_t* s = &g_crash_sources[i];
        if (!s->in_use)
            continue;

        if (s->open_file_object == file_object) {
            s->open_file_object = NULL;
            crash_publish_state(8u, s->idx, STATUS_SUCCESS, s->blob_len);
        } else if (s->create_file_object == file_object) {
            s->create_file_object = CRASH_FILE_ORPHAN;
            crash_publish_state(8u, s->idx, STATUS_SUCCESS, s->blob_len);
        }
    }

    WdfSpinLockRelease(g_crash_lock);
}

enum {
    IOCTL_QCPEP_CRASH_CREATE = 0x22A000,
    IOCTL_QCPEP_CRASH_DESTROY = 0x22A004,
    IOCTL_QCPEP_CRASH_WRITE = 0x22A008,
    IOCTL_QCPEP_CRASH_READ = 0x226010,
    IOCTL_QCPEP_CRASH_OPEN = 0x226014,
    IOCTL_QCPEP_CRASH_CAPTURE = 0x226018,
    IOCTL_QCPEP_LKD_CREATE = 0x22CB00
};

static NTSTATUS write_blob(_Inout_ crash_src_t* s, _In_ const VOID* in, _In_ size_t in_bytes)
{
    if (!s || !in)
        return STATUS_INVALID_PARAMETER;

    /* fixed-size ring entries reject partial writes. */

    if (s->entry_size != 0) {
        if (in_bytes > s->entry_size)
            return STATUS_BUFFER_OVERFLOW;
        if (in_bytes != s->entry_size)
            return STATUS_UNSUCCESSFUL;
    }

    if (s->entry_size != 0 && s->item_cnt != 0) {
        ULONGLONG cap64 = (ULONGLONG)s->entry_size * (ULONGLONG)s->item_cnt;
        if (cap64 == 0 || cap64 > CRASH_MAX)
            return STATUS_BUFFER_OVERFLOW;

        ULONG cap = (ULONG)cap64;
        if (!s->blob || s->blob_capacity != cap) {
            UCHAR* ring = (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, cap, POOL_TAG);
            if (!ring)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(ring, cap);
            crash_free_src(s);
            s->blob = ring;
            s->blob_capacity = cap;
        }

        if (s->ring_write_idx >= s->item_cnt)
            s->ring_write_idx = 0;
        if (s->ring_read_idx >= s->item_cnt)
            s->ring_read_idx = 0;

        ULONG slot = s->ring_write_idx;
        RtlCopyMemory(s->blob + ((size_t)slot * s->entry_size), in, in_bytes);

        if (s->ring_items_present < s->item_cnt) {
            s->ring_items_present++;
        } else {
            s->ring_read_idx = (s->ring_read_idx + 1) % s->item_cnt;
        }

        s->ring_write_idx = (s->ring_write_idx + 1) % s->item_cnt;
        s->blob_len = s->ring_items_present * s->entry_size;
        s->last_write100ns = KeQueryInterruptTime();
        return STATUS_SUCCESS;
    }

    size_t n = in_bytes;
    if (n > CRASH_MAX)
        return STATUS_BUFFER_OVERFLOW;

    UCHAR* buf = (UCHAR*)ExAllocatePool2(POOL_FLAG_NON_PAGED, n, POOL_TAG);
    if (!buf)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(buf, in, n);

    crash_free_src(s);
    s->blob = buf;
    s->blob_capacity = (ULONG)n;
    s->blob_len = (ULONG)n;
    s->last_write100ns = KeQueryInterruptTime();
    return STATUS_SUCCESS;
}

static NTSTATUS read_blob(_In_ const crash_src_t* s, _Out_opt_ VOID* out, _In_ size_t out_bytes,
                          _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!s || !s->in_use)
        return STATUS_NOT_FOUND;

    if (s->entry_size != 0 && s->item_cnt != 0) {
        crash_src_t* rw = (crash_src_t*)s;
        if (!rw->blob || rw->ring_items_present == 0)
            return STATUS_UNSUCCESSFUL;

        size_t need = rw->entry_size;
        if (!out || out_bytes == 0) {
            *written = need;
            return STATUS_BUFFER_TOO_SMALL;
        }

        ULONG slot = rw->ring_read_idx;
        if (slot >= rw->item_cnt)
            slot = 0;
        const UCHAR* src = rw->blob + ((size_t)slot * rw->entry_size);

        if (out_bytes < need) {
            RtlCopyMemory(out, src, out_bytes);
            *written = need;
            return STATUS_BUFFER_TOO_SMALL;
        }

        RtlCopyMemory(out, src, need);
        rw->ring_read_idx = (slot + 1) % rw->item_cnt;
        rw->ring_items_present--;
        rw->blob_len = rw->ring_items_present * rw->entry_size;
        *written = need;
        return STATUS_SUCCESS;
    }

    if (!s->blob || s->blob_len == 0)
        return STATUS_UNSUCCESSFUL;

    if (!out || out_bytes == 0) {
        *written = s->blob_len;
        return STATUS_BUFFER_TOO_SMALL;
    }

    size_t need = s->blob_len;
    if (out_bytes < need) {
        if (s->blob && out_bytes)
            RtlCopyMemory(out, s->blob, out_bytes);
        *written = need;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(out, s->blob, s->blob_len);

    *written = s->blob_len;
    return STATUS_SUCCESS;
}

static ULONG crash_parse_idx(_In_opt_ const VOID* in, _In_ size_t in_bytes, _In_ ULONG default_idx)
{
    if (!in || in_bytes < sizeof(ULONG))
        return default_idx;
    return *(const ULONG*)in;
}

static VOID crash_parse_create_meta(_In_opt_ const VOID* in, _In_ size_t in_bytes,
                                    _Out_ PULONG item_cnt, _Out_ PULONG entry_size,
                                    _Out_opt_ GUID* src_guid, _Out_ PBOOLEAN guid_valid)
{
    *item_cnt = 0;
    *entry_size = 0;
    *guid_valid = FALSE;

    if (!in)
        return;

    const UCHAR* p = (const UCHAR*)in;
    if (in_bytes >= 8)
        *item_cnt = *(const ULONG*)(p + 4);
    if (in_bytes >= 12)
        *entry_size = *(const ULONG*)(p + 8);
    if (src_guid && in_bytes >= 28) {
        RtlCopyMemory(src_guid, p + 12, sizeof(GUID));
        *guid_valid = TRUE;
    }
}

static BOOLEAN crash_parse_create_meta_v1(_In_opt_ const VOID* in, _In_ size_t in_bytes,
                                          _Out_ PULONG item_cnt, _Out_ PULONG entry_size,
                                          _Out_ GUID* src_guid)
{
    if (!in || !item_cnt || !entry_size || !src_guid || in_bytes < 24)
        return FALSE;

    const UCHAR* p = (const UCHAR*)in;
    *item_cnt = *(const ULONG*)(p + 0);
    *entry_size = *(const ULONG*)(p + 4);
    RtlCopyMemory(src_guid, p + 8, sizeof(GUID));
    return TRUE;
}

NTSTATUS crash_dump_handle_ioctl(_In_ ULONG ioctl_code, _In_opt_ PVOID file_object,
                                 _In_opt_ const VOID* in, _In_ size_t in_bytes, _Out_opt_ VOID* out,
                                 _In_ size_t out_bytes, _Out_ size_t* written)
{
    if (!written)
        return STATUS_INVALID_PARAMETER;
    *written = 0;

    if (!g_crash_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_crash_lock);

    NTSTATUS status = STATUS_SUCCESS;
    crash_src_t* s = NULL;
    ULONG src_idx = 0;

    switch (ioctl_code) {
        case IOCTL_QCPEP_CRASH_CREATE: {
            if (!file_object) {
                status = STATUS_INVALID_ADDRESS;
                break;
            }

            BOOLEAN reuse_existing_src = FALSE;
            ULONG item_cnt = 0, entry_size = 0;
            GUID g;
            BOOLEAN parsed_v1 =
                crash_parse_create_meta_v1(in, in_bytes, &item_cnt, &entry_size, &g);
            BOOLEAN guid_valid = FALSE;

            if (!parsed_v1) {
                if (!in) {
                    status = STATUS_INVALID_PARAMETER;
                    break;
                }

                if (in_bytes < 24) {
                    status = STATUS_BUFFER_TOO_SMALL;
                    break;
                }

                crash_parse_create_meta(in, in_bytes, &item_cnt, &entry_size, &g, &guid_valid);
            } else {
                guid_valid = TRUE;
            }

            if (guid_valid && crash_guid_is_zero(&g))
                guid_valid = FALSE;

            if (item_cnt == 0 || entry_size == 0) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            {
                ULONGLONG cap64 = (ULONGLONG)item_cnt * (ULONGLONG)entry_size;
                if (cap64 == 0 || cap64 > CRASH_MAX) {
                    status = STATUS_BUFFER_OVERFLOW;
                    break;
                }
            }

            s = guid_valid ? crash_find_src_by_guid_from_idx(&g, 1) : NULL;
            if (s && file_object) {
                if (s->create_file_object != NULL && s->create_file_object != CRASH_FILE_ORPHAN) {
                    s = NULL;
                } else {
                    reuse_existing_src = TRUE;
                }
            }
            if (!s) {

                if (file_object && crash_find_src_by_create_file(file_object)) {
                    status = STATUS_NO_MORE_FILES;
                    break;
                }
                src_idx = crash_parse_idx(in, in_bytes, CRASH_IDX_AUTO);
                s = parsed_v1 ? crash_alloc_aux_src(CRASH_IDX_AUTO) : crash_alloc_src(src_idx);
                reuse_existing_src = FALSE;
            }
            if (!s) {
                status = parsed_v1 ? STATUS_NO_MORE_FILES : STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            if (!reuse_existing_src) {
                s->item_cnt = item_cnt;
                s->entry_size = entry_size;
                s->guid_valid = guid_valid;
                if (guid_valid)
                    s->src_guid = g;
                s->open_file_object = NULL;
            }
            s->create_file_object = file_object;

            g_active_src = s->idx;
            InterlockedIncrement(&g_crash_create_cnt);
            UNREFERENCED_PARAMETER(out);
            UNREFERENCED_PARAMETER(out_bytes);
            *written = in_bytes;

            crash_publish_state(1u, s->idx, status, s->blob_len);
            break;
        }

        case IOCTL_QCPEP_CRASH_OPEN:
            if (!file_object) {
                status = STATUS_INVALID_ADDRESS;
                break;
            }

            if (!in) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (in_bytes < 24) {
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (!out || out_bytes < 24) {
                status = out ? STATUS_BUFFER_TOO_SMALL : STATUS_INVALID_PARAMETER;
                break;
            }

            const GUID* req_guid = (const GUID*)((const UCHAR*)in + 8);
            if (crash_guid_is_zero(req_guid)) {
                status = STATUS_UNSUCCESSFUL;
                break;
            }

            s = crash_find_src_by_guid_from_idx(req_guid, 1);
            if (!s) {
                status = STATUS_UNSUCCESSFUL;
            } else {
                g_active_src = s->idx;
                s->open_file_object = file_object;
                InterlockedIncrement(&g_crash_open_cnt);

                ULONG* out32 = (ULONG*)out;
                out32[0] = s->item_cnt;
                out32[1] = s->entry_size;
                RtlCopyMemory(((UCHAR*)out) + 8, ((const UCHAR*)in) + 8, sizeof(GUID));
                *written = 24;

                crash_publish_state(2u, s->idx, STATUS_SUCCESS, s->blob_len);
            }
            break;

        case IOCTL_QCPEP_CRASH_DESTROY:
            s = NULL;
            if (!file_object && (!in || in_bytes < sizeof(ULONG))) {
                status = STATUS_INVALID_ADDRESS;
                break;
            }
            if (file_object) {
                s = crash_find_src_for_file(file_object);
            } else if (in && in_bytes >= sizeof(ULONG)) {

                ULONG idx = crash_parse_idx(in, in_bytes, CRASH_IDX_AUTO);
                if (idx != CRASH_IDX_AUTO)
                    s = crash_find_src(idx);

                if (!s && in_bytes >= 24) {
                    const GUID* g = (const GUID*)((const UCHAR*)in + 8);
                    if (!crash_guid_is_zero(g))
                        s = crash_find_src_by_guid(g);
                }
            }

            if (s) {
                ULONG destroyed = s->idx;
                crash_reset_src(s, destroyed);
                InterlockedIncrement(&g_crash_destroy_cnt);

                if (g_active_src == destroyed) {
                    g_active_src = 0;
                    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
                        if (g_crash_sources[i].in_use) {
                            g_active_src = i;
                            break;
                        }
                    }
                }

                crash_publish_state(3u, destroyed, status, 0);
            }
            if (!s && file_object) {
                /* an unmatched per-file destroy is idempotent. */

                status = STATUS_SUCCESS;
                crash_publish_state(3u, g_active_src, status, 0);
            } else
                status = STATUS_SUCCESS;
            break;

        case IOCTL_QCPEP_CRASH_CAPTURE:
            s = crash_find_src_by_open_file(file_object);
            if (!s && !file_object)
                s = crash_get_active(FALSE);
            if (!out) {
                status = STATUS_INVALID_PARAMETER;
                *written = 0;
                crash_publish_state(6u, s ? s->idx : g_active_src, status, s ? s->blob_len : 0);
                break;
            }
            if (!s && file_object) {
                status = STATUS_INVALID_HANDLE;
                *written = 0;
                crash_publish_state(6u, g_active_src, status, 0);
                break;
            }
            if (s && out) {
                size_t required_capture = 0;
                if (s->entry_size != 0 && s->item_cnt != 0) {
                    ULONGLONG total = (ULONGLONG)s->entry_size * (ULONGLONG)s->item_cnt;
                    if (total > (ULONGLONG)((size_t)-1)) {
                        status = STATUS_INTEGER_OVERFLOW;
                        *written = 0;
                        crash_publish_state(6u, s->idx, status, s->blob_len);
                        break;
                    }
                    required_capture = (size_t)total;
                } else if (s->blob_len != 0) {
                    required_capture = s->blob_len;
                }

                if (required_capture != 0 && out_bytes < required_capture) {
                    status = STATUS_BUFFER_OVERFLOW;
                    *written = 0;
                    crash_publish_state(6u, s->idx, status, s->blob_len);
                    break;
                }
            }

            if (s && s->entry_size != 0 && s->item_cnt != 0 && s->ring_items_present != 0) {
                ULONG to_capture = s->ring_items_present;
                size_t captured = 0;
                for (ULONG i = 0; i < to_capture; i++) {
                    ULONG slot = s->ring_read_idx;
                    if (slot >= s->item_cnt)
                        slot = 0;

                    RtlCopyMemory(((UCHAR*)out) + captured,
                                  s->blob + ((size_t)slot * s->entry_size), s->entry_size);
                    captured += s->entry_size;
                    s->ring_read_idx = (slot + 1) % s->item_cnt;
                    s->ring_items_present--;
                }
                s->blob_len = s->ring_items_present * s->entry_size;
                status = STATUS_SUCCESS;
                *written = captured;
            } else if (s && (!s->blob || s->blob_len == 0)) {
                status = STATUS_SUCCESS;
                *written = 0;
            } else {
                size_t captured = 0;
                status = read_blob(s, out, out_bytes, &captured);
                if (NT_SUCCESS(status))
                    *written = captured;
                else
                    *written = 0;
            }
            if (NT_SUCCESS(status))
                InterlockedIncrement(&g_crash_capture_cnt);
            crash_publish_state(6u, s ? s->idx : g_active_src, status, s ? s->blob_len : 0);
            break;

        case IOCTL_QCPEP_CRASH_WRITE:
            if (!file_object) {
                status = STATUS_INVALID_PARAMETER;
                *written = 0;
                crash_publish_state(4u, g_active_src, status, 0);
                break;
            }

            s = crash_find_src_by_create_file(file_object);
            if (!s && file_object) {
                status = STATUS_INVALID_HANDLE;
                *written = in_bytes;
                crash_publish_state(4u, g_active_src, status, 0);
                break;
            }
            if (!s) {
                status = STATUS_INVALID_HANDLE;
                crash_publish_state(4u, g_active_src, status, 0);
                break;
            }

            *written = in_bytes;
            status = write_blob(s, in, in_bytes);
            if (NT_SUCCESS(status)) {
                InterlockedIncrement(&g_crash_write_cnt);
                if (s)
                    g_active_src = s->idx;
            }
            crash_publish_state(4u, s ? s->idx : g_active_src, status, s ? s->blob_len : 0);
            break;

        case IOCTL_QCPEP_CRASH_READ:
            s = crash_find_src_by_open_file(file_object);
            if (!s && !file_object)
                s = crash_get_active(FALSE);
            if (!out) {
                status = STATUS_INVALID_PARAMETER;
                *written = 0;
                crash_publish_state(5u, s ? s->idx : g_active_src, status, s ? s->blob_len : 0);
                break;
            }
            if (!s && file_object) {
                status = STATUS_INVALID_HANDLE;
                if (out)
                    *written = out_bytes;
                else
                    *written = 0;
                crash_publish_state(5u, g_active_src, status, 0);
                break;
            }
            if (s && out && s->entry_size != 0 && out_bytes < s->entry_size) {
                status = STATUS_BUFFER_OVERFLOW;
                *written = out_bytes;
                crash_publish_state(5u, s->idx, status, s->blob_len);
                break;
            }
            status = read_blob(s, out, out_bytes, written);
            if (out)
                *written = out_bytes;
            if (NT_SUCCESS(status)) {
                InterlockedIncrement(&g_crash_read_cnt);
                if (s)
                    g_active_src = s->idx;
            }
            crash_publish_state(5u, s ? s->idx : g_active_src, status, s ? s->blob_len : 0);
            break;

        case IOCTL_QCPEP_LKD_CREATE:

            if (!in || in_bytes < sizeof(lkd_create_in_t)) {
                *written = 0;
                status = STATUS_BUFFER_TOO_SMALL;
            } else {

                const lkd_create_in_t* req = (const lkd_create_in_t*)in;

                if ((req->number_of_client_structures != 0 &&
                     req->array_of_client_structures == 0) ||
                    (req->size_of_secondary_data != 0 && req->secondary_data_buf == 0)) {
                    status = STATUS_INVALID_PARAMETER;
                } else {
                    status = STATUS_SUCCESS;
                    InterlockedIncrement(&g_live_kernel_create_cnt);
                }

                res_key_t k;
                res_value_t v;

                k.res_id = 0x30444B4Cu;
                k.sub_type = req->exclude_dmf_data ? 1u : 0u;
                v.value0 = req->bug_check_code;
                v.value1 = (ULONG)req->bug_check_param;
                v.value2 = (ULONG)(req->bug_check_param >> 32);
                v.value3 = req->number_of_client_structures;
                (VOID) res_set(k, v);

                k.res_id = 0x31444B4Cu;
                k.sub_type = 0;
                v.value0 = req->size_of_secondary_data;
                v.value1 = (ULONG)status;
                v.value2 = (ULONG)req->array_of_client_structures;
                v.value3 = (ULONG)g_live_kernel_create_cnt;
                (VOID) res_set(k, v);

                *written = 0;
            }
            crash_publish_state(7u, g_active_src, status, (ULONG)in_bytes);
            break;

        default:
            status = STATUS_NOT_SUPPORTED;
            crash_publish_state(0xFFu, g_active_src, status, 0);
            break;
    }

    WdfSpinLockRelease(g_crash_lock);
    return status;
}

static VOID crash_fill_src_info(_In_ const crash_src_t* src, _Out_ crash_src_info_t* info)
{
    if (!info)
        return;

    RtlZeroMemory(info, sizeof(*info));
    if (!src)
        return;

    info->idx = src->idx;
    info->in_use = src->in_use ? 1u : 0u;
    info->guid_valid = src->guid_valid ? 1u : 0u;
    if (src->guid_valid)
        info->src_guid = src->src_guid;
    info->item_cnt = src->item_cnt;
    info->entry_size = src->entry_size;
    info->blob_len = src->blob_len;
    info->blob_capacity = src->blob_capacity;
    info->ring_read_idx = src->ring_read_idx;
    info->ring_write_idx = src->ring_write_idx;
    info->ring_items_present = src->ring_items_present;
    info->last_write100ns = src->last_write100ns;
    info->has_create_file_object = (src->create_file_object != NULL) ? 1u : 0u;
    info->has_open_file_object = (src->open_file_object != NULL) ? 1u : 0u;
    info->create_file_object_orphaned = (src->create_file_object == CRASH_FILE_ORPHAN) ? 1u : 0u;
}

VOID crash_dump_get_snapshot(_Out_ crash_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 1;
    snapshot->src_capacity = CRASH_SRC_MAX;

    if (!g_crash_lock)
        return;

    WdfSpinLockAcquire(g_crash_lock);

    snapshot->active_src = g_active_src;
    snapshot->create_cnt = (ULONG)g_crash_create_cnt;
    snapshot->open_cnt = (ULONG)g_crash_open_cnt;
    snapshot->destroy_cnt = (ULONG)g_crash_destroy_cnt;
    snapshot->write_cnt = (ULONG)g_crash_write_cnt;
    snapshot->read_cnt = (ULONG)g_crash_read_cnt;
    snapshot->capture_cnt = (ULONG)g_crash_capture_cnt;
    snapshot->live_kernel_create_cnt = (ULONG)g_live_kernel_create_cnt;
    snapshot->event_sequence = g_crash_event_sequence;
    snapshot->history_cnt = (g_crash_event_sequence < CRASH_HISTORY_CAPACITY)
                                ? (ULONG)g_crash_event_sequence
                                : CRASH_HISTORY_CAPACITY;
    snapshot->history_dropped = (g_crash_event_sequence > CRASH_HISTORY_CAPACITY)
                                    ? (ULONG)(g_crash_event_sequence - CRASH_HISTORY_CAPACITY)
                                    : 0u;
    snapshot->last_event_code = g_crash_last_event_code;
    snapshot->last_event_status = g_crash_last_event_status;
    snapshot->last_event_src_idx = g_crash_last_event_src_idx;
    snapshot->last_event_size = g_crash_last_event_size;
    snapshot->last_event_tick100ns = g_crash_last_event_tick100ns;

    ULONG used = 0;
    for (ULONG i = 0; i < CRASH_SRC_MAX; i++) {
        crash_fill_src_info(&g_crash_sources[i], &snapshot->sources[i]);
        if (g_crash_sources[i].in_use)
            used++;
    }
    snapshot->in_use_src_cnt = used;

    WdfSpinLockRelease(g_crash_lock);
}

NTSTATUS crash_dump_get_history(_Out_writes_to_(capacity, *cnt) crash_event_t* events,
                                _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    *dropped = 0;

    if (!g_crash_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_crash_lock);

    ULONGLONG seq = g_crash_event_sequence;
    if (seq == 0) {
        WdfSpinLockRelease(g_crash_lock);
        return STATUS_SUCCESS;
    }

    ULONGLONG avail = (seq < CRASH_HISTORY_CAPACITY) ? seq : CRASH_HISTORY_CAPACITY;
    ULONGLONG take = (capacity < avail) ? capacity : avail;
    ULONGLONG skip = avail - take;
    ULONGLONG start_seq = (seq - avail) + skip + 1ull;

    for (ULONGLONG i = 0; i < take; i++) {
        ULONGLONG current_seq = start_seq + i;
        ULONGLONG idx = (current_seq - 1ull) % CRASH_HISTORY_CAPACITY;
        events[i] = g_crash_history[idx];
    }

    *cnt = (ULONG)take;
    *dropped =
        (ULONG)((seq > CRASH_HISTORY_CAPACITY) ? (seq - CRASH_HISTORY_CAPACITY + skip) : skip);

    WdfSpinLockRelease(g_crash_lock);
    return STATUS_SUCCESS;
}

NTSTATUS crash_dump_get_src_info(_In_ ULONG idx, _Out_ crash_src_info_t* info)
{
    if (!info)
        return STATUS_INVALID_PARAMETER;

    if (idx >= CRASH_SRC_MAX)
        return STATUS_INVALID_PARAMETER;

    if (!g_crash_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_crash_lock);
    crash_fill_src_info(&g_crash_sources[idx], info);
    WdfSpinLockRelease(g_crash_lock);

    return info->in_use ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

NTSTATUS crash_dump_set_active_src(_In_ ULONG idx)
{
    if (idx >= CRASH_SRC_MAX)
        return STATUS_INVALID_PARAMETER;

    if (!g_crash_lock)
        return STATUS_DEVICE_NOT_READY;

    WdfSpinLockAcquire(g_crash_lock);

    crash_src_t* s = crash_find_src(idx);
    if (!s) {
        WdfSpinLockRelease(g_crash_lock);
        return STATUS_NOT_FOUND;
    }

    g_active_src = idx;
    crash_publish_state(CRASH_EVENT_SET_ACTIVE, idx, STATUS_SUCCESS, s->blob_len);

    WdfSpinLockRelease(g_crash_lock);
    return STATUS_SUCCESS;
}
