#include "runtime_component_win.h"

static int live(void* ctx, uint64_t epoch)
{
    runtime_comp_win_t* s = ctx;
    return s && s->serial == epoch && epoch && KeGetCurrentIrql() == PASSIVE_LEVEL &&
           !InterlockedCompareExchange(&s->proof_lost, 0, 0) &&
           epoch == hw_backend_verified_serial() && s->graph && s->graph->sealed &&
           !s->graph->retired && !s->graph->quiescing;
}

static int begin(void* ctx, hw_backend_t* provider)
{
    runtime_comp_win_t* s = ctx;
    if (!s || !live(s, s->serial) || s->provider_held || !s->graph->acquire || !s->graph->release ||
        !s->graph->acquire(s->graph->lock_ctx))
        return 0;
    if (!hw_backend_begin_bound(s->serial, provider)) {
        s->graph->release(s->graph->lock_ctx);
        return 0;
    }
    s->provider_held = TRUE;
    return 1;
}

static void end(void* ctx)
{
    runtime_comp_win_t* s = ctx;
    if (!s || !s->provider_held)
        return;
    s->provider_held = FALSE;
    hw_backend_end_bound();
    s->graph->release(s->graph->lock_ctx);
}

static BOOLEAN lock(runtime_comp_win_t* s)
{
    LARGE_INTEGER timeout;
    if (!s || !live(s, s->serial))
        return FALSE;
    timeout.QuadPart = -20000000LL;
    if (KeWaitForSingleObject(&s->mutex, Executive, KernelMode, FALSE, &timeout) != STATUS_SUCCESS)
        return FALSE;
    if (live(s, s->serial))
        return TRUE;
    (void)KeReleaseMutex(&s->mutex, FALSE);
    return FALSE;
}

static void unlock(runtime_comp_win_t* s)
{
    (void)KeReleaseMutex(&s->mutex, FALSE);
}

/* the same mutex protects callbacks and their retained scratch space. Avoid allocating and
 * zeroing a large program workspace on every idle/query. */
static comp_state_work_t* work(runtime_comp_win_t* s)
{
    return s ? s->workspace : NULL;
}

BOOLEAN runtime_comp_win_prepare(runtime_comp_win_t* s, const void* blob, ULONG bytes,
                                 const char* path, res_graph_t* graph, uint64_t epoch)
{
    runtime_desc_info_t* info;
    comp_state_work_t* w;
    BOOLEAN result = FALSE;
    if (!s || !blob || !bytes || bytes > AEOB_MAX_SIZE || !graph || !epoch)
        return FALSE;
    KeInitializeMutex(&s->mutex, 0u);
    s->graph = graph;
    s->serial = epoch;
    s->desc = ExAllocatePool2(POOL_FLAG_NON_PAGED, bytes, POOL_TAG);
    info = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*info), POOL_TAG);
    s->workspace = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*s->workspace), POOL_TAG);
    w = work(s);
    if (!s->desc || !info || !w)
        goto done;
    RtlCopyMemory(s->desc, blob, bytes);
    s->desc_bytes = bytes;
    if (!lock(s))
        goto done;
    result = (BOOLEAN)runtime_comp_bind(&s->model, s->desc, bytes, path, epoch, s, begin, end, live,
                                        info, w);
    unlock(s);
done:
    if (info)
        ExFreePool(info);
    return result;
}

power_result_t runtime_comp_win_f(runtime_comp_win_t* s, ULONG comp, ULONG state)
{
    comp_state_work_t* w = work(s);
    power_execution_t trace = {0};
    power_result_t result = POWER_UNSUPPORTED;
    if (w && lock(s)) {
        result = runtime_comp_f(&s->model, comp, state, w, &trace);
        unlock(s);
    }
    return result;
}

power_result_t runtime_comp_win_p(runtime_comp_win_t* s, ULONG comp, const uint32_t* sets,
                                  const uint32_t* states, ULONG cnt)
{
    comp_state_work_t* w = work(s);
    power_execution_t trace = {0};
    power_result_t result = POWER_UNSUPPORTED;
    if (w && lock(s)) {
        result = runtime_comp_p(&s->model, comp, sets, states, cnt, w, &trace);
        unlock(s);
    }
    return result;
}

BOOLEAN runtime_comp_win_query(runtime_comp_win_t* s, ULONG comp, ULONG set, uint32_t* state)
{
    comp_state_work_t* w = work(s);
    BOOLEAN result = FALSE;
    if (w && lock(s)) {
        result = (BOOLEAN)runtime_comp_query(&s->model, comp, set, w, state);
        unlock(s);
    }
    return result;
}

BOOLEAN runtime_comp_win_dx(runtime_comp_win_t* s, ULONG target)
{
    comp_state_work_t* w = work(s);
    BOOLEAN result = FALSE;
    if (w && lock(s)) {
        result = (BOOLEAN)runtime_comp_dx(&s->model, target, w);
        unlock(s);
    }
    return result;
}

BOOLEAN runtime_comp_win_close(runtime_comp_win_t* s)
{
    comp_state_work_t* w = work(s);
    BOOLEAN result = FALSE;
    if (w && lock(s)) {
        result = (BOOLEAN)runtime_comp_close(&s->model, w);
        unlock(s);
    }
    return result;
}

BOOLEAN runtime_comp_win_reopen(runtime_comp_win_t* s, const void* blob, ULONG bytes)
{
    comp_state_work_t* w = work(s);
    power_execution_t trace = {0};
    ULONG c;
    BOOLEAN result = FALSE;
    if (s && w && blob && bytes == s->desc_bytes && lock(s)) {
        if (s->model.retain || s->model.owner.quarantined ||
            RtlCompareMemory(blob, s->desc, bytes) != bytes || !runtime_comp_dx(&s->model, 0u, w))
            goto done;
        s->model.endpoint.bound = 1u;
        s->model.retain = 1u;
        for (c = 0u; c < s->model.components; ++c)
            if (runtime_comp_f(&s->model, c, 0u, w, &trace) != POWER_OK)
                goto done;
        result = TRUE;
    done:
        unlock(s);
    }
    return result;
}

BOOLEAN runtime_comp_win_forget(runtime_comp_win_t* s)
{
    if (!s)
        return FALSE;
    if (!lock(s)) {
        InterlockedExchange(&s->proof_lost, 1);
        return FALSE;
    }
    runtime_comp_forget(&s->model);
    unlock(s);
    return TRUE;
}
