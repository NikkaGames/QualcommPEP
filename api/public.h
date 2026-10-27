#pragma once
#include <ntddk.h>

DEFINE_GUID(GUID_DEVINTERFACE_QCPEP8280, 0x2d9f7e41, 0x0f44, 0x4a5b, 0xa8, 0x72, 0x3a, 0x9e, 0x8f,
            0x2b, 0x55, 0x8c);

DEFINE_GUID(GUID_TSENS_QUERY, 0xc5a89f41, 0xe8a3, 0x4579, 0x9e, 0x09, 0xdb, 0x61, 0xb3, 0xa3, 0x9c,
            0x1e);

DEFINE_GUID(GUID_READ_STABLE_TSENS, 0xe1a92c2e, 0x4747, 0x4f69, 0xbe, 0xf7, 0x1b, 0xec, 0x83, 0x24,
            0x59, 0xa4);

DEFINE_GUID(GUID_DEVINTERFACE_BranchTrack, 0x1964f671, 0x9f87, 0x4d91, 0x93, 0x8e, 0x2b, 0x15, 0x00,
            0x2f, 0x24, 0x9b);

DEFINE_GUID(GUID_QCPEP_QCPPX_DEV_INTERFACE, 0xb921b0c4, 0xdc55, 0x48da, 0xa0, 0x23, 0x65, 0xa5,
            0x9e, 0xe2, 0xe6, 0x2a);

DEFINE_GUID(GUID_QCPEP_QCPPX_GPIO_FUNCTION_INTERFACE, 0xa1a1a1a8, 0x899d, 0x4b9b, 0xb0, 0x89, 0xba,
            0x84, 0xd1, 0xda, 0x5c, 0xaf);

DEFINE_GUID(GUID_PEP_BCM_INTERFACE, 0x99f8bbdb, 0x5b65, 0x444b, 0xad, 0xbe, 0x5b, 0x87, 0xef, 0xed,
            0xd7, 0x6c);

DEFINE_GUID(GUID_DEVINTERFACE_QCPEP_SOC_PEP, 0x55beca03, 0xba2c, 0x4fbc, 0x9e, 0xfc, 0xee, 0x8e,
            0xca, 0x56, 0xf0, 0xc7);

DEFINE_GUID(GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE, 0xa0e1dd36, 0xdfb9, 0x4c0e, 0xbb, 0xaa,
            0x78, 0xf0, 0x74, 0x6b, 0xe1, 0x81);

DEFINE_GUID(GUID_PEP_RUNTIME_RESOURCE_ENUMERATION_INTERFACE_V2, 0x0919728e, 0x6bd7, 0x4a9c, 0x8f,
            0x3b, 0xc8, 0x35, 0x87, 0x3d, 0x4d, 0xae);

DEFINE_GUID(GUID_PEP_PSTATE_TRANSITION_INTERFACE, 0x99f7397b, 0x5b65, 0x444b, 0xad, 0xbe, 0x5b,
            0x87, 0xef, 0xed, 0xe7, 0x6c);

DEFINE_GUID(GUID_DEVINTERFACE_PEP_USB_WAKEUPINTERRUTS_DDI, 0x902e0f43, 0xd28a, 0x49c4, 0xa8, 0xe0,
            0x23, 0xef, 0xce, 0xdc, 0xf7, 0xc7);

DEFINE_GUID(GUID_PEP_ENABLE_GFX_ACD_INTERFACE, 0xff4f9890, 0x6da8, 0x4beb, 0xa5, 0x15, 0xfc, 0xa7,
            0x45, 0x28, 0x6f, 0x3d);

#define QCPEP_DEVICE_NAME L"\\Device\\QCOM_PEPDriver"
#define QCPEP_DOS_SYMLINK_DEEPSTANDBY L"\\DosDevices\\QcDeepStandbyMdm"
#define QCPEP_DOS_SYMLINK_GLOBAL_THM L"\\DosDevices\\Global\\PTHERMAL"
#define QCPEP_DOS_SYMLINK L"\\DosDevices\\QCOM_PEPDriver"
#define QCPEP_DOS_SYMLINK_THM L"\\DosDevices\\PTHERMAL"
#define QCPEP_DOS_SYMLINK_GLOBAL_DEEPSTANDBY L"\\DosDevices\\Global\\QcDeepStandbyMdm"

#define IOCTL_QCPEP_PING CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_PORTID                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_HID CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_CONFIG                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_STATS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RES_SNAPSHOT                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_ARM_WAKE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DISARM_WAKE                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_DIAG CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_GET_PEPLOG                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_CLEAR_PEPLOG                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _QCPEP_VETO_OPERATION {
    ULONG LprId;
    ULONG ModeId;
    ULONG VetoId;
} QCPEP_VETO_OPERATION;

#define IOCTL_QCPEP_REGISTER_VETO_REASON                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_APPLY_VETO                                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_REMOVE_VETO                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_QUERY_VETO_REFCOUNT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_SOC_SUBSYS_COUNT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_SOC_SUBSYS_QUERY                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_SOC_SUBSYS_RESET                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_SOC_SUBSYS_BLOCKTIME                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_SOC_SUBSYS_METADATA                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_INTERRUPT_SNAPSHOT                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_INTERRUPT_HISTORY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_INTERRUPT_WAKECOUNTERS                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_INTERRUPT_LOOKUP                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x817, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_MMRM_SNAPSHOT                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_MMRM_HISTORY                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x819, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_MMRM_DOMAIN_QUERY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_TSENS_SNAPSHOT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_TSENS_HISTORY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_TSENS_SENSOR_RANGE                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_TSENS_SET_THRESHOLD                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_THERMALREQ_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x81F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_THERMALREQ_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DEEPSTANDBY_SNAPSHOT                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DEEPSTANDBY_HISTORY                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DEEPSTANDBY_MODEM_GET                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_CRASHDUMP_SNAPSHOT                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_CRASHDUMP_HISTORY                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_CRASHDUMP_SOURCE_QUERY                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x826, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_CRASHDUMP_SET_ACTIVE                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x827, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_BCL_SNAPSHOT                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x828, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_BCL_HISTORY                                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x829, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_BCL_THRESHOLDS                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_BCL_SUBMIT_TEST                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_BCL_READSET_THRESHOLD                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DCVS_SNAPSHOT                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DCVS_HISTORY                                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DCVS_PERF_TABLE                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x82F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_DCVS_SET_POLICY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_PARKSEL_SNAPSHOT                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_PARKSEL_HISTORY                                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x832, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_PARKSEL_GET_POLICY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x833, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_PARKSEL_SET_POLICY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x834, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_EXSOC_SNAPSHOT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x835, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_EXSOC_HISTORY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x836, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_SNAPSHOT                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x837, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_HISTORY                                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x838, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_SET_NODE                                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x839, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_QUERY_NODE                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_APPLY_BATCH                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_GET_CATALOG                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_RUN_SCENARIO                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_SCENARIO_HISTORY                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DIAG_SNAPSHOT                                                            \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x83F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DIAG_RUN_MATRIX                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DIAG_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PROFILE_CATALOG                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x842, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PROFILE_RUN                                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x843, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PROFILE_HISTORY                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x844, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_GRAPH_SNAPSHOT                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x845, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PLAN_EXECUTE                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x846, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PLAN_HISTORY                                                             \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x847, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CHECKPOINT_SNAPSHOT                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x848, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CHECKPOINT_CAPTURE                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x849, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CHECKPOINT_APPLY                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CHECKPOINT_HISTORY                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PLAN_PREVIEW_SNAPSHOT                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PLAN_PREVIEW_EXECUTE                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_PLAN_PREVIEW_HISTORY                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_COVERAGE_SNAPSHOT                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x84F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_COVERAGE_ANALYZE                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x850, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_COVERAGE_HISTORY                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x851, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_REMEDIATION_SNAPSHOT                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x852, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_REMEDIATION_RUN                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x853, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_REMEDIATION_HISTORY                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x854, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_STABILIZATION_SNAPSHOT                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x855, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_STABILIZATION_RUN                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x856, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_STABILIZATION_HISTORY                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x857, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CONFORMANCE_SNAPSHOT                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x858, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CONFORMANCE_RUN                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x859, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_CONFORMANCE_HISTORY                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_ORCHESTRATION_SNAPSHOT                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_ORCHESTRATION_RUN                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_ORCHESTRATION_HISTORY                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_POLICY_SNAPSHOT                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_POLICY_HISTORY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x85F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_FABRIC_SNAPSHOT                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x860, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_FABRIC_RUN                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x861, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_FABRIC_HISTORY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x862, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DOMAIN_SNAPSHOT                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x863, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DOMAIN_RUN                                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x864, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DOMAIN_HISTORY                                                           \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x865, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_INTERCONNECT_SNAPSHOT                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x866, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_INTERCONNECT_RUN                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x867, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_INTERCONNECT_HISTORY                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x868, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_TOPOLOGY_SNAPSHOT                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x869, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_CATALOG                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SYNC_SNAPSHOT                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SYNC_RUN                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SYNC_HISTORY                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_VALIDATE_SNAPSHOT                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_VALIDATE_RUN                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x86F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_VALIDATE_HISTORY                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x870, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SEQUENCE_SNAPSHOT                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x871, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SEQUENCE_RUN                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x872, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SEQUENCE_HISTORY                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x873, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_LOOKUP_SNAPSHOT                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x874, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_LOOKUP_RUN                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x875, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_LOOKUP_HISTORY                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x876, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_PROVISION_SNAPSHOT                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x877, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_PROVISION_RUN                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x878, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_PROVISION_HISTORY                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x879, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_RAIL_SNAPSHOT                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_RAIL_RUN                                                         \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_RAIL_HISTORY                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_TCS_SNAPSHOT                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_TCS_RUN                                                          \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_TCS_HISTORY                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x87F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_THERMAL_SNAPSHOT                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_THERMAL_RUN                                                      \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x881, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_THERMAL_HISTORY                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x882, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_COORD_SNAPSHOT                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x883, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_COORD_RUN                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x884, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_COORD_HISTORY                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x885, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUDIT_SNAPSHOT                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x886, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUDIT_RUN                                                        \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x887, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUDIT_HISTORY                                                    \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x888, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SUPERVISOR_SNAPSHOT                                              \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x889, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SUPERVISOR_RUN                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_SUPERVISOR_HISTORY                                               \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUTOTUNE_SNAPSHOT                                                \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUTOTUNE_RUN                                                     \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_AUTOTUNE_HISTORY                                                 \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_HARDEN_SNAPSHOT                                                  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x88F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_HARDEN_RUN                                                       \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x890, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_QCPEP_RSCSW_DRIVERA_HARDEN_HISTORY                                                   \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x891, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_QCPEP_VENDOR_88882000 0x88882000u
#define IOCTL_QCPEP_VENDOR_8888204C 0x8888204Cu
#define IOCTL_QCPEP_VENDOR_88882058 0x88882058u
#define IOCTL_QCPEP_VENDOR_8888205C 0x8888205Cu
#define IOCTL_QCPEP_VENDOR_88882080 0x88882080u
#define IOCTL_QCPEP_VENDOR_88882084 0x88882084u
#define IOCTL_QCPEP_VENDOR_88882088 0x88882088u
#define IOCTL_QCPEP_VENDOR_8888208C 0x8888208Cu
#define IOCTL_QCPEP_VENDOR_88882094 0x88882094u
#define IOCTL_QCPEP_VENDOR_88882098 0x88882098u
#define IOCTL_QCPEP_VENDOR_8888209C 0x8888209Cu
#define IOCTL_QCPEP_VENDOR_888820A4 0x888820A4u
#define IOCTL_QCPEP_VENDOR_88884084 0x88884084u
#define IOCTL_QCPEP_VENDOR_9C414000 0x9C414000u
#define IOCTL_QCPEP_VENDOR_C3502FF8 0xC3502FF8u

#define QCPEP_CLOCKDBG_TOTAL_BYTES 83984u

#define IOCTL_QCPEP_TSENS_GET_TEMPS 0x002A4004u
#define IOCTL_QCPEP_TSENS_GET_TEMP_FOR_IDX 0x002A4008u
#define IOCTL_QCPEP_TSENS_STABLE_REQ 0x002A4014u
#define IOCTL_QCPEP_THERMAL_READ_REQUEST 0x00294090u

#define IOCTL_QCPEP_MMRM_POLICY_NOTSUP 0x00294090u
#define IOCTL_QCPEP_MMRM_POLICY_READ 0x00294094u
#define IOCTL_QCPEP_BRANCHTRACK_QUERY 0x0022502Cu

#define IOCTL_QCPEP_VENDOR_9C414004 0x9C414004u
#define IOCTL_QCPEP_VENDOR_9C420400 0x9C420400u

typedef struct _QCPEP_CONFIG {
    ULONG BranchTrackEnabled;
    ULONG EnableDirectedPStateFeatures;
    ULONG EnableParkSelection;
    ULONG EnablePEPOCDTest;
    ULONG EnablePEPOCDTestLowPerf;
    ULONG PepBcl_Notification_Level;
} QCPEP_CONFIG;

typedef struct _QCPEP_PORTID_REPLY {
    ULONG PortId;
} QCPEP_PORTID_REPLY;

typedef struct _QCPEP_CONFIG_REPLY {
    QCPEP_CONFIG Config;
} QCPEP_CONFIG_REPLY;

