#include "acpi.h"
#include "aeob.h"
#include "res_graph.h"
#include "runtime_res.h"
#include "resource_ddi.h"
#include "trace.h"
#include "acpi_eval.tmh"
#ifndef IOCTL_ACPI_EVAL_METHOD_EX
#define IOCTL_ACPI_EVAL_METHOD_EX 0x32C018u
#endif

C_ASSERT(sizeof(ACPI_EVAL_INPUT_BUFFER_EX) == 0x104u);
/* require the extended ACPI input layout and signature used by the evaluation IOCTL. */

C_ASSERT(ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX == 0x41696541u);
C_ASSERT(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_EX, MethodName) == sizeof(ULONG));

static NTSTATUS build_eval_ex_input(_In_reads_bytes_(in_len) const VOID* in_buf, _In_ ULONG in_len,
                                    _Outptr_result_bytebuffer_(*out_len) PVOID* converted,
                                    _Out_ PULONG out_len)
{
    ULONG signature;
    const UCHAR* b = (const UCHAR*)in_buf;
    if (!converted || !out_len)
        return STATUS_INVALID_PARAMETER;
    *converted = NULL;
    *out_len = 0;
    if (!in_buf || in_len < sizeof(ULONG) || in_len > (1u << 20))
        return STATUS_INVALID_PARAMETER;
    RtlCopyMemory(&signature, in_buf, sizeof(signature));

    if (signature == ACPI_EVAL_INPUT_BUFFER_SIGNATURE) {
        PACPI_EVAL_INPUT_BUFFER_EX dest;
        if (in_len != sizeof(ACPI_EVAL_INPUT_BUFFER))
            return STATUS_INVALID_PARAMETER;
        dest = (PACPI_EVAL_INPUT_BUFFER_EX)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*dest),
                                                           POOL_TAG);
        if (!dest)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(dest, sizeof(*dest));
        dest->Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
        RtlCopyMemory(dest->MethodName, b + sizeof(ULONG), 4u);
        *converted = dest;
        *out_len = (ULONG)sizeof(*dest);
        return STATUS_SUCCESS;
    }
    if (signature == ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE) {
        const ACPI_EVAL_INPUT_BUFFER_COMPLEX* src = (const ACPI_EVAL_INPUT_BUFFER_COMPLEX*)in_buf;
        ULONG header = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
        ULONG ex_header = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX_EX, Argument);
        ULONG args_size, alloc_size;
        PACPI_EVAL_INPUT_BUFFER_COMPLEX_EX dest;
        if (in_len < header + sizeof(ACPI_METHOD_ARGUMENT) || src->Size != in_len ||
            src->ArgumentCount == 0 || src->ArgumentCount > 7)
            return STATUS_INVALID_PARAMETER;
        args_size = in_len - header;
        if (args_size > (1u << 20) - ex_header)
            return STATUS_INVALID_PARAMETER;
        alloc_size = ex_header + args_size;
        dest = (PACPI_EVAL_INPUT_BUFFER_COMPLEX_EX)ExAllocatePool2(POOL_FLAG_NON_PAGED, alloc_size,
                                                                   POOL_TAG);
        if (!dest)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(dest, alloc_size);
        dest->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE_EX;
        RtlCopyMemory(dest->MethodName, b + sizeof(ULONG), 4u);
        dest->Size = alloc_size;
        dest->ArgumentCount = src->ArgumentCount;
        RtlCopyMemory(dest->Argument, src->Argument, args_size);
        *converted = dest;
        *out_len = alloc_size;
        return STATUS_SUCCESS;
    }
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS send_acpi_ioctl(_In_ WDFDEVICE dev, _In_reads_bytes_(in_len) PVOID in_buf,
                                _In_ ULONG in_len, _Out_writes_bytes_(out_len) PVOID out_buf,
                                _In_ ULONG out_len, _Out_opt_ PULONG_PTR bytes_returned)
{
    NTSTATUS status;
    WDFIOTARGET target;
    WDF_MEMORY_DESCRIPTOR in_desc, out_desc;
    ULONG_PTR br = 0;
    PVOID converted = NULL;
    ULONG converted_len = 0;
    ULONG ioctl = IOCTL_ACPI_EVAL_METHOD;
    if (!dev || !in_buf || !out_buf || !out_len)
        return STATUS_INVALID_PARAMETER;
    if (bytes_returned)
        *bytes_returned = 0;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    if (registry_query_u_long(L"AcpiUseEvalEx", 1) != 0) {
        status = build_eval_ex_input(in_buf, in_len, &converted, &converted_len);
        if (!NT_SUCCESS(status))
            return status;
        in_buf = converted;
        in_len = converted_len;
        ioctl = IOCTL_ACPI_EVAL_METHOD_EX;
    }
    target = WdfDeviceGetIoTarget(dev);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&in_desc, in_buf, in_len);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&out_desc, out_buf, out_len);
    status = WdfIoTargetSendIoctlSynchronously(target, NULL, ioctl, &in_desc, &out_desc, NULL, &br);
    if (converted)
        ExFreePool(converted);
    if (bytes_returned)
        *bytes_returned = br;
    return status;
}

