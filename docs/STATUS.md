# SM8845 status

Support on SM8845 (Molokai), using Windows ARM64 build 26100. Device support also depends on the installed firmware, ACPI tables and companion drivers. Other SoC branches are currently empty.

## Working

| Feature | Notes |
| --- | --- |
| Windows boot and desktop | Boots from UFS and runs the desktop. |
| CPU frequency scaling | All eight CPUs register, and both CPU groups change frequency. |
| CPU idle | Per-core idle and core parking work. |
| Screen off/on | Basic wake works with touch and Bluetooth. |
| Touch | SPI touch works at the desktop and after wake. |
| Bluetooth | UART initialization and device discovery work. |
| UFS storage | Boot and normal disk access work. |
| Subsystem startup | ADSP and CDSP parent devices start. |
| Peripheral resources | Clock, GPIO, regulator and interconnect control work for the devices tested so far. |
| Tracing | WPP and IFR are available in Debug and Release builds. |

## Needs testing

Cluster and platform idle support is implemented, but full deep sleep/DRIPS residency and power consumption are unconfirmed. Long sleep/wake cycles, additional wake sources and hibernate need testing.

GPU/display power management, PCIe, thermal limits and the remaining peripherals also need testing. Support for every device power state and performance setting is still being worked through. Performance and power tuning are ongoing.

Bus DCVS uses interrupt-driven LLCC and DDR bandwidth monitors, with LAGG request counters for CPU activity. Hardware testing is needed for bandwidth scaling, idle power and resume.

RPMh sleep and wake commands are staged before platform idle. Active-only bandwidth votes are removed for sleep; required client votes are retained. Hardware testing is needed for firmware execution of the command sets and restoration on wake.

ADSP audio and CDSP compute operation need testing beyond subsystem startup.
