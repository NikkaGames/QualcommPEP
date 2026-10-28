#include "identity.h"
#include "action_opcode.h"
#include <string.h>
#include <stdint.h>

static int has_prefix(const char* str, const char* prefix)
{
    size_t i;
    if (!str || !prefix)
        return 0;
    for (i = 0u; prefix[i]; ++i)
        if (!str[i] || str[i] != prefix[i])
            return 0;
    return 1;
}

static int valid_clock_name(const char* name)
{
    size_t i;
    if (!name)
        return 0;
    for (i = 0; i < NPA_NAME_BYTES; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c)
            return i > 0u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return 0;
    }
    return 0;
}

static int is_same_clock(const npa_key_t* key, const power_action_t* action)
{
    return key && action && key->kind == POWER_CLOCK && action->kind == POWER_CLOCK &&
           key->d_state < POWER_DSTATE_COUNT && valid_clock_name(key->res) &&
           strcmp(key->res, action->u.clock.name) == 0 && action->u.clock.vote < 32u &&
           (key->clock_mode_bits & (1u << action->u.clock.vote)) != 0u;
}

int npa_describe_clock_client(const npa_key_t* key, const power_action_t* action,
                              native_npa_client_desc_t* out)
{
    static const char prefix[] = "/clk/";
    static const char enable[] = "/enable";
    size_t n;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    clock_opcode_t decoded;
    if (!is_same_clock(key, action) || !decode_clock_opcode(action, &decoded) ||
        (decoded.kind != CLOCK_OP_ENABLE && decoded.kind != CLOCK_OP_DISABLE))
        return 0;
    /* enable-and-rate uses separate enable and frequency requests. */

    n = strlen(key->res);
    if (n + sizeof(prefix) - 1u + sizeof(enable) > sizeof(out->res_name))
        return 0;
    memcpy(out->res_name, prefix, sizeof(prefix) - 1u);
    memcpy(out->res_name + sizeof(prefix) - 1u, key->res, n);
    memcpy(out->res_name + sizeof(prefix) - 1u + n, enable, sizeof(enable));
    out->class = NATIVE_NPA_CLOCK_ENABLE;
    out->clock_aggregation = NATIVE_CLOCK_ENABLE_MIN_NONZERO;
    out->client_type = NPA_CLOCK_CLIENT_REQUIRED;
    return 1;
}

int npa_describe_composite_clock_clients(const npa_key_t* key, const power_action_t* action,
                                         native_npa_composite_clock_desc_t* out)
{
    static const char prefix[] = "/clk/";
    static const char suffix[] = "/enable";
    clock_opcode_t decoded;
    size_t n, total;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!is_same_clock(key, action) || !decode_clock_opcode(action, &decoded) ||
        decoded.kind != CLOCK_OP_ENABLE_SET_FREQ || decoded.param_cnt != 2u || !decoded.parameter0)
        return 0;
    n = strlen(key->res);
    total = (sizeof(prefix) - 1u) + n;
    if (total + 1u > sizeof(out->freq.res_name) ||
        total + sizeof(suffix) > sizeof(out->enable.res_name))
        return 0;
    memcpy(out->freq.res_name, prefix, sizeof(prefix) - 1u);
    memcpy(out->freq.res_name + sizeof(prefix) - 1u, key->res, n);
    out->freq.res_name[total] = '\0';
    memcpy(out->enable.res_name, out->freq.res_name, total);
    memcpy(out->enable.res_name + total, suffix, sizeof(suffix));
    out->freq.class = NATIVE_NPA_CLOCK_FREQ;
    out->freq.clock_aggregation = NATIVE_CLOCK_FREQ_MAX;
    out->enable.class = NATIVE_NPA_CLOCK_ENABLE;
    out->enable.clock_aggregation = NATIVE_CLOCK_ENABLE_MIN_NONZERO;
    out->freq.client_type = NPA_CLOCK_CLIENT_REQUIRED;
    out->enable.client_type = NPA_CLOCK_CLIENT_REQUIRED;
    out->requested_hertz = decoded.parameter0;
    /* validate the opaque DSDT match policy through the read-only NPA query. */

    out->opaque_match_policy = decoded.parameter1;
    return 1;
}