static NTSTATUS acpi_eval_with_auto_buf(_In_ WDFDEVICE dev, _In_reads_bytes_(in_len) PVOID in_buf,
                                        _In_ ULONG in_len,
                                        _Outptr_ PACPI_EVAL_OUTPUT_BUFFER* out_buf,
                                        _Out_ PULONG out_buf_length)
{
    if (!out_buf || !out_buf_length)
        return STATUS_INVALID_PARAMETER;
    *out_buf = NULL;
    *out_buf_length = 0;

    ULONG out_len = registry_query_u_long(L"AcpiEvalOutBytes", 0x20000u);
    if (out_len < sizeof(ACPI_EVAL_OUTPUT_BUFFER))
        out_len = sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 64;
    if (out_len > (1u << 20))
        out_len = (1u << 20);

    PACPI_EVAL_OUTPUT_BUFFER out = NULL;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    for (ULONG attempt = 0; attempt < 4; attempt++) {
        out = (PACPI_EVAL_OUTPUT_BUFFER)ExAllocatePool2(POOL_FLAG_NON_PAGED, out_len, POOL_TAG);
        if (!out)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(out, out_len);

        ULONG_PTR br = 0;
        status = send_acpi_ioctl(dev, in_buf, in_len, out, out_len, &br);
        /* an unsuccessful ACPI evaluation must not return an empty success package. */

        if (!NT_SUCCESS(status)) {
            CHAR method[5] = {0};
            if (in_len >= sizeof(ULONG) + 4u)
                RtlCopyMemory(method, (const UCHAR*)in_buf + sizeof(ULONG), 4u);
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                        "acpi: method=%c%c%c%c attempt=%lu status=%08X requested=%lu returned=%llu "
                        "signature=%08X length=%lu count=%lu",
                        (method)[0], (method)[1], (method)[2], (method)[3], attempt + 1u, status,
                        out_len, (unsigned long long)br, out->Signature, out->Length, out->Count);
        }
        if (NT_SUCCESS(status)) {

            /* a successful IOCTL must not be interpreted as a valid package. */
            ULONG actual_length = out->Length;
            if (actual_length < 12u || actual_length > out_len || (br != 0 && actual_length > br) ||
                aeo_b_walk(out, actual_length, NULL, NULL, NULL) != AEO_B_OK) {
                CHAR method[5] = {0};
                aeob_status_t parse_result = aeo_b_walk(
                    out, actual_length <= out_len ? actual_length : out_len, NULL, NULL, NULL);
                if (in_len >= sizeof(ULONG) + 4u)
                    RtlCopyMemory(method, (const UCHAR*)in_buf + sizeof(ULONG), 4u);
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                            "acpi: method=%c%c%c%c response invalid attempt=%lu requested=%lu "
                            "returned=%llu signature=%08X length=%lu count=%lu AEOB=%u",
                            (method)[0], (method)[1], (method)[2], (method)[3], attempt + 1u,
                            out_len, (unsigned long long)br, out->Signature, out->Length,
                            out->Count, (unsigned)parse_result);
                status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            *out_buf = out;
            *out_buf_length = actual_length;
            return STATUS_SUCCESS;
        }

        if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
            break;
        ULONG required = (ULONG)br;
        if (out->Length > required)
            required = out->Length;

        if (required <= out_len) {
            if (out_len >= AEOB_MAX_SIZE || out_len > AEOB_MAX_SIZE / 2u)
                break;
            required = out_len * 2u;
        }
        if (required > AEOB_MAX_SIZE)
            break;

        ExFreePool(out);
        out = NULL;
        out_len = required;
    }

    if (out)
        ExFreePool(out);
    return status;
}

