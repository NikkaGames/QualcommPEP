/* oem-derived FOOTSWITCH NPA client factory boundary. 8280 Aggr_AddFsResource /
 * CreateFsResourceNPAHandles use the res names "/fs/<domain>/enable" and
 * "/fs/<domain>/hwctrlenable", a PepClockGetDomainId lookup, and native NPA client
 * creation/recreation. */

#ifndef FOOTSWITCH_FACTORY_H
#define FOOTSWITCH_FACTORY_H
#include "fs_npa_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_FACTORY_NAME_BYTES 64u
/* these are native NPA client TYPE values, not footswitch action scalars. */

#define FS_NPA_REQUIRED_CLIENT_TYPE 0x40u

/* the provider must guarantee that a failed open_idle_client creates NO client and holds
 * NO vote. A successful Open must return a UNIQUE, independently inspectable, idle native
 * client, not an invented cookie. */

typedef struct fs_factory_provider {
    void* ctx;
    int (*owns_domain)(void*, const char* domain);
    int (*read_owner_epoch)(void*, const char* domain, uint64_t* epoch);
    int (*resolve_domain_id)(void*, const char* domain, uint32_t* id);
    /* the native client type is explicit: passes NPA_CLIENT_REQUIRED (0x40) in BOTH FOOTSWITCH
     * client families. An implementation must reject unsupported types rather than silently
     * creating suppressible or other clients. */

    int (*open_idle_client)(void*, const char* res_name, const char* client_name,
                            uint32_t client_type, uint64_t* native_handle);
    int (*read_client)(void*, const char* res_name, uint64_t native_handle, uint32_t* present,
                       uint32_t* scalar);
    int (*issue_client)(void*, const char* res_name, uint64_t native_handle, uint32_t scalar);
    /* optional physical aggregate-aware npa_complete_request equivalent. */
    int (*complete_client)(void*, const char* res_name, uint64_t native_handle);
    int (*close_idle_client)(void*, const char* res_name, uint64_t native_handle);
    int (*client_gone)(void*, const char* res_name, uint64_t native_handle);
    /* optional, read-only preflight under the same owner lock as complete. */
    int (*can_complete_client)(void*, const char* res_name, uint64_t native_handle);
    /* whole-res shutdown, not npa_complete_request: requires a prior verified OFF vote, no
     * remaining contributors, external admission closure and rundown. */
    int (*can_retire_off_client)(void*, const char* res_name, uint64_t native_handle);
    int (*retire_off_client)(void*, const char* res_name, uint64_t native_handle);
} fs_factory_provider_t;

typedef struct fs_factory {
    fs_factory_provider_t provider;
    fs_res_t native;
    char domain[FS_NAME_BYTES];
    char normal_res[FS_FACTORY_NAME_BYTES];
    char hw_res[FS_FACTORY_NAME_BYTES];
    uint64_t normal_handle;
    uint64_t hw_handle;
    uint64_t epoch;
    uint32_t domain_id;
    uint8_t normal_open;
    uint8_t hw_open;
    uint8_t bound;
    uint8_t quarantined;
} fs_factory_t;

/* client_name is owner-selected, never fabricated from a device path. on an ambiguous
 * partial creation/cleanup, returns FS_PROVIDER_FAILURE or FS_OWNERSHIP_LOST, sets
 * quarantined and retains any potentially-live handles for external owner rundown. */

fs_result_t fs_factory_open(fs_factory_t* factory, const char* domain, const char* client_name,
                            int need_hw, const fs_factory_provider_t* provider);

/* open(need_hw) is retained for existing multi-client callers. */

fs_result_t fs_factory_open_for_action(fs_factory_t* factory, const char* domain,
                                       const char* client_name, uint32_t action,
                                       const fs_factory_provider_t* provider);

fs_result_t fs_factory_issue(fs_factory_t* factory, uint32_t action);
fs_result_t fs_factory_verify(fs_factory_t* factory);
fs_result_t fs_factory_complete(fs_factory_t* factory, uint32_t action_family);
fs_result_t fs_factory_retire_off(fs_factory_t* factory, uint32_t action_family);

/* close refuses any previously submitted 1/2/3/4 request; a verified external NPA owner
 * must retire it under its own proven contract first. A close failure preserves native
 * identities for external owner recovery. */

fs_result_t fs_factory_close(fs_factory_t* factory);

#ifdef __cplusplus
}
#endif
#endif
