#ifndef BCM_H
#define BCM_H
/* pure, non-hardware calculations. */

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bcm_aux {
    uint32_t bandwidth_unit;
    uint16_t port;
    uint8_t clock_id;
} bcm_aux_t;

typedef struct bcm_bandwidth {
    uint64_t average;
    uint64_t peak;
    uint64_t suppressible_average;
    uint64_t suppressible_peak;
    uint64_t total;
} bcm_bandwidth_t;

/* succeeds whenever the record is long enough; bandwidth_unit and port may be zero (a
 * VECTOR node), so the caller must check them before using them as divisors. */

int bcm_decode_aux(const uint8_t* aux, size_t bytes, bcm_aux_t* out);
/* implements BOOT.MXF ul_int.c BW_SCALE and max aggregation, checked math. width and port
 * counts come from verified ul_data.c route nodes. */

int bcm_scale_req(uint64_t required_ab, uint64_t required_ib, uint64_t suppressible_ab,
                  uint64_t suppressible_ib, uint64_t bcm_width, uint64_t node_width,
                  uint32_t node_ports, bcm_bandwidth_t* out);
#ifdef __cplusplus
}
#endif
#endif
