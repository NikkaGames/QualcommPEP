#pragma once
#include <ntddk.h>

DEFINE_GUID(GUID_DEVINTERFACE_PEP_CONTROL, 0x2d9f7e41, 0x0f44, 0x4a5b, 0xa8, 0x72, 0x3a, 0x9e, 0x8f,
            0x2b, 0x55, 0x8c);

DEFINE_GUID(GUID_TSENS_QUERY, 0xc5a89f41, 0xe8a3, 0x4579, 0x9e, 0x09, 0xdb, 0x61, 0xb3, 0xa3, 0x9c,
            0x1e);

DEFINE_GUID(GUID_READ_STABLE_TSENS, 0xe1a92c2e, 0x4747, 0x4f69, 0xbe, 0xf7, 0x1b, 0xec, 0x83, 0x24,
            0x59, 0xa4);

DEFINE_GUID(GUID_DEVINTERFACE_BRANCH_TRACK, 0x1964f671, 0x9f87, 0x4d91, 0x93, 0x8e, 0x2b, 0x15,
            0x00, 0x2f, 0x24, 0x9b);

DEFINE_GUID(GUID_PEP_GPIO_DEVICE_INTERFACE, 0xb921b0c4, 0xdc55, 0x48da, 0xa0, 0x23, 0x65, 0xa5,
            0x9e, 0xe2, 0xe6, 0x2a);

DEFINE_GUID(GUID_PEP_TLMM_GPIO_INTERFACE, 0xa1a1a1a8, 0x899d, 0x4b9b, 0xb0, 0x89, 0xba, 0x84, 0xd1,
            0xda, 0x5c, 0xaf);

DEFINE_GUID(GUID_PEP_BCM_INTERFACE, 0x99f8bbdb, 0x5b65, 0x444b, 0xad, 0xbe, 0x5b, 0x87, 0xef, 0xed,
            0xd7, 0x6c);

DEFINE_GUID(GUID_DEVINTERFACE_SOC_PEP, 0x55beca03, 0xba2c, 0x4fbc, 0x9e, 0xfc, 0xee, 0x8e, 0xca,
            0x56, 0xf0, 0xc7);

DEFINE_GUID(GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE, 0xa0e1dd36, 0xdfb9, 0x4c0e, 0xbb, 0xaa,
            0x78, 0xf0, 0x74, 0x6b, 0xe1, 0x81);

DEFINE_GUID(GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2, 0x0919728e, 0x6bd7, 0x4a9c, 0x8f,
            0x3b, 0xc8, 0x35, 0x87, 0x3d, 0x4d, 0xae);

DEFINE_GUID(GUID_PEP_PSTATE_TRANSITION_INTERFACE, 0x99f7397b, 0x5b65, 0x444b, 0xad, 0xbe, 0x5b,
            0x87, 0xef, 0xed, 0xe7, 0x6c);

DEFINE_GUID(GUID_PEP_USB_WAKEUP_INTERRUPTS_INTERFACE, 0x902e0f43, 0xd28a, 0x49c4, 0xa8, 0xe0, 0x23,
            0xef, 0xce, 0xdc, 0xf7, 0xc7);

DEFINE_GUID(GUID_PEP_ENABLE_GFX_ACD_INTERFACE, 0xff4f9890, 0x6da8, 0x4beb, 0xa5, 0x15, 0xfc, 0xa7,
            0x45, 0x28, 0x6f, 0x3d);

#define CONTROL_DEVICE_NAME L"\\Device\\QCOM_PEPDriver"
#define MODEM_STANDBY_SYMBOLIC_LINK L"\\DosDevices\\QcDeepStandbyMdm"
#define THERMAL_GLOBAL_SYMBOLIC_LINK L"\\DosDevices\\Global\\PTHERMAL"
#define CONTROL_SYMBOLIC_LINK L"\\DosDevices\\QCOM_PEPDriver"
#define THERMAL_SYMBOLIC_LINK L"\\DosDevices\\PTHERMAL"
#define MODEM_STANDBY_GLOBAL_SYMBOLIC_LINK L"\\DosDevices\\Global\\QcDeepStandbyMdm"

#define IOCTL_PEP_PING CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_PORTID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_HID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_CONFIG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_STATS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RES_SNAPSHOT                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_ARM_WAKE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DISARM_WAKE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_DIAG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_GET_PEPLOG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_CLEAR_PEPLOG                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct ioctl_veto_operation {
    ULONG LprId;
    ULONG ModeId;
    ULONG VetoId;
} ioctl_veto_operation_t;

#define IOCTL_PEP_REGISTER_VETO_REASON                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_APPLY_VETO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_REMOVE_VETO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_QUERY_VETO_REFCOUNT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_SOC_SUBSYS_COUNT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_SOC_SUBSYS_QUERY                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_SOC_SUBSYS_RESET                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_SOC_SUBSYS_BLOCKTIME                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_SOC_SUBSYS_METADATA                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_INTERRUPT_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_INTERRUPT_HISTORY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_INTERRUPT_WAKECOUNTERS                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_INTERRUPT_LOOKUP                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x817, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_MMRM_SNAPSHOT                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_MMRM_HISTORY                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x819, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_MMRM_DOMAIN_QUERY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_TSENS_SNAPSHOT                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_TSENS_HISTORY                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_TSENS_SENSOR_RANGE                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_TSENS_SET_THRESHOLD                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_THERMALREQ_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_THERMALREQ_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DEEPSTANDBY_SNAPSHOT                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DEEPSTANDBY_HISTORY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DEEPSTANDBY_MODEM_GET                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_CRASHDUMP_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_CRASHDUMP_HISTORY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_CRASHDUMP_SOURCE_QUERY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x826, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_CRASHDUMP_SET_ACTIVE                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x827, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_BCL_SNAPSHOT                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x828, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_BCL_HISTORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x829, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_BCL_THRESHOLDS                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_BCL_SUBMIT_TEST                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_BCL_READSET_THRESHOLD                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DCVS_SNAPSHOT                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DCVS_HISTORY                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DCVS_PERF_TABLE                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_DCVS_SET_POLICY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_PARKSEL_SNAPSHOT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_PARKSEL_HISTORY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x832, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_PARKSEL_GET_POLICY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x833, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_PARKSEL_SET_POLICY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x834, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_EXSOC_SNAPSHOT                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x835, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_EXSOC_HISTORY                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x836, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SNAPSHOT                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x837, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_HISTORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x838, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SET_NODE                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x839, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_QUERY_NODE                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_APPLY_BATCH                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_GET_CATALOG                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_RUN_SCENARIO                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SCENARIO_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DIAG_SNAPSHOT                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DIAG_RUN_MATRIX                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DIAG_HISTORY                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROFILE_CATALOG                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x842, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROFILE_RUN                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x843, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROFILE_HISTORY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x844, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_GRAPH_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x845, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ACTION_SEQUENCE_EXECUTE                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x846, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ACTION_SEQUENCE_HISTORY                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x847, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CHECKPOINT_SNAPSHOT                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x848, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CHECKPOINT_CAPTURE                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x849, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CHECKPOINT_APPLY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CHECKPOINT_HISTORY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_SNAPSHOT                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_EXECUTE                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COVERAGE_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COVERAGE_ANALYZE                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x850, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COVERAGE_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x851, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_REMEDIATION_SNAPSHOT                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x852, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_REMEDIATION_RUN                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x853, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_REMEDIATION_HISTORY                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x854, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_STABILIZATION_SNAPSHOT                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x855, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_STABILIZATION_RUN                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x856, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_STABILIZATION_HISTORY                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x857, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CONFORMANCE_SNAPSHOT                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x858, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CONFORMANCE_RUN                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x859, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CONFORMANCE_HISTORY                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ORCHESTRATION_SNAPSHOT                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ORCHESTRATION_RUN                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_ORCHESTRATION_HISTORY                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_POLICY_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_POLICY_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_FABRIC_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x860, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_FABRIC_RUN                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x861, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_FABRIC_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x862, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DOMAIN_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x863, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DOMAIN_RUN                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x864, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_DOMAIN_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x865, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_INTERCONNECT_SNAPSHOT                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x866, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_INTERCONNECT_RUN                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x867, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_INTERCONNECT_HISTORY                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x868, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_TOPOLOGY_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x869, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_CATALOG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SYNC_SNAPSHOT                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SYNC_RUN                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SYNC_HISTORY                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_VALIDATE_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_VALIDATE_RUN                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_VALIDATE_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x870, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SEQUENCE_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x871, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SEQUENCE_RUN                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x872, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SEQUENCE_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x873, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_LOOKUP_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x874, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_LOOKUP_RUN                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x875, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_LOOKUP_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x876, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROVISION_SNAPSHOT                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x877, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROVISION_RUN                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x878, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_PROVISION_HISTORY                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x879, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_RAIL_SNAPSHOT                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_RAIL_RUN                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_RAIL_HISTORY                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_TCS_SNAPSHOT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_TCS_RUN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_TCS_HISTORY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_THERMAL_SNAPSHOT                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_THERMAL_RUN                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x881, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_THERMAL_HISTORY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x882, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COORD_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x883, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COORD_RUN                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x884, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_COORD_HISTORY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x885, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUDIT_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x886, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUDIT_RUN                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x887, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUDIT_HISTORY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x888, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SUPERVISOR_SNAPSHOT                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x889, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SUPERVISOR_RUN                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_SUPERVISOR_HISTORY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUTOTUNE_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUTOTUNE_RUN                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_AUTOTUNE_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_HARDEN_SNAPSHOT                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_HARDEN_RUN                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x890, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PEP_RSC_HARDEN_HISTORY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x891, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_PEP_VENDOR_88882000 0x88882000u
#define IOCTL_PEP_VENDOR_8888204C 0x8888204Cu
#define IOCTL_PEP_VENDOR_88882058 0x88882058u
#define IOCTL_PEP_VENDOR_8888205C 0x8888205Cu
#define IOCTL_PEP_VENDOR_88882080 0x88882080u
#define IOCTL_PEP_VENDOR_88882084 0x88882084u
#define IOCTL_PEP_VENDOR_88882088 0x88882088u
#define IOCTL_PEP_VENDOR_8888208C 0x8888208Cu
#define IOCTL_PEP_VENDOR_88882094 0x88882094u
#define IOCTL_PEP_VENDOR_88882098 0x88882098u
#define IOCTL_PEP_VENDOR_8888209C 0x8888209Cu
#define IOCTL_PEP_VENDOR_888820A4 0x888820A4u
#define IOCTL_PEP_VENDOR_88884084 0x88884084u
#define IOCTL_PEP_VENDOR_9C414000 0x9C414000u
#define IOCTL_PEP_VENDOR_C3502FF8 0xC3502FF8u

#define IOCTL_CLOCKDBG_TOTAL_BYTES 83984u

#define IOCTL_PEP_TSENS_GET_TEMPS 0x002A4004u
#define IOCTL_PEP_TSENS_GET_TEMP_FOR_IDX 0x002A4008u
#define IOCTL_PEP_TSENS_STABLE_REQ 0x002A4014u
#define IOCTL_PEP_THERMAL_READ_REQUEST 0x00294090u

#define IOCTL_PEP_MMRM_POLICY_NOTSUP 0x00294090u
#define IOCTL_PEP_MMRM_POLICY_READ 0x00294094u
#define IOCTL_PEP_BRANCHTRACK_QUERY 0x0022502Cu

#define IOCTL_PEP_VENDOR_9C414004 0x9C414004u
#define IOCTL_PEP_VENDOR_9C420400 0x9C420400u

typedef struct ioctl_config {
    ULONG BranchTrackEnabled;
    ULONG EnableDirectedPStateFeatures;
    ULONG EnableParkSelection;
    ULONG EnablePEPOCDTest;
    ULONG EnablePEPOCDTestLowPerf;
    ULONG PepBcl_Notification_Level;
} ioctl_config_t;

typedef struct ioctl_portid_response {
    ULONG PortId;
} ioctl_portid_response_t;

typedef struct ioctl_config_response {
    ioctl_config_t Config;
} ioctl_config_response_t;

typedef struct ioctl_stats_response {
    ULONG IoctlCount;
    ULONG PoFxPowerRequired;
    ULONG PoFxPowerNotRequired;
    ULONG PoFxComponentActive;
    ULONG PoFxComponentIdle;
    ULONG PoFxIdleState;
    ULONG D0Entry;
    ULONG D0Exit;
} ioctl_stats_response_t;

typedef struct ioctl_soc_subsys_query_request {
    ULONG Index;
} ioctl_soc_subsys_query_request_t;

typedef struct ioctl_soc_subsys_query_response {
    ULONG Index;
    ULONG Enabled;
    ULONG MetadataCount;
    ULONG WakeupCount;
    ULONGLONG BlockedDuration100ns;
    ULONGLONG SleepDuration100ns;
    WCHAR ParentName[32];
    WCHAR Name[96];
} ioctl_soc_subsys_query_response_t;

typedef struct ioctl_soc_subsys_block_request {
    ULONG Index;
} ioctl_soc_subsys_block_request_t;

typedef struct ioctl_soc_subsys_block_response {
    ULONGLONG BlockingTime100ns;
} ioctl_soc_subsys_block_response_t;

typedef struct ioctl_soc_subsys_meta_pair {
    WCHAR Key[48];
    WCHAR Value[64];
} ioctl_soc_subsys_meta_pair_t;

typedef struct ioctl_soc_subsys_meta_request {
    ULONG Index;
} ioctl_soc_subsys_meta_request_t;

typedef struct ioctl_soc_subsys_meta_response {
    ULONG Index;
    ULONG PairCount;
    ioctl_soc_subsys_meta_pair_t Pairs[4];
} ioctl_soc_subsys_meta_response_t;

#define IOCTL_INTERRUPT_PORT_CAPACITY 16u
#define IOCTL_INTERRUPT_WAKE_TOP_CAPACITY 16u

typedef struct ioctl_interrupt_port_info {
    ULONG PortId;
    ULONG IsrCount;
    ULONG DpcCount;
    ULONG EnableCount;
    ULONG DisableCount;
    ULONG DpcQueuedCount;
    ULONG LastMessageId;
    ULONGLONG LastIsrTick100ns;
    ULONGLONG LastDpcTick100ns;
} ioctl_interrupt_port_info_t;

typedef struct ioctl_interrupt_wake_counter_info {
    ULONG Offset;
    ULONG InterruptId;
    ULONG HitCount;
} ioctl_interrupt_wake_counter_info_t;

typedef struct ioctl_interrupt_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG PortId;
    ULONG MessageId;
    ULONG InterruptId;
    LONG MapOffset;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
} ioctl_interrupt_event_info_t;

typedef struct ioctl_interrupt_snapshot_response {
    ULONG Version;
    ULONG IsrCount;
    ULONG DpcCount;
    ULONG EnableCount;
    ULONG DisableCount;
    ULONG DpcQueuedCount;
    ULONG LastMessageId;
    ULONGLONG LastIsrTick100ns;
    ULONGLONG LastDpcTick100ns;
    ULONG IdleScanCount;
    ULONG IdleWakeSampleCount;
    ULONG LastIdleLevel;
    ULONG LastIdleModeIndex;
    ULONG LastWakeIrqStatus;
    ULONG LastWakeGeniStatus;
    ULONG LastWakeCmd0;
    ULONG LastWakeCmdCtrl;
    ULONG MapCount;
    ULONG MapHitCount;
    ULONG MapMissCount;
    ULONG LastLookupInterruptId;
    LONG LastLookupOffset;
    ULONG LastLookupTriggerType;
    ULONG LastLookupDriverId;
    ULONGLONG WakeBitHitCount;
    ULONG LastWakeOffset;
    ULONG LastWakeInterruptId;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONGLONG LastEventTick100ns;
    ULONG PortCount;
    ioctl_interrupt_port_info_t Ports[IOCTL_INTERRUPT_PORT_CAPACITY];
    ULONG WakeTopCount;
    ioctl_interrupt_wake_counter_info_t WakeTop[IOCTL_INTERRUPT_WAKE_TOP_CAPACITY];
} ioctl_interrupt_snapshot_response_t;

typedef struct ioctl_interrupt_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_interrupt_event_info_t Entries[1];
} ioctl_interrupt_history_response_t;

typedef struct ioctl_interrupt_wakecounters_response {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ioctl_interrupt_wake_counter_info_t Entries[1];
} ioctl_interrupt_wakecounters_response_t;

typedef struct ioctl_interrupt_lookup_request {
    ULONG InterruptId;
} ioctl_interrupt_lookup_request_t;

typedef struct ioctl_interrupt_lookup_response {
    ULONG InterruptId;
    LONG Offset;
    ULONG TriggerType;
    ULONG DriverId;
    ULONG Flags;
    LONG Status;
} ioctl_interrupt_lookup_response_t;

#define IOCTL_MMRM_DOMAIN_CAPACITY 12u

typedef struct ioctl_mmrm_domain_info {
    ULONG FrequencyKhz;
    ULONG PowerMilliWatts;
    ULONG Flags;
    ULONG DynPermille;
    ULONG LkgPermille;
    LONG TempBiasDeciC;
} ioctl_mmrm_domain_info_t;

typedef struct ioctl_mmrm_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG Reason;
    ULONG Percent;
    ULONG Updated;
    ULONG PeakMilliAmps;
    LONG TempDeciK;
    LONG LowTempDeciK;
    LONG HighTempDeciK;
    ULONG PassiveLimit;
    ULONG ActivationReasons;
    ULONG FeatureEnabled;
    ULONG DsmFlags;
} ioctl_mmrm_event_info_t;

typedef struct ioctl_mmrm_snapshot_response {
    ULONG Version;
    ULONG FeatureEnabled;
    ULONG ReservedPowerMilliWatts;
    ULONG SamplingPeriodMilliSeconds;
    ULONG MaxThrottlePercentage;
    ULONG MitigationStartThresholdMilliAmps;
    ULONG MitigationStopThresholdMilliAmps;
    ULONG TemperatureSensorId;
    ULONG VideoEncodingState;
    ULONG VideoDecodingState;
    ULONG LastTotalPowerMilliWatts;
    ULONG LastPeakMilliAmps;
    ULONG ThrottleReason;
    ULONG ThrottlePercentage;
    ULONG Updated;
    ULONG LastPassiveLimit;
    ULONG LastActivationReasons;
    ULONG LastVersion;
    ULONG PolicyWaitActive;
    ULONG SamplingTimerRunning;
    ULONG DsmFlags;
    ULONG DsmRefreshCount;
    ULONG LastDsmStatus;
    ULONG DsmPollIntervalMilliSeconds;
    ULONG LastDsmPolicyItemCount;
    ULONG LastDsmClockItemCount;
    ULONG PeakVoltageMilliVolts;
    ULONG GlobalDynPermille;
    ULONG GlobalLkgPermille;
    ULONG LastCameraPowerMilliWatts;
    ULONG LastVideoPowerMilliWatts;
    ULONG LastDisplayPowerMilliWatts;
    ULONG DomainCount;
    LONG CurrentTempDeciK;
    LONG ThresholdLowDeciK;
    LONG ThresholdHighDeciK;
    LONG CameraTempDeciC;
    LONG VideoTempDeciC;
    LONG DisplayTempDeciC;
    ioctl_mmrm_domain_info_t Domains[IOCTL_MMRM_DOMAIN_CAPACITY];
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastEventReason;
    ULONG LastEventPercent;
    ULONG LastEventUpdated;
    ULONG LastEventPeakMilliAmps;
    LONG LastEventTempDeciK;
    LONG LastEventLowTempDeciK;
    LONG LastEventHighTempDeciK;
    ULONGLONG LastEventTick100ns;
} ioctl_mmrm_snapshot_response_t;

typedef struct ioctl_mmrm_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_mmrm_event_info_t Entries[1];
} ioctl_mmrm_history_response_t;

typedef struct ioctl_mmrm_domain_query_request {
    ULONG Index;
} ioctl_mmrm_domain_query_request_t;

typedef struct ioctl_mmrm_domain_query_response {
    ULONG Index;
    LONG Status;
    ULONG DomainCount;
    ioctl_mmrm_domain_info_t Domain;
} ioctl_mmrm_domain_query_response_t;

#define IOCTL_TSENS_SENSOR_CAPACITY 64u

typedef struct ioctl_tsens_sensor_info {
    LONG LastTempDeciC;
    LONG MinTempDeciC;
    LONG MaxTempDeciC;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG LowHitCount;
    ULONG HighHitCount;
    ULONG Flags;
} ioctl_tsens_sensor_info_t;

typedef struct ioctl_tsens_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG SensorId;
    LONG TempDeciC;
    LONG LowDeciC;
    LONG HighDeciC;
    ULONG Aux0;
    ULONG Aux1;
} ioctl_tsens_event_info_t;

