#include "fs_votes.h"
#include <string.h>

static int live(fs_votes_t* v)
{
    return v && v->bound && !v->quarantined && v->driver && fs_nodes_verify(&v->driver->npa);
}

static fs_vote_client_t* find(fs_votes_t* v, const char* path, uint32_t family)
{
    uint32_t i;
    for (i = 0u; i < FS_VOTE_CLIENTS; ++i)
        if (v->clients[i].used && (v->clients[i].action > 2u) == family &&
            !strcmp(v->clients[i].path, path))
            return &v->clients[i];
    return NULL;
}

static void notification(fs_votes_t* v, const char* path, uint32_t notify)
{
    v->driver->notify_client = path;
    v->driver->notify_enabled = (uint8_t)notify;
}

int fs_votes_bind(fs_votes_t* v, fs_gdsc_driver_t* driver)
{
    if (!v || !driver || v->bound || !fs_gdsc_provider(driver, &v->provider) ||
        !fs_gdsc_read(driver, 0u, &v->baseline[0]) ||
        ((driver->npa.family_mask & FS_NODE_HWCTL) && !fs_gdsc_read(driver, 1u, &v->baseline[1])))
        return 0;
    v->driver = driver;
    v->bound = 1u;
    return live(v);
}

int fs_votes_apply(fs_votes_t* v, const char* path, uint32_t action, uint32_t notify)
{
    fs_vote_client_t* c;
    uint32_t i, family = action > 2u;
    char name[] = "pep_fs_00";
    if (!v || !path || !*path || strlen(path) >= sizeof(v->clients[0].path) || action < 1u ||
        action > 4u || notify > 1u || !live(v) || !(v->driver->npa.family_mask & (1u << family)) ||
        (notify && !v->driver->notify))
        return 0;
    c = find(v, path, family);
    if (!c) {
        for (i = 0u; i < FS_VOTE_CLIENTS && v->clients[i].used; ++i) {
        }
        if (i == FS_VOTE_CLIENTS)
            return 0;
        c = &v->clients[i];
        name[7] = (char)('0' + i / 10u);
        name[8] = (char)('0' + i % 10u);
        if (fs_factory_open_for_action(&c->factory, v->driver->domain, name, action,
                                       &v->provider) != FS_OK) {
            if (c->factory.quarantined)
                v->quarantined = 1u;
            return 0;
        }
        strcpy(c->path, path);
        c->used = 1u;
        c->action = family * 2u + 2u;
    }
    notification(v, path, notify);
    if (fs_factory_issue(&c->factory, action) != FS_OK)
        goto lost;
    /* A restored baseline must not constrain a subsequent descriptor request. */
    if (v->restore[family].bound &&
        fs_factory_complete(&v->restore[family], family * 2u + 1u) != FS_OK)
        goto lost;
    if (fs_factory_verify(&c->factory) != FS_OK)
        goto lost;
    c->action = action;
    c->notify = notify;
    notification(v, NULL, 0u);
    return 1;
lost:
    notification(v, NULL, 0u);
    v->quarantined = 1u;
    return 0;
}

int fs_votes_client(fs_votes_t* v, const char* path, uint32_t family, uint32_t* action,
                    uint32_t* notify)
{
    fs_vote_client_t* c;
    if (!path || !action || !notify || family > 1u || !live(v))
        return 0;
    c = find(v, path, family);
    if (c && fs_factory_verify(&c->factory) != FS_OK) {
        v->quarantined = 1u;
        return 0;
    }
    *action = c ? c->action : 0u;
    *notify = c ? c->notify : 0u;
    return 1;
}

int fs_votes_withdraw(fs_votes_t* v, const char* path, uint32_t family)
{
    fs_vote_client_t* c;
    fs_factory_t* f;
    uint64_t handle;
    const char* resource;
    char name[] = "pep_restore_0";
    if (!path || family > 1u || !live(v))
        return 0;
    c = find(v, path, family);
    if (!c)
        return 1;
    f = &c->factory;
    handle = family ? f->hw_handle : f->normal_handle;
    resource = family ? f->hw_res : f->normal_res;
    notification(v, path, c->notify);
    if (!v->provider.can_complete_client(v->provider.ctx, resource, handle)) {
        fs_factory_t* restore = &v->restore[family];
        name[12] = (char)('0' + family);
        if (!restore->bound &&
            fs_factory_open_for_action(restore, v->driver->domain, name, v->baseline[family],
                                       &v->provider) != FS_OK)
            goto lost;
        if (fs_factory_issue(restore, v->baseline[family]) != FS_OK)
            goto lost;
    }
    if (fs_factory_complete(f, family * 2u + 1u) != FS_OK || fs_factory_close(f) != FS_OK)
        goto lost;
    memset(c, 0, sizeof(*c));
    notification(v, NULL, 0u);
    return live(v);
lost:
    notification(v, NULL, 0u);
    v->quarantined = 1u;
    return 0;
}
