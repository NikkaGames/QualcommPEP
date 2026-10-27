#ifndef ICB_BUS_H
#define ICB_BUS_H
/* /100: Molokai interconnect (ICB) bandwidth owner - portable core. LATENCY
 * AxiCfgInt_SetLatency: an encoded latency this PEP never requests. */

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BCM_KIND_BANDWIDTH 1u
#define BCM_KIND_LATENCY 2u
#define BCM_KIND_VECTOR 3u
#define BCM_KIND_OTHER 4u

/* generated BCM hardware node (src/molokai/icb_bcm8845_gen.c). */

typedef struct icb_bcm_node {
    const char* name;
    uint32_t kind;
    uint32_t output;
    uint32_t relative_first;
    uint32_t relative_cnt;
    uint32_t keep_alive;
} icb_bcm_node_t;

typedef struct icb_node {
    const char* symbol;
    uint32_t id;
    const char* name;
    uint32_t width_bytes;
    uint32_t agg_ports;
    uint32_t bcm_first;
    uint32_t bcm_cnt;
} icb_node_t;

extern const icb_bcm_node_t icb_bcm_nodes[];
extern const unsigned int icb_bcm_node_cnt;
extern const unsigned char icb_bcm_relatives[];
extern const unsigned int icb_bcm_relative_cnt;
extern const unsigned char icb_node_bcms[];
extern const unsigned int icb_node_bcm_cnt;
extern const icb_node_t icb_master_nodes[];
extern const unsigned int icb_master_node_cnt;
extern const icb_node_t icb_slave_nodes[];
extern const unsigned int icb_slave_node_cnt;

#define ICB_MAX_BCMS 32u
#define ICB_MAX_CLIENTS 128u
#define ICB_MAX_ROUTE_NODES 16u
#define ICB_MAX_COMMANDS 16u
#define ICB_PATH_BYTES 256u

#define BCM_VOTE_MASK 0x3FFFu
#define BCM_VOTE_X_SHIFT 14u
#define BCM_VALID 0x20000000u
#define BCM_COMMIT 0x40000000u

typedef struct icb_bcm {
    const char* name;
    uint32_t address;
    uint32_t unit;
    uint32_t width;
    uint32_t clk_id;
    uint8_t address_known;
    uint8_t aux_known;
    uint8_t votable;
} icb_bcm_t;

typedef struct icb_client {
    uint8_t in_use;
    char dev_path[ICB_PATH_BYTES];
    uint32_t master_id, slave_id;
    uint64_t ab, ib;
} icb_client_t;

typedef struct icb_command {
    uint32_t address;
    uint32_t data;
    uint8_t completion;
} icb_command_t;

typedef struct icb_bus {
    icb_bcm_t bcms[ICB_MAX_BCMS];
    uint32_t bcm_cnt;
    icb_client_t clients[ICB_MAX_CLIENTS];
    uint8_t initialized;
} icb_bus_t;

uint32_t icb_bus_init(icb_bus_t* bus, const uint8_t* cmd_db, size_t cmd_db_bytes);

const icb_node_t* icb_find_master(const char* symbol);
const icb_node_t* icb_find_slave(const char* symbol);

uint32_t icb_route_nodes(const char* master_symbol, const char* slave_symbol,
                         const icb_node_t** nodes, uint32_t capacity);

int icb_route_supported(const icb_bus_t* bus, const char* master_symbol, const char* slave_symbol);
/* the command-database name of the first BCM of the route this core cannot vote, or 0 when
 * the route is fully supported (diagnostics for the log). */

const char* icb_route_missing_bcm(const icb_bus_t* bus, const char* master_symbol,
                                  const char* slave_symbol);

/* record / clear one client's vote (ab == ib == 0 clears). */
int icb_set_client_vote(icb_bus_t* bus, const char* dev_path, const char* master_symbol,
                        const char* slave_symbol, uint64_t ab, uint64_t ib);
int icb_get_client_vote(const icb_bus_t* bus, const char* dev_path, const char* master_symbol,
                        const char* slave_symbol, uint64_t* ab, uint64_t* ib);

/* commands for the BCMs of a route, from the aggregate of ALL recorded client votes. */

int icb_route_commands(const icb_bus_t* bus, const char* master_symbol, const char* slave_symbol,
                       icb_command_t* out, uint32_t capacity, uint32_t* cnt);

int icb_single_req_commands(const icb_bus_t* bus, const char* master_symbol,
                            const char* slave_symbol, uint64_t ab, uint64_t ib, icb_command_t* out,
                            uint32_t capacity, uint32_t* cnt);

uint32_t icb_encode_vote(uint64_t scaled_ib, uint64_t scaled_ab);
#ifdef __cplusplus
}
#endif
#endif