typedef struct ioctl_tsens_snapshot_response {
    ULONG Version;
    ULONG SensorCount;
    LONG AggregateTempDeciC;
    ULONGLONG ReadCount;
    ULONGLONG DsmRefreshCount;
    ULONGLONG ThresholdHitCount;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastSensorId;
    LONG LastTempDeciC;
    LONG LastLowDeciC;
    LONG LastHighDeciC;
    ULONGLONG LastEventTick100ns;
    ULONG SensorEntryCount;
    ioctl_tsens_sensor_info_t Sensors[IOCTL_TSENS_SENSOR_CAPACITY];
} ioctl_tsens_snapshot_response_t;

typedef struct ioctl_tsens_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_tsens_event_info_t Entries[1];
} ioctl_tsens_history_response_t;

typedef struct ioctl_tsens_sensor_range_request {
    ULONG SensorId;
} ioctl_tsens_sensor_range_request_t;

typedef struct ioctl_tsens_sensor_range_response {
    ULONG SensorId;
    LONG MinTempDeciC;
    LONG MaxTempDeciC;
    LONG Status;
} ioctl_tsens_sensor_range_response_t;

typedef struct ioctl_tsens_set_threshold_request {
    ULONG SensorId;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG Flags;
} ioctl_tsens_set_threshold_request_t;

typedef struct ioctl_tsens_set_threshold_response {
    ULONG SensorId;
    LONG Status;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG Flags;
} ioctl_tsens_set_threshold_response_t;

typedef struct ioctl_thermalreq_event_info {
    ULONGLONG Sequence;
    ULONGLONG Tick100ns;
    ULONG EventCode;
    LONG Status;
    ULONG RequestId;
    ULONG SensorId;
    LONG TempDeciK;
    LONG LowTempDeciK;
    LONG HighTempDeciK;
    ULONG ActiveCount;
    ULONG PortId;
} ioctl_thermalreq_event_info_t;

typedef struct ioctl_thermalreq_snapshot_response {
    ULONG Version;
    ULONG PollMs;
    ULONG ActiveCount;
    ULONG LastRequestId;
    ULONGLONG TotalQueued;
    ULONGLONG TotalCompleted;
    ULONGLONG TotalCancelled;
    ULONGLONG TotalTimeout;
    ULONGLONG TotalLowThresholdHits;
    ULONGLONG TotalHighThresholdHits;
    ULONGLONG TotalBusyRejected;
    ULONGLONG TotalNoSlotRejected;
    ULONGLONG TotalMarkCancelableFailures;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastEventRequestId;
    ULONG LastEventSensorId;
    LONG LastEventTempDeciK;
    LONG LastEventLowTempDeciK;
    LONG LastEventHighTempDeciK;
    ULONGLONG LastEventTick100ns;
} ioctl_thermalreq_snapshot_response_t;

typedef struct ioctl_thermalreq_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_thermalreq_event_info_t Entries[1];
} ioctl_thermalreq_history_response_t;

typedef struct ioctl_deepstandby_event_info {
    ULONGLONG Tick100ns;
    ULONG EventCode;
    ULONG Status;
    ULONG PrevMsOnDcPwr;
    ULONG CurrMsOnDcPwr;
    ULONG PrevDeepStandbyState;
    ULONG CurrDeepStandbyState;
    ULONG Aux0;
    ULONG Aux1;
} ioctl_deepstandby_event_info_t;

typedef struct ioctl_deepstandby_snapshot_response {
    ULONG Version;
    ULONG FeatureEnabled;
    ULONG CallbacksEnabled;
    ULONG ModemEnabled;
    ULONG WlanEnabled;
    ULONG PowerSource;
    ULONG ScreenState;
    ULONG UserState;
    ULONG MsOnDcPwr;
    ULONG DeepStandbyState;
    ULONG TimerExpired;
    ULONG BattDrainExceededLimit;
    ULONG BattDrainSinceTmrStart;
    ULONG TmrStartingBattPct;
    ULONG CurrentBattPct;
    ULONG MaxBattDrainPct;
    ULONG TimeoutSeconds;
    ULONG ModemReq;
    ULONG EvaluateCount;
    ULONG CallbackCount;
    ULONG StateTransitionCount;
    ULONG EnterCount;
    ULONG ExitCount;
    ULONG WlanIoctlErrors;
    ULONG ModemIoctlErrors;
    ULONG LastWlanIoctlStatus;
    ULONG LastModemIoctlStatus;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    ULONG LastEventStatus;
    ULONGLONG TmrStartTime100ns;
    ULONGLONG DeepStandbyDeadline100ns;
    ULONGLONG LastEvaluateTick100ns;
    ULONGLONG LastTransitionTick100ns;
} ioctl_deepstandby_snapshot_response_t;

typedef struct ioctl_deepstandby_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_deepstandby_event_info_t Entries[1];
} ioctl_deepstandby_history_response_t;

typedef struct ioctl_deepstandby_modem_get_response {
    ULONG ModemReq;
} ioctl_deepstandby_modem_get_response_t;

#define IOCTL_CRASHDUMP_SOURCE_CAPACITY 8u

typedef struct ioctl_crashdump_source_info {
    ULONG Index;
    ULONG InUse;
    ULONG GuidValid;
    GUID SourceGuid;
    ULONG ItemCount;
    ULONG EntrySize;
    ULONG BlobLen;
    ULONG BlobCapacity;
    ULONG RingReadIndex;
    ULONG RingWriteIndex;
    ULONG RingItemsPresent;
    ULONGLONG LastWrite100ns;
    ULONG HasCreateFileObject;
    ULONG HasOpenFileObject;
    ULONG CreateFileObjectOrphaned;
} ioctl_crashdump_source_info_t;

typedef struct ioctl_crashdump_event_info {
    ULONGLONG Sequence;
    ULONGLONG Tick100ns;
    ULONG EventCode;
    LONG Status;
    ULONG SourceIndex;
    ULONG ActiveSource;
    ULONG Size;
    ULONG CreateCount;
    ULONG OpenCount;
    ULONG DestroyCount;
    ULONG WriteCount;
    ULONG ReadCount;
    ULONG CaptureCount;
    ULONG LiveKernelCreateCount;
} ioctl_crashdump_event_info_t;

typedef struct ioctl_crashdump_snapshot_response {
    ULONG Version;
    ULONG ActiveSource;
    ULONG InUseSourceCount;
    ULONG SourceCapacity;
    ULONG CreateCount;
    ULONG OpenCount;
    ULONG DestroyCount;
    ULONG WriteCount;
    ULONG ReadCount;
    ULONG CaptureCount;
    ULONG LiveKernelCreateCount;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastEventSourceIndex;
    ULONG LastEventSize;
    ULONGLONG LastEventTick100ns;
    ioctl_crashdump_source_info_t Sources[IOCTL_CRASHDUMP_SOURCE_CAPACITY];
} ioctl_crashdump_snapshot_response_t;

typedef struct ioctl_crashdump_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_crashdump_event_info_t Entries[1];
} ioctl_crashdump_history_response_t;

typedef struct ioctl_crashdump_source_query_request {
    ULONG Index;
} ioctl_crashdump_source_query_request_t;

typedef struct ioctl_crashdump_source_query_response {
    ULONG Index;
    LONG Status;
    ULONG ActiveSource;
    ioctl_crashdump_source_info_t Source;
} ioctl_crashdump_source_query_response_t;

typedef struct ioctl_crashdump_set_active_request {
    ULONG Index;
} ioctl_crashdump_set_active_request_t;

typedef struct ioctl_crashdump_set_active_response {
    ULONG Index;
    LONG Status;
    ULONG ActiveSource;
} ioctl_crashdump_set_active_response_t;

#define IOCTL_BCL_THRESHOLD_CAPACITY 64u

typedef struct ioctl_bcl_threshold_info {
    ULONG ResourceId;
    ULONG Field;
    ULONG SubType;
    ULONG Value0;
    ULONG Value1;
    ULONG UpdateCount;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
    UCHAR Flag;
    UCHAR Reserved0;
    USHORT Reserved1;
} ioctl_bcl_threshold_info_t;

typedef struct ioctl_bcl_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG NotificationType;
    ULONG ParkingState;
    ULONG DesiredParkedCpus;
    ULONG ResourceId;
    ULONG Field;
    ULONG SubType;
    ULONG Value0;
    ULONG Value1;
    ULONG Flag;
    ULONG Aux0;
} ioctl_bcl_event_info_t;

typedef struct ioctl_bcl_snapshot_response {
    ULONG Version;
    ULONG TestEnabled;
    ULONG CurrentNotificationType;
    ULONG ParkingState;
    ULONG DesiredParkedCpus;
    ULONG CurrentParkedCpus;
    ULONG ThrottlePermille;
    ULONG IsLowImaxState;
    ULONG MitigationState;
    ULONG ThresholdCount;
    ULONGLONG SubmitCount;
    ULONGLONG SubmitAccepted;
    ULONGLONG SubmitRejected;
    ULONGLONG WorkItemRunCount;
    ULONGLONG ThresholdOpCount;
    ULONGLONG ThresholdDownstackSuccess;
    ULONGLONG ThresholdDownstackFailure;
    ULONGLONG ThresholdCacheHits;
    ULONGLONG ThresholdCacheMisses;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONGLONG LastEventTick100ns;
    ULONG LastResourceId;
    ULONG LastField;
    ULONG LastSubType;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastFlag;
    ULONG LastAux0;
    ioctl_bcl_threshold_info_t Thresholds[IOCTL_BCL_THRESHOLD_CAPACITY];
} ioctl_bcl_snapshot_response_t;

typedef struct ioctl_bcl_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_bcl_event_info_t Entries[1];
} ioctl_bcl_history_response_t;

typedef struct ioctl_bcl_thresholds_response {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ioctl_bcl_threshold_info_t Entries[1];
} ioctl_bcl_thresholds_response_t;

typedef struct ioctl_bcl_submit_test_request {
    ULONG NotificationType;
} ioctl_bcl_submit_test_request_t;

typedef struct ioctl_bcl_submit_test_response {
    ULONG NotificationType;
    LONG Status;
    ULONG CurrentNotificationType;
    ULONG ParkingState;
    ULONG DesiredParkedCpus;
    ULONG CurrentParkedCpus;
    ULONG ThrottlePermille;
} ioctl_bcl_submit_test_response_t;

typedef struct ioctl_bcl_readset_threshold_request {
    ULONG ResourceId;
    ULONG Field;
    ULONG Threshold;
    ULONG Value0;
    ULONG Flags;
} ioctl_bcl_readset_threshold_request_t;

typedef struct ioctl_bcl_readset_threshold_response {
    ULONG ResourceId;
    ULONG Field;
    ULONG Threshold;
    ULONG Value0;
    ULONG Flags;
    LONG Status;
} ioctl_bcl_readset_threshold_response_t;

#define IOCTL_DCVS_CPU_INFO_CAPACITY 64u
#define IOCTL_DCVS_PERF_TABLE_CAPACITY 64u

typedef struct ioctl_dcvs_cpu_info {
    ULONG Cpu;
    ULONG DomainIndex;
    ULONG CurrentPerfLevel;
    ULONG CurrentFrequencyHz;
    ULONG RequestedPerfStateIndex;
    ULONG MinPerf;
    ULONG MaxPerf;
    ULONG DesiredPerf;
    ULONG Epp;
    ULONGLONG EnterTime100ns;
    ULONGLONG Residency100ns;
    ULONGLONG TransitionCount;
    ULONG NormalizedPerformance;
    ULONG Reserved;
} ioctl_dcvs_cpu_info_t;

typedef struct ioctl_dcvs_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG Cpu;
    ULONG MinPerfState;
    ULONG MaxPerfState;
    ULONG DesiredPerfState;
    ULONG AppliedPerfLevel;
    ULONG AppliedFrequencyHz;
    ULONG ThermalCapPercent;
    ULONG Epp;
    ULONG Aux0;
    ULONG Aux1;
} ioctl_dcvs_event_info_t;

typedef struct ioctl_dcvs_snapshot_response {
    ULONG Version;
    ULONG Initialized;
    ULONG CpuCount;
    ULONG DomainId;
    ULONG DomainMembers;
    ULONG CoordinationType;
    ULONG WorstCaseTransitionLatency;
    ULONG WorstCaseTransitionOverhead;
    ULONG EnablePerfSelection;
    ULONG EnableThermalPerfSelection;
    ULONG EnableBoost;
    ULONG BoostFrequencyHz;
    ULONG MaxFrequencyHz;
    ULONG MinFrequencyHz;
    ULONG NominalFrequencyHz;
    ULONG LowestNonlinearFrequencyHz;
    ULONG ThermalHotDeciC;
    ULONG ThermalCriticalDeciC;
    ULONG ThermalCapHotPercent;
    ULONG ThermalCapCriticalPercent;
    ULONG CurrentThermalCapPercent;
    ULONG PerfTableCount;
    ULONG PerfTableHz[IOCTL_DCVS_PERF_TABLE_CAPACITY];
    ULONG CpuEntryCount;
    ioctl_dcvs_cpu_info_t Cpus[IOCTL_DCVS_CPU_INFO_CAPACITY];
    ULONG PolicyUpdateCount;
    ULONGLONG LastPolicyTick100ns;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastEventCpu;
    ULONG LastEventMinPerfState;
    ULONG LastEventMaxPerfState;
    ULONG LastEventDesiredPerfState;
    ULONG LastEventAppliedPerfLevel;
    ULONG LastEventAppliedFrequencyHz;
    ULONG LastEventThermalCapPercent;
    ULONG LastEventEpp;
    ULONG LastEventAux0;
    ULONG LastEventAux1;
    ULONGLONG LastEventTick100ns;
} ioctl_dcvs_snapshot_response_t;

typedef struct ioctl_dcvs_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_dcvs_event_info_t Entries[1];
} ioctl_dcvs_history_response_t;

typedef struct ioctl_dcvs_perf_table_response {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ULONG Entries[1];
} ioctl_dcvs_perf_table_response_t;

typedef struct ioctl_dcvs_set_policy_request {
    ULONG Flags;
    ULONG EnablePerfSelection;
    ULONG EnableThermalPerfSelection;
    ULONG ThermalHotDeciC;
    ULONG ThermalCriticalDeciC;
    ULONG ThermalCapHotPercent;
    ULONG ThermalCapCriticalPercent;
} ioctl_dcvs_set_policy_request_t;

typedef struct ioctl_dcvs_set_policy_response {
    ULONG Flags;
    LONG Status;
    ULONG AppliedEnablePerfSelection;
    ULONG AppliedEnableThermalPerfSelection;
    ULONG AppliedThermalHotDeciC;
    ULONG AppliedThermalCriticalDeciC;
    ULONG AppliedThermalCapHotPercent;
    ULONG AppliedThermalCapCriticalPercent;
    ULONG CurrentThermalCapPercent;
    ULONG PolicyUpdateCount;
    ULONGLONG LastPolicyTick100ns;
} ioctl_dcvs_set_policy_response_t;

#define IOCTL_PARKSEL_CPU_INFO_CAPACITY 8u

typedef struct ioctl_parksel_cpu_info {
    ULONG Cpu;
    UCHAR CurrentParkState;
    UCHAR PreviousParkState;
    UCHAR CurrentPreference;
    UCHAR CurrentRank;
    ULONG TransitionCount;
    LONG TemperatureDeciC;
} ioctl_parksel_cpu_info_t;

typedef struct ioctl_parksel_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG EventCode;
    LONG Status;
    ULONG Cpu;
    ULONG CpuCount;
    ULONG AdditionalUnparked;
    ULONG RemainingUnparked;
    ULONG EvaluationType;
    ULONGLONG EvaluationTime;
    ULONG Cluster0Unparked;
    ULONG Cluster1Unparked;
    ULONG Aux0;
    ULONG Aux1;
} ioctl_parksel_event_info_t;

typedef struct ioctl_parksel_snapshot_response {
    ULONG Version;
    ULONG CpuCount;
    ULONG ActiveCpuCount;
    ULONG LastAdditionalUnparked;
    ULONG LastEvaluationType;
    ULONGLONG LastEvaluationTime;
    ULONG TimerThreshold0Sec;
    ULONG TimerThreshold1Sec;
    ULONG CoreRotationStat0;
    ULONG CoreRotationStat1;
    ULONG Cluster0Unparked;
    ULONG Cluster1Unparked;
    ULONG PrefRailId;
    ULONG TimerRailId;
    ULONG NumUnparkRailId;
    ULONG CoreRotationRailId;
    ULONGLONG LastCluster0EvaluationTime;
    ULONGLONG LastCluster1EvaluationTime;
    ULONG ToggleState;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONG LastEventAdditionalUnparked;
    ULONG LastEventRemainingUnparked;
    ULONGLONG LastEventTick100ns;
    ULONG ThermalRankEnabled;
    LONG ThermalRankHysteresisDeciC;
    ULONG ThermalRankUpdateIntervalMs;
    ULONG ThermalSensorBaseIndex;
    ULONG ThermalRefreshCount;
    ULONG ThermalApplyCount;
    LONG LastThermalStatus;
    ULONG RankEntryCount;
    UCHAR RankPreference[IOCTL_PARKSEL_CPU_INFO_CAPACITY];
    UCHAR PreviousRankPreference[IOCTL_PARKSEL_CPU_INFO_CAPACITY];
    LONG RankCpuTempDeciC[IOCTL_PARKSEL_CPU_INFO_CAPACITY];
    ULONG CpuEntryCount;
    ioctl_parksel_cpu_info_t Cpus[IOCTL_PARKSEL_CPU_INFO_CAPACITY];
} ioctl_parksel_snapshot_response_t;

typedef struct ioctl_parksel_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_parksel_event_info_t Entries[1];
} ioctl_parksel_history_response_t;

typedef struct ioctl_parksel_policy_request {
    ULONG Version;
    ULONG Flags;
    ULONG ThermalRankEnabled;
    LONG ThermalRankHysteresisDeciC;
    ULONG ThermalRankUpdateIntervalMs;
    ULONG ThermalSensorBaseIndex;
    ULONG TimerThreshold0Sec;
    ULONG TimerThreshold1Sec;
} ioctl_parksel_policy_request_t;

typedef struct ioctl_parksel_policy_response {
    ULONG Version;
    LONG Status;
    ULONG AppliedFlags;
    ULONG ThermalRankEnabled;
    LONG ThermalRankHysteresisDeciC;
    ULONG ThermalRankUpdateIntervalMs;
    ULONG ThermalSensorBaseIndex;
    ULONG TimerThreshold0Sec;
    ULONG TimerThreshold1Sec;
    ULONG ThermalRefreshCount;
    ULONG ThermalApplyCount;
    LONG LastThermalStatus;
} ioctl_parksel_policy_response_t;

#define IOCTL_EXSOC_DEVICE_INFO_CAPACITY 32u
#define IOCTL_EXSOC_HISTORY_INFO_CAPACITY 64u

typedef struct ioctl_exsoc_device_info {
    ULONG DeviceHash;
    CHAR DeviceId[21];
    ULONG DeviceIdLength;
    ULONG LastDState;
    ULONG LastIoctlCode;
    ULONG ApplyCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONGLONG LastTick100ns;
} ioctl_exsoc_device_info_t;

typedef struct ioctl_exsoc_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG IoctlCode;
    LONG Status;
    ULONG DeviceHash;
    ULONG DState;
    ULONG RequestedOutputBytes;
    ULONG ReturnedInformation;
} ioctl_exsoc_event_info_t;

typedef struct ioctl_exsoc_snapshot_response {
    ULONG Version;
    ULONG DeviceCount;
    ULONG ApplyCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONG AdspStateIoctlCount;
    ULONG AdspCtrlIoctlCount;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastIoctlCode;
    LONG LastStatus;
    ULONG LastDeviceHash;
    ULONG LastDState;
    ULONGLONG LastTick100ns;
    ULONG DeviceEntryCount;
    ioctl_exsoc_device_info_t Devices[IOCTL_EXSOC_DEVICE_INFO_CAPACITY];
} ioctl_exsoc_snapshot_response_t;

typedef struct ioctl_exsoc_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_exsoc_event_info_t Entries[1];
} ioctl_exsoc_history_response_t;

#define IOCTL_RSC_SUBSYS_AOP 0u
#define IOCTL_RSC_SUBSYS_ARC 1u
#define IOCTL_RSC_SUBSYS_CLOCK 2u
#define IOCTL_RSC_SUBSYS_CMDDB 3u
#define IOCTL_RSC_SUBSYS_DAL 4u
#define IOCTL_RSC_SUBSYS_GLINK 5u
#define IOCTL_RSC_SUBSYS_ICB 6u
#define IOCTL_RSC_SUBSYS_NPA 7u
#define IOCTL_RSC_SUBSYS_PCU 8u
#define IOCTL_RSC_SUBSYS_PDC 9u
#define IOCTL_RSC_SUBSYS_PPP 10u
#define IOCTL_RSC_SUBSYS_RPMH 11u
#define IOCTL_RSC_SUBSYS_RSC 12u
#define IOCTL_RSC_SUBSYS_TLMM 13u
#define IOCTL_RSC_SUBSYS_TSENS 14u
#define IOCTL_RSC_SUBSYS_ARCMGR 15u
#define IOCTL_RSC_SUBSYS_COUNT 16u

