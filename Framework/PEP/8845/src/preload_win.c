#include "preload_win.h"
#include "preload.h"
#include "trace.h"
#include "preload_win.tmh"

typedef struct preload_node {
    struct preload_node* next;
    res_graph_t* graph;
    preload_t owner;
    char method[5];
    power_result_t result;
    BOOLEAN duplicate;
} preload_node_t;

static int begin(void* ctx, hw_backend_t* provider)
{
    preload_node_t* node = ctx;
    res_graph_t* g = node->graph;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || !g || !g->sealed || g->retired || g->quiescing ||
        g->backend_binding_serial != node->owner.serial || !g->acquire || !g->release ||
        !g->acquire(g->lock_ctx))
        return 0;
    if (hw_backend_begin_bound(node->owner.serial, provider))
        return 1;
    g->release(g->lock_ctx);
    return 0;
}

static void end(void* ctx)
{
    preload_node_t* node = ctx;
    hw_backend_end_bound();
    node->graph->release(node->graph->lock_ctx);
}

static void trace_failure(const preload_node_t* node)
{
    const preload_t* s = &node->owner;
    uint32_t i = s->trace.failure_idx;
    const power_action_t* a = i < s->sequence.action_cnt ? &s->sequence.actions[i] : NULL;
    TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                "preload: method=%s path=%s result=%u action=%lu applied=%lu restored=%lu kind=%s",
                node->method, s->sequence.dev_path, node->result, i, s->trace.applied,
                s->trace.restored, s->failure_kind);
    if (a && a->kind == POWER_CLOCK)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_CLOCK, "preload: clock=%s operation=%lu",
                    a->u.clock.name, a->u.clock.vote);
    else if (a && a->kind == POWER_ICB)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE, "preload: bus=%s -> %s", a->u.icb.master,
                    a->u.icb.slave);
    else if (a && a->kind == POWER_FOOTSWITCH)
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RESOURCE,
                    "preload: domain=%s operation=%lu notify=%lu", a->u.footswitch.name,
                    a->u.footswitch.action, a->u.footswitch.notify);
}

NTSTATUS preload_win_start(res_graph_t* g)
{
    char (*paths)[256];
    uint32_t source, previous, count, p, owners = 0u;
    preload_node_t *node, **tail;
    NTSTATUS status = STATUS_SUCCESS;
    if (!g || !g->sealed || g->retired || g->quiescing || !g->backend_binding_serial ||
        g->backend_binding_serial != hw_backend_verified_serial() ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    if (g->preloads_done)
        return STATUS_SUCCESS;
    if (g->preloads || g->src_cnt > GRAPH_SRC_MAX)
        return STATUS_INVALID_DEVICE_STATE;
    paths = ExAllocatePool2(POOL_FLAG_NON_PAGED, GRAPH_MAX_DEVICES * 256u, POOL_TAG);
    if (!paths)
        return STATUS_INSUFFICIENT_RESOURCES;
    tail = &g->preloads;
    /* Compile before voting so duplicate identities cannot partially execute. */
    for (source = 0u; source < g->src_cnt; ++source) {
        const graph_desc_t* desc = &g->sources[source];
        for (previous = 0u; previous < source; ++previous)
            if (!strcmp(desc->method, g->sources[previous].method))
                break;
        if (previous < source) {
            const graph_desc_t* prior = &g->sources[previous];
            if (desc->bytes != prior->bytes || memcmp(desc->buf, prior->buf, desc->bytes)) {
                status = STATUS_DATA_ERROR;
                goto done;
            }
            continue;
        }
        if (aeo_b_empty(desc->buf, desc->bytes))
            continue;
        if (aeo_b_list_dev_paths(desc->buf, desc->bytes, (char*)paths, GRAPH_MAX_DEVICES, &count) !=
            AEO_B_OK) {
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_ACPI,
                        "preload: method=%s invalid device description", desc->method);
            continue;
        }
        for (p = 0u; p < count; ++p) {
            node = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*node), POOL_TAG);
            if (!node) {
                status = STATUS_INSUFFICIENT_RESOURCES;
                goto done;
            }
            node->owner.trace.failure_idx = UINT32_MAX;
            node->result = preload_compile(&node->owner, desc->buf, desc->bytes, paths[p]);
            if (node->result == POWER_OK && !node->owner.selected.d_states &&
                !node->owner.selected.f_states && !node->owner.selected.p_states &&
                !node->owner.selected.inactive) {
                ExFreePool(node);
                continue;
            }
            if (owners == GRAPH_MAX_DEVICES) {
                ExFreePool(node);
                status = STATUS_QUOTA_EXCEEDED;
                goto done;
            }
            ++owners;
            node->graph = g;
            node->owner.ctx = node;
            node->owner.serial = g->backend_binding_serial;
            node->owner.begin = begin;
            node->owner.end = end;
            RtlCopyMemory(node->method, desc->method, sizeof(node->method));
            for (preload_node_t* prior = g->preloads; prior; prior = prior->next)
                if (!strcmp(prior->owner.sequence.dev_path, paths[p])) {
                    prior->duplicate = TRUE;
                    node->duplicate = TRUE;
                }
            *tail = node;
            tail = &node->next;
        }
    }
    for (node = g->preloads; node; node = node->next) {
        if (node->duplicate)
            node->result = POWER_INVALID_PROGRAM;
        else if (node->result == POWER_OK)
            node->result = preload_execute(&node->owner);
        if (node->result != POWER_OK) {
            trace_failure(node);
            if (node->owner.quarantined) {
                status = STATUS_DEVICE_HARDWARE_ERROR;
                goto done;
            }
            continue;
        }
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_RESOURCE,
                    "preload: method=%s path=%s D=%lu F=%lu P=%lu inactive=%lu actions=%lu",
                    node->method, node->owner.sequence.dev_path, node->owner.selected.d_states,
                    node->owner.selected.f_states, node->owner.selected.p_states,
                    node->owner.selected.inactive, node->owner.selected.actions);
    }
    g->preloads_done = 1u;
done:
    ExFreePool(paths);
    return status;
}

BOOLEAN preload_win_abort(res_graph_t* g)
{
    preload_node_t* nodes[GRAPH_MAX_DEVICES];
    preload_node_t* node;
    uint32_t count = 0u;
    if (!g || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return FALSE;
    for (node = g->preloads; node; node = node->next) {
        if (count == GRAPH_MAX_DEVICES)
            return FALSE;
        nodes[count++] = node;
    }
    while (count) {
        node = nodes[--count];
        if (!preload_rollback(&node->owner))
            return FALSE;
    }
    while (g->preloads) {
        node = g->preloads;
        g->preloads = node->next;
        ExFreePool(node);
    }
    g->preloads_done = 0u;
    return TRUE;
}
