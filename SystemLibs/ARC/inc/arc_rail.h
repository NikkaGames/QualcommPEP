#ifndef ARC_RAIL_H
#define ARC_RAIL_H
#include "cmddb.h"
#define ARC_MAX_LEVELS 32u
#define ARC_CLIENTS 16u

typedef struct arc_io {
    void* ctx;
    int (*owns)(void*, uint64_t);
    int (*read)(void*, uint32_t, uint32_t*);
    /* 1: exact completion, 0: never dispatched, -1: outcome uncertain. */
    int (*write)(void*, uint32_t, uint32_t);
} arc_io_t;

typedef struct arc_client {
    char identity[256];
    uint32_t corner;
    uint8_t used;
} arc_client_t;

typedef struct arc_rail {
    arc_io_t io;
    char name[9];
    uint32_t address;
    uint16_t levels[ARC_MAX_LEVELS];
    uint32_t level_cnt;
    uint32_t boot_floor;
    uint64_t epoch;
    arc_client_t clients[ARC_CLIENTS];
    uint8_t bound;
    uint8_t quarantined;
    uint8_t floor_published;
} arc_rail_t;

typedef struct arc_bind_diag {
    uint32_t phase, address, aux_length, level_idx, level_cnt;
    uint32_t raw[2];
} arc_bind_diag_t;

/* hold the passive owner mutex. Bind reads ARC state without adopting a firmware vote as a client;
 * retain inherited aggregate floors until verified consumer handoff. */
int arc_bind(arc_rail_t*, const uint8_t*, size_t, const char*, const arc_io_t*, uint64_t);
int arc_bind_ex(arc_rail_t*, const uint8_t*, size_t, const char*, const arc_io_t*, uint64_t,
                arc_bind_diag_t*);
int arc_map(const arc_rail_t*, uint32_t, uint32_t*);
int arc_read(arc_rail_t*, uint32_t*, uint32_t*);
int arc_client_vote(const arc_rail_t*, const char*, uint32_t*, uint32_t*);
int arc_vote(arc_rail_t*, const char*, uint32_t, uint32_t);
int arc_retire(arc_rail_t*);
#endif
