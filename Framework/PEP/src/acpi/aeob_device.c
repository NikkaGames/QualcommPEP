/* read-only, zero-allocation device/DSTATE selector for Qualcomm aeo_b BSRC. intentionally
 * never submits votes or executes ACPI methods. */

#include "aeob.h"
#include <string.h>

#define AEOB_HEADER_SIZE 12u

typedef struct aeob_span {
    uint16_t type;
    uint16_t length;
    size_t start;
    size_t payload;
    size_t end;
    size_t next;
} aeob_span_t;

static uint16_t read16(const unsigned char* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_int(const unsigned char* p, uint16_t length)
{
    uint32_t v = 0;
    unsigned i;
    if (length > 4) {
        for (i = 4; i < length; ++i)
            if (p[i] != 0)
                return UINT32_MAX;
        length = 4;
    }
    for (i = 0; i < length; i++)
        v |= (uint32_t)p[i] << (8u * i);
    return v;
}

static int next_span(const unsigned char* bytes, size_t limit, size_t* cursor, aeob_span_t* s)
{
    size_t remaining, pad;
    if (!cursor || !s || *cursor > limit)
        return 0;
    remaining = limit - *cursor;
    if (remaining < 4u)
        return 0;
    s->start = *cursor;
    s->type = read16(bytes + *cursor);
    s->length = read16(bytes + *cursor + 2u);
    s->payload = *cursor + 4u;
    pad = s->length < 4u ? 4u : s->length;
    if (pad > limit - s->payload)
        return 0;
    s->end = s->payload + s->length;
    s->next = s->payload + pad;
    *cursor = s->next;
    return 1;
}

static int is_text(const unsigned char* b, aeob_span_t s, const char* text)
{
    const size_t n = strlen(text);
    return s.type == 1 && s.length == n + 1u && memcmp(b + s.payload, text, n) == 0 &&
           b[s.payload + n] == 0;
}

static int copy_kind(const unsigned char* b, aeob_span_t s, char* out, size_t cap)
{
    size_t n;
    if (s.type != 1 || s.length < 2 || b[s.end - 1u] != 0)
        return 0;
    n = s.length - 1u;
    if (n >= cap)
        return 0;
    memcpy(out, b + s.payload, n);
    out[n] = 0;
    return 1;
}

static aeob_status_t enumerate_actions(const unsigned char* b, aeob_span_t state,
                                       aeob_action_visitor_t visit, void* ctx, uint32_t* cnt)
{
    size_t cur = state.payload;
    aeob_span_t e;
    uint32_t actions = 0;

    if (!next_span(b, state.end, &cur, &e) || !next_span(b, state.end, &cur, &e))
        return AEO_B_INVALID;
    while (cur < state.end) {
        aeob_span_t action, kind, params;
        aeob_action_t entry;
        size_t pos;
        if (!next_span(b, state.end, &cur, &action) || action.type != 3)
            return AEO_B_INVALID;
        pos = action.payload;
        if (!next_span(b, action.end, &pos, &kind))
            return AEO_B_INVALID;
        memset(&entry, 0, sizeof(entry));
        if (!copy_kind(b, kind, entry.kind, sizeof(entry.kind)))
            return AEO_B_INVALID;
        if (!next_span(b, action.end, &pos, &params) || params.type != 3 || pos != action.end)
            return AEO_B_INVALID;
        entry.arguments = b + params.payload;
        entry.argument_bytes = (uint32_t)params.length;
        if (visit && !visit(&entry, ctx))
            return AEO_B_CALLBACK_ERROR;
        ++actions;
    }
    *cnt = actions;
    return AEO_B_OK;
}

/* standalone Qualcomm BSRC AeoBs may instead place DEVICE packages directly at output-buf
 * top level. Accept both, but never recursively search inside arbitrary res payloads. */

static int match_dev(const unsigned char* b, aeob_span_t candidate, const char* path)
{
    aeob_span_t kind, name;
    size_t pos;
    if (candidate.type != 3)
        return 0;
    pos = candidate.payload;
    if (!next_span(b, candidate.end, &pos, &kind) || !is_text(b, kind, "DEVICE"))
        return 0;
    if (!next_span(b, candidate.end, &pos, &name))
        return -1;
    /* its meaning is not established, so it is skipped and never relied on. The DSDT and the
     * other peers omit it entirely, so it must stay optional. */

    if (name.type == 0u && (name.length == 2u || name.length == 4u || name.length == 8u)) {
        if (!next_span(b, candidate.end, &pos, &name))
            return -1;
    }
    return is_text(b, name, path) ? 1 : 0;
}

aeob_status_t aeo_b_visit_dev_d_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                      uint32_t state, aeob_action_visitor_t visit, void* ctx,
                                      uint32_t* action_cnt)
{
    const unsigned char* b = (const unsigned char*)buf;
    size_t length, cur;
    aeob_status_t st;
    aeob_span_t found_dev = {0}, matched_state = {0}, top;
    uint32_t matches = 0;
    if (action_cnt)
        *action_cnt = 0;
    if (!buf || !acpi_path || !action_cnt || state > 3u || strlen(acpi_path) < 5u ||
        strlen(acpi_path) > 255u)
        return AEO_B_INVALID;
    uint32_t top_cnt = 0;
    st = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (st != AEO_B_OK)
        return st;
    if (top_cnt == 0u)
        return AEO_B_NOT_FOUND;
    length = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < length) {
        aeob_span_t first;
        size_t pos;
        int matched;
        if (!next_span(b, length, &cur, &top))
            return AEO_B_INVALID;
        if (top.type != 3)
            continue;
        matched = match_dev(b, top, acpi_path);
        if (matched < 0)
            return AEO_B_INVALID;
        if (matched == 1) {
            found_dev = top;
            if (++matches > 1u)
                return AEO_B_AMBIGUOUS;
            continue;
        }

        pos = top.payload;
        if (!next_span(b, top.end, &pos, &first) || first.type != 3)
            continue;
        pos = top.payload;
        while (pos < top.end) {
            aeob_span_t candidate;
            if (!next_span(b, top.end, &pos, &candidate) || candidate.type != 3)
                return AEO_B_INVALID;
            matched = match_dev(b, candidate, acpi_path);
            if (matched < 0)
                return AEO_B_INVALID;
            if (matched == 1) {
                found_dev = candidate;
                if (++matches > 1u)
                    return AEO_B_AMBIGUOUS;
            }
        }
    }
    if (matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = found_dev.payload;
    if (!next_span(b, found_dev.end, &cur, &top) || !next_span(b, found_dev.end, &cur, &top))
        return AEO_B_INVALID;
    matches = 0;
    while (cur < found_dev.end) {
        aeob_span_t section, kind, value;
        size_t pos;
        if (!next_span(b, found_dev.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &kind) || !is_text(b, kind, "DSTATE"))
            continue;
        if (!next_span(b, section.end, &pos, &value) || value.type != 0 ||
            (value.length != 2u && value.length != 4u && value.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + value.payload, value.length) == state) {
            matched_state = section;
            if (++matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }

    if (matches != 1u)
        return AEO_B_NOT_FOUND;
    return enumerate_actions(b, matched_state, visit, ctx, action_cnt);
}

/* the caller must not use a successful parse as authorization to take PEP ownership or
 * vote hardware. */

aeob_status_t aeo_b_visit_comp_f_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                       uint32_t comp_arg, uint32_t fstate,
                                       aeob_action_visitor_t visit, void* ctx, uint32_t* action_cnt)
{
    const unsigned char* b = (const unsigned char*)buf;
    aeob_span_t outer, dev = {0}, comp = {0}, selected = {0};
    aeob_status_t status;
    size_t size, cur;
    unsigned dev_matches = 0, comp_matches = 0, state_matches = 0;
    if (action_cnt)
        *action_cnt = 0;
    if (!b || !acpi_path || !action_cnt || comp_arg >= 32u || fstate >= 32u ||
        strlen(acpi_path) < 5u || strlen(acpi_path) > 255u)
        return AEO_B_INVALID;
    uint32_t top_cnt = 0;
    status = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (status != AEO_B_OK)
        return status;
    if (top_cnt == 0u)
        return AEO_B_NOT_FOUND;
    size = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < size) {
        size_t pos;
        aeob_span_t first;
        int match;
        if (!next_span(b, size, &cur, &outer))
            return AEO_B_INVALID;
        if (outer.type != 3u)
            continue;
        match = match_dev(b, outer, acpi_path);
        if (match < 0)
            return AEO_B_INVALID;
        if (match == 1) {
            dev = outer;
            if (++dev_matches > 1u)
                return AEO_B_AMBIGUOUS;
            continue;
        }
        pos = outer.payload;
        if (!next_span(b, outer.end, &pos, &first) || first.type != 3u)
            continue;
        pos = outer.payload;
        while (pos < outer.end) {
            aeob_span_t candidate;
            if (!next_span(b, outer.end, &pos, &candidate) || candidate.type != 3u)
                return AEO_B_INVALID;
            match = match_dev(b, candidate, acpi_path);
            if (match < 0)
                return AEO_B_INVALID;
            if (match == 1) {
                dev = candidate;
                if (++dev_matches > 1u)
                    return AEO_B_AMBIGUOUS;
            }
        }
    }
    if (dev_matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = dev.payload;
    if (!next_span(b, dev.end, &cur, &outer) || !next_span(b, dev.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < dev.end) {
        size_t pos;
        aeob_span_t section, label, number;
        if (!next_span(b, dev.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "COMPONENT"))
            continue;
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == comp_arg) {
            comp = section;
            if (++comp_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (comp_matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = comp.payload;
    if (!next_span(b, comp.end, &cur, &outer) || !next_span(b, comp.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < comp.end) {
        size_t pos;
        aeob_span_t section, label, number;
        if (!next_span(b, comp.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "FSTATE"))
            continue;
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == fstate) {
            selected = section;
            if (++state_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (state_matches != 1u)
        return AEO_B_NOT_FOUND;
    return enumerate_actions(b, selected, visit, ctx, action_cnt);
}

/* never confuse PSTATE with a CPU OPP, a DSTATE, or an FSTATE transition. */

aeob_status_t aeo_b_visit_comp_p_state(const void* buf, size_t avail_bytes, const char* acpi_path,
                                       uint32_t comp_arg, uint32_t set_id, uint32_t pstate,
                                       aeob_action_visitor_t visit, void* ctx, uint32_t* action_cnt)
{
    const unsigned char* b = (const unsigned char*)buf;
    aeob_span_t outer, dev = {0}, comp = {0}, selected_set = {0}, selected_state = {0};
    aeob_status_t status;
    size_t size, cur;
    unsigned dev_matches = 0, comp_matches = 0, set_matches = 0, state_matches = 0;

    int bare_set = 0;
    if (action_cnt)
        *action_cnt = 0;
    if (!b || !acpi_path || !action_cnt || comp_arg >= 32u || set_id >= 32u || pstate >= 32u ||
        strlen(acpi_path) < 5u || strlen(acpi_path) > 255u)
        return AEO_B_INVALID;
    uint32_t top_cnt = 0;
    status = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (status != AEO_B_OK)
        return status;
    if (top_cnt == 0u)
        return AEO_B_NOT_FOUND;
    size = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < size) {
        size_t pos;
        aeob_span_t first;
        int match;
        if (!next_span(b, size, &cur, &outer))
            return AEO_B_INVALID;
        if (outer.type != 3u)
            continue;
        match = match_dev(b, outer, acpi_path);
        if (match < 0)
            return AEO_B_INVALID;
        if (match == 1) {
            dev = outer;
            if (++dev_matches > 1u)
                return AEO_B_AMBIGUOUS;
            continue;
        }
        pos = outer.payload;
        if (!next_span(b, outer.end, &pos, &first) || first.type != 3u)
            continue;
        pos = outer.payload;
        while (pos < outer.end) {
            aeob_span_t candidate;
            if (!next_span(b, outer.end, &pos, &candidate) || candidate.type != 3u)
                return AEO_B_INVALID;
            match = match_dev(b, candidate, acpi_path);
            if (match < 0)
                return AEO_B_INVALID;
            if (match == 1) {
                dev = candidate;
                if (++dev_matches > 1u)
                    return AEO_B_AMBIGUOUS;
            }
        }
    }
    if (dev_matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = dev.payload;
    if (!next_span(b, dev.end, &cur, &outer) || !next_span(b, dev.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < dev.end) {
        size_t pos;
        aeob_span_t section, label, number;
        if (!next_span(b, dev.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "COMPONENT"))
            continue;
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == comp_arg) {
            comp = section;
            if (++comp_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (comp_matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = comp.payload;
    if (!next_span(b, comp.end, &cur, &outer) || !next_span(b, comp.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < comp.end) {
        aeob_span_t section, label, number;
        size_t pos;
        if (!next_span(b, comp.end, &cur, &section) || section.type != 3u)
            return AEO_B_INVALID;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "PSTATE_SET"))
            continue;
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == set_id) {
            selected_set = section;
            if (++set_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (set_matches != 1u) {

        if (set_matches != 0u || set_id != 0u)
            return AEO_B_NOT_FOUND;
        selected_set = comp;
        bare_set = 1;
    }
    cur = selected_set.payload;
    if (!next_span(b, selected_set.end, &cur, &outer) ||
        !next_span(b, selected_set.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < selected_set.end) {
        aeob_span_t section, label, number;
        size_t pos;
        if (!next_span(b, selected_set.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u) {
            if (bare_set)
                continue;
            return AEO_B_INVALID;
        }
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "PSTATE")) {
            if (bare_set)
                continue;
            return AEO_B_INVALID;
        }
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == pstate) {
            selected_state = section;
            if (++state_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (state_matches != 1u)
        return AEO_B_NOT_FOUND;
    return enumerate_actions(b, selected_state, visit, ctx, action_cnt);
}

/* a component on an otherwise d0/d3-only device must still block ownership. */

aeob_status_t aeo_b_inventory_dev_components(const void* buf, size_t avail_bytes,
                                             const char* acpi_path, uint32_t* comp_mask,
                                             uint32_t comp_f_states[32],
                                             uint32_t comp_pstate_sets[32],
                                             uint32_t* crashdump_state)
{
    const unsigned char* b = (const unsigned char*)buf;
    aeob_span_t top, dev = {0};
    aeob_status_t st;
    size_t length, cur;
    uint32_t components = 0, seen_devices = 0, crashdump = UINT32_MAX;
    uint32_t collected_f_states[32] = {0}, collected_p_states[32] = {0};

    uint32_t bare_states = 0u, bare_seen = 0u;
    if (comp_mask)
        *comp_mask = 0;
    if (comp_f_states)
        memset(comp_f_states, 0, 32u * sizeof(uint32_t));
    if (comp_pstate_sets)
        memset(comp_pstate_sets, 0, 32u * sizeof(uint32_t));
    if (crashdump_state)
        *crashdump_state = UINT32_MAX;
    if (!b || !acpi_path || !comp_mask || !comp_f_states || !comp_pstate_sets || !crashdump_state ||
        strlen(acpi_path) < 5u || strlen(acpi_path) > 255u)
        return AEO_B_INVALID;
    uint32_t top_cnt = 0;
    st = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (st != AEO_B_OK)
        return st;
    if (top_cnt == 0u)
        return AEO_B_NOT_FOUND;
    length = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < length) {
        aeob_span_t first;
        size_t pos;
        int matched;
        if (!next_span(b, length, &cur, &top))
            return AEO_B_INVALID;
        if (top.type != 3u)
            continue;
        matched = match_dev(b, top, acpi_path);
        if (matched < 0)
            return AEO_B_INVALID;
        if (matched == 1) {
            dev = top;
            if (++seen_devices > 1u)
                return AEO_B_AMBIGUOUS;
            continue;
        }
        pos = top.payload;
        if (!next_span(b, top.end, &pos, &first) || first.type != 3u)
            continue;
        pos = top.payload;
        while (pos < top.end) {
            aeob_span_t candidate;
            if (!next_span(b, top.end, &pos, &candidate) || candidate.type != 3u)
                return AEO_B_INVALID;
            matched = match_dev(b, candidate, acpi_path);
            if (matched < 0)
                return AEO_B_INVALID;
            if (matched == 1) {
                dev = candidate;
                if (++seen_devices > 1u)
                    return AEO_B_AMBIGUOUS;
            }
        }
    }
    if (seen_devices != 1u)
        return AEO_B_NOT_FOUND;
    cur = dev.payload;
    if (!next_span(b, dev.end, &cur, &top) || !next_span(b, dev.end, &cur, &top))
        return AEO_B_INVALID;
    while (cur < dev.end) {
        aeob_span_t section, kind, id;
        size_t pos, sub;
        uint32_t comp, states = 0, psets = 0;
        if (!next_span(b, dev.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &kind))
            return AEO_B_INVALID;
        if (is_text(b, kind, "CRASHDUMP_DSTATE")) {
            uint32_t state;
            if (crashdump != UINT32_MAX || !next_span(b, section.end, &pos, &id) || id.type != 0u ||
                (id.length != 2u && id.length != 4u && id.length != 8u) || pos != section.end)
                return AEO_B_INVALID;
            state = read_int(b + id.payload, id.length);
            if (state > 3u)
                return AEO_B_INVALID;
            crashdump = state;
            continue;
        }
        if (!is_text(b, kind, "COMPONENT"))
            continue;
        if (!next_span(b, section.end, &pos, &id) || id.type != 0u ||
            (id.length != 2u && id.length != 4u && id.length != 8u))
            return AEO_B_INVALID;
        comp = read_int(b + id.payload, id.length);
        if (comp >= 32u || (components & (1u << comp)))
            return AEO_B_AMBIGUOUS;
        bare_states = 0u;
        bare_seen = 0u;
        while (pos < section.end) {
            aeob_span_t fs, label, num;
            uint32_t state, n = 0;
            int bare;
            if (!next_span(b, section.end, &pos, &fs) || fs.type != 3u)
                return AEO_B_INVALID;
            sub = fs.payload;
            if (!next_span(b, fs.end, &sub, &label))
                return AEO_B_INVALID;

            /* discovery carries a res identity, not explicit state selectors. The clock-discovery
             * owner resolves its BSP freq catalog separately. */

            if (is_text(b, label, "DISCOVERABLE_PSTATE")) {
                aeob_span_t how, res;
                if (!next_span(b, fs.end, &sub, &how) || !is_text(b, how, "CLOCK") ||
                    !next_span(b, fs.end, &sub, &res) || res.type != 1u || res.length < 2u ||
                    res.length > 128u || b[res.end - 1u] != 0u || sub < fs.end)
                    return AEO_B_INVALID;
                continue;
            }
            bare = is_text(b, label, "PSTATE");
            if (!bare && !is_text(b, label, "FSTATE") && !is_text(b, label, "PSTATE_SET"))
                return AEO_B_INVALID;
            if (!next_span(b, fs.end, &sub, &num) || num.type != 0u ||
                (num.length != 2u && num.length != 4u && num.length != 8u))
                return AEO_B_INVALID;
            state = read_int(b + num.payload, num.length);
            if (state >= 32u)
                return AEO_B_AMBIGUOUS;
            if (bare) {

                if (psets & 1u)
                    return AEO_B_AMBIGUOUS;
                if (bare_states & (1u << state))
                    return AEO_B_AMBIGUOUS;

                if (enumerate_actions(b, fs, NULL, NULL, &n) != AEO_B_OK)
                    return AEO_B_INVALID;
                bare_states |= 1u << state;
                bare_seen = 1u;
            } else if (is_text(b, label, "FSTATE")) {
                if (states & (1u << state))
                    return AEO_B_AMBIGUOUS;
                if (enumerate_actions(b, fs, NULL, NULL, &n) != AEO_B_OK)
                    return AEO_B_INVALID;
                states |= 1u << state;
            } else {
                uint32_t pstate_ids = 0;
                if (psets & (1u << state))
                    return AEO_B_AMBIGUOUS;
                while (sub < fs.end) {
                    aeob_span_t one, pname, pnumber;
                    size_t x;
                    uint32_t pnumber_value;
                    if (!next_span(b, fs.end, &sub, &one) || one.type != 3u)
                        return AEO_B_INVALID;
                    x = one.payload;
                    if (!next_span(b, one.end, &x, &pname) || !is_text(b, pname, "PSTATE") ||
                        !next_span(b, one.end, &x, &pnumber) || pnumber.type != 0u ||
                        (pnumber.length != 2u && pnumber.length != 4u && pnumber.length != 8u))
                        return AEO_B_INVALID;
                    pnumber_value = read_int(b + pnumber.payload, pnumber.length);
                    if (pnumber_value >= 32u || (pstate_ids & (1u << pnumber_value)))
                        return AEO_B_AMBIGUOUS;
                    if (enumerate_actions(b, one, NULL, NULL, &n) != AEO_B_OK)
                        return AEO_B_INVALID;
                    pstate_ids |= 1u << pnumber_value;
                }
                if (!pstate_ids)
                    return AEO_B_INVALID;
                if (state == 0u && bare_seen)
                    return AEO_B_AMBIGUOUS;
                psets |= 1u << state;
            }
        }
        if (bare_seen)
            psets |= 1u;
        if (!states && !psets)
            return AEO_B_INVALID;
        components |= 1u << comp;
        collected_f_states[comp] = states;
        collected_p_states[comp] = psets;
    }
    memcpy(comp_f_states, collected_f_states, sizeof(collected_f_states));
    memcpy(comp_pstate_sets, collected_p_states, sizeof(collected_p_states));
    *comp_mask = components;
    *crashdump_state = crashdump;
    return AEO_B_OK;
}

static int read_dev_path(const unsigned char* b, aeob_span_t candidate, char path[256])
{
    aeob_span_t kind, name;
    size_t pos, i;
    if (candidate.type != 3u)
        return 0;
    pos = candidate.payload;
    if (!next_span(b, candidate.end, &pos, &kind) || !is_text(b, kind, "DEVICE"))
        return 0;
    if (!next_span(b, candidate.end, &pos, &name))
        return -1;
    /* both must accept it: the device inventory selects with match_dev and the path list reads
     * it here, so widening only one makes the desc fail later. */

    if (name.type == 0u && (name.length == 2u || name.length == 4u || name.length == 8u)) {
        if (!next_span(b, candidate.end, &pos, &name))
            return -1;
    }
    if (name.type != 1u || name.length < 6u || name.length > 256u || b[name.payload] != '\\' ||
        b[name.end - 1u] != 0u)
        return -1;
    for (i = 0; i + 1u < name.length; ++i)
        if (b[name.payload + i] == 0u)
            return -1;
    memcpy(path, b + name.payload, name.length);
    return 1;
}

static int add_dev_path(char* paths, uint32_t capacity, uint32_t* cnt, const char name[256])
{
    uint32_t i;
    for (i = 0; i < *cnt; ++i)
        if (!strcmp(paths + i * 256u, name))
            return 0;
    if (*cnt >= capacity)
        return 0;
    memcpy(paths + *cnt * 256u, name, strlen(name) + 1u);
    ++*cnt;
    return 1;
}

aeob_status_t aeo_b_list_dev_paths(const void* buf, size_t avail_bytes, char* paths,
                                   uint32_t capacity, uint32_t* cnt)
{
    const unsigned char* b = (const unsigned char*)buf;
    aeob_status_t st;
    aeob_span_t top;
    uint32_t found = 0;
    size_t length, cur;
    if (cnt)
        *cnt = 0;
    if (!buf || !paths || !capacity || capacity > 32u || !cnt)
        return AEO_B_INVALID;
    uint32_t top_cnt = 0;
    st = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (st != AEO_B_OK)
        return st;
    if (top_cnt == 0u)
        return AEO_B_OK;
    length = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < length) {
        int match;
        char name[256] = {0};
        size_t pos;
        aeob_span_t first;
        if (!next_span(b, length, &cur, &top))
            return AEO_B_INVALID;
        if (top.type != 3u)
            continue;
        match = read_dev_path(b, top, name);
        if (match < 0 || (match == 1 && !add_dev_path(paths, capacity, &found, name)))
            return AEO_B_INVALID;
        if (match == 1)
            continue;
        pos = top.payload;
        if (!next_span(b, top.end, &pos, &first) || first.type != 3u)
            continue;
        pos = top.payload;
        while (pos < top.end) {
            aeob_span_t child;
            if (!next_span(b, top.end, &pos, &child) || child.type != 3u)
                return AEO_B_INVALID;
            memset(name, 0, sizeof(name));
            match = read_dev_path(b, child, name);
            if (match < 0 || (match == 1 && !add_dev_path(paths, capacity, &found, name)))
                return AEO_B_INVALID;
        }
    }
    *cnt = found;
    return AEO_B_OK;
}

aeob_status_t aeo_b_discover_comp_clock(const void* buf, size_t avail_bytes, const char* acpi_path,
                                        uint32_t comp_arg, char clock_name[128])
{
    const unsigned char* b = buf;
    aeob_span_t outer, dev = {0}, comp = {0};
    aeob_status_t status;
    size_t size, cur;
    unsigned dev_matches = 0, comp_matches = 0, discoveries = 0;
    uint32_t mask, fs[32], ps[32], crash;
    if (!clock_name)
        return AEO_B_INVALID;
    clock_name[0] = 0;
    if (comp_arg >= 32u)
        return AEO_B_INVALID;
    status = aeo_b_inventory_dev_components(buf, avail_bytes, acpi_path, &mask, fs, ps, &crash);
    if (status != AEO_B_OK)
        return status;
    uint32_t top_cnt = 0;
    status = aeo_b_walk(buf, avail_bytes, NULL, NULL, &top_cnt);
    if (status != AEO_B_OK)
        return status;
    if (top_cnt == 0u)
        return AEO_B_NOT_FOUND;
    size = (size_t)b[4] | ((size_t)b[5] << 8) | ((size_t)b[6] << 16) | ((size_t)b[7] << 24);
    cur = AEOB_HEADER_SIZE;
    while (cur < size) {
        size_t pos;
        aeob_span_t first;
        int match;
        if (!next_span(b, size, &cur, &outer))
            return AEO_B_INVALID;
        if (outer.type != 3u)
            continue;
        match = match_dev(b, outer, acpi_path);
        if (match < 0)
            return AEO_B_INVALID;
        if (match == 1) {
            dev = outer;
            if (++dev_matches > 1u)
                return AEO_B_AMBIGUOUS;
            continue;
        }
        pos = outer.payload;
        if (!next_span(b, outer.end, &pos, &first) || first.type != 3u)
            continue;
        pos = outer.payload;
        while (pos < outer.end) {
            aeob_span_t candidate;
            if (!next_span(b, outer.end, &pos, &candidate) || candidate.type != 3u)
                return AEO_B_INVALID;
            match = match_dev(b, candidate, acpi_path);
            if (match < 0)
                return AEO_B_INVALID;
            if (match == 1) {
                dev = candidate;
                if (++dev_matches > 1u)
                    return AEO_B_AMBIGUOUS;
            }
        }
    }
    if (dev_matches != 1u)
        return AEO_B_NOT_FOUND;
    cur = dev.payload;
    if (!next_span(b, dev.end, &cur, &outer) || !next_span(b, dev.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < dev.end) {
        size_t pos;
        aeob_span_t section, label, number;
        if (!next_span(b, dev.end, &cur, &section))
            return AEO_B_INVALID;
        if (section.type != 3u)
            continue;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "COMPONENT"))
            continue;
        if (!next_span(b, section.end, &pos, &number) || number.type != 0u ||
            (number.length != 2u && number.length != 4u && number.length != 8u))
            return AEO_B_INVALID;
        if (read_int(b + number.payload, number.length) == comp_arg) {
            comp = section;
            if (++comp_matches > 1u)
                return AEO_B_AMBIGUOUS;
        }
    }
    if (comp_matches != 1u)
        return AEO_B_NOT_FOUND;

    cur = comp.payload;
    if (!next_span(b, comp.end, &cur, &outer) || !next_span(b, comp.end, &cur, &outer))
        return AEO_B_INVALID;
    while (cur < comp.end) {
        aeob_span_t section, label, kind, name;
        size_t pos;
        if (!next_span(b, comp.end, &cur, &section) || section.type != 3u)
            return AEO_B_INVALID;
        pos = section.payload;
        if (!next_span(b, section.end, &pos, &label))
            return AEO_B_INVALID;
        if (!is_text(b, label, "DISCOVERABLE_PSTATE"))
            continue;
        if (++discoveries != 1u) {
            clock_name[0] = 0;
            return AEO_B_AMBIGUOUS;
        }
        if (!next_span(b, section.end, &pos, &kind) || !is_text(b, kind, "CLOCK") ||
            !next_span(b, section.end, &pos, &name) || pos != section.end ||
            !copy_kind(b, name, clock_name, 128u)) {
            clock_name[0] = 0;
            return AEO_B_INVALID;
        }
    }
    return discoveries == 1u ? AEO_B_OK : AEO_B_NOT_FOUND;
}
