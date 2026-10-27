#include "oem_gic_dispatch.h"

static oem_gic_result_t apply_owned_distributor_bit(const oem_gic_io_t* io, uint32_t gic_intid,
                                                    uint32_t base_offset)
{
    uint32_t offset;
    uint32_t bit;
    if (!io || !io->write32 || !io->barrier || gic_intid < 32u || gic_intid > 1019u)
        return OEM_GIC_INVALID;

    offset = base_offset + ((gic_intid >> 5u) * 4u);
    bit = (uint32_t)1u << (gic_intid & 31u);
    if (!io->write32(io->ctx, offset, bit))
        return OEM_GIC_IO_FAILURE;
    io->barrier(io->ctx);
    return OEM_GIC_OK;
}

oem_gic_result_t oem_gic_mask(const oem_gic_io_t* io, uint32_t gic_intid)
{
    return apply_owned_distributor_bit(io, gic_intid, OEM_GICD_ICENABLER);
}

oem_gic_result_t oem_gic_unmask(const oem_gic_io_t* io, uint32_t gic_intid)
{
    return apply_owned_distributor_bit(io, gic_intid, OEM_GICD_ISENABLER);
}
