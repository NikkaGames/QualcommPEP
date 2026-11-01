#ifndef DOMAIN_RAIL_IO_H
#define DOMAIN_RAIL_IO_H

#include <stdint.h>

typedef struct domain_rail_io {
    void* ctx;
    int (*owns_domain)(void*, const char*, uint64_t);
    int (*owner_epoch)(void*, uint64_t*);
    int (*rail_resolved)(void*, const char*, uint32_t);
    /* return per-client vote; no aggregate is a substitute for this query. */
    int (*read_client_vote)(void*, const char*, const char*, uint32_t*, uint32_t*);

    int (*read_physical)(void*, const char*, uint32_t*, uint32_t*);

    int (*vote)(void*, const char*, const char*, uint32_t, uint32_t);
    int (*withdraw)(void*, const char*, const char*);
} domain_rail_io_t;

#endif
