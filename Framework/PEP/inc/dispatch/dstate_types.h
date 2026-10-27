#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"

typedef enum pep_dev_acceptance_type {
    QC_PEP_DEV_NOT_ACCEPTED = 0,
    QC_PEP_DEV_ACCEPTED_RESERVED = 1,
    QC_PEP_DEV_ACCEPTED = 2,
    QC_PEP_DEV_ACEPTED_MAX = 3,
} pep_dev_acceptance_type_t;

typedef enum pep_dev_reg_state {
    DEV_NOT_PREPARED = 0,
    DEV_PREPARE_ACCEPTED = 1,
    DEV_PREPARE_NOT_ACCEPTED = 2,
    DEV_REGISTERED_MINI_PEP = 3,
    DEV_REG_NOT_ACCEPTED = 4,
    DEV_UNREGISTERED = 5,
    DEV_ABANDONED = 6,
    DEV_REG_STATE_MAX = 7,
} pep_dev_reg_state_t;

typedef struct pep_prepare_dev {
    const UNICODE_STRING* dev_id;
    UCHAR dev_accepted;
    UCHAR reserved[7];
} pep_prepare_dev_t;

typedef struct pep_abandon_dev {
    const UNICODE_STRING* dev_id;
    UCHAR dev_accepted;
    UCHAR reserved[7];
} pep_abandon_dev_t;

typedef struct pep_dev_reg_v2 {
    ULONGLONG flags;
    ULONG comp_cnt;
    PVOID components[1];
} pep_dev_reg_v2_t;

typedef struct pep_reg_dev_v2 {
    const UNICODE_STRING* dev_id;
    POHANDLE kernel_handle;
    pep_dev_reg_v2_t* reg;
    PVOID dev_handle;
    pep_dev_acceptance_type_t dev_accepted;
} pep_reg_dev_v2_t;

typedef struct pep_unregister_dev {
    PVOID dev_handle;
} pep_unregister_dev_t;

typedef struct pep_dev_power_state {
    PVOID dev_handle;
    DEVICE_POWER_STATE power_state;
    UCHAR complete;
    UCHAR system_transition;
} pep_dev_power_state_t;

typedef enum usb_sub_dx_state {
    USB_SUB_DX_STATE_INVALID = 0,
    USB_SUB_DX_STATE_CONNECTED = 1,
    USB_SUB_DX_STATE_DISCONNECTED = 2,
} usb_sub_dx_state_t;