#define IOCTL_RSC_NODE_INFO_CAPACITY 16u
#define IOCTL_RSC_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_CATALOG_INFO_CAPACITY 32u
#define IOCTL_RSC_SCENARIO_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_DIAG_INFO_CAPACITY 16u
#define IOCTL_RSC_DIAG_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_PROFILE_INFO_CAPACITY 16u
#define IOCTL_RSC_PROFILE_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_GRAPH_LINK_INFO_CAPACITY 64u
#define IOCTL_RSC_ACTION_SEQUENCE_STEP_INFO_CAPACITY 64u
#define IOCTL_RSC_ACTION_SEQUENCE_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_CHECKPOINT_INFO_CAPACITY 16u
#define IOCTL_RSC_CHECKPOINT_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_COVERAGE_INFO_CAPACITY 16u
#define IOCTL_RSC_COVERAGE_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_REMEDIATION_INFO_CAPACITY 32u
#define IOCTL_RSC_REMEDIATION_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_STABILIZATION_LANE_INFO_CAPACITY 4u
#define IOCTL_RSC_STABILIZATION_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_CONFORMANCE_INFO_CAPACITY 16u
#define IOCTL_RSC_CONFORMANCE_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_ORCHESTRATION_STAGE_INFO_CAPACITY 24u
#define IOCTL_RSC_ORCHESTRATION_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_POLICY_RESOURCE_INFO_CAPACITY 96u
#define IOCTL_RSC_POLICY_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_FABRIC_LANE_INFO_CAPACITY 16u
#define IOCTL_RSC_FABRIC_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_DOMAIN_INFO_CAPACITY 8u
#define IOCTL_RSC_DOMAIN_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_INTERCONNECT_ROUTE_INFO_CAPACITY 48u
#define IOCTL_RSC_INTERCONNECT_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_TOPOLOGY_NODE_INFO_CAPACITY 16u
#define IOCTL_RSC_TOPOLOGY_ROUTE_INFO_CAPACITY 48u
#define IOCTL_RSC_SUBSYSTEM_INFO_CAPACITY 16u
#define IOCTL_RSC_AOP_SUBSYSTEM_INFO_CAPACITY 8u
#define IOCTL_RSC_ARC_RESOURCE_INFO_CAPACITY 16u
#define IOCTL_RSC_CMDDB_SLV_INFO_CAPACITY 8u
#define IOCTL_RSC_PDC_DRV_INFO_CAPACITY 8u
#define IOCTL_RSC_PDC_TRIGGER_INFO_CAPACITY 16u
#define IOCTL_RSC_RPMH_SET_INFO_CAPACITY 8u
#define IOCTL_RSC_NPA_CLIENT_INFO_CAPACITY 24u
#define IOCTL_RSC_GLINK_EVENT_INFO_CAPACITY 48u
#define IOCTL_RSC_TLMM_DRIVE_INFO_CAPACITY 16u
#define IOCTL_RSC_TSENS_THRESHOLD_INFO_CAPACITY 8u
#define IOCTL_RSC_CLOCK_DRIVER_INFO_CAPACITY 8u
#define IOCTL_RSC_CMDDB_QUERY_INFO_CAPACITY 8u
#define IOCTL_RSC_SYNC_LANE_INFO_CAPACITY 16u
#define IOCTL_RSC_SYNC_HISTORY_INFO_CAPACITY 96u
#define IOCTL_RSC_VALIDATE_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_VALIDATE_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_SEQUENCE_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_SEQUENCE_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY 64u
#define IOCTL_RSC_LOOKUP_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_PROVISION_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_PROVISION_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_RAIL_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_RAIL_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_TCS_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_TCS_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_THERMAL_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_THERMAL_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_COORD_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_COORD_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_AUDIT_ROW_INFO_CAPACITY 16u
#define IOCTL_RSC_AUDIT_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_SUPERVISOR_MODULE_INFO_CAPACITY 16u
#define IOCTL_RSC_SUPERVISOR_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_AUTOTUNE_MODULE_INFO_CAPACITY 16u
#define IOCTL_RSC_AUTOTUNE_HISTORY_INFO_CAPACITY 128u
#define IOCTL_RSC_HARDEN_MODULE_INFO_CAPACITY 16u
#define IOCTL_RSC_HARDEN_HISTORY_INFO_CAPACITY 128u

#define IOCTL_RSC_SYNC_PROFILE_BOOT 1u
#define IOCTL_RSC_SYNC_PROFILE_BALANCED 2u
#define IOCTL_RSC_SYNC_PROFILE_PERFORMANCE 3u
#define IOCTL_RSC_SYNC_PROFILE_INTERCONNECT 4u
#define IOCTL_RSC_SYNC_PROFILE_THERMAL 5u

#define IOCTL_RSC_SYNC_FLAG_FORCE_ENABLE 0x00000001u
#define IOCTL_RSC_SYNC_FLAG_RUN_INTERCONNECT 0x00000002u
#define IOCTL_RSC_SYNC_FLAG_RUN_DOMAIN 0x00000004u
#define IOCTL_RSC_SYNC_FLAG_THERMAL_GUARD 0x00000008u
#define IOCTL_RSC_SYNC_FLAG_VERIFY_QUERY 0x00000010u

#define IOCTL_RSC_VALIDATE_MODE_BASELINE 1u
#define IOCTL_RSC_VALIDATE_MODE_STRICT 2u
#define IOCTL_RSC_VALIDATE_MODE_THERMAL 3u

#define IOCTL_RSC_VALIDATE_FLAG_REPAIR_STATE 0x00000001u
#define IOCTL_RSC_VALIDATE_FLAG_REPAIR_CLOCK 0x00000002u
#define IOCTL_RSC_VALIDATE_FLAG_REPAIR_POLICY 0x00000004u
#define IOCTL_RSC_VALIDATE_FLAG_VERIFY_ICB 0x00000008u
#define IOCTL_RSC_VALIDATE_FLAG_VERIFY_ENUM_NAMES 0x00000010u

#define IOCTL_RSC_VALIDATE_ISSUE_ARC 0x00000001u
#define IOCTL_RSC_VALIDATE_ISSUE_CMDDB 0x00000002u
#define IOCTL_RSC_VALIDATE_ISSUE_PDC 0x00000004u
#define IOCTL_RSC_VALIDATE_ISSUE_RPMH 0x00000008u
#define IOCTL_RSC_VALIDATE_ISSUE_NPA 0x00000010u
#define IOCTL_RSC_VALIDATE_ISSUE_GLINK 0x00000020u
#define IOCTL_RSC_VALIDATE_ISSUE_PPP 0x00000040u
#define IOCTL_RSC_VALIDATE_ISSUE_TLMM 0x00000080u
#define IOCTL_RSC_VALIDATE_ISSUE_TSENS 0x00000100u
#define IOCTL_RSC_VALIDATE_ISSUE_CLOCK 0x00000200u
#define IOCTL_RSC_VALIDATE_ISSUE_AOP 0x00000400u
#define IOCTL_RSC_VALIDATE_ISSUE_ICB 0x00000800u
#define IOCTL_RSC_VALIDATE_ISSUE_THERMAL 0x00001000u

#define IOCTL_RSC_VALIDATE_WARN_ICB_NAME 0x00000001u
#define IOCTL_RSC_VALIDATE_WARN_RPMH_PROFILE 0x00000002u
#define IOCTL_RSC_VALIDATE_WARN_TSENS_PROFILE 0x00000004u

#define IOCTL_RSC_SEQUENCE_MODE_DEFAULT 1u
#define IOCTL_RSC_SEQUENCE_MODE_PERFORMANCE 2u
#define IOCTL_RSC_SEQUENCE_MODE_THERMAL 3u

#define IOCTL_RSC_SEQUENCE_FLAG_INCLUDE_ACTIVE 0x00000001u
#define IOCTL_RSC_SEQUENCE_FLAG_INCLUDE_SLEEP 0x00000002u
#define IOCTL_RSC_SEQUENCE_FLAG_INCLUDE_WAKE 0x00000004u
#define IOCTL_RSC_SEQUENCE_FLAG_FORCE_POLICY 0x00000008u
#define IOCTL_RSC_SEQUENCE_FLAG_RUN_INTERCONNECT 0x00000010u
#define IOCTL_RSC_SEQUENCE_FLAG_RUN_DOMAIN 0x00000020u
#define IOCTL_RSC_SEQUENCE_FLAG_VERIFY_QUERY 0x00000040u

#define IOCTL_RSC_SEQUENCE_PHASE_ACTIVE 0x00000001u
#define IOCTL_RSC_SEQUENCE_PHASE_SLEEP 0x00000002u
#define IOCTL_RSC_SEQUENCE_PHASE_WAKE 0x00000004u

#define IOCTL_RSC_LOOKUP_QUERY_SUBSYSTEM 1u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_ID 2u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_MASTER_ID 3u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_SLAVE_ID 4u
#define IOCTL_RSC_LOOKUP_QUERY_TEXT 5u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_SUBSYSTEM_ID 6u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_RESOURCE_ID 7u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_SLV_ID 8u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_QUERY_TYPE 9u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_DRV_ID 10u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_TRIGGER_ID 11u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_SET_ID 12u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_CLIENT_TYPE 13u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_EVENT_ID 14u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_DRIVE_TYPE 15u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_THRESHOLD_ID 16u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_DRIVER_ID 17u
#define IOCTL_RSC_LOOKUP_QUERY_DRIVER_MAP_ID 18u
#define IOCTL_RSC_LOOKUP_QUERY_DOMAIN_ID 19u
#define IOCTL_RSC_LOOKUP_QUERY_INTERCONNECT_ROUTE_ID 20u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_FREQ_MATCH_ID 21u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_RESOURCE_TYPE_ID 22u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_CONFIG_PARAM_ID 23u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_STATUS_ID 24u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_XPORT_STATE_ID 25u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_LINK_STATE_ID 26u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_ERROR_ID 27u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_QUERY_TYPE 28u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_REQUEST_TYPE 29u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_OBJECT_TYPE 30u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_STATE_ID 31u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_TX_ERROR_ID 32u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_STATUS_ID 33u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_QUERY_TYPE_ID 34u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_ERROR_ID 35u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_REMOTE_STATE_ID 36u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_BUS_TYPE_ID 37u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_NOTIFY_THRESHOLD_ID 38u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_NODE_TYPE_ID 39u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_INTERFACE_TYPE_ID 40u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_FLAGS_ID 41u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_PIN_TYPE_ID 42u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_DIR_TYPE_ID 43u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_PULL_TYPE_ID 44u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_VALUE_TYPE_ID 45u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_SROT_STATE_ID 46u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_RESULT_TYPE_ID 47u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_LOCAL_STATE_ID 48u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_CHANNEL_EVENT_ID 49u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_XPORT_PRIORITY_ID 50u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_NOTIFY_CROSSING_ID 51u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_REQUEST_KIND_ID 52u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_HW_NODE_KIND_ID 53u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_SW_STATE_ID 54u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_STAT_INDEX_ID 55u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_SETUP_ERROR_ID 56u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_ERROR_ID 57u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_REQUEST_ATTR_ID 58u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_NODE_ATTR_ID 59u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_RESOURCE_ATTR_ID 60u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_STATUS_ID 61u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_CLIENT_ATTR_ID 62u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_ACTION_ID 63u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_SLEEP_MODE_ID 64u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_RESET_ID 65u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_REQUEST_DATA_ID 66u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_RESOURCE_STATE_ID 67u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_RESOURCE_TYPE_ID 68u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_CLIENT_TYPE_ID 69u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_TEMP_ALARM_ID 70u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_RESOURCE_RANGE_ID 71u
#define IOCTL_RSC_LOOKUP_QUERY_POWERDOMAIN_ACTION_ID 72u
#define IOCTL_RSC_LOOKUP_QUERY_SUPPRESSIBLE_STATE_ID 73u
#define IOCTL_RSC_LOOKUP_QUERY_PM_RESOURCE_IF_ID 74u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_QDSS_EVENT_ID 75u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_TIMER_DEADLINE_ID 76u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_IO_TYPE_ID 77u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_LIMIT_TYPE_ID 78u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_VOTE_SET_TYPE_ID 79u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_PMIC_ID 80u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_MVS_SUBTYPE_ID 81u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_CXOBUF_SUBTYPE_ID 82u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_RWAY_SUBTYPE_ID 83u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_REQUEST_TYPE_ID 84u
#define IOCTL_RSC_LOOKUP_QUERY_PM_VRM_VOTE_OFFSET_ID 85u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_SOURCE_CONFIG_ID 86u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_SOURCE_CONFIG_MODE_ID 87u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_TRANSACTION_ATTR_ID 88u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_RESOURCE_TYPE_ID 89u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_EVENT_TRIGGER_ID 90u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_MUTEX_ATTR_ID 91u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_MUTEX_LOCK_TYPE_ID 92u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_LOG_EVENT_ID 93u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_PKT_RESULT_ID 94u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_XPORT_LOG_MASK_ID 95u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_STATE_ID 96u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_MDSS_CFG_ID 97u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_PCIE_CFG_ID 98u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_APSS_CFG_ID 99u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_LPM_MONITOR_STATE_ID 100u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_VOTE_CLASS_ID 101u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_AGG_DRIVER_ID 102u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_ACCESS_PROFILE_ID 103u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_EVENT_CTRL_ID 104u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_SSR_EVENT_ID 105u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_QUERY_PATH_ID 106u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_AGG_DRIVER_ID 107u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_PIN_STATE_ID 108u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_COMPARATOR_TYPE_ID 109u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_CMDQ_STAGE_ID 110u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TRIGGER_REASON_ID 111u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_GPIO_ACTION_ID 112u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_CALLBACK_TYPE_ID 113u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_ADDRESS_MODE_ID 114u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_OPERATING_POINT_ID 115u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_NPA_QUERY_ID 116u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_LOOKUP_STATUS_ID 117u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_SYNC_PRIMITIVE_ID 118u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_QOS_EVENT_ID 119u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_QUERY_STAGE_ID 120u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_RESOURCE_DRIVER_ID 121u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_VOTE_REASON_ID 122u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_TRIGGER_CONFIG_ID 123u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_RESOURCE_SERVICE_ID 124u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_AMC_PHASE_ID 125u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_CMD_ACTION_ID 126u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_CONFIG_PHASE_ID 127u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_CONTROLLER_EVENT_ID 128u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_IPCC_TARGET_ID 129u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_VOTE_BUCKET_ID 130u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_AGG_QUERY_ID 131u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_QUERY_SOURCE_ID 132u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_EVENT_TYPE_ID 133u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_XPORT_CMD_ID 134u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_ROUTE_CLASS_ID 135u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_CLIENT_LIFECYCLE_ID 136u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_TRANSITION_PHASE_ID 137u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_SEQUENCE_PHASE_ID 138u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_REQUEST_OPCODE_ID 139u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_BARRIER_TYPE_ID 140u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_MODE_ID 141u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_TILE_SELECTOR_ID 142u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_INTERRUPT_THRESHOLD_ID 143u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_SERVICE_CLASS_ID 144u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_RM_ACTION_ID 145u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_DOMAIN_POLICY_ID 146u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_ACCESS_PATH_ID 147u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_SYNC_EVENT_ID 148u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_CHANNEL_ROLE_ID 149u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_BW_CLASS_ID 150u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_EVENT_LIFECYCLE_ID 151u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_POLICY_REASON_ID 152u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_WAKE_SOURCE_ID 153u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_VOTE_PATH_ID 154u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_COMPLETION_STATE_ID 155u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_TCS_PRIORITY_ID 156u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_DRIVE_STRENGTH_ID 157u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_TRIP_CLASS_ID 158u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_MAILBOX_STATE_ID 159u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_POLICY_PHASE_ID 160u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_CONFIG_PHASE_ID 161u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_SLAVE_REGION_ID 162u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_WORKLOOP_EVENT_ID 163u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_RX_INTENT_STATE_ID 164u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_LATENCY_CLASS_ID 165u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_REQUEST_PHASE_ID 166u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_FREQ_FLOOR_STATE_ID 167u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_LATENCY_MODE_ID 168u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_RAIL_STATE_ID 169u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_WAKESET_TYPE_ID 170u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_SEQ_PHASE_ID 171u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_OWNER_DOMAIN_ID 172u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_FILTER_MODE_ID 173u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_IPCC_PROTOCOL_ID 174u
#define IOCTL_RSC_LOOKUP_QUERY_ARC_RAIL_VOLTAGE_LEVEL_ID 175u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_HAL_API_TYPE_ID 176u
#define IOCTL_RSC_LOOKUP_QUERY_CMDDB_AUX_CTRL_TYPE_ID 177u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_ULOG_LOCK_TYPE_ID 178u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_TRACER_EVENT_ID 179u
#define IOCTL_RSC_LOOKUP_QUERY_ICB_PEP_PERF_STATE_ID 180u
#define IOCTL_RSC_LOOKUP_QUERY_NPA_EVENT_TYPE_ID 181u
#define IOCTL_RSC_LOOKUP_QUERY_PCU_POWER_DOMAIN_API_ID 182u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_DEEPSTANDBY_MODE_ID 183u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_PARENT_CHILD_PARSE_STATE_ID 184u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_ISR_TYPE_ID 185u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_RC_DIRTY_STATE_ID 186u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_DISCRETE_GPIO_PARSE_STATE_ID 187u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_INTERRUPT_TYPE_ID 188u
#define IOCTL_RSC_LOOKUP_QUERY_AOP_SLEEP_STATE_ID 189u
#define IOCTL_RSC_LOOKUP_QUERY_RPMH_RC_PRIORITY_ID 190u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_CONFIG_API_ID 191u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_ULOG_MEMORY_CONFIG_ID 192u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_ULOG_ALLOC_ID 193u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_QDSS_TRACE_STREAM_ID 194u
#define IOCTL_RSC_LOOKUP_QUERY_GLINK_QDSS_SPEED_ID 195u
#define IOCTL_RSC_LOOKUP_QUERY_DAL_ULOG_MEMORY_USAGE_ID 196u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_SOURCE_DISABLE_MODE_ID 197u
#define IOCTL_RSC_LOOKUP_QUERY_PDC_IDLE_CALLBACK_TYPE_ID 198u
#define IOCTL_RSC_LOOKUP_QUERY_PPP_PMIC_GPIO_PARSE_STATE_ID 199u
#define IOCTL_RSC_LOOKUP_QUERY_RSC_RC_STATE_ID 200u
#define IOCTL_RSC_LOOKUP_QUERY_CLOCK_PLL_FRAC_MODE_ID 201u
#define IOCTL_RSC_LOOKUP_QUERY_TLMM_LED_GPIO_PARSE_STATE_ID 202u
#define IOCTL_RSC_LOOKUP_QUERY_TSENS_DEVICE_STATE_ID 203u

#define IOCTL_RSC_LOOKUP_FLAG_PARTIAL_MATCH 0x00000001u
#define IOCTL_RSC_LOOKUP_FLAG_CASE_INSENSITIVE 0x00000002u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_PPP 0x00000004u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_ICB_MASTER 0x00000008u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_ICB_SLAVE 0x00000010u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_AOP 0x00000020u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_ARC 0x00000040u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_CMDDB 0x00000080u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_PDC 0x00000100u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_RPMH 0x00000200u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_NPA 0x00000400u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_GLINK 0x00000800u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_TLMM 0x00001000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_TSENS 0x00002000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_CLOCK 0x00004000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_DRIVER_MAP 0x00008000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_DOMAIN 0x00010000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_ROUTE 0x00020000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_DAL 0x00040000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_PCU 0x00080000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_RSC 0x00100000u
#define IOCTL_RSC_LOOKUP_FLAG_INCLUDE_ARCMGR 0x00200000u

