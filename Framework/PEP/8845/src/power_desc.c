#include "power_desc.h"
#include <string.h>

int resource_method_valid(const char method[5])
{
    unsigned i;
    if (!method || !(method[0] == '_' || (method[0] >= 'A' && method[0] <= 'Z')))
        return 0;
    for (i = 1u; i < 4u; ++i)
        if (!(method[i] == '_' || (method[i] >= 'A' && method[i] <= 'Z') ||
              (method[i] >= '0' && method[i] <= '9')))
            return 0;
    return method[4] == '\0';
}

power_result_t power_discover_dev_d_state(const char* const* methods, uint32_t cnt,
                                          method_evaluate_t evaluate, method_release_t release,
                                          void* ctx, const char* path, uint32_t state,
                                          power_sequence_t* sequence, char selected_method[5])
{
    uint32_t i, found = 0;
    char winning_method[5] = {0};
    power_result_t result = POWER_INVALID_PROGRAM;
    if (!methods || !evaluate || !release || !path || !sequence || !selected_method || cnt == 0 ||
        cnt > DISCOVERY_MAX_METHODS || state >= POWER_DSTATE_COUNT)
        return POWER_BAD_INPUT;
    memset(sequence, 0, sizeof(*sequence));
    memset(selected_method, 0, 5u);
    for (i = 0; i < cnt; ++i) {
        const void* bytes = NULL;
        size_t length = 0;
        uint32_t action_cnt = 0;
        aeob_status_t ast;
        method_eval_result_t er;
        uint32_t j;
        int repeated = 0;
        if (!methods[i] || strlen(methods[i]) != 4u) {
            result = POWER_BAD_INPUT;
            goto error;
        }

        if (!resource_method_valid(methods[i])) {
            result = POWER_BAD_INPUT;
            goto error;
        }
        for (j = 0; j < i; ++j)
            if (methods[j] && strcmp(methods[i], methods[j]) == 0) {
                repeated = 1;
                break;
            }
        if (repeated)
            continue;
        er = evaluate(ctx, methods[i], &bytes, &length);
        if (er == METHOD_NOT_PRESENT)
            continue;
        if (er != METHOD_EVALUATED || !bytes || !length) {
            if (bytes)
                release(ctx, bytes);
            goto error;
        }
        ast = aeo_b_visit_dev_d_state(bytes, length, path, state, NULL, NULL, &action_cnt);
        if (ast == AEO_B_NOT_FOUND) {
            release(ctx, bytes);
            continue;
        }
        if (ast != AEO_B_OK || found) {
            release(ctx, bytes);
            goto error;
        }
        result = power_compile_aeo_b(bytes, length, path, state, sequence);
        release(ctx, bytes);
        if (result != POWER_OK)
            goto error;
        memcpy(winning_method, methods[i], 5u);
        found = 1;
    }
    if (!found)
        return POWER_UNSUPPORTED;
    memcpy(selected_method, winning_method, 5u);
    return POWER_OK;
error:
    memset(sequence, 0, sizeof(*sequence));
    memset(selected_method, 0, 5u);
    return result == POWER_BAD_INPUT ? POWER_BAD_INPUT : POWER_INVALID_PROGRAM;
}
