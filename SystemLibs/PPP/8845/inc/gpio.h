#ifndef PMIC_GPIO_H
#define PMIC_GPIO_H
/* this codec does not drive a PMIC; the only valid executor is an actual owned PMIC GPIO
 * provider. */

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PM_GPIO_CONFIG_DIGITAL_OUTPUT_IOCTL 0x80028FA8u
#define PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES 28u

typedef struct pm_gpio_digital_output {
    uint32_t pmic_number;
    uint32_t gpio;
    uint32_t out_buf_config;
    uint32_t voltage_src;
    uint32_t src;
    uint32_t out_buf_strength;
    uint32_t i_src_pulls;
} pm_gpio_digital_output_t;

/* GPIO state IOCTL 0x80024FB4 returns eight opaque bytes, not a digital-output configuration. */

#define PM_GPIO_GET_GPIO_STATE_IOCTL 0x80024FB4u
#define PM_GPIO_GET_GPIO_STATE_INPUT_BYTES 12u
#define PM_GPIO_GET_GPIO_STATE_OUTPUT_BYTES 8u

typedef struct pm_gpio_state_query {
    uint32_t pmic_number;
    uint32_t gpio;
    uint32_t write_configured_pins;
} pm_gpio_state_query_t;

/* preserve opaque bytes until the provider defines their fields. */

typedef struct pm_gpio_state_raw {
    uint8_t bytes[PM_GPIO_GET_GPIO_STATE_OUTPUT_BYTES];
} pm_gpio_state_raw_t;

int pm_gpio_decode_state_query(const void* wire, size_t bytes, pm_gpio_state_query_t* out);
int pm_gpio_encode_state_query(const pm_gpio_state_query_t* query, void* out, size_t bytes);
int pm_gpio_decode_raw_state(const void* wire, size_t bytes, pm_gpio_state_raw_t* out);
/* never invent defaults. */
int pm_gpio_decode_digital_output(const void* buf, size_t bytes, pm_gpio_digital_output_t* decoded);
int pm_gpio_encode_digital_output(const pm_gpio_digital_output_t* decoded, void* buf, size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
