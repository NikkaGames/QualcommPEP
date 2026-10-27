#include "factory.h"
#include <string.h>

static int valid_part(const char* s, size_t capacity)
{
    size_t i;
    if (!s || !capacity)
        return 0;
    for (i = 0u; i < capacity; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c)
            return i != 0u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }
    return 0;
}

static int append_name(char* out, size_t bytes, const char* domain, const char* suffix)
{
    size_t d, s, p;
    static const char prefix[] = "/fs/";
    if (!out || !domain || !suffix || !valid_part(domain, FS_NAME_BYTES))
        return 0;
    d = strlen(domain);
    s = strlen(suffix);
    p = sizeof(prefix) - 1u;
    if (p + d + s >= bytes)
        return 0;
    memcpy(out, prefix, p);
    memcpy(out + p, domain, d);
    memcpy(out + p + d, suffix, s + 1u);
    return 1;
}

static int callbacks_ready(const fs_factory_provider_t* p)
{
    return p && p->owns_domain && p->read_owner_epoch && p->resolve_domain_id &&
           p->open_idle_client && p->read_client && p->issue_client && p->close_idle_client &&
           p->client_gone;
}

static int same_epoch(const fs_factory_t* f, const char* domain)
{
    uint64_t actual = 0u;
    return f->epoch && f->provider.owns_domain(f->provider.ctx, domain) &&
           f->provider.read_owner_epoch(f->provider.ctx, domain, &actual) && actual == f->epoch &&
           f->provider.owns_domain(f->provider.ctx, domain);
}

static const char* by_handle(const fs_factory_t* f, uint64_t handle)
{
    if (f->normal_open && handle == f->normal_handle)
        return f->normal_res;
    if (f->hw_open && handle == f->hw_handle)
        return f->hw_res;
    return NULL;
}

/* callbacks see the owner domain, while the actual native NPA transport is constrained to
 * the exact decorator + domain client name. Never forward an unknown token or a name
 * belonging to another domain. */

static int fs_owns(void* ctx, const char* domain)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    return f && !f->quarantined && !strcmp(f->native.name, domain) && same_epoch(f, domain);
}

static int fs_epoch(void* ctx, const char* domain, uint64_t* epoch)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    if (!f || !epoch || !fs_owns(ctx, domain))
        return 0;
    *epoch = f->epoch;
    return 1;
}

static int fs_read(void* ctx, const char* domain, uint64_t handle, uint32_t* present,
                   uint32_t* scalar)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    const char* res;
    if (!fs_owns(ctx, domain) || !(res = by_handle(f, handle)))
        return 0;
    return f->provider.read_client(f->provider.ctx, res, handle, present, scalar);
}

static int factory_issue(void* ctx, const char* domain, uint64_t handle, uint32_t scalar)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    const char* res;
    if (!fs_owns(ctx, domain) || !(res = by_handle(f, handle)))
        return 0;
    if ((handle == f->normal_handle && scalar != 1u && scalar != 2u) ||
        (handle == f->hw_handle && scalar != 3u && scalar != 4u))
        return 0;
    return f->provider.issue_client(f->provider.ctx, res, handle, scalar);
}

static int factory_complete(void* ctx, const char* domain, uint64_t handle)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    const char* res;
    if (!fs_owns(ctx, domain) || !f->provider.complete_client || !(res = by_handle(f, handle)))
        return 0;
    return f->provider.complete_client(f->provider.ctx, res, handle);
}

static int idle_client(fs_factory_t* f, const char* res, uint64_t h)
{
    uint32_t present = ~0u, scalar = ~0u;
    return f->provider.read_client(f->provider.ctx, res, h, &present, &scalar) && !present;
}

/* caller quarantines on failure; never retry a destructive close on an unknown state. It
 * also checks the original owner epoch both before and after. */

static int close_one(fs_factory_t* f, const char* domain, const char* res, uint64_t handle,
                     uint8_t* is_open)
{
    if (!*is_open)
        return 1;
    if (!same_epoch(f, domain) || !idle_client(f, res, handle) || !same_epoch(f, domain) ||
        !f->provider.close_idle_client(f->provider.ctx, res, handle) ||
        !f->provider.client_gone(f->provider.ctx, res, handle) || !same_epoch(f, domain))
        return 0;
    *is_open = 0u;
    return 1;
}

