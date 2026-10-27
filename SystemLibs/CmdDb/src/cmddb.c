#include "cmddb.h"
#include <string.h>

static uint16_t rd16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t* p)
{
    uint64_t v = 0;
    unsigned i;
    for (i = 0; i < 8; ++i)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

int cmd_db_lookup(const uint8_t* db, size_t length, const char* name, cmddb_entry_t* out)
{
    uint64_t wanted = 0;
    size_t name_len = 0;
    unsigned group, i;
    unsigned found = 0;
    cmddb_entry_t candidate = {0};
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!db || !name || length < CMDDB_HEADER_BYTES || length > CMDDB_MAX_BYTES ||
        rd32(db) != CMDDB_VERSION || rd32(db + 4) != CMDDB_MAGIC)
        return 0;
    while (name_len <= 8u && name[name_len])
        ++name_len;
    if (!name_len || name_len > 8u)
        return 0;
    for (i = 0; i < name_len; ++i) {
        if ((unsigned char)name[i] < 0x21 || (unsigned char)name[i] > 0x7e)
            return 0;
        wanted |= (uint64_t)(unsigned char)name[i] << (i * 8u);
    }
    for (group = 0; group < CMDDB_SLV_CNT; ++group) {
        const uint8_t* hdr = db + 8u + 16u * group;
        uint16_t slave = rd16(hdr), head_offset = rd16(hdr + 2), data_offset = rd16(hdr + 4);
        uint16_t cnt = rd16(hdr + 6), version = rd16(hdr + 8);
        size_t data_base = CMDDB_HEADER_BYTES;
        size_t head_base = data_base + head_offset;
        size_t aux_base = data_base + data_offset;
        if (!cnt)
            continue;
        /* slv_ID encoded in RPMh TCS CMDn_ADDR[18:16]. */
        if (slave > 7u || head_base > length || aux_base > length ||
            (size_t)cnt > (length - head_base) / CMDDB_ENTRY_BYTES)
            return 0;
        for (i = 0; i < cnt; ++i) {
            const uint8_t* entry = db + head_base + (size_t)i * CMDDB_ENTRY_BYTES;
            uint32_t address;
            uint16_t len, offset;
            if (rd64(entry) != wanted)
                continue;
            address = rd32(entry + 16);
            len = rd16(entry + 20);
            offset = rd16(entry + 22);
            if (address > 0x7ffffu || ((address >> 16) & 7u) != slave ||
                (size_t)offset > length - aux_base || (size_t)len > length - aux_base - offset)
                return 0;
            /* never take the first matching regulator by accident. */

            if (found)
                return 0;
            found = 1;
            candidate.address = address;
            candidate.priority[0] = rd32(entry + 8);
            candidate.priority[1] = rd32(entry + 12);
            candidate.version = version;
            candidate.aux_data = db + aux_base + offset;
            candidate.aux_length = len;
        }
    }
    if (found)
        *out = candidate;
    return found != 0;
}
