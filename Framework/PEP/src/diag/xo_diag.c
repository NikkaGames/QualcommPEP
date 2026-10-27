#include "xo_diag.h"
#include "resource_cache.h"

static volatile LONG g_xo_in_progress;
static volatile LONG g_xo_blocked_cnt;
static volatile LONG g_xo_acquire_cnt;
static volatile LONG g_xo_release_cnt;
static volatile LONG g_xo_timeout_cnt;
static KEVENT g_xo_event;

static LARGE_INTEGER rel_timeout_ms(_In_ ULONG ms)
{
    LARGE_INTEGER t;

    t.QuadPart = -((LONGLONG)ms * 10000);
    return t;
}

static VOID xo_publish_state(_In_ ULONG event_code)
{
    res_key_t k;
    res_value_t v;
    ULONGLONG t = KeQueryInterruptTime();

    k.res_id = 0x30544F58u;
    k.sub_type = 0;

    v.value0 = (ULONG)g_xo_acquire_cnt;
    v.value1 = (ULONG)g_xo_release_cnt;
    v.value2 = (ULONG)g_xo_timeout_cnt;
    v.value3 = (event_code & 0xFFu) | (((ULONG)g_xo_in_progress & 0xFFu) << 8) |
               (((ULONG)g_xo_blocked_cnt & 0xFFFFu) << 16);

    (VOID) res_set(k, v);

    k.res_id = 0x31544F58u;
    v.value0 = (ULONG)t;
    v.value1 = (ULONG)(t >> 32);
    v.value2 = (ULONG)KeGetCurrentIrql();
    v.value3 = event_code;
    (VOID) res_set(k, v);
}

NTSTATUS xo_test_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);
    g_xo_in_progress = 0;
    g_xo_blocked_cnt = 0;
    g_xo_acquire_cnt = 0;
    g_xo_release_cnt = 0;
    g_xo_timeout_cnt = 0;

    KeInitializeEvent(&g_xo_event, SynchronizationEvent, FALSE);
    xo_publish_state(1u);
    return STATUS_SUCCESS;
}

VOID xo_test_deinit(VOID)
{
    g_xo_in_progress = 0;
    g_xo_blocked_cnt = 0;
    g_xo_acquire_cnt = 0;
    g_xo_release_cnt = 0;
    g_xo_timeout_cnt = 0;
    KeClearEvent(&g_xo_event);
    xo_publish_state(2u);
}

VOID xo_test_handle_req(_In_ LONG req)
{
    /* never block above APC_LEVEL. */
    ULONG wait_ms = registry_query_u_long(L"XoTestWaitMs", 5000);

    if (req > 0) {
        if (InterlockedCompareExchange(&g_xo_in_progress, 1, 0) == 0) {
            InterlockedIncrement(&g_xo_acquire_cnt);
            xo_publish_state(3u);
            return;
        }

        if (KeGetCurrentIrql() > APC_LEVEL) {
            /* caller is at elevated IRQL; cannot wait. */
            InterlockedIncrement(&g_xo_timeout_cnt);
            xo_publish_state(4u);
            return;
        }

        InterlockedIncrement(&g_xo_blocked_cnt);

        LARGE_INTEGER to = rel_timeout_ms(wait_ms);
        NTSTATUS st = KeWaitForSingleObject(&g_xo_event, Executive, KernelMode, FALSE, &to);
        BOOLEAN acquired = FALSE;
        if (NT_SUCCESS(st) && InterlockedCompareExchange(&g_xo_in_progress, 1, 0) == 0) {
            InterlockedIncrement(&g_xo_acquire_cnt);
            xo_publish_state(5u);
            acquired = TRUE;
        } else {
            InterlockedIncrement(&g_xo_timeout_cnt);
            xo_publish_state(6u);
        }

        if (!acquired) {
            LONG bc = InterlockedCompareExchange(&g_xo_blocked_cnt, 0, 0);
            if (bc > 0)
                InterlockedDecrement(&g_xo_blocked_cnt);
        }
        return;
    }

    if (req == -1) {
        if (InterlockedCompareExchange(&g_xo_in_progress, 0, 1) != 1)
            return;

        InterlockedIncrement(&g_xo_release_cnt);

        LONG bc = InterlockedCompareExchange(&g_xo_blocked_cnt, 0, 0);
        if (bc > 0) {
            LONG left = InterlockedDecrement(&g_xo_blocked_cnt);
            if (left < 0)
                InterlockedIncrement(&g_xo_blocked_cnt);
            KeSetEvent(&g_xo_event, IO_NO_INCREMENT, FALSE);
        }
        xo_publish_state(7u);
        return;
    }

    xo_publish_state(8u);
}
