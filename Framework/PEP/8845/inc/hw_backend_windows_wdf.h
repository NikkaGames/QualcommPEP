#ifndef HW_BACKEND_WIN_WDF_H
#define HW_BACKEND_WIN_WDF_H
/* / kernel-only entry points of the Windows-owned hardware backend
 * (src/molokai/hw_backend_windows8845.c). Never include in host tests. */

#include "driver.h"
#include "res_graph.h"
#include "bus.h"
#include "gic_irq37_owned_win.h"
#include "gcc.h"

/* publication additionally checks that dev is the current owner PEP FDO (not a control
 * device or a foreign FDO), even on the graph fast path. */

/* driverentry only: prepare owner lifecycle lock before WdfDriverCreate. */
VOID hw_backend_win_lifecycle_init(VOID);
/* call only after verified WDF ISR, completion-work ownership and RSC ledger ownership.
 * Promotes the existing production scheduler in place, retaining
 * BUSARB/PMIC/READBACK/PROBE clients. */

NTSTATUS hw_backend_win_bind_deferred_completion(_In_ WDFDEVICE dev, _In_ ULONGLONG owner_epoch,
                                                 _In_ ULONG ledger_epoch);
/* physically prepare the exact AMC subset granted by an independently established native
 * Windows DRV2+IRQ37 owner. It does not acquire or fabricate those rights: the external
 * owner must prove the precise slots, controller identity and IRQ generation via a
 * nonblocking callback. */

NTSTATUS hw_backend_win_prepare_granted_amcs(
    _In_ WDFDEVICE dev, _In_ ULONGLONG pnp_generation, _In_ ULONGLONG owner_epoch,
    _In_ ULONG ledger_epoch, _In_ UCHAR granted_amcs, _In_ void* proof_ctx,
    _In_ int (*exclusive_amc_owner)(void*, uint64_t, uint64_t, void*, uint8_t));

/* roll back prepared TCS IRQ-enable and ledger ONLY before KMDF owner publication, with
 * the original independent grant still held. Once WDF has an owner or a deferred request
 * exists, the full native ISR/AMC teardown is required instead. */

NTSTATUS hw_backend_win_abort_unpublished_granted_amcs(
    _In_ WDFDEVICE dev, _In_ ULONGLONG pnp_generation, _In_ ULONGLONG owner_epoch,
    _In_ ULONG ledger_epoch, _In_ UCHAR granted_amcs, _In_ void* proof_ctx,
    _In_ int (*exclusive_amc_owner)(void*, uint64_t, uint64_t, void*, uint8_t));

/* requires exclusive RSC/GICD/IRQ37 ownership before binding the physical mask callback. */

NTSTATUS
hw_backend_win_prepare_gic_irq37_mask(WDFDEVICE dev, ULONGLONG pnp_generation,
                                      ULONGLONG owner_epoch, void* wdf_interrupt, void* proof_ctx,
                                      int (*exclusive_owner)(void*, uint64_t, void*, uint64_t),
                                      oem_irq_mask_ops_t* ops);
NTSTATUS hw_backend_win_retire_gic_irq37_mask(WDFDEVICE dev, ULONGLONG owner_epoch,
                                              void* wdf_interrupt, int owner_drained);
/* hlos DRV2 direct-MMIO d0 acquisition from the Molokai firmware assignment, with the
 * Windows PEP0-exclusive IRQ37 and current physical hardware geometry/readback. */

NTSTATUS hw_backend_win_d0_acquire_firmware_configured_rpmh(_In_ WDFDEVICE dev,
                                                            _In_ ULONGLONG pnp_generation);
NTSTATUS hw_backend_win_d0_map_rsc(_In_ WDFDEVICE dev, _In_ ULONGLONG pnp_generation,
                                   _In_ const rsc_identity_t* identity);
/* re-run physical verification only after the current KMDF d0 IRQ enable. Missing native
 * AMC/IRQ ownership is a normal fail-closed result. */

NTSTATUS hw_backend_win_d0_revalidate(_In_ WDFDEVICE dev);
/* descriptor-derived P-state replacement is serialized. Sets 1/2 use clock/ICB/PMIC providers; set
 * 0 uses FOOTSWITCH required clients. */
/* native required-client request handoff for UFS PSTATE_SET 0. Both f0 and d0 must be
 * independently verified before admission. */
NTSTATUS hw_backend_win_ufs_fs_transition(_In_z_ const char* path, _In_ ULONG comp,
                                          _In_ ULONG state, _In_ const power_dev_t* verified_d0,
                                          _In_ const power_dev_t* verified_f0);
/* exact desc set dispatch: set 0 is native FS; 1/2 are grouped. */
NTSTATUS hw_backend_win_ufs_p_state_transition(_In_z_ const char* path, _In_ ULONG comp,
                                               _In_ ULONG set, _In_ ULONG state,
                                               _In_ const power_dev_t* verified_d0,
                                               _In_ const power_dev_t* verified_f0);
NTSTATUS hw_backend_win_ufs_scalar_transition(_In_z_ const char* path, _In_ ULONG comp,
                                              _In_ ULONG set, _In_ ULONG state,
                                              _In_ const power_dev_t* verified_d0,
                                              _In_ const power_dev_t* verified_f0);
int hw_backend_win_ufs_owner_ready(const res_graph_t* graph, uint64_t owner_epoch);
/* execute/verify a complete desc-owned UFS native F/P program. */
power_result_t hw_backend_win_ufs_program_execute(const power_plan_t* program, uint64_t owner_epoch,
                                                  const power_dev_t* verified_d0,
                                                  const power_dev_t* current_f_state,
                                                  power_execution_t* trace);
int hw_backend_win_ufs_program_verify(const power_plan_t* program, uint64_t owner_epoch);
NTSTATUS hw_backend_win_d0_quiesce(_In_ WDFDEVICE dev);
/* retire after KMDF disable. Return busy while any AMC, IRQ state or WDF callback remains
 * unresolved. */

NTSTATUS hw_backend_win_d0_retire_published_owner(_In_ WDFDEVICE dev);
NTSTATUS hw_backend_win_d0_unmap_rsc(_In_ WDFDEVICE dev);
NTSTATUS hw_backend_win_attach(_In_ WDFDEVICE dev);
VOID hw_backend_win_detach(_In_ WDFDEVICE dev);
NTSTATUS hw_backend_win_try_publish(_In_ WDFDEVICE dev);

/* status_DEVICE_NOT_READY while the bus owner is not live, STATUS_NOT_FOUND for an unknown
 * route, STATUS_BUFFER_TOO_SMALL when Capacity < the route's command count. */

NTSTATUS hw_backend_win_bcm_command_cnt(_In_z_ const char* master, _In_z_ const char* slave,
                                        _Out_ ULONG* cnt);
NTSTATUS hw_backend_win_bcm_commands(_In_z_ const char* master, _In_z_ const char* slave,
                                     _In_ ULONGLONG ib, _In_ ULONGLONG ab,
                                     _Out_writes_to_(capacity, *cnt) icb_command_t* out,
                                     _In_ ULONG capacity, _Out_ ULONG* cnt);

int hw_backend_win_clock_read(uint64_t serial, const char* name, uint64_t* out);
int hw_backend_win_clock_dfs_catalog(uint64_t serial, const char* name, gcc_dfs_catalog_t* out);

#endif