typedef struct _QCPEP_STATS_REPLY {
    ULONG IoctlCount;
    ULONG PoFxPowerRequired;
    ULONG PoFxPowerNotRequired;
    ULONG PoFxComponentActive;
    ULONG PoFxComponentIdle;
    ULONG PoFxIdleState;
    ULONG D0Entry;
    ULONG D0Exit;
} QCPEP_STATS_REPLY;

typedef struct _QCPEP_SOC_SUBSYS_QUERY_IN {
    ULONG Index;
} QCPEP_SOC_SUBSYS_QUERY_IN;

typedef struct _QCPEP_SOC_SUBSYS_QUERY_OUT {
    ULONG Index;
    ULONG Enabled;
    ULONG MetadataCount;
    ULONG WakeupCount;
    ULONGLONG BlockedDuration100ns;
    ULONGLONG SleepDuration100ns;
    WCHAR ParentName[32];
    WCHAR Name[96];
} QCPEP_SOC_SUBSYS_QUERY_OUT;

typedef struct _QCPEP_SOC_SUBSYS_BLOCK_IN {
    ULONG Index;
} QCPEP_SOC_SUBSYS_BLOCK_IN;

typedef struct _QCPEP_SOC_SUBSYS_BLOCK_OUT {
    ULONGLONG BlockingTime100ns;
} QCPEP_SOC_SUBSYS_BLOCK_OUT;

typedef struct _QCPEP_SOC_SUBSYS_META_PAIR {
    WCHAR Key[48];
    WCHAR Value[64];
} QCPEP_SOC_SUBSYS_META_PAIR;

typedef struct _QCPEP_SOC_SUBSYS_META_IN {
    ULONG Index;
} QCPEP_SOC_SUBSYS_META_IN;

typedef struct _QCPEP_SOC_SUBSYS_META_OUT {
    ULONG Index;
    ULONG PairCount;
    QCPEP_SOC_SUBSYS_META_PAIR Pairs[4];
} QCPEP_SOC_SUBSYS_META_OUT;

#define QCPEP_INTERRUPT_PORT_CAPACITY 16u
#define QCPEP_INTERRUPT_WAKE_TOP_CAPACITY 16u

typedef struct _QCPEP_INTERRUPT_PORT_INFO {
    ULONG PortId;
    ULONG IsrCount;
    ULONG DpcCount;
    ULONG EnableCount;
    ULONG DisableCount;
    ULONG DpcQueuedCount;
    ULONG LastMessageId;
    ULONGLONG LastIsrTick100ns;
    ULONGLONG LastDpcTick100ns;
} QCPEP_INTERRUPT_PORT_INFO;

typedef struct _QCPEP_INTERRUPT_WAKE_COUNTER_INFO {
    ULONG Offset;
    ULONG InterruptId;
    ULONG HitCount;
} QCPEP_INTERRUPT_WAKE_COUNTER_INFO;

typedef struct _QCPEP_INTERRUPT_EVENT_INFO {
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
} QCPEP_INTERRUPT_EVENT_INFO;

typedef struct _QCPEP_INTERRUPT_SNAPSHOT_OUT {
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
    QCPEP_INTERRUPT_PORT_INFO Ports[QCPEP_INTERRUPT_PORT_CAPACITY];
    ULONG WakeTopCount;
    QCPEP_INTERRUPT_WAKE_COUNTER_INFO WakeTop[QCPEP_INTERRUPT_WAKE_TOP_CAPACITY];
} QCPEP_INTERRUPT_SNAPSHOT_OUT;

typedef struct _QCPEP_INTERRUPT_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_INTERRUPT_EVENT_INFO Entries[1];
} QCPEP_INTERRUPT_HISTORY_OUT;

typedef struct _QCPEP_INTERRUPT_WAKECOUNTERS_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    QCPEP_INTERRUPT_WAKE_COUNTER_INFO Entries[1];
} QCPEP_INTERRUPT_WAKECOUNTERS_OUT;

typedef struct _QCPEP_INTERRUPT_LOOKUP_IN {
    ULONG InterruptId;
} QCPEP_INTERRUPT_LOOKUP_IN;

typedef struct _QCPEP_INTERRUPT_LOOKUP_OUT {
    ULONG InterruptId;
    LONG Offset;
    ULONG TriggerType;
    ULONG DriverId;
    ULONG Flags;
    LONG Status;
} QCPEP_INTERRUPT_LOOKUP_OUT;

#define QCPEP_MMRM_DOMAIN_CAPACITY 12u

typedef struct _QCPEP_MMRM_DOMAIN_INFO {
    ULONG FrequencyKhz;
    ULONG PowerMilliWatts;
    ULONG Flags;
    ULONG DynPermille;
    ULONG LkgPermille;
    LONG TempBiasDeciC;
} QCPEP_MMRM_DOMAIN_INFO;

typedef struct _QCPEP_MMRM_EVENT_INFO {
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
} QCPEP_MMRM_EVENT_INFO;

typedef struct _QCPEP_MMRM_SNAPSHOT_OUT {
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
    QCPEP_MMRM_DOMAIN_INFO Domains[QCPEP_MMRM_DOMAIN_CAPACITY];
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
} QCPEP_MMRM_SNAPSHOT_OUT;

typedef struct _QCPEP_MMRM_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_MMRM_EVENT_INFO Entries[1];
} QCPEP_MMRM_HISTORY_OUT;

typedef struct _QCPEP_MMRM_DOMAIN_QUERY_IN {
    ULONG Index;
} QCPEP_MMRM_DOMAIN_QUERY_IN;

typedef struct _QCPEP_MMRM_DOMAIN_QUERY_OUT {
    ULONG Index;
    LONG Status;
    ULONG DomainCount;
    QCPEP_MMRM_DOMAIN_INFO Domain;
} QCPEP_MMRM_DOMAIN_QUERY_OUT;

#define QCPEP_TSENS_SENSOR_CAPACITY 64u

typedef struct _QCPEP_TSENS_SENSOR_INFO {
    LONG LastTempDeciC;
    LONG MinTempDeciC;
    LONG MaxTempDeciC;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG LowHitCount;
    ULONG HighHitCount;
    ULONG Flags;
} QCPEP_TSENS_SENSOR_INFO;

typedef struct _QCPEP_TSENS_EVENT_INFO {
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
} QCPEP_TSENS_EVENT_INFO;

typedef struct _QCPEP_TSENS_SNAPSHOT_OUT {
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
    QCPEP_TSENS_SENSOR_INFO Sensors[QCPEP_TSENS_SENSOR_CAPACITY];
} QCPEP_TSENS_SNAPSHOT_OUT;

typedef struct _QCPEP_TSENS_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_TSENS_EVENT_INFO Entries[1];
} QCPEP_TSENS_HISTORY_OUT;

typedef struct _QCPEP_TSENS_SENSOR_RANGE_IN {
    ULONG SensorId;
} QCPEP_TSENS_SENSOR_RANGE_IN;

typedef struct _QCPEP_TSENS_SENSOR_RANGE_OUT {
    ULONG SensorId;
    LONG MinTempDeciC;
    LONG MaxTempDeciC;
    LONG Status;
} QCPEP_TSENS_SENSOR_RANGE_OUT;

typedef struct _QCPEP_TSENS_SET_THRESHOLD_IN {
    ULONG SensorId;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG Flags;
} QCPEP_TSENS_SET_THRESHOLD_IN;

typedef struct _QCPEP_TSENS_SET_THRESHOLD_OUT {
    ULONG SensorId;
    LONG Status;
    LONG LowThresholdDeciC;
    LONG HighThresholdDeciC;
    ULONG Flags;
} QCPEP_TSENS_SET_THRESHOLD_OUT;

typedef struct _QCPEP_THERMALREQ_EVENT_INFO {
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
} QCPEP_THERMALREQ_EVENT_INFO;

typedef struct _QCPEP_THERMALREQ_SNAPSHOT_OUT {
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
} QCPEP_THERMALREQ_SNAPSHOT_OUT;

typedef struct _QCPEP_THERMALREQ_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_THERMALREQ_EVENT_INFO Entries[1];
} QCPEP_THERMALREQ_HISTORY_OUT;

typedef struct _QCPEP_DEEPSTANDBY_EVENT_INFO {
    ULONGLONG Tick100ns;
    ULONG EventCode;
    ULONG Status;
    ULONG PrevMsOnDcPwr;
    ULONG CurrMsOnDcPwr;
    ULONG PrevDeepStandbyState;
    ULONG CurrDeepStandbyState;
    ULONG Aux0;
    ULONG Aux1;
} QCPEP_DEEPSTANDBY_EVENT_INFO;

typedef struct _QCPEP_DEEPSTANDBY_SNAPSHOT_OUT {
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
} QCPEP_DEEPSTANDBY_SNAPSHOT_OUT;

typedef struct _QCPEP_DEEPSTANDBY_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_DEEPSTANDBY_EVENT_INFO Entries[1];
} QCPEP_DEEPSTANDBY_HISTORY_OUT;

typedef struct _QCPEP_DEEPSTANDBY_MODEM_GET_OUT {
    ULONG ModemReq;
} QCPEP_DEEPSTANDBY_MODEM_GET_OUT;

#define QCPEP_CRASHDUMP_SOURCE_CAPACITY 8u

typedef struct _QCPEP_CRASHDUMP_SOURCE_INFO {
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
} QCPEP_CRASHDUMP_SOURCE_INFO;

typedef struct _QCPEP_CRASHDUMP_EVENT_INFO {
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
} QCPEP_CRASHDUMP_EVENT_INFO;

typedef struct _QCPEP_CRASHDUMP_SNAPSHOT_OUT {
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
    QCPEP_CRASHDUMP_SOURCE_INFO Sources[QCPEP_CRASHDUMP_SOURCE_CAPACITY];
} QCPEP_CRASHDUMP_SNAPSHOT_OUT;

typedef struct _QCPEP_CRASHDUMP_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_CRASHDUMP_EVENT_INFO Entries[1];
} QCPEP_CRASHDUMP_HISTORY_OUT;

typedef struct _QCPEP_CRASHDUMP_SOURCE_QUERY_IN {
    ULONG Index;
} QCPEP_CRASHDUMP_SOURCE_QUERY_IN;

typedef struct _QCPEP_CRASHDUMP_SOURCE_QUERY_OUT {
    ULONG Index;
    LONG Status;
    ULONG ActiveSource;
    QCPEP_CRASHDUMP_SOURCE_INFO Source;
} QCPEP_CRASHDUMP_SOURCE_QUERY_OUT;

typedef struct _QCPEP_CRASHDUMP_SET_ACTIVE_IN {
    ULONG Index;
} QCPEP_CRASHDUMP_SET_ACTIVE_IN;

typedef struct _QCPEP_CRASHDUMP_SET_ACTIVE_OUT {
    ULONG Index;
    LONG Status;
    ULONG ActiveSource;
} QCPEP_CRASHDUMP_SET_ACTIVE_OUT;

#define QCPEP_BCL_THRESHOLD_CAPACITY 64u

typedef struct _QCPEP_BCL_THRESHOLD_INFO {
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
} QCPEP_BCL_THRESHOLD_INFO;

typedef struct _QCPEP_BCL_EVENT_INFO {
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
} QCPEP_BCL_EVENT_INFO;

typedef struct _QCPEP_BCL_SNAPSHOT_OUT {
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
    QCPEP_BCL_THRESHOLD_INFO Thresholds[QCPEP_BCL_THRESHOLD_CAPACITY];
} QCPEP_BCL_SNAPSHOT_OUT;

typedef struct _QCPEP_BCL_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_BCL_EVENT_INFO Entries[1];
} QCPEP_BCL_HISTORY_OUT;

typedef struct _QCPEP_BCL_THRESHOLDS_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    QCPEP_BCL_THRESHOLD_INFO Entries[1];
} QCPEP_BCL_THRESHOLDS_OUT;

typedef struct _QCPEP_BCL_SUBMIT_TEST_IN {
    ULONG NotificationType;
} QCPEP_BCL_SUBMIT_TEST_IN;

typedef struct _QCPEP_BCL_SUBMIT_TEST_OUT {
    ULONG NotificationType;
    LONG Status;
    ULONG CurrentNotificationType;
    ULONG ParkingState;
    ULONG DesiredParkedCpus;
    ULONG CurrentParkedCpus;
    ULONG ThrottlePermille;
} QCPEP_BCL_SUBMIT_TEST_OUT;

typedef struct _QCPEP_BCL_READSET_THRESHOLD_IN {
    ULONG ResourceId;
    ULONG Field;
    ULONG Threshold;
    ULONG Value0;
    ULONG Flags;
} QCPEP_BCL_READSET_THRESHOLD_IN;

typedef struct _QCPEP_BCL_READSET_THRESHOLD_OUT {
    ULONG ResourceId;
    ULONG Field;
    ULONG Threshold;
    ULONG Value0;
    ULONG Flags;
    LONG Status;
} QCPEP_BCL_READSET_THRESHOLD_OUT;

#define QCPEP_DCVS_CPU_INFO_CAPACITY 64u
#define QCPEP_DCVS_PERF_TABLE_CAPACITY 64u

typedef struct _QCPEP_DCVS_CPU_INFO {
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
} QCPEP_DCVS_CPU_INFO;

typedef struct _QCPEP_DCVS_EVENT_INFO {
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
} QCPEP_DCVS_EVENT_INFO;

