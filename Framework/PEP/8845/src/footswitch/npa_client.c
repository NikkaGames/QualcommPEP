#include "fs_npa_client.h"
#include <string.h>

/* never dereference a caller-provided malformed or unterminated name. */
static int valid_name(const char* name)
{
    uint32_t i;
    if (!name)
        return 0;
    for (i = 0; i < FS_NAME_BYTES; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c)
            return i > 0u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 0;
}

static int io_complete(const fs_native_io_t* io)
{
    return io && io->owns_res && io->read_owner_epoch && io->read_native_client &&
           io->issue_native_client;
}

static int epoch_valid(const fs_res_t* r)
{
    uint64_t epoch = 0u;
    return r->owner_epoch && r->io.owns_res(r->io.ctx, r->name) &&
           r->io.read_owner_epoch(r->io.ctx, r->name, &epoch) && epoch == r->owner_epoch &&
           r->io.owns_res(r->io.ctx, r->name);
}

static int native_matches(const fs_res_t* r, uint64_t handle, uint32_t expected)
{
    uint32_t present = ~0u, action = ~0u;
    if (!handle)
        return expected == 0u;
    if (!r->io.read_native_client(r->io.ctx, r->name, handle, &present, &action))
        return 0;
    return expected ? (present == 1u && action == expected) : (present == 0u);
}

fs_result_t fs_bind(fs_res_t* r, const char* name, const fs_native_io_t* io, uint64_t normal,
                    uint64_t hwctl)
{
    fs_res_t candidate;
    uint64_t epoch = 0u;
    if (!r || r->bound || r->quarantined || !valid_name(name) || !io_complete(io) ||
        (!normal && !hwctl) || (normal && normal == hwctl) || !io->owns_res(io->ctx, name) ||
        !io->read_owner_epoch(io->ctx, name, &epoch) || !epoch)
        return FS_INVALID;
    memset(&candidate, 0, sizeof(candidate));
    candidate.io = *io;
    memcpy(candidate.name, name, strlen(name) + 1u);
    candidate.owner_epoch = epoch;
    candidate.enable_client = normal;
    candidate.hw_ctl_client = hwctl;
    if (!epoch_valid(&candidate) || !native_matches(&candidate, normal, 0u) ||
        !native_matches(&candidate, hwctl, 0u) || !epoch_valid(&candidate))
        return FS_OWNERSHIP_LOST;
    candidate.bound = 1u;
    *r = candidate;
    return FS_OK;
}

fs_result_t fs_verify(fs_res_t* r)
{
    if (!r || !r->bound)
        return FS_INVALID;
    if (r->quarantined)
        return FS_QUARANTINED;
    if (!epoch_valid(r)) {
        r->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    if (!native_matches(r, r->enable_client, r->normal_action) ||
        !native_matches(r, r->hw_ctl_client, r->hw_ctl_action)) {
        r->quarantined = 1u;
        return FS_READBACK_MISMATCH;
    }
    if (!epoch_valid(r)) {
        r->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    return FS_OK;
}

fs_result_t fs_issue(fs_res_t* r, uint32_t action)
{
    uint64_t token;
    uint32_t* cached;
    fs_result_t status;
    if (!r || !r->bound)
        return FS_INVALID;
    if (r->quarantined)
        return FS_QUARANTINED;
    /* the required NPA context is unavailable; reject domain programming. */

    if (action == FS_MARK_SUPPRESSIBLE || action == FS_MARK_INSUPPRESSIBLE)
        return FS_UNSUPPORTED;
    if (action == FS_ENABLE || action == FS_DISABLE) {
        token = r->enable_client;
        cached = &r->normal_action;
    } else if (action == FS_ENABLE_HW_CTL || action == FS_DISABLE_HW_CTL) {
        token = r->hw_ctl_client;
        cached = &r->hw_ctl_action;
    } else
        return FS_INVALID;
    if (!token)
        return FS_UNSUPPORTED;
    status = fs_verify(r);
    if (status != FS_OK)
        return status;
    /* the node owns duplicate suppression and post-completion reissue rules. */
    if (!epoch_valid(r)) {
        r->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    if (!r->io.issue_native_client(r->io.ctx, r->name, token, action)) {
        /* a failed IOCTL/NPA issue may already have reached the controller. */

        r->quarantined = 1u;
        return FS_PROVIDER_FAILURE;
    }
    if (!native_matches(r, token, action) || !epoch_valid(r)) {
        r->quarantined = 1u;
        return FS_READBACK_MISMATCH;
    }
    *cached = action;
    return FS_OK;
}

fs_result_t fs_complete(fs_res_t* r, uint32_t family, int (*complete)(void*, const char*, uint64_t))
{
    uint64_t token;
    uint32_t* state;
    fs_result_t status;
    if (!r || !r->bound)
        return FS_INVALID;
    if (r->quarantined)
        return FS_QUARANTINED;
    if (family == 1u) {
        token = r->enable_client;
        state = &r->normal_action;
    } else if (family == 3u) {
        token = r->hw_ctl_client;
        state = &r->hw_ctl_action;
    } else
        return FS_INVALID;
    if (!token)
        return FS_UNSUPPORTED;
    status = fs_verify(r);
    if (status != FS_OK)
        return status;
    if (!*state)
        return FS_OK; /* already idle, no hardware operation. */
    if (!complete)
        return FS_UNSUPPORTED;
    if (!epoch_valid(r)) {
        r->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    if (!complete(r->io.ctx, r->name, token) || !native_matches(r, token, 0u) || !epoch_valid(r)) {
        /* an NPA terminate may have reached the physical domain even when the provider loses
         * readback; do not replay it speculatively. */

        r->quarantined = 1u;
        return FS_PROVIDER_FAILURE;
    }
    *state = 0u;
    return FS_OK;
}

fs_result_t fs_unbind(fs_res_t* r)
{
    fs_result_t status;
    if (!r || !r->bound)
        return FS_INVALID;
    status = fs_verify(r);
    if (status != FS_OK)
        return status;
    /* destroying a native client with an outstanding vote can implicitly change the aggregated
     * domain state. */

    if (r->normal_action || r->hw_ctl_action)
        return FS_UNSUPPORTED;
    memset(r, 0, sizeof(*r));
    return FS_OK;
}
