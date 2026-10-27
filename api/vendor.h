#pragma once
#include "driver.h"

#define QCPEP_MAGIC 0x51504350u
#define QCPEP_VEND_VERSION 0x00010007u

#pragma pack(push, 1)

typedef struct _QCPEP_VENDOR_HEADER {
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
} QCPEP_VENDOR_HEADER;

typedef struct _QCPEP_VENDOR_CHIPINFO16 {
    ULONG ChipId;
    ULONG ChipFamily;
    ULONG ChipVersion;
    ULONG ChipSku;
} QCPEP_VENDOR_CHIPINFO16;

#pragma pack(pop)
