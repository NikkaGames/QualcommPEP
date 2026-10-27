#pragma once
#include <ntddk.h>
#include <evntrace.h>

#define WPP_CONTROL_GUIDS                                                                          \
    WPP_DEFINE_CONTROL_GUID(                                                                       \
        trace_provider, (8d43706c, 132e, 4c46, 9348, b84cbb524bfe),                                \
        WPP_DEFINE_BIT(TRACE_DRIVER) WPP_DEFINE_BIT(TRACE_DEVICE) WPP_DEFINE_BIT(TRACE_POWER)      \
            WPP_DEFINE_BIT(TRACE_RESOURCE) WPP_DEFINE_BIT(TRACE_RPMH) WPP_DEFINE_BIT(TRACE_CLOCK)  \
                WPP_DEFINE_BIT(TRACE_ACPI) WPP_DEFINE_BIT(TRACE_PPM) WPP_DEFINE_BIT(TRACE_IO))

#define WPP_LEVEL_FLAGS_LOGGER(level, flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(level, flags)                                                      \
    (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_##flags).Level >= (level))

#if ENABLE_WPP_RECORDER
#define trace_verbose_enabled(flags)                                                               \
    ((WPP_CHECK_INIT WPP_LEVEL_FLAGS_ENABLED(TRACE_LEVEL_VERBOSE, flags)) ||                       \
     (WPP_RECORDER_CHECK_INIT WPP_RECORDER_LEVEL_FLAGS_FILTER(TRACE_LEVEL_VERBOSE, flags)))
#else
#define trace_verbose_enabled(flags)                                                               \
    (WPP_CHECK_INIT WPP_LEVEL_FLAGS_ENABLED(TRACE_LEVEL_VERBOSE, flags))
#endif

// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