typedef struct _QCPEP_DCVS_SNAPSHOT_OUT {
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
    ULONG PerfTableHz[QCPEP_DCVS_PERF_TABLE_CAPACITY];
    ULONG CpuEntryCount;
    QCPEP_DCVS_CPU_INFO Cpus[QCPEP_DCVS_CPU_INFO_CAPACITY];
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
} QCPEP_DCVS_SNAPSHOT_OUT;

typedef struct _QCPEP_DCVS_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_DCVS_EVENT_INFO Entries[1];
} QCPEP_DCVS_HISTORY_OUT;

typedef struct _QCPEP_DCVS_PERF_TABLE_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ULONG Entries[1];
} QCPEP_DCVS_PERF_TABLE_OUT;

typedef struct _QCPEP_DCVS_SET_POLICY_IN {
    ULONG Flags;
    ULONG EnablePerfSelection;
    ULONG EnableThermalPerfSelection;
    ULONG ThermalHotDeciC;
    ULONG ThermalCriticalDeciC;
    ULONG ThermalCapHotPercent;
    ULONG ThermalCapCriticalPercent;
} QCPEP_DCVS_SET_POLICY_IN;

typedef struct _QCPEP_DCVS_SET_POLICY_OUT {
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
} QCPEP_DCVS_SET_POLICY_OUT;

#define QCPEP_PARKSEL_CPU_INFO_CAPACITY 8u

typedef struct _QCPEP_PARKSEL_CPU_INFO {
    ULONG Cpu;
    UCHAR CurrentParkState;
    UCHAR PreviousParkState;
    UCHAR CurrentPreference;
    UCHAR CurrentRank;
    ULONG TransitionCount;
    LONG TemperatureDeciC;
} QCPEP_PARKSEL_CPU_INFO;

typedef struct _QCPEP_PARKSEL_EVENT_INFO {
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
} QCPEP_PARKSEL_EVENT_INFO;

typedef struct _QCPEP_PARKSEL_SNAPSHOT_OUT {
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
    UCHAR RankPreference[QCPEP_PARKSEL_CPU_INFO_CAPACITY];
    UCHAR PreviousRankPreference[QCPEP_PARKSEL_CPU_INFO_CAPACITY];
    LONG RankCpuTempDeciC[QCPEP_PARKSEL_CPU_INFO_CAPACITY];
    ULONG CpuEntryCount;
    QCPEP_PARKSEL_CPU_INFO Cpus[QCPEP_PARKSEL_CPU_INFO_CAPACITY];
} QCPEP_PARKSEL_SNAPSHOT_OUT;

typedef struct _QCPEP_PARKSEL_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_PARKSEL_EVENT_INFO Entries[1];
} QCPEP_PARKSEL_HISTORY_OUT;

typedef struct _QCPEP_PARKSEL_POLICY_IN {
    ULONG Version;
    ULONG Flags;
    ULONG ThermalRankEnabled;
    LONG ThermalRankHysteresisDeciC;
    ULONG ThermalRankUpdateIntervalMs;
    ULONG ThermalSensorBaseIndex;
    ULONG TimerThreshold0Sec;
    ULONG TimerThreshold1Sec;
} QCPEP_PARKSEL_POLICY_IN;

typedef struct _QCPEP_PARKSEL_POLICY_OUT {
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
} QCPEP_PARKSEL_POLICY_OUT;

#define QCPEP_EXSOC_DEVICE_INFO_CAPACITY 32u
#define QCPEP_EXSOC_HISTORY_INFO_CAPACITY 64u

typedef struct _QCPEP_EXSOC_DEVICE_INFO {
    ULONG DeviceHash;
    CHAR DeviceId[21];
    ULONG DeviceIdLength;
    ULONG LastDState;
    ULONG LastIoctlCode;
    ULONG ApplyCount;
    ULONG SuccessCount;
    ULONG FailureCount;
    ULONGLONG LastTick100ns;
} QCPEP_EXSOC_DEVICE_INFO;

typedef struct _QCPEP_EXSOC_EVENT_INFO {
    ULONGLONG Tick100ns;
    ULONGLONG Sequence;
    ULONG IoctlCode;
    LONG Status;
    ULONG DeviceHash;
    ULONG DState;
    ULONG RequestedOutputBytes;
    ULONG ReturnedInformation;
} QCPEP_EXSOC_EVENT_INFO;

typedef struct _QCPEP_EXSOC_SNAPSHOT_OUT {
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
    QCPEP_EXSOC_DEVICE_INFO Devices[QCPEP_EXSOC_DEVICE_INFO_CAPACITY];
} QCPEP_EXSOC_SNAPSHOT_OUT;

typedef struct _QCPEP_EXSOC_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_EXSOC_EVENT_INFO Entries[1];
} QCPEP_EXSOC_HISTORY_OUT;

#define QCPEP_RSCSW_SUBSYS_AOP 0u
#define QCPEP_RSCSW_SUBSYS_ARC 1u
#define QCPEP_RSCSW_SUBSYS_CLOCK 2u
#define QCPEP_RSCSW_SUBSYS_CMDDB 3u
#define QCPEP_RSCSW_SUBSYS_DAL 4u
#define QCPEP_RSCSW_SUBSYS_GLINK 5u
#define QCPEP_RSCSW_SUBSYS_ICB 6u
#define QCPEP_RSCSW_SUBSYS_NPA 7u
#define QCPEP_RSCSW_SUBSYS_PCU 8u
#define QCPEP_RSCSW_SUBSYS_PDC 9u
#define QCPEP_RSCSW_SUBSYS_PPP 10u
#define QCPEP_RSCSW_SUBSYS_RPMH 11u
#define QCPEP_RSCSW_SUBSYS_RSC 12u
#define QCPEP_RSCSW_SUBSYS_TLMM 13u
#define QCPEP_RSCSW_SUBSYS_TSENS 14u
#define QCPEP_RSCSW_SUBSYS_ARCMGR 15u
#define QCPEP_RSCSW_SUBSYS_COUNT 16u

#define QCPEP_RSCSW_NODE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_CATALOG_INFO_CAPACITY 32u
#define QCPEP_RSCSW_SCENARIO_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DIAG_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DIAG_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_PROFILE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_PROFILE_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_GRAPH_LINK_INFO_CAPACITY 64u
#define QCPEP_RSCSW_PLAN_STEP_INFO_CAPACITY 64u
#define QCPEP_RSCSW_PLAN_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_PLAN_PREVIEW_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_CHECKPOINT_INFO_CAPACITY 16u
#define QCPEP_RSCSW_CHECKPOINT_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_COVERAGE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_COVERAGE_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_REMEDIATION_INFO_CAPACITY 32u
#define QCPEP_RSCSW_REMEDIATION_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_STABILIZATION_LANE_INFO_CAPACITY 4u
#define QCPEP_RSCSW_STABILIZATION_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_CONFORMANCE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_CONFORMANCE_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_ORCHESTRATION_STAGE_INFO_CAPACITY 24u
#define QCPEP_RSCSW_ORCHESTRATION_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_POLICY_RESOURCE_INFO_CAPACITY 96u
#define QCPEP_RSCSW_POLICY_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_FABRIC_LANE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_FABRIC_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_DOMAIN_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DOMAIN_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_INFO_CAPACITY 48u
#define QCPEP_RSCSW_INTERCONNECT_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_TOPOLOGY_NODE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_TOPOLOGY_ROUTE_INFO_CAPACITY 48u
#define QCPEP_RSCSW_DRIVERA_SUBSYSTEM_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_AOP_SUBSYSTEM_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_ARC_RESOURCE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_CMDDB_SLV_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_PDC_DRV_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_PDC_TRIGGER_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_RPMH_SET_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_NPA_CLIENT_INFO_CAPACITY 24u
#define QCPEP_RSCSW_DRIVERA_GLINK_EVENT_INFO_CAPACITY 48u
#define QCPEP_RSCSW_DRIVERA_TLMM_DRIVE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_TSENS_THRESHOLD_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_CLOCK_DRIVER_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_CMDDB_QUERY_INFO_CAPACITY 8u
#define QCPEP_RSCSW_DRIVERA_SYNC_LANE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_SYNC_HISTORY_INFO_CAPACITY 96u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO_CAPACITY 64u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_PROVISION_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_PROVISION_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_RAIL_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_RAIL_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_TCS_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_TCS_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_THERMAL_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_THERMAL_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_COORD_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_COORD_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_AUDIT_ROW_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_AUDIT_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODULE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_HISTORY_INFO_CAPACITY 128u
#define QCPEP_RSCSW_DRIVERA_HARDEN_MODULE_INFO_CAPACITY 16u
#define QCPEP_RSCSW_DRIVERA_HARDEN_HISTORY_INFO_CAPACITY 128u

#define QCPEP_RSCSW_DRIVERA_SYNC_PROFILE_BOOT 1u
#define QCPEP_RSCSW_DRIVERA_SYNC_PROFILE_BALANCED 2u
#define QCPEP_RSCSW_DRIVERA_SYNC_PROFILE_PERFORMANCE 3u
#define QCPEP_RSCSW_DRIVERA_SYNC_PROFILE_INTERCONNECT 4u
#define QCPEP_RSCSW_DRIVERA_SYNC_PROFILE_THERMAL 5u

#define QCPEP_RSCSW_DRIVERA_SYNC_FLAG_FORCE_ENABLE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_SYNC_FLAG_RUN_INTERCONNECT 0x00000002u
#define QCPEP_RSCSW_DRIVERA_SYNC_FLAG_RUN_DOMAIN 0x00000004u
#define QCPEP_RSCSW_DRIVERA_SYNC_FLAG_THERMAL_GUARD 0x00000008u
#define QCPEP_RSCSW_DRIVERA_SYNC_FLAG_VERIFY_QUERY 0x00000010u

#define QCPEP_RSCSW_DRIVERA_VALIDATE_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_MODE_STRICT 2u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_MODE_THERMAL 3u

#define QCPEP_RSCSW_DRIVERA_VALIDATE_FLAG_REPAIR_STATE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_FLAG_REPAIR_CLOCK 0x00000002u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_FLAG_REPAIR_POLICY 0x00000004u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_FLAG_VERIFY_ICB 0x00000008u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_FLAG_VERIFY_ENUM_NAMES 0x00000010u

#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_ARC 0x00000001u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_CMDDB 0x00000002u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_PDC 0x00000004u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_RPMH 0x00000008u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_NPA 0x00000010u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_GLINK 0x00000020u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_PPP 0x00000040u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_TLMM 0x00000080u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_TSENS 0x00000100u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_CLOCK 0x00000200u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_AOP 0x00000400u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_ICB 0x00000800u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_THERMAL 0x00001000u

#define QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_ICB_NAME 0x00000001u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_RPMH_PROFILE 0x00000002u
#define QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_TSENS_PROFILE 0x00000004u

#define QCPEP_RSCSW_DRIVERA_SEQUENCE_MODE_DEFAULT 1u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_MODE_THERMAL 3u

#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_INCLUDE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_INCLUDE_SLEEP 0x00000002u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_INCLUDE_WAKE 0x00000004u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_FORCE_POLICY 0x00000008u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_RUN_INTERCONNECT 0x00000010u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_RUN_DOMAIN 0x00000020u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_FLAG_VERIFY_QUERY 0x00000040u

#define QCPEP_RSCSW_DRIVERA_SEQUENCE_PHASE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_PHASE_SLEEP 0x00000002u
#define QCPEP_RSCSW_DRIVERA_SEQUENCE_PHASE_WAKE 0x00000004u

