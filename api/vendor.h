#pragma once
#include "driver.h"

#define VENDOR_PAYLOAD_MAGIC 0x51504350u
#define VENDOR_PAYLOAD_VERSION 0x00010007u

#pragma pack(push, 1)

typedef struct vendor_payload_header {
    ULONG Magic;
    ULONG Version;
    ULONG TotalSize;
    ULONG Flags;

    ULONG PortId;
    ULONG Reserved0;
    ULONG ProcessorCount;
    ULONG ActiveGroupCount;

    ULONGLONG QpcFreq;
    ULONGLONG InterruptTime;
    ULONGLONG SystemTime;
    ULONGLONG TickCount;

    ULONG Reg_BranchTrackEnabled;
    ULONG Reg_EnableDirectedPStateFeatures;
    ULONG Reg_EnableParkSelection;
    ULONG Reg_EnablePEPOCDTest;
    ULONG Reg_EnablePEPOCDTestLowPerf;
    ULONG Reg_PepBcl_Notification_Level;

    WCHAR Hid[64];
} vendor_payload_header_t;

typedef struct vendor_chip_info {
    ULONG ChipId;
    ULONG ChipFamily;
    ULONG ChipVersion;
    ULONG ChipSku;
} vendor_chip_info_t;

#pragma pack(pop)