static NTSTATUS acpi_get_first_arg(_In_ PACPI_EVAL_OUTPUT_BUFFER out,
                                   _Out_ PACPI_METHOD_ARGUMENT* arg0)
{
    if (!out || out->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    if (out->Count < 1)
        return STATUS_NOT_FOUND;
    *arg0 = (PACPI_METHOD_ARGUMENT)out->Argument;
    return STATUS_SUCCESS;
}

static NTSTATUS acpi_extract_ulong_from_argument(_In_ PACPI_METHOD_ARGUMENT arg, _Out_ PULONG value)
{
    if (!arg || !value)
        return STATUS_INVALID_PARAMETER;
    *value = 0;

    switch (arg->Type) {
        case ACPI_METHOD_ARGUMENT_INTEGER:
            *value = (ULONG)arg->Argument;
            return STATUS_SUCCESS;

        case ACPI_METHOD_ARGUMENT_BUFFER:
            if (arg->DataLength >= sizeof(ULONG)) {
                ULONG v = 0;
                RtlCopyMemory(&v, arg->Data, sizeof(v));
                *value = v;
                return STATUS_SUCCESS;
            }
            if (arg->DataLength >= sizeof(USHORT)) {
                USHORT v = 0;
                RtlCopyMemory(&v, arg->Data, sizeof(v));
                *value = (ULONG)v;
                return STATUS_SUCCESS;
            }
            if (arg->DataLength >= sizeof(UCHAR)) {
                *value = *((UCHAR*)arg->Data);
                return STATUS_SUCCESS;
            }
            return STATUS_OBJECT_TYPE_MISMATCH;

        case ACPI_METHOD_ARGUMENT_STRING: {
            CHAR tmp[64];
            RtlZeroMemory(tmp, sizeof(tmp));
            ULONG copy = arg->DataLength;
            if (copy >= sizeof(tmp))
                copy = sizeof(tmp) - 1;
            if (copy != 0)
                RtlCopyMemory(tmp, arg->Data, copy);
            ULONG v = 0;
            NTSTATUS st = RtlCharToInteger(tmp, 0, &v);
            if (!NT_SUCCESS(st))
                return STATUS_OBJECT_TYPE_MISMATCH;
            *value = v;
            return STATUS_SUCCESS;
        }

        case ACPI_METHOD_ARGUMENT_PACKAGE: {
            if (arg->DataLength < sizeof(ACPI_METHOD_ARGUMENT))
                return STATUS_OBJECT_TYPE_MISMATCH;
            PACPI_METHOD_ARGUMENT inner = (PACPI_METHOD_ARGUMENT)arg->Data;
            return acpi_extract_ulong_from_argument(inner, value);
        }

        default:
            return STATUS_OBJECT_TYPE_MISMATCH;
    }
}

NTSTATUS acpi_eval_method_no_args_ulong(_In_ WDFDEVICE dev, _In_ ULONG method_name,
                                        _Out_ PULONG value)
{
    if (!value)
        return STATUS_INVALID_PARAMETER;

    *value = 0;

    ACPI_EVAL_INPUT_BUFFER inb;
    RtlZeroMemory(&inb, sizeof(inb));
    inb.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    inb.MethodNameAsUlong = method_name;

    PACPI_EVAL_OUTPUT_BUFFER out = NULL;
    ULONG out_len = 0;
    NTSTATUS status = acpi_eval_with_auto_buf(dev, &inb, sizeof(inb), &out, &out_len);
    if (NT_SUCCESS(status)) {
        PACPI_METHOD_ARGUMENT a0 = NULL;
        status = acpi_get_first_arg(out, &a0);
        if (NT_SUCCESS(status))
            status = acpi_extract_ulong_from_argument(a0, value);
    }

    if (out)
        ExFreePool(out);
    return status;
}

NTSTATUS acpi_eval_method_no_args_string(_In_ WDFDEVICE dev, _In_ ULONG method_name,
                                         _Out_ UNICODE_STRING* out_arg, _In_ USHORT max_chars)
{
    if (!out_arg)
        return STATUS_INVALID_PARAMETER;

    out_arg->Length = 0;
    out_arg->MaximumLength = (USHORT)(max_chars * sizeof(WCHAR));
    out_arg->Buffer = NULL;

    ACPI_EVAL_INPUT_BUFFER inb;
    RtlZeroMemory(&inb, sizeof(inb));
    inb.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    inb.MethodNameAsUlong = method_name;

    PACPI_EVAL_OUTPUT_BUFFER out = NULL;
    ULONG out_len = 0;
    NTSTATUS status = acpi_eval_with_auto_buf(dev, &inb, sizeof(inb), &out, &out_len);
    if (NT_SUCCESS(status)) {
        PACPI_METHOD_ARGUMENT a0 = NULL;
        status = acpi_get_first_arg(out, &a0);
        if (NT_SUCCESS(status)) {
            USHORT max_bytes = out_arg->MaximumLength;
            if (max_bytes == 0) {
                status = STATUS_INVALID_PARAMETER;
            } else {
                out_arg->Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED, max_bytes, POOL_TAG);
                if (!out_arg->Buffer) {
                    status = STATUS_INSUFFICIENT_RESOURCES;
                    goto cleanup;
                }
                RtlZeroMemory(out_arg->Buffer, max_bytes);

                if (a0->Type == ACPI_METHOD_ARGUMENT_STRING && a0->DataLength > 0) {
                    ULONG src_len = a0->DataLength;
                    ULONG max_chars_local = (ULONG)(max_chars > 0 ? max_chars - 1 : 0);
                    if (src_len > max_chars_local)
                        src_len = max_chars_local;
                    for (ULONG i = 0; i < src_len; i++)
                        out_arg->Buffer[i] = (WCHAR)((PUCHAR)a0->Data)[i];
                    out_arg->Length = (USHORT)(wcslen(out_arg->Buffer) * sizeof(WCHAR));
                    status = STATUS_SUCCESS;
                } else if (a0->Type == ACPI_METHOD_ARGUMENT_BUFFER && a0->DataLength > 0) {
                    ULONG copy_bytes = a0->DataLength;
                    if (copy_bytes > (ULONG)(max_bytes - sizeof(WCHAR)))
                        copy_bytes = (ULONG)(max_bytes - sizeof(WCHAR));
                    if ((copy_bytes & 1u) == 0 && copy_bytes >= sizeof(WCHAR)) {
                        RtlCopyMemory(out_arg->Buffer, a0->Data, copy_bytes);
                        out_arg->Length = (USHORT)copy_bytes;
                    } else {
                        ULONG max_chars_local = (ULONG)(max_chars > 0 ? max_chars - 1 : 0);
                        if (copy_bytes > max_chars_local)
                            copy_bytes = max_chars_local;
                        for (ULONG i = 0; i < copy_bytes; i++)
                            out_arg->Buffer[i] = (WCHAR)((PUCHAR)a0->Data)[i];
                        out_arg->Length = (USHORT)(wcslen(out_arg->Buffer) * sizeof(WCHAR));
                    }
                    status = STATUS_SUCCESS;
                } else if (a0->Type == ACPI_METHOD_ARGUMENT_INTEGER) {
                    UNICODE_STRING us;
                    us.Buffer = out_arg->Buffer;
                    us.Length = 0;
                    us.MaximumLength = max_bytes;
                    status = RtlInt64ToUnicodeString((LONGLONG)a0->Argument, 10, &us);
                    if (NT_SUCCESS(status))
                        out_arg->Length = us.Length;
                } else {
                    status = STATUS_OBJECT_TYPE_MISMATCH;
                }
            }
        }
    }

cleanup:
    if (out)
        ExFreePool(out);
    return status;
}