#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_SUBSYSTEM 1u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_ID 2u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_MASTER_ID 3u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_SLAVE_ID 4u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TEXT 5u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_SUBSYSTEM_ID 6u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_RESOURCE_ID 7u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_SLV_ID 8u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_QUERY_TYPE 9u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_DRV_ID 10u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_TRIGGER_ID 11u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_SET_ID 12u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_CLIENT_TYPE 13u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_EVENT_ID 14u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_DRIVE_TYPE 15u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_THRESHOLD_ID 16u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_DRIVER_ID 17u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DRIVER_MAP_ID 18u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DOMAIN_ID 19u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_INTERCONNECT_ROUTE_ID 20u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_FREQ_MATCH_ID 21u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_RESOURCE_TYPE_ID 22u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_CONFIG_PARAM_ID 23u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_STATUS_ID 24u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_XPORT_STATE_ID 25u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_LINK_STATE_ID 26u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_ERROR_ID 27u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_QUERY_TYPE 28u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_REQUEST_TYPE 29u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_OBJECT_TYPE 30u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_STATE_ID 31u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_TX_ERROR_ID 32u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_STATUS_ID 33u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_QUERY_TYPE_ID 34u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_ERROR_ID 35u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_REMOTE_STATE_ID 36u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_BUS_TYPE_ID 37u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_NOTIFY_THRESHOLD_ID 38u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_NODE_TYPE_ID 39u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_INTERFACE_TYPE_ID 40u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_FLAGS_ID 41u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_PIN_TYPE_ID 42u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_DIR_TYPE_ID 43u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_PULL_TYPE_ID 44u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_VALUE_TYPE_ID 45u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_SROT_STATE_ID 46u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_RESULT_TYPE_ID 47u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_LOCAL_STATE_ID 48u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_CHANNEL_EVENT_ID 49u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_XPORT_PRIORITY_ID 50u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_NOTIFY_CROSSING_ID 51u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_REQUEST_KIND_ID 52u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_HW_NODE_KIND_ID 53u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_SW_STATE_ID 54u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_STAT_INDEX_ID 55u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_SETUP_ERROR_ID 56u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_ERROR_ID 57u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_REQUEST_ATTR_ID 58u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_NODE_ATTR_ID 59u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_RESOURCE_ATTR_ID 60u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_STATUS_ID 61u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_CLIENT_ATTR_ID 62u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_ACTION_ID 63u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_SLEEP_MODE_ID 64u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_RESET_ID 65u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_REQUEST_DATA_ID 66u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_RESOURCE_STATE_ID 67u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_RESOURCE_TYPE_ID 68u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_CLIENT_TYPE_ID 69u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_TEMP_ALARM_ID 70u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_RESOURCE_RANGE_ID 71u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_POWERDOMAIN_ACTION_ID 72u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_SUPPRESSIBLE_STATE_ID 73u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PM_RESOURCE_IF_ID 74u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_QDSS_EVENT_ID 75u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_TIMER_DEADLINE_ID 76u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_IO_TYPE_ID 77u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_LIMIT_TYPE_ID 78u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_VOTE_SET_TYPE_ID 79u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_PMIC_ID 80u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_MVS_SUBTYPE_ID 81u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_CXOBUF_SUBTYPE_ID 82u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_RWAY_SUBTYPE_ID 83u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_REQUEST_TYPE_ID 84u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PM_VRM_VOTE_OFFSET_ID 85u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_SOURCE_CONFIG_ID 86u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_SOURCE_CONFIG_MODE_ID 87u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_TRANSACTION_ATTR_ID 88u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_RESOURCE_TYPE_ID 89u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_EVENT_TRIGGER_ID 90u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_MUTEX_ATTR_ID 91u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_MUTEX_LOCK_TYPE_ID 92u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_LOG_EVENT_ID 93u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_PKT_RESULT_ID 94u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_XPORT_LOG_MASK_ID 95u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_STATE_ID 96u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_MDSS_CFG_ID 97u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_PCIE_CFG_ID 98u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_APSS_CFG_ID 99u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_LPM_MONITOR_STATE_ID 100u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_VOTE_CLASS_ID 101u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_AGG_DRIVER_ID 102u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_ACCESS_PROFILE_ID 103u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_EVENT_CTRL_ID 104u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_SSR_EVENT_ID 105u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_QUERY_PATH_ID 106u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_AGG_DRIVER_ID 107u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_PIN_STATE_ID 108u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_COMPARATOR_TYPE_ID 109u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_CMDQ_STAGE_ID 110u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TRIGGER_REASON_ID 111u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_GPIO_ACTION_ID 112u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_CALLBACK_TYPE_ID 113u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_ADDRESS_MODE_ID 114u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_OPERATING_POINT_ID 115u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_NPA_QUERY_ID 116u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_LOOKUP_STATUS_ID 117u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_SYNC_PRIMITIVE_ID 118u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_QOS_EVENT_ID 119u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_QUERY_STAGE_ID 120u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_RESOURCE_DRIVER_ID 121u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_VOTE_REASON_ID 122u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_TRIGGER_CONFIG_ID 123u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_RESOURCE_SERVICE_ID 124u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_AMC_PHASE_ID 125u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_CMD_ACTION_ID 126u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_CONFIG_PHASE_ID 127u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_CONTROLLER_EVENT_ID 128u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_IPCC_TARGET_ID 129u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_VOTE_BUCKET_ID 130u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_AGG_QUERY_ID 131u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_QUERY_SOURCE_ID 132u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_EVENT_TYPE_ID 133u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_XPORT_CMD_ID 134u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_ROUTE_CLASS_ID 135u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_CLIENT_LIFECYCLE_ID 136u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_TRANSITION_PHASE_ID 137u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_SEQUENCE_PHASE_ID 138u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_REQUEST_OPCODE_ID 139u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_BARRIER_TYPE_ID 140u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_MODE_ID 141u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_TILE_SELECTOR_ID 142u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_INTERRUPT_THRESHOLD_ID 143u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_SERVICE_CLASS_ID 144u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_RM_ACTION_ID 145u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_DOMAIN_POLICY_ID 146u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_ACCESS_PATH_ID 147u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_SYNC_EVENT_ID 148u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_CHANNEL_ROLE_ID 149u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_BW_CLASS_ID 150u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_EVENT_LIFECYCLE_ID 151u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_POLICY_REASON_ID 152u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_WAKE_SOURCE_ID 153u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_VOTE_PATH_ID 154u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_COMPLETION_STATE_ID 155u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_TCS_PRIORITY_ID 156u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_DRIVE_STRENGTH_ID 157u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_TRIP_CLASS_ID 158u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_MAILBOX_STATE_ID 159u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_POLICY_PHASE_ID 160u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_PLAN_STAGE_ID 161u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_SLAVE_REGION_ID 162u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_WORKLOOP_EVENT_ID 163u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_RX_INTENT_STATE_ID 164u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_LATENCY_CLASS_ID 165u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_REQUEST_PHASE_ID 166u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_FREQ_FLOOR_STATE_ID 167u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_LATENCY_MODE_ID 168u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_RAIL_STATE_ID 169u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_WAKESET_TYPE_ID 170u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_SEQ_PHASE_ID 171u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_OWNER_DOMAIN_ID 172u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_FILTER_MODE_ID 173u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_IPCC_PROTOCOL_ID 174u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ARC_RAIL_VOLTAGE_LEVEL_ID 175u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_HAL_API_TYPE_ID 176u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CMDDB_AUX_CTRL_TYPE_ID 177u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_ULOG_LOCK_TYPE_ID 178u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_TRACER_EVENT_ID 179u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_ICB_PEP_PERF_STATE_ID 180u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_NPA_EVENT_TYPE_ID 181u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PCU_POWER_DOMAIN_API_ID 182u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_DEEPSTANDBY_MODE_ID 183u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_PARENT_CHILD_PARSE_STATE_ID 184u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_ISR_TYPE_ID 185u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_RC_DIRTY_STATE_ID 186u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_DISCRETE_GPIO_PARSE_STATE_ID 187u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_INTERRUPT_TYPE_ID 188u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_AOP_SLEEP_STATE_ID 189u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RPMH_RC_PRIORITY_ID 190u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_CONFIG_API_ID 191u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_ULOG_MEMORY_CONFIG_ID 192u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_ULOG_ALLOC_ID 193u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_QDSS_TRACE_STREAM_ID 194u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_GLINK_QDSS_SPEED_ID 195u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_DAL_ULOG_MEMORY_USAGE_ID 196u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_SOURCE_DISABLE_MODE_ID 197u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PDC_IDLE_CALLBACK_TYPE_ID 198u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_PPP_PMIC_GPIO_PARSE_STATE_ID 199u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_RSC_RC_STATE_ID 200u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_CLOCK_PLL_FRAC_MODE_ID 201u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TLMM_LED_GPIO_PARSE_STATE_ID 202u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_QUERY_TSENS_DEVICE_STATE_ID 203u

#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_PARTIAL_MATCH 0x00000001u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_CASE_INSENSITIVE 0x00000002u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_PPP 0x00000004u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_ICB_MASTER 0x00000008u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_ICB_SLAVE 0x00000010u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_AOP 0x00000020u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_ARC 0x00000040u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_CMDDB 0x00000080u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_PDC 0x00000100u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_RPMH 0x00000200u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_NPA 0x00000400u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_GLINK 0x00000800u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_TLMM 0x00001000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_TSENS 0x00002000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_CLOCK 0x00004000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_DRIVER_MAP 0x00008000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_DOMAIN 0x00010000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_ROUTE 0x00020000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_DAL 0x00040000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_PCU 0x00080000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_RSC 0x00100000u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_FLAG_INCLUDE_ARCMGR 0x00200000u

#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_SUBSYSTEM 1u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_RESOURCE 2u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_MASTER 3u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_SLAVE 4u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_SUBSYSTEM 5u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_RESOURCE 6u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_SLV 7u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_QUERY 8u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_DRV 9u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_TRIGGER 10u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_SET 11u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_CLIENT 12u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_EVENT 13u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_DRIVE 14u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_THRESHOLD 15u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_DRIVER 16u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DRIVER_MAP 17u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DOMAIN 18u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_INTERCONNECT_ROUTE 19u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_FREQ_MATCH 20u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_RESOURCE 21u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_CONFIG_PARAM 22u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_STATUS 23u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_XPORT_STATE 24u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_LINK_STATE 25u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_ERROR 26u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_QUERY 27u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_REQUEST 28u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_OBJECT 29u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_STATE 30u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_TX_ERROR 31u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_STATUS 32u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_QUERY 33u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_ERROR 34u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_REMOTE_STATE 35u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_BUS 36u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_NOTIFY_THRESHOLD 37u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_NODE 38u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_INTERFACE 39u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_FLAGS 40u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_PIN_TYPE 41u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_DIR 42u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_PULL 43u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_VALUE 44u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_SROT_STATE 45u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_RESULT_TYPE 46u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_LOCAL_STATE 47u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_CHANNEL_EVENT 48u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_XPORT_PRIORITY 49u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_NOTIFY_CROSSING 50u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_REQUEST_KIND 51u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_HW_NODE_KIND 52u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_SW_STATE 53u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_STAT_INDEX 54u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_SETUP_ERROR 55u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_ERROR 56u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_REQUEST_ATTR 57u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_NODE_ATTR 58u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_RESOURCE_ATTR 59u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_STATUS 60u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_CLIENT_ATTR 61u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_ACTION 62u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_SLEEP_MODE 63u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_RESET 64u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_REQUEST_DATA 65u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_RESOURCE_STATE 66u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_RESOURCE_TYPE 67u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_CLIENT_TYPE 68u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_TEMP_ALARM 69u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_RESOURCE_RANGE 70u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_POWERDOMAIN_ACTION 71u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_SUPPRESSIBLE_STATE 72u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PM_RESOURCE_IF 73u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_QDSS_EVENT 74u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_TIMER_DEADLINE 75u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_IO_TYPE 76u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_LIMIT_TYPE 77u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_VOTE_SET_TYPE 78u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_PMIC_ID 79u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_MVS_SUBTYPE 80u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_CXOBUF_SUBTYPE 81u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_RWAY_SUBTYPE 82u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_REQUEST_TYPE 83u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PM_VRM_VOTE_OFFSET 84u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_SOURCE_CONFIG 85u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_SOURCE_CONFIG_MODE 86u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_TRANSACTION_ATTR 87u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_RESOURCE_TYPE 88u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_EVENT_TRIGGER 89u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_MUTEX_ATTR 90u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_MUTEX_LOCK_TYPE 91u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_LOG_EVENT 92u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_PKT_RESULT 93u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_XPORT_LOG_MASK 94u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_STATE 95u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_MDSS_CFG 96u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_PCIE_CFG 97u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_APSS_CFG 98u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_LPM_MONITOR_STATE 99u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_VOTE_CLASS 100u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_AGG_DRIVER 101u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_ACCESS_PROFILE 102u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_EVENT_CTRL 103u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_SSR_EVENT 104u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_QUERY_PATH 105u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_AGG_DRIVER 106u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_PIN_STATE 107u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_COMPARATOR_TYPE 108u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_CMDQ_STAGE 109u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TRIGGER_REASON 110u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_GPIO_ACTION 111u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_CALLBACK_TYPE 112u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_ADDRESS_MODE 113u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_OPERATING_POINT 114u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_NPA_QUERY 115u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_LOOKUP_STATUS 116u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_SYNC_PRIMITIVE 117u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_QOS_EVENT 118u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_QUERY_STAGE 119u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_RESOURCE_DRIVER 120u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_VOTE_REASON 121u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_TRIGGER_CONFIG 122u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_RESOURCE_SERVICE 123u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_AMC_PHASE 124u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_CMD_ACTION 125u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_CONFIG_PHASE 126u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_CONTROLLER_EVENT 127u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_IPCC_TARGET 128u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_VOTE_BUCKET 129u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_AGG_QUERY 130u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_QUERY_SOURCE 131u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_EVENT_TYPE 132u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_XPORT_CMD 133u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_ROUTE_CLASS 134u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_CLIENT_LIFECYCLE 135u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_TRANSITION_PHASE 136u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_SEQUENCE_PHASE 137u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_REQUEST_OPCODE 138u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_BARRIER_TYPE 139u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_MODE 140u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_TILE_SELECTOR 141u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_INTERRUPT_THRESHOLD 142u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_SERVICE_CLASS 143u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_RM_ACTION 144u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_DOMAIN_POLICY 145u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_ACCESS_PATH 146u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_SYNC_EVENT 147u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_CHANNEL_ROLE 148u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_BW_CLASS 149u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_EVENT_LIFECYCLE 150u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_POLICY_REASON 151u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_WAKE_SOURCE 152u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_VOTE_PATH 153u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_COMPLETION_STATE 154u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_TCS_PRIORITY 155u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_DRIVE_STRENGTH 156u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_TRIP_CLASS 157u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_MAILBOX_STATE 158u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_POLICY_PHASE 159u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_PLAN_STAGE 160u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_SLAVE_REGION 161u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_WORKLOOP_EVENT 162u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_RX_INTENT_STATE 163u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_LATENCY_CLASS 164u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_REQUEST_PHASE 165u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_FREQ_FLOOR_STATE 166u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_LATENCY_MODE 167u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_RAIL_STATE 168u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_WAKESET_TYPE 169u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_SEQ_PHASE 170u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_OWNER_DOMAIN 171u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_FILTER_MODE 172u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_IPCC_PROTOCOL 173u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ARC_RAIL_VOLTAGE_LEVEL 174u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_HAL_API_TYPE 175u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CMDDB_AUX_CTRL_TYPE 176u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_ULOG_LOCK_TYPE 177u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_TRACER_EVENT 178u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_ICB_PEP_PERF_STATE 179u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_NPA_EVENT_TYPE 180u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PCU_POWER_DOMAIN_API 181u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_DEEPSTANDBY_MODE 182u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_PARENT_CHILD_PARSE_STATE 183u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_ISR_TYPE 184u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_RC_DIRTY_STATE 185u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_DISCRETE_GPIO_PARSE_STATE 186u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_INTERRUPT_TYPE 187u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_AOP_SLEEP_STATE 188u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RPMH_RC_PRIORITY 189u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_CONFIG_API 190u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_ULOG_MEMORY_CONFIG 191u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_ULOG_ALLOC 192u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_QDSS_TRACE_STREAM 193u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_GLINK_QDSS_SPEED 194u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_DAL_ULOG_MEMORY_USAGE 195u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_SOURCE_DISABLE_MODE 196u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PDC_IDLE_CALLBACK_TYPE 197u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_PPP_PMIC_GPIO_PARSE_STATE 198u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_RSC_RC_STATE 199u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_CLOCK_PLL_FRAC_MODE 200u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TLMM_LED_GPIO_PARSE_STATE 201u
#define QCPEP_RSCSW_DRIVERA_LOOKUP_KIND_TSENS_DEVICE_STATE 202u

