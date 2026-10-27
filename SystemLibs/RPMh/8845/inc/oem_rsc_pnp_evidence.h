#ifndef OEM_RSC_PNP_EVIDENCE_H
#define OEM_RSC_PNP_EVIDENCE_H
/* read-only PnP src-provenance census. A physical-window or IRQ observation is never a
 * Qualcomm firmware / TCS / GIC ownership grant. */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RSC_EVIDENCE_MAX 64u
#define RSC_EVIDENCE_DRV2_BASE UINT64_C(0x18920000)
#define RSC_EVIDENCE_DRV2_BYTES UINT64_C(0x10000)
#define RSC_EVIDENCE_IRQ 37u
#define RSC_EVIDENCE_EXCLUSIVE 1u
#define RSC_EVIDENCE_LATCHED 0x0001u
#define RSC_EVIDENCE_MEMORY 1u
#define RSC_EVIDENCE_INTERRUPT 2u
#define RSC_EVIDENCE_OTHER 3u

typedef struct rsc_evidence_desc {
    uint8_t kind, message, share, reserved;
    uint16_t flags, padding;
    uint32_t vector, level;
    uint64_t base, bytes;
} rsc_evidence_desc_t;

typedef enum rsc_evidence_finding {
    RSC_EVIDENCE_NO_WINDOW = 0,
    RSC_EVIDENCE_PAIRED_CANDIDATE = 1,
    RSC_EVIDENCE_INVALID = 2,
    RSC_EVIDENCE_AMBIGUOUS_WINDOW = 3,
    RSC_EVIDENCE_NO_MATCHING_IRQ = 4,
    RSC_EVIDENCE_AMBIGUOUS_IRQ = 5,
    RSC_EVIDENCE_SHARED_IRQ = 6,
    RSC_EVIDENCE_MISMATCHED_PAIR = 7
} rsc_evidence_finding_t;

typedef struct rsc_evidence_report {
    uint64_t pnp_generation;
    rsc_evidence_finding_t finding;
    uint32_t res_cnt, window_idx, irq_idx, win_vector, win_level;
    uint32_t exact_win, matching_raw_irqs;
    uint8_t has_dedicated_window, has_unique_irq, reserved[2];
} rsc_evidence_report_t;

/* arrays must be captured from the same device, same PrepareHardware, with each
 * raw/translated desc at the SAME res-list idx. Never call with a made-up list from
 * firmware or a different device. */

rsc_evidence_finding_t rsc_audit_pnp_evidence(const rsc_evidence_desc_t* raw,
                                              const rsc_evidence_desc_t* translated, size_t cnt,
                                              uint64_t generation, rsc_evidence_report_t* out);
#ifdef __cplusplus
}
#endif
#endif
