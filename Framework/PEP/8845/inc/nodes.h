#ifndef FOOTSWITCH_NODES_H
#define FOOTSWITCH_NODES_H

#include "factory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_NODE_CLIENTS 16u
#define FS_NODE_NORMAL 1u
#define FS_NODE_HWCTL 2u
#define FS_CLIENT_INITIAL 0xffffu
#define FS_NODE_INITIAL 7u

/* the caller holds one lock across both nodes, callbacks, and owner removal. Tokens
 * identify these retained records, never pointers into an OEM image. */
typedef struct fs_node_io {
    void* ctx;
    int (*owns)(void*, const char*, uint32_t, uint64_t);
    int (*read_action)(void*, const char*, uint64_t, uint32_t, uint32_t*);
    int (*apply_action)(void*, const char*, uint64_t, uint32_t);
} fs_node_io_t;

typedef struct fs_node_client {
    uint64_t token;
    char name[FS_FACTORY_NAME_BYTES];
    uint32_t work[2];
    uint8_t idx;
    uint8_t family;
    uint8_t in_use;
    uint8_t has_req;
} fs_node_client_t;

typedef struct fs_node {
    char name[FS_FACTORY_NAME_BYTES];
    uint32_t maximum;
    uint32_t aggregate;
    uint32_t active_action;
    uint8_t has_physical_state;
    uint8_t force_driver;
} fs_node_t;

typedef struct fs_nodes {
    fs_node_io_t io;
    char domain[FS_NAME_BYTES];
    fs_node_t nodes[2];
    fs_node_client_t clients[FS_NODE_CLIENTS];
    uint64_t epoch;
    uint64_t next_token;
    uint32_t domain_id;
    uint32_t family_mask;
    uint8_t bound;
    uint8_t quarantined;
    uint8_t closing;
} fs_nodes_t;

int fs_nodes_bind(fs_nodes_t*, const char*, uint32_t, uint32_t, const fs_node_io_t*, uint64_t);
int fs_nodes_provider(fs_nodes_t*, fs_factory_provider_t*);
int fs_nodes_verify(fs_nodes_t*);
/* owner must have closed the external admission gate and drained ALL callers. No callback
 * can acquire this node lock after it is released for shutdown. */
int fs_nodes_begin_shutdown(fs_nodes_t*, uint64_t epoch);
/* requires external callback rundown. Does not withdraw votes or release HAL. */
int fs_nodes_retire(fs_nodes_t*);

#ifdef __cplusplus
}
#endif
#endif
