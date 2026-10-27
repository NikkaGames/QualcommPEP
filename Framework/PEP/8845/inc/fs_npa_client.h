/* footswitch native-NPA action transport. Qualcomm 8280 AdjustAFootswitchResource
 * dispatches actions 1/2 to one enable client and 3/4 to a separate hardware-control
 * enable client. */

#ifndef FOOTSWITCH_NPA_H
#define FOOTSWITCH_NPA_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FS_NAME_BYTES 64u

typedef enum fs_action {
    FS_ENABLE = 1,
    FS_DISABLE = 2,
    FS_ENABLE_HW_CTL = 3,
    FS_DISABLE_HW_CTL = 4,
    FS_MARK_SUPPRESSIBLE = 5,
    FS_MARK_INSUPPRESSIBLE = 6
} fs_action_t;

typedef enum fs_result {
    FS_OK = 0,
    FS_INVALID,
    FS_UNSUPPORTED,
    FS_OWNERSHIP_LOST,
    FS_READBACK_MISMATCH,
    FS_PROVIDER_FAILURE,
    FS_QUARANTINED
} fs_result_t;

typedef struct fs_native_io {
    void* ctx;
    /* functions must be serialized externally against owner removal and *other* users of these
     * exact NPA client handles. Reads must reflect actual owner state, not copies of the
     * adapter's local expectations. */

    int (*owns_res)(void* ctx, const char* name);
    int (*read_owner_epoch)(void* ctx, const char* name, uint64_t* epoch);
    int (*read_native_client)(void* ctx, const char* name, uint64_t handle, uint32_t* present,
                              uint32_t* scalar);
    int (*issue_native_client)(void* ctx, const char* name, uint64_t handle, uint32_t scalar);
} fs_native_io_t;

typedef struct fs_res {
    fs_native_io_t io;
    char name[FS_NAME_BYTES];
    uint64_t owner_epoch;
    uint64_t enable_client;
    uint64_t hw_ctl_client;
    uint32_t normal_action;
    uint32_t hw_ctl_action;
    uint8_t bound;
    uint8_t quarantined;
} fs_res_t;

/* bind externally-created handles; refuse already-voting clients, missing owner identity,
 * invalid tokens or an epoch change during readback. The owner manages handle
 * creation/destruction, never this adapter. */

fs_result_t fs_bind(fs_res_t* res, const char* name, const fs_native_io_t* io,
                    uint64_t enable_client, uint64_t hw_ctl_client);

/* issue the scalar action, then read the individual native client back. on an ambiguous
 * submit/readback failure, QUARANTINE: do not retry or undo while controller outcome is
 * unknown. */

fs_result_t fs_issue(fs_res_t* res, uint32_t action);
/* the caller selects the normal (1/2) or HW-control (3/4) client family. */

fs_result_t fs_complete(fs_res_t* res, uint32_t action_family,
                        int (*complete)(void* ctx, const char* name, uint64_t handle));

/* read both handles and check epoch and expected client requests without issuing anything.
 * This is per-client proof, not proof of physical GDSC status: peer/firmware votes can
 * influence the aggregate power domain. */

fs_result_t fs_verify(fs_res_t* res);

/* does NOT cancel votes or free the externally owned native client handles. After
 * uncertain I/O, keep owner lifetime quarantined until external rundown/recovery
 * establishes safety. */

fs_result_t fs_unbind(fs_res_t* res);

#ifdef __cplusplus
}
#endif
#endif
