#include "clock_target.h"
#include "ufs_footswitch_component.h"
#include "aeob_validate.h"
#include <stdint.h>
#include <string.h>

static uint16_t read_u16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

typedef struct pstate_audit {
    uint32_t state;
    uint32_t seen;
    uint8_t bad;
} pstate_audit_t;

/* serialized ACPI package: string (type 1, NUL-terminated with its own length), followed
 * by unsigned integer (type 0, 2/4/8-byte value). The checked arithmetic never indexes
 * past argument_bytes. */

static int exact_ufs_action(const aeob_action_t* action, uint32_t state)
{
    const char* name = clock_ufs_domain;
    const uint8_t* b;
    uint32_t n, sz, off, pad, i;
    uint64_t scalar = 0u;
    if (!action || state > 1u || strcmp(action->kind, "FOOTSWITCH") || !action->arguments)
        return 0;
    b = action->arguments;
    n = action->argument_bytes;
    if (n < 4u || read_u16(b) != 1u)
        return 0;
    sz = read_u16(b + 2u);
    if (sz != strlen(name) + 1u || n - 4u < sz || memcmp(b + 4u, name, sz))
        return 0;
    pad = sz < 4u ? 4u : sz;
    if (n - 4u < pad)
        return 0;
    off = 4u + pad;
    if (n - off < 4u || read_u16(b + off) != 0u)
        return 0;
    sz = read_u16(b + off + 2u);
    off += 4u;
    if ((sz != 2u && sz != 4u && sz != 8u))
        return 0;
    pad = sz < 4u ? 4u : sz;
    if (n - off != pad)
        return 0;
    for (i = 0u; i < sz; ++i)
        scalar |= (uint64_t)b[off + i] << (i * 8u);
    return scalar == (state == 0u ? 1u : 2u);
}

static int audit_action(const aeob_action_t* action, void* opaque)
{
    pstate_audit_t* a = (pstate_audit_t*)opaque;
    ++a->seen;
    if (a->seen != 1u || !exact_ufs_action(action, a->state))
        a->bad = 1u;
    return 1;
}

static int validate_both(const void* b, size_t bytes)
{
    uint32_t st;
    if (!b || !bytes)
        return 0;
    for (st = 0u; st <= 1u; ++st) {
        pstate_audit_t a = {0};
        aeob_validation_t valid;
        uint32_t cnt = 0u;
        a.state = st;
        if (aeo_b_validate_molokai_comp_p_state(b, bytes, "\\_SB.UFS0", 0u, 0u, st, &valid) !=
                AEO_B_OK ||
            valid.failure != AEO_B_RES_NONE || valid.footswitch_cnt != 1u ||
            aeo_b_visit_comp_p_state(b, bytes, "\\_SB.UFS0", 0u, 0u, st, audit_action, &a, &cnt) !=
                AEO_B_OK ||
            cnt != 1u || a.seen != 1u || a.bad)
            return 0;
    }
    return 1;
}

fs_result_t ufs_fs_open(ufs_fs_comp_t* scope, const void* reviewed, size_t bytes,
                        fs_registry_t* registry, uint32_t owner_set, uint32_t owner_idx,
                        const char* client_name, const fs_factory_provider_t* provider)
{
    fs_result_t r;
    uint64_t ticket = 0u;
    if (!scope || scope->bound || scope->quarantined || scope->ticket || !registry ||
        !client_name || !provider || !validate_both(reviewed, bytes))
        return FS_INVALID;
    /* no NPA side effects occur before complete aeo_b validation. */
    r = fs_registry_acquire(registry, owner_set, owner_idx, clock_ufs_domain, client_name,
                            FS_ENABLE, provider, &ticket);
    if (r != FS_OK) {
        if (registry->quarantined)
            scope->quarantined = 1u;
        return r;
    }
    scope->registry = registry;
    scope->ticket = ticket;
    scope->owner_state_set = owner_set;
    scope->owner_state_idx = owner_idx;
    scope->last_verified_state = UINT32_MAX;
    scope->bound = 1u;
    return FS_OK;
}

fs_result_t ufs_fs_set_p_state(ufs_fs_comp_t* scope, uint32_t state)
{
    fs_result_t r;
    if (!scope || state > 1u || !scope->bound || !scope->registry || !scope->ticket)
        return FS_INVALID;
    if (scope->quarantined)
        return FS_QUARANTINED;
    /* every transition issues the exact scalar, even if the last software state matches; the
     * provider verifies the actual client. */

    r = fs_registry_issue(scope->registry, scope->ticket, state == 0u ? FS_ENABLE : FS_DISABLE);
    if (r != FS_OK) {
        scope->last_verified_state = UINT32_MAX;
        if (scope->registry->quarantined)
            scope->quarantined = 1u;
        return r;
    }
    r = fs_registry_verify(scope->registry, scope->ticket);
    if (r != FS_OK) {
        scope->last_verified_state = UINT32_MAX;
        if (scope->registry->quarantined)
            scope->quarantined = 1u;
        return r;
    }
    scope->last_verified_state = state;
    return FS_OK;
}

fs_result_t ufs_fs_verify(ufs_fs_comp_t* scope)
{
    fs_result_t r;
    if (!scope || !scope->bound || !scope->ticket || !scope->registry)
        return FS_INVALID;
    if (scope->quarantined)
        return FS_QUARANTINED;
    r = fs_registry_verify(scope->registry, scope->ticket);
    if (r != FS_OK) {
        scope->last_verified_state = UINT32_MAX;
        if (scope->registry->quarantined)
            scope->quarantined = 1u;
    }
    return r;
}

fs_result_t ufs_fs_close(ufs_fs_comp_t* scope)
{
    fs_result_t r;
    if (!scope || !scope->bound || !scope->registry || !scope->ticket)
        return FS_INVALID;
    if (scope->quarantined)
        return FS_QUARANTINED;
    r = fs_registry_release(scope->registry, scope->ticket);
    /* a verified PSTATE1 is the explicit physical disable request. Only then may the native
     * NPA provider terminate its scalar-2 client vote and destroy it. npa_complete_request is
     * NOT scalar 2; if no true completion provider exists, the old unsupported outcome
     * survives. */

    if (r == FS_UNSUPPORTED && scope->last_verified_state == 1u) {
        r = fs_registry_complete(scope->registry, scope->ticket);
        if (r == FS_UNSUPPORTED)
            r = fs_registry_retire_off(scope->registry, scope->ticket);
        if (r == FS_OK)
            r = fs_registry_release(scope->registry, scope->ticket);
    }
    if (r == FS_OK)
        memset(scope, 0, sizeof(*scope));
    else if (scope->registry->quarantined)
        scope->quarantined = 1u;
    return r;
}
