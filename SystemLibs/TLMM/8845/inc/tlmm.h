#ifndef TLMM_H
#define TLMM_H
/* the TLMM block is at physical 0x0f000000 (op15r.dts, msmhwiobase.h TLMM_BASE, 16 MiB).
 * the vendor TLMM HAL places the pin registers in tile 0 at TLMM_BASE + 0x100000 with a
 * 0x1000 stride per pin (GPIOMolokai.c block desc: aTiles[0] = 0x100000, nPinRegWidth =
 * 0x1000, 204 pins; HALgpioCore.c. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TLMM_BLOCK_PHYS UINT64_C(0x0f000000)
#define TLMM_TILE0_OFFSET 0x100000u

#define TLMM_PHYS (TLMM_BLOCK_PHYS + TLMM_TILE0_OFFSET)
#define TLMM_BYTES 0x100000u
#define TLMM_PIN_CNT 204u
#define TLMM_PIN_STRIDE 0x1000u
#define TLMM_CFG_EGPIO_PRESENT 0x800u
#define TLMM_CFG_EGPIO_ENABLE 0x1000u
#define TLMM_CTL(pin) ((uint32_t)(pin) * TLMM_PIN_STRIDE)
#define TLMM_IO(pin) (TLMM_CTL(pin) + 4u)
#define SPI5_GPIO_FIRST 48u
#define SPI5_GPIO_LAST 51u

/* provider ownership remains mandatory for each pin; this allowlist does not confer
 * ownership. */

typedef struct tlmm_gpio {
    uint32_t pin;
    uint32_t state;
    uint32_t function;
    uint32_t direction;
    uint32_t pull;
    uint32_t drive;
} tlmm_gpio_t;

typedef struct tlmm_io {
    void* ctx;
    int (*lock)(void*);
    void (*unlock)(void*);
    int (*owns_pin)(void*, uint32_t pin);
    int (*read32)(void*, uint32_t offset, uint32_t* value);
    int (*write32)(void*, uint32_t offset, uint32_t value);
    void (*barrier)(void*);
} tlmm_io_t;

typedef enum tlmm_result {
    TLMM_OK = 0,
    TLMM_INVALID,
    TLMM_NOT_OWNED,
    TLMM_IO_FAILED,
    TLMM_VERIFY_FAILED,
    TLMM_ROLLBACK_FAILED
} tlmm_result_t;

/* populate the provider request in the exact Qualcomm ACPI order: [PinNumber, GPIOState,
 * FunctionSelect, Direction, PullValue, DriveStrength]. */

int tlmm_decode_dsd_t_fields(const uint32_t fields[6], tlmm_gpio_t* config);
/* integration must confirm the Molokai pin tile selection; DSDT _CRS and a PCI/ACPI res
 * assignment are NOT implicit ownership. */

/* still requires provider-owned TLMM MMIO and does not imply device/PoFx ownership. */

struct power_action;
tlmm_result_t tlmm_apply_power_action(const tlmm_io_t* io, const struct power_action* action);
tlmm_result_t tlmm_apply(const tlmm_io_t* io, const tlmm_gpio_t* config);
#ifdef __cplusplus
}
#endif
#endif
