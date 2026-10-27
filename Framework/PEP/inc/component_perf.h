#pragma once
#include "driver.h"

/* keep these LONG-sized for ABI stability (the decompiled driver uses LONG),. */

typedef LONG pep_perf_state_unit_t;
#define PEP_PERF_STATE_UNIT_OTHER ((pep_perf_state_unit_t)0)
#define PEP_PERF_STATE_UNIT_FREQ ((pep_perf_state_unit_t)1)
#define PEP_PERF_STATE_UNIT_BANDWIDTH ((pep_perf_state_unit_t)2)

typedef LONG pep_perf_state_type_t;
#define PEP_PERF_STATE_TYPE_DISCRETE ((pep_perf_state_type_t)0)
#define PEP_PERF_STATE_TYPE_RANGE ((pep_perf_state_type_t)1)

typedef struct pep_perf_state {
    ULONGLONG value;
    PVOID ctx;
} pep_perf_state_t;

typedef struct pep_query_comp_perf_caps {
    PVOID dev_handle;
    ULONG comp;
    ULONG set_cnt;
} pep_query_comp_perf_caps_t;

typedef struct pep_query_comp_perf_set {
    PVOID dev_handle;
    ULONG comp;
    ULONG set;
    ULONGLONG flags;
    pep_perf_state_unit_t unit;
    pep_perf_state_type_t type;

    union {
        struct {
            ULONG cnt;
        } discrete;

        struct {
            ULONGLONG minimum;
            ULONGLONG maximum;
        } range;
    } u;
} pep_query_comp_perf_set_t;

typedef struct pep_query_comp_perf_set_name {
    PVOID dev_handle;
    ULONG comp;
    ULONG set;
    USHORT name_size;
    PWSTR name;
} pep_query_comp_perf_set_name_t;

typedef struct pep_query_comp_perf_states {
    PVOID dev_handle;
    ULONG comp;
    ULONG set;
    pep_perf_state_t* states;
} pep_query_comp_perf_states_t;

typedef struct pep_comp_perf_state_req {
    ULONG set;

    union {
        ULONG state_idx;
        ULONGLONG state_value;
    } u;
} pep_comp_perf_state_req_t;

typedef struct pep_req_comp_perf_state {
    PVOID dev_handle;
    ULONG comp;
    UCHAR completed;
    UCHAR succeeded;
    ULONG perf_requests_cnt;
    pep_comp_perf_state_req_t* perf_requests;
} pep_req_comp_perf_state_t;

typedef struct pep_query_current_comp_perf_state {
    PVOID dev_handle;
    ULONG comp;
    ULONG set;

    union {
        ULONG state_idx;
        ULONGLONG state_value;
    } u;
} pep_query_current_comp_perf_state_t;

typedef struct pep_comp_perf_set {
    UNICODE_STRING name;
    ULONGLONG flags;
    pep_perf_state_unit_t unit;
    pep_perf_state_type_t type;

    union {
        struct {
            ULONG cnt;
            pep_perf_state_t* states;
        } discrete;

        struct {
            ULONGLONG minimum;
            ULONGLONG maximum;
        } range;
    } u;
} pep_comp_perf_set_t;

typedef struct pep_comp_perf_info {
    ULONG set_cnt;
    pep_comp_perf_set_t perf_state_sets[1];
} pep_comp_perf_info_t;

typedef struct pep_reg_comp_perf_states {
    PVOID dev_handle;
    ULONG comp;
    ULONGLONG flags;
    pep_comp_perf_info_t* perf_state_info;
} pep_reg_comp_perf_states_t;

/* never advertise cache-only PoFx F/P states as physical transitions. A verified
 * per-device provider must replace this disabled gate before component support or UFS
 * PSTATE_SET may be advertised. */

BOOLEAN comp_perf_hardware_backend_ready(VOID);

NTSTATUS comp_perf_init_for_dev(_Inout_ PVOID pep_dev_handle, _In_ ULONG comp_cnt);
VOID comp_perf_deinit_for_dev(_Inout_ PVOID pep_dev_handle);

ULONG comp_perf_get_set_cnt(_In_ PVOID pep_dev_handle, _In_ ULONG comp);
BOOLEAN comp_perf_query_set(_In_ PVOID pep_dev_handle, _In_ ULONG comp,
                            _Inout_ pep_query_comp_perf_set_t* q);
BOOLEAN comp_perf_query_set_name(_In_ PVOID pep_dev_handle,
                                 _Inout_ pep_query_comp_perf_set_name_t* q);
BOOLEAN comp_perf_query_states(_In_ PVOID pep_dev_handle, _Inout_ pep_query_comp_perf_states_t* q);
BOOLEAN comp_perf_reg_states(_In_ PVOID pep_dev_handle, _Inout_ pep_reg_comp_perf_states_t* r);
BOOLEAN comp_perf_req_state(_In_ PVOID pep_dev_handle, _Inout_ pep_req_comp_perf_state_t* r);
BOOLEAN comp_perf_query_current_state(_In_ PVOID pep_dev_handle,
                                      _Inout_ pep_query_current_comp_perf_state_t* q);