#define IOCTL_RSC_LOOKUP_KIND_SUBSYSTEM 1u
#define IOCTL_RSC_LOOKUP_KIND_PPP_RESOURCE 2u
#define IOCTL_RSC_LOOKUP_KIND_ICB_MASTER 3u
#define IOCTL_RSC_LOOKUP_KIND_ICB_SLAVE 4u
#define IOCTL_RSC_LOOKUP_KIND_AOP_SUBSYSTEM 5u
#define IOCTL_RSC_LOOKUP_KIND_ARC_RESOURCE 6u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_SLV 7u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_QUERY 8u
#define IOCTL_RSC_LOOKUP_KIND_PDC_DRV 9u
#define IOCTL_RSC_LOOKUP_KIND_PDC_TRIGGER 10u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_SET 11u
#define IOCTL_RSC_LOOKUP_KIND_NPA_CLIENT 12u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_EVENT 13u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_DRIVE 14u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_THRESHOLD 15u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_DRIVER 16u
#define IOCTL_RSC_LOOKUP_KIND_DRIVER_MAP 17u
#define IOCTL_RSC_LOOKUP_KIND_DOMAIN 18u
#define IOCTL_RSC_LOOKUP_KIND_INTERCONNECT_ROUTE 19u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_FREQ_MATCH 20u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_RESOURCE 21u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_CONFIG_PARAM 22u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_STATUS 23u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_XPORT_STATE 24u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_LINK_STATE 25u
#define IOCTL_RSC_LOOKUP_KIND_ICB_ERROR 26u
#define IOCTL_RSC_LOOKUP_KIND_ICB_QUERY 27u
#define IOCTL_RSC_LOOKUP_KIND_ICB_REQUEST 28u
#define IOCTL_RSC_LOOKUP_KIND_DAL_OBJECT 29u
#define IOCTL_RSC_LOOKUP_KIND_AOP_STATE 30u
#define IOCTL_RSC_LOOKUP_KIND_AOP_TX_ERROR 31u
#define IOCTL_RSC_LOOKUP_KIND_PDC_STATUS 32u
#define IOCTL_RSC_LOOKUP_KIND_NPA_QUERY 33u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_ERROR 34u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_REMOTE_STATE 35u
#define IOCTL_RSC_LOOKUP_KIND_ICB_BUS 36u
#define IOCTL_RSC_LOOKUP_KIND_ICB_NOTIFY_THRESHOLD 37u
#define IOCTL_RSC_LOOKUP_KIND_ICB_NODE 38u
#define IOCTL_RSC_LOOKUP_KIND_ICB_INTERFACE 39u
#define IOCTL_RSC_LOOKUP_KIND_PDC_FLAGS 40u
#define IOCTL_RSC_LOOKUP_KIND_PDC_PIN_TYPE 41u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_DIR 42u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_PULL 43u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_VALUE 44u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_SROT_STATE 45u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_RESULT_TYPE 46u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_LOCAL_STATE 47u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_CHANNEL_EVENT 48u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_XPORT_PRIORITY 49u
#define IOCTL_RSC_LOOKUP_KIND_ICB_NOTIFY_CROSSING 50u
#define IOCTL_RSC_LOOKUP_KIND_ICB_REQUEST_KIND 51u
#define IOCTL_RSC_LOOKUP_KIND_ICB_HW_NODE_KIND 52u
#define IOCTL_RSC_LOOKUP_KIND_ICB_SW_STATE 53u
#define IOCTL_RSC_LOOKUP_KIND_AOP_STAT_INDEX 54u
#define IOCTL_RSC_LOOKUP_KIND_AOP_SETUP_ERROR 55u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_ERROR 56u
#define IOCTL_RSC_LOOKUP_KIND_NPA_REQUEST_ATTR 57u
#define IOCTL_RSC_LOOKUP_KIND_NPA_NODE_ATTR 58u
#define IOCTL_RSC_LOOKUP_KIND_NPA_RESOURCE_ATTR 59u
#define IOCTL_RSC_LOOKUP_KIND_NPA_STATUS 60u
#define IOCTL_RSC_LOOKUP_KIND_ICB_CLIENT_ATTR 61u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_ACTION 62u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_SLEEP_MODE 63u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_RESET 64u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_REQUEST_DATA 65u
#define IOCTL_RSC_LOOKUP_KIND_PCU_RESOURCE_STATE 66u
#define IOCTL_RSC_LOOKUP_KIND_PCU_RESOURCE_TYPE 67u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_CLIENT_TYPE 68u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_TEMP_ALARM 69u
#define IOCTL_RSC_LOOKUP_KIND_PPP_RESOURCE_RANGE 70u
#define IOCTL_RSC_LOOKUP_KIND_POWERDOMAIN_ACTION 71u
#define IOCTL_RSC_LOOKUP_KIND_SUPPRESSIBLE_STATE 72u
#define IOCTL_RSC_LOOKUP_KIND_PM_RESOURCE_IF 73u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_QDSS_EVENT 74u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_TIMER_DEADLINE 75u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_IO_TYPE 76u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_LIMIT_TYPE 77u
#define IOCTL_RSC_LOOKUP_KIND_PPP_VOTE_SET_TYPE 78u
#define IOCTL_RSC_LOOKUP_KIND_PPP_PMIC_ID 79u
#define IOCTL_RSC_LOOKUP_KIND_PPP_MVS_SUBTYPE 80u
#define IOCTL_RSC_LOOKUP_KIND_PPP_CXOBUF_SUBTYPE 81u
#define IOCTL_RSC_LOOKUP_KIND_PPP_RWAY_SUBTYPE 82u
#define IOCTL_RSC_LOOKUP_KIND_RSC_REQUEST_TYPE 83u
#define IOCTL_RSC_LOOKUP_KIND_PM_VRM_VOTE_OFFSET 84u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_SOURCE_CONFIG 85u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_SOURCE_CONFIG_MODE 86u
#define IOCTL_RSC_LOOKUP_KIND_NPA_TRANSACTION_ATTR 87u
#define IOCTL_RSC_LOOKUP_KIND_NPA_RESOURCE_TYPE 88u
#define IOCTL_RSC_LOOKUP_KIND_NPA_EVENT_TRIGGER 89u
#define IOCTL_RSC_LOOKUP_KIND_DAL_MUTEX_ATTR 90u
#define IOCTL_RSC_LOOKUP_KIND_DAL_MUTEX_LOCK_TYPE 91u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_LOG_EVENT 92u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_PKT_RESULT 93u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_XPORT_LOG_MASK 94u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_STATE 95u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_MDSS_CFG 96u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_PCIE_CFG 97u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_APSS_CFG 98u
#define IOCTL_RSC_LOOKUP_KIND_AOP_LPM_MONITOR_STATE 99u
#define IOCTL_RSC_LOOKUP_KIND_ARC_VOTE_CLASS 100u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_AGG_DRIVER 101u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_ACCESS_PROFILE 102u
#define IOCTL_RSC_LOOKUP_KIND_DAL_EVENT_CTRL 103u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_SSR_EVENT 104u
#define IOCTL_RSC_LOOKUP_KIND_ICB_QUERY_PATH 105u
#define IOCTL_RSC_LOOKUP_KIND_NPA_AGG_DRIVER 106u
#define IOCTL_RSC_LOOKUP_KIND_PDC_PIN_STATE 107u
#define IOCTL_RSC_LOOKUP_KIND_PPP_COMPARATOR_TYPE 108u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_CMDQ_STAGE 109u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TRIGGER_REASON 110u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_GPIO_ACTION 111u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_CALLBACK_TYPE 112u
#define IOCTL_RSC_LOOKUP_KIND_AOP_ADDRESS_MODE 113u
#define IOCTL_RSC_LOOKUP_KIND_ARC_OPERATING_POINT 114u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_NPA_QUERY 115u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_LOOKUP_STATUS 116u
#define IOCTL_RSC_LOOKUP_KIND_DAL_SYNC_PRIMITIVE 117u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_QOS_EVENT 118u
#define IOCTL_RSC_LOOKUP_KIND_ICB_QUERY_STAGE 119u
#define IOCTL_RSC_LOOKUP_KIND_NPA_RESOURCE_DRIVER 120u
#define IOCTL_RSC_LOOKUP_KIND_PCU_VOTE_REASON 121u
#define IOCTL_RSC_LOOKUP_KIND_PDC_TRIGGER_CONFIG 122u
#define IOCTL_RSC_LOOKUP_KIND_PPP_RESOURCE_SERVICE 123u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_AMC_PHASE 124u
#define IOCTL_RSC_LOOKUP_KIND_RSC_CMD_ACTION 125u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_CONFIG_PHASE 126u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_CONTROLLER_EVENT 127u
#define IOCTL_RSC_LOOKUP_KIND_AOP_IPCC_TARGET 128u
#define IOCTL_RSC_LOOKUP_KIND_ARC_VOTE_BUCKET 129u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_AGG_QUERY 130u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_QUERY_SOURCE 131u
#define IOCTL_RSC_LOOKUP_KIND_DAL_EVENT_TYPE 132u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_XPORT_CMD 133u
#define IOCTL_RSC_LOOKUP_KIND_ICB_ROUTE_CLASS 134u
#define IOCTL_RSC_LOOKUP_KIND_NPA_CLIENT_LIFECYCLE 135u
#define IOCTL_RSC_LOOKUP_KIND_PCU_TRANSITION_PHASE 136u
#define IOCTL_RSC_LOOKUP_KIND_PDC_SEQUENCE_PHASE 137u
#define IOCTL_RSC_LOOKUP_KIND_PPP_REQUEST_OPCODE 138u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_BARRIER_TYPE 139u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_MODE 140u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_TILE_SELECTOR 141u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_INTERRUPT_THRESHOLD 142u
#define IOCTL_RSC_LOOKUP_KIND_AOP_SERVICE_CLASS 143u
#define IOCTL_RSC_LOOKUP_KIND_ARC_RM_ACTION 144u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_DOMAIN_POLICY 145u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_ACCESS_PATH 146u
#define IOCTL_RSC_LOOKUP_KIND_DAL_SYNC_EVENT 147u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_CHANNEL_ROLE 148u
#define IOCTL_RSC_LOOKUP_KIND_ICB_BW_CLASS 149u
#define IOCTL_RSC_LOOKUP_KIND_NPA_EVENT_LIFECYCLE 150u
#define IOCTL_RSC_LOOKUP_KIND_PCU_POLICY_REASON 151u
#define IOCTL_RSC_LOOKUP_KIND_PDC_WAKE_SOURCE 152u
#define IOCTL_RSC_LOOKUP_KIND_PPP_VOTE_PATH 153u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_COMPLETION_STATE 154u
#define IOCTL_RSC_LOOKUP_KIND_RSC_TCS_PRIORITY 155u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_DRIVE_STRENGTH 156u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_TRIP_CLASS 157u
#define IOCTL_RSC_LOOKUP_KIND_AOP_MAILBOX_STATE 158u
#define IOCTL_RSC_LOOKUP_KIND_ARC_POLICY_PHASE 159u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_CONFIG_PHASE 160u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_SLAVE_REGION 161u
#define IOCTL_RSC_LOOKUP_KIND_DAL_WORKLOOP_EVENT 162u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_RX_INTENT_STATE 163u
#define IOCTL_RSC_LOOKUP_KIND_ICB_LATENCY_CLASS 164u
#define IOCTL_RSC_LOOKUP_KIND_NPA_REQUEST_PHASE 165u
#define IOCTL_RSC_LOOKUP_KIND_PCU_FREQ_FLOOR_STATE 166u
#define IOCTL_RSC_LOOKUP_KIND_PDC_LATENCY_MODE 167u
#define IOCTL_RSC_LOOKUP_KIND_PPP_RAIL_STATE 168u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_WAKESET_TYPE 169u
#define IOCTL_RSC_LOOKUP_KIND_RSC_SEQ_PHASE 170u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_OWNER_DOMAIN 171u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_FILTER_MODE 172u
#define IOCTL_RSC_LOOKUP_KIND_AOP_IPCC_PROTOCOL 173u
#define IOCTL_RSC_LOOKUP_KIND_ARC_RAIL_VOLTAGE_LEVEL 174u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_HAL_API_TYPE 175u
#define IOCTL_RSC_LOOKUP_KIND_CMDDB_AUX_CTRL_TYPE 176u
#define IOCTL_RSC_LOOKUP_KIND_DAL_ULOG_LOCK_TYPE 177u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_TRACER_EVENT 178u
#define IOCTL_RSC_LOOKUP_KIND_ICB_PEP_PERF_STATE 179u
#define IOCTL_RSC_LOOKUP_KIND_NPA_EVENT_TYPE 180u
#define IOCTL_RSC_LOOKUP_KIND_PCU_POWER_DOMAIN_API 181u
#define IOCTL_RSC_LOOKUP_KIND_PDC_DEEPSTANDBY_MODE 182u
#define IOCTL_RSC_LOOKUP_KIND_PPP_PARENT_CHILD_PARSE_STATE 183u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_ISR_TYPE 184u
#define IOCTL_RSC_LOOKUP_KIND_RSC_RC_DIRTY_STATE 185u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_DISCRETE_GPIO_PARSE_STATE 186u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_INTERRUPT_TYPE 187u
#define IOCTL_RSC_LOOKUP_KIND_AOP_SLEEP_STATE 188u
#define IOCTL_RSC_LOOKUP_KIND_RPMH_RC_PRIORITY 189u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_CONFIG_API 190u
#define IOCTL_RSC_LOOKUP_KIND_DAL_ULOG_MEMORY_CONFIG 191u
#define IOCTL_RSC_LOOKUP_KIND_DAL_ULOG_ALLOC 192u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_QDSS_TRACE_STREAM 193u
#define IOCTL_RSC_LOOKUP_KIND_GLINK_QDSS_SPEED 194u
#define IOCTL_RSC_LOOKUP_KIND_DAL_ULOG_MEMORY_USAGE 195u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_SOURCE_DISABLE_MODE 196u
#define IOCTL_RSC_LOOKUP_KIND_PDC_IDLE_CALLBACK_TYPE 197u
#define IOCTL_RSC_LOOKUP_KIND_PPP_PMIC_GPIO_PARSE_STATE 198u
#define IOCTL_RSC_LOOKUP_KIND_RSC_RC_STATE 199u
#define IOCTL_RSC_LOOKUP_KIND_CLOCK_PLL_FRAC_MODE 200u
#define IOCTL_RSC_LOOKUP_KIND_TLMM_LED_GPIO_PARSE_STATE 201u
#define IOCTL_RSC_LOOKUP_KIND_TSENS_DEVICE_STATE 202u

#define IOCTL_RSC_PROVISION_MODE_DEFAULT 1u
#define IOCTL_RSC_PROVISION_MODE_PERFORMANCE 2u
#define IOCTL_RSC_PROVISION_MODE_THERMAL 3u
#define IOCTL_RSC_PROVISION_MODE_RECOVERY 4u

#define IOCTL_RSC_PROVISION_FLAG_APPLY_STATE 0x00000001u
#define IOCTL_RSC_PROVISION_FLAG_APPLY_POLICY 0x00000002u
#define IOCTL_RSC_PROVISION_FLAG_RUN_INTERCONNECT 0x00000004u
#define IOCTL_RSC_PROVISION_FLAG_RUN_DOMAIN 0x00000008u
#define IOCTL_RSC_PROVISION_FLAG_VERIFY_QUERY 0x00000010u
#define IOCTL_RSC_PROVISION_FLAG_THERMAL_GUARD 0x00000020u

#define IOCTL_RSC_RAIL_MODE_BALANCED 1u
#define IOCTL_RSC_RAIL_MODE_PERFORMANCE 2u
#define IOCTL_RSC_RAIL_MODE_THERMAL 3u
#define IOCTL_RSC_RAIL_MODE_RETENTION 4u

#define IOCTL_RSC_RAIL_FLAG_APPLY_ARC 0x00000001u
#define IOCTL_RSC_RAIL_FLAG_APPLY_CLOCK 0x00000002u
#define IOCTL_RSC_RAIL_FLAG_APPLY_POLICY 0x00000004u
#define IOCTL_RSC_RAIL_FLAG_RUN_INTERCONNECT 0x00000008u
#define IOCTL_RSC_RAIL_FLAG_RUN_DOMAIN 0x00000010u
#define IOCTL_RSC_RAIL_FLAG_VERIFY_QUERY 0x00000020u
#define IOCTL_RSC_RAIL_FLAG_ISSUE_GLINK 0x00000040u
#define IOCTL_RSC_RAIL_FLAG_THERMAL_GUARD 0x00000080u

#define IOCTL_RSC_TCS_MODE_ACTIVE 1u
#define IOCTL_RSC_TCS_MODE_SLEEP 2u
#define IOCTL_RSC_TCS_MODE_WAKE 3u
#define IOCTL_RSC_TCS_MODE_HIBERNATE 4u

#define IOCTL_RSC_TCS_FLAG_PROGRAM_TCS 0x00000001u
#define IOCTL_RSC_TCS_FLAG_VOTE_POLICY 0x00000002u
#define IOCTL_RSC_TCS_FLAG_QUEUE_GLINK 0x00000004u
#define IOCTL_RSC_TCS_FLAG_RUN_INTERCONNECT 0x00000008u
#define IOCTL_RSC_TCS_FLAG_RUN_DOMAIN 0x00000010u
#define IOCTL_RSC_TCS_FLAG_VERIFY_QUERY 0x00000020u
#define IOCTL_RSC_TCS_FLAG_THERMAL_GUARD 0x00000040u
#define IOCTL_RSC_TCS_FLAG_STRICT_TCS 0x00000080u

#define IOCTL_RSC_THERMAL_MODE_MONITOR 1u
#define IOCTL_RSC_THERMAL_MODE_MITIGATE 2u
#define IOCTL_RSC_THERMAL_MODE_RECOVER 3u
#define IOCTL_RSC_THERMAL_MODE_CALIBRATE 4u

#define IOCTL_RSC_THERMAL_FLAG_APPLY_STATE 0x00000001u
#define IOCTL_RSC_THERMAL_FLAG_APPLY_THRESHOLDS 0x00000002u
#define IOCTL_RSC_THERMAL_FLAG_APPLY_POLICY 0x00000004u
#define IOCTL_RSC_THERMAL_FLAG_RUN_INTERCONNECT 0x00000008u
#define IOCTL_RSC_THERMAL_FLAG_RUN_DOMAIN 0x00000010u
#define IOCTL_RSC_THERMAL_FLAG_VERIFY_QUERY 0x00000020u
#define IOCTL_RSC_THERMAL_FLAG_FORCE_COOLING 0x00000040u
#define IOCTL_RSC_THERMAL_FLAG_ISSUE_GLINK 0x00000080u

#define IOCTL_RSC_COORD_MODE_BASELINE 1u
#define IOCTL_RSC_COORD_MODE_PERFORMANCE 2u
#define IOCTL_RSC_COORD_MODE_THERMAL 3u
#define IOCTL_RSC_COORD_MODE_RECOVERY 4u

#define IOCTL_RSC_COORD_FLAG_RUN_SYNC 0x00000001u
#define IOCTL_RSC_COORD_FLAG_RUN_VALIDATE 0x00000002u
#define IOCTL_RSC_COORD_FLAG_RUN_SEQUENCE 0x00000004u
#define IOCTL_RSC_COORD_FLAG_RUN_PROVISION 0x00000008u
#define IOCTL_RSC_COORD_FLAG_RUN_RAIL 0x00000010u
#define IOCTL_RSC_COORD_FLAG_RUN_TCS 0x00000020u
#define IOCTL_RSC_COORD_FLAG_RUN_THERMAL 0x00000040u
#define IOCTL_RSC_COORD_FLAG_RUN_FABRIC 0x00000080u
#define IOCTL_RSC_COORD_FLAG_RUN_INTERCONNECT 0x00000100u
#define IOCTL_RSC_COORD_FLAG_RUN_DOMAIN 0x00000200u
#define IOCTL_RSC_COORD_FLAG_STRICT_VERIFY 0x00000400u

#define IOCTL_RSC_AUDIT_MODE_BASELINE 1u
#define IOCTL_RSC_AUDIT_MODE_PERFORMANCE 2u
#define IOCTL_RSC_AUDIT_MODE_THERMAL 3u
#define IOCTL_RSC_AUDIT_MODE_RECOVERY 4u

#define IOCTL_RSC_AUDIT_FLAG_REPAIR_STATE 0x00000001u
#define IOCTL_RSC_AUDIT_FLAG_REPAIR_CLOCK 0x00000002u
#define IOCTL_RSC_AUDIT_FLAG_REPAIR_POLICY 0x00000004u
#define IOCTL_RSC_AUDIT_FLAG_VERIFY_ICB 0x00000008u
#define IOCTL_RSC_AUDIT_FLAG_VERIFY_ENUM_NAMES 0x00000010u
#define IOCTL_RSC_AUDIT_FLAG_RUN_COORD 0x00000020u
#define IOCTL_RSC_AUDIT_FLAG_RUN_INTERCONNECT 0x00000040u
#define IOCTL_RSC_AUDIT_FLAG_RUN_DOMAIN 0x00000080u
#define IOCTL_RSC_AUDIT_FLAG_STRICT 0x00000100u
#define IOCTL_RSC_AUDIT_FLAG_VERIFY_DRIVER_MAP 0x00000200u
#define IOCTL_RSC_AUDIT_FLAG_VERIFY_DOMAIN_MAP 0x00000400u
#define IOCTL_RSC_AUDIT_FLAG_VERIFY_ROUTE_MAP 0x00000800u

