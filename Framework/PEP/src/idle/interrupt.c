#include "interrupt.h"
#include "resource_cache.h"
#include "stats.h"
#include "trace.h"
#include "interrupt.tmh"

typedef struct interrupt_port_entry {
    BOOLEAN in_use;
    ULONG port_id;
    ULONG isr_cnt;
    ULONG dpc_cnt;
    ULONG enable_cnt;
    ULONG disable_cnt;
    ULONG dpc_queued_cnt;
    ULONG last_message_id;
    ULONGLONG last_isr_tick100ns;
    ULONGLONG last_dpc_tick100ns;
} interrupt_port_entry_t;

typedef struct interrupt_ctx {
    volatile LONG infra_state;
    KSPIN_LOCK lock;
    ULONG isr_cnt;
    ULONG dpc_cnt;
    ULONG enable_cnt;
    ULONG disable_cnt;
    ULONG dpc_queued_cnt;
    ULONG last_message_id;
    ULONGLONG last_isr_tick100ns;
    ULONGLONG last_dpc_tick100ns;
    ULONG idle_scan_cnt;
    ULONG idle_wake_sample_cnt;
    ULONG last_idle_level;
    ULONG last_idle_mode_idx;
    ULONG last_wake_irq_status;
    ULONG last_wake_geni_status;
    ULONG last_wake_cmd0;
    ULONG last_wake_cmd_ctrl;
    ULONG map_cnt;
    ULONG map_hit_cnt;
    ULONG map_miss_cnt;
    ULONG last_lookup_interrupt_id;
    LONG last_lookup_offset;
    ULONG last_lookup_trigger_type;
    ULONG last_lookup_driver_id;
    ULONG last_lookup_flags;
    ULONG last_map_query_status;
    ULONGLONG wake_bit_hit_cnt;
    ULONG last_wake_offset;
    ULONG last_wake_interrupt_id;
    ULONG wake_counters[INTERRUPT_MAP_CAPACITY];
    ULONG port_cnt;
    interrupt_port_entry_t ports[INTERRUPT_PORT_CAPACITY];
    interrupt_map_entry_t map[INTERRUPT_MAP_CAPACITY];
    ULONGLONG event_sequence;
    ULONG history_head;
    ULONG history_cnt;
    ULONG history_dropped;
    interrupt_event_t history[INTERRUPT_HISTORY_CAPACITY];
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
} interrupt_ctx_t;

static interrupt_ctx_t g_interrupt;

