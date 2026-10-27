#include "registry.h"
#include <string.h>
#include <stdint.h>

static int bounded_label(const char* s, uint32_t capacity)
{
    uint32_t i;
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

static unsigned char lower_ascii(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

static int domain_equal(const char* a, const char* b)
{
    unsigned char x, y;
    do {
        x = lower_ascii((unsigned char)*a++);
        y = lower_ascii((unsigned char)*b++);
        if (x != y)
            return 0;
    } while (x);
    return 1;
}

static uint8_t action_family(uint32_t action)
{
    if (action == FS_ENABLE || action == FS_DISABLE)
        return 1u;
    if (action == FS_ENABLE_HW_CTL || action == FS_DISABLE_HW_CTL)
        return 3u;
    return 0u;
}

static int token_alias(const fs_registry_entry_t* a, const fs_registry_entry_t* b)
{
    if (!a->in_use || !b->in_use)
        return 0;
    return (a->factory.normal_handle && (a->factory.normal_handle == b->factory.normal_handle ||
                                         a->factory.normal_handle == b->factory.hw_handle)) ||
           (a->factory.hw_handle && (a->factory.hw_handle == b->factory.normal_handle ||
                                     a->factory.hw_handle == b->factory.hw_handle));
}

static fs_result_t check_entry(fs_registry_t* reg, fs_registry_entry_t* entry)
{
    fs_result_t result;
    if (reg->quarantined)
        return FS_QUARANTINED;
    if (!entry->in_use || !entry->references || !entry->factory.bound)
        return FS_INVALID;
    result = fs_factory_verify(&entry->factory);
    if (result != FS_OK) {
        /* a native readback error may hide an outstanding vote. A different state set must not
         * reopen the same token during a bad epoch. */

        reg->quarantined = 1u;
    }
    return result;
}

/* do not allow a different native provider to borrow a cached client. A matching name is
 * not a transferable native Windows NPA capability. */

static int same_provider(const fs_factory_provider_t* a, const fs_factory_provider_t* b)
{
    return a && b && a->ctx == b->ctx && a->owns_domain == b->owns_domain &&
           a->read_owner_epoch == b->read_owner_epoch &&
           a->resolve_domain_id == b->resolve_domain_id &&
           a->open_idle_client == b->open_idle_client && a->read_client == b->read_client &&
           a->issue_client == b->issue_client && a->complete_client == b->complete_client &&
           a->close_idle_client == b->close_idle_client && a->client_gone == b->client_gone &&
           a->can_complete_client == b->can_complete_client &&
           a->can_retire_off_client == b->can_retire_off_client &&
           a->retire_off_client == b->retire_off_client;
}

/* generation zero and saturated generations are never handed out. */

static int find_free_lease(const fs_registry_t* reg, uint32_t* lease_idx)
{
    uint32_t i;
    for (i = 0u; i < FS_REGISTRY_LEASES; ++i) {
        if (!reg->leases[i].active && reg->last_generation[i] != UINT32_MAX) {
            *lease_idx = i;
            return 1;
        }
    }
    return 0;
}

static uint64_t assign_lease(fs_registry_t* reg, uint32_t lease_idx, uint32_t entry_idx)
{
    fs_registry_lease_t* lease = &reg->leases[lease_idx];
    uint32_t generation = ++reg->last_generation[lease_idx];
    lease->generation = generation;
    lease->entry_idx = entry_idx;
    lease->active = 1u;
    return ((uint64_t)generation << 32) | ((uint64_t)lease_idx + 1u);
}

static fs_registry_entry_t* from_ticket(fs_registry_t* reg, uint64_t ticket,
                                        fs_registry_lease_t** out_lease)
{
    uint32_t raw = (uint32_t)ticket;
    fs_registry_lease_t* lease;
    if (!raw || raw > FS_REGISTRY_LEASES || !(ticket >> 32))
        return NULL;
    lease = &reg->leases[raw - 1u];
    if (!lease->active || lease->generation != (uint32_t)(ticket >> 32) ||
        lease->entry_idx >= FS_REGISTRY_CAPACITY)
        return NULL;
    *out_lease = lease;
    return &reg->entries[lease->entry_idx];
}

fs_result_t fs_registry_acquire(fs_registry_t* reg, uint32_t state_set, uint32_t state_idx,
                                const char* domain, const char* client_name, uint32_t action,
                                const fs_factory_provider_t* provider, uint64_t* ticket)
{
    uint32_t i, free_idx = FS_REGISTRY_CAPACITY;
    uint32_t lease_idx = UINT32_MAX;
    uint8_t family = action_family(action);
    fs_result_t result;
    fs_registry_entry_t* entry;
    if (ticket)
        *ticket = 0u;
    if (!reg || !ticket || !family || !bounded_label(domain, FS_NAME_BYTES) ||
        !bounded_label(client_name, FS_FACTORY_NAME_BYTES))
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    if (!find_free_lease(reg, &lease_idx))
        return FS_UNSUPPORTED;
    for (i = 0u; i < FS_REGISTRY_CAPACITY; ++i) {
        entry = &reg->entries[i];
        if (!entry->in_use) {
            if (free_idx == FS_REGISTRY_CAPACITY)
                free_idx = i;
            continue;
        }
        /* one registry belongs to exactly one native provider/owner. A different state scope must
         * not silently switch backend identity. */

        if (!same_provider(&entry->factory.provider, provider))
            return FS_OWNERSHIP_LOST;
        if (entry->state_set != state_set || entry->state_idx != state_idx ||
            entry->family != family || strcmp(entry->client_name, client_name) ||
            !domain_equal(entry->factory.domain, domain))
            continue;
        result = check_entry(reg, entry);
        if (result != FS_OK)
            return result;
        if (entry->references == UINT32_MAX)
            return FS_INVALID;
        ++entry->references;
        *ticket = assign_lease(reg, lease_idx, i);
        return FS_OK;
    }
    if (free_idx == FS_REGISTRY_CAPACITY)
        return FS_UNSUPPORTED;
    entry = &reg->entries[free_idx];
    /* partial failure must keep the owner visible in this slot if the factory retains a
     * possibly-live handle. */

    memset(entry, 0, sizeof(*entry));
    entry->state_set = state_set;
    entry->state_idx = state_idx;
    entry->family = family;
    memcpy(entry->client_name, client_name, strlen(client_name) + 1u);
    entry->in_use = 1u;
    entry->references = 1u;
    result = fs_factory_open_for_action(&entry->factory, domain, client_name, action, provider);
    if (result != FS_OK) {
        if (entry->factory.quarantined || entry->factory.normal_open || entry->factory.hw_open ||
            entry->factory.normal_handle || entry->factory.hw_handle)
            reg->quarantined = 1u;
        else
            memset(entry, 0, sizeof(*entry));
        return result;
    }
    /* reusing one provider token for different state scopes would allow one state destructor
     * to retire the other's live client. lookup is state-scoped. A shared token here is
     * ambiguous: do not close it. */

    for (i = 0u; i < FS_REGISTRY_CAPACITY; ++i) {
        if (i != free_idx && token_alias(entry, &reg->entries[i])) {
            entry->factory.quarantined = 1u;
            reg->quarantined = 1u;
            return FS_OWNERSHIP_LOST;
        }
    }
    *ticket = assign_lease(reg, lease_idx, free_idx);
    return FS_OK;
}

fs_result_t fs_registry_verify(fs_registry_t* reg, uint64_t ticket)
{
    fs_registry_lease_t* lease = NULL;
    fs_registry_entry_t* entry;
    if (!reg)
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    entry = from_ticket(reg, ticket, &lease);
    if (!entry)
        return FS_INVALID;
    return check_entry(reg, entry);
}

fs_result_t fs_registry_issue(fs_registry_t* reg, uint64_t ticket, uint32_t action)
{
    fs_result_t result;
    fs_registry_lease_t* lease = NULL;
    fs_registry_entry_t* entry;
    if (!reg)
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    entry = from_ticket(reg, ticket, &lease);
    if (!entry)
        return FS_INVALID;
    if (action_family(action) != entry->family)
        return FS_UNSUPPORTED;
    result = check_entry(reg, entry);
    if (result != FS_OK)
        return result;
    result = fs_factory_issue(&entry->factory, action);
    if (entry->factory.quarantined)
        reg->quarantined = 1u;
    return result;
}

fs_result_t fs_registry_complete(fs_registry_t* reg, uint64_t ticket)
{
    fs_registry_lease_t* lease = NULL;
    fs_registry_entry_t* entry;
    fs_result_t result;
    if (!reg)
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    entry = from_ticket(reg, ticket, &lease);
    if (!entry || entry->references != 1u)
        return FS_UNSUPPORTED;
    result = check_entry(reg, entry);
    if (result != FS_OK)
        return result;
    result = fs_factory_complete(&entry->factory, entry->family);
    if (entry->factory.quarantined)
        reg->quarantined = 1u;
    return result;
}

fs_result_t fs_registry_retire_off(fs_registry_t* reg, uint64_t ticket)
{
    fs_registry_lease_t* lease = NULL;
    fs_registry_entry_t* entry;
    fs_result_t result;
    if (!reg)
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    entry = from_ticket(reg, ticket, &lease);
    if (!entry || entry->references != 1u)
        return FS_UNSUPPORTED;
    result = check_entry(reg, entry);
    if (result != FS_OK)
        return result;
    result = fs_factory_retire_off(&entry->factory, entry->family);
    if (entry->factory.quarantined)
        reg->quarantined = 1u;
    return result;
}

fs_result_t fs_registry_release(fs_registry_t* reg, uint64_t ticket)
{
    fs_result_t result;
    fs_registry_lease_t* lease = NULL;
    fs_registry_entry_t* entry;
    if (!reg)
        return FS_INVALID;
    if (reg->quarantined)
        return FS_QUARANTINED;
    entry = from_ticket(reg, ticket, &lease);
    if (!entry)
        return FS_INVALID;
    result = check_entry(reg, entry);
    if (result != FS_OK)
        return result;
    if (entry->references > 1u) {
        --entry->references;
        lease->active = 0u;
        return FS_OK;
    }
    result = fs_factory_close(&entry->factory);
    if (entry->factory.quarantined)
        reg->quarantined = 1u;
    if (result == FS_OK) {
        memset(entry, 0, sizeof(*entry));
        lease->active = 0u;
    }
    return result;
}
