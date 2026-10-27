#pragma once
#include "driver.h"
#include "gpio.h"

NTSTATUS forward_global_init(VOID);
NTSTATUS forward_init_from_dev(_In_ WDFDEVICE dev);
VOID forward_prepare_for_hibernate(VOID);
VOID forward_deinit(VOID);

BOOLEAN forward_enabled(VOID);
BOOLEAN forward_is_interesting_ioctl(_In_ ULONG ioctl);

NTSTATUS forward_send_guid(_In_ WDFDEVICE dev, _In_ const GUID* interface_guid, _In_ ULONG ioctl,
                           _In_opt_ PVOID in_buf, _In_ ULONG in_len, _Out_opt_ PVOID out_buf,
                           _In_ ULONG out_len, _Out_opt_ PULONG bytes_returned);

NTSTATUS forward_send(_In_ WDFDEVICE dev, _In_ ULONG ioctl, _In_opt_ PVOID in_buf,
                      _In_ ULONG in_len, _Out_opt_ PVOID out_buf, _In_ ULONG out_len,
                      _Out_opt_ PULONG bytes_returned);

/* verified 8480 PMIC GPIO class interface (QCOM0F2D / qcpmgpio_8480). Caller must be
 * PASSIVE_LEVEL; recipient must already be started. */

NTSTATUS pmic_gpio_send(_In_ WDFDEVICE dev, _In_ ULONG ioctl,
                        _In_reads_bytes_opt_(in_len) PVOID in_buf, _In_ ULONG in_len,
                        _Out_writes_bytes_opt_(out_len) PVOID out_buf, _In_ ULONG out_len,
                        _Out_opt_ PULONG bytes_returned);

/* the D-state transaction engine must provide snapshot/rollback around this call; this
 * helper does not independently execute a d0/d3 program. */

struct power_action;
NTSTATUS pmic_gpio_apply_action(_In_ WDFDEVICE dev, _In_ const struct power_action* action);

/* actual Windows WDF call to the uniquely selected signed PMIC GPIO provider's
 * GET_GPIO_STATE IOCTL. opaque 8-byte response only: this is NOT a full configuration
 * readback, and NOT a DSDT res vote. */

NTSTATUS pmic_gpio_query_raw_state(_In_ WDFDEVICE dev, _In_ const pm_gpio_state_query_t* query,
                                   _Out_ pm_gpio_state_raw_t* raw_state);