static const interrupt_map_entry_t g_default_map[] = {
    {512u, 1u, 2u, 0u}, {513u, 1u, 2u, 0u}, {514u, 1u, 2u, 0u}, {515u, 1u, 0u, 0u},
    {516u, 1u, 2u, 0u}, {517u, 1u, 2u, 0u}, {518u, 1u, 2u, 0u}, {519u, 1u, 2u, 0u},
    {520u, 1u, 2u, 0u}, {521u, 1u, 2u, 0u}, {522u, 1u, 2u, 0u}, {523u, 1u, 2u, 0u},
    {524u, 1u, 2u, 0u}, {525u, 1u, 2u, 0u}, {526u, 1u, 2u, 0u}, {527u, 1u, 2u, 0u},
    {528u, 1u, 2u, 0u}, {529u, 1u, 2u, 0u}, {530u, 1u, 2u, 0u}, {531u, 1u, 2u, 0u},
    {532u, 1u, 2u, 0u}, {533u, 1u, 2u, 0u}, {534u, 1u, 2u, 0u}, {535u, 1u, 2u, 0u},
    {536u, 1u, 2u, 0u}, {537u, 1u, 2u, 0u}, {538u, 1u, 2u, 0u}, {539u, 1u, 2u, 0u},
    {540u, 1u, 2u, 0u}, {541u, 1u, 2u, 0u}, {542u, 1u, 2u, 0u}, {543u, 1u, 2u, 0u},
    {544u, 1u, 2u, 0u}, {545u, 1u, 2u, 0u}, {546u, 1u, 2u, 0u}, {547u, 1u, 2u, 0u},
    {548u, 1u, 2u, 0u}, {549u, 1u, 2u, 0u}, {550u, 1u, 2u, 0u}, {551u, 1u, 2u, 0u},
    {172u, 1u, 2u, 0u}, {173u, 1u, 2u, 0u}, {174u, 1u, 2u, 0u}, {175u, 1u, 2u, 0u},
    {176u, 1u, 2u, 0u}, {177u, 1u, 2u, 0u}, {178u, 1u, 2u, 0u}, {179u, 1u, 2u, 0u},
    {180u, 1u, 2u, 0u}, {181u, 1u, 2u, 0u}, {182u, 1u, 2u, 0u}, {183u, 1u, 2u, 0u},
    {184u, 1u, 2u, 0u}, {185u, 1u, 2u, 0u}, {295u, 1u, 2u, 0u}, {338u, 1u, 2u, 0u},
    {339u, 1u, 2u, 0u}, {340u, 1u, 2u, 0u}, {341u, 1u, 2u, 0u}, {344u, 1u, 2u, 0u},
    {345u, 1u, 2u, 0u}, {346u, 1u, 2u, 0u}, {406u, 1u, 2u, 0u}, {407u, 1u, 2u, 0u},
    {466u, 1u, 2u, 0u}, {467u, 1u, 2u, 0u}, {470u, 1u, 2u, 0u}, {471u, 1u, 2u, 0u},
    {472u, 1u, 2u, 0u}, {118u, 1u, 2u, 0u}, {552u, 1u, 2u, 0u}, {553u, 1u, 2u, 0u},
    {554u, 1u, 2u, 0u}, {555u, 1u, 2u, 0u}, {556u, 1u, 2u, 0u}, {557u, 1u, 2u, 0u},
    {558u, 1u, 2u, 0u}, {559u, 1u, 2u, 0u}, {560u, 1u, 2u, 0u}, {561u, 1u, 2u, 0u},
    {562u, 1u, 2u, 0u}, {563u, 1u, 2u, 0u}, {564u, 1u, 2u, 0u}, {565u, 1u, 2u, 0u},
    {566u, 1u, 2u, 0u}, {567u, 1u, 2u, 0u}, {568u, 1u, 2u, 0u}, {569u, 1u, 2u, 0u},
    {570u, 1u, 2u, 0u}, {571u, 1u, 2u, 0u}, {572u, 1u, 2u, 0u}, {573u, 1u, 2u, 0u},
    {574u, 1u, 2u, 0u}, {575u, 1u, 2u, 0u}, {576u, 1u, 2u, 0u}, {577u, 1u, 2u, 0u},
    {578u, 1u, 2u, 0u}, {579u, 1u, 2u, 0u}, {580u, 1u, 2u, 0u}, {581u, 1u, 2u, 0u},
    {582u, 1u, 2u, 0u}, {583u, 1u, 2u, 0u}, {584u, 1u, 2u, 0u}, {585u, 1u, 2u, 0u},
    {586u, 1u, 2u, 0u}, {587u, 1u, 2u, 0u}, {588u, 1u, 2u, 0u}, {589u, 1u, 2u, 0u},
    {590u, 1u, 2u, 0u}, {591u, 1u, 2u, 0u}, {592u, 1u, 2u, 0u}, {593u, 1u, 2u, 0u},
    {594u, 1u, 2u, 0u}, {595u, 1u, 2u, 0u}, {596u, 1u, 2u, 0u}, {597u, 1u, 2u, 0u},
    {598u, 1u, 2u, 0u}, {599u, 1u, 2u, 0u}, {600u, 1u, 2u, 0u}, {601u, 1u, 2u, 0u},
    {602u, 1u, 2u, 0u}, {603u, 1u, 2u, 0u}, {604u, 1u, 2u, 0u}, {605u, 1u, 2u, 0u},
    {641u, 1u, 2u, 0u}, {642u, 1u, 2u, 0u}, {643u, 1u, 2u, 0u}, {644u, 1u, 2u, 0u},
    {645u, 1u, 2u, 0u}, {646u, 1u, 2u, 0u}, {647u, 1u, 2u, 0u}, {648u, 1u, 2u, 0u},
    {649u, 1u, 2u, 0u}, {650u, 1u, 2u, 0u}, {651u, 1u, 2u, 0u}, {652u, 1u, 2u, 0u},
    {653u, 1u, 2u, 0u}, {654u, 1u, 2u, 0u}, {655u, 1u, 2u, 0u}, {656u, 1u, 2u, 0u},
    {657u, 1u, 2u, 0u}, {658u, 1u, 2u, 0u}, {659u, 1u, 2u, 0u}, {660u, 1u, 2u, 0u},
    {661u, 1u, 2u, 0u}, {662u, 1u, 2u, 0u}, {663u, 1u, 2u, 0u}, {664u, 1u, 2u, 0u},
    {665u, 1u, 2u, 0u}, {666u, 1u, 2u, 0u}, {667u, 1u, 2u, 0u}, {668u, 1u, 2u, 0u},
    {0u, 1u, 0u, 0u},   {0u, 1u, 0u, 0u},   {0u, 1u, 0u, 0u},   {0u, 1u, 0u, 0u},
    {0u, 1u, 0u, 0u},   {0u, 1u, 0u, 0u},   {0u, 1u, 0u, 0u},   {670u, 1u, 2u, 0u},
    {752u, 1u, 2u, 0u}, {753u, 1u, 2u, 0u}, {754u, 1u, 2u, 0u}, {755u, 1u, 2u, 0u},
    {756u, 1u, 2u, 0u}, {757u, 1u, 2u, 0u}, {758u, 1u, 2u, 0u}, {759u, 1u, 2u, 0u},
    {833u, 1u, 2u, 0u},
};

