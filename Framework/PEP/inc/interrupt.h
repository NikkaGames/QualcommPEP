#pragma once
#include "driver.h"

#define INTERRUPT_HISTORY_CAPACITY 128u
#define INTERRUPT_PORT_CAPACITY 16u
#define INTERRUPT_MAP_CAPACITY 256u
#define INTERRUPT_WAKE_TOP_CAPACITY 16u

typedef enum interrupt_event_code {
    INTERRUPT_EVENT_INIT = 1u,
    INTERRUPT_EVENT_ENABLE = 2u,
    INTERRUPT_EVENT_DISABLE = 3u,
    INTERRUPT_EVENT_ISR = 4u,
    INTERRUPT_EVENT_DPC_QUEUED = 5u,
    INTERRUPT_EVENT_DPC_RUN = 6u,
    INTERRUPT_EVENT_IDLE_SCAN = 7u,
    INTERRUPT_EVENT_IDLE_WAKE_SNAPSHOT = 8u,
    INTERRUPT_EVENT_MAP_LOOKUP_HIT = 9u,
    INTERRUPT_EVENT_MAP_LOOKUP_MISS = 10u,
    INTERRUPT_EVENT_DEINIT = 11u,
    INTERRUPT_EVENT_WAKE_OFFSET = 12u
} interrupt_event_code_t;

typedef struct interrupt_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG port_id;
    ULONG message_id;
    ULONG interrupt_id;
    LONG map_offset;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
} interrupt_event_t;

typedef struct interrupt_port_snapshot {
    ULONG port_id;
    ULONG isr_cnt;
    ULONG dpc_cnt;
    ULONG enable_cnt;
    ULONG disable_cnt;
    ULONG dpc_queued_cnt;
    ULONG last_message_id;
    ULONGLONG last_isr_tick100ns;
    ULONGLONG last_dpc_tick100ns;
} interrupt_port_snapshot_t;

typedef struct interrupt_map_entry {
    ULONG interrupt_id;
    ULONG trigger_type;
    ULONG driver_id;
    ULONG flags;
} interrupt_map_entry_t;

typedef struct interrupt_wake_counter {
    ULONG offset;
    ULONG interrupt_id;
    ULONG hit_cnt;
} interrupt_wake_counter_t;

typedef struct interrupt_snapshot {
    ULONG version;
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
    ULONGLONG wake_bit_hit_cnt;
    ULONG last_wake_offset;
    ULONG last_wake_interrupt_id;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
    ULONG port_cnt;
    interrupt_port_snapshot_t ports[INTERRUPT_PORT_CAPACITY];
    ULONG wake_top_cnt;
    interrupt_wake_counter_t wake_top[INTERRUPT_WAKE_TOP_CAPACITY];
} interrupt_snapshot_t;

NTSTATUS interrupt_init(_In_ WDFDEVICE dev);
VOID interrupt_deinit(_In_ WDFDEVICE dev);
VOID interrupt_record_idle_scan(_In_ ULONG level, _In_ ULONG coordinated_mode_idx);
VOID interrupt_record_idle_wake(_In_ ULONG coordinated_mode_idx, _In_ ULONG irq_status,
                                _In_ ULONG geni_status, _In_ ULONG cmd0, _In_ ULONG cmd_ctrl);
VOID interrupt_record_wake_offset(_In_ ULONG offset, _In_ ULONG coordinated_mode_idx);
VOID interrupt_get_snapshot(_Out_ interrupt_snapshot_t* snapshot);
NTSTATUS interrupt_get_history(_Out_writes_to_(capacity, *cnt) interrupt_event_t* entries,
                               _In_ ULONG capacity, _Out_ ULONG* cnt, _Out_ ULONG* dropped);
NTSTATUS interrupt_get_wake_counters(_Out_writes_to_(capacity, *cnt)
                                         interrupt_wake_counter_t* entries,
                                     _In_ ULONG capacity, _Out_ ULONG* cnt);
LONG interrupt_find_offset(_In_ ULONG interrupt_id);
NTSTATUS interrupt_lookup_mapping(_In_ ULONG interrupt_id, _Out_ LONG* offset,
                                  _Out_opt_ ULONG* trigger_type, _Out_opt_ ULONG* driver_id,
                                  _Out_opt_ ULONG* flags);
