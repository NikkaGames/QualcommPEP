#include <initguid.h>
#include "wmi.h"
#include "chipinfo.h"
#include "resource_cache.h"
#include "tsens.h"
#include "mmrm.h"
#include "deep_standby.h"
#include "thermal_request.h"
#include "bcl.h"
#include "interrupt.h"
#include "soc_subsystem.h"
#include "dcvs.h"
#include "crash_dump.h"
#include "ex_soc_mgmt.h"
#include "resource_diag.h"
#include <wdmguid.h>

DEFINE_GUID(guid_wmi_provider, 0xd9c9d4b6, 0x8f60, 0x4b48, 0x9e, 0x1a, 0x9c, 0x90, 0xb7, 0x4e, 0x34,
            0xb0);

#pragma pack(push, 1)
#define WMI_STATS_HISTORY_CNT 16u

typedef struct wmi_stats_event {
    ULONG event_code;
    LONG status;
    ULONG arg0;
    ULONG arg1;
    ULONG arg2;
    ULONG arg3;
    ULONGLONG tick100ns;
} wmi_stats_event_t;

typedef struct wmi_config {
    ULONG branch_track_enabled;
    ULONG enable_directed_p_state_features;
    ULONG enable_park_selection;
    ULONG enable_pepocd_test;
    ULONG enable_pepocd_test_low_perf;
    ULONG pep_bcl_notify_level;
} wmi_config_t;

#define WMI_BRANCHTRACK_HISTORY_CNT 16u

typedef struct wmi_branchtrack_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG res_id;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
    ULONG active;
    ULONG update_cnt;
} wmi_branchtrack_event_t;

typedef struct wmi_branchtrack {
    ULONG version;
    ULONG entry_cnt;
    ULONG active_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_active;
    ULONG last_event_update_cnt;
    ULONG last_event_res_id;
    ULONG last_event_sub_type;
    ULONG last_event_value0;
    ULONG last_event_value1;
    ULONG last_event_value2;
    ULONG last_event_value3;
    ULONGLONG last_event_tick100ns;
    ULONG history_entry_cnt;
    wmi_branchtrack_event_t history[WMI_BRANCHTRACK_HISTORY_CNT];
} wmi_branchtrack_t;

#define WMI_EXSOC_HISTORY_CNT 16u

typedef struct wmi_exsoc_dev {
    ULONG dev_hash;
    CHAR dev_id[21];
    ULONG dev_id_length;
    ULONG last_d_state;
    ULONG last_ioctl_code;
    ULONG apply_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONGLONG last_tick100ns;
} wmi_exsoc_dev_t;

typedef struct wmi_exsoc_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG ioctl_code;
    LONG status;
    ULONG dev_hash;
    ULONG d_state;
    ULONG requested_output_bytes;
    ULONG returned_information;
} wmi_exsoc_event_t;

typedef struct wmi_exsoc {
    ULONG version;
    ULONG dev_cnt;
    ULONG apply_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG adsp_state_ioctl_cnt;
    ULONG adsp_ctrl_ioctl_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_ioctl_code;
    LONG last_status;
    ULONG last_dev_hash;
    ULONG last_d_state;
    ULONGLONG last_tick100ns;
    ULONG dev_entry_cnt;
    wmi_exsoc_dev_t devices[EXSOC_DEV_CAPACITY];
    ULONG history_entry_cnt;
    wmi_exsoc_event_t history[WMI_EXSOC_HISTORY_CNT];
} wmi_exsoc_t;

#define WMI_RSCSW_HISTORY_CNT 32u
#define WMI_RSCSW_CATALOG_CNT 16u
#define WMI_RSCSW_SCENARIO_HISTORY_CNT 16u
#define WMI_RSCSW_DIAG_CNT 16u
#define WMI_RSCSW_DIAG_HISTORY_CNT 16u
#define WMI_RSCSW_PROFILE_CNT 16u
#define WMI_RSCSW_PROFILE_HISTORY_CNT 16u
#define WMI_RSCSW_GRAPH_LINK_CNT 32u
#define WMI_RSCSW_ACTION_SEQUENCE_HISTORY_CNT 16u
#define WMI_RSCSW_ACTION_SEQUENCE_PREVIEW_HISTORY_CNT 16u
#define WMI_RSCSW_CHECKPOINT_CNT 16u
#define WMI_RSCSW_CHECKPOINT_HISTORY_CNT 16u
#define WMI_RSCSW_COVERAGE_HISTORY_CNT 16u
#define WMI_RSCSW_REMEDIATION_HISTORY_CNT 16u
#define WMI_RSCSW_STABILIZATION_HISTORY_CNT 16u
#define WMI_RSCSW_CONFORMANCE_HISTORY_CNT 16u
#define WMI_RSCSW_ORCHESTRATION_STAGE_CNT 24u
#define WMI_RSCSW_ORCHESTRATION_HISTORY_CNT 16u
#define WMI_RSCSW_POLICY_RES_CNT 96u
#define WMI_RSCSW_POLICY_HISTORY_CNT 32u
#define WMI_RSCSW_FABRIC_LANE_CNT 24u
#define WMI_RSCSW_FABRIC_HISTORY_CNT 32u
#define WMI_RSCSW_DOMAIN_CNT 8u
#define WMI_RSCSW_DOMAIN_HISTORY_CNT 32u
#define WMI_RSCSW_INTERCONNECT_ROUTE_CNT 32u
#define WMI_RSCSW_INTERCONNECT_HISTORY_CNT 32u

typedef struct wmi_rscsw_node {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG present;
    ULONG state;
    ULONG clock_khz;
    ULONG flags;
    ULONG last_ioctl_code;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONG req_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG vote_cnt;
    ULONG command_cnt;
    ULONG pending_cnt;
    LONG last_status;
    ULONGLONG last_tick100ns;
    ULONGLONG activity_accumulator;
} wmi_rscsw_node_t;

typedef struct wmi_rscsw_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG subsystem_id;
    ULONG event_code;
    LONG status;
    ULONG ioctl_code;
    ULONG tag;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
    ULONG flags;
} wmi_rscsw_event_t;

typedef struct wmi_rscsw_catalog {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG cap_mask;
    ULONG default_state;
    ULONG default_clock_khz;
    ULONG default_flags;
    ULONG res_tag;
    ULONG suggested_ioctl;
} wmi_rscsw_catalog_t;