static __forceinline ULONG interrupt_get_port_id(_In_opt_ WDFDEVICE dev)
{
    if (!dev)
        return 0;

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    return ctx ? ctx->port_id : 0;
}

static VOID interrupt_insert_wake_top(_Inout_updates_(INTERRUPT_WAKE_TOP_CAPACITY)
                                          interrupt_wake_counter_t* top,
                                      _Inout_ ULONG* top_cnt, _In_ ULONG offset,
                                      _In_ ULONG interrupt_id, _In_ ULONG hit_cnt)
{
    if (!top || !top_cnt || hit_cnt == 0)
        return;

    ULONG cnt = *top_cnt;
    ULONG cap = INTERRUPT_WAKE_TOP_CAPACITY;
    if (cap == 0)
        return;

    ULONG pos = cnt;
    if (cnt < cap) {
        *top_cnt = cnt + 1;
    } else {
        interrupt_wake_counter_t* tail = &top[cnt - 1];
        if (hit_cnt < tail->hit_cnt || (hit_cnt == tail->hit_cnt && offset >= tail->offset))
            return;
        pos = cnt - 1;
    }

    while (pos > 0) {
        interrupt_wake_counter_t* prev = &top[pos - 1];
        if (hit_cnt < prev->hit_cnt)
            break;
        if (hit_cnt == prev->hit_cnt && offset >= prev->offset)
            break;
        top[pos] = *prev;
        pos--;
    }

    top[pos].offset = offset;
    top[pos].interrupt_id = interrupt_id;
    top[pos].hit_cnt = hit_cnt;
}

