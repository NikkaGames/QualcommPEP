#ifndef DOMAIN_RAILS_H
#define DOMAIN_RAILS_H

#include "gdsc_native.h"
#include "domain_rail_io.h"

typedef struct domain_rail_spec {
    const char* domain;
    const char* client;
    const char* names[2];
    uint32_t floors[2];
    uint32_t mask;
} domain_rail_spec_t;

typedef struct domain_rails {
    domain_rail_io_t io;
    const domain_rail_spec_t* spec;
    uint64_t epoch;
    uint8_t bound, held, quarantined;
} domain_rails_t;

int domain_rails_bind(domain_rails_t*, const domain_rail_spec_t*, const domain_rail_io_t*,
                      uint64_t);
int domain_rails_provider(domain_rails_t*, fs_gdsc_rail_provider_t*);
int domain_rails_retire(domain_rails_t*);

#endif
