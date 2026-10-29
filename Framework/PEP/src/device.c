#include "driver.h"
#include "stats.h"
#include "po_fx.h"
#include "acpi.h"
#include "lpi.h"
#include "wmi.h"
#include "chipinfo.h"
#include "tsens.h"
#include "thermal_request.h"
#include "mmrm.h"
#include "io_target.h"
#include "deep_standby.h"
#include "interrupt.h"
#include "rsc_pnp_win.h"
#include "oem_rsc_pnp_evidence_win.h"
#include "rsc_irq_pnp_win.h"
#include "rpmh_oem_pnp_win.h"
#include "oem_wdf_irq_create.h"
#include "hw_backend_windows_wdf.h"
#include "bus_dcvs_win.h"
#include <sddl.h>
#include "trace.h"
#include "device.tmh"

globals_t g = {0};

EVT_WDF_DEVICE_ARM_WAKE_FROM_SX evt_dev_arm_wake_from_sx;
EVT_WDF_DEVICE_DISARM_WAKE_FROM_SX evt_dev_disarm_wake_from_sx;
EVT_WDF_DEVICE_WAKE_FROM_SX_TRIGGERED evt_dev_wake_from_sx_triggered;

typedef struct dmf_exsoc_reg_payload {
    GUID interface_guid;
    USHORT port_id;
    USHORT reserved;
    ULONG state_ioctl;
    ULONG ctrl_ioctl;
    ULONG input_size;
} dmf_exsoc_reg_payload_t;

static const GUID dmf_exsoc_interface_guid = {
    0x55beca03, 0xba2c, 0x4fbc, {0x9e, 0xfc, 0xee, 0x8e, 0xca, 0x56, 0xf0, 0xc7}};

static NTSTATUS try_reg_ex_soc_dmf_target(_In_ WDFDEVICE dev, _In_ ULONG port_id)
{
    if (!dev || port_id > 0xFFFFu)
        return STATUS_INVALID_PARAMETER;

    if (!registry_query_u_long(L"DmfExSocRegisterOnStart", 1))
        return STATUS_NOT_SUPPORTED;

    dmf_exsoc_reg_payload_t payload;
    RtlZeroMemory(&payload, sizeof(payload));
    payload.interface_guid = dmf_exsoc_interface_guid;
    payload.port_id = (USHORT)port_id;
    payload.state_ioctl = IOCTL_PEP_SUBSYS_ADSP_STATE;
    payload.ctrl_ioctl = IOCTL_PEP_SUBSYS_ADSP_CTRL;
    payload.input_size = sizeof(ULONGLONG);

    ULONG bytes = 0;
    NTSTATUS st = forward_send(dev, IOCTL_PEP_DMF_DEVICEINTERFACE_SEND, &payload, sizeof(payload),
                               NULL, 0, &bytes);

    if (NT_SUCCESS(st)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: ExSoc registration port=%lu",
                    port_id);
        return STATUS_SUCCESS;
    }
    if (st != STATUS_NOT_FOUND) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: ExSoc registration failed status=%08X port=%lu", st, port_id);
    }
    return st;
}

static NTSTATUS create_ioctl_queue(_In_ WDFDEVICE dev, _Out_ WDFQUEUE* queue_out)
{
    WDF_IO_QUEUE_CONFIG qcfg;
    WDFQUEUE queue;
    NTSTATUS status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&qcfg, WdfIoQueueDispatchParallel);
    qcfg.EvtIoDeviceControl = evt_io_dev_control;
    qcfg.EvtIoStop = evt_io_stop;

    status = WdfIoQueueCreate(dev, &qcfg, WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status))
        return status;

    *queue_out = queue;
    (VOID) forward_init_from_dev(dev);
    return STATUS_SUCCESS;
}

VOID evt_dev_ctx_cleanup(_In_ WDFOBJECT dev_object)
{
    WDFDEVICE dev = (WDFDEVICE)dev_object;

    p_dev_ctx_t ctx = get_dev_ctx(dev);

    thermal_req_cancel_dev(dev);
    mmrm_detach_dev(dev);

    if (ctx->hid.Buffer) {
        ExFreePool(ctx->hid.Buffer);
        ctx->hid.Buffer = NULL;
        ctx->hid.Length = 0;
        ctx->hid.MaximumLength = 0;
    }
}