static VOID interrupt_publish_summary(_In_ ULONG event_code)
{
    res_key_t k;
    res_value_t v;

    ULONG isr = 0;
    ULONG dpc = 0;
    ULONG en = 0;
    ULONG dis = 0;
    ULONG dq = 0;
    ULONG msg = 0;
    ULONGLONG isr_tick = 0;

    ULONG map_cnt = 0;
    ULONG map_hit = 0;
    ULONG map_miss = 0;
    ULONG wake_hits = 0;
    ULONG wake_off = 0xFFFFFFFFu;
    ULONG wake_irq = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    isr = g_interrupt.isr_cnt;
    dpc = g_interrupt.dpc_cnt;
    en = g_interrupt.enable_cnt;
    dis = g_interrupt.disable_cnt;
    dq = g_interrupt.dpc_queued_cnt;
    msg = g_interrupt.last_message_id;
    isr_tick = g_interrupt.last_isr_tick100ns;

    map_cnt = g_interrupt.map_cnt;
    map_hit = g_interrupt.map_hit_cnt;
    map_miss = g_interrupt.map_miss_cnt;
    wake_hits = (ULONG)g_interrupt.wake_bit_hit_cnt;
    wake_off = g_interrupt.last_wake_offset;
    wake_irq = g_interrupt.last_wake_interrupt_id;

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    k.res_id = 0x52544E49u;
    k.sub_type = 0;
    v.value0 = isr;
    v.value1 = dpc;
    v.value2 = (event_code & 0xFFFFu) | ((msg & 0xFFFFu) << 16);
    v.value3 = (ULONG)isr_tick;
    (VOID) res_set(k, v);

    k.res_id = 0x30544E49u;
    k.sub_type = 0;
    v.value0 = isr;
    v.value1 = dpc;
    v.value2 = en;
    v.value3 = dis;
    (VOID) res_set(k, v);

    k.res_id = 0x31544E49u;
    k.sub_type = event_code;
    v.value0 = dq;
    v.value1 = msg;
    v.value2 = map_hit;
    v.value3 = map_miss;
    (VOID) res_set(k, v);

    k.res_id = 0x33544E49u;
    k.sub_type = 0;
    v.value0 = map_cnt;
    v.value1 = map_hit;
    v.value2 = map_miss;
    v.value3 = event_code;
    (VOID) res_set(k, v);

    k.res_id = 0x34544E49u;
    k.sub_type = 0;
    v.value0 = wake_hits;
    v.value1 = wake_off;
    v.value2 = wake_irq;
    v.value3 = event_code;
    (VOID) res_set(k, v);
}

static VOID interrupt_publish_idle_wake(_In_ ULONG coordinated_mode_idx, _In_ ULONG irq_status,
                                        _In_ ULONG geni_status, _In_ ULONG cmd0,
                                        _In_ ULONG cmd_ctrl)
{
    res_key_t k;
    res_value_t v;

    k.res_id = 0x32544E49u;
    k.sub_type = coordinated_mode_idx;
    v.value0 = irq_status;
    v.value1 = geni_status;
    v.value2 = cmd0;
    v.value3 = cmd_ctrl;
    (VOID) res_set(k, v);
}

static VOID interrupt_record_event(_In_ ULONG event_code, _In_ NTSTATUS status, _In_ ULONG port_id,
                                   _In_ ULONG message_id, _In_ ULONG interrupt_id,
                                   _In_ LONG map_offset, _In_ ULONG value0, _In_ ULONG value1,
                                   _In_ ULONG value2, _In_ ULONG value3)
{
    ULONGLONG tick = KeQueryInterruptTime();

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    ULONGLONG seq = ++g_interrupt.event_sequence;
    ULONG idx = g_interrupt.history_head % INTERRUPT_HISTORY_CAPACITY;

    interrupt_event_t* e = &g_interrupt.history[idx];
    e->tick100ns = tick;
    e->sequence = seq;
    e->event_code = event_code;
    e->status = (LONG)status;
    e->port_id = port_id;
    e->message_id = message_id;
    e->interrupt_id = interrupt_id;
    e->map_offset = map_offset;
    e->value0 = value0;
    e->value1 = value1;
    e->value2 = value2;
    e->value3 = value3;

    g_interrupt.history_head = (idx + 1u) % INTERRUPT_HISTORY_CAPACITY;
    if (g_interrupt.history_cnt < INTERRUPT_HISTORY_CAPACITY)
        g_interrupt.history_cnt++;
    else
        g_interrupt.history_dropped++;

    g_interrupt.last_event_code = event_code;
    g_interrupt.last_event_status = (LONG)status;
    g_interrupt.last_event_tick100ns = tick;

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    stats_record_event(0x600u + event_code, status, port_id, message_id, interrupt_id, value0);
    interrupt_publish_summary(event_code);
}

