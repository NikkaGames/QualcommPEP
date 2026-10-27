#include "driver.h"

NTSTATUS query_hardware_id(_In_ WDFDEVICE dev, _Out_ UNICODE_STRING* out_id, _In_ USHORT max_chars)
{
    NTSTATUS status;
    WDFMEMORY mem = NULL;
    PWSTR buf = NULL;
    size_t buf_bytes = 0;

    if (!out_id || max_chars < 2u || (size_t)max_chars * sizeof(WCHAR) > MAXUSHORT)
        return STATUS_INVALID_PARAMETER;

    out_id->Length = 0;
    out_id->MaximumLength = (USHORT)(max_chars * sizeof(WCHAR));
    out_id->Buffer = NULL;

    status = WdfDeviceAllocAndQueryProperty(dev, DevicePropertyHardwareID, NonPagedPoolNx,
                                            WDF_NO_OBJECT_ATTRIBUTES, &mem);
    if (!NT_SUCCESS(status))
        return status;

    buf = (PWSTR)WdfMemoryGetBuffer(mem, &buf_bytes);
    if (!buf || buf_bytes < sizeof(WCHAR)) {
        WdfObjectDelete(mem);
        return STATUS_NOT_FOUND;
    }

    /* never call unbounded wcslen() on a property buf returned by the device stack. */

    size_t chars = 0;
    const size_t avail_chars = buf_bytes / sizeof(WCHAR);
    while (chars < avail_chars && buf[chars] != L'\0')
        ++chars;
    if (chars == avail_chars) {
        WdfObjectDelete(mem);
        return STATUS_INVALID_BUFFER_SIZE;
    }
    if (chars >= max_chars)
        chars = max_chars - 1u;

    out_id->Buffer = (PWCH)ExAllocatePool2(POOL_FLAG_PAGED, out_id->MaximumLength, POOL_TAG);
    if (!out_id->Buffer) {
        WdfObjectDelete(mem);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(out_id->Buffer, out_id->MaximumLength);
    RtlCopyMemory(out_id->Buffer, buf, chars * sizeof(WCHAR));
    out_id->Length = (USHORT)(chars * sizeof(WCHAR));

    WdfObjectDelete(mem);
    return STATUS_SUCCESS;
}

static ULONG parse_first_number(_In_ PCUNICODE_STRING s)
{
    ULONG v = 0;
    BOOLEAN seen = FALSE;

    if (!s || !s->Buffer)
        return 0;

    for (USHORT i = 0; i < (s->Length / sizeof(WCHAR)); i++) {
        WCHAR ch = s->Buffer[i];
        if (ch >= L'0' && ch <= L'9') {
            seen = TRUE;
            v = (v * 10) + (ULONG)(ch - L'0');
        } else if (seen) {
            break;
        }
    }

    return v;
}

NTSTATUS acpi_query_port_id(_In_ WDFDEVICE dev, _Out_ PULONG port_id)
{
    UNICODE_STRING hwid = {0};
    ULONG id = 0;

    if (!port_id)
        return STATUS_INVALID_PARAMETER;

    *port_id = 0;

    NTSTATUS qst = query_hardware_id(dev, &hwid, 128);
    if (!NT_SUCCESS(qst))
        return qst;

    id = parse_first_number(&hwid);
    if (hwid.Buffer)
        ExFreePool(hwid.Buffer);

    *port_id = id;
    return STATUS_SUCCESS;
}