NTSTATUS create_control_dev(_In_ WDFDRIVER driver)
{
    NTSTATUS status;
    PWDFDEVICE_INIT p_init = NULL;
    WDFDEVICE dev = NULL;
    WDF_OBJECT_ATTRIBUTES attrs;
    UNICODE_STRING name;
    UNICODE_STRING sym;
    UNICODE_STRING sym2;

    UNICODE_STRING sym_global;

    if (g.control_dev_created)
        return STATUS_SUCCESS;

    static const UNICODE_STRING sddl = RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    p_init = WdfControlDeviceInitAllocate(driver, &sddl);
    if (!p_init)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlInitUnicodeString(&name, CONTROL_DEVICE_NAME);
    status = WdfDeviceInitAssignName(p_init, &name);
    if (!NT_SUCCESS(status))
        goto exit;

    WdfDeviceInitSetDeviceType(p_init, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(p_init, FALSE);

    WDF_FILEOBJECT_CONFIG fcfg;
    WDF_FILEOBJECT_CONFIG_INIT(&fcfg, evt_file_create, WDF_NO_EVENT_CALLBACK, evt_file_close);
    WdfDeviceInitSetFileObjectConfig(p_init, &fcfg, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, dev_ctx_t);
    attrs.EvtCleanupCallback = evt_dev_ctx_cleanup;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfDeviceCreate(&p_init, &attrs, &dev);
    if (!NT_SUCCESS(status))
        goto exit;

    /* control devices have no PnP package; these power-policy DDIs dereference it and
     * bugcheck. */

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->dev = dev;
    ctx->is_control_dev = TRUE;
    ctx->is_first_boot = TRUE;
    ctx->thermal_low_temp_deci_k = MAXLONG;
    ctx->thermal_high_temp_deci_k = MAXLONG;
    ctx->pre_power_action = PowerActionNone;
    ctx->rsc_drv2_assignment_status = STATUS_DEVICE_NOT_READY;
    ctx->rsc_drv2_assignment_observed = 0;
    ctx->rsc_drv2_pn_p_window_present = 0;

    status = create_ioctl_queue(dev, &ctx->ioctl_queue);
    if (!NT_SUCCESS(status))
        goto exit;

    /* default; an explicit developer override is required until verified. */
    if (registry_query_u_long(L"AllowUnverified8480Interfaces", 0) == 1) {
        (VOID) WdfDeviceCreateDeviceInterface(dev, &GUID_TSENS_QUERY, NULL);
        (VOID) WdfDeviceCreateDeviceInterface(dev, &GUID_READ_STABLE_TSENS, NULL);
    }
    (VOID) WdfDeviceCreateDeviceInterface(dev, &GUID_DEVINTERFACE_BRANCH_TRACK, NULL);

    RtlInitUnicodeString(&sym, CONTROL_SYMBOLIC_LINK);
    (VOID) WdfDeviceCreateSymbolicLink(dev, &sym);

    RtlInitUnicodeString(&sym2, THERMAL_SYMBOLIC_LINK);
    (VOID) WdfDeviceCreateSymbolicLink(dev, &sym2);

    RtlInitUnicodeString(&sym_global, MODEM_STANDBY_GLOBAL_SYMBOLIC_LINK);
    (VOID) WdfDeviceCreateSymbolicLink(dev, &sym_global);

    WdfControlFinishInitializing(dev);
    g.control_dev_created = TRUE;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: control endpoint created");
    return STATUS_SUCCESS;

exit:
    if (p_init)
        WdfDeviceInitFree(p_init);
    return status;
}

NTSTATUS evt_dev_add(_In_ WDFDRIVER driver, _Inout_ PWDFDEVICE_INIT dev_init)
{
    WDF_DEVICE_POWER_CAPABILITIES pwr_caps;
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idle;
    WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS wake;
    WDF_POWER_POLICY_EVENT_CALLBACKS ppc;
    UNREFERENCED_PARAMETER(driver);

    NTSTATUS status;
    WDFDEVICE dev;
    WDF_OBJECT_ATTRIBUTES attrs;
    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware = evt_dev_prepare_hardware;
    pnp.EvtDeviceReleaseHardware = evt_dev_release_hardware;
    pnp.EvtDeviceSelfManagedIoInit = evt_dev_self_managed_io_init;
    pnp.EvtDeviceSelfManagedIoCleanup = evt_dev_self_managed_io_cleanup;
    pnp.EvtDeviceSurpriseRemoval = evt_dev_surprise_removal;
    pnp.EvtDeviceD0Entry = evt_dev_d0_entry;
    pnp.EvtDeviceD0EntryPostInterruptsEnabled = evt_dev_d0_entry_post_interrupts_enabled;
    pnp.EvtDeviceD0Exit = evt_dev_d0_exit;
    pnp.EvtDeviceD0ExitPreInterruptsDisabled = evt_dev_d0_exit_pre_interrupts_disabled;
    WdfDeviceInitSetPnpPowerEventCallbacks(dev_init, &pnp);
    WDF_POWER_POLICY_EVENT_CALLBACKS_INIT(&ppc);
    ppc.EvtDeviceArmWakeFromSx = evt_dev_arm_wake_from_sx;
    ppc.EvtDeviceDisarmWakeFromSx = evt_dev_disarm_wake_from_sx;
    ppc.EvtDeviceWakeFromSxTriggered = evt_dev_wake_from_sx_triggered;
    WdfDeviceInitSetPowerPolicyEventCallbacks(dev_init, &ppc);
    WdfDeviceInitSetPowerPolicyOwnership(dev_init, TRUE);

    WdfDeviceInitSetPowerNotPageable(dev_init);

    WDF_FILEOBJECT_CONFIG fcfg;
    WDF_FILEOBJECT_CONFIG_INIT(&fcfg, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK,
                               WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(dev_init, &fcfg, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs, dev_ctx_t);
    attrs.EvtCleanupCallback = evt_dev_ctx_cleanup;
    attrs.ExecutionLevel = WdfExecutionLevelPassive;
    status = WdfDeviceCreate(&dev_init, &attrs, &dev);
    if (!NT_SUCCESS(status))
        return status;

    WdfDeviceSetStaticStopRemove(dev, FALSE);

    WDF_DEVICE_POWER_CAPABILITIES_INIT(&pwr_caps);
    pwr_caps.DeviceD1 = WdfTrue;
    pwr_caps.WakeFromD3 = WdfTrue;
    WdfDeviceSetPowerCapabilities(dev, &pwr_caps);

    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idle, IdleCannotWakeFromS0);
    idle.IdleTimeoutType = SystemManagedIdleTimeoutWithHint;
    (VOID) WdfDeviceAssignS0IdleSettings(dev, &idle);
    WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS_INIT(&wake);
    (VOID) WdfDeviceAssignSxWakeSettings(dev, &wake);

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->dev = dev;
    ctx->is_first_boot = TRUE;
    ctx->thermal_low_temp_deci_k = MAXLONG;
    ctx->thermal_high_temp_deci_k = MAXLONG;
    ctx->pre_power_action = PowerActionNone;
    ctx->rsc_drv2_assignment_status = STATUS_DEVICE_NOT_READY;
    ctx->rsc_drv2_assignment_observed = 0;
    ctx->rsc_drv2_pn_p_window_present = 0;
    rsc_irq_snapshot_init(&ctx->rsc_irq_snapshot);
    rpmh_oem_pnp_observation_init(&ctx->rpmh_oem_pnp_observation);
    rpmh_wdf_lifecycle_init(&ctx->rpmh_wdf_lifecycle);
    RtlZeroMemory(&ctx->rpmh_d0_lease, sizeof(ctx->rpmh_d0_lease));

    (VOID) resolve_port_id(dev, &ctx->port_id);
    (VOID) query_hardware_id(dev, &ctx->hid, 128);

    (VOID) ddi_reg_all(dev);

    status = create_control_dev(g.driver);
    if (!NT_SUCCESS(status))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: control endpoint creation failed status=%08X", status);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: added port=%lu", ctx->port_id);
    (VOID) forward_init_from_dev(dev);
    if (NT_SUCCESS(try_reg_ex_soc_dmf_target(dev, ctx->port_id))) {
        ctx->ex_soc_registered = TRUE;
        InterlockedIncrement(&g_stats.dev_attached);
        stats_record_event(STATS_EVENT_DEV_ATTACH, STATUS_SUCCESS, ctx->port_id, 1u,
                           (ULONG)g_stats.dev_attached, 0);
    }
    return STATUS_SUCCESS;
}