static VOID interrupt_ensure_infra(VOID)
{
    if (InterlockedCompareExchange(&g_interrupt.infra_state, 1, 0) != 0)
        return;

    RtlZeroMemory(&g_interrupt, sizeof(g_interrupt));
    g_interrupt.infra_state = 1;
    KeInitializeSpinLock(&g_interrupt.lock);

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    ULONG copy_cnt = RTL_NUMBER_OF(g_default_map);
    if (copy_cnt > RTL_NUMBER_OF(g_interrupt.map))
        copy_cnt = RTL_NUMBER_OF(g_interrupt.map);

    for (ULONG i = 0; i < copy_cnt; i++) {
        if (g_default_map[i].interrupt_id == 0)
            continue;
        g_interrupt.map[g_interrupt.map_cnt++] = g_default_map[i];
    }
    g_interrupt.last_wake_offset = 0xFFFFFFFFu;

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    interrupt_publish_summary(0);
    interrupt_record_event(INTERRUPT_EVENT_INIT, STATUS_SUCCESS, 0, 0, 0, -1, g_interrupt.map_cnt,
                           0, 0, 0);
}

LONG interrupt_find_offset(_In_ ULONG interrupt_id)
{
    LONG offset = -1;
    NTSTATUS st = interrupt_lookup_mapping(interrupt_id, &offset, NULL, NULL, NULL);
    if (!NT_SUCCESS(st))
        return -1;
    return offset;
}

NTSTATUS interrupt_lookup_mapping(_In_ ULONG interrupt_id, _Out_ LONG* offset,
                                  _Out_opt_ ULONG* trigger_type, _Out_opt_ ULONG* driver_id,
                                  _Out_opt_ ULONG* flags)
{
    if (!offset)
        return STATUS_INVALID_PARAMETER;

    interrupt_ensure_infra();

    LONG found = -1;
    ULONG trig = 0;
    ULONG drv = 0;
    ULONG fl = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    for (ULONG i = 0; i < g_interrupt.map_cnt; i++) {
        if (g_interrupt.map[i].interrupt_id == interrupt_id) {
            found = (LONG)i;
            trig = g_interrupt.map[i].trigger_type;
            drv = g_interrupt.map[i].driver_id;
            fl = g_interrupt.map[i].flags;
            break;
        }
    }

    g_interrupt.last_lookup_interrupt_id = interrupt_id;
    g_interrupt.last_lookup_offset = found;
    g_interrupt.last_lookup_trigger_type = trig;
    g_interrupt.last_lookup_driver_id = drv;
    g_interrupt.last_lookup_flags = fl;

    if (found >= 0) {
        g_interrupt.map_hit_cnt++;
        g_interrupt.last_map_query_status = STATUS_SUCCESS;
    } else {
        g_interrupt.map_miss_cnt++;
        g_interrupt.last_map_query_status = STATUS_NOT_FOUND;
    }

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    *offset = found;
    if (trigger_type)
        *trigger_type = trig;
    if (driver_id)
        *driver_id = drv;
    if (flags)
        *flags = fl;

    if (found >= 0) {
        interrupt_record_event(INTERRUPT_EVENT_MAP_LOOKUP_HIT, STATUS_SUCCESS, 0, 0, interrupt_id,
                               found, trig, drv, fl, 0);
        return STATUS_SUCCESS;
    }

    interrupt_record_event(INTERRUPT_EVENT_MAP_LOOKUP_MISS, STATUS_NOT_FOUND, 0, 0, interrupt_id,
                           -1, 0, 0, 0, 0);
    return STATUS_NOT_FOUND;
}