int npa_build_clock_requests(const npa_key_t* key, const power_action_t* action,
                             native_npa_clock_requests_t* out)
{
    clock_opcode_t decoded;
    native_npa_composite_clock_desc_t composite;
    native_npa_client_desc_t simple;
    native_npa_clock_requests_t sequence;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!key || !action || !decode_clock_opcode(action, &decoded))
        return 0;
    memset(&sequence, 0, sizeof(sequence));
    if (decoded.kind == CLOCK_OP_ENABLE_SET_FREQ) {
        /* the DSDT match-policy operand is not an NPA scalar issue. */

        if (!npa_describe_composite_clock_clients(key, action, &composite))
            return 0;
        sequence.steps[0].client = composite.enable;
        sequence.steps[0].requested_scalar = NPA_CLOCK_ENABLE;
        sequence.steps[1].client = composite.freq;
        sequence.steps[1].requested_scalar = composite.requested_hertz;
        sequence.opaque_match_policy = composite.opaque_match_policy;
        sequence.cnt = 2u;
    } else {
        if (!npa_describe_clock_client(key, action, &simple))
            return 0;
        sequence.steps[0].client = simple;
        sequence.steps[0].requested_scalar =
            (decoded.kind == CLOCK_OP_ENABLE) ? NPA_CLOCK_ENABLE : NPA_CLOCK_DISABLE;
        sequence.cnt = 1u;
    }
    *out = sequence;
    return 1;
}

static int valid_bus_resource_name(const char* name)
{
    size_t i;
    /* require an independently verified native resource mapping. */

    if (!has_prefix(name, "/icb/") || !name[5])
        return 0;
    for (i = 0; i < NATIVE_NPA_NAME_SIZE; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!c)
            return i >= 6u;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '/' || c == '-'))
            return 0;
        if (c == '/' && i > 0u && name[i - 1u] == '/')
            return 0;
    }
    return 0;
}

int npa_describe_bus_client(const npa_key_t* key, const char* resolved_native_res_name,
                            uint32_t rsc_driver_id, uint32_t native_client_type,
                            native_npa_client_desc_t* out)
{
    uint32_t ref[2];
    size_t n;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    /* the ACPI BUSARB selector does not encode a native driver ID. */

    if (!key || key->kind != POWER_ICB || key->d_state >= POWER_DSTATE_COUNT ||
        rsc_driver_id == UINT32_MAX || native_client_type == 0u ||
        native_client_type == UINT32_MAX || !valid_bus_resource_name(resolved_native_res_name) ||
        !npa_build_bus_client_reference(key, ref))
        return 0;
    n = strlen(resolved_native_res_name);
    memcpy(out->res_name, resolved_native_res_name, n + 1u);
    out->class = NATIVE_NPA_BUS;
    out->client_reference[0] = ref[0];
    out->client_reference[1] = ref[1];
    out->client_reference_bytes = 8u;
    out->rsc_driver_id = rsc_driver_id;
    out->client_type = native_client_type;
    return 1;
}

/* client types 0x40 and 0x800 have distinct stored vote banks at res offsets +0x40/+0x44.
 * the former's final merge at 0x23565C..0x235674 selects the nonzero minimum, while the
 * latter's 0x235804..0x23580C selects the unsigned maximum. */

int npa_aggregate_native_clock_votes(native_clock_aggregation_t aggregation,
                                     const native_clock_vote_t* votes, uint32_t vote_cnt,
                                     uint32_t* aggregate, uint32_t* issued_cnt)
{
    uint32_t required = 0u, suppressible = 0u, present = 0u, i;
    if (aggregate)
        *aggregate = 0u;
    if (issued_cnt)
        *issued_cnt = 0u;
    if (!aggregate || !issued_cnt ||
        (aggregation != NATIVE_CLOCK_ENABLE_MIN_NONZERO && aggregation != NATIVE_CLOCK_FREQ_MAX) ||
        (!votes && vote_cnt) || vote_cnt > NATIVE_CLOCK_MAX_AGGREGATE_VOTES)
        return 0;
    for (i = 0u; i < vote_cnt; ++i) {
        uint32_t v = votes[i].scalar;
        uint32_t* bank;
        if (votes[i].client_type == NPA_CLOCK_CLIENT_REQUIRED)
            bank = &required;
        else if (votes[i].client_type == NPA_CLOCK_CLIENT_SUPPRESSIBLE)
            bank = &suppressible;
        else
            return 0;
        if (votes[i].is_issued > 1u)
            return 0;
        if (!votes[i].is_issued) {
            if (v != 0u)
                return 0; /* a completed client cannot hold a vote. */
            continue;
        }
        if (v == 0u || (aggregation == NATIVE_CLOCK_ENABLE_MIN_NONZERO && v != NPA_CLOCK_ENABLE &&
                        v != NPA_CLOCK_DISABLE))
            return 0;
        if (*bank == 0u || (aggregation == NATIVE_CLOCK_FREQ_MAX && v > *bank) ||
            (aggregation == NATIVE_CLOCK_ENABLE_MIN_NONZERO && v < *bank))
            *bank = v;
        ++present;
    }
    if (aggregation == NATIVE_CLOCK_FREQ_MAX)
        *aggregate = required > suppressible ? required : suppressible;
    else if (!required)
        *aggregate = suppressible;
    else if (!suppressible)
        *aggregate = required;
    else
        *aggregate = required < suppressible ? required : suppressible;
    *issued_cnt = present;
    return 1;
}
