#include "provider_select.h"

int is_unique_interface_multi_sz(const uint16_t* data, size_t units)
{
    size_t i;
    if (!data || units < 3u || units > 32768u || data[0] == 0u)
        return 0;
    /* a second live interface is ambiguous: never submit a PMIC write to whichever enumeration
     * entry happens to be first. */

    for (i = 0u; i < units; ++i) {
        if (data[i] == 0u) {
            return i > 0u && i + 2u == units && data[i + 1u] == 0u;
        }
    }
    return 0;
}
