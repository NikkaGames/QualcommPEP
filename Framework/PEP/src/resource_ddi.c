#include "resource_ddi.h"
#include "resource_cache.h"
#include <ntstrsafe.h>
#include "runtime_res.h"
#include "hw_backend_windows_wdf.h"
#include "trace.h"
#include "resource_ddi.tmh"

typedef struct ddi_parse_stats {
    ULONG arg_cnt;
    ULONG integer_cnt;
    ULONG string_cnt;
    ULONG buf_cnt;
    ULONG package_cnt;
    ULONG unknown_cnt;
    ULONGLONG first_integer;
    ULONG hash;
    CHAR first_string[64];
} ddi_parse_stats_t;

static VOID interface_reference_no_op(_In_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(ctx);
}

static VOID interface_dereference_no_op(_In_ PVOID ctx)
{
    UNREFERENCED_PARAMETER(ctx);
}

static VOID pep_result_init_fail(_Inout_ resource_parse_result_ptr_t r,
                                 _In_ resource_error_level_t lvl, _In_ resource_parse_status_t st)
{
    if (!r)
        return;
    RtlZeroMemory(r, sizeof(*r));
    r->errorLevel = lvl;
    r->eStatus = st;
    (VOID) RtlStringCchCopyA(r->deviceName, RTL_NUMBER_OF(r->deviceName), "qcpep");
}

static __forceinline ULONG ddi_fnv1a32(_In_reads_bytes_(len) const UCHAR* data, _In_ ULONG len,
                                       _In_ ULONG seed)
{
    ULONG h = seed ? seed : 2166136261u;
    for (ULONG i = 0; i < len; i++) {
        h ^= (ULONG)data[i];
        h *= 16777619u;
    }
    return h;
}

static VOID ddi_mirror_parse_stats(_In_ const ddi_parse_stats_t* stats)
{
    if (!stats)
        return;

    res_key_t k;
    res_value_t v;

    k.res_id = 0x53524150u;
    k.sub_type = stats->hash;

    v.value0 = stats->arg_cnt;
    v.value1 = stats->integer_cnt;
    v.value2 = stats->string_cnt;
    v.value3 = stats->buf_cnt | (stats->package_cnt << 16);

    (VOID) res_set(k, v);
}

