#include "aeob.h"
#include <string.h>

typedef struct span {
    uint16_t type;
    size_t begin, end;
} span_t;

typedef struct reader {
    const uint8_t* data;
    aeob_preload_visitor_t visit;
    void* ctx;
    aeob_preload_result_t* result;
    uint32_t device_state;
    int has_device_states;
} reader_t;

static int next(reader_t* r, span_t parent, size_t* pos, span_t* out)
{
    const uint8_t* p;
    size_t bytes, padded;
    if (*pos > parent.end || parent.end - *pos < 4u)
        return 0;
    p = r->data + *pos;
    out->type = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    bytes = (size_t)p[2] | ((size_t)p[3] << 8);
    padded = bytes < 4u ? 4u : bytes;
    out->begin = *pos + 4u;
    if (padded > parent.end - out->begin)
        return 0;
    out->end = out->begin + bytes;
    *pos = out->begin + padded;
    return 1;
}

static int text(reader_t* r, span_t s, const char* value)
{
    size_t len = strlen(value) + 1u;
    return s.type == 1u && s.end - s.begin == len && !memcmp(r->data + s.begin, value, len);
}

static int number(reader_t* r, span_t s, uint32_t* out)
{
    size_t i, len = s.end - s.begin;
    uint64_t value = 0u;
    if (s.type || (len != 2u && len != 4u && len != 8u))
        return 0;
    for (i = 0u; i < len; ++i)
        value |= (uint64_t)r->data[s.begin + i] << (8u * i);
    if (value > UINT32_MAX)
        return 0;
    *out = (uint32_t)value;
    return 1;
}

static int label(reader_t* r, span_t s, const char* name, size_t* pos)
{
    span_t first;
    *pos = s.begin;
    return s.type == 3u && next(r, s, pos, &first) && text(r, first, name);
}

/* Search direct children only. Resource parameters cannot introduce selectors. */
static aeob_status_t find(reader_t* r, span_t parent, const char* name, uint32_t id, span_t* out)
{
    size_t pos = parent.begin, inner;
    span_t child, n;
    uint32_t value, count = 0u;
    while (pos < parent.end) {
        if (!next(r, parent, &pos, &child))
            return AEO_B_INVALID;
        if (!label(r, child, name, &inner))
            continue;
        if (id != UINT32_MAX) {
            if (!next(r, child, &inner, &n) || !number(r, n, &value) || value >= 32u)
                return AEO_B_INVALID;
            if (value != id)
                continue;
        }
        *out = child;
        if (++count > 1u)
            return AEO_B_AMBIGUOUS;
    }
    return count ? AEO_B_OK : AEO_B_NOT_FOUND;
}

static aeob_status_t selector(reader_t* r, span_t parent, const char* name, uint32_t limit,
                              uint32_t* value)
{
    span_t selected, n;
    size_t pos;
    aeob_status_t status = find(r, parent, name, UINT32_MAX, &selected);
    *value = UINT32_MAX;
    if (status == AEO_B_NOT_FOUND)
        return AEO_B_OK;
    if (status != AEO_B_OK)
        return status;
    if (!label(r, selected, name, &pos) || !next(r, selected, &pos, &n) || !number(r, n, value) ||
        pos != selected.end)
        return AEO_B_INVALID;
    if (*value == 0xfffu) {
        *value = UINT32_MAX;
        return AEO_B_OK;
    }
    return *value < limit ? AEO_B_OK : AEO_B_INVALID;
}

static int present(reader_t* r, span_t scope, const char* name)
{
    size_t pos = scope.begin, inner;
    span_t child;
    while (pos < scope.end) {
        if (!next(r, scope, &pos, &child))
            return 0;
        if (label(r, child, name, &inner))
            return 1;
    }
    return 0;
}

static aeob_status_t emit(reader_t* r, span_t program, size_t pos, span_t component,
                          aeob_state_kind_t kind, uint32_t c, uint32_t set, uint32_t state);

static aeob_status_t phase(reader_t* r, span_t scope, const char* name, uint32_t state, int exit,
                           span_t component, aeob_state_kind_t kind, uint32_t c, uint32_t set)
{
    span_t selected, action, first;
    size_t pos, inner, start;
    aeob_status_t status = find(r, scope, name, state, &selected);
    if (status != AEO_B_OK)
        return status;
    if (!label(r, selected, name, &pos) || !next(r, selected, &pos, &first))
        return AEO_B_INVALID;
    while (pos < selected.end) {
        start = pos;
        if (!next(r, selected, &pos, &action) || action.type != 3u)
            return AEO_B_INVALID;
        inner = action.begin;
        if (!next(r, action, &inner, &first))
            return AEO_B_INVALID;
        if (text(r, first, "ENTER") || text(r, first, "EXIT")) {
            if ((text(r, first, "EXIT") != 0) != (exit != 0))
                continue;
            status = emit(r, action, inner, component, kind, c, set, state);
        } else if (!exit) {
            span_t one = {3u, start, pos};
            status = emit(r, one, start, component, kind, c, set, state);
        } else {
            continue;
        }
        if (status != AEO_B_OK)
            return status;
    }
    return AEO_B_OK;
}

