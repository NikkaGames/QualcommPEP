#include "timer.h"
#include "resource_cache.h"
#include "stats.h"

static WDFTIMER g_timer;
static volatile LONG g_timer_tick_cnt;
static ULONG g_timer_period_ms;
static ULONG g_timer_snapshot_every;

static VOID timer_publish_snapshot(VOID)
{
    res_key_t k;
    res_value_t v;
    ULONGLONG t = KeQueryInterruptTime();

    k.res_id = 0x304D4954u;
    k.sub_type = 0;

    v.value0 = (ULONG)g_timer_tick_cnt;
    v.value1 = g_timer_period_ms;
    v.value2 = (ULONG)t;
    v.value3 = (ULONG)(t >> 32);
    (VOID) res_set(k, v);
}

static VOID timer_func(_In_ WDFTIMER timer)
{
    UNREFERENCED_PARAMETER(timer);
    InterlockedIncrement(&g_timer_tick_cnt);

    InterlockedIncrement(&g_stats.ioctl_cnt);

    if (((ULONG)g_timer_tick_cnt % g_timer_snapshot_every) == 0)
        timer_publish_snapshot();
}

NTSTATUS timer_init(_In_ WDFDRIVER driver)
{
    WDF_TIMER_CONFIG cfg;
    WDF_OBJECT_ATTRIBUTES attrs;

    g_timer_tick_cnt = 0;
    /* the periodic callback runs at DISPATCH_LEVEL; read policy before starting it. */
    g_timer_snapshot_every = registry_query_u_long(L"TimerSnapshotEvery", 6);
    if (g_timer_snapshot_every == 0)
        g_timer_snapshot_every = 1;
    g_timer_period_ms = registry_query_u_long(L"TimerPeriodMs", 5000);
    if (g_timer_period_ms < 200)
        g_timer_period_ms = 200;
    if (g_timer_period_ms > 60000)
        g_timer_period_ms = 60000;

    WDF_TIMER_CONFIG_INIT_PERIODIC(&cfg, timer_func, g_timer_period_ms);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.ParentObject = driver;

    NTSTATUS status = WdfTimerCreate(&cfg, &attrs, &g_timer);
    if (!NT_SUCCESS(status))
        return status;

    (VOID) WdfTimerStart(g_timer, WDF_REL_TIMEOUT_IN_MS(g_timer_period_ms));
    timer_publish_snapshot();
    return STATUS_SUCCESS;
}

VOID timer_deinit(VOID)
{
    if (g_timer) {
        (VOID) WdfTimerStop(g_timer, TRUE);
        timer_publish_snapshot();
        WdfObjectDelete(g_timer);
        g_timer = NULL;
    }
    g_timer_tick_cnt = 0;
}