static NTSTATUS ddi_parse_acpi_output_buf(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                          _Out_ ddi_parse_stats_t* stats)
{
    if (!output_buf || !stats)
        return STATUS_INVALID_PARAMETER;

    if (output_buf->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;

    if (output_buf->Length < sizeof(ACPI_EVAL_OUTPUT_BUFFER))
        return STATUS_INVALID_BUFFER_SIZE;

    RtlZeroMemory(stats, sizeof(*stats));
    stats->hash = 2166136261u;

    PUCHAR base = (PUCHAR)output_buf;
    PUCHAR end = base + output_buf->Length;
    PACPI_METHOD_ARGUMENT arg = (PACPI_METHOD_ARGUMENT)output_buf->Argument;

    for (ULONG i = 0; i < output_buf->Count; i++) {
        if (((PUCHAR)arg + sizeof(ACPI_METHOD_ARGUMENT)) > end)
            return STATUS_DATA_ERROR;

        ULONG arg_bytes = ACPI_METHOD_ARGUMENT_LENGTH(arg->DataLength);
        if (arg_bytes < sizeof(ACPI_METHOD_ARGUMENT))
            return STATUS_DATA_ERROR;
        if (((PUCHAR)arg + arg_bytes) > end)
            return STATUS_DATA_ERROR;

        stats->arg_cnt++;
        stats->hash = ddi_fnv1a32((const UCHAR*)&arg->Type, sizeof(arg->Type), stats->hash);
        stats->hash =
            ddi_fnv1a32((const UCHAR*)&arg->DataLength, sizeof(arg->DataLength), stats->hash);

        switch (arg->Type) {
            case ACPI_METHOD_ARGUMENT_INTEGER: {
                stats->integer_cnt++;
                if (stats->integer_cnt == 1)
                    stats->first_integer = arg->Argument;
                stats->hash =
                    ddi_fnv1a32((const UCHAR*)&arg->Argument, sizeof(arg->Argument), stats->hash);
                break;
            }

            case ACPI_METHOD_ARGUMENT_STRING: {
                stats->string_cnt++;
                if (!stats->first_string[0] && arg->DataLength) {
                    SIZE_T copy = arg->DataLength;
                    if (copy > (sizeof(stats->first_string) - 1))
                        copy = sizeof(stats->first_string) - 1;
                    RtlCopyMemory(stats->first_string, arg->Data, copy);
                    stats->first_string[copy] = 0;
                }
                stats->hash = ddi_fnv1a32((const UCHAR*)arg->Data, arg->DataLength, stats->hash);
                break;
            }

            case ACPI_METHOD_ARGUMENT_BUFFER:
                stats->buf_cnt++;
                stats->hash = ddi_fnv1a32((const UCHAR*)arg->Data, arg->DataLength, stats->hash);
                break;

            case ACPI_METHOD_ARGUMENT_PACKAGE:
                stats->package_cnt++;
                break;

            default:
                stats->unknown_cnt++;
                if (arg->DataLength)
                    stats->hash =
                        ddi_fnv1a32((const UCHAR*)arg->Data, arg->DataLength, stats->hash);
                break;
        }

        arg = ACPI_METHOD_NEXT_ARGUMENT(arg);
    }

    return STATUS_SUCCESS;
}

/* those records used to carry a copy of the buf each, so qcspi8480's 24-device
 * BSRC_SPI.bin (53,556 bytes) was retained 24 times and the device's whole peer population
 * cost about 2.7 MiB of non-paged pool. The submitting thread MUST hold its own reference
 * for as long as it keeps the pointer. */

typedef struct runtime_blob {
    /* only used to queue a dead blob for freeing after the lock is released; a blob referenced
     * by a record is on no list. */

    LIST_ENTRY link;
    LONG ref;
    BOOLEAN charged;
    ULONG bytes;
    UCHAR data[1];
} runtime_blob_t;

typedef struct runtime_dev_record {
    LIST_ENTRY link;
    CHAR path[256];
    ULONG comp_mask;
    /* keep the validated BPMD component desc independently of BSMD. */
    UCHAR comp_desc;
    ULONGLONG registered_at100ns;

    runtime_blob_t* blob;
} runtime_dev_record_t;

/* the total quota bounds the retained bytes whatever a peer submits. */

#define RUNTIME_MAX_RECORDS 128u
#define RUNTIME_MAX_BYTES (64u * 1024u)
#define RUNTIME_MAX_TOTAL_BYTES (4u * 1024u * 1024u)

static LIST_ENTRY g_runtime_devices;
static FAST_MUTEX g_runtime_devices_lock;
static BOOLEAN g_runtime_devices_ready;
static ULONG g_runtime_dev_cnt;
static SIZE_T g_runtime_retained_bytes;

/* the first record to reference a blob is the one that puts its bytes on the quota - not
 * the first reference, which belongs to the submitting thread and must not be charged, or
 * an in-flight submission no record accepts would be counted. */

static VOID runtime_blob_add_record_locked(_Inout_ runtime_blob_t* blob)
{
    ++blob->ref;
    if (!blob->charged) {
        blob->charged = TRUE;
        g_runtime_retained_bytes += (SIZE_T)blob->bytes;
    }
}

/* returns the blob when this was its last reference, for the caller to free once the lock
 * is released; returns NULL otherwise. */

static runtime_blob_t* runtime_blob_release_locked(_Inout_opt_ runtime_blob_t* blob)
{
    if (!blob)
        return NULL;
    if (blob->ref <= 0) {

        TraceEvents(TRACE_LEVEL_ERROR, TRACE_RESOURCE,
                    "ddi: blob reference underflow count=%ld storage=retained", blob->ref);
        return NULL;
    }
    if (--blob->ref > 0)
        return NULL;
    if (blob->charged) {
        blob->charged = FALSE;
        g_runtime_retained_bytes -= (SIZE_T)blob->bytes;
    }
    return blob;
}

/* takes the lock itself, so it must be called at PASSIVE_LEVEL or APC_LEVEL and never with
 * the lock already held; parse_desc_validated, its only caller, has checked PASSIVE_LEVEL. */

static VOID runtime_blob_release_owner(_Inout_ runtime_blob_t* blob)
{
    runtime_blob_t* dead;
    ExAcquireFastMutex(&g_runtime_devices_lock);
    dead = runtime_blob_release_locked(blob);
    ExReleaseFastMutex(&g_runtime_devices_lock);
    if (dead)
        ExFreePool(dead);
}

/* ref starts at 1: that reference belongs to the submitting thread and it must drop it
 * with runtime_blob_release_owner before it returns. */

static runtime_blob_t* runtime_blob_create(_In_reads_bytes_(bytes) const VOID* buf,
                                           _In_ ULONG bytes)
{
    runtime_blob_t* blob;
    if (!buf || bytes == 0u || bytes > RUNTIME_MAX_BYTES)
        return NULL;
    blob = (runtime_blob_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                            FIELD_OFFSET(runtime_blob_t, data) + bytes, POOL_TAG);
    if (!blob)
        return NULL;
    InitializeListHead(&blob->link);
    blob->ref = 1;
    blob->charged = FALSE;
    blob->bytes = bytes;
    RtlCopyMemory(blob->data, buf, bytes);
    return blob;
}

NTSTATUS ddi_global_init(VOID)
{
    if (g_runtime_devices_ready)
        return STATUS_SUCCESS;
    InitializeListHead(&g_runtime_devices);
    ExInitializeFastMutex(&g_runtime_devices_lock);
    g_runtime_dev_cnt = 0;
    g_runtime_retained_bytes = 0;
    g_runtime_devices_ready = TRUE;
    return STATUS_SUCCESS;
}

/* drains under the lock and frees outside it: each record drops its blob reference while
 * the lock is held, and the blobs that reach zero are queued for the same free pass. */

VOID ddi_global_deinit(VOID)
{
    LIST_ENTRY records, blobs;
    if (!g_runtime_devices_ready)
        return;
    InitializeListHead(&records);
    InitializeListHead(&blobs);
    ExAcquireFastMutex(&g_runtime_devices_lock);
    while (!IsListEmpty(&g_runtime_devices)) {
        PLIST_ENTRY e = RemoveHeadList(&g_runtime_devices);
        runtime_dev_record_t* r = CONTAINING_RECORD(e, runtime_dev_record_t, link);
        runtime_blob_t* dead = runtime_blob_release_locked(r->blob);
        r->blob = NULL;
        if (dead)
            InsertTailList(&blobs, &dead->link);
        InsertTailList(&records, e);
    }
    g_runtime_dev_cnt = 0;
    /* every release accounts for itself, so the total returns to zero on its own once the last
     * owner lets go - and every charged blob does still have a holder, because the charge is
     * only ever dropped by the release that takes a blob's count to zero. */

    g_runtime_devices_ready = FALSE;
    ExReleaseFastMutex(&g_runtime_devices_lock);
    while (!IsListEmpty(&records)) {
        PLIST_ENTRY e = RemoveHeadList(&records);
        ExFreePool(CONTAINING_RECORD(e, runtime_dev_record_t, link));
    }
    while (!IsListEmpty(&blobs)) {
        PLIST_ENTRY e = RemoveHeadList(&blobs);
        ExFreePool(CONTAINING_RECORD(e, runtime_blob_t, link));
    }
}

static BOOLEAN ascii_path_equal(_In_z_ const CHAR* a, _In_z_ const CHAR* b)
{
    ULONG i;
    for (i = 0; i < 256u; ++i) {
        UCHAR x = (UCHAR)a[i], y = (UCHAR)b[i];
        if (x >= 'a' && x <= 'z')
            x = (UCHAR)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z')
            y = (UCHAR)(y - 'a' + 'A');
        if (x != y)
            return FALSE;
        if (!x)
            return TRUE;
    }
    return FALSE;
}

/* record one device of a submission against the submission's shared blob. The caller holds
 * its own reference to blob from runtime_blob_create until it drops it with
 * runtime_blob_release_owner, so it may - and must - keep passing the same pointer here
 * once per device of the submission; each. */

static NTSTATUS runtime_res_record(_In_z_ const CHAR* path, _In_ ULONG comp_mask,
                                   _Inout_ runtime_blob_t* blob)
{
    runtime_dev_record_t* rec;
    runtime_dev_record_t* old = NULL;
    runtime_blob_t* dead = NULL;
    PLIST_ENTRY e;
    SIZE_T add, drop, held;
    size_t n = 0;
    if (!g_runtime_devices_ready || !path || !blob)
        return STATUS_INVALID_DEVICE_STATE;
    while (n < 256u && path[n])
        ++n;
    if (n == 0u || n >= 256u)
        return STATUS_INVALID_PARAMETER;
    rec = (runtime_dev_record_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*rec), POOL_TAG);
    if (!rec)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(rec, sizeof(*rec));
    RtlCopyMemory(rec->path, path, n + 1u);
    rec->comp_mask = comp_mask;
    rec->comp_desc = (UCHAR)(comp_mask != 0u);
    rec->registered_at100ns = KeQueryInterruptTime();

    ExAcquireFastMutex(&g_runtime_devices_lock);
    /* the teardown clears this flag with the lock held, so the check above it - which is
     * unsynchronized - is not enough on its own: without this one a record could be inserted
     * into an already drained registry and leak with the blob it references. */

    if (!g_runtime_devices_ready) {
        ExReleaseFastMutex(&g_runtime_devices_lock);
        ExFreePool(rec);
        return STATUS_INVALID_DEVICE_STATE;
    }
    /* locate the record this one replaces WITHOUT unlinking it yet: the two quotas below must
     * be able to refuse this submission and leave the registry exactly as it was. */

    for (e = g_runtime_devices.Flink; e != &g_runtime_devices; e = e->Flink) {
        runtime_dev_record_t* r = CONTAINING_RECORD(e, runtime_dev_record_t, link);
        if (ascii_path_equal(r->path, path) && r->comp_desc == rec->comp_desc) {
            old = r;
            break;
        }
    }
    if (!old && g_runtime_dev_cnt >= RUNTIME_MAX_RECORDS) {
        held = g_runtime_dev_cnt;
        ExReleaseFastMutex(&g_runtime_devices_lock);
        ExFreePool(rec);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: path=%s record limit reached held=%Iu", path, held);
        return STATUS_QUOTA_EXCEEDED;
    }
    /* the quota counts a submission once, however many of its devices are recorded: this
     * insert adds its bytes only if no record has charged the blob yet - ref cannot be used
     * for that test, because the submitting thread already holds a reference - and the record
     * being replaced returns its bytes only if. */

    add = blob->charged ? (SIZE_T)0 : (SIZE_T)blob->bytes;
    drop = (old && old->blob != blob && old->blob->ref == 1 && old->blob->charged)
               ? (SIZE_T)old->blob->bytes
               : (SIZE_T)0;
    if (g_runtime_retained_bytes - drop + add > (SIZE_T)RUNTIME_MAX_TOTAL_BYTES) {
        held = g_runtime_retained_bytes;
        ExReleaseFastMutex(&g_runtime_devices_lock);
        ExFreePool(rec);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: path=%s byte limit reached held=%Iu requested=%Iu", path, held, add);
        return STATUS_QUOTA_EXCEEDED;
    }
    /* reference before release, so that re-recording a path against the blob it already holds
     * cannot take the count through zero. */

    rec->blob = blob;
    runtime_blob_add_record_locked(blob);
    if (old) {
        RemoveEntryList(&old->link);
        dead = runtime_blob_release_locked(old->blob);
        old->blob = NULL;
    }
    InsertTailList(&g_runtime_devices, &rec->link);
    if (!old)
        ++g_runtime_dev_cnt;
    ExReleaseFastMutex(&g_runtime_devices_lock);
    if (old)
        ExFreePool(old);
    if (dead)
        ExFreePool(dead);
    return STATUS_SUCCESS;
}