/* without a verified DRV2 resource pair, interrupt creation could bind an unrelated PEP0 vector. No
 * controller grant or ACK routine is available here. */

NTSTATUS interrupt_init(_In_ WDFDEVICE dev)
{
    p_dev_ctx_t ctx;
    if (!dev)
        return STATUS_INVALID_PARAMETER;
    interrupt_ensure_infra();
    ctx = get_dev_ctx(dev);
    if (!ctx || ctx->is_control_dev)
        return STATUS_SUCCESS;
    if (ctx->interrupt) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_RPMH,
                    "rpmh: interrupt already exists ownership=unverified");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    interrupt_record_event(INTERRUPT_EVENT_INIT, STATUS_NOT_SUPPORTED, interrupt_get_port_id(dev),
                           0, 0, -1, 0, 0, 0, 0);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_RPMH,
                "rpmh: interrupt registration absent owner=unverified");
    return STATUS_SUCCESS;
}

VOID interrupt_deinit(_In_ WDFDEVICE dev)
{
    interrupt_ensure_infra();

    p_dev_ctx_t ctx = get_dev_ctx(dev);
    if (!ctx)
        return;

    ULONG port = interrupt_get_port_id(dev);

    if (ctx->interrupt) {
        WdfObjectDelete(ctx->interrupt);
        ctx->interrupt = NULL;
    }

    interrupt_record_event(INTERRUPT_EVENT_DEINIT, STATUS_SUCCESS, port, 0, 0, -1, 0, 0, 0, 0);
}

VOID interrupt_record_idle_scan(_In_ ULONG level, _In_ ULONG coordinated_mode_idx)
{
    interrupt_ensure_infra();

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);
    g_interrupt.idle_scan_cnt++;
    g_interrupt.last_idle_level = level;
    g_interrupt.last_idle_mode_idx = coordinated_mode_idx;
    KeReleaseSpinLock(&g_interrupt.lock, irql);

    interrupt_record_event(INTERRUPT_EVENT_IDLE_SCAN, STATUS_SUCCESS, 0, 0, 0, -1, level,
                           coordinated_mode_idx, g_interrupt.idle_scan_cnt, 0);
}

VOID interrupt_record_idle_wake(_In_ ULONG coordinated_mode_idx, _In_ ULONG irq_status,
                                _In_ ULONG geni_status, _In_ ULONG cmd0, _In_ ULONG cmd_ctrl)
{
    interrupt_ensure_infra();

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);
    g_interrupt.idle_wake_sample_cnt++;
    g_interrupt.last_idle_mode_idx = coordinated_mode_idx;
    g_interrupt.last_wake_irq_status = irq_status;
    g_interrupt.last_wake_geni_status = geni_status;
    g_interrupt.last_wake_cmd0 = cmd0;
    g_interrupt.last_wake_cmd_ctrl = cmd_ctrl;
    KeReleaseSpinLock(&g_interrupt.lock, irql);

    interrupt_publish_idle_wake(coordinated_mode_idx, irq_status, geni_status, cmd0, cmd_ctrl);
    interrupt_record_event(INTERRUPT_EVENT_IDLE_WAKE_SNAPSHOT, STATUS_SUCCESS, 0, 0, 0, -1,
                           irq_status, geni_status, cmd0, cmd_ctrl);
}