NTSTATUS resolve_port_id(_In_ WDFDEVICE dev, _Out_ PULONG port_id)
{
    if (!port_id)
        return STATUS_INVALID_PARAMETER;

    *port_id = 0;

    ULONG uid = 0;
    NTSTATUS status =
        acpi_eval_method_no_args_ulong(dev, (ULONG)('D' << 24 | 'I' << 16 | 'U' << 8 | '_'), &uid);
    if (NT_SUCCESS(status)) {
        *port_id = uid;
        return STATUS_SUCCESS;
    }

    return acpi_query_port_id(dev, port_id);
}

static NTSTATUS acpi_build_dsm_input(_In_ const GUID* guid, _In_ ULONG revision,
                                     _In_ ULONG function_idx,
                                     _In_reads_opt_(arg_cnt) const ULONG* args, _In_ ULONG arg_cnt,
                                     _Outptr_result_bytebuffer_(*in_len_out)
                                         PACPI_EVAL_INPUT_BUFFER_COMPLEX* inb_out,
                                     _Out_ PULONG in_len_out)
{
    if (!guid || !inb_out || !in_len_out)
        return STATUS_INVALID_PARAMETER;
    *inb_out = NULL;
    *in_len_out = 0;

    if (arg_cnt != 0 && args == NULL)
        return STATUS_INVALID_PARAMETER;

    const ULONG inner_arg_len = ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONGLONG));
    ULONG package_bytes = 0;
    for (ULONG i = 0; i < arg_cnt; i++) {
        if (package_bytes > (MAXULONG - inner_arg_len))
            return STATUS_INTEGER_OVERFLOW;
        package_bytes += inner_arg_len;
    }
    if (package_bytes > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    const ULONG arg0_len = ACPI_METHOD_ARGUMENT_LENGTH(16);
    const ULONG arg1_len = ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONGLONG));
    const ULONG arg2_len = ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONGLONG));
    const ULONG arg3_len = ACPI_METHOD_ARGUMENT_LENGTH(package_bytes);
    const ULONG in_len = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + arg0_len +
                         arg1_len + arg2_len + arg3_len;

    PACPI_EVAL_INPUT_BUFFER_COMPLEX inb =
        (PACPI_EVAL_INPUT_BUFFER_COMPLEX)ExAllocatePool2(POOL_FLAG_NON_PAGED, in_len, POOL_TAG);
    if (!inb)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(inb, in_len);

    inb->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    inb->MethodNameAsUlong = (ULONG)('M' << 24 | 'S' << 16 | 'D' << 8 | '_');
    inb->Size = in_len;
    inb->ArgumentCount = 4;

    PACPI_METHOD_ARGUMENT arg = (PACPI_METHOD_ARGUMENT)inb->Argument;
    arg->Type = ACPI_METHOD_ARGUMENT_BUFFER;
    arg->DataLength = 16;
    RtlCopyMemory(arg->Data, guid, 16);

    arg = ACPI_METHOD_NEXT_ARGUMENT(arg);
    arg->Type = ACPI_METHOD_ARGUMENT_INTEGER;
    arg->DataLength = sizeof(ULONGLONG);
    *(ULONGLONG*)arg->Data = (ULONGLONG)revision;

    arg = ACPI_METHOD_NEXT_ARGUMENT(arg);
    arg->Type = ACPI_METHOD_ARGUMENT_INTEGER;
    arg->DataLength = sizeof(ULONGLONG);
    *(ULONGLONG*)arg->Data = (ULONGLONG)function_idx;

    arg = ACPI_METHOD_NEXT_ARGUMENT(arg);
    arg->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    arg->DataLength = (USHORT)package_bytes;

    PACPI_METHOD_ARGUMENT inner = (PACPI_METHOD_ARGUMENT)arg->Data;
    for (ULONG i = 0; i < arg_cnt; i++) {
        inner->Type = ACPI_METHOD_ARGUMENT_INTEGER;
        inner->DataLength = sizeof(ULONGLONG);
        *(ULONGLONG*)inner->Data = (ULONGLONG)args[i];
        inner = ACPI_METHOD_NEXT_ARGUMENT(inner);
    }

    *inb_out = inb;
    *in_len_out = in_len;
    return STATUS_SUCCESS;
}