typedef struct wmi_rscsw_scenario {
    ULONG scenario_id;
    ULONG iterations;
    ULONG flags;
    LONG status;
    ULONG applied_nodes;
    ULONG failed_nodes;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_scenario_t;

typedef struct wmi_rscsw_diag_entry {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG driver_map_id;
    ULONG ppp_res_type;
    ULONG glink_event_seed;
    ULONG icb_error_code;
    ULONG health_score;
    ULONG confidence_score;
    ULONG activity_score;
    ULONG cap_mask;
    LONG last_scenario_status;
    LONG last_node_status;
    ULONG reserved0;
    ULONG reserved1;
} wmi_rscsw_diag_entry_t;

typedef struct wmi_rscsw_diag_history {
    ULONG iterations;
    ULONG flags;
    LONG status;
    ULONG scenario_runs;
    ULONG scenario_failures;
    ULONG last_scenario_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONGLONG tick100ns;
} wmi_rscsw_diag_history_t;

typedef struct wmi_rscsw_profile {
    ULONG profile_id;
    CHAR name[24];
    ULONG subsystem_mask;
    ULONG scenario_flags;
    ULONG matrix_flags;
    ULONG default_iterations;
    ULONG priority;
    ULONG reserved;
} wmi_rscsw_profile_t;

typedef struct wmi_rscsw_profile_history {
    ULONG profile_id;
    ULONG iterations;
    ULONG flags;
    LONG status;
    ULONG scenario_runs;
    ULONG scenario_failures;
    ULONG matrix_runs;
    ULONG matrix_failures;
    ULONG last_scenario_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONGLONG tick100ns;
} wmi_rscsw_profile_history_t;

typedef struct wmi_rscsw_graph_link {
    ULONG from_subsystem_id;
    ULONG to_subsystem_id;
    ULONG weight;
    ULONG link_type;
    ULONG flags;
} wmi_rscsw_graph_link_t;

typedef struct wmi_rscsw_action_sequence_history {
    ULONG requested_steps;
    ULONG executed_steps;
    ULONG failed_steps;
    LONG status;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_action_sequence_history_t;

typedef struct wmi_rscsw_action_sequence_preview_history {
    ULONG requested_steps;
    ULONG flags;
    ULONG expanded_steps;
    ULONG invalid_steps;
    ULONG duplicate_steps;
    ULONG dependency_edges;
    ULONG reordered_steps;
    ULONG active_mask_before;
    ULONG active_mask_after;
    LONG status;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_action_sequence_preview_history_t;

typedef struct wmi_rscsw_checkpoint {
    ULONG slot_id;
    ULONG valid;
    CHAR name[24];
    ULONG capture_cnt;
    ULONG apply_cnt;
    LONG last_status;
    ULONG node_cnt;
    ULONG active_mask;
    ULONGLONG last_tick100ns;
} wmi_rscsw_checkpoint_t;

typedef struct wmi_rscsw_checkpoint_history {
    ULONG operation;
    ULONG slot_id;
    ULONG flags;
    LONG status;
    ULONG node_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_checkpoint_history_t;

typedef struct wmi_rscsw_coverage_row {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG present;
    ULONG active;
    ULONG req_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG success_permille;
    ULONG dependency_in;
    ULONG dependency_out;
    ULONG health_score;
    ULONG activity_score;
    ULONG flags;
    LONG last_status;
    ULONGLONG last_tick100ns;
} wmi_rscsw_coverage_row_t;

typedef struct wmi_rscsw_coverage_history {
    ULONG flags;
    ULONG node_cnt;
    LONG status;
    ULONG health_score;
    ULONG activity_score;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG below_threshold_cnt;
    ULONG threshold_permille;
    ULONG active_mask_before;
    ULONG active_mask_after;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_coverage_history_t;

typedef struct wmi_rscsw_remediation_action {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG priority;
    ULONG reason_flags;
    ULONG current_state;
    ULONG recommended_state;
    ULONG current_clock_khz;
    ULONG recommended_clock_khz;
    ULONG current_flags;
    ULONG recommended_flags;
    ULONG health_score;
    ULONG activity_score;
    ULONG success_permille;
    ULONG dependency_in;
    ULONG dependency_out;
    LONG last_status;
} wmi_rscsw_remediation_action_t;

typedef struct wmi_rscsw_remediation_history {
    ULONG flags;
    LONG status;
    ULONG recommended_cnt;
    ULONG executed_cnt;
    ULONG failed_cnt;
    ULONG coverage_threshold_permille;
    ULONG coverage_health_score;
    ULONG coverage_activity_score;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG worst_subsystem_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_remediation_history_t;

typedef struct wmi_rscsw_stabilization_lane {
    ULONG lane_id;
    CHAR name[16];
    ULONG subsystem_mask;
    ULONG node_cnt;
    ULONG degraded_cnt;
    ULONG health_score;
    ULONG activity_score;
    ULONG warning_cnt;
    ULONG error_cnt;
    LONG last_status;
    ULONGLONG last_tick100ns;
} wmi_rscsw_stabilization_lane_t;

typedef struct wmi_rscsw_stabilization_history {
    ULONG flags;
    LONG status;
    ULONG lane_cnt;
    ULONG degraded_lane_cnt;
    LONG remediation_status;
    ULONG coverage_health_score;
    ULONG coverage_activity_score;
    ULONG coverage_warning_cnt;
    ULONG coverage_error_cnt;
    ULONG profile_runs;
    ULONG profile_failures;
    ULONG last_lane_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_stabilization_history_t;

typedef struct wmi_rscsw_conformance_row {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG driver_map_id;
    ULONG ppp_res_type;
    ULONG glink_event_seed;
    ULONG cap_mask;
    ULONG res_tag;
    ULONG suggested_ioctl;
    ULONG dependency_in;
    ULONG dependency_out;
    ULONG active;
    ULONG success_permille;
    ULONG activity_score;
    ULONG conformance_score_permille;
    ULONG reason_flags;
    LONG last_status;
    ULONGLONG last_tick100ns;
} wmi_rscsw_conformance_row_t;

typedef struct wmi_rscsw_conformance_history {
    ULONG flags;
    LONG status;
    ULONG node_cnt;
    ULONG passing_cnt;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG overall_score_permille;
    ULONG threshold_permille;
    ULONG worst_subsystem_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_conformance_history_t;

typedef struct wmi_rscsw_orchestration_stage {
    ULONG stage_id;
    CHAR stage_name[16];
    ULONG enabled;
    LONG status;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG score_permille;
    ULONG detail0;
    ULONG detail1;
    ULONG detail2;
    ULONG detail3;
    ULONGLONG tick100ns;
} wmi_rscsw_orchestration_stage_t;

typedef struct wmi_rscsw_orchestration_history {
    ULONG flags;
    LONG status;
    ULONG stage_cnt;
    ULONG successful_stages;
    ULONG warning_stages;
    ULONG failed_stages;
    ULONG overall_score_permille;
    ULONG last_stage_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_orchestration_history_t;

typedef struct wmi_rscsw_policy_res {
    ULONG res_id;
    ULONG parent_res_id;
    CHAR res_name[48];
    CHAR parent_res_name[48];
    ULONG subsystem_id;
    CHAR subsystem_name[16];
    ULONG parent_subsystem_id;
    CHAR parent_subsystem_name[16];
    ULONG res_type;
    ULONG active_vote_cnt;
    ULONG set_mask;
    ULONG driver_map_id;
    ULONG flags;
    LONG last_status;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG last_tick100ns;
} wmi_rscsw_policy_res_t;

typedef struct wmi_rscsw_policy_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG flags;
    LONG status;
    ULONG res_id;
    ULONG parent_res_id;
    CHAR res_name[48];
    CHAR parent_res_name[48];
    ULONG subsystem_id;
    CHAR subsystem_name[16];
    ULONG parent_subsystem_id;
    CHAR parent_subsystem_name[16];
    ULONG res_type;
    ULONG active_vote_cnt;
    ULONG set_mask;
    ULONG driver_map_id;
    ULONG tag;
    ULONG value0;
    ULONG value1;
    ULONG value2;
    ULONG value3;
    ULONG ioctl_code;
} wmi_rscsw_policy_event_t;

typedef struct wmi_rscsw_fabric_lane {
    ULONG subsystem_id;
    CHAR name[16];
    ULONG present;
    ULONG active;
    ULONG state;
    ULONG clock_khz;
    ULONG driver_map_id;
    ULONG ppp_res_type;
    ULONG policy_res_cnt;
    ULONG policy_active_vote_cnt;
    ULONG dependency_in;
    ULONG dependency_out;
    ULONG req_cnt;
    ULONG success_cnt;
    ULONG failure_cnt;
    ULONG success_permille;
    ULONG activity_score;
    ULONG health_score;
    LONG thermal_deci_c;
    ULONG flags;
    LONG last_status;
    ULONGLONG last_tick100ns;
} wmi_rscsw_fabric_lane_t;

typedef struct wmi_rscsw_fabric_history {
    ULONG flags;
    LONG status;
    ULONG lane_cnt;
    ULONG active_lane_cnt;
    ULONG policy_active_lane_cnt;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG overall_health_score;
    ULONG overall_activity_score;
    LONG thermal_max_deci_c;
    LONG thermal_min_deci_c;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_fabric_history_t;

typedef struct wmi_rscsw_domain {
    ULONG domain_id;
    CHAR name[16];
    ULONG subsystem_mask;
    ULONG lane_cnt;
    ULONG active_lane_cnt;
    ULONG policy_active_lane_cnt;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG avg_health_score;
    ULONG avg_activity_score;
    LONG thermal_max_deci_c;
    LONG thermal_min_deci_c;
    ULONG last_subsystem_id;
    LONG last_status;
    ULONG flags;
    ULONGLONG last_tick100ns;
} wmi_rscsw_domain_t;

typedef struct wmi_rscsw_domain_history {
    ULONG flags;
    LONG status;
    ULONG domain_cnt;
    ULONG healthy_domain_cnt;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG overall_health_score;
    ULONG overall_activity_score;
    ULONG last_domain_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_domain_history_t;

typedef struct wmi_rscsw_interconnect_route {
    ULONG route_id;
    CHAR name[24];
    ULONG subsystem_id;
    CHAR subsystem_name[16];
    ULONG master_id;
    CHAR master_name[48];
    ULONG slave_id;
    CHAR slave_name[48];
    ULONG active;
    ULONG policy_linked;
    ULONG required_bandwidth_mb;
    ULONG granted_bandwidth_mb;
    ULONG utilization_permille;
    ULONG vote_cnt;
    ULONG dependency_in;
    ULONG dependency_out;
    LONG thermal_deci_c;
    ULONG flags;
    LONG last_status;
    ULONGLONG last_tick100ns;
} wmi_rscsw_interconnect_route_t;

typedef struct wmi_rscsw_interconnect_history {
    ULONG flags;
    LONG status;
    ULONG route_cnt;
    ULONG active_route_cnt;
    ULONG policy_linked_route_cnt;
    ULONG warning_cnt;
    ULONG error_cnt;
    ULONG avg_utilization_permille;
    ULONG peak_utilization_permille;
    ULONG total_required_bandwidth_mb;
    ULONG total_granted_bandwidth_mb;
    ULONG last_route_id;
    ULONG last_subsystem_id;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONGLONG tick100ns;
} wmi_rscsw_interconnect_history_t;

typedef struct wmi_rscsw {
    ULONG version;
    ULONG node_cnt;
    ULONG active_mask;
    ULONG reserved;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_subsystem_id;
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
    ULONG last_ioctl_code;
    ULONG last_tag;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_value2;
    ULONG last_value3;
    ULONG last_flags;
    ULONG batch_apply_cnt;
    ULONG batch_command_cnt;
    ULONG batch_error_cnt;
    ULONG last_batch_cnt;
    LONG last_batch_status;
    ULONGLONG last_batch_tick100ns;
    ULONG scenario_run_cnt;
    ULONG scenario_failure_cnt;
    ULONG last_scenario_id;
    LONG last_scenario_status;
    ULONG last_scenario_iterations;
    ULONGLONG last_scenario_tick100ns;
    ULONG node_entry_cnt;
    wmi_rscsw_node_t nodes[RSCSW_NODE_CAPACITY];
    ULONG history_entry_cnt;
    wmi_rscsw_event_t history[WMI_RSCSW_HISTORY_CNT];
    ULONG catalog_entry_cnt;
    wmi_rscsw_catalog_t catalog[WMI_RSCSW_CATALOG_CNT];
    ULONG scenario_history_entry_cnt;
    wmi_rscsw_scenario_t scenario_history[WMI_RSCSW_SCENARIO_HISTORY_CNT];
    ULONG matrix_run_cnt;
    ULONG matrix_failure_cnt;
    LONG last_matrix_status;
    ULONGLONG last_matrix_tick100ns;
    ULONG last_matrix_flags;
    ULONG last_matrix_iterations;
    ULONG last_matrix_failures;
    ULONG diag_entry_cnt;
    wmi_rscsw_diag_entry_t diag[WMI_RSCSW_DIAG_CNT];
    ULONG diag_history_entry_cnt;
    wmi_rscsw_diag_history_t diag_history[WMI_RSCSW_DIAG_HISTORY_CNT];
    ULONG profile_cnt;
    ULONG profile_run_cnt;
    ULONG profile_failure_cnt;
    ULONG last_profile_id;
    LONG last_profile_status;
    ULONG last_profile_iterations;
    ULONG last_profile_flags;
    ULONGLONG last_profile_tick100ns;
    ULONG profile_entry_cnt;
    wmi_rscsw_profile_t profiles[WMI_RSCSW_PROFILE_CNT];
    ULONG profile_history_entry_cnt;
    wmi_rscsw_profile_history_t profile_history[WMI_RSCSW_PROFILE_HISTORY_CNT];
    ULONG graph_version;
    ULONG graph_node_cnt;
    ULONG graph_link_cnt;
    ULONG graph_active_mask;
    LONG last_action_sequence_status;
    ULONG last_action_sequence_step_cnt;
    ULONG last_action_sequence_failure_cnt;
    ULONGLONG last_action_sequence_tick100ns;
    ULONG graph_entry_cnt;
    wmi_rscsw_graph_link_t graph[WMI_RSCSW_GRAPH_LINK_CNT];
    ULONG action_sequence_history_entry_cnt;
    wmi_rscsw_action_sequence_history_t
        action_sequence_history[WMI_RSCSW_ACTION_SEQUENCE_HISTORY_CNT];
    ULONG action_sequence_preview_run_cnt;
    ULONG action_sequence_preview_failure_cnt;
    LONG action_sequence_preview_last_status;
    ULONG action_sequence_preview_last_requested_steps;
    ULONG action_sequence_preview_last_expanded_steps;
    ULONG action_sequence_preview_last_invalid_steps;
    ULONG action_sequence_preview_last_duplicate_steps;
    ULONG action_sequence_preview_last_dependency_edges;
    ULONG action_sequence_preview_last_reordered_steps;
    ULONG action_sequence_preview_last_active_mask_before;
    ULONG action_sequence_preview_last_active_mask_after;
    ULONG action_sequence_preview_last_flags;
    ULONGLONG action_sequence_preview_last_tick100ns;
    ULONG action_sequence_preview_history_entry_cnt;
    ULONG action_sequence_preview_history_dropped;
    wmi_rscsw_action_sequence_preview_history_t
        action_sequence_preview_history[WMI_RSCSW_ACTION_SEQUENCE_PREVIEW_HISTORY_CNT];
    ULONG checkpoint_slot_cnt;
    ULONG checkpoint_valid_cnt;
    ULONG checkpoint_capture_cnt;
    ULONG checkpoint_apply_cnt;
    LONG checkpoint_last_status;
    ULONG checkpoint_last_slot_id;
    ULONGLONG checkpoint_last_tick100ns;
    ULONG checkpoint_entry_cnt;
    wmi_rscsw_checkpoint_t checkpoints[WMI_RSCSW_CHECKPOINT_CNT];
    ULONG checkpoint_history_entry_cnt;
    ULONG checkpoint_history_dropped;
    wmi_rscsw_checkpoint_history_t checkpoint_history[WMI_RSCSW_CHECKPOINT_HISTORY_CNT];
    ULONG coverage_run_cnt;
    ULONG coverage_failure_cnt;
    LONG coverage_last_status;
    ULONG coverage_last_health_score;
    ULONG coverage_last_activity_score;
    ULONG coverage_last_warning_cnt;
    ULONG coverage_last_error_cnt;
    ULONG coverage_last_threshold_permille;
    ULONG coverage_last_flags;
    ULONGLONG coverage_last_tick100ns;
    ULONG coverage_entry_cnt;
    wmi_rscsw_coverage_row_t coverage[RSCSW_NODE_CAPACITY];
    ULONG coverage_history_entry_cnt;
    ULONG coverage_history_dropped;
    wmi_rscsw_coverage_history_t coverage_history[WMI_RSCSW_COVERAGE_HISTORY_CNT];
    ULONG remediation_run_cnt;
    ULONG remediation_failure_cnt;
    LONG remediation_last_status;
    ULONG remediation_last_recommended_cnt;
    ULONG remediation_last_executed_cnt;
    ULONG remediation_last_failed_cnt;
    ULONG remediation_last_coverage_threshold_permille;
    ULONG remediation_last_coverage_health_score;
    ULONG remediation_last_coverage_activity_score;
    ULONG remediation_last_warning_cnt;
    ULONG remediation_last_error_cnt;
    ULONG remediation_last_worst_subsystem_id;
    ULONG remediation_last_flags;
    ULONGLONG remediation_last_tick100ns;
    ULONG remediation_action_entry_cnt;
    wmi_rscsw_remediation_action_t remediation_actions[RSCSW_REMEDIATION_ACTION_CAPACITY];
    ULONG remediation_history_entry_cnt;
    ULONG remediation_history_dropped;
    wmi_rscsw_remediation_history_t remediation_history[WMI_RSCSW_REMEDIATION_HISTORY_CNT];
    ULONG stabilization_run_cnt;
    ULONG stabilization_failure_cnt;
    LONG stabilization_last_status;
    ULONG stabilization_last_flags;
    ULONG stabilization_last_lane_cnt;
    ULONG stabilization_last_degraded_lane_cnt;
    LONG stabilization_last_remediation_status;
    ULONG stabilization_last_coverage_health_score;
    ULONG stabilization_last_coverage_activity_score;
    ULONG stabilization_last_coverage_warning_cnt;
    ULONG stabilization_last_coverage_error_cnt;
    ULONG stabilization_last_profile_runs;
    ULONG stabilization_last_profile_failures;
    ULONGLONG stabilization_last_tick100ns;
    ULONG stabilization_lane_entry_cnt;
    wmi_rscsw_stabilization_lane_t stabilization_lanes[RSCSW_STABILIZATION_LANE_CAPACITY];
    ULONG stabilization_history_entry_cnt;
    ULONG stabilization_history_dropped;
    wmi_rscsw_stabilization_history_t stabilization_history[WMI_RSCSW_STABILIZATION_HISTORY_CNT];
    ULONG conformance_run_cnt;
    ULONG conformance_failure_cnt;
    LONG conformance_last_status;
    ULONG conformance_last_flags;
    ULONG conformance_last_overall_score_permille;
    ULONG conformance_last_threshold_permille;
    ULONG conformance_last_passing_cnt;
    ULONG conformance_last_warning_cnt;
    ULONG conformance_last_error_cnt;
    ULONG conformance_last_worst_subsystem_id;
    ULONGLONG conformance_last_tick100ns;
    ULONG conformance_row_entry_cnt;
    wmi_rscsw_conformance_row_t conformance_rows[RSCSW_NODE_CAPACITY];
    ULONG conformance_history_entry_cnt;
    ULONG conformance_history_dropped;
    wmi_rscsw_conformance_history_t conformance_history[WMI_RSCSW_CONFORMANCE_HISTORY_CNT];
    ULONG orchestration_run_cnt;
    ULONG orchestration_failure_cnt;
    LONG orchestration_last_status;
    ULONG orchestration_last_flags;
    ULONG orchestration_last_overall_score_permille;
    ULONG orchestration_last_stage_cnt;
    ULONG orchestration_last_successful_stages;
    ULONG orchestration_last_warning_stages;
    ULONG orchestration_last_failed_stages;
    ULONG orchestration_last_stage_id;
    ULONGLONG orchestration_last_tick100ns;
    ULONG orchestration_stage_entry_cnt;
    wmi_rscsw_orchestration_stage_t orchestration_stages[WMI_RSCSW_ORCHESTRATION_STAGE_CNT];
    ULONG orchestration_history_entry_cnt;
    ULONG orchestration_history_dropped;
    wmi_rscsw_orchestration_history_t orchestration_history[WMI_RSCSW_ORCHESTRATION_HISTORY_CNT];
    ULONG policy_res_cnt;
    ULONG policy_active_res_cnt;
    ULONGLONG policy_event_sequence;
    ULONG policy_vote_cnt;
    ULONG policy_remove_cnt;
    ULONG policy_propagation_cnt;
    ULONG policy_last_res_id;
    ULONG policy_last_subsystem_id;
    ULONG policy_last_parent_subsystem_id;
    ULONG policy_last_ioctl_code;
    LONG policy_last_status;
    ULONG policy_last_flags;
    ULONGLONG policy_last_tick100ns;
    ULONG policy_res_entry_cnt;
    wmi_rscsw_policy_res_t policy_resources[WMI_RSCSW_POLICY_RES_CNT];
    ULONG policy_history_entry_cnt;
    ULONG policy_history_dropped;
    wmi_rscsw_policy_event_t policy_history[WMI_RSCSW_POLICY_HISTORY_CNT];
    ULONG fabric_run_cnt;
    ULONG fabric_failure_cnt;
    LONG fabric_last_status;
    ULONG fabric_last_flags;
    ULONG fabric_last_lane_cnt;
    ULONG fabric_last_active_lane_cnt;
    ULONG fabric_last_policy_active_lane_cnt;
    ULONG fabric_last_warning_cnt;
    ULONG fabric_last_error_cnt;
    ULONG fabric_last_overall_health_score;
    ULONG fabric_last_overall_activity_score;
    LONG fabric_last_thermal_max_deci_c;
    LONG fabric_last_thermal_min_deci_c;
    ULONGLONG fabric_last_tick100ns;
    ULONG fabric_lane_entry_cnt;
    wmi_rscsw_fabric_lane_t fabric_lanes[WMI_RSCSW_FABRIC_LANE_CNT];
    ULONG fabric_history_entry_cnt;
    ULONG fabric_history_dropped;
    wmi_rscsw_fabric_history_t fabric_history[WMI_RSCSW_FABRIC_HISTORY_CNT];
    ULONG domain_run_cnt;
    ULONG domain_failure_cnt;
    LONG domain_last_status;
    ULONG domain_last_flags;
    ULONG domain_last_domain_cnt;
    ULONG domain_last_healthy_domain_cnt;
    ULONG domain_last_warning_cnt;
    ULONG domain_last_error_cnt;
    ULONG domain_last_overall_health_score;
    ULONG domain_last_overall_activity_score;
    ULONGLONG domain_last_tick100ns;
    ULONG domain_entry_cnt;
    wmi_rscsw_domain_t domains[WMI_RSCSW_DOMAIN_CNT];
    ULONG domain_history_entry_cnt;
    ULONG domain_history_dropped;
    wmi_rscsw_domain_history_t domain_history[WMI_RSCSW_DOMAIN_HISTORY_CNT];
    ULONG interconnect_run_cnt;
    ULONG interconnect_failure_cnt;
    LONG interconnect_last_status;
    ULONG interconnect_last_flags;
    ULONG interconnect_last_route_cnt;
    ULONG interconnect_last_active_route_cnt;
    ULONG interconnect_last_policy_linked_route_cnt;
    ULONG interconnect_last_warning_cnt;
    ULONG interconnect_last_error_cnt;
    ULONG interconnect_last_avg_utilization_permille;
    ULONG interconnect_last_peak_utilization_permille;
    ULONG interconnect_last_total_required_bandwidth_mb;
    ULONG interconnect_last_total_granted_bandwidth_mb;
    ULONGLONG interconnect_last_tick100ns;
    ULONG interconnect_route_entry_cnt;
    wmi_rscsw_interconnect_route_t interconnect_routes[WMI_RSCSW_INTERCONNECT_ROUTE_CNT];
    ULONG interconnect_history_entry_cnt;
    ULONG interconnect_history_dropped;
    wmi_rscsw_interconnect_history_t interconnect_history[WMI_RSCSW_INTERCONNECT_HISTORY_CNT];
} wmi_rscsw_t;

typedef struct wmi_stats {
    ULONG ioctl_cnt;
    ULONG po_fx_power_required;
    ULONG po_fx_power_not_required;
    ULONG po_fx_comp_active;
    ULONG po_fx_comp_idle;
    ULONG po_fx_idle_state;
    ULONG d0_entry;
    ULONG d0_exit;
    ULONG dev_attached;
    ULONGLONG event_cnt;
    ULONGLONG error_cnt;
    ULONGLONG ioctl_error_cnt;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_arg0;
    ULONG last_event_arg1;
    ULONG last_event_arg2;
    ULONG last_event_arg3;
    ULONGLONG last_event_tick100ns;
    ULONG last_ioctl_code;
    LONG last_ioctl_status;
    ULONG last_ioctl_in_len;
    ULONG last_ioctl_out_len;
    ULONG history_entry_cnt;
    wmi_stats_event_t history[WMI_STATS_HISTORY_CNT];
} wmi_stats_t;

typedef struct wmi_chipinfo {
    ULONG chip_id;
    ULONG chip_family;
    ULONG chip_version;
    ULONG chip_sku;
} wmi_chipinfo_t;

typedef struct wmi_tsens {
    ULONG sensor_cnt;
    ULONG temp_deci_c;
    ULONG temp_deci_k;
    ULONGLONG read_cnt;
    ULONGLONG dsm_refresh_cnt;
    ULONGLONG threshold_hit_cnt;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_sensor_id;
    LONG last_temp_deci_c;
    LONG last_low_deci_c;
    LONG last_high_deci_c;
    ULONGLONG last_event_tick100ns;
    ULONG sensor_entry_cnt;

    struct {
        LONG last_temp_deci_c;
        LONG min_temp_deci_c;
        LONG max_temp_deci_c;
        LONG low_threshold_deci_c;
        LONG high_threshold_deci_c;
        ULONG low_hit_cnt;
        ULONG high_hit_cnt;
        ULONG flags;
    } sensors[16];

    ULONG history_entry_cnt;

    struct {
        ULONGLONG tick100ns;
        ULONGLONG sequence;
        ULONG event_code;
        LONG status;
        ULONG sensor_id;
        LONG temp_deci_c;
        LONG low_deci_c;
        LONG high_deci_c;
        ULONG aux0;
        ULONG aux1;
    } history[16];
} wmi_tsens_t;

typedef struct wmi_mmrm_domain {
    ULONG freq_khz;
    ULONG power_milli_watts;
    ULONG flags;
    ULONG dyn_permille;
    ULONG lkg_permille;
    LONG temp_bias_deci_c;
} wmi_mmrm_domain_t;

#define WMI_DS_HISTORY_CNT 16u

typedef struct wmi_deepstandby_event {
    ULONG event_code;
    ULONG status;
    ULONG prev_ms_on_dc_pwr;
    ULONG curr_ms_on_dc_pwr;
    ULONG prev_deep_standby_state;
    ULONG curr_deep_standby_state;
    ULONG aux0;
    ULONG aux1;
    ULONGLONG tick100ns;
} wmi_deepstandby_event_t;

#define WMI_MMRM_HISTORY_CNT 16u

typedef struct wmi_mmrm_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG reason;
    ULONG percent;
    ULONG updated;
    ULONG peak_milli_amps;
    LONG temp_deci_k;
    LONG low_temp_deci_k;
    LONG high_temp_deci_k;
    ULONG passive_limit;
    ULONG activation_reasons;
    ULONG feature_enabled;
    ULONG dsm_flags;
} wmi_mmrm_event_t;

typedef struct wmi_mmrm {
    ULONG feature_enabled;
    ULONG throttle_reason;
    ULONG throttle_percentage;
    ULONG updated;
    ULONG reserved_power_milli_watts;
    ULONG sampling_period_milli_seconds;
    ULONG max_throttle_percentage;
    ULONG mitigation_start_threshold_milli_amps;
    ULONG mitigation_stop_threshold_milli_amps;
    ULONG temperature_sensor_id;
    ULONG last_total_power_milli_watts;
    ULONG last_peak_milli_amps;
    ULONG video_encoding_state;
    ULONG video_decoding_state;
    ULONG policy_wait_active;
    ULONG sampling_timer_running;
    ULONG dsm_flags;
    ULONG dsm_refresh_cnt;
    ULONG last_dsm_status;
    ULONG dsm_poll_interval_milli_seconds;
    ULONG last_dsm_policy_item_cnt;
    ULONG last_dsm_clock_item_cnt;
    ULONG peak_voltage_milli_volts;
    ULONG global_dyn_permille;
    ULONG global_lkg_permille;
    ULONG last_camera_power_milli_watts;
    ULONG last_video_power_milli_watts;
    ULONG last_display_power_milli_watts;
    ULONG domain_cnt;
    LONG current_temp_deci_k;
    LONG threshold_low_deci_k;
    LONG threshold_high_deci_k;
    LONG camera_temp_deci_c;
    LONG video_temp_deci_c;
    LONG display_temp_deci_c;
    wmi_mmrm_domain_t domains[MMRM_MAX_CLOCK_DOMAINS];
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_reason;
    ULONG last_event_percent;
    ULONG last_event_updated;
    ULONG last_event_peak_milli_amps;
    LONG last_event_temp_deci_k;
    LONG last_event_low_temp_deci_k;
    LONG last_event_high_temp_deci_k;
    ULONGLONG last_event_tick100ns;
    ULONG history_entry_cnt;
    wmi_mmrm_event_t history[WMI_MMRM_HISTORY_CNT];
} wmi_mmrm_t;

typedef struct wmi_deepstandby {
    ULONG feature_enabled;
    ULONG modem_enabled;
    ULONG wlan_enabled;
    ULONG power_src;
    ULONG screen_state;
    ULONG user_state;
    ULONG ms_on_dc_pwr;
    ULONG deep_standby_state;
    ULONG timer_expired;
    ULONG batt_drain_exceeded_limit;
    ULONG batt_drain_since_tmr_start;
    ULONG current_batt_pct;
    ULONG timeout_seconds;
    ULONG max_batt_drain_pct;
    ULONG modem_req;
    ULONG evaluate_cnt;
    ULONG callback_cnt;
    ULONG state_transition_cnt;
    ULONG enter_cnt;
    ULONG exit_cnt;
    ULONG wlan_ioctl_errors;
    ULONG modem_ioctl_errors;
    ULONG last_wlan_ioctl_status;
    ULONG last_modem_ioctl_status;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    ULONG last_event_status;
    ULONG history_entry_cnt;
    wmi_deepstandby_event_t history[WMI_DS_HISTORY_CNT];
} wmi_deepstandby_t;

#define WMI_THERMALREQ_HISTORY_CNT 16u

typedef struct wmi_thermalreq_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG req_id;
    ULONG sensor_id;
    LONG temp_deci_k;
    LONG low_temp_deci_k;
    LONG high_temp_deci_k;
    ULONG active_cnt;
    ULONG port_id;
} wmi_thermalreq_event_t;

typedef struct wmi_thermalreq {
    ULONG poll_ms;
    ULONG active_cnt;
    ULONG last_req_id;
    ULONGLONG total_queued;
    ULONGLONG total_completed;
    ULONGLONG total_cancelled;
    ULONGLONG total_timeout;
    ULONGLONG total_low_threshold_hits;
    ULONGLONG total_high_threshold_hits;
    ULONGLONG total_busy_rejected;
    ULONGLONG total_no_slot_rejected;
    ULONGLONG total_mark_cancelable_failures;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_req_id;
    ULONG last_event_sensor_id;
    LONG last_event_temp_deci_k;
    LONG last_event_low_temp_deci_k;
    LONG last_event_high_temp_deci_k;
    ULONGLONG last_event_tick100ns;
    ULONG history_entry_cnt;
    wmi_thermalreq_event_t history[WMI_THERMALREQ_HISTORY_CNT];
} wmi_thermalreq_t;

#define WMI_BCL_THRESH_CNT 16u
#define WMI_BCL_HISTORY_CNT 16u

typedef struct wmi_bcl_threshold {
    ULONG res_id;
    ULONG field;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG update_cnt;
    LONG last_status;
    ULONGLONG last_tick100ns;
    ULONG flag;
} wmi_bcl_threshold_t;

typedef struct wmi_bcl_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG notify_type;
    ULONG parking_state;
    ULONG desired_parked_cpus;
    ULONG res_id;
    ULONG field;
    ULONG sub_type;
    ULONG value0;
    ULONG value1;
    ULONG flag;
    ULONG aux0;
} wmi_bcl_event_t;

typedef struct wmi_bcl {
    ULONG version;
    ULONG test_enabled;
    ULONG current_notify_type;
    ULONG parking_state;
    ULONG desired_parked_cpus;
    ULONG current_parked_cpus;
    ULONG throttle_permille;
    ULONG is_low_imax_state;
    ULONG mitigation_state;
    ULONG threshold_cnt;
    ULONGLONG submit_cnt;
    ULONGLONG submit_accepted;
    ULONGLONG submit_rejected;
    ULONGLONG work_item_run_cnt;
    ULONGLONG threshold_op_cnt;
    ULONGLONG threshold_downstack_success;
    ULONGLONG threshold_downstack_failure;
    ULONGLONG threshold_cache_hits;
    ULONGLONG threshold_cache_misses;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONGLONG last_event_tick100ns;
    ULONG last_res_id;
    ULONG last_field;
    ULONG last_sub_type;
    ULONG last_value0;
    ULONG last_value1;
    ULONG last_flag;
    ULONG last_aux0;
    ULONG threshold_entry_cnt;
    wmi_bcl_threshold_t thresholds[WMI_BCL_THRESH_CNT];
    ULONG history_entry_cnt;
    wmi_bcl_event_t history[WMI_BCL_HISTORY_CNT];
} wmi_bcl_t;

#define WMI_INTERRUPT_PORT_CNT 16u
#define WMI_INTERRUPT_HISTORY_CNT 16u
#define WMI_INTERRUPT_WAKE_TOP_CNT 16u

typedef struct wmi_interrupt_port {
    ULONG port_id;
    ULONG isr_cnt;
    ULONG dpc_cnt;
    ULONG enable_cnt;
    ULONG disable_cnt;
    ULONG dpc_queued_cnt;
    ULONG last_message_id;
    ULONGLONG last_isr_tick100ns;
    ULONGLONG last_dpc_tick100ns;
} wmi_interrupt_port_t;

typedef struct wmi_interrupt_event {
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
} wmi_interrupt_event_t;

typedef struct wmi_interrupt_wake_counter {
    ULONG offset;
    ULONG interrupt_id;
    ULONG hit_cnt;
} wmi_interrupt_wake_counter_t;

typedef struct wmi_interrupt {
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
    ULONG port_entry_cnt;
    wmi_interrupt_port_t ports[WMI_INTERRUPT_PORT_CNT];
    ULONG wake_top_entry_cnt;
    wmi_interrupt_wake_counter_t wake_top[WMI_INTERRUPT_WAKE_TOP_CNT];
    ULONG history_entry_cnt;
    wmi_interrupt_event_t history[WMI_INTERRUPT_HISTORY_CNT];
} wmi_interrupt_t;

#define WMI_SOCSUBSYS_HISTORY_CNT 16u

typedef struct wmi_socsubsys_entry {
    ULONG metadata_cnt;
    ULONG flags;
    ULONG wakeup_cnt;
    ULONGLONG blocked_duration100ns;
    ULONGLONG sleep_duration100ns;
    ULONGLONG last_entered_at100ns;
    ULONGLONG last_exited_at100ns;
    ULONGLONG island_entry_cnt;
    ULONGLONG island_entry_time100ns;
    ULONGLONG island_exit_time100ns;
    ULONGLONG island_accum_duration100ns;
} wmi_socsubsys_entry_t;

typedef struct wmi_socsubsys_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG subsystem_idx;
    ULONG reason;
    ULONG in_drips;
    ULONG wakeup_cnt;
    ULONGLONG blocked_duration100ns;
    ULONGLONG sleep_duration100ns;
    ULONGLONG aux100ns;
} wmi_socsubsys_event_t;

typedef struct wmi_socsubsys {
    ULONG version;
    ULONG drips_state_id;
    ULONG enabled_mask;
    ULONG subsystem_cnt;
    ULONG in_drips;
    ULONG drips_blocked;
    ULONG drips_blocked_reason;
    ULONG last_blocked_reason;
    ULONGLONG current_blocked_duration100ns;
    ULONGLONG blocked_boot100ns;
    ULONGLONG blocked_usb100ns;
    ULONGLONG blocked_dbg100ns;
    ULONGLONG blocked_other100ns;
    ULONGLONG aoss_sleep_count100ns;
    ULONGLONG ddr_sleep_count100ns;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_subsystem_idx;
    ULONG last_event_reason;
    ULONGLONG last_event_tick100ns;
    ULONG entry_cnt;
    wmi_socsubsys_entry_t entries[SOC_SUBSYS_CNT];
    ULONG history_entry_cnt;
    wmi_socsubsys_event_t history[WMI_SOCSUBSYS_HISTORY_CNT];
} wmi_socsubsys_t;

#define WMI_DCVS_CPU_CNT 16u
#define WMI_DCVS_PERF_CNT 32u
#define WMI_DCVS_HISTORY_CNT 16u

typedef struct wmi_dcvs_cpu {
    ULONG cpu;
    ULONG domain_idx;
    ULONG current_perf_level;
    ULONG current_freq_hz;
    ULONG requested_perf_state_idx;
    ULONG min_perf;
    ULONG max_perf;
    ULONG desired_perf;
    ULONG epp;
    ULONGLONG enter_time100ns;
    ULONGLONG residency100ns;
    ULONGLONG transition_cnt;
    ULONG normalized_performance;
    ULONG reserved;
} wmi_dcvs_cpu_t;

typedef struct wmi_dcvs_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG cpu;
    ULONG min_perf_state;
    ULONG max_perf_state;
    ULONG desired_perf_state;
    ULONG applied_perf_level;
    ULONG applied_freq_hz;
    ULONG thermal_cap_percent;
    ULONG epp;
    ULONG aux0;
    ULONG aux1;
} wmi_dcvs_event_t;

typedef struct wmi_dcvs {
    ULONG version;
    ULONG initialized;
    ULONG cpu_cnt;
    ULONG domain_id;
    ULONG domain_members;
    ULONG coordination_type;
    ULONG worst_case_transition_latency;
    ULONG worst_case_transition_overhead;
    ULONG enable_perf_selection;
    ULONG enable_thermal_perf_selection;
    ULONG enable_boost;
    ULONG boost_freq_hz;
    ULONG max_freq_hz;
    ULONG min_freq_hz;
    ULONG nominal_freq_hz;
    ULONG lowest_nonlinear_freq_hz;
    ULONG thermal_hot_deci_c;
    ULONG thermal_critical_deci_c;
    ULONG thermal_cap_hot_percent;
    ULONG thermal_cap_critical_percent;
    ULONG current_thermal_cap_percent;
    ULONG perf_table_cnt;
    ULONG perf_table_hz[WMI_DCVS_PERF_CNT];
    ULONG cpu_entry_cnt;
    wmi_dcvs_cpu_t cpus[WMI_DCVS_CPU_CNT];
    ULONG policy_update_cnt;
    ULONGLONG last_policy_tick100ns;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_cpu;
    ULONG last_event_min_perf_state;
    ULONG last_event_max_perf_state;
    ULONG last_event_desired_perf_state;
    ULONG last_event_applied_perf_level;
    ULONG last_event_applied_freq_hz;
    ULONG last_event_thermal_cap_percent;
    ULONG last_event_epp;
    ULONG last_event_aux0;
    ULONG last_event_aux1;
    ULONGLONG last_event_tick100ns;
    ULONG history_entry_cnt;
    wmi_dcvs_event_t history[WMI_DCVS_HISTORY_CNT];
} wmi_dcvs_t;

#define WMI_PARKSEL_CPU_CNT PARKSEL_MAX_CPU_SNAPSHOT
#define WMI_PARKSEL_HISTORY_CNT 16u

typedef struct wmi_parksel_cpu {
    UCHAR current_park_state;
    UCHAR previous_park_state;
    UCHAR current_preference;
    UCHAR current_rank;
    ULONG transition_cnt;
    LONG temperature_deci_c;
} wmi_parksel_cpu_t;

typedef struct wmi_parksel_event {
    ULONGLONG tick100ns;
    ULONGLONG sequence;
    ULONG event_code;
    LONG status;
    ULONG cpu;
    ULONG cpu_cnt;
    ULONG additional_unparked;
    ULONG remaining_unparked;
    ULONG evaluation_type;
    ULONGLONG evaluation_time;
    ULONG cluster0_unparked;
    ULONG cluster1_unparked;
    ULONG aux0;
    ULONG aux1;
} wmi_parksel_event_t;

typedef struct wmi_parksel {
    ULONG version;
    ULONG capture_status;
    ULONG cpu_cnt;
    ULONG active_cpu_cnt;
    ULONG cpu_entry_cnt;
    ULONG last_additional_unparked;
    ULONG last_evaluation_type;
    ULONGLONG last_evaluation_time;
    ULONGLONG last_cluster0_evaluation_time;
    ULONGLONG last_cluster1_evaluation_time;
    ULONG timer_threshold0_sec;
    ULONG timer_threshold1_sec;
    ULONG core_rotation_stat0;
    ULONG core_rotation_stat1;
    ULONG toggle_state;
    ULONG cluster0_unparked;
    ULONG cluster1_unparked;
    ULONG pref_rail_id;
    ULONG timer_rail_id;
    ULONG num_unpark_rail_id;
    ULONG core_rotation_rail_id;
    ULONG current_parked_cnt;
    ULONG current_unparked_cnt;
    ULONG previous_parked_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_additional_unparked;
    ULONG last_event_remaining_unparked;
    ULONGLONG last_event_tick100ns;
    ULONG thermal_rank_enabled;
    LONG thermal_rank_hysteresis_deci_c;
    ULONG thermal_rank_update_interval_ms;
    ULONG thermal_sensor_base_idx;
    ULONG thermal_refresh_cnt;
    ULONG thermal_apply_cnt;
    LONG last_thermal_status;
    ULONG rank_entry_cnt;
    UCHAR rank_preference[WMI_PARKSEL_CPU_CNT];
    UCHAR previous_rank_preference[WMI_PARKSEL_CPU_CNT];
    LONG rank_cpu_temp_deci_c[WMI_PARKSEL_CPU_CNT];
    wmi_parksel_cpu_t cpus[WMI_PARKSEL_CPU_CNT];
    ULONG history_entry_cnt;
    wmi_parksel_event_t history[WMI_PARKSEL_HISTORY_CNT];
} wmi_parksel_t;

#define WMI_CRASH_SRC_CNT 8u
#define WMI_CRASH_HISTORY_CNT 16u

typedef struct wmi_crash_src {
    ULONG idx;
    ULONG in_use;
    ULONG guid_valid;
    GUID src_guid;
    ULONG item_cnt;
    ULONG entry_size;
    ULONG blob_len;
    ULONG blob_capacity;
    ULONG ring_read_idx;
    ULONG ring_write_idx;
    ULONG ring_items_present;
    ULONGLONG last_write100ns;
    ULONG has_create_file_object;
    ULONG has_open_file_object;
    ULONG create_file_object_orphaned;
} wmi_crash_src_t;

typedef struct wmi_crash_event {
    ULONGLONG sequence;
    ULONGLONG tick100ns;
    ULONG event_code;
    LONG status;
    ULONG src_idx;
    ULONG active_src;
    ULONG size;
    ULONG create_cnt;
    ULONG open_cnt;
    ULONG destroy_cnt;
    ULONG write_cnt;
    ULONG read_cnt;
    ULONG capture_cnt;
    ULONG live_kernel_create_cnt;
} wmi_crash_event_t;

typedef struct wmi_crashdump {
    ULONG version;
    ULONG active_src;
    ULONG in_use_src_cnt;
    ULONG src_capacity;
    ULONG create_cnt;
    ULONG open_cnt;
    ULONG destroy_cnt;
    ULONG write_cnt;
    ULONG read_cnt;
    ULONG capture_cnt;
    ULONG live_kernel_create_cnt;
    ULONGLONG event_sequence;
    ULONG history_cnt;
    ULONG history_dropped;
    ULONG last_event_code;
    LONG last_event_status;
    ULONG last_event_src_idx;
    ULONG last_event_size;
    ULONGLONG last_event_tick100ns;
    ULONG src_entry_cnt;
    wmi_crash_src_t sources[WMI_CRASH_SRC_CNT];
    ULONG history_entry_cnt;
    wmi_crash_event_t history[WMI_CRASH_HISTORY_CNT];
} wmi_crashdump_t;

typedef struct wmi_res_keys {
    ULONG res_snapshot_cnt;
    res_value_t dsd0;
    res_value_t dsd1;
    res_value_t dsd2;
    res_value_t dsd3;
    res_value_t dsd4;
    res_value_t dsd5_0;
    res_value_t mmr1;
    res_value_t mmr2;
    res_value_t mmr3;
    res_value_t mmr4;
    res_value_t mmr5;
    res_value_t mmr6;
    res_value_t mmr8;
    res_value_t mmr9;
    res_value_t sts0;
    res_value_t sts1;
    res_value_t sts2;
    res_value_t thq0;
    res_value_t thq1;
    res_value_t thq2;
    res_value_t tsr0;
    res_value_t tsr1_0;
    res_value_t tsr2_0;
    res_value_t tsr3;
    res_value_t bcl0;
    res_value_t bcl1;
    res_value_t bcl2;
    res_value_t bcl3;
    res_value_t bcl4;
    res_value_t int0;
    res_value_t int1;
    res_value_t int2;
    res_value_t int3;
    res_value_t int4;
    res_value_t qupw;
    res_value_t qwm0;
    res_value_t qwm1;
    res_value_t ssq0;
    res_value_t ssq1;
    res_value_t ssq2;
    res_value_t ssq3;
    res_value_t ssq4;
    res_value_t pkpr0;
    res_value_t pkpr4;
    res_value_t pkst0;
    res_value_t pkst4;
    res_value_t pkcl0;
    res_value_t pkcl1;
    res_value_t pkrt0;
    res_value_t pkrt1;
    res_value_t pksc0;
    res_value_t pksc1;
} wmi_res_keys_t;

typedef struct wmi_payload {
    ULONG version;
    wmi_config_t cfg;
    wmi_branchtrack_t branch_track;
    wmi_exsoc_t ex_soc;
    wmi_rscsw_t rsc_sw;
    wmi_stats_t stats;
    wmi_chipinfo_t chip;
    wmi_tsens_t tsens;
    wmi_mmrm_t mmrm;
    wmi_deepstandby_t deep_standby;
    wmi_thermalreq_t thermal_req;
    wmi_bcl_t bcl;
    wmi_interrupt_t interrupt;
    wmi_socsubsys_t soc_subsys;
    wmi_parksel_t park_sel;
    wmi_res_keys_t res;
    wmi_dcvs_t dcvs;
    wmi_crashdump_t crash_dump;
} wmi_payload_t;

#pragma pack(pop)

static VOID copy_name_a(_Out_writes_(capacity) CHAR* dst, _In_ SIZE_T capacity,
                        _In_opt_z_ const CHAR* src)
{
    SIZE_T i;

    if (!dst || capacity == 0)
        return;

    RtlZeroMemory(dst, capacity);
    if (!src)
        return;

    for (i = 0; (i + 1) < capacity && src[i] != '\0'; i++)
        dst[i] = src[i];
}

EVT_WDF_WMI_INSTANCE_QUERY_INSTANCE wmi_query_instance;
EVT_WDF_WMI_INSTANCE_SET_INSTANCE wmi_set_instance;

static NTSTATUS wmi_query_instance(_In_ WDFWMIINSTANCE wmi_instance, _In_ ULONG out_buf_size,
                                   _Out_writes_bytes_to_(out_buf_size, *buf_used) PVOID out_buf,
                                   _Out_ PULONG buf_used)
{
    UNREFERENCED_PARAMETER(wmi_instance);

    *buf_used = 0;
    if (out_buf_size < sizeof(wmi_payload_t))
        return STATUS_BUFFER_TOO_SMALL;

    wmi_payload_t* p = (wmi_payload_t*)out_buf;
    RtlZeroMemory(p, sizeof(*p));

    p->version = 23;

    p->cfg.branch_track_enabled = g.config.BranchTrackEnabled;
    p->cfg.enable_directed_p_state_features = g.config.EnableDirectedPStateFeatures;
    p->cfg.enable_park_selection = g.config.EnableParkSelection;
    p->cfg.enable_pepocd_test = g.config.EnablePEPOCDTest;
    p->cfg.enable_pepocd_test_low_perf = g.config.EnablePEPOCDTestLowPerf;
    p->cfg.pep_bcl_notify_level = g.config.PepBcl_Notification_Level;

    {
        res_branchtrack_snapshot_t bt;
        RtlZeroMemory(&bt, sizeof(bt));
        if (NT_SUCCESS(res_get_branch_track_snapshot(&bt))) {
            p->branch_track.version = bt.version;
            p->branch_track.entry_cnt = bt.entry_cnt;
            p->branch_track.active_cnt = bt.active_cnt;
            p->branch_track.event_sequence = bt.event_sequence;
            p->branch_track.history_cnt = bt.history_cnt;
            p->branch_track.history_dropped = bt.history_dropped;
            p->branch_track.last_event_active = bt.last_event_active;
            p->branch_track.last_event_update_cnt = bt.last_event_update_cnt;
            p->branch_track.last_event_res_id = bt.last_event_res_id;
            p->branch_track.last_event_sub_type = bt.last_event_sub_type;
            p->branch_track.last_event_value0 = bt.last_event_value0;
            p->branch_track.last_event_value1 = bt.last_event_value1;
            p->branch_track.last_event_value2 = bt.last_event_value2;
            p->branch_track.last_event_value3 = bt.last_event_value3;
            p->branch_track.last_event_tick100ns = bt.last_event_tick100ns;
        }

        res_branchtrack_event_t history[WMI_BRANCHTRACK_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(res_get_branch_track_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                                    &hist_dropped))) {
            p->branch_track.history_entry_cnt = hist_cnt;
            p->branch_track.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->branch_track.history); i++) {
                p->branch_track.history[i].tick100ns = history[i].tick100ns;
                p->branch_track.history[i].sequence = history[i].sequence;
                p->branch_track.history[i].res_id = history[i].res_id;
                p->branch_track.history[i].sub_type = history[i].sub_type;
                p->branch_track.history[i].value0 = history[i].value0;
                p->branch_track.history[i].value1 = history[i].value1;
                p->branch_track.history[i].value2 = history[i].value2;
                p->branch_track.history[i].value3 = history[i].value3;
                p->branch_track.history[i].active = history[i].active;
                p->branch_track.history[i].update_cnt = history[i].update_cnt;
            }
        }
    }

    {
        exsoc_snapshot_t ex;
        RtlZeroMemory(&ex, sizeof(ex));
        if (NT_SUCCESS(ex_soc_get_snapshot(&ex))) {
            p->ex_soc.version = ex.version;
            p->ex_soc.dev_cnt = ex.dev_cnt;
            p->ex_soc.apply_cnt = ex.apply_cnt;
            p->ex_soc.success_cnt = ex.success_cnt;
            p->ex_soc.failure_cnt = ex.failure_cnt;
            p->ex_soc.adsp_state_ioctl_cnt = ex.adsp_state_ioctl_cnt;
            p->ex_soc.adsp_ctrl_ioctl_cnt = ex.adsp_ctrl_ioctl_cnt;
            p->ex_soc.event_sequence = ex.event_sequence;
            p->ex_soc.history_cnt = ex.history_cnt;
            p->ex_soc.history_dropped = ex.history_dropped;
            p->ex_soc.last_ioctl_code = ex.last_ioctl_code;
            p->ex_soc.last_status = ex.last_status;
            p->ex_soc.last_dev_hash = ex.last_dev_hash;
            p->ex_soc.last_d_state = ex.last_d_state;
            p->ex_soc.last_tick100ns = ex.last_tick100ns;

            ULONG dev_cnt = ex.dev_cnt;
            if (dev_cnt > RTL_NUMBER_OF(p->ex_soc.devices))
                dev_cnt = RTL_NUMBER_OF(p->ex_soc.devices);
            p->ex_soc.dev_entry_cnt = dev_cnt;
            for (ULONG i = 0; i < dev_cnt; i++) {
                p->ex_soc.devices[i].dev_hash = ex.devices[i].dev_hash;
                p->ex_soc.devices[i].dev_id_length = ex.devices[i].dev_id_length;
                p->ex_soc.devices[i].last_d_state = ex.devices[i].last_d_state;
                p->ex_soc.devices[i].last_ioctl_code = ex.devices[i].last_ioctl_code;
                p->ex_soc.devices[i].apply_cnt = ex.devices[i].apply_cnt;
                p->ex_soc.devices[i].success_cnt = ex.devices[i].success_cnt;
                p->ex_soc.devices[i].failure_cnt = ex.devices[i].failure_cnt;
                p->ex_soc.devices[i].last_tick100ns = ex.devices[i].last_tick100ns;
                RtlCopyMemory(p->ex_soc.devices[i].dev_id, ex.devices[i].dev_id,
                              RTL_NUMBER_OF(p->ex_soc.devices[i].dev_id));
            }
        }

        exsoc_event_t history[WMI_EXSOC_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                ex_soc_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->ex_soc.history_entry_cnt = hist_cnt;
            p->ex_soc.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->ex_soc.history); i++) {
                p->ex_soc.history[i].tick100ns = history[i].tick100ns;
                p->ex_soc.history[i].sequence = history[i].sequence;
                p->ex_soc.history[i].ioctl_code = history[i].ioctl_code;
                p->ex_soc.history[i].status = history[i].status;
                p->ex_soc.history[i].dev_hash = history[i].dev_hash;
                p->ex_soc.history[i].d_state = history[i].d_state;
                p->ex_soc.history[i].requested_output_bytes = history[i].requested_output_bytes;
                p->ex_soc.history[i].returned_information = history[i].returned_information;
            }
        }
    }

    {
        rscsw_snapshot_t rs;
        RtlZeroMemory(&rs, sizeof(rs));
        if (NT_SUCCESS(rsc_sw_get_snapshot(&rs))) {
            p->rsc_sw.version = rs.version;
            p->rsc_sw.node_cnt = rs.node_cnt;
            p->rsc_sw.active_mask = rs.active_mask;
            p->rsc_sw.reserved = rs.reserved;
            p->rsc_sw.event_sequence = rs.event_sequence;
            p->rsc_sw.history_cnt = rs.history_cnt;
            p->rsc_sw.history_dropped = rs.history_dropped;
            p->rsc_sw.last_event_subsystem_id = rs.last_event_subsystem_id;
            p->rsc_sw.last_event_code = rs.last_event_code;
            p->rsc_sw.last_event_status = rs.last_event_status;
            p->rsc_sw.last_event_tick100ns = rs.last_event_tick100ns;
            p->rsc_sw.last_ioctl_code = rs.last_ioctl_code;
            p->rsc_sw.last_tag = rs.last_tag;
            p->rsc_sw.last_value0 = rs.last_value0;
            p->rsc_sw.last_value1 = rs.last_value1;
            p->rsc_sw.last_value2 = rs.last_value2;
            p->rsc_sw.last_value3 = rs.last_value3;
            p->rsc_sw.last_flags = rs.last_flags;
            p->rsc_sw.batch_apply_cnt = rs.batch_apply_cnt;
            p->rsc_sw.batch_command_cnt = rs.batch_command_cnt;
            p->rsc_sw.batch_error_cnt = rs.batch_error_cnt;
            p->rsc_sw.last_batch_cnt = rs.last_batch_cnt;
            p->rsc_sw.last_batch_status = rs.last_batch_status;
            p->rsc_sw.last_batch_tick100ns = rs.last_batch_tick100ns;
            p->rsc_sw.scenario_run_cnt = rs.scenario_run_cnt;
            p->rsc_sw.scenario_failure_cnt = rs.scenario_failure_cnt;
            p->rsc_sw.last_scenario_id = rs.last_scenario_id;
            p->rsc_sw.last_scenario_status = rs.last_scenario_status;
            p->rsc_sw.last_scenario_iterations = rs.last_scenario_iterations;
            p->rsc_sw.last_scenario_tick100ns = rs.last_scenario_tick100ns;

            ULONG node_cnt = rs.node_cnt;
            if (node_cnt > RTL_NUMBER_OF(p->rsc_sw.nodes))
                node_cnt = RTL_NUMBER_OF(p->rsc_sw.nodes);
            p->rsc_sw.node_entry_cnt = node_cnt;
            for (ULONG i = 0; i < node_cnt; i++) {
                p->rsc_sw.nodes[i].subsystem_id = rs.nodes[i].subsystem_id;
                RtlCopyMemory(p->rsc_sw.nodes[i].name, rs.nodes[i].name,
                              sizeof(p->rsc_sw.nodes[i].name));
                p->rsc_sw.nodes[i].present = rs.nodes[i].present;
                p->rsc_sw.nodes[i].state = rs.nodes[i].state;
                p->rsc_sw.nodes[i].clock_khz = rs.nodes[i].clock_khz;
                p->rsc_sw.nodes[i].flags = rs.nodes[i].flags;
                p->rsc_sw.nodes[i].last_ioctl_code = rs.nodes[i].last_ioctl_code;
                p->rsc_sw.nodes[i].last_tag = rs.nodes[i].last_tag;
                p->rsc_sw.nodes[i].last_value0 = rs.nodes[i].last_value0;
                p->rsc_sw.nodes[i].last_value1 = rs.nodes[i].last_value1;
                p->rsc_sw.nodes[i].last_value2 = rs.nodes[i].last_value2;
                p->rsc_sw.nodes[i].last_value3 = rs.nodes[i].last_value3;
                p->rsc_sw.nodes[i].req_cnt = rs.nodes[i].req_cnt;
                p->rsc_sw.nodes[i].success_cnt = rs.nodes[i].success_cnt;
                p->rsc_sw.nodes[i].failure_cnt = rs.nodes[i].failure_cnt;
                p->rsc_sw.nodes[i].vote_cnt = rs.nodes[i].vote_cnt;
                p->rsc_sw.nodes[i].command_cnt = rs.nodes[i].command_cnt;
                p->rsc_sw.nodes[i].pending_cnt = rs.nodes[i].pending_cnt;
                p->rsc_sw.nodes[i].last_status = rs.nodes[i].last_status;
                p->rsc_sw.nodes[i].last_tick100ns = rs.nodes[i].last_tick100ns;
                p->rsc_sw.nodes[i].activity_accumulator = rs.nodes[i].activity_accumulator;
            }
        }

        rscsw_event_t history[WMI_RSCSW_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                rsc_sw_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->rsc_sw.history_entry_cnt = hist_cnt;
            p->rsc_sw.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->rsc_sw.history); i++) {
                p->rsc_sw.history[i].tick100ns = history[i].tick100ns;
                p->rsc_sw.history[i].sequence = history[i].sequence;
                p->rsc_sw.history[i].subsystem_id = history[i].subsystem_id;
                p->rsc_sw.history[i].event_code = history[i].event_code;
                p->rsc_sw.history[i].status = history[i].status;
                p->rsc_sw.history[i].ioctl_code = history[i].ioctl_code;
                p->rsc_sw.history[i].tag = history[i].tag;
                p->rsc_sw.history[i].value0 = history[i].value0;
                p->rsc_sw.history[i].value1 = history[i].value1;
                p->rsc_sw.history[i].value2 = history[i].value2;
                p->rsc_sw.history[i].value3 = history[i].value3;
                p->rsc_sw.history[i].flags = history[i].flags;
            }
        }

        rscsw_catalog_entry_t catalog[WMI_RSCSW_CATALOG_CNT];
        ULONG cat_cnt = 0;
        RtlZeroMemory(catalog, sizeof(catalog));
        if (NT_SUCCESS(rsc_sw_get_catalog(catalog, RTL_NUMBER_OF(catalog), &cat_cnt))) {
            p->rsc_sw.catalog_entry_cnt = cat_cnt;
            for (ULONG i = 0; i < cat_cnt && i < RTL_NUMBER_OF(p->rsc_sw.catalog); i++) {
                p->rsc_sw.catalog[i].subsystem_id = catalog[i].subsystem_id;
                RtlCopyMemory(p->rsc_sw.catalog[i].name, catalog[i].name,
                              sizeof(p->rsc_sw.catalog[i].name));
                p->rsc_sw.catalog[i].cap_mask = catalog[i].cap_mask;
                p->rsc_sw.catalog[i].default_state = catalog[i].default_state;
                p->rsc_sw.catalog[i].default_clock_khz = catalog[i].default_clock_khz;
                p->rsc_sw.catalog[i].default_flags = catalog[i].default_flags;
                p->rsc_sw.catalog[i].res_tag = catalog[i].res_tag;
                p->rsc_sw.catalog[i].suggested_ioctl = catalog[i].suggested_ioctl;
            }
        }

        rscsw_scenario_result_t scenarios[WMI_RSCSW_SCENARIO_HISTORY_CNT];
        ULONG scenario_cnt = 0;
        ULONG scenario_dropped = 0;
        RtlZeroMemory(scenarios, sizeof(scenarios));
        if (NT_SUCCESS(rsc_sw_get_scenario_history(scenarios, RTL_NUMBER_OF(scenarios),
                                                   &scenario_cnt, &scenario_dropped))) {
            UNREFERENCED_PARAMETER(scenario_dropped);
            p->rsc_sw.scenario_history_entry_cnt = scenario_cnt;
            for (ULONG i = 0; i < scenario_cnt && i < RTL_NUMBER_OF(p->rsc_sw.scenario_history);
                 i++) {
                p->rsc_sw.scenario_history[i].scenario_id = scenarios[i].scenario_id;
                p->rsc_sw.scenario_history[i].iterations = scenarios[i].iterations;
                p->rsc_sw.scenario_history[i].flags = scenarios[i].flags;
                p->rsc_sw.scenario_history[i].status = scenarios[i].status;
                p->rsc_sw.scenario_history[i].applied_nodes = scenarios[i].applied_nodes;
                p->rsc_sw.scenario_history[i].failed_nodes = scenarios[i].failed_nodes;
                p->rsc_sw.scenario_history[i].last_subsystem_id = scenarios[i].last_subsystem_id;
                p->rsc_sw.scenario_history[i].last_tag = scenarios[i].last_tag;
                p->rsc_sw.scenario_history[i].last_value0 = scenarios[i].last_value0;
                p->rsc_sw.scenario_history[i].last_value1 = scenarios[i].last_value1;
                p->rsc_sw.scenario_history[i].last_value2 = scenarios[i].last_value2;
                p->rsc_sw.scenario_history[i].last_value3 = scenarios[i].last_value3;
                p->rsc_sw.scenario_history[i].tick100ns = scenarios[i].tick100ns;
            }
        }

        {
            rscsw_diag_snapshot_t diag;
            RtlZeroMemory(&diag, sizeof(diag));
            if (NT_SUCCESS(rsc_sw_get_diagnostics(&diag))) {
                p->rsc_sw.matrix_run_cnt = diag.matrix_run_cnt;
                p->rsc_sw.matrix_failure_cnt = diag.matrix_failure_cnt;
                p->rsc_sw.last_matrix_status = diag.last_matrix_status;
                p->rsc_sw.last_matrix_tick100ns = diag.last_matrix_tick100ns;
                p->rsc_sw.last_matrix_flags = diag.last_matrix_flags;
                p->rsc_sw.last_matrix_iterations = diag.last_matrix_iterations;
                p->rsc_sw.last_matrix_failures = diag.last_matrix_failures;

                ULONG diag_cnt = diag.entry_cnt;
                if (diag_cnt > RTL_NUMBER_OF(p->rsc_sw.diag))
                    diag_cnt = RTL_NUMBER_OF(p->rsc_sw.diag);
                p->rsc_sw.diag_entry_cnt = diag_cnt;
                for (ULONG i = 0; i < diag_cnt; i++) {
                    p->rsc_sw.diag[i].subsystem_id = diag.entries[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.diag[i].name, diag.entries[i].name,
                                  sizeof(p->rsc_sw.diag[i].name));
                    p->rsc_sw.diag[i].driver_map_id = diag.entries[i].driver_map_id;
                    p->rsc_sw.diag[i].ppp_res_type = diag.entries[i].ppp_res_type;
                    p->rsc_sw.diag[i].glink_event_seed = diag.entries[i].glink_event_seed;
                    p->rsc_sw.diag[i].icb_error_code = diag.entries[i].icb_error_code;
                    p->rsc_sw.diag[i].health_score = diag.entries[i].health_score;
                    p->rsc_sw.diag[i].confidence_score = diag.entries[i].confidence_score;
                    p->rsc_sw.diag[i].activity_score = diag.entries[i].activity_score;
                    p->rsc_sw.diag[i].cap_mask = diag.entries[i].cap_mask;
                    p->rsc_sw.diag[i].last_scenario_status = diag.entries[i].last_scenario_status;
                    p->rsc_sw.diag[i].last_node_status = diag.entries[i].last_node_status;
                    p->rsc_sw.diag[i].reserved0 = diag.entries[i].reserved0;
                    p->rsc_sw.diag[i].reserved1 = diag.entries[i].reserved1;
                }
            }
        }

        {
            rscsw_diag_result_t diag_history[WMI_RSCSW_DIAG_HISTORY_CNT];
            ULONG diag_history_cnt = 0;
            ULONG diag_history_dropped = 0;
            RtlZeroMemory(diag_history, sizeof(diag_history));
            if (NT_SUCCESS(rsc_sw_get_diagnostic_history(diag_history, RTL_NUMBER_OF(diag_history),
                                                         &diag_history_cnt,
                                                         &diag_history_dropped))) {
                UNREFERENCED_PARAMETER(diag_history_dropped);
                p->rsc_sw.diag_history_entry_cnt = diag_history_cnt;
                for (ULONG i = 0; i < diag_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.diag_history);
                     i++) {
                    p->rsc_sw.diag_history[i].iterations = diag_history[i].iterations;
                    p->rsc_sw.diag_history[i].flags = diag_history[i].flags;
                    p->rsc_sw.diag_history[i].status = diag_history[i].status;
                    p->rsc_sw.diag_history[i].scenario_runs = diag_history[i].scenario_runs;
                    p->rsc_sw.diag_history[i].scenario_failures = diag_history[i].scenario_failures;
                    p->rsc_sw.diag_history[i].last_scenario_id = diag_history[i].last_scenario_id;
                    p->rsc_sw.diag_history[i].last_subsystem_id = diag_history[i].last_subsystem_id;
                    p->rsc_sw.diag_history[i].last_tag = diag_history[i].last_tag;
                    p->rsc_sw.diag_history[i].tick100ns = diag_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_profile_snapshot_t profile;
            RtlZeroMemory(&profile, sizeof(profile));
            if (NT_SUCCESS(rsc_sw_get_profile_catalog(&profile))) {
                p->rsc_sw.profile_cnt = profile.profile_cnt;
                p->rsc_sw.profile_run_cnt = profile.profile_run_cnt;
                p->rsc_sw.profile_failure_cnt = profile.profile_failure_cnt;
                p->rsc_sw.last_profile_id = profile.last_profile_id;
                p->rsc_sw.last_profile_status = profile.last_profile_status;
                p->rsc_sw.last_profile_iterations = profile.last_profile_iterations;
                p->rsc_sw.last_profile_flags = profile.last_profile_flags;
                p->rsc_sw.last_profile_tick100ns = profile.last_profile_tick100ns;

                ULONG profile_cnt = profile.profile_cnt;
                if (profile_cnt > RTL_NUMBER_OF(p->rsc_sw.profiles))
                    profile_cnt = RTL_NUMBER_OF(p->rsc_sw.profiles);
                p->rsc_sw.profile_entry_cnt = profile_cnt;
                for (ULONG i = 0; i < profile_cnt; i++) {
                    p->rsc_sw.profiles[i].profile_id = profile.profiles[i].profile_id;
                    RtlCopyMemory(p->rsc_sw.profiles[i].name, profile.profiles[i].name,
                                  sizeof(p->rsc_sw.profiles[i].name));
                    p->rsc_sw.profiles[i].subsystem_mask = profile.profiles[i].subsystem_mask;
                    p->rsc_sw.profiles[i].scenario_flags = profile.profiles[i].scenario_flags;
                    p->rsc_sw.profiles[i].matrix_flags = profile.profiles[i].matrix_flags;
                    p->rsc_sw.profiles[i].default_iterations =
                        profile.profiles[i].default_iterations;
                    p->rsc_sw.profiles[i].priority = profile.profiles[i].priority;
                    p->rsc_sw.profiles[i].reserved = profile.profiles[i].reserved;
                }
            }
        }

        {
            rscsw_profile_result_t profile_history[WMI_RSCSW_PROFILE_HISTORY_CNT];
            ULONG profile_history_cnt = 0;
            ULONG profile_history_dropped = 0;
            RtlZeroMemory(profile_history, sizeof(profile_history));
            if (NT_SUCCESS(
                    rsc_sw_get_profile_history(profile_history, RTL_NUMBER_OF(profile_history),
                                               &profile_history_cnt, &profile_history_dropped))) {
                UNREFERENCED_PARAMETER(profile_history_dropped);
                p->rsc_sw.profile_history_entry_cnt = profile_history_cnt;
                for (ULONG i = 0;
                     i < profile_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.profile_history); i++) {
                    p->rsc_sw.profile_history[i].profile_id = profile_history[i].profile_id;
                    p->rsc_sw.profile_history[i].iterations = profile_history[i].iterations;
                    p->rsc_sw.profile_history[i].flags = profile_history[i].flags;
                    p->rsc_sw.profile_history[i].status = profile_history[i].status;
                    p->rsc_sw.profile_history[i].scenario_runs = profile_history[i].scenario_runs;
                    p->rsc_sw.profile_history[i].scenario_failures =
                        profile_history[i].scenario_failures;
                    p->rsc_sw.profile_history[i].matrix_runs = profile_history[i].matrix_runs;
                    p->rsc_sw.profile_history[i].matrix_failures =
                        profile_history[i].matrix_failures;
                    p->rsc_sw.profile_history[i].last_scenario_id =
                        profile_history[i].last_scenario_id;
                    p->rsc_sw.profile_history[i].last_subsystem_id =
                        profile_history[i].last_subsystem_id;
                    p->rsc_sw.profile_history[i].last_tag = profile_history[i].last_tag;
                    p->rsc_sw.profile_history[i].tick100ns = profile_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_graph_snapshot_t graph;
            RtlZeroMemory(&graph, sizeof(graph));
            if (NT_SUCCESS(rsc_sw_get_dependency_graph(&graph))) {
                p->rsc_sw.graph_version = graph.version;
                p->rsc_sw.graph_node_cnt = graph.node_cnt;
                p->rsc_sw.graph_link_cnt = graph.link_cnt;
                p->rsc_sw.graph_active_mask = graph.active_mask;
                p->rsc_sw.last_action_sequence_status = (LONG)graph.last_action_sequence_status;
                p->rsc_sw.last_action_sequence_step_cnt = graph.last_action_sequence_step_cnt;
                p->rsc_sw.last_action_sequence_failure_cnt = graph.last_action_sequence_failure_cnt;
                p->rsc_sw.last_action_sequence_tick100ns = graph.last_action_sequence_tick100ns;

                ULONG graph_cnt = graph.link_cnt;
                if (graph_cnt > RTL_NUMBER_OF(p->rsc_sw.graph))
                    graph_cnt = RTL_NUMBER_OF(p->rsc_sw.graph);
                p->rsc_sw.graph_entry_cnt = graph_cnt;
                for (ULONG i = 0; i < graph_cnt; i++) {
                    p->rsc_sw.graph[i].from_subsystem_id = graph.links[i].from_subsystem_id;
                    p->rsc_sw.graph[i].to_subsystem_id = graph.links[i].to_subsystem_id;
                    p->rsc_sw.graph[i].weight = graph.links[i].weight;
                    p->rsc_sw.graph[i].link_type = graph.links[i].link_type;
                    p->rsc_sw.graph[i].flags = graph.links[i].flags;
                }
            }
        }

        {
            rscsw_action_sequence_result_t
                action_sequence_history[WMI_RSCSW_ACTION_SEQUENCE_HISTORY_CNT];
            ULONG action_sequence_history_cnt = 0;
            ULONG action_sequence_history_dropped = 0;
            RtlZeroMemory(action_sequence_history, sizeof(action_sequence_history));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_history(
                    action_sequence_history, RTL_NUMBER_OF(action_sequence_history),
                    &action_sequence_history_cnt, &action_sequence_history_dropped))) {
                UNREFERENCED_PARAMETER(action_sequence_history_dropped);
                p->rsc_sw.action_sequence_history_entry_cnt = action_sequence_history_cnt;
                for (ULONG i = 0; i < action_sequence_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.action_sequence_history);
                     i++) {
                    p->rsc_sw.action_sequence_history[i].requested_steps =
                        action_sequence_history[i].requested_steps;
                    p->rsc_sw.action_sequence_history[i].executed_steps =
                        action_sequence_history[i].executed_steps;
                    p->rsc_sw.action_sequence_history[i].failed_steps =
                        action_sequence_history[i].failed_steps;
                    p->rsc_sw.action_sequence_history[i].status = action_sequence_history[i].status;
                    p->rsc_sw.action_sequence_history[i].last_subsystem_id =
                        action_sequence_history[i].last_subsystem_id;
                    p->rsc_sw.action_sequence_history[i].last_tag =
                        action_sequence_history[i].last_tag;
                    p->rsc_sw.action_sequence_history[i].last_value0 =
                        action_sequence_history[i].last_value0;
                    p->rsc_sw.action_sequence_history[i].last_value1 =
                        action_sequence_history[i].last_value1;
                    p->rsc_sw.action_sequence_history[i].last_value2 =
                        action_sequence_history[i].last_value2;
                    p->rsc_sw.action_sequence_history[i].last_value3 =
                        action_sequence_history[i].last_value3;
                    p->rsc_sw.action_sequence_history[i].tick100ns =
                        action_sequence_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_action_sequence_preview_snapshot_t preview;
            RtlZeroMemory(&preview, sizeof(preview));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_preview_snapshot(&preview))) {
                p->rsc_sw.action_sequence_preview_run_cnt = preview.run_cnt;
                p->rsc_sw.action_sequence_preview_failure_cnt = preview.failure_cnt;
                p->rsc_sw.action_sequence_preview_last_status = preview.last_status;
                p->rsc_sw.action_sequence_preview_last_requested_steps =
                    preview.last_requested_steps;
                p->rsc_sw.action_sequence_preview_last_expanded_steps = preview.last_expanded_steps;
                p->rsc_sw.action_sequence_preview_last_invalid_steps = preview.last_invalid_steps;
                p->rsc_sw.action_sequence_preview_last_duplicate_steps =
                    preview.last_duplicate_steps;
                p->rsc_sw.action_sequence_preview_last_dependency_edges =
                    preview.last_dependency_edges;
                p->rsc_sw.action_sequence_preview_last_reordered_steps =
                    preview.last_reordered_steps;
                p->rsc_sw.action_sequence_preview_last_active_mask_before =
                    preview.last_active_mask_before;
                p->rsc_sw.action_sequence_preview_last_active_mask_after =
                    preview.last_active_mask_after;
                p->rsc_sw.action_sequence_preview_last_flags = preview.last_flags;
                p->rsc_sw.action_sequence_preview_last_tick100ns = preview.last_tick100ns;
            }
        }

        {
            rscsw_action_sequence_preview_result_t
                preview_history[WMI_RSCSW_ACTION_SEQUENCE_PREVIEW_HISTORY_CNT];
            ULONG preview_history_cnt = 0;
            ULONG preview_history_dropped = 0;
            RtlZeroMemory(preview_history, sizeof(preview_history));
            if (NT_SUCCESS(rsc_sw_get_action_sequence_preview_history(
                    preview_history, RTL_NUMBER_OF(preview_history), &preview_history_cnt,
                    &preview_history_dropped))) {
                p->rsc_sw.action_sequence_preview_history_entry_cnt = preview_history_cnt;
                p->rsc_sw.action_sequence_preview_history_dropped = preview_history_dropped;
                for (ULONG i = 0; i < preview_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.action_sequence_preview_history);
                     i++) {
                    p->rsc_sw.action_sequence_preview_history[i].requested_steps =
                        preview_history[i].requested_steps;
                    p->rsc_sw.action_sequence_preview_history[i].flags = preview_history[i].flags;
                    p->rsc_sw.action_sequence_preview_history[i].expanded_steps =
                        preview_history[i].expanded_steps;
                    p->rsc_sw.action_sequence_preview_history[i].invalid_steps =
                        preview_history[i].invalid_steps;
                    p->rsc_sw.action_sequence_preview_history[i].duplicate_steps =
                        preview_history[i].duplicate_steps;
                    p->rsc_sw.action_sequence_preview_history[i].dependency_edges =
                        preview_history[i].dependency_edges;
                    p->rsc_sw.action_sequence_preview_history[i].reordered_steps =
                        preview_history[i].reordered_steps;
                    p->rsc_sw.action_sequence_preview_history[i].active_mask_before =
                        preview_history[i].active_mask_before;
                    p->rsc_sw.action_sequence_preview_history[i].active_mask_after =
                        preview_history[i].active_mask_after;
                    p->rsc_sw.action_sequence_preview_history[i].status = preview_history[i].status;
                    p->rsc_sw.action_sequence_preview_history[i].last_subsystem_id =
                        preview_history[i].last_subsystem_id;
                    p->rsc_sw.action_sequence_preview_history[i].last_tag =
                        preview_history[i].last_tag;
                    p->rsc_sw.action_sequence_preview_history[i].last_value0 =
                        preview_history[i].last_value0;
                    p->rsc_sw.action_sequence_preview_history[i].last_value1 =
                        preview_history[i].last_value1;
                    p->rsc_sw.action_sequence_preview_history[i].last_value2 =
                        preview_history[i].last_value2;
                    p->rsc_sw.action_sequence_preview_history[i].last_value3 =
                        preview_history[i].last_value3;
                    p->rsc_sw.action_sequence_preview_history[i].tick100ns =
                        preview_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_checkpoint_snapshot_t checkpoint;
            RtlZeroMemory(&checkpoint, sizeof(checkpoint));
            if (NT_SUCCESS(rsc_sw_get_checkpoint_snapshot(&checkpoint))) {
                p->rsc_sw.checkpoint_slot_cnt = checkpoint.slot_cnt;
                p->rsc_sw.checkpoint_valid_cnt = checkpoint.valid_cnt;
                p->rsc_sw.checkpoint_capture_cnt = checkpoint.capture_cnt;
                p->rsc_sw.checkpoint_apply_cnt = checkpoint.apply_cnt;
                p->rsc_sw.checkpoint_last_status = checkpoint.last_operation_status;
                p->rsc_sw.checkpoint_last_slot_id = checkpoint.last_operation_slot_id;
                p->rsc_sw.checkpoint_last_tick100ns = checkpoint.last_operation_tick100ns;

                ULONG checkpoint_cnt = checkpoint.slot_cnt;
                if (checkpoint_cnt > RTL_NUMBER_OF(p->rsc_sw.checkpoints))
                    checkpoint_cnt = RTL_NUMBER_OF(p->rsc_sw.checkpoints);
                p->rsc_sw.checkpoint_entry_cnt = checkpoint_cnt;
                for (ULONG i = 0; i < checkpoint_cnt; i++) {
                    p->rsc_sw.checkpoints[i].slot_id = checkpoint.slots[i].slot_id;
                    p->rsc_sw.checkpoints[i].valid = checkpoint.slots[i].valid;
                    RtlCopyMemory(p->rsc_sw.checkpoints[i].name, checkpoint.slots[i].name,
                                  sizeof(p->rsc_sw.checkpoints[i].name));
                    p->rsc_sw.checkpoints[i].capture_cnt = checkpoint.slots[i].capture_cnt;
                    p->rsc_sw.checkpoints[i].apply_cnt = checkpoint.slots[i].apply_cnt;
                    p->rsc_sw.checkpoints[i].last_status = checkpoint.slots[i].last_status;
                    p->rsc_sw.checkpoints[i].node_cnt = checkpoint.slots[i].node_cnt;
                    p->rsc_sw.checkpoints[i].active_mask = checkpoint.slots[i].active_mask;
                    p->rsc_sw.checkpoints[i].last_tick100ns = checkpoint.slots[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_checkpoint_result_t checkpoint_history[WMI_RSCSW_CHECKPOINT_HISTORY_CNT];
            ULONG checkpoint_history_cnt = 0;
            ULONG checkpoint_history_dropped = 0;
            RtlZeroMemory(checkpoint_history, sizeof(checkpoint_history));
            if (NT_SUCCESS(rsc_sw_get_checkpoint_history(
                    checkpoint_history, RTL_NUMBER_OF(checkpoint_history), &checkpoint_history_cnt,
                    &checkpoint_history_dropped))) {
                p->rsc_sw.checkpoint_history_entry_cnt = checkpoint_history_cnt;
                p->rsc_sw.checkpoint_history_dropped = checkpoint_history_dropped;
                for (ULONG i = 0;
                     i < checkpoint_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.checkpoint_history);
                     i++) {
                    p->rsc_sw.checkpoint_history[i].operation = checkpoint_history[i].operation;
                    p->rsc_sw.checkpoint_history[i].slot_id = checkpoint_history[i].slot_id;
                    p->rsc_sw.checkpoint_history[i].flags = checkpoint_history[i].flags;
                    p->rsc_sw.checkpoint_history[i].status = checkpoint_history[i].status;
                    p->rsc_sw.checkpoint_history[i].node_cnt = checkpoint_history[i].node_cnt;
                    p->rsc_sw.checkpoint_history[i].success_cnt = checkpoint_history[i].success_cnt;
                    p->rsc_sw.checkpoint_history[i].failure_cnt = checkpoint_history[i].failure_cnt;
                    p->rsc_sw.checkpoint_history[i].last_subsystem_id =
                        checkpoint_history[i].last_subsystem_id;
                    p->rsc_sw.checkpoint_history[i].last_tag = checkpoint_history[i].last_tag;
                    p->rsc_sw.checkpoint_history[i].last_value0 = checkpoint_history[i].last_value0;
                    p->rsc_sw.checkpoint_history[i].last_value1 = checkpoint_history[i].last_value1;
                    p->rsc_sw.checkpoint_history[i].last_value2 = checkpoint_history[i].last_value2;
                    p->rsc_sw.checkpoint_history[i].last_value3 = checkpoint_history[i].last_value3;
                    p->rsc_sw.checkpoint_history[i].tick100ns = checkpoint_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_coverage_snapshot_t coverage;
            RtlZeroMemory(&coverage, sizeof(coverage));
            if (NT_SUCCESS(rsc_sw_get_coverage_snapshot(&coverage))) {
                p->rsc_sw.coverage_run_cnt = coverage.run_cnt;
                p->rsc_sw.coverage_failure_cnt = coverage.failure_cnt;
                p->rsc_sw.coverage_last_status = coverage.last_status;
                p->rsc_sw.coverage_last_health_score = coverage.last_health_score;
                p->rsc_sw.coverage_last_activity_score = coverage.last_activity_score;
                p->rsc_sw.coverage_last_warning_cnt = coverage.last_warning_cnt;
                p->rsc_sw.coverage_last_error_cnt = coverage.last_error_cnt;
                p->rsc_sw.coverage_last_threshold_permille = coverage.last_threshold_permille;
                p->rsc_sw.coverage_last_flags = coverage.last_flags;
                p->rsc_sw.coverage_last_tick100ns = coverage.last_tick100ns;

                ULONG coverage_cnt = coverage.node_cnt;
                if (coverage_cnt > RTL_NUMBER_OF(p->rsc_sw.coverage))
                    coverage_cnt = RTL_NUMBER_OF(p->rsc_sw.coverage);
                p->rsc_sw.coverage_entry_cnt = coverage_cnt;
                for (ULONG i = 0; i < coverage_cnt; i++) {
                    p->rsc_sw.coverage[i].subsystem_id = coverage.rows[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.coverage[i].name, coverage.rows[i].name,
                                  sizeof(p->rsc_sw.coverage[i].name));
                    p->rsc_sw.coverage[i].present = coverage.rows[i].present;
                    p->rsc_sw.coverage[i].active = coverage.rows[i].active;
                    p->rsc_sw.coverage[i].req_cnt = coverage.rows[i].req_cnt;
                    p->rsc_sw.coverage[i].success_cnt = coverage.rows[i].success_cnt;
                    p->rsc_sw.coverage[i].failure_cnt = coverage.rows[i].failure_cnt;
                    p->rsc_sw.coverage[i].success_permille = coverage.rows[i].success_permille;
                    p->rsc_sw.coverage[i].dependency_in = coverage.rows[i].dependency_in;
                    p->rsc_sw.coverage[i].dependency_out = coverage.rows[i].dependency_out;
                    p->rsc_sw.coverage[i].health_score = coverage.rows[i].health_score;
                    p->rsc_sw.coverage[i].activity_score = coverage.rows[i].activity_score;
                    p->rsc_sw.coverage[i].flags = coverage.rows[i].flags;
                    p->rsc_sw.coverage[i].last_status = coverage.rows[i].last_status;
                    p->rsc_sw.coverage[i].last_tick100ns = coverage.rows[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_coverage_result_t coverage_history[WMI_RSCSW_COVERAGE_HISTORY_CNT];
            ULONG coverage_history_cnt = 0;
            ULONG coverage_history_dropped = 0;
            RtlZeroMemory(coverage_history, sizeof(coverage_history));
            if (NT_SUCCESS(rsc_sw_get_coverage_history(
                    coverage_history, RTL_NUMBER_OF(coverage_history), &coverage_history_cnt,
                    &coverage_history_dropped))) {
                p->rsc_sw.coverage_history_entry_cnt = coverage_history_cnt;
                p->rsc_sw.coverage_history_dropped = coverage_history_dropped;
                for (ULONG i = 0;
                     i < coverage_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.coverage_history);
                     i++) {
                    p->rsc_sw.coverage_history[i].flags = coverage_history[i].flags;
                    p->rsc_sw.coverage_history[i].node_cnt = coverage_history[i].node_cnt;
                    p->rsc_sw.coverage_history[i].status = coverage_history[i].status;
                    p->rsc_sw.coverage_history[i].health_score = coverage_history[i].health_score;
                    p->rsc_sw.coverage_history[i].activity_score =
                        coverage_history[i].activity_score;
                    p->rsc_sw.coverage_history[i].warning_cnt = coverage_history[i].warning_cnt;
                    p->rsc_sw.coverage_history[i].error_cnt = coverage_history[i].error_cnt;
                    p->rsc_sw.coverage_history[i].below_threshold_cnt =
                        coverage_history[i].below_threshold_cnt;
                    p->rsc_sw.coverage_history[i].threshold_permille =
                        coverage_history[i].threshold_permille;
                    p->rsc_sw.coverage_history[i].active_mask_before =
                        coverage_history[i].active_mask_before;
                    p->rsc_sw.coverage_history[i].active_mask_after =
                        coverage_history[i].active_mask_after;
                    p->rsc_sw.coverage_history[i].last_subsystem_id =
                        coverage_history[i].last_subsystem_id;
                    p->rsc_sw.coverage_history[i].last_tag = coverage_history[i].last_tag;
                    p->rsc_sw.coverage_history[i].last_value0 = coverage_history[i].last_value0;
                    p->rsc_sw.coverage_history[i].last_value1 = coverage_history[i].last_value1;
                    p->rsc_sw.coverage_history[i].last_value2 = coverage_history[i].last_value2;
                    p->rsc_sw.coverage_history[i].last_value3 = coverage_history[i].last_value3;
                    p->rsc_sw.coverage_history[i].tick100ns = coverage_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_remediation_snapshot_t remediation;
            RtlZeroMemory(&remediation, sizeof(remediation));
            if (NT_SUCCESS(rsc_sw_get_remediation_snapshot(&remediation))) {
                p->rsc_sw.remediation_run_cnt = remediation.run_cnt;
                p->rsc_sw.remediation_failure_cnt = remediation.failure_cnt;
                p->rsc_sw.remediation_last_status = remediation.last_status;
                p->rsc_sw.remediation_last_recommended_cnt = remediation.last_recommended_cnt;
                p->rsc_sw.remediation_last_executed_cnt = remediation.last_executed_cnt;
                p->rsc_sw.remediation_last_failed_cnt = remediation.last_failed_cnt;
                p->rsc_sw.remediation_last_coverage_threshold_permille =
                    remediation.last_coverage_threshold_permille;
                p->rsc_sw.remediation_last_coverage_health_score =
                    remediation.last_coverage_health_score;
                p->rsc_sw.remediation_last_coverage_activity_score =
                    remediation.last_coverage_activity_score;
                p->rsc_sw.remediation_last_warning_cnt = remediation.last_warning_cnt;
                p->rsc_sw.remediation_last_error_cnt = remediation.last_error_cnt;
                p->rsc_sw.remediation_last_worst_subsystem_id = remediation.last_worst_subsystem_id;
                p->rsc_sw.remediation_last_flags = remediation.last_flags;
                p->rsc_sw.remediation_last_tick100ns = remediation.last_tick100ns;

                ULONG remediation_action_cnt = remediation.action_cnt;
                if (remediation_action_cnt > RTL_NUMBER_OF(p->rsc_sw.remediation_actions))
                    remediation_action_cnt = RTL_NUMBER_OF(p->rsc_sw.remediation_actions);
                p->rsc_sw.remediation_action_entry_cnt = remediation_action_cnt;
                for (ULONG i = 0; i < remediation_action_cnt; i++) {
                    p->rsc_sw.remediation_actions[i].subsystem_id =
                        remediation.actions[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.remediation_actions[i].name,
                                  remediation.actions[i].name,
                                  sizeof(p->rsc_sw.remediation_actions[i].name));
                    p->rsc_sw.remediation_actions[i].priority = remediation.actions[i].priority;
                    p->rsc_sw.remediation_actions[i].reason_flags =
                        remediation.actions[i].reason_flags;
                    p->rsc_sw.remediation_actions[i].current_state =
                        remediation.actions[i].current_state;
                    p->rsc_sw.remediation_actions[i].recommended_state =
                        remediation.actions[i].recommended_state;
                    p->rsc_sw.remediation_actions[i].current_clock_khz =
                        remediation.actions[i].current_clock_khz;
                    p->rsc_sw.remediation_actions[i].recommended_clock_khz =
                        remediation.actions[i].recommended_clock_khz;
                    p->rsc_sw.remediation_actions[i].current_flags =
                        remediation.actions[i].current_flags;
                    p->rsc_sw.remediation_actions[i].recommended_flags =
                        remediation.actions[i].recommended_flags;
                    p->rsc_sw.remediation_actions[i].health_score =
                        remediation.actions[i].health_score;
                    p->rsc_sw.remediation_actions[i].activity_score =
                        remediation.actions[i].activity_score;
                    p->rsc_sw.remediation_actions[i].success_permille =
                        remediation.actions[i].success_permille;
                    p->rsc_sw.remediation_actions[i].dependency_in =
                        remediation.actions[i].dependency_in;
                    p->rsc_sw.remediation_actions[i].dependency_out =
                        remediation.actions[i].dependency_out;
                    p->rsc_sw.remediation_actions[i].last_status =
                        remediation.actions[i].last_status;
                }
            }
        }

        {
            rscsw_remediation_result_t remediation_history[WMI_RSCSW_REMEDIATION_HISTORY_CNT];
            ULONG remediation_history_cnt = 0;
            ULONG remediation_history_dropped = 0;
            RtlZeroMemory(remediation_history, sizeof(remediation_history));
            if (NT_SUCCESS(rsc_sw_get_remediation_history(
                    remediation_history, RTL_NUMBER_OF(remediation_history),
                    &remediation_history_cnt, &remediation_history_dropped))) {
                p->rsc_sw.remediation_history_entry_cnt = remediation_history_cnt;
                p->rsc_sw.remediation_history_dropped = remediation_history_dropped;
                for (ULONG i = 0; i < remediation_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.remediation_history);
                     i++) {
                    p->rsc_sw.remediation_history[i].flags = remediation_history[i].flags;
                    p->rsc_sw.remediation_history[i].status = remediation_history[i].status;
                    p->rsc_sw.remediation_history[i].recommended_cnt =
                        remediation_history[i].recommended_cnt;
                    p->rsc_sw.remediation_history[i].executed_cnt =
                        remediation_history[i].executed_cnt;
                    p->rsc_sw.remediation_history[i].failed_cnt = remediation_history[i].failed_cnt;
                    p->rsc_sw.remediation_history[i].coverage_threshold_permille =
                        remediation_history[i].coverage_threshold_permille;
                    p->rsc_sw.remediation_history[i].coverage_health_score =
                        remediation_history[i].coverage_health_score;
                    p->rsc_sw.remediation_history[i].coverage_activity_score =
                        remediation_history[i].coverage_activity_score;
                    p->rsc_sw.remediation_history[i].warning_cnt =
                        remediation_history[i].warning_cnt;
                    p->rsc_sw.remediation_history[i].error_cnt = remediation_history[i].error_cnt;
                    p->rsc_sw.remediation_history[i].worst_subsystem_id =
                        remediation_history[i].worst_subsystem_id;
                    p->rsc_sw.remediation_history[i].last_subsystem_id =
                        remediation_history[i].last_subsystem_id;
                    p->rsc_sw.remediation_history[i].last_tag = remediation_history[i].last_tag;
                    p->rsc_sw.remediation_history[i].last_value0 =
                        remediation_history[i].last_value0;
                    p->rsc_sw.remediation_history[i].last_value1 =
                        remediation_history[i].last_value1;
                    p->rsc_sw.remediation_history[i].last_value2 =
                        remediation_history[i].last_value2;
                    p->rsc_sw.remediation_history[i].last_value3 =
                        remediation_history[i].last_value3;
                    p->rsc_sw.remediation_history[i].tick100ns = remediation_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_stabilization_snapshot_t stabilization;
            RtlZeroMemory(&stabilization, sizeof(stabilization));
            if (NT_SUCCESS(rsc_sw_get_stabilization_snapshot(&stabilization))) {
                p->rsc_sw.stabilization_run_cnt = stabilization.run_cnt;
                p->rsc_sw.stabilization_failure_cnt = stabilization.failure_cnt;
                p->rsc_sw.stabilization_last_status = stabilization.last_status;
                p->rsc_sw.stabilization_last_flags = stabilization.last_flags;
                p->rsc_sw.stabilization_last_lane_cnt = stabilization.last_lane_cnt;
                p->rsc_sw.stabilization_last_degraded_lane_cnt =
                    stabilization.last_degraded_lane_cnt;
                p->rsc_sw.stabilization_last_remediation_status =
                    stabilization.last_remediation_status;
                p->rsc_sw.stabilization_last_coverage_health_score =
                    stabilization.last_coverage_health_score;
                p->rsc_sw.stabilization_last_coverage_activity_score =
                    stabilization.last_coverage_activity_score;
                p->rsc_sw.stabilization_last_coverage_warning_cnt =
                    stabilization.last_coverage_warning_cnt;
                p->rsc_sw.stabilization_last_coverage_error_cnt =
                    stabilization.last_coverage_error_cnt;
                p->rsc_sw.stabilization_last_profile_runs = stabilization.last_profile_runs;
                p->rsc_sw.stabilization_last_profile_failures = stabilization.last_profile_failures;
                p->rsc_sw.stabilization_last_tick100ns = stabilization.last_tick100ns;

                ULONG lane_cnt = stabilization.lane_cnt;
                if (lane_cnt > RTL_NUMBER_OF(p->rsc_sw.stabilization_lanes))
                    lane_cnt = RTL_NUMBER_OF(p->rsc_sw.stabilization_lanes);
                p->rsc_sw.stabilization_lane_entry_cnt = lane_cnt;
                for (ULONG i = 0; i < lane_cnt; i++) {
                    p->rsc_sw.stabilization_lanes[i].lane_id = stabilization.lanes[i].lane_id;
                    RtlCopyMemory(p->rsc_sw.stabilization_lanes[i].name,
                                  stabilization.lanes[i].name,
                                  sizeof(p->rsc_sw.stabilization_lanes[i].name));
                    p->rsc_sw.stabilization_lanes[i].subsystem_mask =
                        stabilization.lanes[i].subsystem_mask;
                    p->rsc_sw.stabilization_lanes[i].node_cnt = stabilization.lanes[i].node_cnt;
                    p->rsc_sw.stabilization_lanes[i].degraded_cnt =
                        stabilization.lanes[i].degraded_cnt;
                    p->rsc_sw.stabilization_lanes[i].health_score =
                        stabilization.lanes[i].health_score;
                    p->rsc_sw.stabilization_lanes[i].activity_score =
                        stabilization.lanes[i].activity_score;
                    p->rsc_sw.stabilization_lanes[i].warning_cnt =
                        stabilization.lanes[i].warning_cnt;
                    p->rsc_sw.stabilization_lanes[i].error_cnt = stabilization.lanes[i].error_cnt;
                    p->rsc_sw.stabilization_lanes[i].last_status =
                        stabilization.lanes[i].last_status;
                    p->rsc_sw.stabilization_lanes[i].last_tick100ns =
                        stabilization.lanes[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_stabilization_result_t stabilization_history[WMI_RSCSW_STABILIZATION_HISTORY_CNT];
            ULONG stabilization_history_cnt = 0;
            ULONG stabilization_history_dropped = 0;
            RtlZeroMemory(stabilization_history, sizeof(stabilization_history));
            if (NT_SUCCESS(rsc_sw_get_stabilization_history(
                    stabilization_history, RTL_NUMBER_OF(stabilization_history),
                    &stabilization_history_cnt, &stabilization_history_dropped))) {
                p->rsc_sw.stabilization_history_entry_cnt = stabilization_history_cnt;
                p->rsc_sw.stabilization_history_dropped = stabilization_history_dropped;
                for (ULONG i = 0; i < stabilization_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.stabilization_history);
                     i++) {
                    p->rsc_sw.stabilization_history[i].flags = stabilization_history[i].flags;
                    p->rsc_sw.stabilization_history[i].status = stabilization_history[i].status;
                    p->rsc_sw.stabilization_history[i].lane_cnt = stabilization_history[i].lane_cnt;
                    p->rsc_sw.stabilization_history[i].degraded_lane_cnt =
                        stabilization_history[i].degraded_lane_cnt;
                    p->rsc_sw.stabilization_history[i].remediation_status =
                        stabilization_history[i].remediation_status;
                    p->rsc_sw.stabilization_history[i].coverage_health_score =
                        stabilization_history[i].coverage_health_score;
                    p->rsc_sw.stabilization_history[i].coverage_activity_score =
                        stabilization_history[i].coverage_activity_score;
                    p->rsc_sw.stabilization_history[i].coverage_warning_cnt =
                        stabilization_history[i].coverage_warning_cnt;
                    p->rsc_sw.stabilization_history[i].coverage_error_cnt =
                        stabilization_history[i].coverage_error_cnt;
                    p->rsc_sw.stabilization_history[i].profile_runs =
                        stabilization_history[i].profile_runs;
                    p->rsc_sw.stabilization_history[i].profile_failures =
                        stabilization_history[i].profile_failures;
                    p->rsc_sw.stabilization_history[i].last_lane_id =
                        stabilization_history[i].last_lane_id;
                    p->rsc_sw.stabilization_history[i].last_subsystem_id =
                        stabilization_history[i].last_subsystem_id;
                    p->rsc_sw.stabilization_history[i].last_tag = stabilization_history[i].last_tag;
                    p->rsc_sw.stabilization_history[i].last_value0 =
                        stabilization_history[i].last_value0;
                    p->rsc_sw.stabilization_history[i].last_value1 =
                        stabilization_history[i].last_value1;
                    p->rsc_sw.stabilization_history[i].last_value2 =
                        stabilization_history[i].last_value2;
                    p->rsc_sw.stabilization_history[i].last_value3 =
                        stabilization_history[i].last_value3;
                    p->rsc_sw.stabilization_history[i].tick100ns =
                        stabilization_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_conformance_snapshot_t conformance;
            RtlZeroMemory(&conformance, sizeof(conformance));
            if (NT_SUCCESS(rsc_sw_get_conformance_snapshot(&conformance))) {
                p->rsc_sw.conformance_run_cnt = conformance.run_cnt;
                p->rsc_sw.conformance_failure_cnt = conformance.failure_cnt;
                p->rsc_sw.conformance_last_status = conformance.last_status;
                p->rsc_sw.conformance_last_flags = conformance.last_flags;
                p->rsc_sw.conformance_last_overall_score_permille =
                    conformance.last_overall_score_permille;
                p->rsc_sw.conformance_last_threshold_permille = conformance.last_threshold_permille;
                p->rsc_sw.conformance_last_passing_cnt = conformance.last_passing_cnt;
                p->rsc_sw.conformance_last_warning_cnt = conformance.last_warning_cnt;
                p->rsc_sw.conformance_last_error_cnt = conformance.last_error_cnt;
                p->rsc_sw.conformance_last_worst_subsystem_id = conformance.last_worst_subsystem_id;
                p->rsc_sw.conformance_last_tick100ns = conformance.last_tick100ns;

                ULONG row_cnt = conformance.row_cnt;
                if (row_cnt > RTL_NUMBER_OF(p->rsc_sw.conformance_rows))
                    row_cnt = RTL_NUMBER_OF(p->rsc_sw.conformance_rows);
                p->rsc_sw.conformance_row_entry_cnt = row_cnt;
                for (ULONG i = 0; i < row_cnt; i++) {
                    p->rsc_sw.conformance_rows[i].subsystem_id = conformance.rows[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.conformance_rows[i].name, conformance.rows[i].name,
                                  sizeof(p->rsc_sw.conformance_rows[i].name));
                    p->rsc_sw.conformance_rows[i].driver_map_id = conformance.rows[i].driver_map_id;
                    p->rsc_sw.conformance_rows[i].ppp_res_type = conformance.rows[i].ppp_res_type;
                    p->rsc_sw.conformance_rows[i].glink_event_seed =
                        conformance.rows[i].glink_event_seed;
                    p->rsc_sw.conformance_rows[i].cap_mask = conformance.rows[i].cap_mask;
                    p->rsc_sw.conformance_rows[i].res_tag = conformance.rows[i].res_tag;
                    p->rsc_sw.conformance_rows[i].suggested_ioctl =
                        conformance.rows[i].suggested_ioctl;
                    p->rsc_sw.conformance_rows[i].dependency_in = conformance.rows[i].dependency_in;
                    p->rsc_sw.conformance_rows[i].dependency_out =
                        conformance.rows[i].dependency_out;
                    p->rsc_sw.conformance_rows[i].active = conformance.rows[i].active;
                    p->rsc_sw.conformance_rows[i].success_permille =
                        conformance.rows[i].success_permille;
                    p->rsc_sw.conformance_rows[i].activity_score =
                        conformance.rows[i].activity_score;
                    p->rsc_sw.conformance_rows[i].conformance_score_permille =
                        conformance.rows[i].conformance_score_permille;
                    p->rsc_sw.conformance_rows[i].reason_flags = conformance.rows[i].reason_flags;
                    p->rsc_sw.conformance_rows[i].last_status = conformance.rows[i].last_status;
                    p->rsc_sw.conformance_rows[i].last_tick100ns =
                        conformance.rows[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_conformance_result_t conformance_history[WMI_RSCSW_CONFORMANCE_HISTORY_CNT];
            ULONG conformance_history_cnt = 0;
            ULONG conformance_history_dropped = 0;
            RtlZeroMemory(conformance_history, sizeof(conformance_history));
            if (NT_SUCCESS(rsc_sw_get_conformance_history(
                    conformance_history, RTL_NUMBER_OF(conformance_history),
                    &conformance_history_cnt, &conformance_history_dropped))) {
                p->rsc_sw.conformance_history_entry_cnt = conformance_history_cnt;
                p->rsc_sw.conformance_history_dropped = conformance_history_dropped;
                for (ULONG i = 0; i < conformance_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.conformance_history);
                     i++) {
                    p->rsc_sw.conformance_history[i].flags = conformance_history[i].flags;
                    p->rsc_sw.conformance_history[i].status = conformance_history[i].status;
                    p->rsc_sw.conformance_history[i].node_cnt = conformance_history[i].node_cnt;
                    p->rsc_sw.conformance_history[i].passing_cnt =
                        conformance_history[i].passing_cnt;
                    p->rsc_sw.conformance_history[i].warning_cnt =
                        conformance_history[i].warning_cnt;
                    p->rsc_sw.conformance_history[i].error_cnt = conformance_history[i].error_cnt;
                    p->rsc_sw.conformance_history[i].overall_score_permille =
                        conformance_history[i].overall_score_permille;
                    p->rsc_sw.conformance_history[i].threshold_permille =
                        conformance_history[i].threshold_permille;
                    p->rsc_sw.conformance_history[i].worst_subsystem_id =
                        conformance_history[i].worst_subsystem_id;
                    p->rsc_sw.conformance_history[i].last_subsystem_id =
                        conformance_history[i].last_subsystem_id;
                    p->rsc_sw.conformance_history[i].last_tag = conformance_history[i].last_tag;
                    p->rsc_sw.conformance_history[i].last_value0 =
                        conformance_history[i].last_value0;
                    p->rsc_sw.conformance_history[i].last_value1 =
                        conformance_history[i].last_value1;
                    p->rsc_sw.conformance_history[i].last_value2 =
                        conformance_history[i].last_value2;
                    p->rsc_sw.conformance_history[i].last_value3 =
                        conformance_history[i].last_value3;
                    p->rsc_sw.conformance_history[i].tick100ns = conformance_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_orchestration_snapshot_t orchestration;
            RtlZeroMemory(&orchestration, sizeof(orchestration));
            if (NT_SUCCESS(rsc_sw_get_orchestration_snapshot(&orchestration))) {
                p->rsc_sw.orchestration_run_cnt = orchestration.run_cnt;
                p->rsc_sw.orchestration_failure_cnt = orchestration.failure_cnt;
                p->rsc_sw.orchestration_last_status = orchestration.last_status;
                p->rsc_sw.orchestration_last_flags = orchestration.last_flags;
                p->rsc_sw.orchestration_last_overall_score_permille =
                    orchestration.last_overall_score_permille;
                p->rsc_sw.orchestration_last_stage_cnt = orchestration.last_stage_cnt;
                p->rsc_sw.orchestration_last_successful_stages =
                    orchestration.last_successful_stages;
                p->rsc_sw.orchestration_last_warning_stages = orchestration.last_warning_stages;
                p->rsc_sw.orchestration_last_failed_stages = orchestration.last_failed_stages;
                p->rsc_sw.orchestration_last_stage_id = orchestration.last_stage_id;
                p->rsc_sw.orchestration_last_tick100ns = orchestration.last_tick100ns;

                ULONG stage_cnt = orchestration.stage_cnt;
                if (stage_cnt > RTL_NUMBER_OF(p->rsc_sw.orchestration_stages))
                    stage_cnt = RTL_NUMBER_OF(p->rsc_sw.orchestration_stages);
                p->rsc_sw.orchestration_stage_entry_cnt = stage_cnt;
                for (ULONG i = 0; i < stage_cnt; i++) {
                    p->rsc_sw.orchestration_stages[i].stage_id = orchestration.stages[i].stage_id;
                    RtlCopyMemory(p->rsc_sw.orchestration_stages[i].stage_name,
                                  orchestration.stages[i].stage_name,
                                  sizeof(p->rsc_sw.orchestration_stages[i].stage_name));
                    p->rsc_sw.orchestration_stages[i].enabled = orchestration.stages[i].enabled;
                    p->rsc_sw.orchestration_stages[i].status = orchestration.stages[i].status;
                    p->rsc_sw.orchestration_stages[i].warning_cnt =
                        orchestration.stages[i].warning_cnt;
                    p->rsc_sw.orchestration_stages[i].error_cnt = orchestration.stages[i].error_cnt;
                    p->rsc_sw.orchestration_stages[i].score_permille =
                        orchestration.stages[i].score_permille;
                    p->rsc_sw.orchestration_stages[i].detail0 = orchestration.stages[i].detail0;
                    p->rsc_sw.orchestration_stages[i].detail1 = orchestration.stages[i].detail1;
                    p->rsc_sw.orchestration_stages[i].detail2 = orchestration.stages[i].detail2;
                    p->rsc_sw.orchestration_stages[i].detail3 = orchestration.stages[i].detail3;
                    p->rsc_sw.orchestration_stages[i].tick100ns = orchestration.stages[i].tick100ns;
                }
            }
        }

        {
            rscsw_orchestration_result_t orchestration_history[WMI_RSCSW_ORCHESTRATION_HISTORY_CNT];
            ULONG orchestration_history_cnt = 0;
            ULONG orchestration_history_dropped = 0;
            RtlZeroMemory(orchestration_history, sizeof(orchestration_history));
            if (NT_SUCCESS(rsc_sw_get_orchestration_history(
                    orchestration_history, RTL_NUMBER_OF(orchestration_history),
                    &orchestration_history_cnt, &orchestration_history_dropped))) {
                p->rsc_sw.orchestration_history_entry_cnt = orchestration_history_cnt;
                p->rsc_sw.orchestration_history_dropped = orchestration_history_dropped;
                for (ULONG i = 0; i < orchestration_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.orchestration_history);
                     i++) {
                    p->rsc_sw.orchestration_history[i].flags = orchestration_history[i].flags;
                    p->rsc_sw.orchestration_history[i].status = orchestration_history[i].status;
                    p->rsc_sw.orchestration_history[i].stage_cnt =
                        orchestration_history[i].stage_cnt;
                    p->rsc_sw.orchestration_history[i].successful_stages =
                        orchestration_history[i].successful_stages;
                    p->rsc_sw.orchestration_history[i].warning_stages =
                        orchestration_history[i].warning_stages;
                    p->rsc_sw.orchestration_history[i].failed_stages =
                        orchestration_history[i].failed_stages;
                    p->rsc_sw.orchestration_history[i].overall_score_permille =
                        orchestration_history[i].overall_score_permille;
                    p->rsc_sw.orchestration_history[i].last_stage_id =
                        orchestration_history[i].last_stage_id;
                    p->rsc_sw.orchestration_history[i].last_subsystem_id =
                        orchestration_history[i].last_subsystem_id;
                    p->rsc_sw.orchestration_history[i].last_tag = orchestration_history[i].last_tag;
                    p->rsc_sw.orchestration_history[i].last_value0 =
                        orchestration_history[i].last_value0;
                    p->rsc_sw.orchestration_history[i].last_value1 =
                        orchestration_history[i].last_value1;
                    p->rsc_sw.orchestration_history[i].last_value2 =
                        orchestration_history[i].last_value2;
                    p->rsc_sw.orchestration_history[i].last_value3 =
                        orchestration_history[i].last_value3;
                    p->rsc_sw.orchestration_history[i].tick100ns =
                        orchestration_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_policy_snapshot_t policy;
            RtlZeroMemory(&policy, sizeof(policy));
            if (NT_SUCCESS(rsc_sw_get_policy_snapshot(&policy))) {
                p->rsc_sw.policy_res_cnt = policy.res_cnt;
                p->rsc_sw.policy_active_res_cnt = policy.active_res_cnt;
                p->rsc_sw.policy_event_sequence = policy.event_sequence;
                p->rsc_sw.policy_vote_cnt = policy.vote_cnt;
                p->rsc_sw.policy_remove_cnt = policy.remove_cnt;
                p->rsc_sw.policy_propagation_cnt = policy.propagation_cnt;
                p->rsc_sw.policy_last_res_id = policy.last_res_id;
                p->rsc_sw.policy_last_subsystem_id = policy.last_subsystem_id;
                p->rsc_sw.policy_last_parent_subsystem_id = policy.last_parent_subsystem_id;
                p->rsc_sw.policy_last_ioctl_code = policy.last_ioctl_code;
                p->rsc_sw.policy_last_status = policy.last_status;
                p->rsc_sw.policy_last_flags = policy.last_flags;
                p->rsc_sw.policy_last_tick100ns = policy.last_tick100ns;

                ULONG policy_res_cnt = policy.res_entry_cnt;
                if (policy_res_cnt > RTL_NUMBER_OF(p->rsc_sw.policy_resources))
                    policy_res_cnt = RTL_NUMBER_OF(p->rsc_sw.policy_resources);
                p->rsc_sw.policy_res_entry_cnt = policy_res_cnt;
                for (ULONG i = 0; i < policy_res_cnt; i++) {
                    p->rsc_sw.policy_resources[i].res_id = policy.resources[i].res_id;
                    p->rsc_sw.policy_resources[i].parent_res_id = policy.resources[i].parent_res_id;
                    copy_name_a(p->rsc_sw.policy_resources[i].res_name,
                                sizeof(p->rsc_sw.policy_resources[i].res_name),
                                rsc_sw_get_policy_res_name(policy.resources[i].res_id));
                    copy_name_a(p->rsc_sw.policy_resources[i].parent_res_name,
                                sizeof(p->rsc_sw.policy_resources[i].parent_res_name),
                                rsc_sw_get_policy_res_name(policy.resources[i].parent_res_id));
                    p->rsc_sw.policy_resources[i].subsystem_id = policy.resources[i].subsystem_id;
                    copy_name_a(p->rsc_sw.policy_resources[i].subsystem_name,
                                sizeof(p->rsc_sw.policy_resources[i].subsystem_name),
                                rsc_sw_get_subsystem_name_a(policy.resources[i].subsystem_id));
                    p->rsc_sw.policy_resources[i].parent_subsystem_id =
                        policy.resources[i].parent_subsystem_id;
                    copy_name_a(
                        p->rsc_sw.policy_resources[i].parent_subsystem_name,
                        sizeof(p->rsc_sw.policy_resources[i].parent_subsystem_name),
                        rsc_sw_get_subsystem_name_a(policy.resources[i].parent_subsystem_id));
                    p->rsc_sw.policy_resources[i].res_type = policy.resources[i].res_type;
                    p->rsc_sw.policy_resources[i].active_vote_cnt =
                        policy.resources[i].active_vote_cnt;
                    p->rsc_sw.policy_resources[i].set_mask = policy.resources[i].set_mask;
                    p->rsc_sw.policy_resources[i].driver_map_id = policy.resources[i].driver_map_id;
                    p->rsc_sw.policy_resources[i].flags = policy.resources[i].flags;
                    p->rsc_sw.policy_resources[i].last_status = policy.resources[i].last_status;
                    p->rsc_sw.policy_resources[i].last_tag = policy.resources[i].last_tag;
                    p->rsc_sw.policy_resources[i].last_value0 = policy.resources[i].last_value0;
                    p->rsc_sw.policy_resources[i].last_value1 = policy.resources[i].last_value1;
                    p->rsc_sw.policy_resources[i].last_value2 = policy.resources[i].last_value2;
                    p->rsc_sw.policy_resources[i].last_value3 = policy.resources[i].last_value3;
                    p->rsc_sw.policy_resources[i].last_tick100ns =
                        policy.resources[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_policy_event_t policy_history[WMI_RSCSW_POLICY_HISTORY_CNT];
            ULONG policy_history_cnt = 0;
            ULONG policy_history_dropped = 0;
            RtlZeroMemory(policy_history, sizeof(policy_history));
            if (NT_SUCCESS(rsc_sw_get_policy_history(policy_history, RTL_NUMBER_OF(policy_history),
                                                     &policy_history_cnt,
                                                     &policy_history_dropped))) {
                p->rsc_sw.policy_history_entry_cnt = policy_history_cnt;
                p->rsc_sw.policy_history_dropped = policy_history_dropped;
                for (ULONG i = 0;
                     i < policy_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.policy_history); i++) {
                    p->rsc_sw.policy_history[i].sequence = policy_history[i].sequence;
                    p->rsc_sw.policy_history[i].tick100ns = policy_history[i].tick100ns;
                    p->rsc_sw.policy_history[i].flags = policy_history[i].flags;
                    p->rsc_sw.policy_history[i].status = policy_history[i].status;
                    p->rsc_sw.policy_history[i].res_id = policy_history[i].res_id;
                    p->rsc_sw.policy_history[i].parent_res_id = policy_history[i].parent_res_id;
                    copy_name_a(p->rsc_sw.policy_history[i].res_name,
                                sizeof(p->rsc_sw.policy_history[i].res_name),
                                rsc_sw_get_policy_res_name(policy_history[i].res_id));
                    copy_name_a(p->rsc_sw.policy_history[i].parent_res_name,
                                sizeof(p->rsc_sw.policy_history[i].parent_res_name),
                                rsc_sw_get_policy_res_name(policy_history[i].parent_res_id));
                    p->rsc_sw.policy_history[i].subsystem_id = policy_history[i].subsystem_id;
                    copy_name_a(p->rsc_sw.policy_history[i].subsystem_name,
                                sizeof(p->rsc_sw.policy_history[i].subsystem_name),
                                rsc_sw_get_subsystem_name_a(policy_history[i].subsystem_id));
                    p->rsc_sw.policy_history[i].parent_subsystem_id =
                        policy_history[i].parent_subsystem_id;
                    copy_name_a(p->rsc_sw.policy_history[i].parent_subsystem_name,
                                sizeof(p->rsc_sw.policy_history[i].parent_subsystem_name),
                                rsc_sw_get_subsystem_name_a(policy_history[i].parent_subsystem_id));
                    p->rsc_sw.policy_history[i].res_type = policy_history[i].res_type;
                    p->rsc_sw.policy_history[i].active_vote_cnt = policy_history[i].active_vote_cnt;
                    p->rsc_sw.policy_history[i].set_mask = policy_history[i].set_mask;
                    p->rsc_sw.policy_history[i].driver_map_id = policy_history[i].driver_map_id;
                    p->rsc_sw.policy_history[i].tag = policy_history[i].tag;
                    p->rsc_sw.policy_history[i].value0 = policy_history[i].value0;
                    p->rsc_sw.policy_history[i].value1 = policy_history[i].value1;
                    p->rsc_sw.policy_history[i].value2 = policy_history[i].value2;
                    p->rsc_sw.policy_history[i].value3 = policy_history[i].value3;
                    p->rsc_sw.policy_history[i].ioctl_code = policy_history[i].ioctl_code;
                }
            }
        }

        {
            rscsw_fabric_snapshot_t fabric;
            RtlZeroMemory(&fabric, sizeof(fabric));
            if (NT_SUCCESS(rsc_sw_get_fabric_snapshot(&fabric))) {
                p->rsc_sw.fabric_run_cnt = fabric.run_cnt;
                p->rsc_sw.fabric_failure_cnt = fabric.failure_cnt;
                p->rsc_sw.fabric_last_status = fabric.last_status;
                p->rsc_sw.fabric_last_flags = fabric.last_flags;
                p->rsc_sw.fabric_last_lane_cnt = fabric.last_lane_cnt;
                p->rsc_sw.fabric_last_active_lane_cnt = fabric.last_active_lane_cnt;
                p->rsc_sw.fabric_last_policy_active_lane_cnt = fabric.last_policy_active_lane_cnt;
                p->rsc_sw.fabric_last_warning_cnt = fabric.last_warning_cnt;
                p->rsc_sw.fabric_last_error_cnt = fabric.last_error_cnt;
                p->rsc_sw.fabric_last_overall_health_score = fabric.last_overall_health_score;
                p->rsc_sw.fabric_last_overall_activity_score = fabric.last_overall_activity_score;
                p->rsc_sw.fabric_last_thermal_max_deci_c = fabric.last_thermal_max_deci_c;
                p->rsc_sw.fabric_last_thermal_min_deci_c = fabric.last_thermal_min_deci_c;
                p->rsc_sw.fabric_last_tick100ns = fabric.last_tick100ns;

                ULONG lane_cnt = fabric.lane_cnt;
                if (lane_cnt > RTL_NUMBER_OF(p->rsc_sw.fabric_lanes))
                    lane_cnt = RTL_NUMBER_OF(p->rsc_sw.fabric_lanes);
                p->rsc_sw.fabric_lane_entry_cnt = lane_cnt;
                for (ULONG i = 0; i < lane_cnt; i++) {
                    p->rsc_sw.fabric_lanes[i].subsystem_id = fabric.lanes[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.fabric_lanes[i].name, fabric.lanes[i].name,
                                  sizeof(p->rsc_sw.fabric_lanes[i].name));
                    p->rsc_sw.fabric_lanes[i].present = fabric.lanes[i].present;
                    p->rsc_sw.fabric_lanes[i].active = fabric.lanes[i].active;
                    p->rsc_sw.fabric_lanes[i].state = fabric.lanes[i].state;
                    p->rsc_sw.fabric_lanes[i].clock_khz = fabric.lanes[i].clock_khz;
                    p->rsc_sw.fabric_lanes[i].driver_map_id = fabric.lanes[i].driver_map_id;
                    p->rsc_sw.fabric_lanes[i].ppp_res_type = fabric.lanes[i].ppp_res_type;
                    p->rsc_sw.fabric_lanes[i].policy_res_cnt = fabric.lanes[i].policy_res_cnt;
                    p->rsc_sw.fabric_lanes[i].policy_active_vote_cnt =
                        fabric.lanes[i].policy_active_vote_cnt;
                    p->rsc_sw.fabric_lanes[i].dependency_in = fabric.lanes[i].dependency_in;
                    p->rsc_sw.fabric_lanes[i].dependency_out = fabric.lanes[i].dependency_out;
                    p->rsc_sw.fabric_lanes[i].req_cnt = fabric.lanes[i].req_cnt;
                    p->rsc_sw.fabric_lanes[i].success_cnt = fabric.lanes[i].success_cnt;
                    p->rsc_sw.fabric_lanes[i].failure_cnt = fabric.lanes[i].failure_cnt;
                    p->rsc_sw.fabric_lanes[i].success_permille = fabric.lanes[i].success_permille;
                    p->rsc_sw.fabric_lanes[i].activity_score = fabric.lanes[i].activity_score;
                    p->rsc_sw.fabric_lanes[i].health_score = fabric.lanes[i].health_score;
                    p->rsc_sw.fabric_lanes[i].thermal_deci_c = fabric.lanes[i].thermal_deci_c;
                    p->rsc_sw.fabric_lanes[i].flags = fabric.lanes[i].flags;
                    p->rsc_sw.fabric_lanes[i].last_status = fabric.lanes[i].last_status;
                    p->rsc_sw.fabric_lanes[i].last_tick100ns = fabric.lanes[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_fabric_result_t fabric_history[WMI_RSCSW_FABRIC_HISTORY_CNT];
            ULONG fabric_history_cnt = 0;
            ULONG fabric_history_dropped = 0;
            RtlZeroMemory(fabric_history, sizeof(fabric_history));
            if (NT_SUCCESS(rsc_sw_get_fabric_history(fabric_history, RTL_NUMBER_OF(fabric_history),
                                                     &fabric_history_cnt,
                                                     &fabric_history_dropped))) {
                p->rsc_sw.fabric_history_entry_cnt = fabric_history_cnt;
                p->rsc_sw.fabric_history_dropped = fabric_history_dropped;
                for (ULONG i = 0;
                     i < fabric_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.fabric_history); i++) {
                    p->rsc_sw.fabric_history[i].flags = fabric_history[i].flags;
                    p->rsc_sw.fabric_history[i].status = fabric_history[i].status;
                    p->rsc_sw.fabric_history[i].lane_cnt = fabric_history[i].lane_cnt;
                    p->rsc_sw.fabric_history[i].active_lane_cnt = fabric_history[i].active_lane_cnt;
                    p->rsc_sw.fabric_history[i].policy_active_lane_cnt =
                        fabric_history[i].policy_active_lane_cnt;
                    p->rsc_sw.fabric_history[i].warning_cnt = fabric_history[i].warning_cnt;
                    p->rsc_sw.fabric_history[i].error_cnt = fabric_history[i].error_cnt;
                    p->rsc_sw.fabric_history[i].overall_health_score =
                        fabric_history[i].overall_health_score;
                    p->rsc_sw.fabric_history[i].overall_activity_score =
                        fabric_history[i].overall_activity_score;
                    p->rsc_sw.fabric_history[i].thermal_max_deci_c =
                        fabric_history[i].thermal_max_deci_c;
                    p->rsc_sw.fabric_history[i].thermal_min_deci_c =
                        fabric_history[i].thermal_min_deci_c;
                    p->rsc_sw.fabric_history[i].last_subsystem_id =
                        fabric_history[i].last_subsystem_id;
                    p->rsc_sw.fabric_history[i].last_tag = fabric_history[i].last_tag;
                    p->rsc_sw.fabric_history[i].last_value0 = fabric_history[i].last_value0;
                    p->rsc_sw.fabric_history[i].last_value1 = fabric_history[i].last_value1;
                    p->rsc_sw.fabric_history[i].last_value2 = fabric_history[i].last_value2;
                    p->rsc_sw.fabric_history[i].last_value3 = fabric_history[i].last_value3;
                    p->rsc_sw.fabric_history[i].tick100ns = fabric_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_domain_snapshot_t domain;
            RtlZeroMemory(&domain, sizeof(domain));
            if (NT_SUCCESS(rsc_sw_get_domain_snapshot(&domain))) {
                p->rsc_sw.domain_run_cnt = domain.run_cnt;
                p->rsc_sw.domain_failure_cnt = domain.failure_cnt;
                p->rsc_sw.domain_last_status = domain.last_status;
                p->rsc_sw.domain_last_flags = domain.last_flags;
                p->rsc_sw.domain_last_domain_cnt = domain.last_domain_cnt;
                p->rsc_sw.domain_last_healthy_domain_cnt = domain.last_healthy_domain_cnt;
                p->rsc_sw.domain_last_warning_cnt = domain.last_warning_cnt;
                p->rsc_sw.domain_last_error_cnt = domain.last_error_cnt;
                p->rsc_sw.domain_last_overall_health_score = domain.last_overall_health_score;
                p->rsc_sw.domain_last_overall_activity_score = domain.last_overall_activity_score;
                p->rsc_sw.domain_last_tick100ns = domain.last_tick100ns;

                ULONG domain_cnt = domain.domain_cnt;
                if (domain_cnt > RTL_NUMBER_OF(p->rsc_sw.domains))
                    domain_cnt = RTL_NUMBER_OF(p->rsc_sw.domains);
                p->rsc_sw.domain_entry_cnt = domain_cnt;
                for (ULONG i = 0; i < domain_cnt; i++) {
                    p->rsc_sw.domains[i].domain_id = domain.domains[i].domain_id;
                    RtlCopyMemory(p->rsc_sw.domains[i].name, domain.domains[i].name,
                                  sizeof(p->rsc_sw.domains[i].name));
                    p->rsc_sw.domains[i].subsystem_mask = domain.domains[i].subsystem_mask;
                    p->rsc_sw.domains[i].lane_cnt = domain.domains[i].lane_cnt;
                    p->rsc_sw.domains[i].active_lane_cnt = domain.domains[i].active_lane_cnt;
                    p->rsc_sw.domains[i].policy_active_lane_cnt =
                        domain.domains[i].policy_active_lane_cnt;
                    p->rsc_sw.domains[i].warning_cnt = domain.domains[i].warning_cnt;
                    p->rsc_sw.domains[i].error_cnt = domain.domains[i].error_cnt;
                    p->rsc_sw.domains[i].avg_health_score = domain.domains[i].avg_health_score;
                    p->rsc_sw.domains[i].avg_activity_score = domain.domains[i].avg_activity_score;
                    p->rsc_sw.domains[i].thermal_max_deci_c = domain.domains[i].thermal_max_deci_c;
                    p->rsc_sw.domains[i].thermal_min_deci_c = domain.domains[i].thermal_min_deci_c;
                    p->rsc_sw.domains[i].last_subsystem_id = domain.domains[i].last_subsystem_id;
                    p->rsc_sw.domains[i].last_status = domain.domains[i].last_status;
                    p->rsc_sw.domains[i].flags = domain.domains[i].flags;
                    p->rsc_sw.domains[i].last_tick100ns = domain.domains[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_domain_result_t domain_history[WMI_RSCSW_DOMAIN_HISTORY_CNT];
            ULONG domain_history_cnt = 0;
            ULONG domain_history_dropped = 0;
            RtlZeroMemory(domain_history, sizeof(domain_history));
            if (NT_SUCCESS(rsc_sw_get_domain_history(domain_history, RTL_NUMBER_OF(domain_history),
                                                     &domain_history_cnt,
                                                     &domain_history_dropped))) {
                p->rsc_sw.domain_history_entry_cnt = domain_history_cnt;
                p->rsc_sw.domain_history_dropped = domain_history_dropped;
                for (ULONG i = 0;
                     i < domain_history_cnt && i < RTL_NUMBER_OF(p->rsc_sw.domain_history); i++) {
                    p->rsc_sw.domain_history[i].flags = domain_history[i].flags;
                    p->rsc_sw.domain_history[i].status = domain_history[i].status;
                    p->rsc_sw.domain_history[i].domain_cnt = domain_history[i].domain_cnt;
                    p->rsc_sw.domain_history[i].healthy_domain_cnt =
                        domain_history[i].healthy_domain_cnt;
                    p->rsc_sw.domain_history[i].warning_cnt = domain_history[i].warning_cnt;
                    p->rsc_sw.domain_history[i].error_cnt = domain_history[i].error_cnt;
                    p->rsc_sw.domain_history[i].overall_health_score =
                        domain_history[i].overall_health_score;
                    p->rsc_sw.domain_history[i].overall_activity_score =
                        domain_history[i].overall_activity_score;
                    p->rsc_sw.domain_history[i].last_domain_id = domain_history[i].last_domain_id;
                    p->rsc_sw.domain_history[i].last_subsystem_id =
                        domain_history[i].last_subsystem_id;
                    p->rsc_sw.domain_history[i].last_tag = domain_history[i].last_tag;
                    p->rsc_sw.domain_history[i].last_value0 = domain_history[i].last_value0;
                    p->rsc_sw.domain_history[i].last_value1 = domain_history[i].last_value1;
                    p->rsc_sw.domain_history[i].last_value2 = domain_history[i].last_value2;
                    p->rsc_sw.domain_history[i].last_value3 = domain_history[i].last_value3;
                    p->rsc_sw.domain_history[i].tick100ns = domain_history[i].tick100ns;
                }
            }
        }

        {
            rscsw_interconnect_snapshot_t interconnect;
            RtlZeroMemory(&interconnect, sizeof(interconnect));
            if (NT_SUCCESS(rsc_sw_get_interconnect_snapshot(&interconnect))) {
                p->rsc_sw.interconnect_run_cnt = interconnect.run_cnt;
                p->rsc_sw.interconnect_failure_cnt = interconnect.failure_cnt;
                p->rsc_sw.interconnect_last_status = interconnect.last_status;
                p->rsc_sw.interconnect_last_flags = interconnect.last_flags;
                p->rsc_sw.interconnect_last_route_cnt = interconnect.last_route_cnt;
                p->rsc_sw.interconnect_last_active_route_cnt = interconnect.last_active_route_cnt;
                p->rsc_sw.interconnect_last_policy_linked_route_cnt =
                    interconnect.last_policy_linked_route_cnt;
                p->rsc_sw.interconnect_last_warning_cnt = interconnect.last_warning_cnt;
                p->rsc_sw.interconnect_last_error_cnt = interconnect.last_error_cnt;
                p->rsc_sw.interconnect_last_avg_utilization_permille =
                    interconnect.last_avg_utilization_permille;
                p->rsc_sw.interconnect_last_peak_utilization_permille =
                    interconnect.last_peak_utilization_permille;
                p->rsc_sw.interconnect_last_total_required_bandwidth_mb =
                    interconnect.last_total_required_bandwidth_mb;
                p->rsc_sw.interconnect_last_total_granted_bandwidth_mb =
                    interconnect.last_total_granted_bandwidth_mb;
                p->rsc_sw.interconnect_last_tick100ns = interconnect.last_tick100ns;

                ULONG route_cnt = interconnect.route_cnt;
                if (route_cnt > RTL_NUMBER_OF(p->rsc_sw.interconnect_routes))
                    route_cnt = RTL_NUMBER_OF(p->rsc_sw.interconnect_routes);
                p->rsc_sw.interconnect_route_entry_cnt = route_cnt;
                for (ULONG i = 0; i < route_cnt; i++) {
                    p->rsc_sw.interconnect_routes[i].route_id = interconnect.routes[i].route_id;
                    RtlCopyMemory(p->rsc_sw.interconnect_routes[i].name,
                                  interconnect.routes[i].name,
                                  sizeof(p->rsc_sw.interconnect_routes[i].name));
                    p->rsc_sw.interconnect_routes[i].subsystem_id =
                        interconnect.routes[i].subsystem_id;
                    RtlCopyMemory(p->rsc_sw.interconnect_routes[i].subsystem_name,
                                  interconnect.routes[i].subsystem_name,
                                  sizeof(p->rsc_sw.interconnect_routes[i].subsystem_name));
                    p->rsc_sw.interconnect_routes[i].master_id = interconnect.routes[i].master_id;
                    RtlCopyMemory(p->rsc_sw.interconnect_routes[i].master_name,
                                  interconnect.routes[i].master_name,
                                  sizeof(p->rsc_sw.interconnect_routes[i].master_name));
                    p->rsc_sw.interconnect_routes[i].slave_id = interconnect.routes[i].slave_id;
                    RtlCopyMemory(p->rsc_sw.interconnect_routes[i].slave_name,
                                  interconnect.routes[i].slave_name,
                                  sizeof(p->rsc_sw.interconnect_routes[i].slave_name));
                    p->rsc_sw.interconnect_routes[i].active = interconnect.routes[i].active;
                    p->rsc_sw.interconnect_routes[i].policy_linked =
                        interconnect.routes[i].policy_linked;
                    p->rsc_sw.interconnect_routes[i].required_bandwidth_mb =
                        interconnect.routes[i].required_bandwidth_mb;
                    p->rsc_sw.interconnect_routes[i].granted_bandwidth_mb =
                        interconnect.routes[i].granted_bandwidth_mb;
                    p->rsc_sw.interconnect_routes[i].utilization_permille =
                        interconnect.routes[i].utilization_permille;
                    p->rsc_sw.interconnect_routes[i].vote_cnt = interconnect.routes[i].vote_cnt;
                    p->rsc_sw.interconnect_routes[i].dependency_in =
                        interconnect.routes[i].dependency_in;
                    p->rsc_sw.interconnect_routes[i].dependency_out =
                        interconnect.routes[i].dependency_out;
                    p->rsc_sw.interconnect_routes[i].thermal_deci_c =
                        interconnect.routes[i].thermal_deci_c;
                    p->rsc_sw.interconnect_routes[i].flags = interconnect.routes[i].flags;
                    p->rsc_sw.interconnect_routes[i].last_status =
                        interconnect.routes[i].last_status;
                    p->rsc_sw.interconnect_routes[i].last_tick100ns =
                        interconnect.routes[i].last_tick100ns;
                }
            }
        }

        {
            rscsw_interconnect_result_t interconnect_history[WMI_RSCSW_INTERCONNECT_HISTORY_CNT];
            ULONG interconnect_history_cnt = 0;
            ULONG interconnect_history_dropped = 0;
            RtlZeroMemory(interconnect_history, sizeof(interconnect_history));
            if (NT_SUCCESS(rsc_sw_get_interconnect_history(
                    interconnect_history, RTL_NUMBER_OF(interconnect_history),
                    &interconnect_history_cnt, &interconnect_history_dropped))) {
                p->rsc_sw.interconnect_history_entry_cnt = interconnect_history_cnt;
                p->rsc_sw.interconnect_history_dropped = interconnect_history_dropped;
                for (ULONG i = 0; i < interconnect_history_cnt &&
                                  i < RTL_NUMBER_OF(p->rsc_sw.interconnect_history);
                     i++) {
                    p->rsc_sw.interconnect_history[i].flags = interconnect_history[i].flags;
                    p->rsc_sw.interconnect_history[i].status = interconnect_history[i].status;
                    p->rsc_sw.interconnect_history[i].route_cnt = interconnect_history[i].route_cnt;
                    p->rsc_sw.interconnect_history[i].active_route_cnt =
                        interconnect_history[i].active_route_cnt;
                    p->rsc_sw.interconnect_history[i].policy_linked_route_cnt =
                        interconnect_history[i].policy_linked_route_cnt;
                    p->rsc_sw.interconnect_history[i].warning_cnt =
                        interconnect_history[i].warning_cnt;
                    p->rsc_sw.interconnect_history[i].error_cnt = interconnect_history[i].error_cnt;
                    p->rsc_sw.interconnect_history[i].avg_utilization_permille =
                        interconnect_history[i].avg_utilization_permille;
                    p->rsc_sw.interconnect_history[i].peak_utilization_permille =
                        interconnect_history[i].peak_utilization_permille;
                    p->rsc_sw.interconnect_history[i].total_required_bandwidth_mb =
                        interconnect_history[i].total_required_bandwidth_mb;
                    p->rsc_sw.interconnect_history[i].total_granted_bandwidth_mb =
                        interconnect_history[i].total_granted_bandwidth_mb;
                    p->rsc_sw.interconnect_history[i].last_route_id =
                        interconnect_history[i].last_route_id;
                    p->rsc_sw.interconnect_history[i].last_subsystem_id =
                        interconnect_history[i].last_subsystem_id;
                    p->rsc_sw.interconnect_history[i].last_tag = interconnect_history[i].last_tag;
                    p->rsc_sw.interconnect_history[i].last_value0 =
                        interconnect_history[i].last_value0;
                    p->rsc_sw.interconnect_history[i].last_value1 =
                        interconnect_history[i].last_value1;
                    p->rsc_sw.interconnect_history[i].last_value2 =
                        interconnect_history[i].last_value2;
                    p->rsc_sw.interconnect_history[i].last_value3 =
                        interconnect_history[i].last_value3;
                    p->rsc_sw.interconnect_history[i].tick100ns = interconnect_history[i].tick100ns;
                }
            }
        }
    }

    p->stats.ioctl_cnt = (ULONG)g_stats.ioctl_cnt;
    p->stats.po_fx_power_required = (ULONG)g_stats.po_fx_power_required;
    p->stats.po_fx_power_not_required = (ULONG)g_stats.po_fx_power_not_required;
    p->stats.po_fx_comp_active = (ULONG)g_stats.po_fx_comp_active;
    p->stats.po_fx_comp_idle = (ULONG)g_stats.po_fx_comp_idle;
    p->stats.po_fx_idle_state = (ULONG)g_stats.po_fx_idle_state;
    p->stats.d0_entry = (ULONG)g_stats.d0_entry;
    p->stats.d0_exit = (ULONG)g_stats.d0_exit;

    {
        stats_snapshot_t st;
        RtlZeroMemory(&st, sizeof(st));
        if (NT_SUCCESS(stats_get_snapshot(&st))) {
            p->stats.dev_attached = st.dev_attached;
            p->stats.event_cnt = st.event_cnt;
            p->stats.error_cnt = st.error_cnt;
            p->stats.ioctl_error_cnt = st.ioctl_error_cnt;
            p->stats.history_cnt = st.history_cnt;
            p->stats.history_dropped = st.history_dropped;
            p->stats.last_event_code = st.last_event_code;
            p->stats.last_event_status = st.last_event_status;
            p->stats.last_event_arg0 = st.last_event_arg0;
            p->stats.last_event_arg1 = st.last_event_arg1;
            p->stats.last_event_arg2 = st.last_event_arg2;
            p->stats.last_event_arg3 = st.last_event_arg3;
            p->stats.last_event_tick100ns = st.last_event_tick100ns;
            p->stats.last_ioctl_code = st.last_ioctl_code;
            p->stats.last_ioctl_status = st.last_ioctl_status;
            p->stats.last_ioctl_in_len = st.last_ioctl_in_len;
            p->stats.last_ioctl_out_len = st.last_ioctl_out_len;
        }

        stats_event_t history[WMI_STATS_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                stats_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->stats.history_entry_cnt = hist_cnt;
            p->stats.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->stats.history); i++) {
                p->stats.history[i].event_code = history[i].event_code;
                p->stats.history[i].status = history[i].status;
                p->stats.history[i].arg0 = history[i].arg0;
                p->stats.history[i].arg1 = history[i].arg1;
                p->stats.history[i].arg2 = history[i].arg2;
                p->stats.history[i].arg3 = history[i].arg3;
                p->stats.history[i].tick100ns = history[i].tick100ns;
            }
        }
    }

    {
        chipinfo_t ci = chip_info_get();
        p->chip.chip_id = ci.chip_id;
        p->chip.chip_family = ci.chip_family;
        p->chip.chip_version = ci.chip_version;
        p->chip.chip_sku = ci.chip_sku;
    }

    p->tsens.sensor_cnt = tsens_get_cnt();
    p->tsens.temp_deci_c = tsens_get_temp_deci_c();
    p->tsens.temp_deci_k = p->tsens.temp_deci_c + 2730;
    {
        tsens_snapshot_t ts;
        RtlZeroMemory(&ts, sizeof(ts));
        tsens_get_snapshot(&ts);
        p->tsens.read_cnt = ts.read_cnt;
        p->tsens.dsm_refresh_cnt = ts.dsm_refresh_cnt;
        p->tsens.threshold_hit_cnt = ts.threshold_hit_cnt;
        p->tsens.history_cnt = ts.history_cnt;
        p->tsens.history_dropped = ts.history_dropped;
        p->tsens.last_event_code = ts.last_event_code;
        p->tsens.last_event_status = ts.last_event_status;
        p->tsens.last_sensor_id = ts.last_sensor_id;
        p->tsens.last_temp_deci_c = ts.last_temp_deci_c;
        p->tsens.last_low_deci_c = ts.last_low_deci_c;
        p->tsens.last_high_deci_c = ts.last_high_deci_c;
        p->tsens.last_event_tick100ns = ts.last_event_tick100ns;

        ULONG sensor_cnt = ts.sensor_cnt;
        if (sensor_cnt > RTL_NUMBER_OF(p->tsens.sensors))
            sensor_cnt = RTL_NUMBER_OF(p->tsens.sensors);
        p->tsens.sensor_entry_cnt = sensor_cnt;
        for (ULONG i = 0; i < sensor_cnt; i++) {
            p->tsens.sensors[i].last_temp_deci_c = ts.sensors[i].last_temp_deci_c;
            p->tsens.sensors[i].min_temp_deci_c = ts.sensors[i].min_temp_deci_c;
            p->tsens.sensors[i].max_temp_deci_c = ts.sensors[i].max_temp_deci_c;
            p->tsens.sensors[i].low_threshold_deci_c = ts.sensors[i].low_threshold_deci_c;
            p->tsens.sensors[i].high_threshold_deci_c = ts.sensors[i].high_threshold_deci_c;
            p->tsens.sensors[i].low_hit_cnt = ts.sensors[i].low_hit_cnt;
            p->tsens.sensors[i].high_hit_cnt = ts.sensors[i].high_hit_cnt;
            p->tsens.sensors[i].flags = ts.sensors[i].flags;
        }

        tsens_event_t history[16];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                tsens_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->tsens.history_entry_cnt = hist_cnt;
            p->tsens.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->tsens.history); i++) {
                p->tsens.history[i].tick100ns = history[i].tick100ns;
                p->tsens.history[i].sequence = history[i].sequence;
                p->tsens.history[i].event_code = history[i].event_code;
                p->tsens.history[i].status = history[i].status;
                p->tsens.history[i].sensor_id = history[i].sensor_id;
                p->tsens.history[i].temp_deci_c = history[i].temp_deci_c;
                p->tsens.history[i].low_deci_c = history[i].low_deci_c;
                p->tsens.history[i].high_deci_c = history[i].high_deci_c;
                p->tsens.history[i].aux0 = history[i].aux0;
                p->tsens.history[i].aux1 = history[i].aux1;
            }
        }
    }

    {
        mmrm_snapshot_t mmrm;
        RtlZeroMemory(&mmrm, sizeof(mmrm));
        mmrm_get_snapshot(&mmrm);
        p->mmrm.feature_enabled = mmrm.feature_enabled;
        p->mmrm.throttle_reason = mmrm.throttle_reason;
        p->mmrm.throttle_percentage = mmrm.throttle_percentage;
        p->mmrm.updated = mmrm.updated;
        p->mmrm.reserved_power_milli_watts = mmrm.reserved_power_milli_watts;
        p->mmrm.sampling_period_milli_seconds = mmrm.sampling_period_milli_seconds;
        p->mmrm.max_throttle_percentage = mmrm.max_throttle_percentage;
        p->mmrm.mitigation_start_threshold_milli_amps = mmrm.mitigation_start_threshold_milli_amps;
        p->mmrm.mitigation_stop_threshold_milli_amps = mmrm.mitigation_stop_threshold_milli_amps;
        p->mmrm.temperature_sensor_id = mmrm.temperature_sensor_id;
        p->mmrm.last_total_power_milli_watts = mmrm.last_total_power_milli_watts;
        p->mmrm.last_peak_milli_amps = mmrm.last_peak_milli_amps;
        p->mmrm.video_encoding_state = mmrm.video_encoding_state;
        p->mmrm.video_decoding_state = mmrm.video_decoding_state;
        p->mmrm.policy_wait_active = mmrm.policy_wait_active;
        p->mmrm.sampling_timer_running = mmrm.sampling_timer_running;
        p->mmrm.dsm_flags = mmrm.dsm_flags;
        p->mmrm.dsm_refresh_cnt = mmrm.dsm_refresh_cnt;
        p->mmrm.last_dsm_status = mmrm.last_dsm_status;
        p->mmrm.dsm_poll_interval_milli_seconds = mmrm.dsm_poll_interval_milli_seconds;
        p->mmrm.last_dsm_policy_item_cnt = mmrm.last_dsm_policy_item_cnt;
        p->mmrm.last_dsm_clock_item_cnt = mmrm.last_dsm_clock_item_cnt;
        p->mmrm.peak_voltage_milli_volts = mmrm.peak_voltage_milli_volts;
        p->mmrm.global_dyn_permille = mmrm.global_dyn_permille;
        p->mmrm.global_lkg_permille = mmrm.global_lkg_permille;
        p->mmrm.last_camera_power_milli_watts = mmrm.last_camera_power_milli_watts;
        p->mmrm.last_video_power_milli_watts = mmrm.last_video_power_milli_watts;
        p->mmrm.last_display_power_milli_watts = mmrm.last_display_power_milli_watts;
        p->mmrm.domain_cnt = mmrm.domain_cnt;
        p->mmrm.current_temp_deci_k = mmrm.current_temp_deci_k;
        p->mmrm.threshold_low_deci_k = mmrm.threshold_low_deci_k;
        p->mmrm.threshold_high_deci_k = mmrm.threshold_high_deci_k;
        p->mmrm.camera_temp_deci_c = mmrm.camera_temp_deci_c;
        p->mmrm.video_temp_deci_c = mmrm.video_temp_deci_c;
        p->mmrm.display_temp_deci_c = mmrm.display_temp_deci_c;
        p->mmrm.event_sequence = mmrm.event_sequence;
        p->mmrm.history_cnt = mmrm.history_cnt;
        p->mmrm.history_dropped = mmrm.history_dropped;
        p->mmrm.last_event_code = mmrm.last_event_code;
        p->mmrm.last_event_status = mmrm.last_event_status;
        p->mmrm.last_event_reason = mmrm.last_event_reason;
        p->mmrm.last_event_percent = mmrm.last_event_percent;
        p->mmrm.last_event_updated = mmrm.last_event_updated;
        p->mmrm.last_event_peak_milli_amps = mmrm.last_event_peak_milli_amps;
        p->mmrm.last_event_temp_deci_k = mmrm.last_event_temp_deci_k;
        p->mmrm.last_event_low_temp_deci_k = mmrm.last_event_low_temp_deci_k;
        p->mmrm.last_event_high_temp_deci_k = mmrm.last_event_high_temp_deci_k;
        p->mmrm.last_event_tick100ns = mmrm.last_event_tick100ns;
        for (ULONG i = 0; i < mmrm.domain_cnt && i < RTL_NUMBER_OF(p->mmrm.domains); i++) {
            p->mmrm.domains[i].freq_khz = mmrm.domains[i].freq_khz;
            p->mmrm.domains[i].power_milli_watts = mmrm.domains[i].power_milli_watts;
            p->mmrm.domains[i].flags = mmrm.domains[i].flags;
            p->mmrm.domains[i].dyn_permille = mmrm.domains[i].dyn_permille;
            p->mmrm.domains[i].lkg_permille = mmrm.domains[i].lkg_permille;
            p->mmrm.domains[i].temp_bias_deci_c = mmrm.domains[i].temp_bias_deci_c;
        }

        mmrm_event_t history[WMI_MMRM_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                mmrm_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->mmrm.history_entry_cnt = hist_cnt;
            p->mmrm.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->mmrm.history); i++) {
                p->mmrm.history[i].tick100ns = history[i].tick100ns;
                p->mmrm.history[i].sequence = history[i].sequence;
                p->mmrm.history[i].event_code = history[i].event_code;
                p->mmrm.history[i].status = history[i].status;
                p->mmrm.history[i].reason = history[i].reason;
                p->mmrm.history[i].percent = history[i].percent;
                p->mmrm.history[i].updated = history[i].updated;
                p->mmrm.history[i].peak_milli_amps = history[i].peak_milli_amps;
                p->mmrm.history[i].temp_deci_k = history[i].temp_deci_k;
                p->mmrm.history[i].low_temp_deci_k = history[i].low_temp_deci_k;
                p->mmrm.history[i].high_temp_deci_k = history[i].high_temp_deci_k;
                p->mmrm.history[i].passive_limit = history[i].passive_limit;
                p->mmrm.history[i].activation_reasons = history[i].activation_reasons;
                p->mmrm.history[i].feature_enabled = history[i].feature_enabled;
                p->mmrm.history[i].dsm_flags = history[i].dsm_flags;
            }
        }
    }

    {
        deep_standby_snapshot_t ds;
        RtlZeroMemory(&ds, sizeof(ds));
        if (NT_SUCCESS(deep_standby_get_snapshot(&ds))) {
            p->deep_standby.feature_enabled = ds.feature_enabled;
            p->deep_standby.modem_enabled = ds.modem_enabled;
            p->deep_standby.wlan_enabled = ds.wlan_enabled;
            p->deep_standby.power_src = ds.power_src;
            p->deep_standby.screen_state = ds.screen_state;
            p->deep_standby.user_state = ds.user_state;
            p->deep_standby.ms_on_dc_pwr = ds.ms_on_dc_pwr;
            p->deep_standby.deep_standby_state = ds.deep_standby_state;
            p->deep_standby.timer_expired = ds.timer_expired;
            p->deep_standby.batt_drain_exceeded_limit = ds.batt_drain_exceeded_limit;
            p->deep_standby.batt_drain_since_tmr_start = ds.batt_drain_since_tmr_start;
            p->deep_standby.current_batt_pct = ds.current_batt_pct;
            p->deep_standby.timeout_seconds = ds.timeout_seconds;
            p->deep_standby.max_batt_drain_pct = ds.max_batt_drain_pct;
            p->deep_standby.modem_req = ds.modem_req;
            p->deep_standby.evaluate_cnt = ds.evaluate_cnt;
            p->deep_standby.callback_cnt = ds.callback_cnt;
            p->deep_standby.state_transition_cnt = ds.state_transition_cnt;
            p->deep_standby.enter_cnt = ds.enter_cnt;
            p->deep_standby.exit_cnt = ds.exit_cnt;
            p->deep_standby.wlan_ioctl_errors = ds.wlan_ioctl_errors;
            p->deep_standby.modem_ioctl_errors = ds.modem_ioctl_errors;
            p->deep_standby.last_wlan_ioctl_status = ds.last_wlan_ioctl_status;
            p->deep_standby.last_modem_ioctl_status = ds.last_modem_ioctl_status;
            p->deep_standby.history_cnt = ds.history_cnt;
            p->deep_standby.history_dropped = ds.history_dropped;
            p->deep_standby.last_event_code = ds.last_event_code;
            p->deep_standby.last_event_status = ds.last_event_status;
        }

        deep_standby_history_entry_t history[WMI_DS_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(deep_standby_get_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                                &hist_dropped))) {
            p->deep_standby.history_entry_cnt = hist_cnt;
            p->deep_standby.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->deep_standby.history); i++) {
                p->deep_standby.history[i].event_code = history[i].event_code;
                p->deep_standby.history[i].status = history[i].status;
                p->deep_standby.history[i].prev_ms_on_dc_pwr = history[i].prev_ms_on_dc_pwr;
                p->deep_standby.history[i].curr_ms_on_dc_pwr = history[i].curr_ms_on_dc_pwr;
                p->deep_standby.history[i].prev_deep_standby_state =
                    history[i].prev_deep_standby_state;
                p->deep_standby.history[i].curr_deep_standby_state =
                    history[i].curr_deep_standby_state;
                p->deep_standby.history[i].aux0 = history[i].aux0;
                p->deep_standby.history[i].aux1 = history[i].aux1;
                p->deep_standby.history[i].tick100ns = history[i].tick100ns;
            }
        }
    }

    {
        thermal_req_snapshot_t tr;
        RtlZeroMemory(&tr, sizeof(tr));
        thermal_req_get_snapshot(&tr);

        p->thermal_req.poll_ms = tr.poll_ms;
        p->thermal_req.active_cnt = tr.active_cnt;
        p->thermal_req.last_req_id = tr.last_req_id;
        p->thermal_req.total_queued = tr.total_queued;
        p->thermal_req.total_completed = tr.total_completed;
        p->thermal_req.total_cancelled = tr.total_cancelled;
        p->thermal_req.total_timeout = tr.total_timeout;
        p->thermal_req.total_low_threshold_hits = tr.total_low_threshold_hits;
        p->thermal_req.total_high_threshold_hits = tr.total_high_threshold_hits;
        p->thermal_req.total_busy_rejected = tr.total_busy_rejected;
        p->thermal_req.total_no_slot_rejected = tr.total_no_slot_rejected;
        p->thermal_req.total_mark_cancelable_failures = tr.total_mark_cancelable_failures;
        p->thermal_req.event_sequence = tr.event_sequence;
        p->thermal_req.history_cnt = tr.history_cnt;
        p->thermal_req.history_dropped = tr.history_dropped;
        p->thermal_req.last_event_code = tr.last_event_code;
        p->thermal_req.last_event_status = tr.last_event_status;
        p->thermal_req.last_event_req_id = tr.last_event_req_id;
        p->thermal_req.last_event_sensor_id = tr.last_event_sensor_id;
        p->thermal_req.last_event_temp_deci_k = tr.last_event_temp_deci_k;
        p->thermal_req.last_event_low_temp_deci_k = tr.last_event_low_temp_deci_k;
        p->thermal_req.last_event_high_temp_deci_k = tr.last_event_high_temp_deci_k;
        p->thermal_req.last_event_tick100ns = tr.last_event_tick100ns;

        thermal_req_event_t history[WMI_THERMALREQ_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(thermal_req_get_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                               &hist_dropped))) {
            p->thermal_req.history_entry_cnt = hist_cnt;
            p->thermal_req.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->thermal_req.history); i++) {
                p->thermal_req.history[i].sequence = history[i].sequence;
                p->thermal_req.history[i].tick100ns = history[i].tick100ns;
                p->thermal_req.history[i].event_code = history[i].event_code;
                p->thermal_req.history[i].status = history[i].status;
                p->thermal_req.history[i].req_id = history[i].req_id;
                p->thermal_req.history[i].sensor_id = history[i].sensor_id;
                p->thermal_req.history[i].temp_deci_k = history[i].temp_deci_k;
                p->thermal_req.history[i].low_temp_deci_k = history[i].low_temp_deci_k;
                p->thermal_req.history[i].high_temp_deci_k = history[i].high_temp_deci_k;
                p->thermal_req.history[i].active_cnt = history[i].active_cnt;
                p->thermal_req.history[i].port_id = history[i].port_id;
            }
        }
    }

    {
        bcl_snapshot_t bcl;
        RtlZeroMemory(&bcl, sizeof(bcl));
        bcl_get_snapshot(&bcl);

        p->bcl.version = bcl.version;
        p->bcl.test_enabled = bcl.test_enabled;
        p->bcl.current_notify_type = bcl.current_notify_type;
        p->bcl.parking_state = bcl.parking_state;
        p->bcl.desired_parked_cpus = bcl.desired_parked_cpus;
        p->bcl.current_parked_cpus = bcl.current_parked_cpus;
        p->bcl.throttle_permille = bcl.throttle_permille;
        p->bcl.is_low_imax_state = bcl.is_low_imax_state;
        p->bcl.mitigation_state = bcl.mitigation_state;
        p->bcl.threshold_cnt = bcl.threshold_cnt;
        p->bcl.submit_cnt = bcl.submit_cnt;
        p->bcl.submit_accepted = bcl.submit_accepted;
        p->bcl.submit_rejected = bcl.submit_rejected;
        p->bcl.work_item_run_cnt = bcl.work_item_run_cnt;
        p->bcl.threshold_op_cnt = bcl.threshold_op_cnt;
        p->bcl.threshold_downstack_success = bcl.threshold_downstack_success;
        p->bcl.threshold_downstack_failure = bcl.threshold_downstack_failure;
        p->bcl.threshold_cache_hits = bcl.threshold_cache_hits;
        p->bcl.threshold_cache_misses = bcl.threshold_cache_misses;
        p->bcl.event_sequence = bcl.event_sequence;
        p->bcl.history_cnt = bcl.history_cnt;
        p->bcl.history_dropped = bcl.history_dropped;
        p->bcl.last_event_code = bcl.last_event_code;
        p->bcl.last_event_status = bcl.last_event_status;
        p->bcl.last_event_tick100ns = bcl.last_event_tick100ns;
        p->bcl.last_res_id = bcl.last_res_id;
        p->bcl.last_field = bcl.last_field;
        p->bcl.last_sub_type = bcl.last_sub_type;
        p->bcl.last_value0 = bcl.last_value0;
        p->bcl.last_value1 = bcl.last_value1;
        p->bcl.last_flag = bcl.last_flag;
        p->bcl.last_aux0 = bcl.last_aux0;

        ULONG thr_cnt = bcl.threshold_cnt;
        if (thr_cnt > RTL_NUMBER_OF(p->bcl.thresholds))
            thr_cnt = RTL_NUMBER_OF(p->bcl.thresholds);
        p->bcl.threshold_entry_cnt = thr_cnt;
        for (ULONG i = 0; i < thr_cnt; i++) {
            p->bcl.thresholds[i].res_id = bcl.thresholds[i].res_id;
            p->bcl.thresholds[i].field = bcl.thresholds[i].field;
            p->bcl.thresholds[i].sub_type = bcl.thresholds[i].sub_type;
            p->bcl.thresholds[i].value0 = bcl.thresholds[i].value0;
            p->bcl.thresholds[i].value1 = bcl.thresholds[i].value1;
            p->bcl.thresholds[i].update_cnt = bcl.thresholds[i].update_cnt;
            p->bcl.thresholds[i].last_status = bcl.thresholds[i].last_status;
            p->bcl.thresholds[i].last_tick100ns = bcl.thresholds[i].last_tick100ns;
            p->bcl.thresholds[i].flag = bcl.thresholds[i].flag;
        }

        bcl_event_t history[WMI_BCL_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                bcl_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->bcl.history_entry_cnt = hist_cnt;
            p->bcl.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->bcl.history); i++) {
                p->bcl.history[i].tick100ns = history[i].tick100ns;
                p->bcl.history[i].sequence = history[i].sequence;
                p->bcl.history[i].event_code = history[i].event_code;
                p->bcl.history[i].status = history[i].status;
                p->bcl.history[i].notify_type = history[i].notify_type;
                p->bcl.history[i].parking_state = history[i].parking_state;
                p->bcl.history[i].desired_parked_cpus = history[i].desired_parked_cpus;
                p->bcl.history[i].res_id = history[i].res_id;
                p->bcl.history[i].field = history[i].field;
                p->bcl.history[i].sub_type = history[i].sub_type;
                p->bcl.history[i].value0 = history[i].value0;
                p->bcl.history[i].value1 = history[i].value1;
                p->bcl.history[i].flag = history[i].flag;
                p->bcl.history[i].aux0 = history[i].aux0;
            }
        }
    }

    {
        interrupt_snapshot_t it;
        RtlZeroMemory(&it, sizeof(it));
        interrupt_get_snapshot(&it);

        p->interrupt.version = it.version;
        p->interrupt.isr_cnt = it.isr_cnt;
        p->interrupt.dpc_cnt = it.dpc_cnt;
        p->interrupt.enable_cnt = it.enable_cnt;
        p->interrupt.disable_cnt = it.disable_cnt;
        p->interrupt.dpc_queued_cnt = it.dpc_queued_cnt;
        p->interrupt.last_message_id = it.last_message_id;
        p->interrupt.last_isr_tick100ns = it.last_isr_tick100ns;
        p->interrupt.last_dpc_tick100ns = it.last_dpc_tick100ns;
        p->interrupt.idle_scan_cnt = it.idle_scan_cnt;
        p->interrupt.idle_wake_sample_cnt = it.idle_wake_sample_cnt;
        p->interrupt.last_idle_level = it.last_idle_level;
        p->interrupt.last_idle_mode_idx = it.last_idle_mode_idx;
        p->interrupt.last_wake_irq_status = it.last_wake_irq_status;
        p->interrupt.last_wake_geni_status = it.last_wake_geni_status;
        p->interrupt.last_wake_cmd0 = it.last_wake_cmd0;
        p->interrupt.last_wake_cmd_ctrl = it.last_wake_cmd_ctrl;
        p->interrupt.map_cnt = it.map_cnt;
        p->interrupt.map_hit_cnt = it.map_hit_cnt;
        p->interrupt.map_miss_cnt = it.map_miss_cnt;
        p->interrupt.last_lookup_interrupt_id = it.last_lookup_interrupt_id;
        p->interrupt.last_lookup_offset = it.last_lookup_offset;
        p->interrupt.last_lookup_trigger_type = it.last_lookup_trigger_type;
        p->interrupt.last_lookup_driver_id = it.last_lookup_driver_id;
        p->interrupt.wake_bit_hit_cnt = it.wake_bit_hit_cnt;
        p->interrupt.last_wake_offset = it.last_wake_offset;
        p->interrupt.last_wake_interrupt_id = it.last_wake_interrupt_id;
        p->interrupt.event_sequence = it.event_sequence;
        p->interrupt.history_cnt = it.history_cnt;
        p->interrupt.history_dropped = it.history_dropped;
        p->interrupt.last_event_code = it.last_event_code;
        p->interrupt.last_event_status = it.last_event_status;
        p->interrupt.last_event_tick100ns = it.last_event_tick100ns;

        ULONG port_cnt = it.port_cnt;
        if (port_cnt > RTL_NUMBER_OF(p->interrupt.ports))
            port_cnt = RTL_NUMBER_OF(p->interrupt.ports);
        p->interrupt.port_entry_cnt = port_cnt;
        for (ULONG i = 0; i < port_cnt; i++) {
            p->interrupt.ports[i].port_id = it.ports[i].port_id;
            p->interrupt.ports[i].isr_cnt = it.ports[i].isr_cnt;
            p->interrupt.ports[i].dpc_cnt = it.ports[i].dpc_cnt;
            p->interrupt.ports[i].enable_cnt = it.ports[i].enable_cnt;
            p->interrupt.ports[i].disable_cnt = it.ports[i].disable_cnt;
            p->interrupt.ports[i].dpc_queued_cnt = it.ports[i].dpc_queued_cnt;
            p->interrupt.ports[i].last_message_id = it.ports[i].last_message_id;
            p->interrupt.ports[i].last_isr_tick100ns = it.ports[i].last_isr_tick100ns;
            p->interrupt.ports[i].last_dpc_tick100ns = it.ports[i].last_dpc_tick100ns;
        }

        ULONG wake_top_cnt = it.wake_top_cnt;
        if (wake_top_cnt > RTL_NUMBER_OF(p->interrupt.wake_top))
            wake_top_cnt = RTL_NUMBER_OF(p->interrupt.wake_top);
        p->interrupt.wake_top_entry_cnt = wake_top_cnt;
        for (ULONG i = 0; i < wake_top_cnt; i++) {
            p->interrupt.wake_top[i].offset = it.wake_top[i].offset;
            p->interrupt.wake_top[i].interrupt_id = it.wake_top[i].interrupt_id;
            p->interrupt.wake_top[i].hit_cnt = it.wake_top[i].hit_cnt;
        }

        interrupt_event_t history[WMI_INTERRUPT_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                interrupt_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->interrupt.history_entry_cnt = hist_cnt;
            p->interrupt.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->interrupt.history); i++) {
                p->interrupt.history[i].tick100ns = history[i].tick100ns;
                p->interrupt.history[i].sequence = history[i].sequence;
                p->interrupt.history[i].event_code = history[i].event_code;
                p->interrupt.history[i].status = history[i].status;
                p->interrupt.history[i].port_id = history[i].port_id;
                p->interrupt.history[i].message_id = history[i].message_id;
                p->interrupt.history[i].interrupt_id = history[i].interrupt_id;
                p->interrupt.history[i].map_offset = history[i].map_offset;
                p->interrupt.history[i].value0 = history[i].value0;
                p->interrupt.history[i].value1 = history[i].value1;
                p->interrupt.history[i].value2 = history[i].value2;
                p->interrupt.history[i].value3 = history[i].value3;
            }
        }
    }

    {
        socsubsys_snapshot_t ss;
        RtlZeroMemory(&ss, sizeof(ss));
        soc_subsys_get_snapshot(&ss);

        p->soc_subsys.version = ss.version;
        p->soc_subsys.drips_state_id = ss.drips_state_id;
        p->soc_subsys.enabled_mask = ss.enabled_mask;
        p->soc_subsys.subsystem_cnt = ss.subsystem_cnt;
        p->soc_subsys.in_drips = ss.in_drips;
        p->soc_subsys.drips_blocked = ss.drips_blocked;
        p->soc_subsys.drips_blocked_reason = ss.drips_blocked_reason;
        p->soc_subsys.last_blocked_reason = ss.last_blocked_reason;
        p->soc_subsys.current_blocked_duration100ns = ss.current_blocked_duration100ns;
        p->soc_subsys.blocked_boot100ns = ss.blocked_boot100ns;
        p->soc_subsys.blocked_usb100ns = ss.blocked_usb100ns;
        p->soc_subsys.blocked_dbg100ns = ss.blocked_dbg100ns;
        p->soc_subsys.blocked_other100ns = ss.blocked_other100ns;
        p->soc_subsys.aoss_sleep_count100ns = ss.aoss_sleep_count100ns;
        p->soc_subsys.ddr_sleep_count100ns = ss.ddr_sleep_count100ns;
        p->soc_subsys.event_sequence = ss.event_sequence;
        p->soc_subsys.history_cnt = ss.history_cnt;
        p->soc_subsys.history_dropped = ss.history_dropped;
        p->soc_subsys.last_event_code = ss.last_event_code;
        p->soc_subsys.last_event_status = ss.last_event_status;
        p->soc_subsys.last_event_subsystem_idx = ss.last_event_subsystem_idx;
        p->soc_subsys.last_event_reason = ss.last_event_reason;
        p->soc_subsys.last_event_tick100ns = ss.last_event_tick100ns;

        ULONG entry_cnt = ss.subsystem_cnt;
        if (entry_cnt > RTL_NUMBER_OF(p->soc_subsys.entries))
            entry_cnt = RTL_NUMBER_OF(p->soc_subsys.entries);
        p->soc_subsys.entry_cnt = entry_cnt;

        for (ULONG i = 0; i < entry_cnt; i++) {
            p->soc_subsys.entries[i].metadata_cnt = ss.entries[i].metadata_cnt;
            p->soc_subsys.entries[i].flags = ss.entries[i].flags;
            p->soc_subsys.entries[i].wakeup_cnt = ss.entries[i].stats.wakeup_cnt;
            p->soc_subsys.entries[i].blocked_duration100ns =
                ss.entries[i].stats.blocked_duration100ns;
            p->soc_subsys.entries[i].sleep_duration100ns = ss.entries[i].stats.sleep_duration100ns;
            p->soc_subsys.entries[i].last_entered_at100ns = ss.entries[i].last_entered_at100ns;
            p->soc_subsys.entries[i].last_exited_at100ns = ss.entries[i].last_exited_at100ns;
            p->soc_subsys.entries[i].island_entry_cnt = ss.entries[i].island_entry_cnt;
            p->soc_subsys.entries[i].island_entry_time100ns = ss.entries[i].island_entry_time100ns;
            p->soc_subsys.entries[i].island_exit_time100ns = ss.entries[i].island_exit_time100ns;
            p->soc_subsys.entries[i].island_accum_duration100ns =
                ss.entries[i].island_accum_duration100ns;
        }

        socsubsys_event_t history[WMI_SOCSUBSYS_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(soc_subsys_get_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                              &hist_dropped))) {
            p->soc_subsys.history_entry_cnt = hist_cnt;
            p->soc_subsys.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->soc_subsys.history); i++) {
                p->soc_subsys.history[i].tick100ns = history[i].tick100ns;
                p->soc_subsys.history[i].sequence = history[i].sequence;
                p->soc_subsys.history[i].event_code = history[i].event_code;
                p->soc_subsys.history[i].status = history[i].status;
                p->soc_subsys.history[i].subsystem_idx = history[i].subsystem_idx;
                p->soc_subsys.history[i].reason = history[i].reason;
                p->soc_subsys.history[i].in_drips = history[i].in_drips;
                p->soc_subsys.history[i].wakeup_cnt = history[i].wakeup_cnt;
                p->soc_subsys.history[i].blocked_duration100ns = history[i].blocked_duration100ns;
                p->soc_subsys.history[i].sleep_duration100ns = history[i].sleep_duration100ns;
                p->soc_subsys.history[i].aux100ns = history[i].aux100ns;
            }
        }
    }

    {
        parksel_snapshot_t park;
        RtlZeroMemory(&park, sizeof(park));

        p->park_sel.version = 1;
        NTSTATUS park_status = pep_get_park_selection_snapshot(&park);
        p->park_sel.capture_status = (ULONG)park_status;
        if (NT_SUCCESS(park_status)) {
            p->park_sel.version = park.version;
            p->park_sel.cpu_cnt = park.cpu_cnt;
            p->park_sel.active_cpu_cnt = park.active_cpu_cnt;
            p->park_sel.last_additional_unparked = park.last_additional_unparked;
            p->park_sel.last_evaluation_type = park.last_evaluation_type;
            p->park_sel.last_evaluation_time = park.last_evaluation_time;
            p->park_sel.last_cluster0_evaluation_time = park.last_cluster0_evaluation_time;
            p->park_sel.last_cluster1_evaluation_time = park.last_cluster1_evaluation_time;
            p->park_sel.timer_threshold0_sec = park.timer_threshold0_sec;
            p->park_sel.timer_threshold1_sec = park.timer_threshold1_sec;
            p->park_sel.core_rotation_stat0 = park.core_rotation_stat0;
            p->park_sel.core_rotation_stat1 = park.core_rotation_stat1;
            p->park_sel.toggle_state = park.toggle_state;
            p->park_sel.cluster0_unparked = park.cluster0_unparked;
            p->park_sel.cluster1_unparked = park.cluster1_unparked;
            p->park_sel.pref_rail_id = park.pref_rail_id;
            p->park_sel.timer_rail_id = park.timer_rail_id;
            p->park_sel.num_unpark_rail_id = park.num_unpark_rail_id;
            p->park_sel.core_rotation_rail_id = park.core_rotation_rail_id;
            p->park_sel.event_sequence = park.event_sequence;
            p->park_sel.history_cnt = park.history_cnt;
            p->park_sel.history_dropped = park.history_dropped;
            p->park_sel.last_event_code = park.last_event_code;
            p->park_sel.last_event_status = park.last_event_status;
            p->park_sel.last_event_additional_unparked = park.last_event_additional_unparked;
            p->park_sel.last_event_remaining_unparked = park.last_event_remaining_unparked;
            p->park_sel.last_event_tick100ns = park.last_event_tick100ns;
            p->park_sel.thermal_rank_enabled = park.thermal_rank_enabled;
            p->park_sel.thermal_rank_hysteresis_deci_c = park.thermal_rank_hysteresis_deci_c;
            p->park_sel.thermal_rank_update_interval_ms = park.thermal_rank_update_interval_ms;
            p->park_sel.thermal_sensor_base_idx = park.thermal_sensor_base_idx;
            p->park_sel.thermal_refresh_cnt = park.thermal_refresh_cnt;
            p->park_sel.thermal_apply_cnt = park.thermal_apply_cnt;
            p->park_sel.last_thermal_status = park.last_thermal_status;

            ULONG rank_cnt = park.active_cpu_cnt;
            if (rank_cnt == 0 || rank_cnt > WMI_PARKSEL_CPU_CNT)
                rank_cnt = park.cpu_cnt;
            if (rank_cnt > WMI_PARKSEL_CPU_CNT)
                rank_cnt = WMI_PARKSEL_CPU_CNT;
            p->park_sel.rank_entry_cnt = rank_cnt;

            for (ULONG i = 0; i < rank_cnt; i++) {
                p->park_sel.rank_preference[i] = park.rank_preference[i];
                p->park_sel.previous_rank_preference[i] = park.previous_rank_preference[i];
                p->park_sel.rank_cpu_temp_deci_c[i] = park.rank_cpu_temp_deci_c[i];
            }

            ULONG cpu_cnt = park.cpu_cnt;
            if (cpu_cnt > WMI_PARKSEL_CPU_CNT)
                cpu_cnt = WMI_PARKSEL_CPU_CNT;
            p->park_sel.cpu_entry_cnt = cpu_cnt;

            for (ULONG i = 0; i < cpu_cnt; i++) {
                ULONG rank = park.current_rank[i];
                LONG temp_deci_c = 0;
                if (rank < rank_cnt)
                    temp_deci_c = park.rank_cpu_temp_deci_c[rank];

                p->park_sel.cpus[i].current_park_state = park.current_park_state[i];
                p->park_sel.cpus[i].previous_park_state = park.previous_park_state[i];
                p->park_sel.cpus[i].current_preference = park.current_preference[i];
                p->park_sel.cpus[i].current_rank = park.current_rank[i];
                p->park_sel.cpus[i].transition_cnt = park.transition_cnt[i];
                p->park_sel.cpus[i].temperature_deci_c = temp_deci_c;

                if (park.current_park_state[i] != 0)
                    p->park_sel.current_parked_cnt++;
                else
                    p->park_sel.current_unparked_cnt++;

                if (park.previous_park_state[i] != 0)
                    p->park_sel.previous_parked_cnt++;
            }

            parksel_event_t history[WMI_PARKSEL_HISTORY_CNT];
            ULONG hist_cnt = 0;
            ULONG hist_dropped = 0;
            RtlZeroMemory(history, sizeof(history));
            if (NT_SUCCESS(pep_get_park_selection_history(history, RTL_NUMBER_OF(history),
                                                          &hist_cnt, &hist_dropped))) {
                p->park_sel.history_entry_cnt = hist_cnt;
                p->park_sel.history_dropped = hist_dropped;
                for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->park_sel.history); i++) {
                    p->park_sel.history[i].tick100ns = history[i].tick100ns;
                    p->park_sel.history[i].sequence = history[i].sequence;
                    p->park_sel.history[i].event_code = history[i].event_code;
                    p->park_sel.history[i].status = history[i].status;
                    p->park_sel.history[i].cpu = history[i].cpu;
                    p->park_sel.history[i].cpu_cnt = history[i].cpu_cnt;
                    p->park_sel.history[i].additional_unparked = history[i].additional_unparked;
                    p->park_sel.history[i].remaining_unparked = history[i].remaining_unparked;
                    p->park_sel.history[i].evaluation_type = history[i].evaluation_type;
                    p->park_sel.history[i].evaluation_time = history[i].evaluation_time;
                    p->park_sel.history[i].cluster0_unparked = history[i].cluster0_unparked;
                    p->park_sel.history[i].cluster1_unparked = history[i].cluster1_unparked;
                    p->park_sel.history[i].aux0 = history[i].aux0;
                    p->park_sel.history[i].aux1 = history[i].aux1;
                }
            }
        }
    }

    {
        UCHAR snap[4096];
        size_t written = 0;
        if (NT_SUCCESS(res_snapshot(snap, sizeof(snap), &written)) &&
            written >= sizeof(ULONG) * 3) {
            typedef struct snap_hdr_mini {
                ULONG magic;
                ULONG version;
                ULONG cnt;
                ULONG entry_size;
            } snap_hdr_mini_t;

            snap_hdr_mini_t* h = (snap_hdr_mini_t*)snap;
            p->res.res_snapshot_cnt = h->cnt;
        }

        res_key_t k;
        k.sub_type = 0;
        k.res_id = 0x30445344u;
        (VOID) res_get(k, &p->res.dsd0);
        k.res_id = 0x31445344u;
        (VOID) res_get(k, &p->res.dsd1);
        k.res_id = 0x32445344u;
        (VOID) res_get(k, &p->res.dsd2);
        k.res_id = 0x33445344u;
        (VOID) res_get(k, &p->res.dsd3);
        k.res_id = 0x34445344u;
        (VOID) res_get(k, &p->res.dsd4);
        k.res_id = 0x35445344u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.dsd5_0);
        k.sub_type = 0;
        k.res_id = 0x31524D4Du;
        (VOID) res_get(k, &p->res.mmr1);
        k.res_id = 0x32524D4Du;
        (VOID) res_get(k, &p->res.mmr2);
        k.res_id = 0x33524D4Du;
        (VOID) res_get(k, &p->res.mmr3);
        k.res_id = 0x34524D4Du;
        (VOID) res_get(k, &p->res.mmr4);
        k.sub_type = 0;
        k.res_id = 0x35524D4Du;
        (VOID) res_get(k, &p->res.mmr5);
        k.res_id = 0x36524D4Du;
        (VOID) res_get(k, &p->res.mmr6);
        k.res_id = 0x38524D4Du;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.mmr8);
        k.res_id = 0x39524D4Du;
        k.sub_type = p->mmrm.last_event_code;
        (VOID) res_get(k, &p->res.mmr9);

        k.sub_type = 0;
        k.res_id = 0x30535453u;
        (VOID) res_get(k, &p->res.sts0);
        k.res_id = 0x31535453u;
        (VOID) res_get(k, &p->res.sts1);
        k.res_id = 0x32535453u;
        (VOID) res_get(k, &p->res.sts2);

        k.sub_type = 0;
        k.res_id = 0x30514854u;
        (VOID) res_get(k, &p->res.thq0);
        k.res_id = 0x31514854u;
        (VOID) res_get(k, &p->res.thq1);
        k.res_id = 0x32514854u;
        (VOID) res_get(k, &p->res.thq2);

        k.sub_type = 0;
        k.res_id = 0x30525354u;
        (VOID) res_get(k, &p->res.tsr0);
        k.res_id = 0x31525354u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.tsr1_0);
        k.res_id = 0x32525354u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.tsr2_0);
        k.res_id = 0x33525354u;
        k.sub_type = p->tsens.last_event_code;
        (VOID) res_get(k, &p->res.tsr3);

        k.sub_type = 0;
        k.res_id = 0x304C4342u;
        (VOID) res_get(k, &p->res.bcl0);
        k.res_id = 0x314C4342u;
        k.sub_type = p->bcl.last_event_code;
        (VOID) res_get(k, &p->res.bcl1);
        k.res_id = 0x324C4342u;
        k.sub_type = p->bcl.last_res_id;
        (VOID) res_get(k, &p->res.bcl2);
        k.res_id = 0x334C4342u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.bcl3);
        k.res_id = 0x344C4342u;
        (VOID) res_get(k, &p->res.bcl4);

        k.sub_type = 0;
        k.res_id = 0x30544E49u;
        (VOID) res_get(k, &p->res.int0);
        k.res_id = 0x31544E49u;
        k.sub_type = p->interrupt.last_event_code;
        (VOID) res_get(k, &p->res.int1);
        k.res_id = 0x32544E49u;
        k.sub_type = p->interrupt.last_idle_mode_idx;
        (VOID) res_get(k, &p->res.int2);
        k.res_id = 0x33544E49u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.int3);
        k.res_id = 0x34544E49u;
        (VOID) res_get(k, &p->res.int4);
        k.res_id = 0x57505551u;
        k.sub_type = p->interrupt.last_idle_mode_idx;
        (VOID) res_get(k, &p->res.qupw);
        k.res_id = 0x304D5751u;
        (VOID) res_get(k, &p->res.qwm0);
        k.res_id = 0x314D5751u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.qwm1);

        k.sub_type = 0;
        k.res_id = 0x30515353u;
        (VOID) res_get(k, &p->res.ssq0);
        k.res_id = 0x31515353u;
        k.sub_type = p->soc_subsys.last_event_code;
        (VOID) res_get(k, &p->res.ssq1);
        k.res_id = 0x32515353u;
        k.sub_type = p->soc_subsys.last_event_subsystem_idx;
        (VOID) res_get(k, &p->res.ssq2);
        k.res_id = 0x33515353u;
        (VOID) res_get(k, &p->res.ssq3);
        k.res_id = 0x34515353u;
        k.sub_type = 0;
        (VOID) res_get(k, &p->res.ssq4);

        k.sub_type = 0;
        k.res_id = 0x52504B50u;
        (VOID) res_get(k, &p->res.pkpr0);
        k.sub_type = 4;
        (VOID) res_get(k, &p->res.pkpr4);

        k.sub_type = 0;
        k.res_id = 0x54534B50u;
        (VOID) res_get(k, &p->res.pkst0);
        k.sub_type = 4;
        (VOID) res_get(k, &p->res.pkst4);

        k.sub_type = 0;
        k.res_id = 0x4C434B50u;
        (VOID) res_get(k, &p->res.pkcl0);
        k.sub_type = 1;
        (VOID) res_get(k, &p->res.pkcl1);

        k.sub_type = 0;
        k.res_id = 0x54524B50u;
        (VOID) res_get(k, &p->res.pkrt0);
        k.sub_type = 1;
        (VOID) res_get(k, &p->res.pkrt1);

        k.sub_type = 0;
        k.res_id = 0x43534B50u;
        (VOID) res_get(k, &p->res.pksc0);
        k.sub_type = 1;
        (VOID) res_get(k, &p->res.pksc1);
    }

    {
        dcvs_snapshot_t dcvs;
        RtlZeroMemory(&dcvs, sizeof(dcvs));
        dcvs_get_snapshot(&dcvs);

        p->dcvs.version = dcvs.version;
        p->dcvs.initialized = dcvs.initialized;
        p->dcvs.cpu_cnt = dcvs.cpu_cnt;
        p->dcvs.domain_id = dcvs.domain_id;
        p->dcvs.domain_members = dcvs.domain_members;
        p->dcvs.coordination_type = dcvs.coordination_type;
        p->dcvs.worst_case_transition_latency = dcvs.worst_case_transition_latency;
        p->dcvs.worst_case_transition_overhead = dcvs.worst_case_transition_overhead;
        p->dcvs.enable_perf_selection = dcvs.enable_perf_selection;
        p->dcvs.enable_thermal_perf_selection = dcvs.enable_thermal_perf_selection;
        p->dcvs.enable_boost = dcvs.enable_boost;
        p->dcvs.boost_freq_hz = dcvs.boost_freq_hz;
        p->dcvs.max_freq_hz = dcvs.max_freq_hz;
        p->dcvs.min_freq_hz = dcvs.min_freq_hz;
        p->dcvs.nominal_freq_hz = dcvs.nominal_freq_hz;
        p->dcvs.lowest_nonlinear_freq_hz = dcvs.lowest_nonlinear_freq_hz;
        p->dcvs.thermal_hot_deci_c = dcvs.thermal_hot_deci_c;
        p->dcvs.thermal_critical_deci_c = dcvs.thermal_critical_deci_c;
        p->dcvs.thermal_cap_hot_percent = dcvs.thermal_cap_hot_percent;
        p->dcvs.thermal_cap_critical_percent = dcvs.thermal_cap_critical_percent;
        p->dcvs.current_thermal_cap_percent = dcvs.current_thermal_cap_percent;
        p->dcvs.policy_update_cnt = dcvs.policy_update_cnt;
        p->dcvs.last_policy_tick100ns = dcvs.last_policy_tick100ns;
        p->dcvs.event_sequence = dcvs.event_sequence;
        p->dcvs.history_cnt = dcvs.history_cnt;
        p->dcvs.history_dropped = dcvs.history_dropped;
        p->dcvs.last_event_code = dcvs.last_event_code;
        p->dcvs.last_event_status = dcvs.last_event_status;
        p->dcvs.last_event_cpu = dcvs.last_event_cpu;
        p->dcvs.last_event_min_perf_state = dcvs.last_event_min_perf_state;
        p->dcvs.last_event_max_perf_state = dcvs.last_event_max_perf_state;
        p->dcvs.last_event_desired_perf_state = dcvs.last_event_desired_perf_state;
        p->dcvs.last_event_applied_perf_level = dcvs.last_event_applied_perf_level;
        p->dcvs.last_event_applied_freq_hz = dcvs.last_event_applied_freq_hz;
        p->dcvs.last_event_thermal_cap_percent = dcvs.last_event_thermal_cap_percent;
        p->dcvs.last_event_epp = dcvs.last_event_epp;
        p->dcvs.last_event_aux0 = dcvs.last_event_aux0;
        p->dcvs.last_event_aux1 = dcvs.last_event_aux1;
        p->dcvs.last_event_tick100ns = dcvs.last_event_tick100ns;

        ULONG perf_cnt = dcvs.perf_table_cnt;
        if (perf_cnt > RTL_NUMBER_OF(p->dcvs.perf_table_hz))
            perf_cnt = RTL_NUMBER_OF(p->dcvs.perf_table_hz);
        p->dcvs.perf_table_cnt = perf_cnt;
        for (ULONG i = 0; i < perf_cnt; i++)
            p->dcvs.perf_table_hz[i] = dcvs.perf_table_hz[i];

        ULONG cpu_cnt = dcvs.cpu_entry_cnt;
        if (cpu_cnt > RTL_NUMBER_OF(p->dcvs.cpus))
            cpu_cnt = RTL_NUMBER_OF(p->dcvs.cpus);
        p->dcvs.cpu_entry_cnt = cpu_cnt;
        for (ULONG i = 0; i < cpu_cnt; i++) {
            p->dcvs.cpus[i].cpu = dcvs.cpus[i].cpu;
            p->dcvs.cpus[i].domain_idx = dcvs.cpus[i].domain_idx;
            p->dcvs.cpus[i].current_perf_level = dcvs.cpus[i].current_perf_level;
            p->dcvs.cpus[i].current_freq_hz = dcvs.cpus[i].current_freq_hz;
            p->dcvs.cpus[i].requested_perf_state_idx = dcvs.cpus[i].requested_perf_state_idx;
            p->dcvs.cpus[i].min_perf = dcvs.cpus[i].min_perf;
            p->dcvs.cpus[i].max_perf = dcvs.cpus[i].max_perf;
            p->dcvs.cpus[i].desired_perf = dcvs.cpus[i].desired_perf;
            p->dcvs.cpus[i].epp = dcvs.cpus[i].epp;
            p->dcvs.cpus[i].enter_time100ns = dcvs.cpus[i].enter_time100ns;
            p->dcvs.cpus[i].residency100ns = dcvs.cpus[i].residency100ns;
            p->dcvs.cpus[i].transition_cnt = dcvs.cpus[i].transition_cnt;
            p->dcvs.cpus[i].normalized_performance = dcvs.cpus[i].normalized_performance;
            p->dcvs.cpus[i].reserved = dcvs.cpus[i].reserved;
        }

        dcvs_event_t history[WMI_DCVS_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(
                dcvs_get_history(history, RTL_NUMBER_OF(history), &hist_cnt, &hist_dropped))) {
            p->dcvs.history_entry_cnt = hist_cnt;
            p->dcvs.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->dcvs.history); i++) {
                p->dcvs.history[i].tick100ns = history[i].tick100ns;
                p->dcvs.history[i].sequence = history[i].sequence;
                p->dcvs.history[i].event_code = history[i].event_code;
                p->dcvs.history[i].status = history[i].status;
                p->dcvs.history[i].cpu = history[i].cpu;
                p->dcvs.history[i].min_perf_state = history[i].min_perf_state;
                p->dcvs.history[i].max_perf_state = history[i].max_perf_state;
                p->dcvs.history[i].desired_perf_state = history[i].desired_perf_state;
                p->dcvs.history[i].applied_perf_level = history[i].applied_perf_level;
                p->dcvs.history[i].applied_freq_hz = history[i].applied_freq_hz;
                p->dcvs.history[i].thermal_cap_percent = history[i].thermal_cap_percent;
                p->dcvs.history[i].epp = history[i].epp;
                p->dcvs.history[i].aux0 = history[i].aux0;
                p->dcvs.history[i].aux1 = history[i].aux1;
            }
        }
    }

    {
        crash_snapshot_t crash;
        RtlZeroMemory(&crash, sizeof(crash));
        crash_dump_get_snapshot(&crash);

        p->crash_dump.version = crash.version;
        p->crash_dump.active_src = crash.active_src;
        p->crash_dump.in_use_src_cnt = crash.in_use_src_cnt;
        p->crash_dump.src_capacity = crash.src_capacity;
        p->crash_dump.create_cnt = crash.create_cnt;
        p->crash_dump.open_cnt = crash.open_cnt;
        p->crash_dump.destroy_cnt = crash.destroy_cnt;
        p->crash_dump.write_cnt = crash.write_cnt;
        p->crash_dump.read_cnt = crash.read_cnt;
        p->crash_dump.capture_cnt = crash.capture_cnt;
        p->crash_dump.live_kernel_create_cnt = crash.live_kernel_create_cnt;
        p->crash_dump.event_sequence = crash.event_sequence;
        p->crash_dump.history_cnt = crash.history_cnt;
        p->crash_dump.history_dropped = crash.history_dropped;
        p->crash_dump.last_event_code = crash.last_event_code;
        p->crash_dump.last_event_status = crash.last_event_status;
        p->crash_dump.last_event_src_idx = crash.last_event_src_idx;
        p->crash_dump.last_event_size = crash.last_event_size;
        p->crash_dump.last_event_tick100ns = crash.last_event_tick100ns;

        ULONG src_cnt = crash.src_capacity;
        if (src_cnt > RTL_NUMBER_OF(p->crash_dump.sources))
            src_cnt = RTL_NUMBER_OF(p->crash_dump.sources);
        p->crash_dump.src_entry_cnt = src_cnt;
        for (ULONG i = 0; i < src_cnt; i++) {
            p->crash_dump.sources[i].idx = crash.sources[i].idx;
            p->crash_dump.sources[i].in_use = crash.sources[i].in_use;
            p->crash_dump.sources[i].guid_valid = crash.sources[i].guid_valid;
            p->crash_dump.sources[i].src_guid = crash.sources[i].src_guid;
            p->crash_dump.sources[i].item_cnt = crash.sources[i].item_cnt;
            p->crash_dump.sources[i].entry_size = crash.sources[i].entry_size;
            p->crash_dump.sources[i].blob_len = crash.sources[i].blob_len;
            p->crash_dump.sources[i].blob_capacity = crash.sources[i].blob_capacity;
            p->crash_dump.sources[i].ring_read_idx = crash.sources[i].ring_read_idx;
            p->crash_dump.sources[i].ring_write_idx = crash.sources[i].ring_write_idx;
            p->crash_dump.sources[i].ring_items_present = crash.sources[i].ring_items_present;
            p->crash_dump.sources[i].last_write100ns = crash.sources[i].last_write100ns;
            p->crash_dump.sources[i].has_create_file_object =
                crash.sources[i].has_create_file_object;
            p->crash_dump.sources[i].has_open_file_object = crash.sources[i].has_open_file_object;
            p->crash_dump.sources[i].create_file_object_orphaned =
                crash.sources[i].create_file_object_orphaned;
        }

        crash_event_t history[WMI_CRASH_HISTORY_CNT];
        ULONG hist_cnt = 0;
        ULONG hist_dropped = 0;
        RtlZeroMemory(history, sizeof(history));
        if (NT_SUCCESS(crash_dump_get_history(history, RTL_NUMBER_OF(history), &hist_cnt,
                                              &hist_dropped))) {
            p->crash_dump.history_entry_cnt = hist_cnt;
            p->crash_dump.history_dropped = hist_dropped;
            for (ULONG i = 0; i < hist_cnt && i < RTL_NUMBER_OF(p->crash_dump.history); i++) {
                p->crash_dump.history[i].sequence = history[i].sequence;
                p->crash_dump.history[i].tick100ns = history[i].tick100ns;
                p->crash_dump.history[i].event_code = history[i].event_code;
                p->crash_dump.history[i].status = history[i].status;
                p->crash_dump.history[i].src_idx = history[i].src_idx;
                p->crash_dump.history[i].active_src = history[i].active_src;
                p->crash_dump.history[i].size = history[i].size;
                p->crash_dump.history[i].create_cnt = history[i].create_cnt;
                p->crash_dump.history[i].open_cnt = history[i].open_cnt;
                p->crash_dump.history[i].destroy_cnt = history[i].destroy_cnt;
                p->crash_dump.history[i].write_cnt = history[i].write_cnt;
                p->crash_dump.history[i].read_cnt = history[i].read_cnt;
                p->crash_dump.history[i].capture_cnt = history[i].capture_cnt;
                p->crash_dump.history[i].live_kernel_create_cnt = history[i].live_kernel_create_cnt;
            }
        }
    }

    *buf_used = sizeof(wmi_payload_t);
    return STATUS_SUCCESS;
}