VOID interrupt_record_wake_offset(_In_ ULONG offset, _In_ ULONG coordinated_mode_idx)
{
    interrupt_ensure_infra();

    NTSTATUS st = STATUS_SUCCESS;
    LONG map_offset = (LONG)offset;
    ULONG interrupt_id = 0;
    ULONG hit_cnt = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    if (offset < g_interrupt.map_cnt) {
        if (g_interrupt.wake_counters[offset] != MAXULONG)
            g_interrupt.wake_counters[offset]++;

        g_interrupt.wake_bit_hit_cnt++;
        g_interrupt.last_wake_offset = offset;
        g_interrupt.last_wake_interrupt_id = g_interrupt.map[offset].interrupt_id;

        interrupt_id = g_interrupt.map[offset].interrupt_id;
        hit_cnt = g_interrupt.wake_counters[offset];
    } else {
        st = STATUS_INVALID_PARAMETER;
        map_offset = -1;
        g_interrupt.last_wake_offset = 0xFFFFFFFFu;
        g_interrupt.last_wake_interrupt_id = 0;
    }

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    if (NT_SUCCESS(st)) {
        if (hit_cnt == 1 || (hit_cnt & 0xFFu) == 0) {
            interrupt_record_event(INTERRUPT_EVENT_WAKE_OFFSET, STATUS_SUCCESS, 0, 0, interrupt_id,
                                   map_offset, offset, hit_cnt, coordinated_mode_idx, 0);
        }
        return;
    }

    if ((offset & 0x1Fu) == 0u) {
        interrupt_record_event(INTERRUPT_EVENT_WAKE_OFFSET, st, 0, 0, 0, -1, offset,
                               coordinated_mode_idx, 0, 0);
    }
}

VOID interrupt_get_snapshot(_Out_ interrupt_snapshot_t* snapshot)
{
    if (!snapshot)
        return;

    interrupt_ensure_infra();

    RtlZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->version = 3;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    snapshot->isr_cnt = g_interrupt.isr_cnt;
    snapshot->dpc_cnt = g_interrupt.dpc_cnt;
    snapshot->enable_cnt = g_interrupt.enable_cnt;
    snapshot->disable_cnt = g_interrupt.disable_cnt;
    snapshot->dpc_queued_cnt = g_interrupt.dpc_queued_cnt;
    snapshot->last_message_id = g_interrupt.last_message_id;
    snapshot->last_isr_tick100ns = g_interrupt.last_isr_tick100ns;
    snapshot->last_dpc_tick100ns = g_interrupt.last_dpc_tick100ns;
    snapshot->idle_scan_cnt = g_interrupt.idle_scan_cnt;
    snapshot->idle_wake_sample_cnt = g_interrupt.idle_wake_sample_cnt;
    snapshot->last_idle_level = g_interrupt.last_idle_level;
    snapshot->last_idle_mode_idx = g_interrupt.last_idle_mode_idx;
    snapshot->last_wake_irq_status = g_interrupt.last_wake_irq_status;
    snapshot->last_wake_geni_status = g_interrupt.last_wake_geni_status;
    snapshot->last_wake_cmd0 = g_interrupt.last_wake_cmd0;
    snapshot->last_wake_cmd_ctrl = g_interrupt.last_wake_cmd_ctrl;
    snapshot->map_cnt = g_interrupt.map_cnt;
    snapshot->map_hit_cnt = g_interrupt.map_hit_cnt;
    snapshot->map_miss_cnt = g_interrupt.map_miss_cnt;
    snapshot->last_lookup_interrupt_id = g_interrupt.last_lookup_interrupt_id;
    snapshot->last_lookup_offset = g_interrupt.last_lookup_offset;
    snapshot->last_lookup_trigger_type = g_interrupt.last_lookup_trigger_type;
    snapshot->last_lookup_driver_id = g_interrupt.last_lookup_driver_id;
    snapshot->wake_bit_hit_cnt = g_interrupt.wake_bit_hit_cnt;
    snapshot->last_wake_offset = g_interrupt.last_wake_offset;
    snapshot->last_wake_interrupt_id = g_interrupt.last_wake_interrupt_id;
    snapshot->event_sequence = g_interrupt.event_sequence;
    snapshot->history_cnt = g_interrupt.history_cnt;
    snapshot->history_dropped = g_interrupt.history_dropped;
    snapshot->last_event_code = g_interrupt.last_event_code;
    snapshot->last_event_status = g_interrupt.last_event_status;
    snapshot->last_event_tick100ns = g_interrupt.last_event_tick100ns;

    snapshot->port_cnt = g_interrupt.port_cnt;
    if (snapshot->port_cnt > RTL_NUMBER_OF(snapshot->ports))
        snapshot->port_cnt = RTL_NUMBER_OF(snapshot->ports);

    for (ULONG i = 0; i < snapshot->port_cnt; i++) {
        snapshot->ports[i].port_id = g_interrupt.ports[i].port_id;
        snapshot->ports[i].isr_cnt = g_interrupt.ports[i].isr_cnt;
        snapshot->ports[i].dpc_cnt = g_interrupt.ports[i].dpc_cnt;
        snapshot->ports[i].enable_cnt = g_interrupt.ports[i].enable_cnt;
        snapshot->ports[i].disable_cnt = g_interrupt.ports[i].disable_cnt;
        snapshot->ports[i].dpc_queued_cnt = g_interrupt.ports[i].dpc_queued_cnt;
        snapshot->ports[i].last_message_id = g_interrupt.ports[i].last_message_id;
        snapshot->ports[i].last_isr_tick100ns = g_interrupt.ports[i].last_isr_tick100ns;
        snapshot->ports[i].last_dpc_tick100ns = g_interrupt.ports[i].last_dpc_tick100ns;
    }

    snapshot->wake_top_cnt = 0;
    for (ULONG i = 0; i < g_interrupt.map_cnt; i++) {
        ULONG hit_cnt = g_interrupt.wake_counters[i];
        if (hit_cnt == 0)
            continue;
        interrupt_insert_wake_top(snapshot->wake_top, &snapshot->wake_top_cnt, i,
                                  g_interrupt.map[i].interrupt_id, hit_cnt);
    }

    KeReleaseSpinLock(&g_interrupt.lock, irql);
}