static NTSTATUS acpi_extract_ulong_array_from_argument(_In_ PACPI_METHOD_ARGUMENT arg,
                                                       _Out_writes_to_(capacity, *cnt)
                                                           PULONG values,
                                                       _In_ ULONG capacity, _Out_ PULONG cnt)
{
    if (!arg || !values || !cnt)
        return STATUS_INVALID_PARAMETER;

    *cnt = 0;
    if (capacity == 0)
        return STATUS_BUFFER_TOO_SMALL;

    switch (arg->Type) {
        case ACPI_METHOD_ARGUMENT_PACKAGE: {
            PUCHAR cursor = (PUCHAR)arg->Data;
            PUCHAR end = cursor + arg->DataLength;

            while ((cursor + sizeof(ACPI_METHOD_ARGUMENT)) <= end && *cnt < capacity) {
                PACPI_METHOD_ARGUMENT inner = (PACPI_METHOD_ARGUMENT)cursor;
                ULONG v = 0;
                if (NT_SUCCESS(acpi_extract_ulong_from_argument(inner, &v))) {
                    values[*cnt] = v;
                    (*cnt)++;
                }

                PACPI_METHOD_ARGUMENT next = ACPI_METHOD_NEXT_ARGUMENT(inner);
                if ((PUCHAR)next <= cursor || (PUCHAR)next > end)
                    break;
                cursor = (PUCHAR)next;
            }

            return (*cnt != 0) ? STATUS_SUCCESS : STATUS_OBJECT_TYPE_MISMATCH;
        }

        case ACPI_METHOD_ARGUMENT_BUFFER: {
            if (arg->DataLength >= sizeof(ULONG) && (arg->DataLength % sizeof(ULONG)) == 0) {
                ULONG n = arg->DataLength / sizeof(ULONG);
                if (n > capacity)
                    n = capacity;

                for (ULONG i = 0; i < n; i++) {
                    ULONG v = 0;
                    RtlCopyMemory(&v, ((PUCHAR)arg->Data) + (i * sizeof(ULONG)), sizeof(ULONG));
                    values[i] = v;
                }
                *cnt = n;
                return STATUS_SUCCESS;
            }
            break;
        }

        default:
            break;
    }

    ULONG v = 0;
    NTSTATUS st = acpi_extract_ulong_from_argument(arg, &v);
    if (!NT_SUCCESS(st))
        return st;
    values[0] = v;
    *cnt = 1;
    return STATUS_SUCCESS;
}

NTSTATUS acpi_eval_dsm_package_ulongs(_In_ WDFDEVICE dev, _In_ const GUID* guid,
                                      _In_ ULONG revision, _In_ ULONG function_idx,
                                      _In_reads_opt_(arg_cnt) const ULONG* args, _In_ ULONG arg_cnt,
                                      _Out_writes_to_(value_capacity, *value_cnt) PULONG values,
                                      _In_ ULONG value_capacity, _Out_ PULONG value_cnt)
{
    if (!dev || !guid || !values || !value_cnt || value_capacity == 0)
        return STATUS_INVALID_PARAMETER;

    *value_cnt = 0;

    PACPI_EVAL_INPUT_BUFFER_COMPLEX inb = NULL;
    ULONG in_len = 0;
    NTSTATUS status =
        acpi_build_dsm_input(guid, revision, function_idx, args, arg_cnt, &inb, &in_len);
    if (!NT_SUCCESS(status))
        return status;

    PACPI_EVAL_OUTPUT_BUFFER out = NULL;
    ULONG out_len = 0;
    status = acpi_eval_with_auto_buf(dev, inb, in_len, &out, &out_len);
    if (NT_SUCCESS(status)) {
        PACPI_METHOD_ARGUMENT a0 = NULL;
        status = acpi_get_first_arg(out, &a0);
        if (NT_SUCCESS(status))
            status = acpi_extract_ulong_array_from_argument(a0, values, value_capacity, value_cnt);
    }

    if (out)
        ExFreePool(out);
    ExFreePool(inb);
    return status;
}

