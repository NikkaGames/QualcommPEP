#pragma once
#include "driver.h"
#include <pepfx.h>
#include "limits.h"
#include "npa_client.h"
#include "res_graph.h"

typedef BOOLEAN (*pep_dev_notify_t)(_In_ ULONG notify, _Inout_opt_ PVOID data);

typedef BOOLEAN (*pep_proc_notify_t)(_In_opt_ PVOID pep_handle, _In_ ULONG notify,
                                     _Inout_opt_ PVOID data);

/* match the public PEP_INFORMATION ARM64 ABI: two USHORTs, four bytes of alignment
 * padding, then three callback pointers. The WDK typedefs must be checked during the
 * actual ARM64 WDK build. */
typedef struct pep_plugin_reg {
    USHORT version;
    USHORT size;
    pep_dev_notify_t dev_notify;
    pep_proc_notify_t proc_notify;
    PVOID acpi_notify;
} pep_plugin_reg_t;

typedef NTSTATUS (*pfn_pofx_reg_plugin_ex_t)(PVOID plugin_reg, ULONGLONG flags,
                                             PVOID kernel_information);

typedef ULONG_PTR (*pep_dev_handler_t)(_Inout_opt_ PVOID data);

typedef VOID (*pep_proc_handler_t)(_Inout_opt_ PVOID data, _In_opt_ PVOID pep_handle);

typedef struct pep_msg_handler {
    ULONG notify;
    pep_dev_handler_t handler_func;
} pep_msg_handler_t;

typedef struct pep_proc_msg_handler {
    ULONG notify;
    pep_proc_handler_t handler_func;
} pep_proc_msg_handler_t;

/* future verified hardware-backend initialization may invoke this exactly once before PoFx
 * reg, after establishing all provider ownership, F-state, UFS, crashdump and external
 * lifecycle constraints. */
typedef struct dpm_scope_admission {
    npa_registry_t* npa_clients;
    res_graph_t* graph;
} dpm_scope_admission_t;
