#ifndef MOLOKAI_CATALOG_H
#define MOLOKAI_CATALOG_H
/* no hardware writes. */

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    unsigned int id;
    const char* symbol;
    const char* name;
    const char* bus;
    unsigned int width_bytes;
} icb_entry_t;

typedef struct {
    const char* name;
    const char* domain;
    unsigned char read_only;
    const char* clock_flags;
    const char* domain_flags;
} clock_entry_t;

typedef struct {
    const char* name;
    const char* pmic;
    const char* peripheral;
    unsigned int retention_uv;
} rail_entry_t;

extern const icb_entry_t icb_masters[];
extern const unsigned int icb_master_cnt;
extern const icb_entry_t icb_slaves[];
extern const unsigned int icb_slave_cnt;
extern const clock_entry_t clocks[];
extern const unsigned int clock_catalog_cnt;
extern const rail_entry_t rail_catalog[];
extern const unsigned int rail_catalog_cnt;
extern const char* const pmic_clients[];
extern const unsigned int pmic_client_cnt;

typedef struct {
    unsigned int master_id;
    unsigned int slave_id;
} icb_hop_t;

typedef struct {
    unsigned int master_id;
    unsigned int slave_id;
    unsigned int hop_offset;
    unsigned int hop_cnt;
} icb_route_t;

extern const icb_hop_t icb_hops[];
extern const unsigned int icb_hop_cnt;
extern const icb_route_t icb_routes[];
extern const unsigned int icb_route_cnt;
const icb_route_t* lookup_icb_route(unsigned int master_id, unsigned int slave_id);
const icb_entry_t* lookup_icb_master(unsigned int id);
const icb_entry_t* lookup_icb_slave(unsigned int id);
const clock_entry_t* lookup_clock(const char* name);

int clock_can_reconfigure(const char* name);
const rail_entry_t* lookup_rail(const char* name);
#ifdef __cplusplus
}
#endif
#endif