NTSTATUS evt_dev_prepare_hardware(_In_ WDFDEVICE dev, _In_ WDFCMRESLIST resources_raw,
                                  _In_ WDFCMRESLIST resources_translated)
{
    /* read-only observation of PnP-translated resources actually assigned to THIS PEP device.
     * 's checker was previously orphaned. The supplied PEP0._CRS has interrupts but NO RSC
     * drv2 MMIO window. */

    p_dev_ctx_t res_ctx = get_dev_ctx(dev);
    /* IRQ37 is a KMDF PnP child; it does not own the controller. */

    if (res_ctx && res_ctx->rpmh_observed_wdf_interrupt)
        return STATUS_INVALID_DEVICE_STATE;
    BOOLEAN has_dedicated_rsc_window = FALSE;
    NTSTATUS drv2_assignment =
        rsc_check_this_dev_translated_resources(resources_translated, &has_dedicated_rsc_window);
    if (res_ctx) {
        InterlockedExchange(&res_ctx->rsc_drv2_assignment_status, (LONG)drv2_assignment);
        InterlockedExchange(&res_ctx->rsc_drv2_pn_p_window_present,
                            (NT_SUCCESS(drv2_assignment) && has_dedicated_rsc_window) ? 1 : 0);
        InterlockedExchange(&res_ctx->rsc_drv2_assignment_observed, 1);
    }
    if (!NT_SUCCESS(drv2_assignment))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: DRV2 PnP census invalid status=%08X",
                    drv2_assignment);
    else if (has_dedicated_rsc_window)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH, "rpmh: DRV2 PnP window present ownership=0");
    else
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                    "rpmh: DRV2 PnP window absent direct-map-owner=pending");

    /* its connection ID does NOT identify the Qualcomm SPMI arbiter or grant PMIC ownership.
     * No validated SPMI provider exists yet; never publish an unverified global SPMI
     * connection ID. */

    g.spmi_connection_id = 0;
    ULONG cnt = WdfCmResourceListGetCount(resources_translated);
    ULONG serial_candidates = 0;
    /* correlate raw and translated resources by PnP desc idx. IRQ 37 is an static-controller
     * candidate, not an automatically bound Windows interrupt or ownership grant. */

    {
        rsc_irq_report_t observed = rsc_inspect_irq_resources(resources_raw, resources_translated);
        if (res_ctx) {
            /* pnp epoch-scoped candidate, NOT an RPMh hardware owner token. */
            rpmh_oem_pnp_observation_revoke(&res_ctx->rpmh_oem_pnp_observation);
            /* read-only, exact raw+translated PnP provenance for this FDO. Unlike the historical
             * memory+ANY-interrupt census, an IRQ must be the HLOS DRV2 raw INTID37 and the *same
             * desc idx* must resolve to a unique, exclusive, level-triggered Windows vector. */

            {
                rsc_evidence_report_t evidence;
                uint64_t generation = res_ctx->rpmh_oem_pnp_observation.generation;
                rsc_evidence_finding_t finding = rsc_audit_win_pnp_evidence(
                    resources_raw, resources_translated, generation, &evidence);
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                            "rpmh: PnP census finding=%u generation=%llu window=%u IRQ37=%u "
                            "pair=%lu vector=%lu level=%lu ownership=0",
                            (unsigned)finding, (unsigned long long)generation,
                            (unsigned)evidence.has_dedicated_window,
                            (unsigned)evidence.has_unique_irq, (ULONG)evidence.irq_idx,
                            (ULONG)evidence.win_vector, (ULONG)evidence.win_level);
            }

            /* a repeat PrepareHardware cannot inherit a previous IRQ lifecycle. */
            oem_d0_lease_revoke(&res_ctx->rpmh_d0_lease);
            rpmh_wdf_lifecycle_release(&res_ctx->rpmh_wdf_lifecycle);
            (VOID) rpmh_wdf_lifecycle_prepare(&res_ctx->rpmh_wdf_lifecycle,
                                              res_ctx->rpmh_oem_pnp_observation.generation);
            rsc_irq_snapshot_capture(&res_ctx->rsc_irq_snapshot, observed);
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR irq_raw = NULL, irq_translated = NULL;
                int descriptors_valid = rsc_select_irq_candidate_descriptors(
                    resources_raw, resources_translated, &res_ctx->rsc_irq_snapshot,
                    res_ctx->rsc_irq_snapshot.generation, &irq_raw, &irq_translated);
                /* call the previously orphaned Windows PnP sequence builder on the exact current
                 * raw+translated lists. The legacy owner_epoch field contains only this PnP
                 * generation, NEVER TCS rights. */

                rpmh_oem_pnp_req_t oem_req = {RPMH_HLOS_DRIVER_ID, RPMH_HLOS_INTID,
                                              res_ctx->rpmh_oem_pnp_observation.generation};
                rpmh_pnp_resources_t matched_resources = {0};
                NTSTATUS oem_status = rpmh_match_windows_interrupts(
                    resources_raw, resources_translated, &oem_req, 1u, &matched_resources);
                BOOLEAN oem_observed = FALSE;
                if (descriptors_valid && NT_SUCCESS(oem_status)) {
                    oem_observed =
                        rpmh_oem_pnp_observation_capture(
                            &res_ctx->rpmh_oem_pnp_observation, &matched_resources,
                            &res_ctx->rsc_irq_snapshot.report, res_ctx->rsc_irq_snapshot.generation)
                            ? TRUE
                            : FALSE;
                    if (oem_observed) {
                        (VOID) rpmh_wdf_lifecycle_observe(&res_ctx->rpmh_wdf_lifecycle,
                                                          &res_ctx->rpmh_oem_pnp_observation);
                        /* pnp correlation only. */

                        (VOID) oem_d0_lease_prepare(&res_ctx->rpmh_d0_lease,
                                                    &res_ctx->rpmh_oem_pnp_observation,
                                                    res_ctx->rpmh_oem_pnp_observation.generation);
                    }
                }
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                            "rpmh: PnP generation=%llu candidate=%u pair=%u match=%u status=%08lX "
                            "ownership=0",
                            (unsigned long long)res_ctx->rsc_irq_snapshot.generation,
                            (unsigned)res_ctx->rsc_irq_snapshot.unique_candidate,
                            (unsigned)descriptors_valid, (unsigned)oem_observed, (ULONG)oem_status);
            }
        }
    }
    for (ULONG i = 0; i < cnt; i++) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = WdfCmResourceListGetDescriptor(resources_translated, i);
        if (!d)
            continue;
        if (d->Type == CmResourceTypeConnection &&
            d->u.Connection.Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL) {
            ULONGLONG candidate = ((ULONGLONG)d->u.Connection.IdHighPart << 32) |
                                  (ULONGLONG)d->u.Connection.IdLowPart;
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE,
                        "device: serial connection=%llX type=%lu owner=unverified", candidate,
                        d->u.Connection.Type);
            ++serial_candidates;
        }
    }
    if (!serial_candidates)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: serial connection absent");
    else
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: serial connections=%lu SPMI-owner=unverified", serial_candidates);

    NTSTATUS status = STATUS_SUCCESS;
    /* create IRQ37 before D0 with epoch, worker, mask provider and tracker unbound. Enable remains
     * inactive until ownership is published. */

    if (res_ctx && res_ctx->rpmh_oem_pnp_observation.valid) {
        rpmh_pnp_resources_t matched_resources = {0};
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw = NULL, translated = NULL;
        const ULONGLONG generation = res_ctx->rpmh_oem_pnp_observation.generation;
        if (rpmh_oem_pnp_observation_read(&res_ctx->rpmh_oem_pnp_observation, generation,
                                          &matched_resources) &&
            matched_resources.cnt == 1u &&
            /* do not register an unbound KMDF ISR for a shared line that can never qualify for
             * exclusive GICD mask control. This check alone grants NO controller/AMC ownership. */

            rpmh_has_exclusive_irq37_resource(&matched_resources, generation) &&
            matched_resources.matches[0].driver_id == RPMH_HLOS_DRIVER_ID &&
            matched_resources.matches[0].gic_intid == RPMH_HLOS_INTID &&
            matched_resources.matches[0].res_idx < WdfCmResourceListGetCount(resources_raw) &&
            matched_resources.matches[0].res_idx <
                WdfCmResourceListGetCount(resources_translated)) {
            oem_wdf_irq_req_t req = {0};
            oem_wdf_irq_ctx_t* interrupt_ctx = NULL;
            void* handle = NULL;
            raw =
                WdfCmResourceListGetDescriptor(resources_raw, matched_resources.matches[0].res_idx);
            translated = WdfCmResourceListGetDescriptor(resources_translated,
                                                        matched_resources.matches[0].res_idx);
            if (raw && translated) {
                req.driver_id = RPMH_HLOS_DRIVER_ID;
                req.gic_intid = RPMH_HLOS_INTID;
                req.interrupt_raw = raw;
                req.interrupt_translated = translated;
                /* owner_epoch and amc_work_item intentionally zero: neither an authenticated owner
                 * nor an AMC work item exists. */

                if (oem_wdf_create_one_win(dev, &req, &handle, &interrupt_ctx) && handle &&
                    interrupt_ctx) {
                    interrupt_ctx->driver_id = req.driver_id;
                    interrupt_ctx->gic_intid = req.gic_intid;
                    res_ctx->rpmh_observed_wdf_interrupt = (WDFINTERRUPT)handle;
                    res_ctx->rpmh_observed_pnp_generation = generation;
                    TraceEvents(
                        TRACE_LEVEL_INFORMATION, TRACE_RPMH,
                        "rpmh: IRQ37 object created generation=%llu controller-owner=pending",
                        (unsigned long long)generation);
                } else {
                    if (handle && !oem_wdf_try_destroy_unpublished_win(dev, handle)) {
                        /* preserve a partially created WDF child if cleanup cannot prove it is
                         * quiescent. Never lose the handle. */

                        res_ctx->rpmh_observed_wdf_interrupt = (WDFINTERRUPT)handle;
                        res_ctx->rpmh_observed_pnp_generation = generation;
                        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                    "rpmh: IRQ37 creation rollback incomplete");
                        return STATUS_DEVICE_BUSY;
                    }
                    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                "rpmh: IRQ37 object creation failed ownership=0");
                    status = STATUS_INSUFFICIENT_RESOURCES;
                }
            }
        }
    }
    if (!NT_SUCCESS(status))
        return status;
    if (NT_SUCCESS(status))
        (VOID) chip_info_init_from_dev(dev);
    if (NT_SUCCESS(status))
        (VOID) tsens_init_from_dev(dev);
    if (NT_SUCCESS(status))
        mmrm_attach_dev(dev);
    if (NT_SUCCESS(status))
        status = po_fx_init(dev);
    if (!NT_SUCCESS(status)) {
        if (res_ctx && res_ctx->rpmh_observed_wdf_interrupt) {
            if (!oem_wdf_try_destroy_unpublished_win(dev, res_ctx->rpmh_observed_wdf_interrupt)) {
                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                            "rpmh: prepare rollback child-rundown=pending");
                return STATUS_DEVICE_BUSY;
            }
            res_ctx->rpmh_observed_wdf_interrupt = NULL;
            res_ctx->rpmh_observed_pnp_generation = 0;
        }
        if (res_ctx) {
            rsc_irq_snapshot_revoke(&res_ctx->rsc_irq_snapshot);
            rpmh_oem_pnp_observation_revoke(&res_ctx->rpmh_oem_pnp_observation);
            oem_d0_lease_revoke(&res_ctx->rpmh_d0_lease);
            rpmh_wdf_lifecycle_release(&res_ctx->rpmh_wdf_lifecycle);
        }
        return status;
    }

    /* register the Windows-owned hardware backend with the latch. Non-fatal: a missing peer
     * must not fail PoFx reg. */

    (VOID) hw_backend_win_attach(dev);

    status = bus_dcvs_prepare(dev, resources_raw, resources_translated);
    if (!NT_SUCCESS(status))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "bus: monitor initialization unavailable status=%08X", status);

    (VOID) wmi_init(dev);
    (VOID) interrupt_init(dev);
    (VOID) etw_reg();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: hardware prepared");
    (VOID) forward_init_from_dev(dev);
    return STATUS_SUCCESS;
}

