#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <acpiioct.h>

typedef struct _QCPEP_PPXTLMM_GPIO_Resource_NodeSt {
    ULONG PinNumber;
    ULONG GPIOState;
    ULONG FunctionSelect;
    ULONG Direction;
    ULONG PullValue;
    ULONG DriveStrength;
    ULONG EnableLPM;
} QCPEP_PPXTLMM_GPIO_Resource_NodeSt, *PQCPEP_PPXTLMM_GPIO_Resource_NodeSt;

typedef enum _QCPEP_PepResultStatusType {
    QCPEP_PEP_RESOURCE_PARSE_DDI_RESULT_VALID = 0,
    QCPEP_PEP_RESOURCE_PARSE_DDI_RESULT_RESOURCE_PARSING_FAILURE = 1,
    QCPEP_PEP_RESOURCE_PARSE_DDI_RESULT_SLPM_CONSTRAINTS_FAILURE = 2,
    QCPEP_PEP_RESOURCE_PARSE_DDI_RESULT_PEP_INTERNAL_FAILURE = 3,
} QCPEP_PepResultStatusType;

typedef enum _QCPEP_PepErrorLevelType {
    QCPEP_PEP_RESOURCE_PARSE_DDI_ERROR_LEVEL_NONE = 0,
    QCPEP_PEP_RESOURCE_PARSE_DDI_ERROR_LEVEL_CRITICAL = 1,
    QCPEP_PEP_RESOURCE_PARSE_DDI_ERROR_LEVEL_WARNING = 2,
} QCPEP_PepErrorLevelType;

typedef struct _QCPEP_PepResultType {
    QCPEP_PepErrorLevelType errorLevel;
    QCPEP_PepResultStatusType eStatus;
    CHAR deviceName[256];
    ULONGLONG componentID;
    CHAR stateType[256];
    ULONGLONG stateIndex;
    ULONGLONG stateSetIndex;
    CHAR resourceType[256];
    PVOID additionalInformation;
} QCPEP_PepResultType, *PQCPEP_PepResultType;

typedef struct _QCPEP_COMPONENT_OUTPUT_BUFFER_DATA {
    ULONGLONG ComponentCount;
    PACPI_EVAL_OUTPUT_BUFFER ComponentResourceDataList[1];
} QCPEP_COMPONENT_OUTPUT_BUFFER_DATA, *PQCPEP_COMPONENT_OUTPUT_BUFFER_DATA;

typedef NTSTATUS (*QCPEP_PARSE_DEVICE_RESOURCE_BUFFER)(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                                       _Inout_ PQCPEP_PepResultType pep_result);

typedef NTSTATUS (*QCPEP_PARSE_RESOURCE_BUFFER_V1)(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                                   _Inout_opt_ PVOID legacy_ctx);

typedef NTSTATUS (*QCPEP_PARSE_COMPONENT_RESOURCE_BUFFER)(_In_ PQCPEP_COMPONENT_OUTPUT_BUFFER_DATA
                                                              comp_output_buf,
                                                          _Inout_ PQCPEP_PepResultType pep_result);

typedef NTSTATUS (*QCPEP_QCPPX_API_EXECUTE_GPIO)(
    _Inout_ PQCPEP_PPXTLMM_GPIO_Resource_NodeSt tlmm_gpio_res);

typedef NTSTATUS (*QCPEP_PSTATE_TRANSITION_CB)(_In_ UNICODE_STRING proc_name, _In_ ULONG arg1,
                                               _In_ ULONG arg2, _In_ ULONG arg3, _In_ ULONG arg4);

typedef struct _QCPEP_USB_INTERRUPT_DATA {
    ULONG InterruptID;
    ULONG PolarityType;
} QCPEP_USB_INTERRUPT_DATA;

typedef NTSTATUS (*QCPEP_USB_WAKEUP_UPDATE_CB)(_In_ QCPEP_USB_INTERRUPT_DATA interrupt_data);

