#include <ntddk.h>
#include <stdint.h>
#include <string.h>
#include "oem_rsc_tcs_survey_win.h"

static int permitted(uint32_t off)
{
    uint32_t i;
    if (off == RSC_ID_OFFSET || off == RSC_PARENTCHILD_CONFIG_OFFSET || off == TCS_IRQ_ENABLE ||
        off == TCS_IRQ_STATUS)
        return 1;
    for (i = 0; i < OEM_RSC_SURVEY_TCS_CNT; i++) {
        if (off == TCS_CONTROL(i) || off == TCS_STATUS(i) || off == TCS_ENABLE(i) ||
            off == TCS_WAIT(i))
            return 1;
    }
    return 0;
}

static int read32(void* ctx, volatile void* base, uint32_t off, uint32_t* value)
{
    (void)ctx;
    if (!base || !value || KeGetCurrentIrql() != PASSIVE_LEVEL || (off & 3u) != 0u ||
        off > OEM_RSC_DIRECT_BYTES - 4u || !permitted(off))
        return 0;
    *value = (uint32_t)READ_REGISTER_ULONG((volatile ULONG*)((volatile unsigned char*)base + off));
    return 1;
}

int oem_rsc_win_tcs_survey(oem_rsc_win_map_t* win, oem_rsc_tcs_survey_t* out)
{
    oem_rsc_read_io_t io = {0};
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!win || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return 0;
    io.ctx = win;
    io.read32 = read32;
    return oem_rsc_tcs_survey(&win->state, win->epoch, &io, out);
}
