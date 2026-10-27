#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"
#include "node_types.h"

typedef struct pep_work {
    struct pep_work_information* work_information;
    UCHAR need_work;
} pep_work_t;

#ifdef PEPFX
typedef PEP_WORK_TYPE pep_work_type_t;
#else
typedef enum pep_work_type {
    PepWorkActiveComplete = 0,
    PepWorkRequestIdleState = 1,
    PepWorkDevicePower = 2,
    PepWorkRequestPowerControl = 3,
    PepWorkDeviceIdle = 4,
    PepWorkCompleteIdleState = 5,
    PepWorkCompletePerfState = 6,
    PepWorkAcpiNotify = 7,
    PepWorkAcpiEvaluateControlMethodComplete = 8,
    PepWorkMax = 9,
} pep_work_type_t;
#endif

typedef enum pep_work_payload_type {
    PEP_WORKPAYLOAD_DEFAULT = 0,
    PEP_WORKPAYLOAD_DEVICEPOWERSTATE = 1,
    PEP_WORKPAYLOAD_REQUESTPOWERCONTROL = 2,
    PEP_WORKPAYLOAD_NOTIFYIDLESTATE = 3,
    PEP_WORKPAYLOAD_PASSIVECALLBACK = 4,
    PEP_WORKPAYLOAD_REQUESTPERFSTATE = 5,
} pep_work_payload_type_t;

typedef struct pep_passive_callback {
    VOID (*callback)(_In_opt_ PVOID ctx);
    PVOID ctx;
} pep_passive_callback_t;

typedef struct pep_work_power_control {
    POHANDLE dev_handle;
    const GUID* power_control_code;
    PVOID req_ctx;
    PVOID in_buf;
    ULONGLONG in_buf_size;
    PVOID out_buf;
    ULONGLONG out_buf_size;
} pep_work_power_control_t;

typedef struct pep_power_control_req pep_power_control_req_t;

typedef struct pep_work_power_control_ctx pep_work_power_control_ctx_t;

typedef struct pep_work_active_complete {
    POHANDLE dev_handle;
    ULONG comp;
} pep_work_active_complete_t;

typedef struct pep_work_complete_idle_state {
    POHANDLE dev_handle;
    ULONG comp;
} pep_work_complete_idle_state_t;

typedef struct pep_work_dev_power {
    POHANDLE dev_handle;
    UCHAR power_required;
    UCHAR reserved[7];
} pep_work_dev_power_t;

typedef struct pep_work_dev_idle {
    POHANDLE dev_handle;
    UCHAR ignore_idle_timeout;
    UCHAR reserved[7];
} pep_work_dev_idle_t;

typedef struct pep_work_idle_state {
    POHANDLE dev_handle;
    ULONG comp;
    ULONG state;
} pep_work_idle_state_t;

typedef struct pep_work_complete_perf_state {
    POHANDLE dev_handle;
    ULONG comp;
    UCHAR succeeded;
    UCHAR reserved0[3];
} pep_work_complete_perf_state_t;

typedef struct pep_work_acpi_notify {
    POHANDLE dev_handle;
    ULONG notify_code;
} pep_work_acpi_notify_t;

typedef struct _ACPI_METHOD_ARGUMENT_V1 acpi_method_argument_v1_t;

typedef struct pep_work_acpi_evaluate_control_method_complete {
    POHANDLE dev_handle;
    ULONG completion_flags;
    LONG method_status;
    PVOID completion_ctx;
    ULONGLONG output_argument_size;
    acpi_method_argument_v1_t* output_arguments;
} pep_work_acpi_evaluate_control_method_complete_t;

typedef union pep_work_u {
    pep_work_active_complete_t active_complete;
    pep_work_idle_state_t idle_state;
    pep_work_dev_power_t dev_power;
    pep_work_power_control_t power_control;
    pep_work_dev_idle_t dev_idle;
    pep_work_complete_idle_state_t complete_idle_state;
    pep_work_complete_perf_state_t complete_perf_state;
    pep_work_acpi_notify_t acpi_notify;
    pep_work_acpi_evaluate_control_method_complete_t control_method_complete;
    ULONGLONG raw[7];
} pep_work_u_t;

typedef struct pep_work_information {
    pep_work_type_t work_type;
    pep_work_u_t u1;
} pep_work_information_t;

typedef struct pep_power_control_complete {
    PVOID dev_handle;
    const GUID* power_control_code;
    PVOID req_ctx;
    ULONGLONG bytes_returned;
    NTSTATUS status;
} pep_power_control_complete_t;

struct pep_power_control_req {
    PVOID dev_handle;
    const GUID* power_control_code;
    PVOID in_buf;
    ULONGLONG in_buf_size;
    PVOID out_buf;
    ULONGLONG out_buf_size;
    ULONGLONG bytes_returned;
    NTSTATUS status;
};

struct pep_work_power_control_ctx {
    pep_power_control_req_t req;
    PVOID owned_input;
    PVOID original_input_address; /* completion identity ONLY; never dereference. */
    ULONGLONG original_input_size;
};

typedef struct pep_result_alloc {
    ULONG tag;
    ULONG size;
    ULONG capacity;
    ULONG flags;
} pep_result_alloc_t;

typedef struct pep_state_result {
    LONG pep_status;
    LONG status;
    ULONG idx;
} pep_state_result_t;

typedef struct pep_state_result_hdr {
    ULONG version;
    ULONG comp_idx;
    LONG status;
    ULONG pep_status;
    PVOID user_data;
} pep_state_result_hdr_t;

typedef struct pep_work_item {
    LIST_ENTRY link;
    ULONG work_type;
    PVOID data;
    UCHAR data_owned;
    UCHAR reserved[3];
    pep_devnode_t* deferred_dev;
    ULONG deferred_generation;
    POHANDLE original_kernel_handle;
} pep_work_item_t;