static NTSTATUS wmi_set_instance(_In_ WDFWMIINSTANCE wmi_instance, _In_ ULONG in_buf_size,
                                 _In_reads_bytes_(in_buf_size) PVOID in_buf)
{
    UNREFERENCED_PARAMETER(wmi_instance);

    if (!in_buf || in_buf_size < sizeof(wmi_config_t))
        return STATUS_BUFFER_TOO_SMALL;

    const wmi_config_t* cfg = NULL;
    const wmi_payload_t* payload = NULL;
    if (in_buf_size >= sizeof(wmi_payload_t)) {
        payload = (const wmi_payload_t*)in_buf;
        cfg = &payload->cfg;
    } else {
        cfg = (const wmi_config_t*)in_buf;
    }

    if (!cfg)
        return STATUS_INVALID_PARAMETER;

    g.config.BranchTrackEnabled = cfg->branch_track_enabled;
    g.config.EnableDirectedPStateFeatures = cfg->enable_directed_p_state_features;
    g.config.EnableParkSelection = cfg->enable_park_selection;
    g.config.EnablePEPOCDTest = cfg->enable_pepocd_test;
    g.config.EnablePEPOCDTestLowPerf = cfg->enable_pepocd_test_low_perf;
    g.config.PepBcl_Notification_Level = cfg->pep_bcl_notify_level;

    (VOID) registry_set_u_long(L"BranchTrackEnabled", cfg->branch_track_enabled);
    (VOID)
        registry_set_u_long(L"EnableDirectedPStateFeatures", cfg->enable_directed_p_state_features);
    (VOID) registry_set_u_long(L"EnableParkSelection", cfg->enable_park_selection);
    (VOID) registry_set_u_long(L"EnablePEPOCDTest", cfg->enable_pepocd_test);
    (VOID) registry_set_u_long(L"EnablePEPOCDTestLowPerf", cfg->enable_pepocd_test_low_perf);
    (VOID) registry_set_u_long(L"PepBcl_Notification_Level", cfg->pep_bcl_notify_level);

    res_key_t k;
    res_value_t v;
    k.res_id = 0x30494D57u;
    k.sub_type = 1;
    v.value0 = cfg->branch_track_enabled;
    v.value1 = cfg->enable_directed_p_state_features;
    v.value2 = cfg->enable_park_selection;
    v.value3 = cfg->pep_bcl_notify_level;
    (VOID) res_set(k, v);

    if (payload && payload->version >= 14u && payload->park_sel.version != 0u) {
        parksel_policy_t policy;
        RtlZeroMemory(&policy, sizeof(policy));
        policy.version = 1u;
        policy.flags = 0x1Fu | 0x20u | 0x80u;
        policy.thermal_rank_enabled = payload->park_sel.thermal_rank_enabled;
        policy.thermal_rank_hysteresis_deci_c = payload->park_sel.thermal_rank_hysteresis_deci_c;
        policy.thermal_rank_update_interval_ms = payload->park_sel.thermal_rank_update_interval_ms;
        policy.thermal_sensor_base_idx = payload->park_sel.thermal_sensor_base_idx;
        policy.timer_threshold0_sec = payload->park_sel.timer_threshold0_sec;
        policy.timer_threshold1_sec = payload->park_sel.timer_threshold1_sec;
        (VOID) pep_set_park_selection_policy(&policy);
    }

    return STATUS_SUCCESS;
}

NTSTATUS wmi_init(_In_ WDFDEVICE dev)
{
    WDF_WMI_PROVIDER_CONFIG prov;
    WDF_WMI_INSTANCE_CONFIG inst;
    WDF_OBJECT_ATTRIBUTES attrs;
    WDFWMIPROVIDER provider;
    WDFWMIINSTANCE instance;

    WDF_WMI_PROVIDER_CONFIG_INIT(&prov, &guid_wmi_provider);
    prov.MinInstanceBufferSize = sizeof(wmi_payload_t);

    WDF_OBJECT_ATTRIBUTES_INIT(&attrs);

    NTSTATUS status = WdfWmiProviderCreate(dev, &prov, &attrs, &provider);
    if (!NT_SUCCESS(status))
        return status;

    WDF_WMI_INSTANCE_CONFIG_INIT_PROVIDER(&inst, provider);
    inst.Register = TRUE;
    inst.EvtWmiInstanceQueryInstance = wmi_query_instance;
    inst.EvtWmiInstanceSetInstance = wmi_set_instance;

    return WdfWmiInstanceCreate(dev, &inst, &attrs, &instance);
}