NTSTATUS evt_dev_release_hardware(_In_ WDFDEVICE dev, _In_ WDFCMRESLIST resources_translated)
{
    UNREFERENCED_PARAMETER(resources_translated);
    p_dev_ctx_t ctx = get_dev_ctx(dev);

    NTSTATUS bus_status = bus_dcvs_release(dev);
    if (!NT_SUCCESS(bus_status))
        return bus_status;

    /* keep the DRV2 WDFINTERRUPT handle and d0 identity intact until the native owner has
     * physically retired its ISR/DPC/AMC work. */

    interrupt_deinit(dev);
    /* retain the d0 RSC identity and PnP generation while an unpublished/published IRQ child
     * or mapped controller can still be referenced by a failed teardown. A STATUS_DEVICE_BUSY
     * return must preserve recovery evidence, not zero it preemptively. */

    /* the PnP child remains inert; drain owner work before deleting it. */

    if (ctx && ctx->rpmh_observed_wdf_interrupt) {
        /* retain the bridge while an issued AMC ticket can still complete through its accepted
         * DPC/worker. */

        if (!oem_wdf_try_destroy_unpublished_win(dev, ctx->rpmh_observed_wdf_interrupt)) {

            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: hardware release rejected owner=active");
            return STATUS_DEVICE_BUSY;
        }
        ctx->rpmh_observed_wdf_interrupt = NULL;
        ctx->rpmh_observed_pnp_generation = 0;
    }

    if (!NT_SUCCESS(hw_backend_win_d0_unmap_rsc(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: hardware release pending RSC=retained");
        return STATUS_DEVICE_BUSY;
    }

    if (ctx) {
        RtlZeroMemory(&ctx->rpmh_rsc_identity, sizeof(ctx->rpmh_rsc_identity));
        ctx->rpmh_identity_pnp_generation = 0;
    }
    /* invalidate any prior raw/translated interrupt candidate before owner backend teardown,
     * and before a subsequent PnP restart. */

    if (ctx) {
        rsc_irq_snapshot_revoke(&ctx->rsc_irq_snapshot);
        rpmh_oem_pnp_observation_revoke(&ctx->rpmh_oem_pnp_observation);
        oem_d0_lease_revoke(&ctx->rpmh_d0_lease);
        rpmh_wdf_lifecycle_release(&ctx->rpmh_wdf_lifecycle);
    }
    /* no arbitrary PnP serial res is a verified SPMI owner. */
    g.spmi_connection_id = 0;
    /* the PnP assignment is no longer in force after ReleaseHardware. */

    if (ctx) {
        InterlockedExchange(&ctx->rsc_drv2_assignment_observed, 0);
        InterlockedExchange(&ctx->rsc_drv2_pn_p_window_present, 0);
        InterlockedExchange(&ctx->rsc_drv2_assignment_status, STATUS_DEVICE_NOT_READY);
    }

    hw_backend_win_detach(dev);

    po_fx_deinitialize(dev);
    mmrm_detach_dev(dev);
    pep_unregister(dev);
    etw_unregister();

    if (ctx && ctx->ex_soc_registered) {
        InterlockedDecrement(&g_stats.dev_attached);
        stats_record_event(STATS_EVENT_DEV_DETACH, STATUS_SUCCESS, ctx->port_id, 1u,
                           (ULONG)g_stats.dev_attached, 0);
        ctx->ex_soc_registered = FALSE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: hardware released");
    return STATUS_SUCCESS;
}

NTSTATUS evt_dev_self_managed_io_init(_In_ WDFDEVICE dev)
{
    NTSTATUS status;

    /* Expose the firmware LPI descriptions before this device completes its first start. */
    status = lpi_firmware_init(dev);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI, "acpi: _DSM initialization failed status=%08X",
                    status);
        if (registry_query_u_long(L"MolokaiAcpiStrictLpiInit", 0) != 0)
            return status;
    }

    /* stnd exposes the firmware's reversed four-character method catalog. */

    if (registry_query_u_long(L"MolokaiStndProbeEnable", 0) != 0) {
        CHAR methods[MOLOKAI_STND_METHOD_MAX * 5] = {0};
        ULONG method_cnt = 0;
        NTSTATUS ast =
            acpi_query_stnd_method_names(dev, methods, MOLOKAI_STND_METHOD_MAX, &method_cnt);
        if (!NT_SUCCESS(ast)) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI, "acpi: STND query failed status=%08X",
                        ast);
            if (registry_query_u_long(L"MolokaiStndStrict", 0) != 0)
                return ast;
        } else {
            for (ULONG i = 0; i < method_cnt; ++i)
                TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_ACPI, "acpi: STND index=%lu method=%c%c%c%c",
                            i, (methods + i * 5u)[0], (methods + i * 5u)[1], (methods + i * 5u)[2],
                            (methods + i * 5u)[3]);
        }
    }

    status = pep_warmup_caches();
    if (!NT_SUCCESS(status))
        return status;

    /* production init of the verified res graph (bind -> build -> seal -> NPA activate ->
     * publish). Returns STATUS_DEVICE_NOT_READY while the hardware-backend gate is closed,
     * i.e. on every build without an on-device KD attestation. */

    (VOID) hw_backend_win_try_publish(dev);

    status = pep_reg(dev);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: plugin registration failed status=%08X", status);
        return status;
    }

    mmrm_attach_dev(dev);

    status = deep_standby_attach_dev(dev);
    if (!NT_SUCCESS(status))
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, "device: standby attach failed status=%08X",
                    status);

    (VOID) forward_init_from_dev(dev);
    /* an unsuccessful StopIdle acquires no reference. */

    {
        p_dev_ctx_t ctx = get_dev_ctx(dev);
        if (ctx && ctx->is_first_boot && !ctx->is_control_dev &&
            !ctx->first_boot_idle_reference_held) {
            NTSTATUS idle_status = WdfDeviceStopIdle(dev, FALSE);
            TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                        "device: initial idle reference status=%08X", idle_status);
            if (NT_SUCCESS(idle_status)) {
                ctx->first_boot_idle_reference_held = TRUE;
                ctx->is_first_boot = FALSE;
            }
        }
    }
    bus_dcvs_start(dev);
    return STATUS_SUCCESS;
}

