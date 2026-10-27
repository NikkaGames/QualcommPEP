#include "ex_soc_mgmt.h"

static volatile LONG g_ex_soc_init;
static KSPIN_LOCK g_ex_soc_lock;
static exsoc_dev_state_t g_ex_soc_devices[EXSOC_DEV_CAPACITY];
static ULONG g_ex_soc_dev_cnt;

static exsoc_event_t g_ex_soc_history[EXSOC_HISTORY_CAPACITY];
static ULONG g_ex_soc_history_head;
static ULONG g_ex_soc_history_cnt;
static ULONG g_ex_soc_history_dropped;
static ULONGLONG g_ex_soc_event_sequence;

static ULONG g_ex_soc_apply_cnt;
static ULONG g_ex_soc_success_cnt;
static ULONG g_ex_soc_failure_cnt;
static ULONG g_ex_soc_adsp_state_ioctl_cnt;
static ULONG g_ex_soc_adsp_ctrl_ioctl_cnt;
static ULONG g_ex_soc_last_ioctl_code;
static LONG g_ex_soc_last_status;
static ULONG g_ex_soc_last_dev_hash;
static ULONG g_ex_soc_last_d_state;
static ULONGLONG g_ex_soc_last_tick100ns;

static __forceinline VOID ex_soc_ensure_init(VOID)
{
    if (InterlockedCompareExchange(&g_ex_soc_init, 1, 0) == 0) {
        KeInitializeSpinLock(&g_ex_soc_lock);
        RtlZeroMemory(g_ex_soc_devices, sizeof(g_ex_soc_devices));
        RtlZeroMemory(g_ex_soc_history, sizeof(g_ex_soc_history));
        g_ex_soc_dev_cnt = 0;
        g_ex_soc_history_head = 0;
        g_ex_soc_history_cnt = 0;
        g_ex_soc_history_dropped = 0;
        g_ex_soc_event_sequence = 0;
        g_ex_soc_apply_cnt = 0;
        g_ex_soc_success_cnt = 0;
        g_ex_soc_failure_cnt = 0;
        g_ex_soc_adsp_state_ioctl_cnt = 0;
        g_ex_soc_adsp_ctrl_ioctl_cnt = 0;
        g_ex_soc_last_ioctl_code = 0;
        g_ex_soc_last_status = STATUS_SUCCESS;
        g_ex_soc_last_dev_hash = 0;
        g_ex_soc_last_d_state = 0;
        g_ex_soc_last_tick100ns = 0;
        /* publish readiness only after lock and data initialization. */
        InterlockedExchange(&g_ex_soc_init, 2);
    } else {
        while (InterlockedCompareExchange(&g_ex_soc_init, 2, 2) != 2)
            YieldProcessor();
    }
}

static exsoc_dev_state_t* ex_soc_find_dev_locked(_In_ ULONG dev_hash)
{
    for (ULONG i = 0; i < g_ex_soc_dev_cnt; i++) {
        if (g_ex_soc_devices[i].dev_hash == dev_hash)
            return &g_ex_soc_devices[i];
    }
    return NULL;
}

NTSTATUS ex_soc_init(_In_ WDFDRIVER driver)
{
    UNREFERENCED_PARAMETER(driver);
    ex_soc_ensure_init();
    return STATUS_SUCCESS;
}

VOID ex_soc_deinit(VOID)
{
    if (InterlockedCompareExchange(&g_ex_soc_init, 0, 1) == 1) {
        RtlZeroMemory(g_ex_soc_devices, sizeof(g_ex_soc_devices));
        RtlZeroMemory(g_ex_soc_history, sizeof(g_ex_soc_history));
        g_ex_soc_dev_cnt = 0;
        g_ex_soc_history_head = 0;
        g_ex_soc_history_cnt = 0;
        g_ex_soc_history_dropped = 0;
        g_ex_soc_event_sequence = 0;
        g_ex_soc_apply_cnt = 0;
        g_ex_soc_success_cnt = 0;
        g_ex_soc_failure_cnt = 0;
        g_ex_soc_adsp_state_ioctl_cnt = 0;
        g_ex_soc_adsp_ctrl_ioctl_cnt = 0;
        g_ex_soc_last_ioctl_code = 0;
        g_ex_soc_last_status = STATUS_SUCCESS;
        g_ex_soc_last_dev_hash = 0;
        g_ex_soc_last_d_state = 0;
        g_ex_soc_last_tick100ns = 0;
    }
}