#define IOCTL_RSC_AUDIT_ISSUE_ARC IOCTL_RSC_VALIDATE_ISSUE_ARC
#define IOCTL_RSC_AUDIT_ISSUE_CMDDB IOCTL_RSC_VALIDATE_ISSUE_CMDDB
#define IOCTL_RSC_AUDIT_ISSUE_PDC IOCTL_RSC_VALIDATE_ISSUE_PDC
#define IOCTL_RSC_AUDIT_ISSUE_RPMH IOCTL_RSC_VALIDATE_ISSUE_RPMH
#define IOCTL_RSC_AUDIT_ISSUE_NPA IOCTL_RSC_VALIDATE_ISSUE_NPA
#define IOCTL_RSC_AUDIT_ISSUE_GLINK IOCTL_RSC_VALIDATE_ISSUE_GLINK
#define IOCTL_RSC_AUDIT_ISSUE_PPP IOCTL_RSC_VALIDATE_ISSUE_PPP
#define IOCTL_RSC_AUDIT_ISSUE_TLMM IOCTL_RSC_VALIDATE_ISSUE_TLMM
#define IOCTL_RSC_AUDIT_ISSUE_TSENS IOCTL_RSC_VALIDATE_ISSUE_TSENS
#define IOCTL_RSC_AUDIT_ISSUE_CLOCK IOCTL_RSC_VALIDATE_ISSUE_CLOCK
#define IOCTL_RSC_AUDIT_ISSUE_AOP IOCTL_RSC_VALIDATE_ISSUE_AOP
#define IOCTL_RSC_AUDIT_ISSUE_ICB IOCTL_RSC_VALIDATE_ISSUE_ICB
#define IOCTL_RSC_AUDIT_ISSUE_THERMAL IOCTL_RSC_VALIDATE_ISSUE_THERMAL
#define IOCTL_RSC_AUDIT_ISSUE_DRIVER_MAP 0x00002000u
#define IOCTL_RSC_AUDIT_ISSUE_DOMAIN_MAP 0x00004000u
#define IOCTL_RSC_AUDIT_ISSUE_ROUTE_MAP 0x00008000u
#define IOCTL_RSC_AUDIT_ISSUE_DEPENDENCY_MAP 0x00010000u

#define IOCTL_RSC_AUDIT_WARN_ICB_NAME IOCTL_RSC_VALIDATE_WARN_ICB_NAME
#define IOCTL_RSC_AUDIT_WARN_RPMH_PROFILE IOCTL_RSC_VALIDATE_WARN_RPMH_PROFILE
#define IOCTL_RSC_AUDIT_WARN_TSENS_PROFILE IOCTL_RSC_VALIDATE_WARN_TSENS_PROFILE
#define IOCTL_RSC_AUDIT_WARN_DOMAIN_OVERLAP 0x00000008u
#define IOCTL_RSC_AUDIT_WARN_ROUTE_TOPOLOGY 0x00000010u
#define IOCTL_RSC_AUDIT_WARN_ROUTE_NAME 0x00000020u
#define IOCTL_RSC_AUDIT_WARN_DRIVER_FANOUT 0x00000040u
#define IOCTL_RSC_AUDIT_WARN_DEPENDENCY_GAP 0x00000080u

#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_DRIVER_MAP 0x00000001u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_DOMAIN_MAP 0x00000002u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_ROUTE_MAP 0x00000004u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_ROUTE_NAMES 0x00000008u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_STRICT_PATH 0x00000010u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_PROFILE_POLICY 0x00000020u
#define IOCTL_RSC_AUDIT_INTEGRITY_FLAG_DEPENDENCY_POLICY 0x00000040u

#define IOCTL_RSC_SUPERVISOR_MODE_BASELINE 1u
#define IOCTL_RSC_SUPERVISOR_MODE_PERFORMANCE 2u
#define IOCTL_RSC_SUPERVISOR_MODE_THERMAL 3u
#define IOCTL_RSC_SUPERVISOR_MODE_RECOVERY 4u

#define IOCTL_RSC_SUPERVISOR_FLAG_RUN_AUDIT 0x00000001u
#define IOCTL_RSC_SUPERVISOR_FLAG_RUN_COORD 0x00000002u
#define IOCTL_RSC_SUPERVISOR_FLAG_RUN_THERMAL 0x00000004u
#define IOCTL_RSC_SUPERVISOR_FLAG_RUN_INTERCONNECT 0x00000008u
#define IOCTL_RSC_SUPERVISOR_FLAG_RUN_DOMAIN 0x00000010u
#define IOCTL_RSC_SUPERVISOR_FLAG_STRICT 0x00000020u
#define IOCTL_RSC_SUPERVISOR_FLAG_REPAIR_STATE 0x00000040u
#define IOCTL_RSC_SUPERVISOR_FLAG_REPAIR_CLOCK 0x00000080u
#define IOCTL_RSC_SUPERVISOR_FLAG_REPAIR_POLICY 0x00000100u
#define IOCTL_RSC_SUPERVISOR_FLAG_VERIFY_ICB 0x00000200u
#define IOCTL_RSC_SUPERVISOR_FLAG_VERIFY_ENUM_NAMES 0x00000400u
#define IOCTL_RSC_SUPERVISOR_FLAG_TRACK_MODULE_MATRIX 0x00000800u
#define IOCTL_RSC_SUPERVISOR_FLAG_VERIFY_DRIVER_MAP 0x00001000u
#define IOCTL_RSC_SUPERVISOR_FLAG_VERIFY_DOMAIN_MAP 0x00002000u
#define IOCTL_RSC_SUPERVISOR_FLAG_VERIFY_ROUTE_MAP 0x00004000u

#define IOCTL_RSC_SUPERVISOR_MODULE_AOP 1u
#define IOCTL_RSC_SUPERVISOR_MODULE_ARC 2u
#define IOCTL_RSC_SUPERVISOR_MODULE_CLOCK 3u
#define IOCTL_RSC_SUPERVISOR_MODULE_CMDDB 4u
#define IOCTL_RSC_SUPERVISOR_MODULE_DAL 5u
#define IOCTL_RSC_SUPERVISOR_MODULE_GLINK 6u
#define IOCTL_RSC_SUPERVISOR_MODULE_ICB 7u
#define IOCTL_RSC_SUPERVISOR_MODULE_NPA 8u
#define IOCTL_RSC_SUPERVISOR_MODULE_PCU 9u
#define IOCTL_RSC_SUPERVISOR_MODULE_PDC 10u
#define IOCTL_RSC_SUPERVISOR_MODULE_PPP 11u
#define IOCTL_RSC_SUPERVISOR_MODULE_RPMH 12u
#define IOCTL_RSC_SUPERVISOR_MODULE_RSC 13u
#define IOCTL_RSC_SUPERVISOR_MODULE_TLMM 14u
#define IOCTL_RSC_SUPERVISOR_MODULE_TSENS 15u
#define IOCTL_RSC_SUPERVISOR_MODULE_ARCMGR 16u

#define IOCTL_RSC_AUTOTUNE_MODE_BASELINE 1u
#define IOCTL_RSC_AUTOTUNE_MODE_PERFORMANCE 2u
#define IOCTL_RSC_AUTOTUNE_MODE_THERMAL 3u
#define IOCTL_RSC_AUTOTUNE_MODE_RECOVERY 4u

#define IOCTL_RSC_AUTOTUNE_FLAG_RUN_SUPERVISOR 0x00000001u
#define IOCTL_RSC_AUTOTUNE_FLAG_RUN_THERMAL 0x00000002u
#define IOCTL_RSC_AUTOTUNE_FLAG_APPLY_STATE 0x00000004u
#define IOCTL_RSC_AUTOTUNE_FLAG_APPLY_CLOCK 0x00000008u
#define IOCTL_RSC_AUTOTUNE_FLAG_APPLY_POLICY 0x00000010u
#define IOCTL_RSC_AUTOTUNE_FLAG_STRICT 0x00000020u
#define IOCTL_RSC_AUTOTUNE_FLAG_AGGRESSIVE 0x00000040u
#define IOCTL_RSC_AUTOTUNE_FLAG_VERIFY_QUERY 0x00000080u
#define IOCTL_RSC_AUTOTUNE_FLAG_TRACK_HISTORY 0x00000100u
#define IOCTL_RSC_AUTOTUNE_FLAG_DEPENDENCY_GUARD 0x00000200u
#define IOCTL_RSC_AUTOTUNE_FLAG_PROFILE_POLICY 0x00000400u

#define IOCTL_RSC_AUTOTUNE_ACTION_DEPENDENCY_GAP 0x01000000u
#define IOCTL_RSC_AUTOTUNE_ACTION_DOMAIN_GAP 0x02000000u
#define IOCTL_RSC_AUTOTUNE_ACTION_ROUTE_GAP 0x04000000u
#define IOCTL_RSC_AUTOTUNE_ACTION_FANOUT_GAP 0x08000000u
#define IOCTL_RSC_AUTOTUNE_ACTION_PROFILE_POLICY 0x10000000u
#define IOCTL_RSC_AUTOTUNE_ACTION_RISK_CLAMP 0x20000000u

#define IOCTL_RSC_HARDEN_MODE_BASELINE 1u
#define IOCTL_RSC_HARDEN_MODE_PERFORMANCE 2u
#define IOCTL_RSC_HARDEN_MODE_THERMAL 3u
#define IOCTL_RSC_HARDEN_MODE_RECOVERY 4u

#define IOCTL_RSC_HARDEN_FLAG_RUN_AUTOTUNE 0x00000001u
#define IOCTL_RSC_HARDEN_FLAG_RUN_SUPERVISOR 0x00000002u
#define IOCTL_RSC_HARDEN_FLAG_RUN_AUDIT 0x00000004u
#define IOCTL_RSC_HARDEN_FLAG_RUN_COORD 0x00000008u
#define IOCTL_RSC_HARDEN_FLAG_RUN_THERMAL 0x00000010u
#define IOCTL_RSC_HARDEN_FLAG_RUN_INTERCONNECT 0x00000020u
#define IOCTL_RSC_HARDEN_FLAG_RUN_DOMAIN 0x00000040u
#define IOCTL_RSC_HARDEN_FLAG_STRICT 0x00000080u
#define IOCTL_RSC_HARDEN_FLAG_VERIFY_QUERY 0x00000100u
#define IOCTL_RSC_HARDEN_FLAG_TRACK_HISTORY 0x00000200u

#define IOCTL_RSC_SCENARIO_AOP 1u
#define IOCTL_RSC_SCENARIO_ARC 2u
#define IOCTL_RSC_SCENARIO_CLOCK 3u
#define IOCTL_RSC_SCENARIO_CMDDB 4u
#define IOCTL_RSC_SCENARIO_DAL 5u
#define IOCTL_RSC_SCENARIO_GLINK 6u
#define IOCTL_RSC_SCENARIO_ICB 7u
#define IOCTL_RSC_SCENARIO_NPA 8u
#define IOCTL_RSC_SCENARIO_PCU 9u
#define IOCTL_RSC_SCENARIO_PDC 10u
#define IOCTL_RSC_SCENARIO_PPP 11u
#define IOCTL_RSC_SCENARIO_RPMH 12u
#define IOCTL_RSC_SCENARIO_RSC 13u
#define IOCTL_RSC_SCENARIO_TLMM 14u
#define IOCTL_RSC_SCENARIO_TSENS 15u
#define IOCTL_RSC_SCENARIO_FULL_SWEEP 16u

#define IOCTL_RSC_SCENARIO_FLAG_USE_DEFAULTS 0x00000001u
#define IOCTL_RSC_SCENARIO_FLAG_TOGGLE_STATE 0x00000002u
#define IOCTL_RSC_SCENARIO_FLAG_REFRESH_EACH 0x00000004u
#define IOCTL_RSC_SCENARIO_FLAG_VERIFY_QUERY 0x00000008u
#define IOCTL_RSC_SCENARIO_FLAG_INCLUDE_ARCMGR 0x00000010u

#define IOCTL_RSC_DIAG_FLAG_INCLUDE_FULL_SWEEP 0x00000001u
#define IOCTL_RSC_DIAG_FLAG_TOGGLE_STATE 0x00000002u
#define IOCTL_RSC_DIAG_FLAG_VERIFY_QUERY 0x00000004u
#define IOCTL_RSC_DIAG_FLAG_INCLUDE_ARCMGR 0x00000008u
#define IOCTL_RSC_DIAG_FLAG_REFRESH_EACH 0x00000010u

#define IOCTL_RSC_PROFILE_FLAG_RUN_MATRIX 0x00000001u
#define IOCTL_RSC_PROFILE_FLAG_INCLUDE_FULL_SWEEP 0x00000002u
#define IOCTL_RSC_PROFILE_FLAG_FORCE_VERIFY 0x00000004u
#define IOCTL_RSC_PROFILE_FLAG_FORCE_TOGGLE 0x00000008u

#define IOCTL_RSC_ACTION_SEQUENCE_FLAG_RESPECT_DEPENDENCY 0x00000001u
#define IOCTL_RSC_ACTION_SEQUENCE_FLAG_VERIFY_NODE 0x00000002u
#define IOCTL_RSC_ACTION_SEQUENCE_FLAG_RECORD_TEST_EVENT 0x00000004u

#define IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_FLAG_INCLUDE_DEPENDENCIES 0x00000001u
#define IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_FLAG_ASSUME_ENABLE_DEPENDENCIES 0x00000002u
#define IOCTL_RSC_ACTION_SEQUENCE_PREVIEW_FLAG_RESPECT_ORDER 0x00000004u

#define IOCTL_RSC_CHECKPOINT_OP_CAPTURE 1u
#define IOCTL_RSC_CHECKPOINT_OP_APPLY 2u

#define IOCTL_RSC_COVERAGE_FLAG_REQUIRE_ACTIVE 0x00000001u
#define IOCTL_RSC_COVERAGE_FLAG_REQUIRE_SUCCESS_THRESHOLD 0x00000002u
#define IOCTL_RSC_COVERAGE_FLAG_REQUIRE_RECENT_ACTIVITY 0x00000004u
#define IOCTL_RSC_COVERAGE_FLAG_INCLUDE_DEPENDENCY_LOAD 0x00000008u
#define IOCTL_RSC_COVERAGE_FLAG_STRICT_THRESHOLD 0x00000010u

#define IOCTL_RSC_REMEDIATION_FLAG_APPLY_RECOMMENDATIONS 0x00000001u
#define IOCTL_RSC_REMEDIATION_FLAG_TARGET_INACTIVE 0x00000002u
#define IOCTL_RSC_REMEDIATION_FLAG_TARGET_LOW_SUCCESS 0x00000004u
#define IOCTL_RSC_REMEDIATION_FLAG_TARGET_STALE_ACTIVITY 0x00000008u
#define IOCTL_RSC_REMEDIATION_FLAG_INCLUDE_DEPENDENCY_LOAD 0x00000010u
#define IOCTL_RSC_REMEDIATION_FLAG_STRICT_THRESHOLD 0x00000020u

#define IOCTL_RSC_REMEDIATION_REASON_INACTIVE 0x00000001u
#define IOCTL_RSC_REMEDIATION_REASON_LOW_SUCCESS 0x00000002u
#define IOCTL_RSC_REMEDIATION_REASON_STALE_ACTIVITY 0x00000004u
#define IOCTL_RSC_REMEDIATION_REASON_DEPENDENCY_PRESSURE 0x00000008u
#define IOCTL_RSC_REMEDIATION_REASON_LAST_STATUS_FAIL 0x00000010u
#define IOCTL_RSC_REMEDIATION_REASON_HIGH_FAILURE_RATE 0x00000020u

#define IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_CONTROL_LANE 0x00000001u
#define IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_INTERCONNECT_LANE 0x00000002u
#define IOCTL_RSC_STABILIZATION_FLAG_INCLUDE_THERMAL_LANE 0x00000004u
#define IOCTL_RSC_STABILIZATION_FLAG_RUN_PROFILES 0x00000008u
#define IOCTL_RSC_STABILIZATION_FLAG_EXECUTE_REMEDIATION 0x00000010u
#define IOCTL_RSC_STABILIZATION_FLAG_VERIFY_POST 0x00000020u
#define IOCTL_RSC_STABILIZATION_FLAG_STRICT_COVERAGE 0x00000040u

#define IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_ACTIVE 0x00000001u
#define IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_SUCCESS_THRESHOLD 0x00000002u
#define IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_RECENT_ACTIVITY 0x00000004u
#define IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_DEPENDENCY_SHAPE 0x00000008u
#define IOCTL_RSC_CONFORMANCE_FLAG_REQUIRE_CATALOG_MATCH 0x00000010u
#define IOCTL_RSC_CONFORMANCE_FLAG_STRICT_THRESHOLD 0x00000020u
#define IOCTL_RSC_CONFORMANCE_FLAG_INCLUDE_ARCMGR 0x00000040u

#define IOCTL_RSC_CONFORMANCE_REASON_INACTIVE 0x00000001u
#define IOCTL_RSC_CONFORMANCE_REASON_LOW_SUCCESS 0x00000002u
#define IOCTL_RSC_CONFORMANCE_REASON_LOW_ACTIVITY 0x00000004u
#define IOCTL_RSC_CONFORMANCE_REASON_DEPENDENCY_GAP 0x00000008u
#define IOCTL_RSC_CONFORMANCE_REASON_STATUS_FAIL 0x00000010u
#define IOCTL_RSC_CONFORMANCE_REASON_CATALOG_MISMATCH 0x00000020u
#define IOCTL_RSC_CONFORMANCE_REASON_HIGH_FAILURE_RATE 0x00000040u

#define IOCTL_RSC_ORCHESTRATION_FLAG_RUN_COVERAGE 0x00000001u
#define IOCTL_RSC_ORCHESTRATION_FLAG_RUN_REMEDIATION 0x00000002u
#define IOCTL_RSC_ORCHESTRATION_FLAG_RUN_STABILIZATION 0x00000004u
#define IOCTL_RSC_ORCHESTRATION_FLAG_RUN_CONFORMANCE 0x00000008u
#define IOCTL_RSC_ORCHESTRATION_FLAG_RUN_PROFILES 0x00000010u
#define IOCTL_RSC_ORCHESTRATION_FLAG_STRICT 0x00000020u
#define IOCTL_RSC_ORCHESTRATION_FLAG_INCLUDE_ARCMGR 0x00000040u
#define IOCTL_RSC_ORCHESTRATION_FLAG_REFRESH_EACH_STAGE 0x00000080u

#define IOCTL_RSC_ORCHESTRATION_STAGE_COVERAGE 1u
#define IOCTL_RSC_ORCHESTRATION_STAGE_REMEDIATION 2u
#define IOCTL_RSC_ORCHESTRATION_STAGE_STABILIZATION 3u
#define IOCTL_RSC_ORCHESTRATION_STAGE_CONFORMANCE 4u
#define IOCTL_RSC_ORCHESTRATION_STAGE_PROFILES 5u

#define IOCTL_RSC_POLICY_FLAG_VOTE 0x00000001u
#define IOCTL_RSC_POLICY_FLAG_REMOVE 0x00000002u
#define IOCTL_RSC_POLICY_FLAG_PROPAGATED 0x00000004u
#define IOCTL_RSC_POLICY_FLAG_PARENT 0x00000008u
#define IOCTL_RSC_POLICY_FLAG_ACTIVE 0x00000010u

#define IOCTL_RSC_FABRIC_FLAG_REQUIRE_ACTIVE 0x00000001u
#define IOCTL_RSC_FABRIC_FLAG_REQUIRE_POLICY_ACTIVITY 0x00000002u
#define IOCTL_RSC_FABRIC_FLAG_REQUIRE_DEPENDENCY_HEALTH 0x00000004u
#define IOCTL_RSC_FABRIC_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define IOCTL_RSC_FABRIC_FLAG_STRICT 0x00000010u

#define IOCTL_RSC_DOMAIN_FLAG_REQUIRE_ACTIVE 0x00000001u
#define IOCTL_RSC_DOMAIN_FLAG_REQUIRE_POLICY_ACTIVITY 0x00000002u
#define IOCTL_RSC_DOMAIN_FLAG_REQUIRE_DEPENDENCY_HEALTH 0x00000004u
#define IOCTL_RSC_DOMAIN_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define IOCTL_RSC_DOMAIN_FLAG_STRICT 0x00000010u

#define IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_ACTIVE_ROUTES 0x00000001u
#define IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_BANDWIDTH_HEADROOM 0x00000002u
#define IOCTL_RSC_INTERCONNECT_FLAG_REQUIRE_POLICY_LINK 0x00000004u
#define IOCTL_RSC_INTERCONNECT_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define IOCTL_RSC_INTERCONNECT_FLAG_STRICT 0x00000010u

#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_ACTIVE 0x00000001u
#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_POLICY_LINKED 0x00000002u
#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_THERMAL_HIGH 0x00000004u
#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_STATUS_FAIL 0x00000008u
#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_UNDER_PROVISIONED 0x00000010u
#define IOCTL_RSC_INTERCONNECT_ROUTE_FLAG_OVER_UTILIZED 0x00000020u

typedef struct ioctl_rsc_node_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG Present;
    ULONG State;
    ULONG ClockKhz;
    ULONG Flags;
    ULONG LastIoctlCode;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONG RequestCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONG VoteCount;
    ULONG CommandCount;
    ULONG PendingCount;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
    ULONGLONG ActivityAccumulator;
} ioctl_rsc_node_info_t;