ULONG runtime_res_dev_cnt(VOID)
{
    ULONG n;
    if (!g_runtime_devices_ready)
        return 0;
    ExAcquireFastMutex(&g_runtime_devices_lock);
    n = g_runtime_dev_cnt;
    ExReleaseFastMutex(&g_runtime_devices_lock);
    return n;
}

NTSTATUS runtime_res_copy(_In_z_ const CHAR* acpi_path,
                          _Out_writes_bytes_to_opt_(capacity, *bytes) PVOID out,
                          _In_ ULONG capacity, _Out_ PULONG bytes)
{
    PLIST_ENTRY e;
    NTSTATUS st = STATUS_NOT_FOUND;
    if (!bytes)
        return STATUS_INVALID_PARAMETER;
    *bytes = 0;
    if (!g_runtime_devices_ready || !acpi_path)
        return STATUS_INVALID_DEVICE_STATE;
    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    ExAcquireFastMutex(&g_runtime_devices_lock);
    for (e = g_runtime_devices.Flink; e != &g_runtime_devices; e = e->Flink) {
        runtime_dev_record_t* r = CONTAINING_RECORD(e, runtime_dev_record_t, link);
        if (!ascii_path_equal(r->path, acpi_path))
            continue;
        /* a record on the list owns a reference, so its blob is alive for as long as this lock is
         * held. */

        *bytes = r->blob->bytes;
        if (!out || capacity < r->blob->bytes) {
            st = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlCopyMemory(out, r->blob->data, r->blob->bytes);
        st = STATUS_SUCCESS;
        break;
    }
    ExReleaseFastMutex(&g_runtime_devices_lock);
    return st;
}

/* a BPMD submission must not overwrite a separate BSMD generation for the same ACPI path.
 * Only the independently validated component-class record is eligible to pin a native F/P
 * producer. This copy owns no borrowed pointer. */
NTSTATUS runtime_comp_res_copy(_In_z_ const CHAR* acpi_path,
                               _Out_writes_bytes_to_opt_(capacity, *bytes) PVOID out,
                               _In_ ULONG capacity, _Out_ PULONG bytes)
{
    PLIST_ENTRY e;
    NTSTATUS st = STATUS_NOT_FOUND;
    if (!bytes)
        return STATUS_INVALID_PARAMETER;
    *bytes = 0;
    if (!g_runtime_devices_ready || !acpi_path)
        return STATUS_INVALID_DEVICE_STATE;
    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    ExAcquireFastMutex(&g_runtime_devices_lock);
    for (e = g_runtime_devices.Flink; e != &g_runtime_devices; e = e->Flink) {
        runtime_dev_record_t* r = CONTAINING_RECORD(e, runtime_dev_record_t, link);
        if (!r->comp_desc || !ascii_path_equal(r->path, acpi_path))
            continue;
        *bytes = r->blob->bytes;
        if (!out || capacity < r->blob->bytes)
            st = STATUS_BUFFER_TOO_SMALL;
        else {
            RtlCopyMemory(out, r->blob->data, r->blob->bytes);
            st = STATUS_SUCCESS;
        }
        break;
    }
    ExReleaseFastMutex(&g_runtime_devices_lock);
    return st;
}

/* peers use the same resource and argument-shape validation as DSDT descriptors.
 * Recording a descriptor for DPM never executes its actions here. */

static NTSTATUS parse_desc_validated(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                     _Inout_ resource_parse_result_ptr_t pep_result,
                                     _In_z_ const CHAR* via)
{
    runtime_desc_info_t* info;
    ddi_parse_stats_t stats;
    runtime_blob_t* blob;
    NTSTATUS st;
    ULONG i;

    if (!pep_result)
        return STATUS_INVALID_PARAMETER;
    pep_result_init_fail(pep_result, RESOURCE_ERROR_CRITICAL, RESOURCE_PARSE_FAILED);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!output_buf)
        return STATUS_INVALID_PARAMETER;
    if (aeo_b_empty(output_buf, output_buf->Length)) {
        pep_result->errorLevel = RESOURCE_ERROR_NONE;
        pep_result->eStatus = RESOURCE_PARSE_VALID;
        return STATUS_SUCCESS;
    }

    st = ddi_parse_acpi_output_buf(output_buf, &stats);
    if (!NT_SUCCESS(st))
        return st;
    ddi_mirror_parse_stats(&stats);

    info = (runtime_desc_info_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    if (!info) {
        pep_result->eStatus = RESOURCE_PARSE_INTERNAL_FAILURE;
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (!validate_runtime_desc(output_buf, output_buf->Length, info)) {
        (VOID) RtlStringCchCopyA(pep_result->deviceName, RTL_NUMBER_OF(pep_result->deviceName),
                                 info->fail_path[0] ? info->fail_path : "(descriptor)");
        (VOID) RtlStringCchCopyA(pep_result->stateType, RTL_NUMBER_OF(pep_result->stateType),
                                 info->fail_state);
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: source=%s path=%s rejected resource=%s AEOB=%u failure=%u", via,
                    pep_result->deviceName, info->fail_state, (ULONG)info->status,
                    (ULONG)info->failure);
        ExFreePool(info);
        return STATUS_DATA_ERROR;
    }
    /* Retain a submission reference across per-device publication and concurrent replacement. */

    if (output_buf->Length == 0u || output_buf->Length > RUNTIME_MAX_BYTES) {
        pep_result->eStatus = RESOURCE_PARSE_INTERNAL_FAILURE;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: source=%s retention limit exceeded bytes=%lu", via, output_buf->Length);
        ExFreePool(info);
        return STATUS_INVALID_BUFFER_SIZE;
    }
    blob = runtime_blob_create(output_buf, output_buf->Length);
    if (!blob) {
        pep_result->eStatus = RESOURCE_PARSE_INTERNAL_FAILURE;
        ExFreePool(info);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (i = 0; i < info->dev_cnt; ++i) {
        const runtime_dev_info_t* d = &info->devices[i];
        st = runtime_res_record(d->path, d->comp_mask, blob);
        if (!NT_SUCCESS(st)) {
            pep_result->eStatus = RESOURCE_PARSE_INTERNAL_FAILURE;
            (VOID) RtlStringCchCopyA(pep_result->deviceName, RTL_NUMBER_OF(pep_result->deviceName),
                                     d->path);
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "ddi: source=%s path=%s record failed status=%08X", via, d->path, st);
            runtime_blob_release_owner(blob);
            ExFreePool(info);
            return st;
        }
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                    "ddi: source=%s path=%s components=%lx F-actions=%lu P-actions=%lu "
                    "D-actions=%lu D0=%u D3=%u support=%s",
                    via, d->path, d->comp_mask, d->f_state_actions, d->p_state_actions,
                    d->d_state_actions, d->has_d0, d->has_d3,
                    d->empty ? "EMPTY" : (d->supported ? "SUPPORTED" : "UNSUPPORTED"));
        /* it stays recorded, it is never executed, and it does not refuse the other devices of the
         * same submission. */

        if (!d->supported)
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "ddi: source=%s path=%s unsupported resource=%s AEOB=%u failure=%u "
                        "execution=disabled",
                        via, d->path, d->unsupported_state, (ULONG)d->unsupported_status,
                        (ULONG)d->unsupported_failure);
    }
    /* if no record took it - DeviceCount cannot be 0 for a desc that validated, but if that
     * ever changes - this is also what frees it. */

    runtime_blob_release_owner(blob);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                "ddi: source=%s devices=%lu supported=%lu unsupported=%lu bytes=%lu", via,
                info->dev_cnt, info->supported_devices, info->unsupported_devices,
                output_buf->Length);
    pep_result->errorLevel = RESOURCE_ERROR_NONE;
    pep_result->eStatus = RESOURCE_PARSE_VALID;
    if (info->dev_cnt)
        (VOID) RtlStringCchCopyA(pep_result->deviceName, RTL_NUMBER_OF(pep_result->deviceName),
                                 info->devices[0].path);
    ExFreePool(info);
    return STATUS_SUCCESS;
}