static aeob_status_t emit(reader_t* r, span_t program, size_t pos, span_t component,
                          aeob_state_kind_t kind, uint32_t c, uint32_t set, uint32_t state)
{
    span_t action, name, params;
    aeob_preload_action_t out;
    size_t inner, len;
    while (pos < program.end) {
        if (!next(r, program, &pos, &action) || action.type != 3u)
            return AEO_B_INVALID;
        inner = action.begin;
        if (!next(r, action, &inner, &name) || name.type != 1u ||
            !next(r, action, &inner, &params) || params.type != 3u || inner != action.end)
            return AEO_B_INVALID;
        len = name.end - name.begin;
        if (len < 2u || len > sizeof(out.resource.kind) || r->data[name.end - 1u] ||
            memchr(r->data + name.begin, 0, len - 1u))
            return AEO_B_INVALID;
        if (text(r, name, "PSTATE_ADJUST")) {
            span_t n, pset;
            uint32_t target_set, target;
            aeob_status_t status;
            inner = params.begin;
            if (kind != AEOB_F_STATE || !next(r, params, &inner, &n) ||
                !number(r, n, &target_set) || target_set >= 32u || !next(r, params, &inner, &n) ||
                !number(r, n, &target) || target >= 32u || inner != params.end)
                return AEO_B_INVALID;
            status = find(r, component, "PSTATE_SET", target_set, &pset);
            if (status == AEO_B_NOT_FOUND && target_set == 0u)
                pset = component;
            else if (status != AEO_B_OK)
                return status;
            status = phase(r, pset, "PSTATE", target, 0, component, AEOB_P_STATE, c, target_set);
            if (status != AEO_B_OK)
                return status;
            continue;
        }
        memset(&out, 0, sizeof(out));
        memcpy(out.resource.kind, r->data + name.begin, len);
        out.resource.arguments = r->data + params.begin;
        out.resource.argument_bytes = (uint32_t)(params.end - params.begin);
        out.kind = kind;
        out.component = c;
        out.set = set;
        out.state = state;
        if (r->visit && !r->visit(&out, r->ctx))
            return AEO_B_CALLBACK_ERROR;
        ++r->result->actions;
    }
    return AEO_B_OK;
}

static aeob_status_t transition(reader_t* r, span_t scope, const char* name, uint32_t initial,
                                uint32_t target, span_t component, aeob_state_kind_t kind,
                                uint32_t c, uint32_t set)
{
    aeob_status_t status;
    span_t selected;
    if (initial == target)
        return find(r, scope, name, target, &selected);
    if (kind == AEOB_F_STATE && initial != UINT32_MAX) {
        status = phase(r, scope, name, initial, 1, component, kind, c, set);
        if (status != AEO_B_OK)
            return status;
    }
    return phase(r, scope, name, target, 0, component, kind, c, set);
}

static aeob_status_t component(reader_t* r, span_t comp, uint32_t id)
{
    uint32_t initial, target, p, initial_p, set, sets = 0u;
    int has_f_states = present(r, comp, "FSTATE");
    size_t pos = comp.begin, inner;
    span_t child, n;
    aeob_status_t status = selector(r, comp, "INIT_FSTATE", 32u, &initial);
    if (status != AEO_B_OK)
        return status;
    status = selector(r, comp, "PRELOAD_FSTATE", 32u, &target);
    if (status != AEO_B_OK)
        return status;
    if (target != UINT32_MAX) {
        status = transition(r, comp, "FSTATE", initial, target, comp, AEOB_F_STATE, id, 0u);
        if (status != AEO_B_OK)
            return status;
        ++r->result->f_states;
    }
    while (pos < comp.end) {
        if (!next(r, comp, &pos, &child))
            return AEO_B_INVALID;
        if (!label(r, child, "PSTATE_SET", &inner))
            continue;
        if (!next(r, child, &inner, &n) || !number(r, n, &set) || set >= 32u ||
            (sets & (UINT32_C(1) << set)))
            return AEO_B_INVALID;
        sets |= UINT32_C(1) << set;
        status = selector(r, child, "PRELOAD_PSTATE", 32u, &p);
        if (status != AEO_B_OK)
            return status;
        if (p == UINT32_MAX)
            continue;
        status = find(r, child, "PSTATE", p, &n);
        if (status != AEO_B_OK)
            return status;
        status = selector(r, child, "INIT_PSTATE", 32u, &initial_p);
        if (status != AEO_B_OK)
            return status;
        if ((r->has_device_states && r->device_state != 0u) ||
            (has_f_states && (target == UINT32_MAX ? initial : target) != 0u)) {
            ++r->result->inactive;
            continue;
        }
        status = transition(r, child, "PSTATE", initial_p, p, comp, AEOB_P_STATE, id, set);
        if (status != AEO_B_OK)
            return status;
        ++r->result->p_states;
    }
    if (!sets) {
        status = selector(r, comp, "PRELOAD_PSTATE", 32u, &p);
        if (status != AEO_B_OK || p == UINT32_MAX)
            return status;
        status = find(r, comp, "PSTATE", p, &n);
        if (status != AEO_B_OK)
            return status;
        status = selector(r, comp, "INIT_PSTATE", 32u, &initial_p);
        if (status != AEO_B_OK)
            return status;
        if ((r->has_device_states && r->device_state != 0u) ||
            (has_f_states && (target == UINT32_MAX ? initial : target) != 0u)) {
            ++r->result->inactive;
            return AEO_B_OK;
        }
        status = transition(r, comp, "PSTATE", initial_p, p, comp, AEOB_P_STATE, id, 0u);
        if (status != AEO_B_OK)
            return status;
        ++r->result->p_states;
    }
    return AEO_B_OK;
}

