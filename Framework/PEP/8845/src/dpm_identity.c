#include "dpm_identity.h"
#include <string.h>

static unsigned char upper_ascii(unsigned char c)
{
    if (c >= 'a' && c <= 'z')
        return (unsigned char)(c - 'a' + 'A');
    return c;
}

static int eq_path_ascii(const char* left, const char* right)
{
    size_t i;
    if (!left || !right)
        return 0;
    for (i = 0; i < 256u; ++i) {
        const unsigned char a = (unsigned char)left[i];
        const unsigned char b = (unsigned char)right[i];
        if (upper_ascii(a) != upper_ascii(b))
            return 0;
        if (!a || !b)
            return a == b;
    }
    return 0;
}

static int acpi_path(const char* path)
{
    size_t i = 0, seg = 0;
    if (!path || path[i++] != '\\')
        return 0;
    for (;; ++i) {
        unsigned char ch;
        if (i == 256u)
            return 0;
        ch = (unsigned char)path[i];
        if (ch == '.' || !ch) {
            if (seg == 0u || seg > 4u)
                return 0;
            if (!ch)
                return 1;
            seg = 0;
            continue;
        }
        ch = upper_ascii(ch);
        if ((ch < 'A' || ch > 'Z') && (ch < '0' || ch > '9') && ch != '_')
            return 0;
        if (seg == 0 && (ch >= '0' && ch <= '9'))
            return 0;
        if (++seg > 4u)
            return 0;
    }
}

int resolve_pep_dev_path(const res_graph_t* graph, const uint16_t* unicode_id, size_t length_bytes,
                         char out_path[256])
{
    size_t i, n;
    char candidate[256];
    uint32_t match = GRAPH_MAX_DEVICES;
    if (out_path)
        out_path[0] = '\0';
    if (!out_path || !unicode_id || !graph || !graph->sealed || graph->retired || !length_bytes ||
        (length_bytes & 1u) || length_bytes >= 512u || graph->dev_cnt > GRAPH_MAX_DEVICES)
        return 0;
    n = length_bytes / 2u;
    for (i = 0; i < n; ++i) {
        const uint16_t c = unicode_id[i];
        if (!c || c > 0x7fu)
            return 0;
        candidate[i] = (char)upper_ascii((unsigned char)c);
    }
    candidate[n] = '\0';
    if (!acpi_path(candidate))
        return 0;
    for (i = 0; i < graph->dev_cnt; ++i) {
        if (graph->devices[i].has_d0 && graph->devices[i].has_d3 &&
            eq_path_ascii(candidate, graph->devices[i].path)) {
            if (match != GRAPH_MAX_DEVICES)
                return 0;
            match = (uint32_t)i;
        }
    }
    if (match == GRAPH_MAX_DEVICES)
        return 0;
    memcpy(out_path, graph->devices[match].path, strlen(graph->devices[match].path) + 1u);
    return 1;
}

power_result_t dpm_prepare_unicode(res_graph_t* graph, const uint16_t* id, size_t bytes,
                                   power_execution_t* trace, power_scratch_t* scratch)
{
    char path[256];
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    if (!scratch || !resolve_pep_dev_path(graph, id, bytes, path)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    return graph_prepare_d0(graph, path, trace, scratch);
}

power_result_t dpm_abandon_unicode(res_graph_t* graph, const uint16_t* id, size_t bytes,
                                   power_execution_t* trace, power_scratch_t* scratch)
{
    char path[256];
    if (!trace)
        return POWER_BAD_INPUT;
    memset(trace, 0, sizeof(*trace));
    if (!scratch || !resolve_pep_dev_path(graph, id, bytes, path)) {
        trace->result = POWER_UNSUPPORTED;
        return trace->result;
    }
    return graph_abandon_d3(graph, path, trace, scratch);
}