NTSTATUS runtime_res_import(PACPI_EVAL_OUTPUT_BUFFER buffer, resource_parse_result_ptr_t result)
{
    return parse_desc_validated(buffer, result, "ACPI");
}

static NTSTATUS parse_dev_res_buf_impl(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                       _Inout_ resource_parse_result_ptr_t pep_result)
{
    if (!pep_result)
        return STATUS_INVALID_PARAMETER;
    pep_result_init_fail(pep_result, RESOURCE_ERROR_CRITICAL, RESOURCE_PARSE_FAILED);
    return parse_desc_validated(output_buf, pep_result, "device");
}

static NTSTATUS parse_comp_res_buf_impl(_In_ component_resource_buffers_ptr_t comp_output_buf,
                                        _Inout_ resource_parse_result_ptr_t pep_result)
{
    ULONGLONG cnt, i;
    NTSTATUS st = STATUS_INVALID_PARAMETER;
    if (!pep_result)
        return STATUS_INVALID_PARAMETER;
    pep_result_init_fail(pep_result, RESOURCE_ERROR_CRITICAL, RESOURCE_PARSE_FAILED);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (!comp_output_buf)
        return STATUS_INVALID_PARAMETER;
    cnt = comp_output_buf->ComponentCount;
    if (cnt == 0u || cnt > 1024u)
        return STATUS_INVALID_PARAMETER;
    for (i = 0; i < cnt; i++) {
        PACPI_EVAL_OUTPUT_BUFFER ob = comp_output_buf->ComponentResourceDataList[i];
        if (!ob)
            return STATUS_INVALID_PARAMETER;
        st = parse_desc_validated(ob, pep_result, "component");
        if (!NT_SUCCESS(st))
            return st;
    }
    return st;
}