typedef NTSTATUS (*QCPEP_ENABLE_GFX_ACD_CB)(_In_ UCHAR enable);

typedef NTSTATUS (*QCPEP_BCM_GET_DATA_CB)(_Inout_opt_ PVOID req, _Inout_opt_ PVOID data);

typedef NTSTATUS (*QCPEP_BCM_GET_DATA_SIZE_CB)(_In_opt_z_ PCHAR master, _In_opt_z_ PCHAR slave,
                                               _Out_opt_ PULONG num_cmds_per_bw);

typedef NTSTATUS (*QCPEP_BCM_THROTTLE_CB)(_In_ UCHAR enable_throttling);

typedef struct _QCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE {
    INTERFACE Interface;
    QCPEP_PARSE_RESOURCE_BUFFER_V1 ParseResourceBuffer;
} QCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE,
    *PQCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE;

typedef struct _QCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2 {
    INTERFACE Interface;
    QCPEP_PARSE_DEVICE_RESOURCE_BUFFER ParseDeviceResourceBuffer;
    QCPEP_PARSE_COMPONENT_RESOURCE_BUFFER ParseComponentResourceBuffer;
} QCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2,
    *PQCPEP_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2;

typedef struct _QCPEP_QCPPX_INTERFACE {
    INTERFACE Interface;
    QCPEP_QCPPX_API_EXECUTE_GPIO ExecuteTlmmGpio;
} QCPEP_QCPPX_INTERFACE, *PQCPEP_QCPPX_INTERFACE;

typedef struct _QCPEP_PEP_BCM_INTERFACE {
    INTERFACE Interface;
    QCPEP_BCM_GET_DATA_CB GetData;
    QCPEP_BCM_GET_DATA_SIZE_CB GetDataSize;
    QCPEP_BCM_THROTTLE_CB Throttle;
} QCPEP_PEP_BCM_INTERFACE, *PQCPEP_PEP_BCM_INTERFACE;

typedef struct _QCPEP_PEP_PSTATE_TRANSITION_INTERFACE {
    INTERFACE Interface;
    QCPEP_PSTATE_TRANSITION_CB DriverStateTransition;
} QCPEP_PEP_PSTATE_TRANSITION_INTERFACE, *PQCPEP_PEP_PSTATE_TRANSITION_INTERFACE;

typedef struct _QCPEP_PEP_USB_WAKEUP_INTERRUPT_INTERFACE {
    INTERFACE Interface;
    QCPEP_USB_WAKEUP_UPDATE_CB UpdateUsbWakeupInterrupt;
} QCPEP_PEP_USB_WAKEUP_INTERRUPT_INTERFACE, *PQCPEP_PEP_USB_WAKEUP_INTERRUPT_INTERFACE;

typedef struct _QCPEP_PEP_ENABLE_GFX_ACD_INTERFACE {
    INTERFACE Interface;
    QCPEP_ENABLE_GFX_ACD_CB EnableGfxAcd;
} QCPEP_PEP_ENABLE_GFX_ACD_INTERFACE, *PQCPEP_PEP_ENABLE_GFX_ACD_INTERFACE;

NTSTATUS ddi_reg_all(_In_ WDFDEVICE dev);

/* records are driver-lifetime, nonpaged, keyed by exact ACPI path. */

NTSTATUS ddi_global_init(VOID); /* driverentry, before PnP. */
VOID ddi_global_deinit(VOID);
ULONG runtime_res_dev_cnt(VOID);
NTSTATUS runtime_res_copy(_In_z_ const CHAR* acpi_path,
                          _Out_writes_bytes_to_opt_(capacity, *bytes) PVOID out,
                          _In_ ULONG capacity, _Out_ PULONG bytes);

NTSTATUS runtime_comp_res_copy(_In_z_ const CHAR* acpi_path,
                               _Out_writes_bytes_to_opt_(capacity, *bytes) PVOID out,
                               _In_ ULONG capacity, _Out_ PULONG bytes);