VOID ex_soc_record_apply(_In_ ULONG ioctl_code, _In_z_ const CHAR* dev_id, _In_ ULONG dev_hash,
                         _In_ ULONG d_state, _In_ NTSTATUS status,
                         _In_ ULONG requested_output_bytes, _In_ ULONG returned_information)
{
    ex_soc_ensure_init();

    ULONGLONG tick = KeQueryInterruptTime();
    KIRQL irql;
    KeAcquireSpinLock(&g_ex_soc_lock, &irql);

    g_ex_soc_apply_cnt++;
    if (NT_SUCCESS(status))
        g_ex_soc_success_cnt++;
    else
        g_ex_soc_failure_cnt++;

    if (ioctl_code == IOCTL_QCPEP_SUBSYS_ADSP_STATE)
        g_ex_soc_adsp_state_ioctl_cnt++;
    else if (ioctl_code == IOCTL_QCPEP_SUBSYS_ADSP_CTRL)
        g_ex_soc_adsp_ctrl_ioctl_cnt++;

    g_ex_soc_last_ioctl_code = ioctl_code;
    g_ex_soc_last_status = (LONG)status;
    g_ex_soc_last_dev_hash = dev_hash;
    g_ex_soc_last_d_state = d_state;
    g_ex_soc_last_tick100ns = tick;

    exsoc_dev_state_t* d = ex_soc_find_dev_locked(dev_hash);
    if (!d && dev_hash != 0 && g_ex_soc_dev_cnt < EXSOC_DEV_CAPACITY) {
        d = &g_ex_soc_devices[g_ex_soc_dev_cnt++];
        RtlZeroMemory(d, sizeof(*d));
        d->dev_hash = dev_hash;
    }

    if (d) {
        d->last_d_state = d_state;
        d->last_ioctl_code = ioctl_code;
        d->apply_cnt++;
        if (NT_SUCCESS(status))
            d->success_cnt++;
        else
            d->failure_cnt++;
        d->last_tick100ns = tick;
        if (dev_id) {
            size_t id_len = 0;
            if (!NT_SUCCESS(RtlStringCchLengthA(dev_id, 20, &id_len)))
                id_len = 0;
            RtlZeroMemory(d->dev_id, sizeof(d->dev_id));
            if (id_len)
                RtlCopyMemory(d->dev_id, dev_id, id_len);
            d->dev_id_length = (ULONG)id_len;
        }
    }

    ULONGLONG seq = ++g_ex_soc_event_sequence;
    ULONG idx = g_ex_soc_history_head % EXSOC_HISTORY_CAPACITY;
    exsoc_event_t* e = &g_ex_soc_history[idx];
    RtlZeroMemory(e, sizeof(*e));
    e->tick100ns = tick;
    e->sequence = seq;
    e->ioctl_code = ioctl_code;
    e->status = (LONG)status;
    e->dev_hash = dev_hash;
    e->d_state = d_state;
    e->requested_output_bytes = requested_output_bytes;
    e->returned_information = returned_information;

    g_ex_soc_history_head = (idx + 1u) % EXSOC_HISTORY_CAPACITY;
    if (g_ex_soc_history_cnt < EXSOC_HISTORY_CAPACITY)
        g_ex_soc_history_cnt++;
    else
        g_ex_soc_history_dropped++;

    KeReleaseSpinLock(&g_ex_soc_lock, irql);
}

NTSTATUS ex_soc_get_snapshot(_Out_ exsoc_snapshot_t* snapshot)
{
    if (!snapshot)
        return STATUS_INVALID_PARAMETER;

    ex_soc_ensure_init();
    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 1;

    KIRQL irql;
    KeAcquireSpinLock(&g_ex_soc_lock, &irql);

    snapshot->dev_cnt = g_ex_soc_dev_cnt;
    snapshot->apply_cnt = g_ex_soc_apply_cnt;
    snapshot->success_cnt = g_ex_soc_success_cnt;
    snapshot->failure_cnt = g_ex_soc_failure_cnt;
    snapshot->adsp_state_ioctl_cnt = g_ex_soc_adsp_state_ioctl_cnt;
    snapshot->adsp_ctrl_ioctl_cnt = g_ex_soc_adsp_ctrl_ioctl_cnt;
    snapshot->event_sequence = g_ex_soc_event_sequence;
    snapshot->history_cnt = g_ex_soc_history_cnt;
    snapshot->history_dropped = g_ex_soc_history_dropped;
    snapshot->last_ioctl_code = g_ex_soc_last_ioctl_code;
    snapshot->last_status = g_ex_soc_last_status;
    snapshot->last_dev_hash = g_ex_soc_last_dev_hash;
    snapshot->last_d_state = g_ex_soc_last_d_state;
    snapshot->last_tick100ns = g_ex_soc_last_tick100ns;

    for (ULONG i = 0; i < g_ex_soc_dev_cnt && i < EXSOC_DEV_CAPACITY; i++)
        snapshot->devices[i] = g_ex_soc_devices[i];

    KeReleaseSpinLock(&g_ex_soc_lock, irql);
    return STATUS_SUCCESS;
}

NTSTATUS ex_soc_get_history(_Out_writes_to_(capacity, *cnt) exsoc_event_t* events,
                            _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!events || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    ex_soc_ensure_init();

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_ex_soc_lock, &irql);

    ULONG avail = g_ex_soc_history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;
    ULONG start =
        (g_ex_soc_history_head + EXSOC_HISTORY_CAPACITY - avail + skip) % EXSOC_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % EXSOC_HISTORY_CAPACITY;
        events[i] = g_ex_soc_history[idx];
    }

    *cnt = take;
    *dropped = g_ex_soc_history_dropped + skip;

    KeReleaseSpinLock(&g_ex_soc_lock, irql);
    return STATUS_SUCCESS;
}
