#include "driver.h"
#include "bcl.h"
#include "resource_cache.h"
#include "timer.h"
#include "tsens.h"
#include "diag.h"
#include "peplog.h"
#include "xo_diag.h"
#include "crash_dump.h"
#include "io_target.h"
#include "deep_standby.h"
#include "power_control.h"
#include "mmrm.h"
#include "thermal_request.h"
#include "stats.h"
#include "ex_soc_mgmt.h"
#include "resource_diag.h"
#include "hw_backend_windows_wdf.h"
#include "trace.h"
#include "driver.tmh"

static VOID reinitialize(_In_ PDRIVER_OBJECT driver_object, _In_opt_ PVOID ctx, _In_ ULONG cnt)
{
    UNREFERENCED_PARAMETER(ctx);
    UNREFERENCED_PARAMETER(cnt);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DRIVER, "driver: reinitialize object=%p", driver_object);
}

VOID evt_driver_ctx_cleanup(_In_ WDFOBJECT driver_object)
{
    pep_global_deinit();
    ddi_global_deinit();
    forward_deinit();
    crash_dump_deinit();
    xo_test_deinit();
    deep_standby_deinit();
    power_ctrl_deinit();
    mmrm_deinit();
    thermal_req_deinit();
    ex_soc_deinit();
    rsc_sw_deinit();
    pep_log_deinit();
    diag_deinit();
    tsens_deinit();
    timer_deinit();
    stats_deinit();
    res_deinit();
    bcl_deinit();
    registry_deinit();
    etw_unregister();
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)driver_object));
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT driver_object, _In_ PUNICODE_STRING registry_path)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attrs;

    if (!driver_object || !registry_path)
        return STATUS_INVALID_PARAMETER;

    WPP_INIT_TRACING(driver_object, registry_path);

    /* initialize the backend lifetime mutex before the KMDF driver can receive any PnP,
     * publication, or peer callbacks. */

    hw_backend_win_lifecycle_init();

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);
    attrs.EvtCleanupCallback = evt_driver_ctx_cleanup;

    WDF_DRIVER_CONFIG_INIT(&config, evt_dev_add);
    config.DriverPoolTag = POOL_TAG;

    status = WdfDriverCreate(driver_object, registry_path, &attrs, &config, &g.driver);
    if (!NT_SUCCESS(status)) {
        WPP_CLEANUP(driver_object);
        return status;
    }

    g.driver_object = driver_object;
    RtlInitUnicodeString(&g.services_registry_path, WdfDriverGetRegistryPath(g.driver));

    status = registry_init(g.driver);
    if (NT_SUCCESS(status))
        (VOID) registry_read_config(&g.config);

    /* create forwarding synchronization once, before PnP callbacks. Generic forwarding is
     * optional and must never enable DPM hardware ownership. */

    status = forward_global_init();
    if (!NT_SUCCESS(status))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DRIVER,
                    "driver: forwarding synchronization unavailable status=%08X", status);

    (VOID) ddi_global_init();

    (VOID) bcl_init(g.driver);
    /* a PEP that failed PoFx reg must not appear successfully loaded. */
    status = pep_global_init();
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER,
                    "driver: plugin initialization failed status=%08X", status);
        return status;
    }
    (VOID) res_init(g.driver);
    (VOID) stats_init();
    (VOID) timer_init(g.driver);
    (VOID) tsens_init(g.driver);
    (VOID) diag_init(g.driver);
    (VOID) pep_log_init(g.driver);
    (VOID) deep_standby_init(g.driver);
    (VOID) xo_test_init(g.driver);
    (VOID) crash_dump_init(g.driver);
    (VOID) power_ctrl_init();
    (VOID) mmrm_init(g.driver);
    (VOID) thermal_req_init(g.driver);
    (VOID) ex_soc_init(g.driver);
    (VOID) rsc_sw_init(g.driver);

    IoRegisterDriverReinitialization(driver_object, reinitialize, NULL);

    (VOID) etw_reg();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER,
                "driver: build rev=432 arc-mxa-mapping");
    return STATUS_SUCCESS;
}