typedef struct ioctl_rsc_event_info {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG SubsystemId;
    ULONG EventCode;
    LONG Status;
    ULONG IoctlCode;
    ULONG Tag;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
    ULONG Flags;
} ioctl_rsc_event_info_t;

typedef struct ioctl_rsc_snapshot_response {
    ULONG Version;
    ULONG NodeCount;
    ULONG ActiveMask;
    ULONG Reserved;
    ULONGLONG EventSequence;
    ULONG HistoryCount;
    ULONG HistoryDropped;
    ULONG LastEventSubsystemId;
    ULONG LastEventCode;
    LONG LastEventStatus;
    ULONGLONG LastEventTick100ns;
    ULONG LastIoctlCode;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONG LastFlags;
    ULONG BatchApplyCount;
    ULONG BatchCommandCount;
    ULONG BatchErrorCount;
    ULONG LastBatchCount;
    LONG LastBatchStatus;
    ULONGLONG LastBatchTick100ns;
    ULONG ScenarioRunCount;
    ULONG ScenarioFailureCount;
    ULONG LastScenarioId;
    LONG LastScenarioStatus;
    ULONG LastScenarioIterations;
    ULONGLONG LastScenarioTick100ns;
    ULONG NodeEntryCount;
    ioctl_rsc_node_info_t Nodes[IOCTL_RSC_NODE_INFO_CAPACITY];
} ioctl_rsc_snapshot_response_t;

typedef struct ioctl_rsc_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_event_info_t Entries[1];
} ioctl_rsc_history_response_t;

typedef struct ioctl_rsc_set_node_request {
    ULONG SubsystemId;
    ULONG State;
    ULONG ClockKhz;
    ULONG Flags;
    ULONG Tag;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
    ULONG IoctlCode;
} ioctl_rsc_set_node_request_t;

typedef struct ioctl_rsc_set_node_response {
    LONG Status;
    ioctl_rsc_node_info_t Node;
} ioctl_rsc_set_node_response_t;

typedef struct ioctl_rsc_query_node_request {
    ULONG SubsystemId;
} ioctl_rsc_query_node_request_t;

typedef struct ioctl_rsc_query_node_response {
    LONG Status;
    ioctl_rsc_node_info_t Node;
} ioctl_rsc_query_node_response_t;

typedef struct ioctl_rsc_batch_entry_request {
    ULONG SubsystemId;
    CHAR SubsystemName[16];
    ULONG State;
    ULONG ClockKhz;
    ULONG Flags;
    ULONG Tag;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
} ioctl_rsc_batch_entry_request_t;

typedef struct ioctl_rsc_apply_batch_request {
    ULONG Count;
    ULONG Flags;
    ULONG IoctlCode;
    ULONG Reserved;
    ioctl_rsc_batch_entry_request_t Entries[1];
} ioctl_rsc_apply_batch_request_t;

typedef struct ioctl_rsc_apply_batch_response {
    LONG Status;
    ULONG RequestedCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    LONG FirstFailureStatus;
} ioctl_rsc_apply_batch_response_t;

typedef struct ioctl_rsc_catalog_entry_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG CapabilityMask;
    ULONG DefaultState;
    ULONG DefaultClockKhz;
    ULONG DefaultFlags;
    ULONG ResourceTag;
    ULONG SuggestedIoctl;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_catalog_entry_info_t;

typedef struct ioctl_rsc_catalog_response {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ULONG Reserved;
    ioctl_rsc_catalog_entry_info_t Entries[1];
} ioctl_rsc_catalog_response_t;

typedef struct ioctl_rsc_scenario_result_info {
    ULONG ScenarioId;
    ULONG Iterations;
    ULONG Flags;
    LONG Status;
    ULONG AppliedNodes;
    ULONG FailedNodes;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_scenario_result_info_t;

typedef struct ioctl_rsc_run_scenario_request {
    ULONG ScenarioId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} ioctl_rsc_run_scenario_request_t;

typedef struct ioctl_rsc_run_scenario_response {
    LONG Status;
    ioctl_rsc_scenario_result_info_t Result;
} ioctl_rsc_run_scenario_response_t;

typedef struct ioctl_rsc_scenario_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_scenario_result_info_t Entries[1];
} ioctl_rsc_scenario_history_response_t;

typedef struct ioctl_rsc_diag_entry_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG GlinkEventSeed;
    ULONG IcbErrorCode;
    ULONG HealthScore;
    ULONG ConfidenceScore;
    ULONG ActivityScore;
    ULONG CapabilityMask;
    LONG LastScenarioStatus;
    LONG LastNodeStatus;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_diag_entry_info_t;

typedef struct ioctl_rsc_diag_snapshot_response {
    ULONG Version;
    ULONG EntryCount;
    ULONG MatrixRunCount;
    ULONG MatrixFailureCount;
    LONG LastMatrixStatus;
    ULONGLONG LastMatrixTick100ns;
    ULONG LastMatrixFlags;
    ULONG LastMatrixIterations;
    ULONG LastMatrixFailures;
    ULONG Reserved;
    ioctl_rsc_diag_entry_info_t Entries[IOCTL_RSC_DIAG_INFO_CAPACITY];
} ioctl_rsc_diag_snapshot_response_t;

typedef struct ioctl_rsc_diag_result_info {
    ULONG Iterations;
    ULONG Flags;
    LONG Status;
    ULONG ScenarioRuns;
    ULONG ScenarioFailures;
    ULONG LastScenarioId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONGLONG Tick100ns;
} ioctl_rsc_diag_result_info_t;

typedef struct ioctl_rsc_diag_run_matrix_request {
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_diag_run_matrix_request_t;

typedef struct ioctl_rsc_diag_run_matrix_response {
    LONG Status;
    ioctl_rsc_diag_result_info_t Result;
} ioctl_rsc_diag_run_matrix_response_t;

typedef struct ioctl_rsc_diag_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_diag_result_info_t Entries[1];
} ioctl_rsc_diag_history_response_t;

typedef struct ioctl_rsc_profile_entry_info {
    ULONG ProfileId;
    CHAR Name[24];
    ULONG SubsystemMask;
    ULONG ScenarioFlags;
    ULONG MatrixFlags;
    ULONG DefaultIterations;
    ULONG Priority;
    ULONG Reserved;
} ioctl_rsc_profile_entry_info_t;

typedef struct ioctl_rsc_profile_catalog_response {
    ULONG Version;
    ULONG ProfileCount;
    ULONG ProfileRunCount;
    ULONG ProfileFailureCount;
    ULONG LastProfileId;
    LONG LastProfileStatus;
    ULONG LastProfileIterations;
    ULONG LastProfileFlags;
    ULONGLONG LastProfileTick100ns;
    ioctl_rsc_profile_entry_info_t Profiles[IOCTL_RSC_PROFILE_INFO_CAPACITY];
} ioctl_rsc_profile_catalog_response_t;

typedef struct ioctl_rsc_profile_result_info {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    LONG Status;
    ULONG ScenarioRuns;
    ULONG ScenarioFailures;
    ULONG MatrixRuns;
    ULONG MatrixFailures;
    ULONG LastScenarioId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONGLONG Tick100ns;
} ioctl_rsc_profile_result_info_t;

typedef struct ioctl_rsc_profile_run_request {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} ioctl_rsc_profile_run_request_t;

typedef struct ioctl_rsc_profile_run_response {
    LONG Status;
    ioctl_rsc_profile_result_info_t Result;
} ioctl_rsc_profile_run_response_t;

typedef struct ioctl_rsc_profile_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_profile_result_info_t Entries[1];
} ioctl_rsc_profile_history_response_t;

typedef struct ioctl_rsc_graph_link_info {
    ULONG FromSubsystemId;
    ULONG ToSubsystemId;
    ULONG Weight;
    ULONG LinkType;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_graph_link_info_t;

typedef struct ioctl_rsc_graph_snapshot_response {
    ULONG Version;
    ULONG NodeCount;
    ULONG LinkCount;
    ULONG ActiveMask;
    ULONG LastSequenceStatus;
    ULONG LastSequenceStepCount;
    ULONG LastSequenceFailureCount;
    ULONGLONG LastSequenceTick100ns;
    ioctl_rsc_graph_link_info_t Links[IOCTL_RSC_GRAPH_LINK_INFO_CAPACITY];
} ioctl_rsc_graph_snapshot_response_t;

typedef struct ioctl_rsc_action_sequence_step_request {
    ULONG SubsystemId;
    ULONG State;
    ULONG ClockKhz;
    ULONG Flags;
    ULONG Tag;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
    ULONG DelayUs;
    ULONG IoctlCode;
    ULONG Reserved;
} ioctl_rsc_action_sequence_step_request_t;

typedef struct ioctl_rsc_action_sequence_execute_request {
    ULONG StepCount;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ioctl_rsc_action_sequence_step_request_t Steps[1];
} ioctl_rsc_action_sequence_execute_request_t;

typedef struct ioctl_rsc_action_sequence_result_info {
    ULONG RequestedSteps;
    ULONG ExecutedSteps;
    ULONG FailedSteps;
    LONG Status;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_action_sequence_result_info_t;

typedef struct ioctl_rsc_action_sequence_execute_response {
    LONG Status;
    ioctl_rsc_action_sequence_result_info_t Result;
} ioctl_rsc_action_sequence_execute_response_t;

typedef struct ioctl_rsc_action_sequence_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_action_sequence_result_info_t Entries[1];
} ioctl_rsc_action_sequence_history_response_t;

typedef struct ioctl_rsc_action_sequence_preview_result_info {
    ULONG RequestedSteps;
    ULONG Flags;
    ULONG ExpandedSteps;
    ULONG InvalidSteps;
    ULONG DuplicateSteps;
    ULONG DependencyEdges;
    ULONG ReorderedSteps;
    ULONG ActiveMaskBefore;
    ULONG ActiveMaskAfter;
    LONG Status;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_action_sequence_preview_result_info_t;

typedef struct ioctl_rsc_action_sequence_preview_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastRequestedSteps;
    ULONG LastExpandedSteps;
    ULONG LastInvalidSteps;
    ULONG LastDuplicateSteps;
    ULONG LastDependencyEdges;
    ULONG LastReorderedSteps;
    ULONG LastActiveMaskBefore;
    ULONG LastActiveMaskAfter;
    ULONG LastFlags;
    ULONGLONG LastTick100ns;
} ioctl_rsc_action_sequence_preview_snapshot_response_t;

typedef struct ioctl_rsc_action_sequence_preview_request {
    ULONG StepCount;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ioctl_rsc_action_sequence_step_request_t Steps[1];
} ioctl_rsc_action_sequence_preview_request_t;

typedef struct ioctl_rsc_action_sequence_preview_response {
    LONG Status;
    ioctl_rsc_action_sequence_preview_result_info_t Result;
} ioctl_rsc_action_sequence_preview_response_t;

typedef struct ioctl_rsc_action_sequence_preview_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_action_sequence_preview_result_info_t Entries[1];
} ioctl_rsc_action_sequence_preview_history_response_t;

typedef struct ioctl_rsc_checkpoint_slot_info {
    ULONG SlotId;
    ULONG Valid;
    CHAR Name[24];
    ULONG CaptureCount;
    ULONG ApplyCount;
    LONG LastStatus;
    ULONG NodeCount;
    ULONG ActiveMask;
    ULONGLONG LastTick100ns;
} ioctl_rsc_checkpoint_slot_info_t;

typedef struct ioctl_rsc_checkpoint_result_info {
    ULONG Operation;
    ULONG SlotId;
    ULONG Flags;
    LONG Status;
    ULONG NodeCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_checkpoint_result_info_t;

typedef struct ioctl_rsc_checkpoint_snapshot_response {
    ULONG Version;
    ULONG SlotCount;
    ULONG ValidCount;
    ULONG CaptureCount;
    ULONG ApplyCount;
    LONG LastOperationStatus;
    ULONG LastOperationSlotId;
    ULONGLONG LastOperationTick100ns;
    ioctl_rsc_checkpoint_slot_info_t Slots[IOCTL_RSC_CHECKPOINT_INFO_CAPACITY];
} ioctl_rsc_checkpoint_snapshot_response_t;

typedef struct ioctl_rsc_checkpoint_capture_request {
    ULONG SlotId;
    CHAR Name[24];
    ULONG Flags;
    ULONG Reserved;
} ioctl_rsc_checkpoint_capture_request_t;

typedef struct ioctl_rsc_checkpoint_capture_response {
    LONG Status;
    ioctl_rsc_checkpoint_result_info_t Result;
} ioctl_rsc_checkpoint_capture_response_t;

typedef struct ioctl_rsc_checkpoint_apply_request {
    ULONG SlotId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_checkpoint_apply_request_t;

typedef struct ioctl_rsc_checkpoint_apply_response {
    LONG Status;
    ioctl_rsc_checkpoint_result_info_t Result;
} ioctl_rsc_checkpoint_apply_response_t;

typedef struct ioctl_rsc_checkpoint_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_checkpoint_result_info_t Entries[1];
} ioctl_rsc_checkpoint_history_response_t;

typedef struct ioctl_rsc_coverage_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG Present;
    ULONG Active;
    ULONG RequestCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONG SuccessPermille;
    ULONG DependencyIn;
    ULONG DependencyOut;
    ULONG HealthScore;
    ULONG ActivityScore;
    ULONG Flags;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_coverage_row_info_t;

typedef struct ioctl_rsc_coverage_result_info {
    ULONG Flags;
    ULONG NodeCount;
    LONG Status;
    ULONG HealthScore;
    ULONG ActivityScore;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG BelowThresholdCount;
    ULONG ThresholdPermille;
    ULONG ActiveMaskBefore;
    ULONG ActiveMaskAfter;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_coverage_result_info_t;

typedef struct ioctl_rsc_coverage_snapshot_response {
    ULONG Version;
    ULONG NodeCount;
    ULONG ActiveMask;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastHealthScore;
    ULONG LastActivityScore;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastThresholdPermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_coverage_row_info_t Rows[IOCTL_RSC_COVERAGE_INFO_CAPACITY];
} ioctl_rsc_coverage_snapshot_response_t;

typedef struct ioctl_rsc_coverage_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_coverage_run_request_t;

typedef struct ioctl_rsc_coverage_run_response {
    LONG Status;
    ioctl_rsc_coverage_result_info_t Result;
} ioctl_rsc_coverage_run_response_t;

typedef struct ioctl_rsc_coverage_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_coverage_result_info_t Entries[1];
} ioctl_rsc_coverage_history_response_t;

typedef struct ioctl_rsc_remediation_action_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG Priority;
    ULONG ReasonFlags;
    ULONG CurrentState;
    ULONG RecommendedState;
    ULONG CurrentClockKhz;
    ULONG RecommendedClockKhz;
    ULONG CurrentFlags;
    ULONG RecommendedFlags;
    ULONG HealthScore;
    ULONG ActivityScore;
    ULONG SuccessPermille;
    ULONG DependencyIn;
    ULONG DependencyOut;
    LONG LastStatus;
} ioctl_rsc_remediation_action_info_t;

typedef struct ioctl_rsc_remediation_result_info {
    ULONG Flags;
    LONG Status;
    ULONG RecommendedCount;
    ULONG ExecutedCount;
    ULONG FailedCount;
    ULONG CoverageThresholdPermille;
    ULONG CoverageHealthScore;
    ULONG CoverageActivityScore;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG WorstSubsystemId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_remediation_result_info_t;

typedef struct ioctl_rsc_remediation_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastRecommendedCount;
    ULONG LastExecutedCount;
    ULONG LastFailedCount;
    ULONG LastCoverageThresholdPermille;
    ULONG LastCoverageHealthScore;
    ULONG LastCoverageActivityScore;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastWorstSubsystemId;
    ULONGLONG LastTick100ns;
    ULONG ActionCount;
    ioctl_rsc_remediation_action_info_t Actions[IOCTL_RSC_REMEDIATION_INFO_CAPACITY];
} ioctl_rsc_remediation_snapshot_response_t;

typedef struct ioctl_rsc_remediation_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_remediation_run_request_t;

typedef struct ioctl_rsc_remediation_run_response {
    LONG Status;
    ioctl_rsc_remediation_result_info_t Result;
} ioctl_rsc_remediation_run_response_t;

typedef struct ioctl_rsc_remediation_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_remediation_result_info_t Entries[1];
} ioctl_rsc_remediation_history_response_t;

typedef struct ioctl_rsc_stabilization_lane_info {
    ULONG LaneId;
    CHAR Name[16];
    ULONG SubsystemMask;
    ULONG NodeCount;
    ULONG DegradedCount;
    ULONG HealthScore;
    ULONG ActivityScore;
    ULONG WarningCount;
    ULONG ErrorCount;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_stabilization_lane_info_t;

typedef struct ioctl_rsc_stabilization_result_info {
    ULONG Flags;
    LONG Status;
    ULONG LaneCount;
    ULONG DegradedLaneCount;
    LONG RemediationStatus;
    ULONG CoverageHealthScore;
    ULONG CoverageActivityScore;
    ULONG CoverageWarningCount;
    ULONG CoverageErrorCount;
    ULONG ProfileRuns;
    ULONG ProfileFailures;
    ULONG LastLaneId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_stabilization_result_info_t;

typedef struct ioctl_rsc_stabilization_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastLaneCount;
    ULONG LastDegradedLaneCount;
    LONG LastRemediationStatus;
    ULONG LastCoverageHealthScore;
    ULONG LastCoverageActivityScore;
    ULONG LastCoverageWarningCount;
    ULONG LastCoverageErrorCount;
    ULONG LastProfileRuns;
    ULONG LastProfileFailures;
    ULONGLONG LastTick100ns;
    ULONG LaneCount;
    ioctl_rsc_stabilization_lane_info_t Lanes[IOCTL_RSC_STABILIZATION_LANE_INFO_CAPACITY];
} ioctl_rsc_stabilization_snapshot_response_t;

typedef struct ioctl_rsc_stabilization_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_stabilization_run_request_t;

typedef struct ioctl_rsc_stabilization_run_response {
    LONG Status;
    ioctl_rsc_stabilization_result_info_t Result;
} ioctl_rsc_stabilization_run_response_t;

typedef struct ioctl_rsc_stabilization_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_stabilization_result_info_t Entries[1];
} ioctl_rsc_stabilization_history_response_t;

typedef struct ioctl_rsc_conformance_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG GlinkEventSeed;
    ULONG CapabilityMask;
    ULONG ResourceTag;
    ULONG SuggestedIoctl;
    ULONG DependencyIn;
    ULONG DependencyOut;
    ULONG Active;
    ULONG SuccessPermille;
    ULONG ActivityScore;
    ULONG ConformanceScorePermille;
    ULONG ReasonFlags;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_conformance_row_info_t;

typedef struct ioctl_rsc_conformance_result_info {
    ULONG Flags;
    LONG Status;
    ULONG NodeCount;
    ULONG PassingCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG OverallScorePermille;
    ULONG ThresholdPermille;
    ULONG WorstSubsystemId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_conformance_result_info_t;

typedef struct ioctl_rsc_conformance_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastOverallScorePermille;
    ULONG LastThresholdPermille;
    ULONG LastPassingCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastWorstSubsystemId;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_conformance_row_info_t Rows[IOCTL_RSC_CONFORMANCE_INFO_CAPACITY];
} ioctl_rsc_conformance_snapshot_response_t;

typedef struct ioctl_rsc_conformance_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_conformance_run_request_t;

typedef struct ioctl_rsc_conformance_run_response {
    LONG Status;
    ioctl_rsc_conformance_result_info_t Result;
} ioctl_rsc_conformance_run_response_t;

typedef struct ioctl_rsc_conformance_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_conformance_result_info_t Entries[1];
} ioctl_rsc_conformance_history_response_t;

typedef struct ioctl_rsc_orchestration_stage_info {
    ULONG StageId;
    CHAR StageName[16];
    ULONG Enabled;
    LONG Status;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG ScorePermille;
    ULONG Detail0;
    ULONG Detail1;
    ULONG Detail2;
    ULONG Detail3;
    ULONGLONG Tick100ns;
} ioctl_rsc_orchestration_stage_info_t;

typedef struct ioctl_rsc_orchestration_result_info {
    ULONG Flags;
    LONG Status;
    ULONG StageCount;
    ULONG SuccessfulStages;
    ULONG WarningStages;
    ULONG FailedStages;
    ULONG OverallScorePermille;
    ULONG LastStageId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_orchestration_result_info_t;

typedef struct ioctl_rsc_orchestration_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastOverallScorePermille;
    ULONG LastStageCount;
    ULONG LastSuccessfulStages;
    ULONG LastWarningStages;
    ULONG LastFailedStages;
    ULONG LastStageId;
    ULONGLONG LastTick100ns;
    ULONG StageCount;
    ioctl_rsc_orchestration_stage_info_t Stages[IOCTL_RSC_ORCHESTRATION_STAGE_INFO_CAPACITY];
} ioctl_rsc_orchestration_snapshot_response_t;