static aeob_status_t device(reader_t* r, span_t dev)
{
    uint32_t initial, target, id, components = 0u;
    size_t pos = dev.begin, inner;
    span_t child, n;
    aeob_status_t status = selector(r, dev, "INIT_DSTATE", 4u, &initial);
    if (status != AEO_B_OK)
        return status;
    status = selector(r, dev, "PRELOAD_DSTATE", 4u, &target);
    if (status != AEO_B_OK)
        return status;
    r->has_device_states = present(r, dev, "DSTATE");
    r->device_state = target == UINT32_MAX ? initial : target;
    if (target != UINT32_MAX) {
        status = transition(r, dev, "DSTATE", initial, target, dev, AEOB_D_STATE, 0u, 0u);
        if (status != AEO_B_OK)
            return status;
        ++r->result->d_states;
    }
    while (pos < dev.end) {
        if (!next(r, dev, &pos, &child))
            return AEO_B_INVALID;
        if (!label(r, child, "COMPONENT", &inner))
            continue;
        if (!next(r, child, &inner, &n) || !number(r, n, &id) || id >= 32u ||
            (components & (UINT32_C(1) << id)))
            return AEO_B_INVALID;
        components |= UINT32_C(1) << id;
        status = component(r, child, id);
        if (status != AEO_B_OK)
            return status;
    }
    return AEO_B_OK;
}

static int matches(reader_t* r, span_t dev, const char* path)
{
    size_t pos;
    span_t name;
    if (!label(r, dev, "DEVICE", &pos) || !next(r, dev, &pos, &name))
        return 0;
    if (name.type == 0u && !next(r, dev, &pos, &name))
        return 0;
    return text(r, name, path);
}

aeob_status_t aeo_b_visit_preload(const void* buf, size_t bytes, const char* path,
                                  aeob_preload_visitor_t visit, void* ctx,
                                  aeob_preload_result_t* result)
{
    uint32_t count = 0u, found = 0u;
    size_t pos = 12u, inner;
    span_t root, top, child, selected = {0};
    reader_t r = {buf, visit, ctx, result, UINT32_MAX, 0};
    aeob_status_t status;
    if (!buf || !path || path[0] != '\\' || !result)
        return AEO_B_INVALID;
    memset(result, 0, sizeof(*result));
    status = aeo_b_walk(buf, bytes, NULL, NULL, &count);
    if (status != AEO_B_OK)
        return status;
    root.type = 3u;
    root.begin = 12u;
    root.end = (size_t)r.data[4] | ((size_t)r.data[5] << 8) | ((size_t)r.data[6] << 16) |
               ((size_t)r.data[7] << 24);
    while (pos < root.end) {
        if (!next(&r, root, &pos, &top))
            return AEO_B_INVALID;
        if (matches(&r, top, path)) {
            selected = top;
            ++found;
            continue;
        }
        inner = top.begin;
        if (top.type != 3u || !next(&r, top, &inner, &child) || child.type != 3u)
            continue;
        inner = top.begin;
        while (inner < top.end) {
            if (!next(&r, top, &inner, &child))
                return AEO_B_INVALID;
            if (matches(&r, child, path)) {
                selected = child;
                ++found;
            }
        }
    }
    if (found != 1u)
        return found ? AEO_B_AMBIGUOUS : AEO_B_NOT_FOUND;
    return device(&r, selected);
}