VOID evt_dev_self_managed_io_cleanup(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (ctx && ctx->first_boot_idle_reference_held) {
        ctx->first_boot_idle_reference_held = FALSE;
        WdfDeviceResumeIdle(dev);
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                    "device: initial idle reference released");
    }
}

VOID evt_dev_surprise_removal(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!NT_SUCCESS(bus_dcvs_stop(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "bus: surprise removal monitor owner retained");
        return;
    }
    /* stop the legacy interrupt path, but preserve the DRV2 worker and PnP identity if native
     * RSC/GIC controller rundown fails. Surprise removal cannot claim hardware votes were
     * completed. */

    /* close PoFx D/F submission before touching either interrupt lifetime; a busy PoFx lease
     * retains all owner state for later KMDF rundown. */

    if (!NT_SUCCESS(hw_backend_win_d0_quiesce(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "resources: surprise removal callbacks=active owner=retained");
        return;
    }
    interrupt_deinit(dev);
    /* surprise removal cannot report failure to KMDF, but a pending native completion still
     * pins the IRQ child and RSC/GIC map. */

    if (ctx && ctx->rpmh_observed_wdf_interrupt) {
        /* retain the bridge while an issued AMC ticket can still complete through its accepted
         * DPC/worker. */

        if (!oem_wdf_try_destroy_unpublished_win(dev, ctx->rpmh_observed_wdf_interrupt)) {
            /* keep borrowed owner storage and WDF handle; do not detach the backend while any
             * hardware ticket or callback may still reference it. */

            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                        "rpmh: surprise removal IRQ-owner=quarantined");
            return;
        }
        ctx->rpmh_observed_wdf_interrupt = NULL;
        ctx->rpmh_observed_pnp_generation = 0;
    }
    if (!NT_SUCCESS(hw_backend_win_d0_unmap_rsc(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: surprise removal worker=pending RSC=retained");
        return;
    }
    /* the controller and interrupt were safely retired; revoke the d0 physical identity before
     * the old PnP candidate is forgotten. */

    if (ctx) {
        RtlZeroMemory(&ctx->rpmh_rsc_identity, sizeof(ctx->rpmh_rsc_identity));
        ctx->rpmh_identity_pnp_generation = 0;
    }
    if (ctx) {
        rsc_irq_snapshot_revoke(&ctx->rsc_irq_snapshot);
        rpmh_oem_pnp_observation_revoke(&ctx->rpmh_oem_pnp_observation);
        oem_d0_lease_revoke(&ctx->rpmh_d0_lease);
        rpmh_wdf_lifecycle_release(&ctx->rpmh_wdf_lifecycle);
        InterlockedExchange(&ctx->rsc_drv2_assignment_observed, 0);
        InterlockedExchange(&ctx->rsc_drv2_pn_p_window_present, 0);
        InterlockedExchange(&ctx->rsc_drv2_assignment_status, STATUS_DEVICE_NOT_READY);
    }

    hw_backend_win_detach(dev);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                "device: surprise removal assignment=invalidated");
}

NTSTATUS evt_dev_d0_entry(_In_ WDFDEVICE dev, _In_ WDF_POWER_DEVICE_STATE previous_state)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    POWER_ACTION pre_action = ctx ? ctx->pre_power_action : PowerActionNone;
    /* track the exact PoFx handle activated by THIS d0 entry. */

    POHANDLE activated_po_handle = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    InterlockedIncrement(&g_stats.d0_entry);
    stats_record_event(STATS_EVENT_D0_ENTRY, status, ctx ? ctx->port_id : 0, (ULONG)previous_state,
                       (ULONG)g_stats.d0_entry, 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: D0 entry control=%u",
                ctx ? ctx->is_control_dev : 0);
    /* a preceding d0 exit/partial entry may have retained the native RSC or IRQ lifetime after
     * a failed hardware rundown. Its PnP generation is an active safety lease until D0Exit /
     * ReleaseHardware have positively retired it. */

    /* wdf must not issue a second D0Entry before the previous PoFx activation was balanced. A
     * stale active component would otherwise be overwritten by a new handle and never idled. */

    if (ctx && ctx->d0_activated_po_handle) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: D0 rejected reason=previous-PoFx-activation");
        return STATUS_DEVICE_BUSY;
    }
    if (ctx && !ctx->is_control_dev && ctx->rpmh_identity_pnp_generation) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: D0 rejected previous-generation=%llu",
                    (unsigned long long)ctx->rpmh_identity_pnp_generation);
        return STATUS_DEVICE_BUSY;
    }

    /* do not activate a PoFx component until these prerequisites have succeeded; otherwise
     * KMDF may skip D0Exit and leave a permanent active reference. */

    if (pre_action == PowerActionHibernate) {

        /* per-device hardware/dependency state before normal runtime resumes. */
        NTSTATUS st = chip_info_init_from_dev(dev);
        if (!NT_SUCCESS(st) && NT_SUCCESS(status))
            status = st;

        st = tsens_init_from_dev(dev);
        if (!NT_SUCCESS(st) && NT_SUCCESS(status))
            status = st;

        mmrm_attach_dev(dev);
        st = po_fx_init(dev);
        if (!NT_SUCCESS(st) && NT_SUCCESS(status))
            status = st;

        /* do not recreate a PnP-scoped WDF interrupt in D0Entry. An eventual owned RPMh interrupt
         * will use framework d0 enable/ disable callbacks and verified deferred-work rundown
         * instead. */

        if (NT_SUCCESS(status))
            pep_on_hibernate_resume();
    }
    if (!NT_SUCCESS(status)) {

        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE,
                    "device: hibernate dependency restore failed status=%08X", status);
        return status;
    }
    if (ctx && ctx->po_handle) {
        activated_po_handle = ctx->po_handle;
        PoFxActivateComponent(activated_po_handle, 0, 0);

        ctx->d0_activated_po_handle = activated_po_handle;
    }
    if (ctx)
        ctx->pre_power_action = PowerActionNone;
    if (ctx && NT_SUCCESS(status)) {
        /* the PnP-only lease is recreated per d0 generation. Not d0 ownership: no trusted hardware
         * attestation probe is installed. */

        if (ctx->rpmh_d0_lease.phase == D0_LEASE_CLOSED)
            (VOID) oem_d0_lease_prepare(&ctx->rpmh_d0_lease, &ctx->rpmh_oem_pnp_observation,
                                        ctx->rpmh_oem_pnp_observation.generation);
        uint32_t vector = 0, level = 0;
        int observed_d0 = rpmh_wdf_lifecycle_enter_d0(&ctx->rpmh_wdf_lifecycle);
        /* correlate D0 with the PnP vector/IRQL generation. A candidate does not establish WDF
         * interrupt or DRV2/TCS ownership. */

        int observed_irq =
            observed_d0 &&
            rpmh_wdf_lifecycle_read(&ctx->rpmh_wdf_lifecycle,
                                    ctx->rpmh_oem_pnp_observation.generation, &vector, &level);
        TraceEvents(
            TRACE_LEVEL_VERBOSE, TRACE_RPMH,
            "rpmh: D0 PnP phase=%u generation=%llu candidate=%u vector=%lu IRQL=%lu ownership=0",
            (unsigned)ctx->rpmh_wdf_lifecycle.phase,
            (unsigned long long)ctx->rpmh_wdf_lifecycle.pnp_generation, (unsigned)observed_irq,
            (ULONG)vector, (ULONG)level);
    }
    /* read controller geometry before interrupt admission without changing controller rights. */

    if (ctx) {
        RtlZeroMemory(&ctx->rpmh_rsc_identity, sizeof(ctx->rpmh_rsc_identity));
        ctx->rpmh_identity_pnp_generation = 0;
    }
    if (NT_SUCCESS(status)) {
        /* firmware assigns HLOS RSC outside PEP0._CRS. Read identity/geometry only in the current
         * D0/IRQ37 generation before EvtInterruptEnable. */

        if (ctx) {
            rpmh_pnp_resources_t matched_resources = {0};
            if (ctx->rpmh_observed_wdf_interrupt && ctx->rpmh_observed_pnp_generation &&
                ctx->rpmh_observed_pnp_generation == ctx->rpmh_oem_pnp_observation.generation &&
                ctx->rpmh_wdf_lifecycle.phase == RPMH_WDF_D0_OBSERVED &&
                rpmh_oem_pnp_observation_read(&ctx->rpmh_oem_pnp_observation,
                                              ctx->rpmh_observed_pnp_generation,
                                              &matched_resources) &&
                matched_resources.cnt == 1u &&
                matched_resources.matches[0].driver_id == RPMH_HLOS_DRIVER_ID &&
                matched_resources.matches[0].gic_intid == RPMH_HLOS_INTID &&
                KeGetCurrentIrql() == PASSIVE_LEVEL) {
                if (rsc_read_only_identify_hlos_win(&ctx->rpmh_rsc_identity)) {
                    ctx->rpmh_identity_pnp_generation = ctx->rpmh_observed_pnp_generation;
                    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                                "rpmh: DRV2 id=%08lX config=%08lX TCS=%lu slots=%lu "
                                "irq-enable=%08lX pending=%08lX observation-only=1",
                                (ULONG)ctx->rpmh_rsc_identity.rsc_id,
                                (ULONG)ctx->rpmh_rsc_identity.driver_config,
                                (ULONG)ctx->rpmh_rsc_identity.tcs_cnt,
                                (ULONG)ctx->rpmh_rsc_identity.commands_per_tcs,
                                (ULONG)ctx->rpmh_rsc_identity.irq_enable,
                                (ULONG)ctx->rpmh_rsc_identity.irq_status);
                    /* it is only a physical transport and grants NO TCS, IRQ or RPMh rights; 's
                     * delegates remain gated. */

                    {
                        /* the backend returns BUSY when its failed physical mapping rollback cannot
                         * prove a clean hardware lifetime. */

                        NTSTATUS map_status = hw_backend_win_d0_map_rsc(
                            dev, ctx->rpmh_identity_pnp_generation, &ctx->rpmh_rsc_identity);
                        if (NT_SUCCESS(map_status)) {
                            /* the stock firmware grants HLOS driver2 the fixed physical RSC
                             * channel; the original Qualcomm PEP direct-maps it rather than
                             * requiring a PEP0 _CRS MMIO desc. This d0-scoped startup additionally
                             * requires Windows-exclusive IRQ37, matching physical controller
                             * geometry and idle AMC slots. */

                            NTSTATUS owner_status =
                                hw_backend_win_d0_acquire_firmware_configured_rpmh(
                                    dev, ctx->rpmh_identity_pnp_generation);
                            /* read-only rejection installed no lease and wrote no GIC/TCS state.
                             * An uncertain unmap must retain the old generation as busy. */

                            if (owner_status == STATUS_DEVICE_NOT_READY ||
                                owner_status == STATUS_NOT_SUPPORTED) {
                                NTSTATUS unmap_status = hw_backend_win_d0_unmap_rsc(dev);
                                if (NT_SUCCESS(unmap_status)) {
                                    RtlZeroMemory(&ctx->rpmh_rsc_identity,
                                                  sizeof(ctx->rpmh_rsc_identity));
                                    ctx->rpmh_identity_pnp_generation = 0;
                                    oem_d0_lease_exit(&ctx->rpmh_d0_lease);
                                    rpmh_wdf_lifecycle_leave_d0(&ctx->rpmh_wdf_lifecycle);
                                    if (activated_po_handle) {
                                        PoFxIdleComponent(activated_po_handle, 0, 0);
                                        ctx->d0_activated_po_handle = NULL;
                                    }
                                    TraceEvents(
                                        TRACE_LEVEL_WARNING, TRACE_RPMH,
                                        "rpmh: D0 preflight rejected status=%08X mappings=released",
                                        owner_status);
                                    return owner_status;
                                }
                                if (activated_po_handle) {
                                    PoFxIdleComponent(activated_po_handle, 0, 0);
                                    ctx->d0_activated_po_handle = NULL;
                                }
                                TraceEvents(
                                    TRACE_LEVEL_WARNING, TRACE_RPMH,
                                    "rpmh: D0 preflight rejected status=%08X unmap=unverified",
                                    unmap_status);
                                return STATUS_DEVICE_BUSY;
                            }
                            if (owner_status != STATUS_NOT_SUPPORTED && !NT_SUCCESS(owner_status)) {
                                if (activated_po_handle) {
                                    PoFxIdleComponent(activated_po_handle, 0, 0);
                                    ctx->d0_activated_po_handle = NULL;
                                }
                                TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                            "rpmh: D0 owner publication failed status=%08X",
                                            owner_status);
                                return STATUS_DEVICE_BUSY;
                            }
                        }
                        if (!NT_SUCCESS(map_status)) {
                            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                        "rpmh: DRV2 map failed status=%08X", map_status);
                            if (map_status == STATUS_DEVICE_BUSY) {
                                /* this D0Entry activated PoFx above. KMDF may skip D0Exit after a
                                 * failed D0Entry; release only our unmatched activation, NOT the
                                 * physical mapping/IRQ evidence. */

                                if (activated_po_handle) {
                                    PoFxIdleComponent(activated_po_handle, 0, 0);
                                    ctx->d0_activated_po_handle = NULL;
                                }
                                return STATUS_DEVICE_BUSY;
                            }
                            ctx->rpmh_identity_pnp_generation = 0;
                            RtlZeroMemory(&ctx->rpmh_rsc_identity, sizeof(ctx->rpmh_rsc_identity));
                        }
                    }
                } else
                    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                                "rpmh: DRV2 map or geometry unavailable");
            }
        }
    }
    (VOID) forward_init_from_dev(dev);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                "device: D0 entry status=%08X IRQ=%p generation=%llu", status,
                ctx ? (PVOID)ctx->rpmh_observed_wdf_interrupt : NULL,
                (unsigned long long)(ctx ? ctx->rpmh_identity_pnp_generation : 0));
    return status;
}

