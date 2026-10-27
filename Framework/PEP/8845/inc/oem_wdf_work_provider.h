#ifndef OEM_WDF_WORK_PROVIDER_H
#define OEM_WDF_WORK_PROVIDER_H
/* a provider is per WDFDEVICE; it does NOT authorize RPMh ownership or attach any hardware
 * interrupts. Init/create/wait/destroy: PASSIVE_LEVEL with externally serialized PnP. */

#include "oem_rpmh_workitems.h"
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_WDF_WORK_PENDING_MAX 1024u
#define OEM_WDF_WORK_PASSES_MAX 1024u

typedef struct oem_wdf_work_provider {
    void* parent_dev; /* wdfdevice; owned by PnP, must outlive provider. */
} oem_wdf_work_provider_t;

int oem_wdf_work_provider_init_win(oem_wdf_work_provider_t*, void* parent_dev);
/* caller guarantees no future enqueue until a controller fence is released. */

int oem_wdf_work_flush_reusable_win(void* provider, void* handle);
oem_rpmh_work_ops_t oem_wdf_work_provider_ops_win(oem_wdf_work_provider_t*);
#ifdef __cplusplus
}
#endif
#endif