#define QCPEP_RSCSW_DRIVERA_PROVISION_MODE_DEFAULT 1u
#define QCPEP_RSCSW_DRIVERA_PROVISION_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_PROVISION_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_PROVISION_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_APPLY_STATE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_APPLY_POLICY 0x00000002u
#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_RUN_INTERCONNECT 0x00000004u
#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_RUN_DOMAIN 0x00000008u
#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_VERIFY_QUERY 0x00000010u
#define QCPEP_RSCSW_DRIVERA_PROVISION_FLAG_THERMAL_GUARD 0x00000020u

#define QCPEP_RSCSW_DRIVERA_RAIL_MODE_BALANCED 1u
#define QCPEP_RSCSW_DRIVERA_RAIL_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_RAIL_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_RAIL_MODE_RETENTION 4u

#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_APPLY_ARC 0x00000001u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_APPLY_CLOCK 0x00000002u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_APPLY_POLICY 0x00000004u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_RUN_INTERCONNECT 0x00000008u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_RUN_DOMAIN 0x00000010u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_VERIFY_QUERY 0x00000020u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_ISSUE_GLINK 0x00000040u
#define QCPEP_RSCSW_DRIVERA_RAIL_FLAG_THERMAL_GUARD 0x00000080u

#define QCPEP_RSCSW_DRIVERA_TCS_MODE_ACTIVE 1u
#define QCPEP_RSCSW_DRIVERA_TCS_MODE_SLEEP 2u
#define QCPEP_RSCSW_DRIVERA_TCS_MODE_WAKE 3u
#define QCPEP_RSCSW_DRIVERA_TCS_MODE_HIBERNATE 4u

#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_PROGRAM_TCS 0x00000001u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_VOTE_POLICY 0x00000002u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_QUEUE_GLINK 0x00000004u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_RUN_INTERCONNECT 0x00000008u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_RUN_DOMAIN 0x00000010u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_VERIFY_QUERY 0x00000020u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_THERMAL_GUARD 0x00000040u
#define QCPEP_RSCSW_DRIVERA_TCS_FLAG_STRICT_TCS 0x00000080u

#define QCPEP_RSCSW_DRIVERA_THERMAL_MODE_MONITOR 1u
#define QCPEP_RSCSW_DRIVERA_THERMAL_MODE_MITIGATE 2u
#define QCPEP_RSCSW_DRIVERA_THERMAL_MODE_RECOVER 3u
#define QCPEP_RSCSW_DRIVERA_THERMAL_MODE_CALIBRATE 4u

#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_APPLY_STATE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_APPLY_THRESHOLDS 0x00000002u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_APPLY_POLICY 0x00000004u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_RUN_INTERCONNECT 0x00000008u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_RUN_DOMAIN 0x00000010u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_VERIFY_QUERY 0x00000020u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_FORCE_COOLING 0x00000040u
#define QCPEP_RSCSW_DRIVERA_THERMAL_FLAG_ISSUE_GLINK 0x00000080u

#define QCPEP_RSCSW_DRIVERA_COORD_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_COORD_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_COORD_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_COORD_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_SYNC 0x00000001u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_VALIDATE 0x00000002u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_SEQUENCE 0x00000004u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_PROVISION 0x00000008u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_RAIL 0x00000010u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_TCS 0x00000020u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_THERMAL 0x00000040u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_FABRIC 0x00000080u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_INTERCONNECT 0x00000100u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_RUN_DOMAIN 0x00000200u
#define QCPEP_RSCSW_DRIVERA_COORD_FLAG_STRICT_VERIFY 0x00000400u

#define QCPEP_RSCSW_DRIVERA_AUDIT_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_AUDIT_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_AUDIT_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_AUDIT_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_REPAIR_STATE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_REPAIR_CLOCK 0x00000002u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_REPAIR_POLICY 0x00000004u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_VERIFY_ICB 0x00000008u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_VERIFY_ENUM_NAMES 0x00000010u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_RUN_COORD 0x00000020u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_RUN_INTERCONNECT 0x00000040u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_RUN_DOMAIN 0x00000080u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_STRICT 0x00000100u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_VERIFY_DRIVER_MAP 0x00000200u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_VERIFY_DOMAIN_MAP 0x00000400u
#define QCPEP_RSCSW_DRIVERA_AUDIT_FLAG_VERIFY_ROUTE_MAP 0x00000800u

#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_ARC QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_ARC
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_CMDDB QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_CMDDB
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_PDC QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_PDC
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_RPMH QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_RPMH
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_NPA QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_NPA
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_GLINK QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_GLINK
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_PPP QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_PPP
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_TLMM QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_TLMM
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_TSENS QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_TSENS
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_CLOCK QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_CLOCK
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_AOP QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_AOP
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_ICB QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_ICB
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_THERMAL QCPEP_RSCSW_DRIVERA_VALIDATE_ISSUE_THERMAL
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_DRIVER_MAP 0x00002000u
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_DOMAIN_MAP 0x00004000u
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_ROUTE_MAP 0x00008000u
#define QCPEP_RSCSW_DRIVERA_AUDIT_ISSUE_DEPENDENCY_MAP 0x00010000u

#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_ICB_NAME QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_ICB_NAME
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_RPMH_PROFILE QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_RPMH_PROFILE
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_TSENS_PROFILE QCPEP_RSCSW_DRIVERA_VALIDATE_WARN_TSENS_PROFILE
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_DOMAIN_OVERLAP 0x00000008u
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_ROUTE_TOPOLOGY 0x00000010u
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_ROUTE_NAME 0x00000020u
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_DRIVER_FANOUT 0x00000040u
#define QCPEP_RSCSW_DRIVERA_AUDIT_WARN_DEPENDENCY_GAP 0x00000080u

#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_DRIVER_MAP 0x00000001u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_DOMAIN_MAP 0x00000002u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_ROUTE_MAP 0x00000004u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_ROUTE_NAMES 0x00000008u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_STRICT_PATH 0x00000010u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_PROFILE_POLICY 0x00000020u
#define QCPEP_RSCSW_DRIVERA_AUDIT_INTEGRITY_FLAG_DEPENDENCY_POLICY 0x00000040u

#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_RUN_AUDIT 0x00000001u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_RUN_COORD 0x00000002u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_RUN_THERMAL 0x00000004u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_RUN_INTERCONNECT 0x00000008u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_RUN_DOMAIN 0x00000010u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_STRICT 0x00000020u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_REPAIR_STATE 0x00000040u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_REPAIR_CLOCK 0x00000080u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_REPAIR_POLICY 0x00000100u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_VERIFY_ICB 0x00000200u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_VERIFY_ENUM_NAMES 0x00000400u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_TRACK_MODULE_MATRIX 0x00000800u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_VERIFY_DRIVER_MAP 0x00001000u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_VERIFY_DOMAIN_MAP 0x00002000u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_FLAG_VERIFY_ROUTE_MAP 0x00004000u

#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_AOP 1u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_ARC 2u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_CLOCK 3u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_CMDDB 4u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_DAL 5u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_GLINK 6u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_ICB 7u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_NPA 8u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_PCU 9u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_PDC 10u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_PPP 11u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_RPMH 12u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_RSC 13u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_TLMM 14u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_TSENS 15u
#define QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_ARCMGR 16u

#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_RUN_SUPERVISOR 0x00000001u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_RUN_THERMAL 0x00000002u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_APPLY_STATE 0x00000004u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_APPLY_CLOCK 0x00000008u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_APPLY_POLICY 0x00000010u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_STRICT 0x00000020u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_AGGRESSIVE 0x00000040u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_VERIFY_QUERY 0x00000080u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_TRACK_HISTORY 0x00000100u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_DEPENDENCY_GUARD 0x00000200u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_FLAG_PROFILE_POLICY 0x00000400u

#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_DEPENDENCY_GAP 0x01000000u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_DOMAIN_GAP 0x02000000u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_ROUTE_GAP 0x04000000u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_FANOUT_GAP 0x08000000u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_PROFILE_POLICY 0x10000000u
#define QCPEP_RSCSW_DRIVERA_AUTOTUNE_ACTION_RISK_CLAMP 0x20000000u

#define QCPEP_RSCSW_DRIVERA_HARDEN_MODE_BASELINE 1u
#define QCPEP_RSCSW_DRIVERA_HARDEN_MODE_PERFORMANCE 2u
#define QCPEP_RSCSW_DRIVERA_HARDEN_MODE_THERMAL 3u
#define QCPEP_RSCSW_DRIVERA_HARDEN_MODE_RECOVERY 4u

#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_AUTOTUNE 0x00000001u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_SUPERVISOR 0x00000002u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_AUDIT 0x00000004u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_COORD 0x00000008u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_THERMAL 0x00000010u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_INTERCONNECT 0x00000020u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_RUN_DOMAIN 0x00000040u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_STRICT 0x00000080u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_VERIFY_QUERY 0x00000100u
#define QCPEP_RSCSW_DRIVERA_HARDEN_FLAG_TRACK_HISTORY 0x00000200u

#define QCPEP_RSCSW_SCENARIO_AOP 1u
#define QCPEP_RSCSW_SCENARIO_ARC 2u
#define QCPEP_RSCSW_SCENARIO_CLOCK 3u
#define QCPEP_RSCSW_SCENARIO_CMDDB 4u
#define QCPEP_RSCSW_SCENARIO_DAL 5u
#define QCPEP_RSCSW_SCENARIO_GLINK 6u
#define QCPEP_RSCSW_SCENARIO_ICB 7u
#define QCPEP_RSCSW_SCENARIO_NPA 8u
#define QCPEP_RSCSW_SCENARIO_PCU 9u
#define QCPEP_RSCSW_SCENARIO_PDC 10u
#define QCPEP_RSCSW_SCENARIO_PPP 11u
#define QCPEP_RSCSW_SCENARIO_RPMH 12u
#define QCPEP_RSCSW_SCENARIO_RSC 13u
#define QCPEP_RSCSW_SCENARIO_TLMM 14u
#define QCPEP_RSCSW_SCENARIO_TSENS 15u
#define QCPEP_RSCSW_SCENARIO_FULL_SWEEP 16u

#define QCPEP_RSCSW_SCENARIO_FLAG_USE_DEFAULTS 0x00000001u
#define QCPEP_RSCSW_SCENARIO_FLAG_TOGGLE_STATE 0x00000002u
#define QCPEP_RSCSW_SCENARIO_FLAG_REFRESH_EACH 0x00000004u
#define QCPEP_RSCSW_SCENARIO_FLAG_VERIFY_QUERY 0x00000008u
#define QCPEP_RSCSW_SCENARIO_FLAG_INCLUDE_ARCMGR 0x00000010u

#define QCPEP_RSCSW_DIAG_FLAG_INCLUDE_FULL_SWEEP 0x00000001u
#define QCPEP_RSCSW_DIAG_FLAG_TOGGLE_STATE 0x00000002u
#define QCPEP_RSCSW_DIAG_FLAG_VERIFY_QUERY 0x00000004u
#define QCPEP_RSCSW_DIAG_FLAG_INCLUDE_ARCMGR 0x00000008u
#define QCPEP_RSCSW_DIAG_FLAG_REFRESH_EACH 0x00000010u

#define QCPEP_RSCSW_PROFILE_FLAG_RUN_MATRIX 0x00000001u
#define QCPEP_RSCSW_PROFILE_FLAG_INCLUDE_FULL_SWEEP 0x00000002u
#define QCPEP_RSCSW_PROFILE_FLAG_FORCE_VERIFY 0x00000004u
#define QCPEP_RSCSW_PROFILE_FLAG_FORCE_TOGGLE 0x00000008u