NTSTATUS evt_dev_d0_entry_post_interrupts_enabled(_In_ WDFDEVICE dev,
                                                  _In_ WDF_POWER_DEVICE_STATE previous_state)
{
    UNREFERENCED_PARAMETER(previous_state);
    /* if this marker is absent after owner publication, KMDF aborted between D0Entry and
     * D0EntryPostInterruptsEnabled (usually interrupt enable). */

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "device: D0 interrupts enabled");

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (ctx && ctx->pre_power_action == PowerActionHibernate)
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE,
                    "device: hibernate D0 interrupts enabled");

    /* the old one-shot PrepareHardware probe ran before d0 RSC and GIC mappings; it could
     * never authorize RPMh or the DSDT graph. Revalidate from the current post-enable d0
     * callback ONLY after a complete native ISR/AMC/GIC owner is already bound. */

    /* kmdf/HAL already performed the physical connect. */

    if (ctx && ctx->rpmh_observed_wdf_interrupt &&
        !oem_wdf_framework_connected_win((void*)ctx->rpmh_observed_wdf_interrupt)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: IRQ37 connection unverified D0=rejected");
        return STATUS_DEVICE_NOT_READY;
    }
    if (NT_SUCCESS(hw_backend_win_d0_revalidate(dev))) {
        (VOID) hw_backend_win_try_publish(dev);
        bus_dcvs_start(dev);
    }
    return STATUS_SUCCESS;
}