NTSTATUS interrupt_get_history(_Out_writes_to_(capacity, *cnt) interrupt_event_t* entries,
                               _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped)
{
    if (!entries || !cnt || !dropped || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    interrupt_ensure_infra();

    *cnt = 0;
    *dropped = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    ULONG avail = g_interrupt.history_cnt;
    ULONG take = (capacity < avail) ? capacity : avail;
    ULONG skip = avail - take;

    ULONG start = (g_interrupt.history_head + INTERRUPT_HISTORY_CAPACITY - avail + skip) %
                  INTERRUPT_HISTORY_CAPACITY;

    for (ULONG i = 0; i < take; i++) {
        ULONG idx = (start + i) % INTERRUPT_HISTORY_CAPACITY;
        entries[i] = g_interrupt.history[idx];
    }

    *cnt = take;
    *dropped = g_interrupt.history_dropped + skip;

    KeReleaseSpinLock(&g_interrupt.lock, irql);

    return STATUS_SUCCESS;
}

NTSTATUS interrupt_get_wake_counters(_Out_writes_to_(capacity, *cnt)
                                         interrupt_wake_counter_t* entries,
                                     _In_ ULONG capacity, _Out_ ULONG* cnt)
{
    if (!entries || !cnt || capacity == 0)
        return STATUS_INVALID_PARAMETER;

    interrupt_ensure_infra();

    *cnt = 0;

    KIRQL irql;
    KeAcquireSpinLock(&g_interrupt.lock, &irql);

    ULONG out = 0;
    for (ULONG i = 0; i < g_interrupt.map_cnt && out < capacity; i++) {
        ULONG hit_cnt = g_interrupt.wake_counters[i];
        if (hit_cnt == 0)
            continue;

        entries[out].offset = i;
        entries[out].interrupt_id = g_interrupt.map[i].interrupt_id;
        entries[out].hit_cnt = hit_cnt;
        out++;
    }

    KeReleaseSpinLock(&g_interrupt.lock, irql);
    *cnt = out;
    return STATUS_SUCCESS;
}
