#pragma once
#include "driver.h"

#pragma pack(push, 1)

typedef enum ppmtest_idle_req_type {
    PPMTEST_QUERY_CAPS = 0,
    PPMTEST_QUERY_IDLE_STATES_V2 = 1,
    PPMTEST_QUERY_PLATFORM_STATES = 2,
    PPMTEST_QUERY_PLATFORM_STATE_RESIDENCIES = 3,
    PPMTEST_QUERY_VETO_REASONS = 4,
    PPMTEST_QUERY_VETO_REASON = 5,
    PPMTEST_QUERY_COORDINATED_STATE_NAME = 6,
    PPMTEST_QUERY_PROC_STATE_NAME = 7,
    PPMTEST_QUERY_COORDINATED_STATES = 8,
    PPMTEST_QUERY_COORDINATED_DEPENDENCY = 9,
    PPMTEST_INTERNAL_QUERY_MODE_VETO_REFCOUNT = 10,
    PPMTEST_INTERNAL_ADD_VETO = 11,
    PPMTEST_INTERNAL_REMOVE_VETO = 12,
    PPMTEST_INTERNAL_GET_CPU_HANDLES = 13
} ppmtest_idle_req_type_t;

typedef struct ppmtest_idle_req_header {
    ULONG magic_number;
    ULONG req_type;

    union {
        UCHAR data[1];
        ULONG dummy;
        ULONGLONG dummy_q;
    } u;
} ppmtest_idle_req_header_t;

#pragma pack(pop)

NTSTATUS ppm_test_handle(_Inout_updates_bytes_(buf_size) VOID* buf, _In_ SIZE_T buf_size);