NTSTATUS evt_dev_d0_exit(_In_ WDFDEVICE dev, _In_ WDF_POWER_DEVICE_STATE target_state)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    POWER_ACTION action = WdfDeviceGetSystemPowerAction(dev);
    NTSTATUS status = STATUS_SUCCESS;
    BOOLEAN reinit_forward = TRUE;
    InterlockedIncrement(&g_stats.d0_exit);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: D0 exit control=%u",
                ctx ? ctx->is_control_dev : 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE,
                "device: D0 exit target=%lu action=%lu IRQ=%p generation=%llu activation=%p",
                (ULONG)target_state, (ULONG)action,
                ctx ? (PVOID)ctx->rpmh_observed_wdf_interrupt : NULL,
                (unsigned long long)(ctx ? ctx->rpmh_identity_pnp_generation : 0),
                ctx ? (PVOID)ctx->d0_activated_po_handle : NULL);
    /* after WDF EvtInterruptDisable: a native map may be released only after the complete
     * scheduler/AMC/IRQ work rundown. */

    /* retire published WDF/AMC state after EvtInterruptDisable and before unmapping either
     * controller. Flush workers without holding lifecycle locks. */

    /* isr/DPC-only WDF config: after KMDF disconnected IRQ37, prove that the physical GIC is
     * masked and all native AMC slots are idle. Preserve the owner if either proof fails;
     * never synthesize a mask. */

    if (ctx && ctx->rpmh_observed_wdf_interrupt &&
        !oem_wdf_framework_disconnected_win((void*)ctx->rpmh_observed_wdf_interrupt)) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: IRQ37 disconnect unverified owner=retained");
        return STATUS_DEVICE_BUSY;
    }
    if (!NT_SUCCESS(hw_backend_win_d0_retire_published_owner(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: D0 exit owner=active mappings=retained");
        return STATUS_DEVICE_BUSY;
    }
    if (!NT_SUCCESS(hw_backend_win_d0_unmap_rsc(dev))) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH, "rpmh: D0 exit RSC=retained");
        return STATUS_DEVICE_BUSY;
    }
    if (ctx) {
        RtlZeroMemory(&ctx->rpmh_rsc_identity, sizeof(ctx->rpmh_rsc_identity));
        ctx->rpmh_identity_pnp_generation = 0;
        oem_d0_lease_exit(&ctx->rpmh_d0_lease);
        rpmh_wdf_lifecycle_leave_d0(&ctx->rpmh_wdf_lifecycle);
    }

    if (ctx && ctx->d0_activated_po_handle) {
        POHANDLE active = ctx->d0_activated_po_handle;
        PoFxIdleComponent(active, 0, 0);
        ctx->d0_activated_po_handle = NULL;
    }
    if (action == PowerActionHibernate) {
        status = pep_on_hibernate_enter();
        reinit_forward = FALSE;
    } else if (action > PowerActionHibernate) {
        status = pep_on_power_down_enter();
        reinit_forward = FALSE;
    }
    stats_record_event(STATS_EVENT_D0_EXIT, status, ctx ? ctx->port_id : 0, (ULONG)target_state,
                       (ULONG)g_stats.d0_exit, 0);
    if (ctx)
        ctx->pre_power_action =
            (action == PowerActionHibernate) ? PowerActionHibernate : PowerActionNone;
    if (reinit_forward)
        (VOID) forward_init_from_dev(dev);
    return status;
}

