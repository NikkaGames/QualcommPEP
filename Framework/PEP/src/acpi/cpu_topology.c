#include "cpu_topology.h"
#include "aeob.h"
#include <string.h>

static uint32_t read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t read16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static const uint8_t* next(const uint8_t* p)
{
    uint16_t bytes = read16(p + 2u);
    return p + 4u + (bytes < 4u ? 4u : bytes);
}

static int integer(const uint8_t* p, uint32_t* value)
{
    uint16_t size = read16(p + 2u);
    if (read16(p) != 0u || (size == 8u && read32(p + 8u)))
        return 0;
    *value = size == 2u ? read16(p + 4u) : read32(p + 4u);
    return 1;
}

static int row(const uint8_t* p, cpu_topology_t* out)
{
    const uint8_t *end, *arg;
    cpu_identity_t* cpu;
    uint32_t bytes, i;
    if (read16(p) != 3u || out->count == CPU_TOPOLOGY_MAX)
        return 0;
    end = p + 4u + read16(p + 2u);
    arg = p + 4u;
    if (end - arg < 4 || read16(arg) != 1u)
        return 0;
    bytes = read16(arg + 2u);
    if (bytes < 3u || bytes > CPU_PATH_MAX || arg[4] != '\\' || arg[bytes + 3u] != 0u)
        return 0;
    cpu = &out->cpus[out->count];
    for (i = 0u; i < bytes - 1u; ++i) {
        uint8_t c = arg[4u + i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              (i == 0u && c == '\\') || c == '.'))
            return 0;
        cpu->path[i] = (char)c;
    }
    cpu->path[bytes - 1u] = 0;
    arg = next(arg);
    if (end - arg < 8 || !integer(arg, &cpu->domain))
        return 0;
    arg = next(arg);
    if (end - arg < 8 || !integer(arg, &cpu->core) || next(arg) != end)
        return 0;
    for (i = 0u; i < out->count; ++i)
        if (!strcmp(out->cpus[i].path, cpu->path) ||
            (out->cpus[i].domain == cpu->domain && out->cpus[i].core == cpu->core))
            return 0;
    ++out->count;
    return 1;
}

int cpu_topology_decode(const void* buffer, size_t size, cpu_topology_t* out)
{
    const uint8_t *p = buffer, *end;
    uint32_t count;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (aeo_b_walk(buffer, size, NULL, NULL, &count) != AEO_B_OK || !count)
        return 0;
    end = p + read32(p + 4u);
    p += 12u;
    /* Both flattened IOCTL output and a single enclosing ACPI package are valid. */
    if (count == 1u && read16(p) == 3u && read16(p + 2u) >= 4u && read16(p + 4u) == 3u) {
        end = p + 4u + read16(p + 2u);
        p += 4u;
    }
    while (p < end) {
        if (!row(p, out))
            goto fail;
        p = next(p);
    }
    if (p == end && out->count)
        return 1;
fail:
    memset(out, 0, sizeof(*out));
    return 0;
}
