#include "gpio.h"
#include <string.h>
/* keep the binary ABI independent of compiler padding/alignment. */
typedef char pmic_gpio_abi_must_be_28_bytes_t[(sizeof(pm_gpio_digital_output_t) == 28u) ? 1 : -1];

static uint32_t read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static void write32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8u);
    p[2] = (uint8_t)(v >> 16u);
    p[3] = (uint8_t)(v >> 24u);
}

int pm_gpio_decode_digital_output(const void* buf, size_t bytes, pm_gpio_digital_output_t* decoded)
{
    uint32_t v[7];
    const uint8_t* p = (const uint8_t*)buf;
    size_t i;
    if (!p || !decoded || bytes != PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES)
        return 0;
    for (i = 0; i < 7u; ++i)
        v[i] = read32(p + i * 4u);
    decoded->pmic_number = v[0];
    decoded->gpio = v[1];
    decoded->out_buf_config = v[2];
    decoded->voltage_src = v[3];
    decoded->src = v[4];
    decoded->out_buf_strength = v[5];
    decoded->i_src_pulls = v[6];
    return 1;
}

int pm_gpio_encode_digital_output(const pm_gpio_digital_output_t* decoded, void* buf, size_t bytes)
{
    uint32_t v[7];
    uint8_t* p = (uint8_t*)buf;
    size_t i;
    if (!p || !decoded || bytes != PM_GPIO_CONFIG_DIGITAL_OUTPUT_BYTES)
        return 0;
    v[0] = decoded->pmic_number;
    v[1] = decoded->gpio;
    v[2] = decoded->out_buf_config;
    v[3] = decoded->voltage_src;
    v[4] = decoded->src;
    v[5] = decoded->out_buf_strength;
    v[6] = decoded->i_src_pulls;
    for (i = 0; i < 7u; ++i)
        write32(p + i * 4u, v[i]);
    return 1;
}

typedef char
    pmic_gpio_query_abi_must_be_12_bytes_t[(sizeof(pm_gpio_state_query_t) == 12u) ? 1 : -1];

int pm_gpio_encode_state_query(const pm_gpio_state_query_t* query, void* out, size_t bytes)
{
    uint8_t* p = (uint8_t*)out;
    if (!query || !p || bytes != PM_GPIO_GET_GPIO_STATE_INPUT_BYTES)
        return 0;
    write32(p, query->pmic_number);
    write32(p + 4u, query->gpio);
    write32(p + 8u, query->write_configured_pins);
    return 1;
}

int pm_gpio_decode_raw_state(const void* wire, size_t bytes, pm_gpio_state_raw_t* out)
{
    if (!wire || !out || bytes != PM_GPIO_GET_GPIO_STATE_OUTPUT_BYTES)
        return 0;
    memcpy(out->bytes, wire, bytes);
    return 1;
}

int pm_gpio_decode_state_query(const void* wire, size_t bytes, pm_gpio_state_query_t* out)
{
    const uint8_t* p = (const uint8_t*)wire;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!p || bytes != PM_GPIO_GET_GPIO_STATE_INPUT_BYTES)
        return 0;
    out->pmic_number = read32(p);
    out->gpio = read32(p + 4u);
    out->write_configured_pins = read32(p + 8u);
    return 1;
}