typedef struct ioctl_rsc_orchestration_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_orchestration_run_request_t;

typedef struct ioctl_rsc_orchestration_run_response {
    LONG Status;
    ioctl_rsc_orchestration_result_info_t Result;
} ioctl_rsc_orchestration_run_response_t;

typedef struct ioctl_rsc_orchestration_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_orchestration_result_info_t Entries[1];
} ioctl_rsc_orchestration_history_response_t;

typedef struct ioctl_rsc_policy_resource_info {
    ULONG ResourceId;
    ULONG ParentResourceId;
    ULONG SubsystemId;
    ULONG ParentSubsystemId;
    ULONG ResourceType;
    ULONG ActiveVoteCount;
    ULONG SetMask;
    ULONG DriverMapId;
    ULONG Flags;
    LONG LastStatus;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG LastTick100ns;
} ioctl_rsc_policy_resource_info_t;

typedef struct ioctl_rsc_policy_event_info {
    ULONGLONG Sequence;
    ULONGLONG Tick100ns;
    ULONG Flags;
    LONG Status;
    ULONG ResourceId;
    ULONG ParentResourceId;
    ULONG SubsystemId;
    ULONG ParentSubsystemId;
    ULONG ResourceType;
    ULONG ActiveVoteCount;
    ULONG SetMask;
    ULONG DriverMapId;
    ULONG Tag;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG Value3;
    ULONG IoctlCode;
} ioctl_rsc_policy_event_info_t;

typedef struct ioctl_rsc_policy_snapshot_response {
    ULONG Version;
    ULONG ResourceCount;
    ULONG ActiveResourceCount;
    ULONGLONG EventSequence;
    ULONG VoteCount;
    ULONG RemoveCount;
    ULONG PropagationCount;
    ULONG LastResourceId;
    ULONG LastSubsystemId;
    ULONG LastParentSubsystemId;
    ULONG LastIoctlCode;
    LONG LastStatus;
    ULONG LastFlags;
    ULONGLONG LastTick100ns;
    ULONG ResourceEntryCount;
    ioctl_rsc_policy_resource_info_t Resources[IOCTL_RSC_POLICY_RESOURCE_INFO_CAPACITY];
} ioctl_rsc_policy_snapshot_response_t;

typedef struct ioctl_rsc_policy_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_policy_event_info_t Entries[1];
} ioctl_rsc_policy_history_response_t;

typedef struct ioctl_rsc_fabric_lane_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG Present;
    ULONG Active;
    ULONG State;
    ULONG ClockKhz;
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG PolicyResourceCount;
    ULONG PolicyActiveVoteCount;
    ULONG DependencyIn;
    ULONG DependencyOut;
    ULONG RequestCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONG SuccessPermille;
    ULONG ActivityScore;
    ULONG HealthScore;
    LONG ThermalDeciC;
    ULONG Flags;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_fabric_lane_info_t;

typedef struct ioctl_rsc_fabric_result_info {
    ULONG Flags;
    LONG Status;
    ULONG LaneCount;
    ULONG ActiveLaneCount;
    ULONG PolicyActiveLaneCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG OverallHealthScore;
    ULONG OverallActivityScore;
    LONG ThermalMaxDeciC;
    LONG ThermalMinDeciC;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_fabric_result_info_t;

typedef struct ioctl_rsc_fabric_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastLaneCount;
    ULONG LastActiveLaneCount;
    ULONG LastPolicyActiveLaneCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastOverallHealthScore;
    ULONG LastOverallActivityScore;
    LONG LastThermalMaxDeciC;
    LONG LastThermalMinDeciC;
    ULONGLONG LastTick100ns;
    ULONG LaneCount;
    ioctl_rsc_fabric_lane_info_t Lanes[IOCTL_RSC_FABRIC_LANE_INFO_CAPACITY];
} ioctl_rsc_fabric_snapshot_response_t;

typedef struct ioctl_rsc_fabric_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_fabric_run_request_t;

typedef struct ioctl_rsc_fabric_run_response {
    LONG Status;
    ioctl_rsc_fabric_result_info_t Result;
} ioctl_rsc_fabric_run_response_t;

typedef struct ioctl_rsc_fabric_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_fabric_result_info_t Entries[1];
} ioctl_rsc_fabric_history_response_t;

typedef struct ioctl_rsc_domain_info {
    ULONG DomainId;
    CHAR Name[16];
    ULONG SubsystemMask;
    ULONG LaneCount;
    ULONG ActiveLaneCount;
    ULONG PolicyActiveLaneCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG AvgHealthScore;
    ULONG AvgActivityScore;
    LONG ThermalMaxDeciC;
    LONG ThermalMinDeciC;
    ULONG LastSubsystemId;
    LONG LastStatus;
    ULONG Flags;
    ULONGLONG LastTick100ns;
} ioctl_rsc_domain_info_t;

typedef struct ioctl_rsc_domain_result_info {
    ULONG Flags;
    LONG Status;
    ULONG DomainCount;
    ULONG HealthyDomainCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG OverallHealthScore;
    ULONG OverallActivityScore;
    ULONG LastDomainId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_domain_result_info_t;

typedef struct ioctl_rsc_domain_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastDomainCount;
    ULONG LastHealthyDomainCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastOverallHealthScore;
    ULONG LastOverallActivityScore;
    ULONGLONG LastTick100ns;
    ULONG DomainCount;
    ioctl_rsc_domain_info_t Domains[IOCTL_RSC_DOMAIN_INFO_CAPACITY];
} ioctl_rsc_domain_snapshot_response_t;

typedef struct ioctl_rsc_domain_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_domain_run_request_t;

typedef struct ioctl_rsc_domain_run_response {
    LONG Status;
    ioctl_rsc_domain_result_info_t Result;
} ioctl_rsc_domain_run_response_t;

typedef struct ioctl_rsc_domain_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_domain_result_info_t Entries[1];
} ioctl_rsc_domain_history_response_t;

typedef struct ioctl_rsc_interconnect_route_info {
    ULONG RouteId;
    CHAR Name[24];
    ULONG SubsystemId;
    CHAR SubsystemName[16];
    ULONG MasterId;
    CHAR MasterName[48];
    ULONG SlaveId;
    CHAR SlaveName[48];
    ULONG Active;
    ULONG PolicyLinked;
    ULONG RequiredBandwidthMb;
    ULONG GrantedBandwidthMb;
    ULONG UtilizationPermille;
    ULONG VoteCount;
    ULONG DependencyIn;
    ULONG DependencyOut;
    LONG ThermalDeciC;
    ULONG Flags;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_interconnect_route_info_t;

typedef struct ioctl_rsc_interconnect_result_info {
    ULONG Flags;
    LONG Status;
    ULONG RouteCount;
    ULONG ActiveRouteCount;
    ULONG PolicyLinkedRouteCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG AvgUtilizationPermille;
    ULONG PeakUtilizationPermille;
    ULONG TotalRequiredBandwidthMb;
    ULONG TotalGrantedBandwidthMb;
    ULONG LastRouteId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_interconnect_result_info_t;

typedef struct ioctl_rsc_interconnect_snapshot_response {
    ULONG Version;
    ULONG LastFlags;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastRouteCount;
    ULONG LastActiveRouteCount;
    ULONG LastPolicyLinkedRouteCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastAvgUtilizationPermille;
    ULONG LastPeakUtilizationPermille;
    ULONG LastTotalRequiredBandwidthMb;
    ULONG LastTotalGrantedBandwidthMb;
    ULONGLONG LastTick100ns;
    ULONG RouteCount;
    ioctl_rsc_interconnect_route_info_t Routes[IOCTL_RSC_INTERCONNECT_ROUTE_INFO_CAPACITY];
} ioctl_rsc_interconnect_snapshot_response_t;

typedef struct ioctl_rsc_interconnect_run_request {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} ioctl_rsc_interconnect_run_request_t;

typedef struct ioctl_rsc_interconnect_run_response {
    LONG Status;
    ioctl_rsc_interconnect_result_info_t Result;
} ioctl_rsc_interconnect_run_response_t;

typedef struct ioctl_rsc_interconnect_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_interconnect_result_info_t Entries[1];
} ioctl_rsc_interconnect_history_response_t;

typedef struct ioctl_rsc_topology_node_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG GlinkEventSeed;
    ULONG IcbErrorCode;
    ULONG RepresentativeMasterId;
    CHAR RepresentativeMasterName[48];
    ULONG RepresentativeSlaveId;
    CHAR RepresentativeSlaveName[48];
    ULONG DomainMask;
    ULONG ScenarioId;
    ULONG ProfileMask;
    ULONG CapabilityMask;
    ULONG ResourceTag;
    ULONG SuggestedIoctl;
} ioctl_rsc_topology_node_info_t;

typedef struct ioctl_rsc_topology_route_info {
    ULONG RouteId;
    CHAR Name[24];
    ULONG SubsystemId;
    CHAR SubsystemName[16];
    ULONG DomainId;
    ULONG MasterId;
    CHAR MasterName[48];
    ULONG SlaveId;
    CHAR SlaveName[48];
    ULONG BaseBandwidthMb;
    ULONG PeakBandwidthMb;
} ioctl_rsc_topology_route_info_t;

typedef struct ioctl_rsc_topology_snapshot_response {
    ULONG Version;
    ULONG NodeCount;
    ULONG RouteCount;
    ULONG DriverMapCount;
    ULONG PppResourceNameCount;
    ULONG IcbMasterNameCount;
    ULONG IcbSlaveNameCount;
    ULONG Reserved;
    ioctl_rsc_topology_node_info_t Nodes[IOCTL_RSC_TOPOLOGY_NODE_INFO_CAPACITY];
    ioctl_rsc_topology_route_info_t Routes[IOCTL_RSC_TOPOLOGY_ROUTE_INFO_CAPACITY];
} ioctl_rsc_topology_snapshot_response_t;

typedef struct ioctl_rsc_enum_entry_info {
    ULONG Value;
    CHAR Name[48];
} ioctl_rsc_enum_entry_info_t;

typedef struct ioctl_rsc_subsystem_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG GlinkEventType;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG CmdDbQueryType;
    ULONG PdcDrvType;
    ULONG PdcTriggerType;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG TlmmDriveType;
    ULONG TsensThresholdType;
    ULONG ClockDriverType;
    ULONG AopSubsystemType;
} ioctl_rsc_subsystem_info_t;

typedef struct ioctl_rsc_subsystem_catalog_response {
    ULONG Version;
    ULONG SubsystemCount;
    ULONG AopSubsystemCount;
    ULONG ArcResourceCount;
    ULONG CmdDbSlvCount;
    ULONG PdcDrvCount;
    ULONG PdcTriggerCount;
    ULONG RpmhSetCount;
    ULONG NpaClientCount;
    ULONG GlinkEventCount;
    ULONG TlmmDriveCount;
    ULONG TsensThresholdCount;
    ULONG ClockDriverCount;
    ULONG CmdDbQueryCount;
    ULONG Reserved0;
    ULONG Reserved1;
    ioctl_rsc_subsystem_info_t Subsystems[IOCTL_RSC_SUBSYSTEM_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t AopSubsystems[IOCTL_RSC_AOP_SUBSYSTEM_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t ArcResources[IOCTL_RSC_ARC_RESOURCE_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t CmdDbSlvIds[IOCTL_RSC_CMDDB_SLV_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t PdcDrvTypes[IOCTL_RSC_PDC_DRV_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t PdcTriggers[IOCTL_RSC_PDC_TRIGGER_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t RpmhSets[IOCTL_RSC_RPMH_SET_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t NpaClientTypes[IOCTL_RSC_NPA_CLIENT_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t GlinkEvents[IOCTL_RSC_GLINK_EVENT_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t TlmmDriveTypes[IOCTL_RSC_TLMM_DRIVE_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t TsensThresholds[IOCTL_RSC_TSENS_THRESHOLD_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t ClockDriverTypes[IOCTL_RSC_CLOCK_DRIVER_INFO_CAPACITY];
    ioctl_rsc_enum_entry_info_t CmdDbQueryTypes[IOCTL_RSC_CMDDB_QUERY_INFO_CAPACITY];
} ioctl_rsc_subsystem_catalog_response_t;

typedef struct ioctl_rsc_sync_lane_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ProfileId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG TargetState;
    ULONG TargetClockKhz;
    ULONG TargetFlags;
    ULONG ReasonFlags;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_sync_lane_info_t;

typedef struct ioctl_rsc_sync_result_info {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    LONG Status;
    ULONG AppliedCount;
    ULONG FailedCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_sync_result_info_t;

typedef struct ioctl_rsc_sync_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastProfileId;
    ULONG LastFlags;
    ULONG LastIterations;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONGLONG LastTick100ns;
    ULONG LaneCount;
    ioctl_rsc_sync_lane_info_t Lanes[IOCTL_RSC_SYNC_LANE_INFO_CAPACITY];
} ioctl_rsc_sync_snapshot_response_t;

typedef struct ioctl_rsc_sync_run_request {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} ioctl_rsc_sync_run_request_t;

typedef struct ioctl_rsc_sync_run_response {
    LONG Status;
    ioctl_rsc_sync_result_info_t Result;
} ioctl_rsc_sync_run_response_t;

typedef struct ioctl_rsc_sync_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_sync_result_info_t Entries[1];
} ioctl_rsc_sync_history_response_t;

typedef struct ioctl_rsc_validate_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG IssueMask;
    ULONG WarningMask;
    ULONG ScorePermille;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG CmdDbQueryType;
    ULONG PdcDrvType;
    ULONG PdcTriggerType;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    ULONG TlmmDriveType;
    ULONG TsensThresholdType;
    ULONG ClockDriverType;
    ULONG AopSubsystemType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_validate_row_info_t;

typedef struct ioctl_rsc_validate_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG CheckedCount;
    ULONG RepairedCount;
    ULONG ErrorCount;
    ULONG WarningCount;
    ULONG OverallScorePermille;
    ULONG LastSubsystemId;
    ULONG LastIssueMask;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_validate_result_info_t;

typedef struct ioctl_rsc_validate_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastCheckedCount;
    ULONG LastRepairedCount;
    ULONG LastErrorCount;
    ULONG LastWarningCount;
    ULONG LastOverallScorePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_validate_row_info_t Rows[IOCTL_RSC_VALIDATE_ROW_INFO_CAPACITY];
} ioctl_rsc_validate_snapshot_response_t;

typedef struct ioctl_rsc_validate_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_validate_run_request_t;

typedef struct ioctl_rsc_validate_run_response {
    LONG Status;
    ioctl_rsc_validate_result_info_t Result;
} ioctl_rsc_validate_run_response_t;

typedef struct ioctl_rsc_validate_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_validate_result_info_t Entries[1];
} ioctl_rsc_validate_history_response_t;

typedef struct ioctl_rsc_sequence_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG PrimaryPhase;
    ULONG PhaseMask;
    ULONG ActiveState;
    ULONG SleepState;
    ULONG WakeState;
    ULONG ActiveClockKhz;
    ULONG SleepClockKhz;
    ULONG WakeClockKhz;
    ULONG ActiveFlags;
    ULONG SleepFlags;
    ULONG WakeFlags;
    ULONG CmdDbSlaveId;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    LONG LastStatus;
    ULONG LastPhase;
    ULONGLONG LastTick100ns;
} ioctl_rsc_sequence_row_info_t;

typedef struct ioctl_rsc_sequence_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG ExecutedPhaseMask;
    ULONG StepCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    ULONG PolicyVoteCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG LastSubsystemId;
    ULONG LastPhase;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_sequence_result_info_t;

typedef struct ioctl_rsc_sequence_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPhaseMask;
    ULONG LastStepCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastPolicyVoteCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_sequence_row_info_t Rows[IOCTL_RSC_SEQUENCE_ROW_INFO_CAPACITY];
} ioctl_rsc_sequence_snapshot_response_t;

typedef struct ioctl_rsc_sequence_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_sequence_run_request_t;

typedef struct ioctl_rsc_sequence_run_response {
    LONG Status;
    ioctl_rsc_sequence_result_info_t Result;
} ioctl_rsc_sequence_run_response_t;

typedef struct ioctl_rsc_sequence_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_sequence_result_info_t Entries[1];
} ioctl_rsc_sequence_history_response_t;

typedef struct ioctl_rsc_lookup_match_info {
    ULONG Kind;
    ULONG Id;
    CHAR Name[64];
    ULONG SubsystemMask;
    ULONG ScorePermille;
    ULONG Aux0;
    ULONG Aux1;
} ioctl_rsc_lookup_match_info_t;

typedef struct ioctl_rsc_lookup_result_info {
    ULONG QueryType;
    ULONG QueryId;
    ULONG Flags;
    LONG Status;
    ULONG MatchCount;
    ULONG OverflowCount;
    ULONG PatternHash;
    ULONG LastKind;
    ULONG LastId;
    ULONG LastSubsystemMask;
    ULONG LastScorePermille;
    ULONG LastAux0;
    ULONG LastAux1;
    ULONGLONG Tick100ns;
} ioctl_rsc_lookup_result_info_t;

typedef struct ioctl_rsc_lookup_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    ioctl_rsc_lookup_result_info_t LastResult;
    ULONG LastMatchCount;
    ioctl_rsc_lookup_match_info_t LastMatches[IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY];
} ioctl_rsc_lookup_snapshot_response_t;

typedef struct ioctl_rsc_lookup_run_request {
    ULONG QueryType;
    ULONG QueryId;
    ULONG Flags;
    CHAR QueryText[64];
} ioctl_rsc_lookup_run_request_t;

typedef struct ioctl_rsc_lookup_run_response {
    LONG Status;
    ioctl_rsc_lookup_result_info_t Result;
    ULONG MatchCount;
    ULONG OverflowCount;
    ioctl_rsc_lookup_match_info_t Matches[IOCTL_RSC_LOOKUP_MATCH_INFO_CAPACITY];
} ioctl_rsc_lookup_run_response_t;

typedef struct ioctl_rsc_lookup_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_lookup_result_info_t Entries[1];
} ioctl_rsc_lookup_history_response_t;

typedef struct ioctl_rsc_provision_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG AopSubsystemType;
    ULONG ClockDriverType;
    ULONG TlmmDriveType;
    ULONG TsensThresholdType;
    ULONG TargetState;
    ULONG TargetClockKhz;
    ULONG TargetFlags;
    ULONG PolicyFlags;
    ULONG ConfidencePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_provision_row_info_t;

typedef struct ioctl_rsc_provision_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PreparedCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    ULONG PolicyVoteCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG AvgConfidencePermille;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_provision_result_info_t;

typedef struct ioctl_rsc_provision_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPreparedCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastPolicyVoteCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_provision_row_info_t Rows[IOCTL_RSC_PROVISION_ROW_INFO_CAPACITY];
} ioctl_rsc_provision_snapshot_response_t;

typedef struct ioctl_rsc_provision_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_provision_run_request_t;

typedef struct ioctl_rsc_provision_run_response {
    LONG Status;
    ioctl_rsc_provision_result_info_t Result;
} ioctl_rsc_provision_run_response_t;

typedef struct ioctl_rsc_provision_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_provision_result_info_t Entries[1];
} ioctl_rsc_provision_history_response_t;

typedef struct ioctl_rsc_rail_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG AopSubsystemType;
    ULONG ClockDriverType;
    ULONG TlmmDriveType;
    ULONG TsensThresholdType;
    ULONG PdcTriggerType;
    ULONG TargetState;
    ULONG TargetClockKhz;
    ULONG TargetFlags;
    ULONG RailMicrovolts;
    ULONG BandwidthIb;
    ULONG BandwidthAb;
    ULONG PolicyFlags;
    ULONG ConfidencePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_rail_row_info_t;

typedef struct ioctl_rsc_rail_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PreparedCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    ULONG RailVoteCount;
    ULONG BandwidthVoteCount;
    ULONG PolicyVoteCount;
    ULONG GlinkEventCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG AvgConfidencePermille;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_rail_result_info_t;

typedef struct ioctl_rsc_rail_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPreparedCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastRailVoteCount;
    ULONG LastBandwidthVoteCount;
    ULONG LastPolicyVoteCount;
    ULONG LastGlinkEventCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_rail_row_info_t Rows[IOCTL_RSC_RAIL_ROW_INFO_CAPACITY];
} ioctl_rsc_rail_snapshot_response_t;

typedef struct ioctl_rsc_rail_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_rail_run_request_t;

typedef struct ioctl_rsc_rail_run_response {
    LONG Status;
    ioctl_rsc_rail_result_info_t Result;
} ioctl_rsc_rail_run_response_t;

typedef struct ioctl_rsc_rail_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_rail_result_info_t Entries[1];
} ioctl_rsc_rail_history_response_t;