#define QCPEP_RSCSW_PLAN_FLAG_RESPECT_DEPENDENCY 0x00000001u
#define QCPEP_RSCSW_PLAN_FLAG_VERIFY_NODE 0x00000002u
#define QCPEP_RSCSW_PLAN_FLAG_RECORD_TEST_EVENT 0x00000004u

#define QCPEP_RSCSW_PLAN_PREVIEW_FLAG_INCLUDE_DEPENDENCIES 0x00000001u
#define QCPEP_RSCSW_PLAN_PREVIEW_FLAG_ASSUME_ENABLE_DEPENDENCIES 0x00000002u
#define QCPEP_RSCSW_PLAN_PREVIEW_FLAG_RESPECT_ORDER 0x00000004u

#define QCPEP_RSCSW_CHECKPOINT_OP_CAPTURE 1u
#define QCPEP_RSCSW_CHECKPOINT_OP_APPLY 2u

#define QCPEP_RSCSW_COVERAGE_FLAG_REQUIRE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_COVERAGE_FLAG_REQUIRE_SUCCESS_THRESHOLD 0x00000002u
#define QCPEP_RSCSW_COVERAGE_FLAG_REQUIRE_RECENT_ACTIVITY 0x00000004u
#define QCPEP_RSCSW_COVERAGE_FLAG_INCLUDE_DEPENDENCY_LOAD 0x00000008u
#define QCPEP_RSCSW_COVERAGE_FLAG_STRICT_THRESHOLD 0x00000010u

#define QCPEP_RSCSW_REMEDIATION_FLAG_APPLY_RECOMMENDATIONS 0x00000001u
#define QCPEP_RSCSW_REMEDIATION_FLAG_TARGET_INACTIVE 0x00000002u
#define QCPEP_RSCSW_REMEDIATION_FLAG_TARGET_LOW_SUCCESS 0x00000004u
#define QCPEP_RSCSW_REMEDIATION_FLAG_TARGET_STALE_ACTIVITY 0x00000008u
#define QCPEP_RSCSW_REMEDIATION_FLAG_INCLUDE_DEPENDENCY_LOAD 0x00000010u
#define QCPEP_RSCSW_REMEDIATION_FLAG_STRICT_THRESHOLD 0x00000020u

#define QCPEP_RSCSW_REMEDIATION_REASON_INACTIVE 0x00000001u
#define QCPEP_RSCSW_REMEDIATION_REASON_LOW_SUCCESS 0x00000002u
#define QCPEP_RSCSW_REMEDIATION_REASON_STALE_ACTIVITY 0x00000004u
#define QCPEP_RSCSW_REMEDIATION_REASON_DEPENDENCY_PRESSURE 0x00000008u
#define QCPEP_RSCSW_REMEDIATION_REASON_LAST_STATUS_FAIL 0x00000010u
#define QCPEP_RSCSW_REMEDIATION_REASON_HIGH_FAILURE_RATE 0x00000020u

#define QCPEP_RSCSW_STABILIZATION_FLAG_INCLUDE_CONTROL_LANE 0x00000001u
#define QCPEP_RSCSW_STABILIZATION_FLAG_INCLUDE_INTERCONNECT_LANE 0x00000002u
#define QCPEP_RSCSW_STABILIZATION_FLAG_INCLUDE_THERMAL_LANE 0x00000004u
#define QCPEP_RSCSW_STABILIZATION_FLAG_RUN_PROFILES 0x00000008u
#define QCPEP_RSCSW_STABILIZATION_FLAG_EXECUTE_REMEDIATION 0x00000010u
#define QCPEP_RSCSW_STABILIZATION_FLAG_VERIFY_POST 0x00000020u
#define QCPEP_RSCSW_STABILIZATION_FLAG_STRICT_COVERAGE 0x00000040u

#define QCPEP_RSCSW_CONFORMANCE_FLAG_REQUIRE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_REQUIRE_SUCCESS_THRESHOLD 0x00000002u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_REQUIRE_RECENT_ACTIVITY 0x00000004u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_REQUIRE_DEPENDENCY_SHAPE 0x00000008u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_REQUIRE_CATALOG_MATCH 0x00000010u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_STRICT_THRESHOLD 0x00000020u
#define QCPEP_RSCSW_CONFORMANCE_FLAG_INCLUDE_ARCMGR 0x00000040u

#define QCPEP_RSCSW_CONFORMANCE_REASON_INACTIVE 0x00000001u
#define QCPEP_RSCSW_CONFORMANCE_REASON_LOW_SUCCESS 0x00000002u
#define QCPEP_RSCSW_CONFORMANCE_REASON_LOW_ACTIVITY 0x00000004u
#define QCPEP_RSCSW_CONFORMANCE_REASON_DEPENDENCY_GAP 0x00000008u
#define QCPEP_RSCSW_CONFORMANCE_REASON_STATUS_FAIL 0x00000010u
#define QCPEP_RSCSW_CONFORMANCE_REASON_CATALOG_MISMATCH 0x00000020u
#define QCPEP_RSCSW_CONFORMANCE_REASON_HIGH_FAILURE_RATE 0x00000040u

#define QCPEP_RSCSW_ORCHESTRATION_FLAG_RUN_COVERAGE 0x00000001u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_RUN_REMEDIATION 0x00000002u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_RUN_STABILIZATION 0x00000004u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_RUN_CONFORMANCE 0x00000008u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_RUN_PROFILES 0x00000010u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_STRICT 0x00000020u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_INCLUDE_ARCMGR 0x00000040u
#define QCPEP_RSCSW_ORCHESTRATION_FLAG_REFRESH_EACH_STAGE 0x00000080u

#define QCPEP_RSCSW_ORCHESTRATION_STAGE_COVERAGE 1u
#define QCPEP_RSCSW_ORCHESTRATION_STAGE_REMEDIATION 2u
#define QCPEP_RSCSW_ORCHESTRATION_STAGE_STABILIZATION 3u
#define QCPEP_RSCSW_ORCHESTRATION_STAGE_CONFORMANCE 4u
#define QCPEP_RSCSW_ORCHESTRATION_STAGE_PROFILES 5u

#define QCPEP_RSCSW_POLICY_FLAG_VOTE 0x00000001u
#define QCPEP_RSCSW_POLICY_FLAG_REMOVE 0x00000002u
#define QCPEP_RSCSW_POLICY_FLAG_PROPAGATED 0x00000004u
#define QCPEP_RSCSW_POLICY_FLAG_PARENT 0x00000008u
#define QCPEP_RSCSW_POLICY_FLAG_ACTIVE 0x00000010u

#define QCPEP_RSCSW_FABRIC_FLAG_REQUIRE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_FABRIC_FLAG_REQUIRE_POLICY_ACTIVITY 0x00000002u
#define QCPEP_RSCSW_FABRIC_FLAG_REQUIRE_DEPENDENCY_HEALTH 0x00000004u
#define QCPEP_RSCSW_FABRIC_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define QCPEP_RSCSW_FABRIC_FLAG_STRICT 0x00000010u

#define QCPEP_RSCSW_DOMAIN_FLAG_REQUIRE_ACTIVE 0x00000001u
#define QCPEP_RSCSW_DOMAIN_FLAG_REQUIRE_POLICY_ACTIVITY 0x00000002u
#define QCPEP_RSCSW_DOMAIN_FLAG_REQUIRE_DEPENDENCY_HEALTH 0x00000004u
#define QCPEP_RSCSW_DOMAIN_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define QCPEP_RSCSW_DOMAIN_FLAG_STRICT 0x00000010u

#define QCPEP_RSCSW_INTERCONNECT_FLAG_REQUIRE_ACTIVE_ROUTES 0x00000001u
#define QCPEP_RSCSW_INTERCONNECT_FLAG_REQUIRE_BANDWIDTH_HEADROOM 0x00000002u
#define QCPEP_RSCSW_INTERCONNECT_FLAG_REQUIRE_POLICY_LINK 0x00000004u
#define QCPEP_RSCSW_INTERCONNECT_FLAG_INCLUDE_THERMAL_PRESSURE 0x00000008u
#define QCPEP_RSCSW_INTERCONNECT_FLAG_STRICT 0x00000010u

#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_ACTIVE 0x00000001u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_POLICY_LINKED 0x00000002u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_THERMAL_HIGH 0x00000004u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_STATUS_FAIL 0x00000008u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_UNDER_PROVISIONED 0x00000010u
#define QCPEP_RSCSW_INTERCONNECT_ROUTE_FLAG_OVER_UTILIZED 0x00000020u

typedef struct _QCPEP_RSCSW_NODE_INFO {
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
} QCPEP_RSCSW_NODE_INFO;

typedef struct _QCPEP_RSCSW_EVENT_INFO {
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
} QCPEP_RSCSW_EVENT_INFO;

typedef struct _QCPEP_RSCSW_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_NODE_INFO Nodes[QCPEP_RSCSW_NODE_INFO_CAPACITY];
} QCPEP_RSCSW_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_EVENT_INFO Entries[1];
} QCPEP_RSCSW_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_SET_NODE_IN {
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
} QCPEP_RSCSW_SET_NODE_IN;

typedef struct _QCPEP_RSCSW_SET_NODE_OUT {
    LONG Status;
    QCPEP_RSCSW_NODE_INFO Node;
} QCPEP_RSCSW_SET_NODE_OUT;

typedef struct _QCPEP_RSCSW_QUERY_NODE_IN {
    ULONG SubsystemId;
} QCPEP_RSCSW_QUERY_NODE_IN;

typedef struct _QCPEP_RSCSW_QUERY_NODE_OUT {
    LONG Status;
    QCPEP_RSCSW_NODE_INFO Node;
} QCPEP_RSCSW_QUERY_NODE_OUT;

typedef struct _QCPEP_RSCSW_BATCH_ENTRY_IN {
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
} QCPEP_RSCSW_BATCH_ENTRY_IN;

typedef struct _QCPEP_RSCSW_APPLY_BATCH_IN {
    ULONG Count;
    ULONG Flags;
    ULONG IoctlCode;
    ULONG Reserved;
    QCPEP_RSCSW_BATCH_ENTRY_IN Entries[1];
} QCPEP_RSCSW_APPLY_BATCH_IN;

typedef struct _QCPEP_RSCSW_APPLY_BATCH_OUT {
    LONG Status;
    ULONG RequestedCount;
    ULONG AppliedCount;
    ULONG FailedCount;
    LONG FirstFailureStatus;
} QCPEP_RSCSW_APPLY_BATCH_OUT;

typedef struct _QCPEP_RSCSW_CATALOG_ENTRY_INFO {
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
} QCPEP_RSCSW_CATALOG_ENTRY_INFO;

typedef struct _QCPEP_RSCSW_CATALOG_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG EntrySize;
    ULONG Reserved;
    QCPEP_RSCSW_CATALOG_ENTRY_INFO Entries[1];
} QCPEP_RSCSW_CATALOG_OUT;

typedef struct _QCPEP_RSCSW_SCENARIO_RESULT_INFO {
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
} QCPEP_RSCSW_SCENARIO_RESULT_INFO;

typedef struct _QCPEP_RSCSW_RUN_SCENARIO_IN {
    ULONG ScenarioId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} QCPEP_RSCSW_RUN_SCENARIO_IN;

typedef struct _QCPEP_RSCSW_RUN_SCENARIO_OUT {
    LONG Status;
    QCPEP_RSCSW_SCENARIO_RESULT_INFO Result;
} QCPEP_RSCSW_RUN_SCENARIO_OUT;

typedef struct _QCPEP_RSCSW_SCENARIO_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_SCENARIO_RESULT_INFO Entries[1];
} QCPEP_RSCSW_SCENARIO_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DIAG_ENTRY_INFO {
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
} QCPEP_RSCSW_DIAG_ENTRY_INFO;

typedef struct _QCPEP_RSCSW_DIAG_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DIAG_ENTRY_INFO Entries[QCPEP_RSCSW_DIAG_INFO_CAPACITY];
} QCPEP_RSCSW_DIAG_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DIAG_RESULT_INFO {
    ULONG Iterations;
    ULONG Flags;
    LONG Status;
    ULONG ScenarioRuns;
    ULONG ScenarioFailures;
    ULONG LastScenarioId;
    ULONG LastSubsystemId;
    ULONG LastTag;
    ULONGLONG Tick100ns;
} QCPEP_RSCSW_DIAG_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DIAG_RUN_MATRIX_IN {
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DIAG_RUN_MATRIX_IN;

typedef struct _QCPEP_RSCSW_DIAG_RUN_MATRIX_OUT {
    LONG Status;
    QCPEP_RSCSW_DIAG_RESULT_INFO Result;
} QCPEP_RSCSW_DIAG_RUN_MATRIX_OUT;

typedef struct _QCPEP_RSCSW_DIAG_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DIAG_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DIAG_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_PROFILE_ENTRY_INFO {
    ULONG ProfileId;
    CHAR Name[24];
    ULONG SubsystemMask;
    ULONG ScenarioFlags;
    ULONG MatrixFlags;
    ULONG DefaultIterations;
    ULONG Priority;
    ULONG Reserved;
} QCPEP_RSCSW_PROFILE_ENTRY_INFO;

typedef struct _QCPEP_RSCSW_PROFILE_CATALOG_OUT {
    ULONG Version;
    ULONG ProfileCount;
    ULONG ProfileRunCount;
    ULONG ProfileFailureCount;
    ULONG LastProfileId;
    LONG LastProfileStatus;
    ULONG LastProfileIterations;
    ULONG LastProfileFlags;
    ULONGLONG LastProfileTick100ns;
    QCPEP_RSCSW_PROFILE_ENTRY_INFO Profiles[QCPEP_RSCSW_PROFILE_INFO_CAPACITY];
} QCPEP_RSCSW_PROFILE_CATALOG_OUT;

typedef struct _QCPEP_RSCSW_PROFILE_RESULT_INFO {
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
} QCPEP_RSCSW_PROFILE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_PROFILE_RUN_IN {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} QCPEP_RSCSW_PROFILE_RUN_IN;

typedef struct _QCPEP_RSCSW_PROFILE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_PROFILE_RESULT_INFO Result;
} QCPEP_RSCSW_PROFILE_RUN_OUT;

