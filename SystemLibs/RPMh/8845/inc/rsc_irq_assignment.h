#ifndef RSC_IRQ_ASSIGNMENT_H
#define RSC_IRQ_ASSIGNMENT_H
/* read-only attribution of Windows PnP raw/translated res pairs against the signed adapted
 * SM8845 PEP controller record. No ISR reg, MMIO, GIC acknowledgement, or TCS ownership
 * grant. */

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RSC_IRQ_EXPECTED_GIC_INTID 37u
#define RSC_IRQ_RES_LIMIT 64u

typedef struct rsc_irq_pair {
    uint8_t raw_is_interrupt;
    uint8_t translated_is_interrupt;
    uint8_t raw_is_message;
    uint8_t translated_is_message;
    uint32_t raw_vector;
    uint32_t raw_level; /* bus-specific IRQL; not expected to equal INTID 37. */
    uint32_t translated_vector;
    uint32_t translated_level;
    /* reflect the actual PnP desc trigger and sharing semantics. Molokai DRV2 GIC_SPI 5 uses a
     * high-level trigger (DTS flags 4), never a message or edge-triggered interrupt. */

    uint16_t raw_flags;
    uint16_t translated_flags;
    uint8_t raw_share_disposition;
    uint8_t translated_share_disposition;
} rsc_irq_pair_t;

typedef enum rsc_irq_finding {
    RSC_IRQ_NOT_OBSERVED = 0,
    RSC_IRQ_UNIQUE_RAW_CANDIDATE = 1,
    RSC_IRQ_AMBIGUOUS = 2,
    RSC_IRQ_MALFORMED = 3,
    RSC_IRQ_TRANSLATED_ONLY = 4
} rsc_irq_finding_t;

typedef struct rsc_irq_report {
    rsc_irq_finding_t finding;
    uint32_t raw_interrupts;
    uint32_t translated_interrupts;
    uint32_t matching_raw_pairs;
    uint32_t candidate_idx;
    uint32_t win_vector;
    uint32_t win_level;
    /* immutable identity of the selected PnP desc pair. Diagnostic snapshot only, never proof
     * of interrupt or TCS ownership. */

    uint32_t raw_vector;
    uint32_t raw_level;
    uint16_t raw_flags;
    uint16_t translated_flags;
    uint8_t raw_share_disposition;
    uint8_t translated_share_disposition;
} rsc_irq_report_t;

/* source arrays must preserve actual WDFCMRESLIST indices. caller must reject list-count
 * mismatches before calling. */

rsc_irq_report_t rsc_analyze_irq_pairs(const rsc_irq_pair_t* pairs, size_t cnt);
#ifdef __cplusplus
}
#endif
#endif
