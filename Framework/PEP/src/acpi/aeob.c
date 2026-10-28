#include "aeob.h"

static uint16_t aeo_b_read16(const uint8_t* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t aeo_b_read32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct aeob_walk_ctx {
    const uint8_t* buf;
    uint32_t nodes;
    aeob_visitor_t visit;
    void* callback_ctx;
} aeob_walk_ctx_t;

static aeob_status_t aeo_b_walk_range(aeob_walk_ctx_t* c, size_t begin, size_t end, uint32_t depth,
                                      uint32_t* direct_cnt)
{
    size_t position = begin;
    uint32_t direct = 0;
    if (depth > AEOB_MAX_DEPTH)
        return AEO_B_TOO_DEEP;

    while (position < end) {
        aeob_argument_t a;
        size_t payload, next;
        aeob_status_t st;

        if (end - position < 4)
            return AEO_B_TRUNCATED;
        if (++c->nodes > AEOB_MAX_ARGUMENTS)
            return AEO_B_INVALID;
        a.offset = (uint32_t)position;
        a.type = aeo_b_read16(c->buf + position);
        a.data_length = aeo_b_read16(c->buf + position + 2);
        a.depth = depth;
        payload = position + 4;
        if ((size_t)a.data_length > end - payload)
            return AEO_B_TRUNCATED;
        if ((size_t)(a.data_length < 4 ? 4 : a.data_length) > end - payload)
            return AEO_B_TRUNCATED;
        a.data = c->buf + payload;
        next = payload + (a.data_length < 4 ? 4u : (size_t)a.data_length);
        switch (a.type) {
            case 0:
                if (a.data_length != 2 && a.data_length != 4 && a.data_length != 8)
                    return AEO_B_INVALID;
                break;
            case 1:
                if (a.data_length == 0 || a.data[a.data_length - 1] != 0)
                    return AEO_B_INVALID;
                break;
            case 2: /* opaque ACPI buf. */
                break;
            case 3:
                break;
            case 4:
                return AEO_B_UNSUPPORTED;
            default:
                return AEO_B_UNSUPPORTED;
        }
        if (c->visit && !c->visit(&a, c->callback_ctx))
            return AEO_B_CALLBACK_ERROR;
        if (a.type == 3) {
            uint32_t nested = 0;
            st = aeo_b_walk_range(c, payload, payload + a.data_length, depth + 1, &nested);
            if (st != AEO_B_OK)
                return st;
        }
        position = next;
        ++direct;
    }
    if (position != end)
        return AEO_B_TRUNCATED;
    *direct_cnt = direct;
    return AEO_B_OK;
}

aeob_status_t aeo_b_walk(const void* buf, size_t avail_bytes, aeob_visitor_t visit, void* ctx,
                         uint32_t* top_argument_cnt)
{
    const uint8_t* b = (const uint8_t*)buf;
    aeob_walk_ctx_t c;
    uint32_t direct = 0;
    uint32_t declared;
    size_t length;
    aeob_status_t result;
    if (top_argument_cnt)
        *top_argument_cnt = 0;
    if (!b || avail_bytes < 12)
        return AEO_B_TRUNCATED;
    if (aeo_b_read32(b) != AEOB_SIGNATURE)
        return AEO_B_INVALID;
    length = (size_t)aeo_b_read32(b + 4);
    declared = aeo_b_read32(b + 8);
    if (length < 12 || length > avail_bytes || length > AEOB_MAX_SIZE)
        return AEO_B_INVALID;
    if (declared > AEOB_MAX_ARGUMENTS)
        return AEO_B_INVALID;
    /* never infer an empty result from a failed IOCTL or an arbitrary extra-length buf:
     * Count=0 with any other length is malformed. */

    if (declared == 0u)
        return (length == 12u || length == 20u) ? AEO_B_OK : AEO_B_INVALID;
    c.buf = b;
    c.nodes = 0;
    c.visit = visit;
    c.callback_ctx = ctx;
    result = aeo_b_walk_range(&c, 12, length, 0, &direct);
    if (result != AEO_B_OK)
        return result;
    if (declared != direct)
        return AEO_B_INVALID;
    if (top_argument_cnt)
        *top_argument_cnt = direct;
    return AEO_B_OK;
}

static int empty_visit(const aeob_argument_t* arg, void* ctx)
{
    int* empty = ctx;
    if (arg->type != 3u)
        *empty = 0;
    return 1;
}

int aeo_b_empty(const void* buf, size_t bytes)
{
    int empty = 1;
    return aeo_b_walk(buf, bytes, empty_visit, &empty, NULL) == AEO_B_OK && empty;
}

/* Method manifests contain reversed four-character ACPI names. */

typedef struct stnd_ctx {
    char* names;
    uint32_t capacity;
    uint32_t cnt;
    uint32_t top_nodes;
    uint32_t wrapped;
    uint32_t flat;
    int invalid;
} stnd_ctx_t;

static int aeo_b_stnd_visit(const aeob_argument_t* arg, void* ctx)
{
    stnd_ctx_t* c = (stnd_ctx_t*)ctx;
    uint32_t i, j;
    if (arg->depth == 0 && arg->type == 3) {
        if (++c->top_nodes != 1u || c->flat)
            c->invalid = 1;
        c->wrapped = 1u;
        return 1;
    }
    if (arg->depth == 0) {
        ++c->top_nodes;
        if (c->wrapped)
            c->invalid = 1;
        c->flat = 1u;
    } else if (arg->depth != 1 || !c->wrapped || c->flat) {
        c->invalid = 1;
        return 1;
    }
    if (arg->type != 1 || arg->data_length != 5u || c->cnt >= c->capacity || arg->data[4] != 0) {
        c->invalid = 1;
        return 1;
    }
    for (i = 0; i < 4u; ++i) {
        uint8_t b = arg->data[3u - i];
        if (!((b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9') || b == '_'))
            c->invalid = 1;
        c->names[c->cnt * 5u + i] = (char)b;
    }
    c->names[c->cnt * 5u + 4u] = 0;
    /* duplicated methods are ambiguous: never evaluate them twice. */
    for (j = 0; j < c->cnt; ++j) {
        for (i = 0; i < 5u; ++i)
            if (c->names[j * 5u + i] != c->names[c->cnt * 5u + i])
                break;
        if (i == 5u)
            c->invalid = 1;
    }
    ++c->cnt;
    return 1;
}

aeob_status_t aeo_b_decode_stnd(const void* buf, size_t avail_bytes, char* names, uint32_t capacity,
                                uint32_t* name_cnt)
{
    stnd_ctx_t c;
    uint32_t top = 0;
    aeob_status_t status;
    if (!names || !name_cnt || capacity == 0 || capacity > 32)
        return AEO_B_INVALID;
    *name_cnt = 0;
    if (aeo_b_empty(buf, avail_bytes))
        return AEO_B_OK;
    c.names = names;
    c.capacity = capacity;
    c.cnt = 0;
    c.invalid = 0;
    c.top_nodes = c.wrapped = c.flat = 0;
    status = aeo_b_walk(buf, avail_bytes, aeo_b_stnd_visit, &c, &top);
    if (status != AEO_B_OK)
        return status;
    if (c.invalid || c.cnt == 0 ||
        !((c.wrapped && !c.flat && top == 1u && c.top_nodes == 1u) ||
          (c.flat && !c.wrapped && top == c.cnt && c.top_nodes == c.cnt)))
        return AEO_B_INVALID;
    *name_cnt = c.cnt;
    return AEO_B_OK;
}