static NTSTATUS parse_res_buf_v1_impl(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                      _Inout_opt_ PVOID legacy_ctx)
{
    resource_parse_result_t* tmp;
    NTSTATUS st;
    UNREFERENCED_PARAMETER(legacy_ctx);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    tmp = (resource_parse_result_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*tmp), POOL_TAG);
    if (!tmp)
        return STATUS_INSUFFICIENT_RESOURCES;
    st = parse_dev_res_buf_impl(output_buf, tmp);
    ExFreePool(tmp);
    return st;
}

static NTSTATUS execute_tlmm_gpio_impl(_Inout_ tlmm_gpio_resource_ptr_t tlmm_gpio_res)
{
    if (!tlmm_gpio_res)
        return STATUS_INVALID_PARAMETER;

    res_key_t k;
    res_value_t v;

    k.res_id = 0x4D4D4C54u;
    k.sub_type = tlmm_gpio_res->PinNumber;

    v.value0 = tlmm_gpio_res->GPIOState;
    v.value1 = tlmm_gpio_res->FunctionSelect;
    v.value2 = (tlmm_gpio_res->Direction & 0xFF) | ((tlmm_gpio_res->PullValue & 0xFF) << 8) |
               ((tlmm_gpio_res->DriveStrength & 0xFF) << 16);
    v.value3 = (tlmm_gpio_res->EnableLPM & 0xFF);

    (VOID) res_set(k, v);
    return STATUS_NOT_SUPPORTED;
}

/* the ICB owner resolves command data for one route and bandwidth level. */

typedef struct bcm_bw_req {
    ULONGLONG ib;
    ULONGLONG ab;
} bcm_bw_req_t;