NTSTATUS acpi_eval_dsm_ulong(_In_ WDFDEVICE dev, _In_ const GUID* guid, _In_ ULONG revision,
                             _In_ ULONG function_idx, _Out_ PULONG value)
{
    if (!value)
        return STATUS_INVALID_PARAMETER;

    *value = 0;
    ULONG values[1] = {0};
    ULONG cnt = 0;
    NTSTATUS status = acpi_eval_dsm_package_ulongs(dev, guid, revision, function_idx, NULL, 0,
                                                   values, RTL_NUMBER_OF(values), &cnt);
    if (!NT_SUCCESS(status))
        return status;
    if (cnt == 0)
        return STATUS_NOT_FOUND;

    *value = values[0];
    return STATUS_SUCCESS;
}

NTSTATUS acpi_eval_dsm_ulong_with_arg_ulong(_In_ WDFDEVICE dev, _In_ const GUID* guid,
                                            _In_ ULONG revision, _In_ ULONG function_idx,
                                            _In_ ULONG arg_value, _Out_ PULONG value)
{
    if (!value)
        return STATUS_INVALID_PARAMETER;

    *value = 0;
    ULONG values[1] = {0};
    ULONG args[1] = {arg_value};
    ULONG cnt = 0;
    NTSTATUS status =
        acpi_eval_dsm_package_ulongs(dev, guid, revision, function_idx, args, RTL_NUMBER_OF(args),
                                     values, RTL_NUMBER_OF(values), &cnt);
    if (!NT_SUCCESS(status))
        return status;
    if (cnt == 0)
        return STATUS_NOT_FOUND;

    *value = values[0];
    return STATUS_SUCCESS;
}

/* require Windows hardware testing before deep-idle activation is safe. */
NTSTATUS acpi_molokai_prepare_lpi(_In_ WDFDEVICE dev)
{
    static const GUID pep_acpi_dsm_guid = {
        0x8d5ca34c, 0xae83, 0x4a2a, {0x9d, 0xd1, 0xa7, 0x4f, 0xfe, 0xad, 0x54, 0x8b}};

    if (registry_query_u_long(L"MolokaiAcpiInitDsmEnable", 0) == 0)
        return STATUS_SUCCESS;

    ULONG initialized = 0;
    NTSTATUS status = acpi_eval_dsm_ulong(dev, &pep_acpi_dsm_guid, 0, 1, &initialized);
    if (!NT_SUCCESS(status))
        return status;
    return initialized == 1 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static NTSTATUS query_resource_methods(_In_ WDFDEVICE dev, const CHAR list[5],
                                       _Out_writes_bytes_(capacity * 5) CHAR* names,
                                       _In_ ULONG capacity, _Out_ PULONG name_cnt)
{
    ACPI_EVAL_INPUT_BUFFER inb;
    PACPI_EVAL_OUTPUT_BUFFER out = NULL;
    ULONG out_len = 0;
    NTSTATUS st;
    uint32_t cnt = 0;
    if (!dev || !names || !name_cnt || !capacity || capacity > MOLOKAI_STND_METHOD_MAX)
        return STATUS_INVALID_PARAMETER;
    *name_cnt = 0;
    RtlZeroMemory(names, capacity * 5u);
    RtlZeroMemory(&inb, sizeof(inb));
    inb.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlCopyMemory(&inb.MethodNameAsUlong, list, 4u);
    st = acpi_eval_with_auto_buf(dev, &inb, sizeof(inb), &out, &out_len);
    if (!NT_SUCCESS(st))
        return st;
    aeob_status_t decoded = aeo_b_decode_stnd(out, out_len, names, capacity, &cnt);
    if (decoded != AEO_B_OK) {
        /* print only header metadata, never blindly dereference payload. */

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                    "acpi: list=%s decode failed AEOB=%u length=%lu count=%lu", list,
                    (unsigned)decoded, out_len, (ULONG)out->Count);
        st = STATUS_INVALID_PARAMETER;
    } else {
        *name_cnt = (ULONG)cnt;
        st = STATUS_SUCCESS;
    }
    ExFreePool(out);
    return st;
}

NTSTATUS acpi_query_stnd_method_names(WDFDEVICE dev, CHAR* names, ULONG capacity, PULONG count)
{
    return query_resource_methods(dev, "STND", names, capacity, count);
}

/* do NOT call d0/d3 ACPI execution methods here; obtaining a res description is distinct
 * from applying firmware votes. */