NTSTATUS evt_dev_d0_exit_pre_interrupts_disabled(_In_ WDFDEVICE dev,
                                                 _In_ WDF_POWER_DEVICE_STATE target_state)
{
    UNREFERENCED_PARAMETER(target_state);

    /* a pending PoFx D/F transition holds a backend rundown lease; failing this callback
     * preserves both controller mappings and the WDF owner rather than unmapping memory that
     * the transition can still touch. */

    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: D0 interrupts disabling");
    {
        NTSTATUS bus_status = bus_dcvs_stop(dev);
        if (!NT_SUCCESS(bus_status)) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "bus: monitor shutdown failed status=%08X", bus_status);
            return bus_status;
        }
    }
    {
        NTSTATUS perf_status = pep_native_perf_quiesce_gate();
        if (!NT_SUCCESS(perf_status)) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                        "resources: performance owner active during shutdown status=%08X",
                        perf_status);
            return perf_status;
        }
    }
    {
        NTSTATUS quiesce_status = hw_backend_win_d0_quiesce(dev);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: D0 quiesce status=%08X",
                    quiesce_status);
        if (!NT_SUCCESS(quiesce_status))
            return quiesce_status;
    }
    /* do not retire the PnP/WDF d0 observation while KMDF has yet to invoke
     * EvtInterruptDisable. D0Exit completes the lifecycle AFTER physical mask, DPC/work drain
     * and native map retirement. */

    if (WdfDeviceGetSystemPowerAction(dev) == PowerActionHibernate)
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: hibernate interrupts disabling");

    return STATUS_SUCCESS;
}

VOID evt_io_stop(_In_ WDFQUEUE queue, _In_ WDFREQUEST req, _In_ ULONG action_flags)
{
    UNREFERENCED_PARAMETER(queue);

    if ((action_flags & 0x10000000u) != 0u) {
        (VOID) WdfRequestUnmarkCancelable(req);
    } else if ((action_flags & WdfRequestStopActionSuspend) != 0u) {
        WdfRequestStopAcknowledge(req, FALSE);
    } else if ((action_flags & WdfRequestStopActionPurge) != 0u) {
        (VOID) WdfRequestCancelSentRequest(req);
    }
}

NTSTATUS evt_dev_arm_wake_from_sx(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: wake armed control=%u",
                ctx ? ctx->is_control_dev : 0);
    (VOID) forward_init_from_dev(dev);
    return STATUS_SUCCESS;
}

VOID evt_dev_disarm_wake_from_sx(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: wake disarmed control=%u",
                ctx ? ctx->is_control_dev : 0);
}

VOID evt_dev_wake_from_sx_triggered(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx = get_dev_ctx(dev);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: wake triggered control=%u",
                ctx ? ctx->is_control_dev : 0);
}

VOID evt_file_create(_In_ WDFDEVICE dev, _In_ WDFREQUEST req, _In_ WDFFILEOBJECT file_object)
{
    UNREFERENCED_PARAMETER(dev);
    UNREFERENCED_PARAMETER(file_object);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: file opened");
    WdfRequestComplete(req, STATUS_SUCCESS);
}

VOID evt_file_close(_In_ WDFFILEOBJECT file_object)
{
    crash_dump_on_file_close(WdfFileObjectWdmGetFileObject(file_object));
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_DEVICE, "device: file closed");
}