typedef struct bcm_cmd_req {
    PCHAR master;
    PCHAR slave;
    ULONG num_bw_levels;
    bcm_bw_req_t* bw_levels;
} bcm_cmd_req_t;

typedef struct bcm_cmd {
    ULONG addr;
    ULONG data;
    ULONG completion;
} bcm_cmd_t;

typedef struct bcm_data {
    ULONG cmds_size;
    bcm_cmd_t* cmds;
    ULONG num_cmds_written;
    ULONG entries_per_req;
} bcm_data_t;

C_ASSERT(sizeof(bcm_bw_req_t) == 16);
C_ASSERT(sizeof(bcm_cmd_t) == 12);
C_ASSERT(FIELD_OFFSET(bcm_cmd_req_t, bw_levels) == 24);
C_ASSERT(FIELD_OFFSET(bcm_data_t, cmds) == 8);
C_ASSERT(FIELD_OFFSET(bcm_data_t, entries_per_req) == 20);

/* everything is copied into our own storage under structured exception handling BEFORE the
 * owner is called, and the commands are copied back afterwards: no exception may unwind
 * through the owner's lock, and no peer pointer is dereferenced while it is held. */

#define BCM_NAME_BYTES 128u
#define BCM_MAX_LEVELS 64u

static NTSTATUS bcm_copy_name(_In_ PCHAR src, _Out_writes_z_(BCM_NAME_BYTES) CHAR* out)
{
    ULONG i;
    out[0] = '\0';
    __try {
        for (i = 0; i + 1u < BCM_NAME_BYTES; ++i) {
            CHAR c = src[i];
            out[i] = c;
            if (!c)
                return STATUS_SUCCESS;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = '\0';
        return GetExceptionCode();
    }

    out[0] = '\0';
    return STATUS_INVALID_PARAMETER;
}

static NTSTATUS bcm_get_data_size_impl(_In_opt_z_ PCHAR master_arg, _In_opt_z_ PCHAR slave_arg,
                                       _Out_opt_ PULONG num_cmds_per_bw)
{
    res_key_t k;
    res_value_t v;
    CHAR master[BCM_NAME_BYTES], slave[BCM_NAME_BYTES];
    ULONG n = 0;
    NTSTATUS st;

    k.res_id = 0x5A534342u;
    k.sub_type = 0;
    v.value0 = (ULONG)(ULONG_PTR)master_arg;
    v.value1 = (ULONG)(ULONG_PTR)slave_arg;
    v.value2 = 0;
    v.value3 = 0;

    if (!master_arg || !slave_arg || !num_cmds_per_bw)
        return STATUS_INVALID_PARAMETER;
    st = bcm_copy_name(master_arg, master);
    if (NT_SUCCESS(st))
        st = bcm_copy_name(slave_arg, slave);
    if (NT_SUCCESS(st))
        st = hw_backend_win_bcm_command_cnt(master, slave, &n);
    __try {
        *num_cmds_per_bw = NT_SUCCESS(st) ? n : 0u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    v.value2 = n;
    v.value3 = (ULONG)st;
    (VOID) res_set(k, v);
    if (!NT_SUCCESS(st))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: BCM size route=%s->%s failed status=%08X", master, slave, st);
    else
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE, "ddi: BCM size route=%s->%s commands=%lu",
                    master, slave, n);
    return st;
}

static NTSTATUS bcm_get_data_impl(_Inout_opt_ PVOID req_arg, _Inout_opt_ PVOID data_arg)
{
    bcm_cmd_req_t* req = (bcm_cmd_req_t*)req_arg;
    bcm_data_t* data = (bcm_data_t*)data_arg;
    icb_command_t cmds[ICB_MAX_COMMANDS];
    bcm_cmd_t* staged = NULL;
    CHAR master[BCM_NAME_BYTES], slave[BCM_NAME_BYTES];
    bcm_bw_req_t level;
    bcm_cmd_req_t req_copy;
    bcm_data_t data_copy;
    res_key_t k;
    res_value_t v;
    ULONG per = 0, written = 0, i, n, idx, total;
    NTSTATUS st;

    k.res_id = 0x444D4342u;
    k.sub_type = 0;
    v.value0 = (ULONG)(ULONG_PTR)req_arg;
    v.value1 = (ULONG)((ULONG_PTR)req_arg >> 32);
    v.value2 = (ULONG)(ULONG_PTR)data_arg;
    v.value3 = (ULONG)((ULONG_PTR)data_arg >> 32);
    (VOID) res_set(k, v);

    if (!req || !data)
        return STATUS_INVALID_PARAMETER;

    __try {
        req_copy = *req;
        data_copy = *data;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    if (!req_copy.master || !req_copy.slave || !req_copy.num_bw_levels || !req_copy.bw_levels ||
        !data_copy.cmds || !data_copy.cmds_size)
        return STATUS_INVALID_PARAMETER;
    if (req_copy.num_bw_levels > BCM_MAX_LEVELS)
        return STATUS_INVALID_PARAMETER;
    st = bcm_copy_name(req_copy.master, master);
    if (NT_SUCCESS(st))
        st = bcm_copy_name(req_copy.slave, slave);
    if (!NT_SUCCESS(st))
        return st;

    st = hw_backend_win_bcm_command_cnt(master, slave, &per);
    __try {
        data->entries_per_req = per;
        data->num_cmds_written = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    if (!NT_SUCCESS(st))
        return st;
    if (!per)
        return STATUS_NOT_FOUND;
    /* a later bandwidth-level failure must not leave a valid prefix of a multi-level RPMh
     * command list in the peer's buf. */

    if (per > ICB_MAX_COMMANDS || req_copy.num_bw_levels > BCM_MAX_LEVELS ||
        req_copy.num_bw_levels > MAXULONG / per)
        return STATUS_DATA_ERROR;
    total = per * req_copy.num_bw_levels;
    if (total > data_copy.cmds_size)
        return STATUS_BUFFER_TOO_SMALL;
    staged =
        (bcm_cmd_t*)ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)total * sizeof(*staged), POOL_TAG);
    if (!staged)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (idx = 0; idx < req_copy.num_bw_levels; ++idx) {
        __try {
            level = req_copy.bw_levels[idx];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            st = GetExceptionCode();
            goto cleanup;
        }
        n = 0;
        st = hw_backend_win_bcm_commands(master, slave, level.ib, level.ab, cmds, ICB_MAX_COMMANDS,
                                         &n);
        if (!NT_SUCCESS(st))
            goto cleanup;
        if (n != per || n > total - written) {
            st = STATUS_INTERNAL_ERROR;
            goto cleanup;
        }
        for (i = 0; i < n; ++i) {
            staged[written + i].addr = cmds[i].address;
            staged[written + i].data = cmds[i].data;
            staged[written + i].completion = cmds[i].completion ? 1u : 0u;
        }
        written += n;
    }
    /* a peer-owned buf that faults while being copied cannot offer true atomic writes; the SEH
     * handler still prevents a kernel exception from escaping. */

    __try {
        RtlCopyMemory(data_copy.cmds, staged, (SIZE_T)written * sizeof(*staged));
        data->num_cmds_written = written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        st = GetExceptionCode();
        goto cleanup;
    }
    st = STATUS_SUCCESS;
cleanup:
    ExFreePool(staged);
    if (!NT_SUCCESS(st))
        return st;
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RESOURCE,
                "ddi: BCM data route=%s->%s levels=%lu commands=%lu written=%lu", master, slave,
                req_copy.num_bw_levels, per, written);
    return STATUS_SUCCESS;
}