typedef struct _QCPEP_RSCSW_PROFILE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_PROFILE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_PROFILE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_GRAPH_LINK_INFO {
    ULONG FromSubsystemId;
    ULONG ToSubsystemId;
    ULONG Weight;
    ULONG LinkType;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_GRAPH_LINK_INFO;

typedef struct _QCPEP_RSCSW_GRAPH_SNAPSHOT_OUT {
    ULONG Version;
    ULONG NodeCount;
    ULONG LinkCount;
    ULONG ActiveMask;
    ULONG LastPlanStatus;
    ULONG LastPlanStepCount;
    ULONG LastPlanFailureCount;
    ULONGLONG LastPlanTick100ns;
    QCPEP_RSCSW_GRAPH_LINK_INFO Links[QCPEP_RSCSW_GRAPH_LINK_INFO_CAPACITY];
} QCPEP_RSCSW_GRAPH_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_PLAN_STEP_IN {
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
} QCPEP_RSCSW_PLAN_STEP_IN;

typedef struct _QCPEP_RSCSW_PLAN_EXECUTE_IN {
    ULONG StepCount;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    QCPEP_RSCSW_PLAN_STEP_IN Steps[1];
} QCPEP_RSCSW_PLAN_EXECUTE_IN;

typedef struct _QCPEP_RSCSW_PLAN_RESULT_INFO {
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
} QCPEP_RSCSW_PLAN_RESULT_INFO;

typedef struct _QCPEP_RSCSW_PLAN_EXECUTE_OUT {
    LONG Status;
    QCPEP_RSCSW_PLAN_RESULT_INFO Result;
} QCPEP_RSCSW_PLAN_EXECUTE_OUT;

typedef struct _QCPEP_RSCSW_PLAN_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_PLAN_RESULT_INFO Entries[1];
} QCPEP_RSCSW_PLAN_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_PLAN_PREVIEW_RESULT_INFO {
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
} QCPEP_RSCSW_PLAN_PREVIEW_RESULT_INFO;

typedef struct _QCPEP_RSCSW_PLAN_PREVIEW_SNAPSHOT_OUT {
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
} QCPEP_RSCSW_PLAN_PREVIEW_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_PLAN_PREVIEW_IN {
    ULONG StepCount;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    QCPEP_RSCSW_PLAN_STEP_IN Steps[1];
} QCPEP_RSCSW_PLAN_PREVIEW_IN;

typedef struct _QCPEP_RSCSW_PLAN_PREVIEW_OUT {
    LONG Status;
    QCPEP_RSCSW_PLAN_PREVIEW_RESULT_INFO Result;
} QCPEP_RSCSW_PLAN_PREVIEW_OUT;

typedef struct _QCPEP_RSCSW_PLAN_PREVIEW_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_PLAN_PREVIEW_RESULT_INFO Entries[1];
} QCPEP_RSCSW_PLAN_PREVIEW_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_CHECKPOINT_SLOT_INFO {
    ULONG SlotId;
    ULONG Valid;
    CHAR Name[24];
    ULONG CaptureCount;
    ULONG ApplyCount;
    LONG LastStatus;
    ULONG NodeCount;
    ULONG ActiveMask;
    ULONGLONG LastTick100ns;
} QCPEP_RSCSW_CHECKPOINT_SLOT_INFO;

typedef struct _QCPEP_RSCSW_CHECKPOINT_RESULT_INFO {
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
} QCPEP_RSCSW_CHECKPOINT_RESULT_INFO;

typedef struct _QCPEP_RSCSW_CHECKPOINT_SNAPSHOT_OUT {
    ULONG Version;
    ULONG SlotCount;
    ULONG ValidCount;
    ULONG CaptureCount;
    ULONG ApplyCount;
    LONG LastOperationStatus;
    ULONG LastOperationSlotId;
    ULONGLONG LastOperationTick100ns;
    QCPEP_RSCSW_CHECKPOINT_SLOT_INFO Slots[QCPEP_RSCSW_CHECKPOINT_INFO_CAPACITY];
} QCPEP_RSCSW_CHECKPOINT_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_CHECKPOINT_CAPTURE_IN {
    ULONG SlotId;
    CHAR Name[24];
    ULONG Flags;
    ULONG Reserved;
} QCPEP_RSCSW_CHECKPOINT_CAPTURE_IN;

typedef struct _QCPEP_RSCSW_CHECKPOINT_CAPTURE_OUT {
    LONG Status;
    QCPEP_RSCSW_CHECKPOINT_RESULT_INFO Result;
} QCPEP_RSCSW_CHECKPOINT_CAPTURE_OUT;

typedef struct _QCPEP_RSCSW_CHECKPOINT_APPLY_IN {
    ULONG SlotId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_CHECKPOINT_APPLY_IN;

typedef struct _QCPEP_RSCSW_CHECKPOINT_APPLY_OUT {
    LONG Status;
    QCPEP_RSCSW_CHECKPOINT_RESULT_INFO Result;
} QCPEP_RSCSW_CHECKPOINT_APPLY_OUT;

typedef struct _QCPEP_RSCSW_CHECKPOINT_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_CHECKPOINT_RESULT_INFO Entries[1];
} QCPEP_RSCSW_CHECKPOINT_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_COVERAGE_ROW_INFO {
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
} QCPEP_RSCSW_COVERAGE_ROW_INFO;

typedef struct _QCPEP_RSCSW_COVERAGE_RESULT_INFO {
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
} QCPEP_RSCSW_COVERAGE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_COVERAGE_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_COVERAGE_ROW_INFO Rows[QCPEP_RSCSW_COVERAGE_INFO_CAPACITY];
} QCPEP_RSCSW_COVERAGE_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_COVERAGE_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_COVERAGE_RUN_IN;

typedef struct _QCPEP_RSCSW_COVERAGE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_COVERAGE_RESULT_INFO Result;
} QCPEP_RSCSW_COVERAGE_RUN_OUT;

typedef struct _QCPEP_RSCSW_COVERAGE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_COVERAGE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_COVERAGE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_REMEDIATION_ACTION_INFO {
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
} QCPEP_RSCSW_REMEDIATION_ACTION_INFO;

typedef struct _QCPEP_RSCSW_REMEDIATION_RESULT_INFO {
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
} QCPEP_RSCSW_REMEDIATION_RESULT_INFO;

typedef struct _QCPEP_RSCSW_REMEDIATION_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_REMEDIATION_ACTION_INFO Actions[QCPEP_RSCSW_REMEDIATION_INFO_CAPACITY];
} QCPEP_RSCSW_REMEDIATION_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_REMEDIATION_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_REMEDIATION_RUN_IN;

typedef struct _QCPEP_RSCSW_REMEDIATION_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_REMEDIATION_RESULT_INFO Result;
} QCPEP_RSCSW_REMEDIATION_RUN_OUT;

typedef struct _QCPEP_RSCSW_REMEDIATION_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_REMEDIATION_RESULT_INFO Entries[1];
} QCPEP_RSCSW_REMEDIATION_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_STABILIZATION_LANE_INFO {
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
} QCPEP_RSCSW_STABILIZATION_LANE_INFO;

typedef struct _QCPEP_RSCSW_STABILIZATION_RESULT_INFO {
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
} QCPEP_RSCSW_STABILIZATION_RESULT_INFO;

typedef struct _QCPEP_RSCSW_STABILIZATION_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_STABILIZATION_LANE_INFO Lanes[QCPEP_RSCSW_STABILIZATION_LANE_INFO_CAPACITY];
} QCPEP_RSCSW_STABILIZATION_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_STABILIZATION_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_STABILIZATION_RUN_IN;

typedef struct _QCPEP_RSCSW_STABILIZATION_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_STABILIZATION_RESULT_INFO Result;
} QCPEP_RSCSW_STABILIZATION_RUN_OUT;

typedef struct _QCPEP_RSCSW_STABILIZATION_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_STABILIZATION_RESULT_INFO Entries[1];
} QCPEP_RSCSW_STABILIZATION_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_CONFORMANCE_ROW_INFO {
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
} QCPEP_RSCSW_CONFORMANCE_ROW_INFO;

typedef struct _QCPEP_RSCSW_CONFORMANCE_RESULT_INFO {
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
} QCPEP_RSCSW_CONFORMANCE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_CONFORMANCE_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_CONFORMANCE_ROW_INFO Rows[QCPEP_RSCSW_CONFORMANCE_INFO_CAPACITY];
} QCPEP_RSCSW_CONFORMANCE_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_CONFORMANCE_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_CONFORMANCE_RUN_IN;

typedef struct _QCPEP_RSCSW_CONFORMANCE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_CONFORMANCE_RESULT_INFO Result;
} QCPEP_RSCSW_CONFORMANCE_RUN_OUT;

typedef struct _QCPEP_RSCSW_CONFORMANCE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_CONFORMANCE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_CONFORMANCE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_STAGE_INFO {
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
} QCPEP_RSCSW_ORCHESTRATION_STAGE_INFO;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_RESULT_INFO {
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
} QCPEP_RSCSW_ORCHESTRATION_RESULT_INFO;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_ORCHESTRATION_STAGE_INFO Stages[QCPEP_RSCSW_ORCHESTRATION_STAGE_INFO_CAPACITY];
} QCPEP_RSCSW_ORCHESTRATION_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_ORCHESTRATION_RUN_IN;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_ORCHESTRATION_RESULT_INFO Result;
} QCPEP_RSCSW_ORCHESTRATION_RUN_OUT;

typedef struct _QCPEP_RSCSW_ORCHESTRATION_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_ORCHESTRATION_RESULT_INFO Entries[1];
} QCPEP_RSCSW_ORCHESTRATION_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_POLICY_RESOURCE_INFO {
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
} QCPEP_RSCSW_POLICY_RESOURCE_INFO;

typedef struct _QCPEP_RSCSW_POLICY_EVENT_INFO {
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
} QCPEP_RSCSW_POLICY_EVENT_INFO;

typedef struct _QCPEP_RSCSW_POLICY_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_POLICY_RESOURCE_INFO Resources[QCPEP_RSCSW_POLICY_RESOURCE_INFO_CAPACITY];
} QCPEP_RSCSW_POLICY_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_POLICY_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_POLICY_EVENT_INFO Entries[1];
} QCPEP_RSCSW_POLICY_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_FABRIC_LANE_INFO {
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
} QCPEP_RSCSW_FABRIC_LANE_INFO;

typedef struct _QCPEP_RSCSW_FABRIC_RESULT_INFO {
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
} QCPEP_RSCSW_FABRIC_RESULT_INFO;

typedef struct _QCPEP_RSCSW_FABRIC_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_FABRIC_LANE_INFO Lanes[QCPEP_RSCSW_FABRIC_LANE_INFO_CAPACITY];
} QCPEP_RSCSW_FABRIC_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_FABRIC_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_FABRIC_RUN_IN;

typedef struct _QCPEP_RSCSW_FABRIC_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_FABRIC_RESULT_INFO Result;
} QCPEP_RSCSW_FABRIC_RUN_OUT;

typedef struct _QCPEP_RSCSW_FABRIC_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_FABRIC_RESULT_INFO Entries[1];
} QCPEP_RSCSW_FABRIC_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DOMAIN_INFO {
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
} QCPEP_RSCSW_DOMAIN_INFO;

typedef struct _QCPEP_RSCSW_DOMAIN_RESULT_INFO {
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
} QCPEP_RSCSW_DOMAIN_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DOMAIN_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DOMAIN_INFO Domains[QCPEP_RSCSW_DOMAIN_INFO_CAPACITY];
} QCPEP_RSCSW_DOMAIN_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DOMAIN_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_DOMAIN_RUN_IN;

typedef struct _QCPEP_RSCSW_DOMAIN_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DOMAIN_RESULT_INFO Result;
} QCPEP_RSCSW_DOMAIN_RUN_OUT;

typedef struct _QCPEP_RSCSW_DOMAIN_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DOMAIN_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DOMAIN_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_INTERCONNECT_ROUTE_INFO {
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
} QCPEP_RSCSW_INTERCONNECT_ROUTE_INFO;

typedef struct _QCPEP_RSCSW_INTERCONNECT_RESULT_INFO {
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
} QCPEP_RSCSW_INTERCONNECT_RESULT_INFO;

typedef struct _QCPEP_RSCSW_INTERCONNECT_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_INTERCONNECT_ROUTE_INFO Routes[QCPEP_RSCSW_INTERCONNECT_ROUTE_INFO_CAPACITY];
} QCPEP_RSCSW_INTERCONNECT_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_INTERCONNECT_RUN_IN {
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
    ULONG Reserved2;
} QCPEP_RSCSW_INTERCONNECT_RUN_IN;

