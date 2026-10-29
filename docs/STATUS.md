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
| Peripheral resources | Clock, GPIO, regulator and interconnect control work for the devices tested so far. |
| Tracing | WPP and IFR are available in Debug and Release builds. |

## Known issues

- **ADSP and CDSP:** last reported with Device Manager Code 10 (`STATUS_NOT_IMPLEMENTED`).
- **UFS power management:** D3 transitions can fail.
- **Bluetooth power management:** the current runtime description lacks D3.
- **Bus DCVS:** bandwidth-monitor DCVS and LAGG monitoring are disabled.
- **RPMh sleep/wake sets:** explicit command-set handling remains incomplete.
- **Preload resources:** nonempty resource programs still depend on provider support.

## Needs testing

Cluster and platform idle support is implemented, but full deep sleep/DRIPS residency and power consumption are unconfirmed. Long sleep/wake cycles, additional wake sources and hibernate need testing.

GPU/display power management, PCIe, thermal limits and the remaining peripherals also need testing. Support for every device power state and performance setting is still being worked through. Performance and power tuning are ongoing.