static NTSTATUS bcm_throttle_impl(_In_ UCHAR enable_throttling)
{
    res_key_t k;
    res_value_t v;
    ULONGLONG t = KeQueryInterruptTime();

    k.res_id = 0x52544842u;
    k.sub_type = 0;

    v.value0 = enable_throttling ? 1u : 0u;
    v.value1 = (ULONG)t;
    v.value2 = (ULONG)(t >> 32);
    v.value3 = 0;

    (VOID) res_set(k, v);

    return STATUS_SUCCESS;
}

static NTSTATUS usb_wakeup_interrupt_update_impl(_In_ usb_interrupt_data_t interrupt_data)
{
    if (interrupt_data.InterruptID == 0 || interrupt_data.PolarityType == 0)
        return STATUS_INVALID_PARAMETER;
    if (interrupt_data.PolarityType != 1 && interrupt_data.PolarityType != 2)
        return STATUS_INVALID_PARAMETER;

    /* bookkeeping is not evidence that hardware was programmed. */
    res_key_t k;
    res_value_t v;
    ULONGLONG t = KeQueryInterruptTime();

    k.res_id = 0x57425355u;
    k.sub_type = interrupt_data.InterruptID;

    v.value0 = interrupt_data.PolarityType;
    v.value1 = (ULONG)t;
    v.value2 = (ULONG)(t >> 32);
    v.value3 = 0;

    (VOID) res_set(k, v);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS enable_gfx_acd_impl(_In_ UCHAR enable)
{
    /* bookkeeping is not evidence that hardware was programmed. */
    res_key_t k;
    res_value_t v;
    ULONGLONG t = KeQueryInterruptTime();

    k.res_id = 0x41584647u;
    k.sub_type = 0;

    v.value0 = (enable != 0) ? 1u : 0u;
    v.value1 = (ULONG)t;
    v.value2 = (ULONG)(t >> 32);
    v.value3 = 0;

    (VOID) res_set(k, v);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS pstate_transition_impl(_In_ UNICODE_STRING proc_name, _In_ ULONG arg1,
                                       _In_ ULONG arg2, _In_ ULONG arg3, _In_ ULONG arg4)
{
    UNREFERENCED_PARAMETER(proc_name);

    res_key_t k;
    res_value_t v;

    k.res_id = 0x54535450u;
    k.sub_type = 0;

    v.value0 = arg1;
    v.value1 = arg2;
    v.value2 = arg3;
    v.value3 = arg4;

    (VOID) res_set(k, v);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS ddi_reg_all(_In_ WDFDEVICE dev)
{
    if (!dev)
        return STATUS_INVALID_PARAMETER;

    NTSTATUS st;

    st = WdfDeviceCreateDeviceInterface(dev, &GUID_DEVINTERFACE_SOC_PEP, NULL);
    if (!NT_SUCCESS(st))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: SoC interface creation failed status=%08X", st);
    else
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE, "ddi: SoC interface published");

    st =
        WdfDeviceCreateDeviceInterface(dev, &GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE, NULL);
    if (!NT_SUCCESS(st))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: runtime enumeration interface creation failed status=%08X", st);
    if (NT_SUCCESS(st)) {
        static resource_enumeration_interface_t s_res_enum_v1;
        RtlZeroMemory(&s_res_enum_v1, sizeof(s_res_enum_v1));
        s_res_enum_v1.Interface.Size = (USHORT)sizeof(s_res_enum_v1);
        s_res_enum_v1.Interface.Version = 1;
        s_res_enum_v1.Interface.Context = NULL;
        s_res_enum_v1.Interface.InterfaceReference =
            (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_res_enum_v1.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_res_enum_v1.ParseResourceBuffer = parse_res_buf_v1_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_res_enum_v1,
                                        &GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    st = (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1
              ? WdfDeviceCreateDeviceInterface(
                    dev, &GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2, NULL)
              : STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(st)) {
        static resource_enumeration_interface_v2_t s_res_enum;
        RtlZeroMemory(&s_res_enum, sizeof(s_res_enum));
        s_res_enum.Interface.Size = (USHORT)sizeof(s_res_enum);
        s_res_enum.Interface.Version = 2;
        s_res_enum.Interface.Context = NULL;
        s_res_enum.Interface.InterfaceReference = (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_res_enum.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_res_enum.ParseDeviceResourceBuffer = parse_dev_res_buf_impl;
        s_res_enum.ParseComponentResourceBuffer = parse_comp_res_buf_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_res_enum,
                                        &GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    st = (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1
              ? WdfDeviceCreateDeviceInterface(dev, &GUID_PEP_GPIO_DEVICE_INTERFACE, NULL)
              : STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(st)) {
        static tlmm_gpio_interface_t s_qcppx;
        RtlZeroMemory(&s_qcppx, sizeof(s_qcppx));
        s_qcppx.Interface.Size = (USHORT)sizeof(s_qcppx);
        s_qcppx.Interface.Version = 1;
        s_qcppx.Interface.Context = NULL;
        s_qcppx.Interface.InterfaceReference = (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_qcppx.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_qcppx.ExecuteTlmmGpio = execute_tlmm_gpio_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_qcppx, &GUID_PEP_TLMM_GPIO_INTERFACE,
                                        NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    st = WdfDeviceCreateDeviceInterface(dev, &GUID_PEP_BCM_INTERFACE, NULL);
    if (!NT_SUCCESS(st))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "ddi: BCM interface creation failed status=%08X", st);
    if (NT_SUCCESS(st)) {
        static bcm_interface_t s_bcm;
        RtlZeroMemory(&s_bcm, sizeof(s_bcm));
        s_bcm.Interface.Size = (USHORT)sizeof(s_bcm);
        s_bcm.Interface.Version = 1;
        s_bcm.Interface.Context = NULL;
        s_bcm.Interface.InterfaceReference = (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_bcm.Interface.InterfaceDereference = (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_bcm.GetData = bcm_get_data_impl;
        s_bcm.GetDataSize = bcm_get_data_size_impl;
        s_bcm.Throttle = bcm_throttle_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_bcm, &GUID_PEP_BCM_INTERFACE, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    st = (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1
              ? WdfDeviceCreateDeviceInterface(dev, &GUID_PEP_PSTATE_TRANSITION_INTERFACE, NULL)
              : STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(st)) {
        static pstate_transition_interface_t s_pstate;
        RtlZeroMemory(&s_pstate, sizeof(s_pstate));
        s_pstate.Interface.Size = (USHORT)sizeof(s_pstate);
        s_pstate.Interface.Version = 1;
        s_pstate.Interface.Context = NULL;
        s_pstate.Interface.InterfaceReference = (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_pstate.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_pstate.DriverStateTransition = pstate_transition_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_pstate,
                                        &GUID_PEP_PSTATE_TRANSITION_INTERFACE, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    /* ---------------- USB wakeup interrupt update DDI ----------------. */
    st = (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1
              ? WdfDeviceCreateDeviceInterface(dev, &GUID_PEP_USB_WAKEUP_INTERRUPTS_INTERFACE, NULL)
              : STATUS_NOT_SUPPORTED);
    if (NT_SUCCESS(st)) {
        static usb_wakeup_interrupt_interface_t s_usb_wake_ddi;
        RtlZeroMemory(&s_usb_wake_ddi, sizeof(s_usb_wake_ddi));
        s_usb_wake_ddi.Interface.Size = (USHORT)sizeof(s_usb_wake_ddi);
        s_usb_wake_ddi.Interface.Version = 1;
        s_usb_wake_ddi.Interface.Context = NULL;
        s_usb_wake_ddi.Interface.InterfaceReference =
            (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_usb_wake_ddi.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_usb_wake_ddi.UpdateUsbWakeupInterrupt = usb_wakeup_interrupt_update_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_usb_wake_ddi,
                                        &GUID_PEP_USB_WAKEUP_INTERRUPTS_INTERFACE, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    if (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1) {
        static gfx_acd_interface_t s_gfx_acd_ddi;
        RtlZeroMemory(&s_gfx_acd_ddi, sizeof(s_gfx_acd_ddi));
        s_gfx_acd_ddi.Interface.Size = (USHORT)sizeof(s_gfx_acd_ddi);
        s_gfx_acd_ddi.Interface.Version = 1;
        s_gfx_acd_ddi.Interface.Context = NULL;
        s_gfx_acd_ddi.Interface.InterfaceReference =
            (PINTERFACE_REFERENCE)interface_reference_no_op;
        s_gfx_acd_ddi.Interface.InterfaceDereference =
            (PINTERFACE_DEREFERENCE)interface_dereference_no_op;
        s_gfx_acd_ddi.EnableGfxAcd = enable_gfx_acd_impl;

        WDF_QUERY_INTERFACE_CONFIG cfg;
        WDF_QUERY_INTERFACE_CONFIG_INIT(&cfg, (PINTERFACE)&s_gfx_acd_ddi,
                                        &GUID_PEP_ENABLE_GFX_ACD_INTERFACE, NULL);
        (VOID) WdfDeviceAddQueryInterface(dev, &cfg);
    }

    return STATUS_SUCCESS;
}