NTSTATUS acpi_compile_molokai_lpmx(_In_ WDFDEVICE pep_dev, _In_z_ const char* dev_path,
                                   _In_ ULONG d_state, _Out_ power_sequence_t* sequence)
{
    ACPI_EVAL_INPUT_BUFFER input;
    PACPI_EVAL_OUTPUT_BUFFER output = NULL;
    ULONG output_length = 0;
    power_result_t result;
    NTSTATUS status;

    if (!pep_dev || !dev_path || !sequence || d_state >= POWER_DSTATE_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;

    RtlZeroMemory(sequence, sizeof(*sequence));
    RtlZeroMemory(&input, sizeof(input));
    input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;

    input.MethodNameAsUlong =
        ((ULONG)'L') | ((ULONG)'P' << 8) | ((ULONG)'M' << 16) | ((ULONG)'X' << 24);

    status = acpi_eval_with_auto_buf(pep_dev, &input, sizeof(input), &output, &output_length);
    if (!NT_SUCCESS(status))
        return status;

    result = power_compile_aeo_b(output, (size_t)output_length, dev_path, d_state, sequence);
    ExFreePool(output);
    if (result != POWER_OK) {
        RtlZeroMemory(sequence, sizeof(*sequence));
        return (result == POWER_BAD_INPUT) ? STATUS_INVALID_PARAMETER : STATUS_DATA_ERROR;
    }
    return STATUS_SUCCESS;
}

/* it does not take PoFx ownership, call active AML power operations or submit hardware
 * votes. */

typedef struct acpi_method_ctx {
    WDFDEVICE dev;
    const CHAR* names;
    ULONG count;
} acpi_method_ctx_t;

static method_eval_result_t acpi_evaluate_desc(void* ctx_arg, const char method[5],
                                               const void** buf, size_t* bytes)
{
    acpi_method_ctx_t* ctx = (acpi_method_ctx_t*)ctx_arg;
    ACPI_EVAL_INPUT_BUFFER input;
    PACPI_EVAL_OUTPUT_BUFFER output = NULL;
    ULONG output_length = 0;
    NTSTATUS status;
    ULONG index;
    if (!ctx || !method || !buf || !bytes || !resource_method_valid(method))
        return METHOD_EVALUATION_FAILED;
    for (index = 0u; index < ctx->count; ++index)
        if (!memcmp(ctx->names + index * 5u, method, 5u))
            break;
    if (index == ctx->count)
        return METHOD_EVALUATION_FAILED;
    *buf = NULL;
    *bytes = 0;
    RtlZeroMemory(&input, sizeof(input));
    input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;

    RtlCopyMemory(&input.MethodNameAsUlong, method, 4);
    status = acpi_eval_with_auto_buf(ctx->dev, &input, sizeof(input), &output, &output_length);
    if (!NT_SUCCESS(status)) {
        if (status == STATUS_OBJECT_NAME_NOT_FOUND || status == STATUS_OBJECT_PATH_NOT_FOUND ||
            status == STATUS_NOT_FOUND)
            return METHOD_NOT_PRESENT;
        /* oem-format evaluation remains fail-closed unless ACPI explicitly says absent. */

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                    "acpi: method=%c%c%c%c evaluation failed status=%08X extended=%lu", (method)[0],
                    (method)[1], (method)[2], (method)[3], status,
                    registry_query_u_long(L"AcpiUseEvalEx", 1));
        return METHOD_EVALUATION_FAILED;
    }
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ACPI,
                "acpi: method=%c%c%c%c evaluated bytes=%lu arguments=%lu", (method)[0], (method)[1],
                (method)[2], (method)[3], output_length, (ULONG)output->Count);
    *buf = output;
    *bytes = (size_t)output_length;
    return METHOD_EVALUATED;
}

static void acpi_release_desc(void* ctx, const void* buf)
{
    UNREFERENCED_PARAMETER(ctx);
    if (buf)
        ExFreePool((PVOID)buf);
}

static void acpi_release_graph_src(const void* buf)
{
    acpi_release_desc(NULL, buf);
}

NTSTATUS acpi_compile_molokai_dev_power(_In_ WDFDEVICE pep_dev, _In_z_ const char* dev_path,
                                        _In_ ULONG target_d_state, _Out_ power_sequence_t* sequence,
                                        _Out_writes_(5) CHAR selected_method[5])
{
    CHAR method_names[MOLOKAI_STND_METHOD_MAX * 5u] = {0};
    const char* method_ptrs[MOLOKAI_STND_METHOD_MAX] = {0};
    ULONG method_cnt = 0, i;
    acpi_method_ctx_t ctx;
    power_result_t result;
    NTSTATUS status;
    if (!pep_dev || !dev_path || !sequence || !selected_method ||
        target_d_state >= POWER_DSTATE_COUNT)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(sequence, sizeof(*sequence));
    RtlZeroMemory(selected_method, 5u);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    status =
        acpi_query_stnd_method_names(pep_dev, method_names, MOLOKAI_STND_METHOD_MAX, &method_cnt);
    if (!NT_SUCCESS(status))
        return status;
    if (!method_cnt || method_cnt > MOLOKAI_STND_METHOD_MAX)
        return STATUS_DATA_ERROR;
    for (i = 0; i < method_cnt; ++i)
        method_ptrs[i] = method_names + (i * 5u);
    ctx.dev = pep_dev;
    ctx.names = method_names;
    ctx.count = method_cnt;
    result =
        power_discover_dev_d_state(method_ptrs, method_cnt, acpi_evaluate_desc, acpi_release_desc,
                                   &ctx, dev_path, target_d_state, sequence, selected_method);
    if (result == POWER_OK)
        return STATUS_SUCCESS;
    if (result == POWER_BAD_INPUT)
        return STATUS_INVALID_PARAMETER;
    if (result == POWER_UNSUPPORTED)
        return STATUS_NOT_SUPPORTED;
    return STATUS_DATA_ERROR;
}