typedef struct _QCPEP_RSCSW_INTERCONNECT_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_INTERCONNECT_RESULT_INFO Result;
} QCPEP_RSCSW_INTERCONNECT_RUN_OUT;

typedef struct _QCPEP_RSCSW_INTERCONNECT_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_INTERCONNECT_RESULT_INFO Entries[1];
} QCPEP_RSCSW_INTERCONNECT_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_TOPOLOGY_NODE_INFO {
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
} QCPEP_RSCSW_TOPOLOGY_NODE_INFO;

typedef struct _QCPEP_RSCSW_TOPOLOGY_ROUTE_INFO {
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
} QCPEP_RSCSW_TOPOLOGY_ROUTE_INFO;

typedef struct _QCPEP_RSCSW_TOPOLOGY_SNAPSHOT_OUT {
    ULONG Version;
    ULONG NodeCount;
    ULONG RouteCount;
    ULONG DriverMapCount;
    ULONG PppResourceNameCount;
    ULONG IcbMasterNameCount;
    ULONG IcbSlaveNameCount;
    ULONG Reserved;
    QCPEP_RSCSW_TOPOLOGY_NODE_INFO Nodes[QCPEP_RSCSW_TOPOLOGY_NODE_INFO_CAPACITY];
    QCPEP_RSCSW_TOPOLOGY_ROUTE_INFO Routes[QCPEP_RSCSW_TOPOLOGY_ROUTE_INFO_CAPACITY];
} QCPEP_RSCSW_TOPOLOGY_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO {
    ULONG Value;
    CHAR Name[48];
} QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SUBSYSTEM_INFO {
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
} QCPEP_RSCSW_DRIVERA_SUBSYSTEM_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_CATALOG_OUT {
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
    QCPEP_RSCSW_DRIVERA_SUBSYSTEM_INFO Subsystems[QCPEP_RSCSW_DRIVERA_SUBSYSTEM_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    AopSubsystems[QCPEP_RSCSW_DRIVERA_AOP_SUBSYSTEM_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    ArcResources[QCPEP_RSCSW_DRIVERA_ARC_RESOURCE_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO CmdDbSlvIds[QCPEP_RSCSW_DRIVERA_CMDDB_SLV_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO PdcDrvTypes[QCPEP_RSCSW_DRIVERA_PDC_DRV_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO PdcTriggers[QCPEP_RSCSW_DRIVERA_PDC_TRIGGER_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO RpmhSets[QCPEP_RSCSW_DRIVERA_RPMH_SET_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    NpaClientTypes[QCPEP_RSCSW_DRIVERA_NPA_CLIENT_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO GlinkEvents[QCPEP_RSCSW_DRIVERA_GLINK_EVENT_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    TlmmDriveTypes[QCPEP_RSCSW_DRIVERA_TLMM_DRIVE_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    TsensThresholds[QCPEP_RSCSW_DRIVERA_TSENS_THRESHOLD_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    ClockDriverTypes[QCPEP_RSCSW_DRIVERA_CLOCK_DRIVER_INFO_CAPACITY];
    QCPEP_RSCSW_DRIVERA_ENUM_ENTRY_INFO
    CmdDbQueryTypes[QCPEP_RSCSW_DRIVERA_CMDDB_QUERY_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_CATALOG_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_LANE_INFO {
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
} QCPEP_RSCSW_DRIVERA_SYNC_LANE_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_SYNC_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_SYNC_LANE_INFO Lanes[QCPEP_RSCSW_DRIVERA_SYNC_LANE_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_SYNC_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_RUN_IN {
    ULONG ProfileId;
    ULONG Iterations;
    ULONG Flags;
    ULONG Reserved;
} QCPEP_RSCSW_DRIVERA_SYNC_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_SYNC_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_SYNC_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SYNC_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_SYNC_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_SYNC_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_VALIDATE_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_VALIDATE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_VALIDATE_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_VALIDATE_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_VALIDATE_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_VALIDATE_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_VALIDATE_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_VALIDATE_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_VALIDATE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_VALIDATE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_VALIDATE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_SEQUENCE_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_SEQUENCE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_SEQUENCE_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_SEQUENCE_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_SEQUENCE_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_SEQUENCE_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_SEQUENCE_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_SEQUENCE_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SEQUENCE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_SEQUENCE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_SEQUENCE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO {
    ULONG Kind;
    ULONG Id;
    CHAR Name[64];
    ULONG SubsystemMask;
    ULONG ScorePermille;
    ULONG Aux0;
    ULONG Aux1;
} QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_LOOKUP_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    QCPEP_RSCSW_DRIVERA_LOOKUP_RESULT_INFO LastResult;
    ULONG LastMatchCount;
    QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO
    LastMatches[QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_LOOKUP_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_RUN_IN {
    ULONG QueryType;
    ULONG QueryId;
    ULONG Flags;
    CHAR QueryText[64];
} QCPEP_RSCSW_DRIVERA_LOOKUP_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_LOOKUP_RESULT_INFO Result;
    ULONG MatchCount;
    ULONG OverflowCount;
    QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO Matches[QCPEP_RSCSW_DRIVERA_LOOKUP_MATCH_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_LOOKUP_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_LOOKUP_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_LOOKUP_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_LOOKUP_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_PROVISION_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_RESULT_INFO {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PlannedCount;
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
} QCPEP_RSCSW_DRIVERA_PROVISION_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPlannedCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastPolicyVoteCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    QCPEP_RSCSW_DRIVERA_PROVISION_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_PROVISION_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_PROVISION_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_PROVISION_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_PROVISION_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_PROVISION_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_PROVISION_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_PROVISION_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_PROVISION_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_RAIL_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_RESULT_INFO {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PlannedCount;
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
} QCPEP_RSCSW_DRIVERA_RAIL_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPlannedCount;
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
    QCPEP_RSCSW_DRIVERA_RAIL_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_RAIL_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_RAIL_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_RAIL_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_RAIL_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_RAIL_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_RAIL_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_RAIL_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_RAIL_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_TCS_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_RESULT_INFO {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PlannedCount;
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
} QCPEP_RSCSW_DRIVERA_TCS_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPlannedCount;
    ULONG LastProgrammedCount;
    ULONG LastFailedCount;
    ULONG LastPolicyVoteCount;
    ULONG LastQueuedGlinkCount;
    LONG LastInterconnectStatus;
    LONG LastDomainStatus;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    QCPEP_RSCSW_DRIVERA_TCS_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_TCS_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_TCS_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_TCS_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_TCS_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_TCS_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_TCS_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_TCS_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_TCS_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_THERMAL_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_RESULT_INFO {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PlannedCount;
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
} QCPEP_RSCSW_DRIVERA_THERMAL_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPlannedCount;
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
    QCPEP_RSCSW_DRIVERA_THERMAL_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_THERMAL_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_THERMAL_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_THERMAL_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_THERMAL_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_THERMAL_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_THERMAL_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_THERMAL_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_THERMAL_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_COORD_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_RESULT_INFO {
    ULONG ModeId;
    ULONG Flags;
    LONG Status;
    ULONG PlannedCount;
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
} QCPEP_RSCSW_DRIVERA_COORD_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_SNAPSHOT_OUT {
    ULONG Version;
    ULONG RunCount;
    ULONG FailureCount;
    LONG LastStatus;
    ULONG LastModeId;
    ULONG LastFlags;
    ULONG LastPlannedCount;
    ULONG LastAppliedCount;
    ULONG LastFailedCount;
    ULONG LastExecutedPhaseMask;
    ULONG LastFailedPhaseMask;
    ULONG LastAvgConfidencePermille;
    ULONGLONG LastTick100ns;
    ULONG RowCount;
    QCPEP_RSCSW_DRIVERA_COORD_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_COORD_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_COORD_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_COORD_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_COORD_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_COORD_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_COORD_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_COORD_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_COORD_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_ROW_INFO {
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
} QCPEP_RSCSW_DRIVERA_AUDIT_ROW_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_AUDIT_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_AUDIT_ROW_INFO Rows[QCPEP_RSCSW_DRIVERA_AUDIT_ROW_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_AUDIT_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Reserved0;
    ULONG Reserved1;
} QCPEP_RSCSW_DRIVERA_AUDIT_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_AUDIT_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_AUDIT_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUDIT_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_AUDIT_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_AUDIT_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_INFO {
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
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_INFO
    Modules[QCPEP_RSCSW_DRIVERA_SUPERVISOR_MODULE_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_SUPERVISOR_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_SUPERVISOR_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_SUPERVISOR_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_SUPERVISOR_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODULE_INFO {
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
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODULE_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODULE_INFO
    Modules[QCPEP_RSCSW_DRIVERA_AUTOTUNE_MODULE_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_AUTOTUNE_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_AUTOTUNE_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_AUTOTUNE_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_AUTOTUNE_HISTORY_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_MODULE_INFO {
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
} QCPEP_RSCSW_DRIVERA_HARDEN_MODULE_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_RESULT_INFO {
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
} QCPEP_RSCSW_DRIVERA_HARDEN_RESULT_INFO;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_SNAPSHOT_OUT {
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
    QCPEP_RSCSW_DRIVERA_HARDEN_MODULE_INFO Modules[QCPEP_RSCSW_DRIVERA_HARDEN_MODULE_INFO_CAPACITY];
} QCPEP_RSCSW_DRIVERA_HARDEN_SNAPSHOT_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_RUN_IN {
    ULONG ModeId;
    ULONG Flags;
    ULONG Cycles;
    ULONG Reserved0;
} QCPEP_RSCSW_DRIVERA_HARDEN_RUN_IN;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_RUN_OUT {
    LONG Status;
    QCPEP_RSCSW_DRIVERA_HARDEN_RESULT_INFO Result;
} QCPEP_RSCSW_DRIVERA_HARDEN_RUN_OUT;

typedef struct _QCPEP_RSCSW_DRIVERA_HARDEN_HISTORY_OUT {
    ULONG Capacity;
    ULONG Count;
    ULONG Dropped;
    ULONG EntrySize;
    QCPEP_RSCSW_DRIVERA_HARDEN_RESULT_INFO Entries[1];
} QCPEP_RSCSW_DRIVERA_HARDEN_HISTORY_OUT;

#define IOCTL_QCPEP_PPP_GET_RPMH_COMMAND_SET 0x80044FB4u
#define IOCTL_QCPEP_PPP_RESOURCE_VOTE 0x80048FA0u
#define IOCTL_QCPEP_PPP_GET_RESOURCE_SETTINGS 0x80048FA8u
#define IOCTL_QCPEP_PPP_RESOURCE_ACTION 0x80048FACu

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

#define IOCTL_QCPEP_THERMAL_PMIC_ALARM_QUERY 0x80350FA0u
#define IOCTL_QCPEP_THERMAL_PMIC_ALARM_STATE 0x80350FA4u

#define IOCTL_QCPEP_GPIO_CONFIG 0x80348FA0u
#define IOCTL_QCPEP_GPIO_REQUEST 0x80348FA4u
#define IOCTL_PM_GPIO_CONFIG_BIAS_VOLTAGE 0x80028FA0u
#define IOCTL_PM_GPIO_CONFIG_DIGITAL_INPUT 0x80028FA4u
#define IOCTL_QCPEP_GPIO_PMIC_SET_STATE 0x80028FA8u
#define IOCTL_QCPEP_GPIO_PMIC_SET_PULL 0x80028FACu
#define IOCTL_QCPEP_GPIO_PMIC_SET_DRIVE 0x80028FB0u
#define IOCTL_PM_GPIO_CONFIG_DIGITAL_OUTPUT IOCTL_QCPEP_GPIO_PMIC_SET_STATE
#define IOCTL_PM_GPIO_SET_INVERSION_CONFIGURATION IOCTL_QCPEP_GPIO_PMIC_SET_PULL
#define IOCTL_PM_GPIO_SET_EXT_PIN_CONFIG IOCTL_QCPEP_GPIO_PMIC_SET_DRIVE
#define IOCTL_PM_GPIO_GET_GPIO_STATE 0x80024FB4u
#define IOCTL_PM_GPIO_SET_GPIO_OUTPUT 0x80028FC0u

#define IOCTL_PMIC_PUBLIC_USB_OTG_CONTROL 0x801C0FA8u
#define IOCTL_PMIC_PUBLIC_USB_OTG_ENABLE 0x801C0FACu
#define IOCTL_PM_3P_PUBLIC_USB_OTG_ENABLE 0x80270FA0u
#define IOCTL_PM_WLED_MODULE_ENABLE 0x80100FA8u
#define IOCTL_PM_IBBLAB_MODULE_ENABLE 0x80200FA4u

#define IOCTL_QCPEP_DMF_DEVICEINTERFACE_SEND 0xC3502FA4u

#define IOCTL_QCPEP_SUBSYS_ADSP_STATE 0x9C414000u
#define IOCTL_QCPEP_SUBSYS_ADSP_CTRL 0x9C414004u
#define IOCTL_QCPEP_SUBSYS_CDSP_STATE 0x9C420400u

#define IOCTL_QCPEP_PMIC_HWINFO 0x80110FA4u
#define IOCTL_QCPEP_PMIC_CLX_EVENTCOUNT 0x80110FACu
#define IOCTL_QCPEP_BMS_SET_THRESHOLDS 0x802D0FA0u
#define IOCTL_QCPEP_BMS_QUERY 0x802D0FA4u

#define IOCTL_QCPEP_DEEPSTANDBY_MODEM_SET 0x8FF82004u