typedef struct ioctl_rsc_tcs_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG CmdDbQueryType;
    ULONG RpmhSet;
    ULONG RpmhPriority;
    ULONG TcsCount;
    ULONG CommandAddress;
    ULONG CommandData;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG PdcTriggerType;
    ULONG AopSubsystemType;
    ULONG ClockDriverType;
    ULONG TsensThresholdType;
    ULONG TargetState;
    ULONG TargetClockKhz;
    ULONG TargetFlags;
    ULONG PolicyFlags;
    ULONG CommandFlags;
    ULONG ConfidencePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_tcs_row_info_t;

typedef struct ioctl_rsc_tcs_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PreparedCount;
    ULONG ProgrammedCount;
    ULONG FailedCount;
    ULONG PolicyVoteCount;
    ULONG QueuedGlinkCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG AvgConfidencePermille;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_tcs_result_info_t;

typedef struct ioctl_rsc_tcs_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPreparedCount;
    ULONG LastProgrammedCount;
    ULONG LastFailedCount;
    ULONG LastPolicyVoteCount;
    ULONG LastQueuedGlinkCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_tcs_row_info_t Rows[IOCTL_RSC_TCS_ROW_INFO_CAPACITY];
} ioctl_rsc_tcs_snapshot_response_t;

typedef struct ioctl_rsc_tcs_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_tcs_run_request_t;

typedef struct ioctl_rsc_tcs_run_response {
    LONG Status;
    ioctl_rsc_tcs_result_info_t Result;
} ioctl_rsc_tcs_run_response_t;

typedef struct ioctl_rsc_tcs_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_tcs_result_info_t Entries[1];
} ioctl_rsc_tcs_history_response_t;

typedef struct ioctl_rsc_thermal_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG CmdDbQueryType;
    ULONG RpmhSet;
    ULONG NpaClientType;
    ULONG GlinkEventType;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG TsensThresholdType;
    ULONG TlmmDriveType;
    ULONG ClockDriverType;
    ULONG PdcTriggerType;
    ULONG AopSubsystemType;
    ULONG SensorIndex;
    LONG SampleTempDeciC;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG ThermalPressurePermille;
    ULONG TargetState;
    ULONG TargetClockKhz;
    ULONG TargetFlags;
    ULONG PolicyFlags;
    ULONG ActionFlags;
    ULONG ConfidencePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_thermal_row_info_t;

typedef struct ioctl_rsc_thermal_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PreparedCount;
    ULONG SampledCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    ULONG MitigatedCount;
    ULONG RecoveredCount;
    ULONG PolicyVoteCount;
    ULONG GlinkEventCount;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG AvgThermalPressurePermille;
    ULONG AvgConfidencePermille;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_thermal_result_info_t;

typedef struct ioctl_rsc_thermal_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPreparedCount;
    ULONG LastSampledCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastMitigatedCount;
    ULONG LastRecoveredCount;
    ULONG LastPolicyVoteCount;
    ULONG LastGlinkEventCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgThermalPressurePermille;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_thermal_row_info_t Rows[IOCTL_RSC_THERMAL_ROW_INFO_CAPACITY];
} ioctl_rsc_thermal_snapshot_response_t;

typedef struct ioctl_rsc_thermal_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_thermal_run_request_t;

typedef struct ioctl_rsc_thermal_run_response {
    LONG Status;
    ioctl_rsc_thermal_result_info_t Result;
} ioctl_rsc_thermal_run_response_t;

typedef struct ioctl_rsc_thermal_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_thermal_result_info_t Entries[1];
} ioctl_rsc_thermal_history_response_t;

typedef struct ioctl_rsc_coord_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG StateBefore;
    ULONG ClockBeforeKhz;
    ULONG FlagsBefore;
    ULONG StateAfter;
    ULONG ClockAfterKhz;
    ULONG FlagsAfter;
    ULONG PhaseStatusMask;
    ULONG FailureMask;
    ULONG ConfidencePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_coord_row_info_t;

typedef struct ioctl_rsc_coord_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PreparedCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    ULONG ExecutedPhaseMask;
    ULONG FailedPhaseMask;
    LONG SyncStatus;
    LONG ValidateStatus;
    LONG SequenceStatus;
    LONG ProvisionStatus;
    LONG RailStatus;
    LONG TcsStatus;
    LONG ThermalStatus;
    LONG FabricStatus;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG AvgConfidencePermille;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_coord_result_info_t;

typedef struct ioctl_rsc_coord_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPreparedCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastExecutedPhaseMask;
    ULONG LastFailedPhaseMask;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_coord_row_info_t Rows[IOCTL_RSC_COORD_ROW_INFO_CAPACITY];
} ioctl_rsc_coord_snapshot_response_t;

typedef struct ioctl_rsc_coord_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_coord_run_request_t;

typedef struct ioctl_rsc_coord_run_response {
    LONG Status;
    ioctl_rsc_coord_result_info_t Result;
} ioctl_rsc_coord_run_response_t;

typedef struct ioctl_rsc_coord_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_coord_result_info_t Entries[1];
} ioctl_rsc_coord_history_response_t;

typedef struct ioctl_rsc_audit_row_info {
    ULONG SubsystemId;
    CHAR Name[16];
    ULONG ModeId;
    ULONG DriverMapId;
    ULONG ArcResourceType;
    ULONG CmdDbSlaveId;
    ULONG CmdDbQueryType;
    ULONG PppResourceType;
    ULONG IcbMasterId;
    ULONG IcbSlaveId;
    ULONG AopSubsystemType;
    ULONG DomainId;
    ULONG DomainCount;
    ULONG RouteIdPrimary;
    ULONG RouteCount;
    ULONG RoutePeakBandwidthMb;
    ULONG DriverMapFanout;
    ULONG IntegrityPenaltyPermille;
    ULONG IntegrityFlags;
    ULONG DependencyIn;
    ULONG DependencyOut;
    ULONG DependencyRiskPermille;
    ULONG DomainCoveragePermille;
    ULONG RouteCoveragePermille;
    ULONG TopologyPressurePermille;
    ULONG SemanticCoveragePermille;
    ULONG ProtocolDriftPermille;
    ULONG StateBefore;
    ULONG ClockBeforeKhz;
    ULONG FlagsBefore;
    ULONG StateAfter;
    ULONG ClockAfterKhz;
    ULONG FlagsAfter;
    ULONG IssueMask;
    ULONG WarningMask;
    ULONG ScorePermille;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_audit_row_info_t;

typedef struct ioctl_rsc_audit_result_info {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG CheckedCount;
    ULONG RepairedCount;
    ULONG ErrorCount;
    ULONG WarningCount;
    ULONG OverallScorePermille;
    LONG ValidateStatus;
    LONG CoordStatus;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG LastSubsystemId;
    ULONG LastIssueMask;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_audit_result_info_t;

typedef struct ioctl_rsc_audit_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastCheckedCount;
    ULONG LastRepairedCount;
    ULONG LastErrorCount;
    ULONG LastWarningCount;
    ULONG LastOverallScorePermille;
    LONG LastValidateStatus;
    LONG LastCoordStatus;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    ioctl_rsc_audit_row_info_t Rows[IOCTL_RSC_AUDIT_ROW_INFO_CAPACITY];
} ioctl_rsc_audit_snapshot_response_t;

typedef struct ioctl_rsc_audit_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} ioctl_rsc_audit_run_request_t;

typedef struct ioctl_rsc_audit_run_response {
    LONG Status;
    ioctl_rsc_audit_result_info_t Result;
} ioctl_rsc_audit_run_response_t;

typedef struct ioctl_rsc_audit_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_audit_result_info_t Entries[1];
} ioctl_rsc_audit_history_response_t;

typedef struct ioctl_rsc_supervisor_module_info {
    ULONG ModuleId;
    CHAR Name[16];
    ULONG IssueCount;
    ULONG WarningCount;
    ULONG RepairCount;
    ULONG SampleCount;
    ULONG ScorePermille;
    ULONG IntegrityPenaltyPermilleAvg;
    ULONG DependencyGapCount;
    ULONG DomainGapCount;
    ULONG RouteGapCount;
    ULONG FanoutViolationCount;
    ULONG CoverageScorePermilleAvg;
    ULONG TopologyPressurePermilleAvg;
    ULONG SemanticCoveragePermilleAvg;
    ULONG ProtocolDriftPermilleAvg;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_supervisor_module_info_t;

typedef struct ioctl_rsc_supervisor_result_info {
    ULONG ModeId;
    ULONG Flags;
    ULONG CyclesRequested;
    ULONG CyclesCompleted;
    LONG Status;
    ULONG FailureCount;
    ULONG CheckedCount;
    ULONG RepairedCount;
    ULONG ErrorCount;
    ULONG WarningCount;
    ULONG OverallScorePermille;
    LONG AuditStatus;
    LONG CoordStatus;
    LONG ThermalStatus;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG LastModuleId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_supervisor_result_info_t;

typedef struct ioctl_rsc_supervisor_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastCyclesRequested;
    ULONG LastCyclesCompleted;
    ULONG LastFailureCount;
    ULONG LastCheckedCount;
    ULONG LastRepairedCount;
    ULONG LastErrorCount;
    ULONG LastWarningCount;
    ULONG LastOverallScorePermille;
    LONG LastAuditStatus;
    LONG LastCoordStatus;
    LONG LastThermalStatus;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONGLONG LastTick100ns;
    ULONG ModuleCount;
    ioctl_rsc_supervisor_module_info_t Modules[IOCTL_RSC_SUPERVISOR_MODULE_INFO_CAPACITY];
} ioctl_rsc_supervisor_snapshot_response_t;

typedef struct ioctl_rsc_supervisor_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} ioctl_rsc_supervisor_run_request_t;

typedef struct ioctl_rsc_supervisor_run_response {
    LONG Status;
    ioctl_rsc_supervisor_result_info_t Result;
} ioctl_rsc_supervisor_run_response_t;

typedef struct ioctl_rsc_supervisor_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_supervisor_result_info_t Entries[1];
} ioctl_rsc_supervisor_history_response_t;

typedef struct ioctl_rsc_autotune_module_info {
    ULONG ModuleId;
    CHAR Name[16];
    ULONG TargetScorePermille;
    ULONG EffectiveTargetScorePermille;
    ULONG ObservedScorePermille;
    ULONG DeltaPermille;
    ULONG RiskScorePermille;
    ULONG IntegrityPenaltyPermille;
    ULONG DependencyRiskPermille;
    ULONG DomainGapCount;
    ULONG RouteGapCount;
    ULONG FanoutViolationCount;
    ULONG ThermalPressurePermille;
    ULONG CoverageScorePermille;
    ULONG TopologyPressurePermille;
    ULONG SemanticCoveragePermille;
    ULONG ProtocolDriftPermille;
    ULONG ActionMask;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_autotune_module_info_t;

typedef struct ioctl_rsc_autotune_result_info {
    ULONG ModeId;
    ULONG Flags;
    ULONG CyclesRequested;
    ULONG CyclesCompleted;
    LONG Status;
    ULONG CheckedCount;
    ULONG AdjustedCount;
    ULONG FailedAdjustCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG OverallScorePermille;
    LONG SupervisorStatus;
    LONG ThermalStatus;
    ULONG LastModuleId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_autotune_result_info_t;

typedef struct ioctl_rsc_autotune_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastCyclesRequested;
    ULONG LastCyclesCompleted;
    ULONG LastCheckedCount;
    ULONG LastAdjustedCount;
    ULONG LastFailedAdjustCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastOverallScorePermille;
    LONG LastSupervisorStatus;
    LONG LastThermalStatus;
    ULONGLONG LastTick100ns;
    ULONG ModuleCount;
    ioctl_rsc_autotune_module_info_t Modules[IOCTL_RSC_AUTOTUNE_MODULE_INFO_CAPACITY];
} ioctl_rsc_autotune_snapshot_response_t;

typedef struct ioctl_rsc_autotune_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} ioctl_rsc_autotune_run_request_t;

typedef struct ioctl_rsc_autotune_run_response {
    LONG Status;
    ioctl_rsc_autotune_result_info_t Result;
} ioctl_rsc_autotune_run_response_t;

typedef struct ioctl_rsc_autotune_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_autotune_result_info_t Entries[1];
} ioctl_rsc_autotune_history_response_t;

typedef struct ioctl_rsc_harden_module_info {
    ULONG ModuleId;
    CHAR Name[16];
    ULONG HealthScorePermille;
    ULONG RiskScorePermille;
    ULONG RecoveryScorePermille;
    ULONG IntegrityPenaltyPermille;
    ULONG DependencyRiskPermille;
    ULONG DomainGapCount;
    ULONG RouteGapCount;
    ULONG FanoutViolationCount;
    ULONG CoverageScorePermille;
    ULONG TopologyPressurePermille;
    ULONG SemanticCoveragePermille;
    ULONG ProtocolDriftPermille;
    ULONG ActionMask;
    LONG LastStatus;
    ULONGLONG LastTick100ns;
} ioctl_rsc_harden_module_info_t;

typedef struct ioctl_rsc_harden_result_info {
    ULONG ModeId;
    ULONG Flags;
    ULONG CyclesRequested;
    ULONG CyclesCompleted;
    LONG Status;
    ULONG CheckedCount;
    ULONG HardenedCount;
    ULONG FailedCount;
    ULONG WarningCount;
    ULONG ErrorCount;
    ULONG OverallScorePermille;
    LONG AutotuneStatus;
    LONG SupervisorStatus;
    LONG AuditStatus;
    LONG CoordStatus;
    LONG ThermalStatus;
    LONG InterconnectStatus;
    LONG DomainStatus;
    ULONG LastModuleId;
    ULONG LastTag;
    ULONG LastValue0;
    ULONG LastValue1;
    ULONG LastValue2;
    ULONG LastValue3;
    ULONGLONG Tick100ns;
} ioctl_rsc_harden_result_info_t;

typedef struct ioctl_rsc_harden_snapshot_response {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastCyclesRequested;
    ULONG LastCyclesCompleted;
    ULONG LastCheckedCount;
    ULONG LastHardenedCount;
    ULONG LastFailedCount;
    ULONG LastWarningCount;
    ULONG LastErrorCount;
    ULONG LastOverallScorePermille;
    LONG LastAutotuneStatus;
    LONG LastSupervisorStatus;
    LONG LastAuditStatus;
    LONG LastCoordStatus;
    LONG LastThermalStatus;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONGLONG LastTick100ns;
    ULONG ModuleCount;
    ioctl_rsc_harden_module_info_t Modules[IOCTL_RSC_HARDEN_MODULE_INFO_CAPACITY];
} ioctl_rsc_harden_snapshot_response_t;

typedef struct ioctl_rsc_harden_run_request {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} ioctl_rsc_harden_run_request_t;

typedef struct ioctl_rsc_harden_run_response {
    LONG Status;
    ioctl_rsc_harden_result_info_t Result;
} ioctl_rsc_harden_run_response_t;

typedef struct ioctl_rsc_harden_history_response {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    ioctl_rsc_harden_result_info_t Entries[1];
} ioctl_rsc_harden_history_response_t;

#define IOCTL_PEP_PPP_GET_RPMH_COMMAND_SET 0x80044FB4u
#define IOCTL_PEP_PPP_RESOURCE_VOTE 0x80048FA0u
#define IOCTL_PEP_PPP_GET_RESOURCE_SETTINGS 0x80048FA8u
#define IOCTL_PEP_PPP_RESOURCE_ACTION 0x80048FACu

#define IOCTL_PM_MPP_CONFIG_DIGITAL_INPUT 0x80030FA0u
#define IOCTL_PM_MPP_CONFIG_DIGITAL_OUTPUT 0x80030FA4u
#define IOCTL_PM_MPP_CONFIG_DIGITAL_INOUT 0x80030FA8u
#define IOCTL_PM_MPP_CONFIG_ANALOG_INPUT 0x80030FACu
#define IOCTL_PM_MPP_CONFIG_ANALOG_OUTPUT 0x80030FB0u
#define IOCTL_PM_MPP_CONFIG_I_SINK 0x80030FB4u
#define IOCTL_PM_MPP_CONFIG_ATEST 0x80030FB8u
#define IOCTL_PM_MPP_GET_INPUT_STATE 0x80030FBCu
#define IOCTL_PM_MPP_SET_OUTPUT_STATE 0x80030FC0u

#define IOCTL_PM_MPP_CONFIG_DTEST IOCTL_PM_MPP_CONFIG_ATEST

#define IOCTL_PM_LPG_PATTERN_CONFIG 0x800B0FA0u
#define IOCTL_PM_LPG_PWM_SIZE 0x800B0FA4u
#define IOCTL_PM_LPG_PWM_CLK 0x800B0FA8u
#define IOCTL_PM_LPG_LUT_CFG 0x800B0FACu
#define IOCTL_PM_LPG_PWM_ENABLE 0x800B0FB0u
#define IOCTL_PM_LPG_PWM_SET 0x800B0FB4u
#define IOCTL_PM_LPG_GET_STATUS 0x800B0FB8u
#define IOCTL_PM_LPG_SET_RAMP 0x800B0FBCu
#define IOCTL_PM_LPG_SET_PAUSE 0x800B0FC0u
#define IOCTL_PM_LPG_SET_LUT 0x800B0FC4u
#define IOCTL_PM_LPG_SET_DTEST 0x800B0FC8u
#define IOCTL_PM_LPG_ATEST 0x800B0FCCu

#define IOCTL_PM_LPG_PWM_FREQ IOCTL_PM_LPG_PWM_CLK
#define IOCTL_PM_LPG_PWM_TYPE_CONFIG IOCTL_PM_LPG_LUT_CFG
#define IOCTL_PM_LPG_PWM_VALUE IOCTL_PM_LPG_PWM_ENABLE
#define IOCTL_PM_LPG_ENABLE_CONTROL IOCTL_PM_LPG_PWM_SET
#define IOCTL_PM_LPG_RAMP_STEP_DURATION IOCTL_PM_LPG_GET_STATUS
#define IOCTL_PM_LPG_PAUSE_HI_MULTIPLIER IOCTL_PM_LPG_SET_RAMP
#define IOCTL_PM_LPG_PAUSE_LO_MULTIPLIER IOCTL_PM_LPG_SET_PAUSE
#define IOCTL_PM_LPG_INDEX IOCTL_PM_LPG_SET_LUT
#define IOCTL_PM_LPG_LUT_CONFIG IOCTL_PM_LPG_SET_DTEST
#define IOCTL_PM_LPG_LUT_RAMP_CONTROL IOCTL_PM_LPG_ATEST

#define IOCTL_PEP_THERMAL_PMIC_ALARM_QUERY 0x80350FA0u
#define IOCTL_PEP_THERMAL_PMIC_ALARM_STATE 0x80350FA4u

#define IOCTL_PEP_GPIO_CONFIG 0x80348FA0u
#define IOCTL_PEP_GPIO_REQUEST 0x80348FA4u
#define IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE 0x80028FA0u
#define IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT 0x80028FA4u
#define IOCTL_PEP_GPIO_PMIC_SET_STATE 0x80028FA8u
#define IOCTL_PEP_GPIO_PMIC_SET_PULL 0x80028FACu
#define IOCTL_PEP_GPIO_PMIC_SET_DRIVE 0x80028FB0u
#define IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT IOCTL_PEP_GPIO_PMIC_SET_STATE
#define IOCTL_PM_GPIO_SET_INVERSION_CONFIGURATION IOCTL_PEP_GPIO_PMIC_SET_PULL
#define IOCTL_PM_GPIO_SET_EXT_PIN_CONFIG IOCTL_PEP_GPIO_PMIC_SET_DRIVE
#define IOCTL_PM_GPIO_GET_GPIO_STATE 0x80024FB4u
#define IOCTL_PM_GPIO_SET_GPIO_OUTPUT 0x80028FC0u

#define IOCTL_PMIC_PUBLIC_USB_OTG_CONTROL 0x801C0FA8u
#define IOCTL_PMIC_PUBLIC_USB_OTG_ENABLE 0x801C0FACu
#define IOCTL_PM_3P_PUBLIC_USB_OTG_ENABLE 0x80270FA0u
#define IOCTL_PM_WLED_MODULE_ENABLE 0x80100FA8u
#define IOCTL_PM_IBBLAB_MODULE_ENABLE 0x80200FA4u

#define IOCTL_PEP_DMF_DEVICEINTERFACE_SEND 0xC3502FA4u

#define IOCTL_PEP_SUBSYS_ADSP_STATE 0x9C414000u
#define IOCTL_PEP_SUBSYS_ADSP_CTRL 0x9C414004u
#define IOCTL_PEP_SUBSYS_CDSP_STATE 0x9C420400u

#define IOCTL_PEP_PMIC_HWINFO 0x80110FA4u
#define IOCTL_PEP_PMIC_CLX_EVENTCOUNT 0x80110FACu
#define IOCTL_PEP_BMS_SET_THRESHOLDS 0x802D0FA0u
#define IOCTL_PEP_BMS_QUERY 0x802D0FA4u

#define IOCTL_PEP_DEEPSTANDBY_MODEM_SET 0x8FF82004u