static fs_result_t open_channels(fs_factory_t* f, const char* domain, const char* client_name,
                                 int need_normal, int need_hw, const fs_factory_provider_t* p)
{
    fs_native_io_t io;
    uint32_t id = 0u;
    uint64_t epoch = 0u;
    fs_result_t result;
    if (!f || f->bound || f->quarantined || f->normal_open || f->hw_open || !callbacks_ready(p) ||
        !valid_part(domain, FS_NAME_BYTES) || !valid_part(client_name, FS_FACTORY_NAME_BYTES) ||
        (need_hw != 0 && need_hw != 1) || (need_normal != 0 && need_normal != 1) ||
        (!need_normal && !need_hw))
        return FS_INVALID;
    if (!append_name(f->normal_res, sizeof(f->normal_res), domain, "/enable") ||
        !append_name(f->hw_res, sizeof(f->hw_res), domain, "/hwctrlenable"))
        return FS_INVALID;
    f->provider = *p;
    memcpy(f->domain, domain, strlen(domain) + 1u);
    if (!p->owns_domain(p->ctx, domain) || !p->read_owner_epoch(p->ctx, domain, &epoch) || !epoch ||
        !p->resolve_domain_id(p->ctx, domain, &id))
        return FS_OWNERSHIP_LOST;
    f->epoch = epoch;
    f->domain_id = id;
    /* populate domain before client callbacks. Never let a guessed value masquerade as a
     * physical token. */

    memcpy(f->native.name, domain, strlen(domain) + 1u);
    if (!same_epoch(f, domain)) {
        f->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    if (need_normal && (!p->open_idle_client(p->ctx, f->normal_res, client_name,
                                             FS_NPA_REQUIRED_CLIENT_TYPE, &f->normal_handle) ||
                        !f->normal_handle)) {

        if (f->normal_handle) {
            f->normal_open = 1u;
            f->quarantined = 1u;
            return FS_PROVIDER_FAILURE;
        }
        memset(f, 0, sizeof(*f));
        return FS_PROVIDER_FAILURE;
    }
    if (need_normal)
        f->normal_open = 1u;
    if (!same_epoch(f, domain) || (need_normal && !idle_client(f, f->normal_res, f->normal_handle)))
        goto partial_failure;
    if (need_hw) {
        if (!p->open_idle_client(p->ctx, f->hw_res, client_name, FS_NPA_REQUIRED_CLIENT_TYPE,
                                 &f->hw_handle) ||
            !f->hw_handle) {
            /* a provider returning a handle while reporting Open failure violated its
             * no-side-effect guarantee: don't discard it. */

            if (f->hw_handle) {
                f->hw_open = 1u;
                f->quarantined = 1u;
                return FS_PROVIDER_FAILURE;
            }
            goto partial_failure;
        }
        f->hw_open = 1u;
        if (f->hw_handle == f->normal_handle) {
            /* never attempt two closes on that token. */

            f->quarantined = 1u;
            return FS_OWNERSHIP_LOST;
        }
        if (!same_epoch(f, domain) || !idle_client(f, f->hw_res, f->hw_handle))
            goto partial_failure;
    }
    io.ctx = f;
    io.owns_res = fs_owns;
    io.read_owner_epoch = fs_epoch;
    io.read_native_client = fs_read;
    io.issue_native_client = factory_issue;

    result = fs_bind(&f->native, domain, &io, f->normal_handle, f->hw_handle);
    if (result != FS_OK)
        goto partial_failure;
    if (!same_epoch(f, domain))
        goto partial_failure;
    f->bound = 1u;
    return FS_OK;
partial_failure:
    /* a readback/epoch failure may mean another owner already submitted a vote. Only close
     * clients after explicit readback confirms idle. */

    if (!close_one(f, domain, f->hw_res, f->hw_handle, &f->hw_open) ||
        !close_one(f, domain, f->normal_res, f->normal_handle, &f->normal_open)) {
        f->quarantined = 1u;
        return FS_OWNERSHIP_LOST;
    }
    memset(f, 0, sizeof(*f));
    return FS_PROVIDER_FAILURE;
}

fs_result_t fs_factory_open(fs_factory_t* f, const char* domain, const char* client_name,
                            int need_hw, const fs_factory_provider_t* p)
{
    return open_channels(f, domain, client_name, 1, need_hw, p);
}

/* the original 8480 Aggr_AddFsResource and CreateFsResourceNPAHandles switches choose
 * exactly ONE client family for each action 1/2 or 3/4. A hardware-controlled FOOTSWITCH
 * must not acquire /enable as a side effect. */

fs_result_t fs_factory_open_for_action(fs_factory_t* f, const char* domain, const char* client_name,
                                       uint32_t action, const fs_factory_provider_t* p)
{
    if (action == FS_ENABLE || action == FS_DISABLE)
        return open_channels(f, domain, client_name, 1, 0, p);
    if (action == FS_ENABLE_HW_CTL || action == FS_DISABLE_HW_CTL)
        return open_channels(f, domain, client_name, 0, 1, p);
    /* marker actions 5/6 are not native NPA client constructors. */
    return FS_UNSUPPORTED;
}

fs_result_t fs_factory_issue(fs_factory_t* f, uint32_t action)
{
    fs_result_t r;
    if (!f || !f->bound)
        return FS_INVALID;
    if (f->quarantined)
        return FS_QUARANTINED;
    r = fs_issue(&f->native, action);
    if (f->native.quarantined)
        f->quarantined = 1u;
    return r;
}

fs_result_t fs_factory_verify(fs_factory_t* f)
{
    fs_result_t r;
    if (!f || !f->bound)
        return FS_INVALID;
    if (f->quarantined)
        return FS_QUARANTINED;
    r = fs_verify(&f->native);
    if (f->native.quarantined)
        f->quarantined = 1u;
    return r;
}

fs_result_t fs_factory_complete(fs_factory_t* f, uint32_t family)
{
    fs_result_t r;
    const char* res;
    uint64_t handle;
    if (!f || !f->bound)
        return FS_INVALID;
    if (f->quarantined)
        return FS_QUARANTINED;
    if (family == 1u)
        handle = f->normal_handle;
    else if (family == 3u)
        handle = f->hw_handle;
    else
        return FS_INVALID;
    res = by_handle(f, handle);
    if (!res)
        return FS_UNSUPPORTED;
    r = fs_factory_verify(f);
    if (r != FS_OK)
        return r;
    if (f->provider.can_complete_client &&
        !f->provider.can_complete_client(f->provider.ctx, res, handle))
        return FS_UNSUPPORTED;
    r = fs_complete(&f->native, family, f->provider.complete_client ? factory_complete : NULL);
    if (f->native.quarantined)
        f->quarantined = 1u;
    return r;
}

static int fs_retire_off(void* ctx, const char* domain, uint64_t handle)
{
    fs_factory_t* f = (fs_factory_t*)ctx;
    const char* res;
    if (!fs_owns(ctx, domain) || !f->provider.retire_off_client || !(res = by_handle(f, handle)))
        return 0;
    return f->provider.retire_off_client(f->provider.ctx, res, handle);
}

fs_result_t fs_factory_retire_off(fs_factory_t* f, uint32_t family)
{
    const char* res;
    uint64_t handle;
    uint32_t scalar;
    fs_result_t result;
    if (!f || !f->bound)
        return FS_INVALID;
    if (f->quarantined)
        return FS_QUARANTINED;
    if (family == 1u) {
        handle = f->normal_handle;
        scalar = f->native.normal_action;
    } else if (family == 3u) {
        handle = f->hw_handle;
        scalar = f->native.hw_ctl_action;
    } else
        return FS_INVALID;
    res = by_handle(f, handle);
    if (!res || scalar != family + 1u || !f->provider.can_retire_off_client ||
        !f->provider.retire_off_client)
        return FS_UNSUPPORTED;
    result = fs_factory_verify(f);
    if (result != FS_OK)
        return result;
    if (!f->provider.can_retire_off_client(f->provider.ctx, res, handle))
        return FS_UNSUPPORTED;
    result = fs_complete(&f->native, family, fs_retire_off);
    if (f->native.quarantined)
        f->quarantined = 1u;
    return result;
}

fs_result_t fs_factory_close(fs_factory_t* f)
{
    fs_result_t r;
    if (!f || !f->bound)
        return FS_INVALID;
    if (f->quarantined)
        return FS_QUARANTINED;
    r = fs_unbind(&f->native);
    if (r != FS_OK) {
        if (f->native.quarantined)
            f->quarantined = 1u;
        return r;
    }
    if (!close_one(f, f->domain, f->hw_res, f->hw_handle, &f->hw_open) ||
        !close_one(f, f->domain, f->normal_res, f->normal_handle, &f->normal_open))
        goto failed;
    memset(f, 0, sizeof(*f));
    return FS_OK;
failed:
    f->quarantined = 1u;
    return FS_PROVIDER_FAILURE;
}
