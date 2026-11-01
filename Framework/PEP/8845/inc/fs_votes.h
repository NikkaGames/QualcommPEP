#ifndef FS_VOTES_H
#define FS_VOTES_H

#include "gdsc_native.h"

#define FS_VOTE_CLIENTS 12u

typedef struct fs_vote_client {
    char path[256];
    fs_factory_t factory;
    uint32_t action, notify;
    uint8_t used;
} fs_vote_client_t;

typedef struct fs_votes {
    fs_gdsc_driver_t* driver;
    fs_factory_provider_t provider;
    fs_vote_client_t clients[FS_VOTE_CLIENTS];
    fs_factory_t restore[2];
    uint32_t baseline[2];
    uint8_t bound, quarantined;
} fs_votes_t;

/* Serialized by the controller owner. Rollback clients are active only while the
 * domain has no descriptor clients, preserving its independently read initial state. */
int fs_votes_bind(fs_votes_t*, fs_gdsc_driver_t*);
int fs_votes_apply(fs_votes_t*, const char* path, uint32_t action, uint32_t notify);
int fs_votes_client(fs_votes_t*, const char* path, uint32_t family, uint32_t* action,
                    uint32_t* notify);
int fs_votes_withdraw(fs_votes_t*, const char* path, uint32_t family);

#endif
