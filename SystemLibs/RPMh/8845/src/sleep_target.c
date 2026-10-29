#include "sleep.h"
#include "rpmh_target.h"

/* DRV2 channel 0: three ACTIVE, two SLEEP, two WAKE, one reserved fast-path TCS. */
const rpmh_sleep_layout_t rpmh_sleep_target = {
    MOLOKAI_WIN_TCS_TOTAL, MOLOKAI_WIN_AMC_MASK,   MOLOKAI_WIN_SLEEP_START,
    MOLOKAI_WIN_SLEEP_CNT, MOLOKAI_WIN_WAKE_START, MOLOKAI_WIN_WAKE_CNT,
    TCS_BATCH_MAX};