/* this read-only helper grants no ownership. Bind verified providers and reconcile PnP/ACPI
 * identity before execution. */

NTSTATUS acpi_populate_molokai_res_graph(WDFDEVICE pep_dev, res_graph_t* graph,
                                         power_sequence_t* sequence_scratch)
{
    CHAR names[MOLOKAI_STND_METHOD_MAX * 5u] = {0};
    graph_desc_t retained[GRAPH_SRC_MAX] = {0};
    graph_desc_t boot[GRAPH_SRC_MAX] = {0};
    acpi_method_ctx_t ctx = {0};
    runtime_desc_info_t* info = NULL;
    graph_build_workspace_t* workspace = NULL;
    resource_parse_result_t parsed;
    ULONG retained_count = 0u, boot_count = 0u, count = 0u;
    NTSTATUS status = STATUS_DATA_ERROR;
    if (!pep_dev || !graph || !sequence_scratch)
        return STATUS_INVALID_PARAMETER;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || graph->sealed || graph->retired || graph->dev_cnt)
        return STATUS_INVALID_DEVICE_STATE;
    workspace = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*workspace), POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    if (!workspace || !info) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanup;
    }
    ctx.dev = pep_dev;
    ctx.names = names;
    for (ULONG list = 0u; list < 2u; ++list) {
        const char* list_name = list ? "PREL" : "STND";
        status = query_resource_methods(pep_dev, list_name, names, MOLOKAI_STND_METHOD_MAX, &count);
        if (list && (status == STATUS_OBJECT_NAME_NOT_FOUND ||
                     status == STATUS_OBJECT_PATH_NOT_FOUND || status == STATUS_NOT_FOUND))
            continue;
        if (!NT_SUCCESS(status))
            goto cleanup;
        ctx.count = count;
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_ACPI, "acpi: list=%s methods=%lu", list_name,
                    count);
        for (ULONG i = 0u; i < count; ++i) {
            const char* method = names + i * 5u;
            const void* buffer = NULL;
            size_t size = 0u;
            method_eval_result_t evaluated;
            if (retained_count == GRAPH_SRC_MAX) {
                status = STATUS_QUOTA_EXCEEDED;
                goto cleanup;
            }
            evaluated = acpi_evaluate_desc(&ctx, method, &buffer, &size);
            if (evaluated == METHOD_NOT_PRESENT)
                continue;
            if (evaluated != METHOD_EVALUATED || !buffer || !size) {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                            "acpi: method=%s descriptor unavailable result=%u", method, evaluated);
                continue;
            }
            graph_desc_t* source = &retained[retained_count++];
            RtlCopyMemory(source->method, method, 5u);
            source->buf = buffer;
            source->bytes = size;
            source->preload = (uint8_t)list;
            if (aeo_b_empty(buffer, size))
                continue;
            if (!validate_runtime_desc(buffer, size, info)) {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                            "acpi: method=%s descriptor rejected status=%u failure=%u", method,
                            info->status, info->failure);
                continue;
            }
            if (list) {
                /* Preload programs have separate ownership from OS-managed device states. */
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                            "acpi: method=%s preload devices=%lu execution=unavailable", method,
                            info->dev_cnt);
                continue;
            }
            status = runtime_res_import((PACPI_EVAL_OUTPUT_BUFFER)buffer, &parsed);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                            "acpi: method=%s runtime registration failed status=%08X", method,
                            status);
                continue;
            }
            /* Unsupported providers affect their own runtime devices, not the boot graph. */
            if (!info->unsupported_devices)
                boot[boot_count++] = *source;
        }
    }
    if (!boot_count) {
        status = STATUS_NOT_FOUND;
        goto cleanup;
    }
    status = graph_build_from_descriptors(graph, boot, boot_count, sequence_scratch, workspace)
                 ? STATUS_SUCCESS
                 : STATUS_DATA_ERROR;
    if (NT_SUCCESS(status) &&
        !graph_retain_sources(graph, retained, retained_count, acpi_release_graph_src))
        status = STATUS_DATA_ERROR;
cleanup:
    for (ULONG i = 0u; i < retained_count; ++i)
        acpi_release_desc(&ctx, retained[i].buf);
    if (workspace)
        ExFreePool(workspace);
    if (info)
        ExFreePool(info);
    if (!NT_SUCCESS(status))
        graph->retired = 1u;
    return status;
}
