#include "perf_clock_desc.h"
#include <string.h>

int clock_perf_discover(clock_perf_catalog_t* out, const void* blob, size_t bytes,
                        const program_idx_t* idx)
{
    const gcc_clock_t* clock;
    const gcc_domain_t* domain;
    const unsigned char* data = blob;
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t i, j;
    size_t n;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!data || !idx || !idx->valid || bytes < 12u || bytes > AEOB_MAX_SIZE ||
        bytes != idx->desc_bytes || idx->comp_mask != 1u || idx->f_state_mask[0] != 1u ||
        idx->p_state_set_mask[0])
        return 0;
    for (n = 0u; n < bytes; ++n) {
        hash ^= data[n];
        hash *= UINT64_C(1099511628211);
    }
    if (hash != idx->desc_fingerprint ||
        aeo_b_discover_comp_clock(blob, bytes, idx->path, 0u, out->clock) != AEO_B_OK)
        goto bad;
    clock = gcc_find_req_clock(out->clock);
    domain = gcc_clock_domain(clock);
    if (!clock || !domain || domain->flags || !domain->cmd_rcgr_offset || !domain->freq_cnt ||
        domain->freq_cnt > 32u || domain->freq_first > QcPep8845GccFreqCount ||
        domain->freq_cnt > QcPep8845GccFreqCount - domain->freq_first)
        goto bad;
    for (i = 0u; i < domain->freq_cnt; ++i) {
        const gcc_freq_t* f = &QcPep8845GccFreqs[domain->freq_first + i];
        const gcc_freq_t* selected = NULL;
        if (!f->hz || f->src_kind > GCC_SRC_GPLL4 ||
            gcc_select_freq(clock, f->hz, &selected) != GCC_OK || !selected ||
            selected->hz != f->hz)
            goto bad;
        for (j = 0u; j < out->cnt && out->hertz[j] != f->hz; ++j) {
        }
        if (j == out->cnt)
            out->hertz[out->cnt++] = f->hz;
    }
    for (i = 1u; i < out->cnt; ++i) {
        uint32_t value = out->hertz[i];
        for (j = i; j && out->hertz[j - 1u] > value; --j)
            out->hertz[j] = out->hertz[j - 1u];
        out->hertz[j] = value;
    }
    memcpy(out->path, idx->path, sizeof(out->path));
    out->desc_fingerprint = hash;
    return out->cnt != 0u;
bad:
    memset(out, 0, sizeof(*out));
    return 0;
}
