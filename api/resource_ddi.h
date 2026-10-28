#pragma once
#include <ntddk.h>
#include <wdf.h>
#include <acpiioct.h>

typedef struct tlmm_gpio_resource {
    ULONG PinNumber;
    ULONG GPIOState;
    ULONG FunctionSelect;
    ULONG Direction;
    ULONG PullValue;
    ULONG DriveStrength;
    ULONG EnableLPM;
} tlmm_gpio_resource_t, *tlmm_gpio_resource_ptr_t;

typedef enum resource_parse_status {
    RESOURCE_PARSE_VALID = 0,
    RESOURCE_PARSE_FAILED = 1,
    RESOURCE_PARSE_CONSTRAINTS_FAILED = 2,
    RESOURCE_PARSE_INTERNAL_FAILURE = 3,
} resource_parse_status_t;

typedef enum resource_error_level {
    RESOURCE_ERROR_NONE = 0,
    RESOURCE_ERROR_CRITICAL = 1,
    RESOURCE_ERROR_WARNING = 2,
} resource_error_level_t;

typedef struct resource_parse_result {
    resource_error_level_t errorLevel;
    resource_parse_status_t eStatus;
    CHAR deviceName[256];
    ULONGLONG componentID;
    CHAR stateType[256];
    ULONGLONG stateIndex;
    ULONGLONG stateSetIndex;
    CHAR resourceType[256];
    PVOID additionalInformation;
} resource_parse_result_t, *resource_parse_result_ptr_t;

typedef struct component_resource_buffers {
    ULONGLONG ComponentCount;
    PACPI_EVAL_OUTPUT_BUFFER ComponentResourceDataList[1];
} component_resource_buffers_t, *component_resource_buffers_ptr_t;

typedef NTSTATUS (*parse_device_resources_t)(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                             _Inout_ resource_parse_result_ptr_t pep_result);

typedef NTSTATUS (*parse_legacy_resources_t)(_In_ PACPI_EVAL_OUTPUT_BUFFER output_buf,
                                             _Inout_opt_ PVOID legacy_ctx);

typedef NTSTATUS (*parse_component_resources_t)(_In_ component_resource_buffers_ptr_t
                                                    comp_output_buf,
                                                _Inout_ resource_parse_result_ptr_t pep_result);

typedef NTSTATUS (*execute_tlmm_gpio_t)(_Inout_ tlmm_gpio_resource_ptr_t tlmm_gpio_res);

typedef NTSTATUS (*pstate_transition_t)(_In_ UNICODE_STRING proc_name, _In_ ULONG arg1,
                                        _In_ ULONG arg2, _In_ ULONG arg3, _In_ ULONG arg4);

typedef struct usb_interrupt_data {
    ULONG InterruptID;
    ULONG PolarityType;
} usb_interrupt_data_t;

typedef NTSTATUS (*usb_wakeup_update_t)(_In_ usb_interrupt_data_t interrupt_data);

typedef NTSTATUS (*enable_gfx_acd_t)(_In_ UCHAR enable);

typedef NTSTATUS (*bcm_get_data_t)(_Inout_opt_ PVOID req, _Inout_opt_ PVOID data);

typedef NTSTATUS (*bcm_get_data_size_t)(_In_opt_z_ PCHAR master, _In_opt_z_ PCHAR slave,
                                        _Out_opt_ PULONG num_cmds_per_bw);

typedef NTSTATUS (*bcm_throttle_t)(_In_ UCHAR enable_throttling);

typedef struct resource_enumeration_interface {
    INTERFACE Interface;
    parse_legacy_resources_t ParseResourceBuffer;
} resource_enumeration_interface_t, *resource_enumeration_interface_ptr_t;

typedef struct resource_enumeration_interface_v2 {
    INTERFACE Interface;
    parse_device_resources_t ParseDeviceResourceBuffer;
    parse_component_resources_t ParseComponentResourceBuffer;
} resource_enumeration_interface_v2_t, *resource_enumeration_interface_v2_ptr_t;

typedef struct tlmm_gpio_interface {
    INTERFACE Interface;
    execute_tlmm_gpio_t ExecuteTlmmGpio;
} tlmm_gpio_interface_t, *tlmm_gpio_interface_ptr_t;

typedef struct bcm_interface {
    INTERFACE Interface;
    bcm_get_data_t GetData;
    bcm_get_data_size_t GetDataSize;
    bcm_throttle_t Throttle;
} bcm_interface_t, *bcm_interface_ptr_t;

typedef struct pstate_transition_interface {
    INTERFACE Interface;
    pstate_transition_t DriverStateTransition;
} pstate_transition_interface_t, *pstate_transition_interface_ptr_t;

typedef struct usb_wakeup_interrupt_interface {
    INTERFACE Interface;
    usb_wakeup_update_t UpdateUsbWakeupInterrupt;
} usb_wakeup_interrupt_interface_t, *usb_wakeup_interrupt_interface_ptr_t;

typedef struct gfx_acd_interface {
    INTERFACE Interface;
    enable_gfx_acd_t EnableGfxAcd;
} gfx_acd_interface_t, *gfx_acd_interface_ptr_t;

NTSTATUS ddi_reg_all(_In_ WDFDEVICE dev);
NTSTATUS runtime_res_import(PACPI_EVAL_OUTPUT_BUFFER buffer, resource_parse_result_ptr_t result);

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
