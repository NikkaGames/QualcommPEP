#include "tlmm.h"
#include "state_txn.h"

static int supported_pin(uint32_t p)
{
    if (p >= SPI5_GPIO_FIRST && p <= SPI5_GPIO_LAST)
        return 1;
    switch (p) {
        case 52u:
        case 53u:
        case 55u:
        case 64u:
        case 65u:
        case 68u:
        case 69u:
        case 86u:
        case 129u:
        case 161u:
        case 164u:
            return 1;
        default:
            return 0;
    }
}

static int valid_config(const tlmm_gpio_t* c)
{
    return c && supported_pin(c->pin) && c->pin < TLMM_PIN_CNT && c->state <= 1u &&
           c->function <= 15u && c->direction <= 1u && c->pull <= 3u && c->drive <= 7u &&
           TLMM_IO(c->pin) < TLMM_BYTES;
}

int tlmm_decode_dsd_t_fields(const uint32_t fields[6], tlmm_gpio_t* config)
{
    tlmm_gpio_t tmp;
    if (!fields || !config)
        return 0;
    tmp.pin = fields[0];
    tmp.state = fields[1];
    tmp.function = fields[2];
    tmp.direction = fields[3];
    tmp.pull = fields[4];
    tmp.drive = fields[5];
    if (!valid_config(&tmp))
        return 0;
    *config = tmp;
    return 1;
}

tlmm_result_t tlmm_apply(const tlmm_io_t* io, const tlmm_gpio_t* config)
{
    uint32_t offset, prev_ctl, prev_io, ctl, output, v;
    uint32_t cfg_mask = 0x3ffu;
    tlmm_result_t result = TLMM_IO_FAILED;
    int started = 0;
    if (!io || !config || !io->lock || !io->unlock || !io->owns_pin || !io->read32 ||
        !io->write32 || !io->barrier || !valid_config(config))
        return TLMM_INVALID;
    if (!io->lock(io->ctx))
        return TLMM_NOT_OWNED;
    /* the pin may have been revoked while waiting for the controller lock. */
    if (!io->owns_pin(io->ctx, config->pin)) {
        result = TLMM_NOT_OWNED;
        goto done;
    }
    offset = TLMM_CTL(config->pin);
    if (!io->read32(io->ctx, offset, &prev_ctl) || !io->read32(io->ctx, offset + 4u, &prev_io))
        goto done;
    ctl = (prev_ctl & ~cfg_mask) | config->pull | (config->function << 2) | (config->drive << 6) |
          (config->direction << 9);

    if (prev_ctl & TLMM_CFG_EGPIO_PRESENT)
        ctl |= TLMM_CFG_EGPIO_ENABLE;
    output = (prev_io & ~2u) | (config->state << 1);

    /* a pin can be removed independently of the current MMIO lock. never commence a write
     * after detecting that Windows revoked it. */

    if (!io->owns_pin(io->ctx, config->pin)) {
        result = TLMM_NOT_OWNED;
        goto done;
    }
    started = 1;
    if (!io->write32(io->ctx, offset + 4u, output))
        goto rollback;
    if (!io->owns_pin(io->ctx, config->pin)) {
        result = TLMM_ROLLBACK_FAILED;
        goto done;
    }
    if (!io->write32(io->ctx, offset, ctl))
        goto rollback;
    if (!io->owns_pin(io->ctx, config->pin)) {
        result = TLMM_ROLLBACK_FAILED;
        goto done;
    }
    io->barrier(io->ctx);
    if (!io->read32(io->ctx, offset, &v))
        goto rollback;
    if ((v & cfg_mask) != (ctl & cfg_mask)) {
        result = TLMM_VERIFY_FAILED;
        goto rollback;
    }
    if (!io->read32(io->ctx, offset + 4u, &v))
        goto rollback;
    if ((v & 2u) != (output & 2u)) {
        result = TLMM_VERIFY_FAILED;
        goto rollback;
    }
    if (!io->owns_pin(io->ctx, config->pin)) {
        result = TLMM_ROLLBACK_FAILED;
        goto done;
    }
    result = TLMM_OK;
    goto done;
rollback:
    if (started) {

        if (!io->owns_pin(io->ctx, config->pin)) {
            result = TLMM_ROLLBACK_FAILED;
            goto done;
        }
        if (!io->write32(io->ctx, offset, prev_ctl) || !io->owns_pin(io->ctx, config->pin) ||
            !io->write32(io->ctx, offset + 4u, prev_io) || !io->owns_pin(io->ctx, config->pin)) {
            result = TLMM_ROLLBACK_FAILED;
        } else {
            io->barrier(io->ctx);
            if (!io->read32(io->ctx, offset, &v) || v != prev_ctl ||
                !io->read32(io->ctx, offset + 4u, &v) || v != prev_io ||
                !io->owns_pin(io->ctx, config->pin))
                result = TLMM_ROLLBACK_FAILED;
        }
    }
done:
    io->unlock(io->ctx);
    return result;
}

tlmm_result_t tlmm_apply_power_action(const tlmm_io_t* io, const power_action_t* action)
{
    tlmm_gpio_t config;
    if (!action || action->kind != POWER_TLMM_GPIO ||
        !tlmm_decode_dsd_t_fields(action->u.gpio.fields, &config))
        return TLMM_INVALID;
    return tlmm_apply(io, &config);
}
