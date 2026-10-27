#include "pstate_payload.h"
#include <string.h>

int po_fx_p_state_validate_v1(const void* input, size_t bytes, uint32_t* comp, uint32_t* pstate)
{
    pofx_v1_req_t r;
    if (!input || !comp || !pstate || bytes != sizeof(r))
        return 0;
    memcpy(&r, input, sizeof(r));
    *comp = r.comp;
    *pstate = r.p_state;
    return 1;
}

int po_fx_p_state_validate_v2(const void* input, size_t bytes, uint32_t* comp, uint32_t* first,
                              uint32_t* cnt)
{
    pofx_v2_header_t hdr;
    uint32_t seen = 0u;
    uint32_t i, n;
    const uint8_t* data = (const uint8_t*)input;
    if (!input || !comp || !first || !cnt || sizeof(void*) != 8u || sizeof(hdr) != 24u ||
        bytes < sizeof(hdr) + sizeof(pofx_v2_set_t) ||
        (bytes - sizeof(hdr)) % sizeof(pofx_v2_set_t))
        return 0;
    if ((bytes - sizeof(hdr)) / sizeof(pofx_v2_set_t) > POFX_MAX_SETS)
        return 0;
    n = (uint32_t)((bytes - sizeof(hdr)) / sizeof(pofx_v2_set_t));
    if (!n)
        return 0;
    memcpy(&hdr, data, sizeof(hdr));
    for (i = 0u; i < n; ++i) {
        pofx_v2_set_t s;
        memcpy(&s, data + sizeof(hdr) + (size_t)i * sizeof(s), sizeof(s));
        if (s.set >= POFX_MAX_SETS || (seen & (1u << s.set)))
            return 0;
        seen |= 1u << s.set;
        if (i == 0u)
            *first = s.p_state;
    }
    *comp = hdr.comp;
    *cnt = n;
    return 1;
}
