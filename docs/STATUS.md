# Feature status

[README](../README.md) · [Porting guide](PORTING.md) · [Contribution rules](../CONTRIBUTING.md)

Updated: **2026-10-29**. Scope: the tested SM8845/Molokai configuration running
Windows ARM64 build 26100. Other board, firmware and driver combinations require
their own validation.

## Validation baseline

The source baseline is revision 423,
[a287de4](https://github.com/NikkaGames/QualcommPEP/commit/a287de46260d8c82f938c96cd913a0f6da17fd74).
Its Debug and Release ARM64 packages build, and INF validation and focused
protocol/state-machine checks pass. Existing MSVC/WDK CRT header warnings remain.

The latest hardware session used revision 422 with a temporary correction that
enabled the real PEP0 firmware initialization call. That call returned success
and acknowledgement `1`. Revision 423 makes the handshake automatic, adds
function-support negotiation and requires acknowledgement before platform entry.
An unmodified revision 423 package boot has not yet been recorded.

The tables below summarize development observations. Raw KD traces and the
development test harness are not currently published in this checkout. Source
presence, a successful build and a runtime observation are different levels of
evidence.

## Working in hardware tests

| Feature | Observed behavior | Coverage limit |
| --- | --- | --- |
| Windows boot and desktop | Explorer ran with the compiled PEP installed. | Applies to the tested software and board configuration. |
| CPU registration and DVFS/DCVS | All eight processors registered; both performance domains changed frequency. Firmware readback reached 3.3216 GHz and 3.8016 GHz. | Sustained load, thermal limits and all firmware/SKU variants need separate validation. |
| CPU policy floors | The target policy uses 556.8 MHz for the six-core domain and 806.4 MHz for the two-core domain. Delivered-frequency feedback remains distinct from policy requests. | These are SM8845 policy values, not portable constants. |
| Per-core idle | Windows recorded C4 selections and successful idle accounting. Six processors were parked in one screen-off capture. | Parking is dynamic; no fixed seven-asleep/one-awake policy is imposed. |
| Cluster/platform LPI discovery | The real firmware handshake exposed both clusters and the platform state. Windows accepted three coordinated states. | The baseline requires the runtime correction described above. |
| Platform entry/completion callbacks | A post-wake capture recorded 224 accepted entry/completion pairs, no active session or retained I/O fence, and zero resume-validation failures. | Callback accounting does not measure physical DRIPS residency. |
| Screen off/on | The desktop returned after the screen-off test; touch and Bluetooth continued to work. | A single development session does not cover every wake source or long suspend interval. |
| SPI touch path | Touch initialized and responded at the desktop and after screen wake. | Other SPI controllers and transfer modes need their own tests. |
| Bluetooth UART path | Bluetooth initialized; discovery and operation were reported working in runtime tests. | Its D3 descriptor limitation remains listed below. |
| Selected I2C, clock, GPIO, rail and interconnect paths | Resource programs for enabled devices completed during boot and peripheral use. | This is evidence for exercised paths, not every entry in the resource catalogs. |
| UFS boot and normal I/O | Windows booted from UFS; the native required-client owner bound. | UFS low-power transitions remain incomplete. |
| WPP/IFR | The attached debugger decoded driver records using matching symbols. | This verifies tracing, not hardware behavior described by each event. |

## Known failures and incomplete support

| Feature | Status | Outstanding work |
| --- | --- | --- |
| UFS D3 | Known failure | The latest captured boot reported `\_SB.UFS0 D3 backend failed result=4`. Validate the full descriptor, ARC/required-client, clock and rail transition before claiming storage power-state support. |
| Bluetooth D3 | Descriptor limitation | The captured runtime descriptor omitted D3; the PEP reported the absent state. Working Bluetooth operation does not establish complete Bluetooth power management. |
| ADSP and CDSP startup | Last reported non-working | Both were reported with Code 10 / `STATUS_NOT_IMPLEMENTED`. No later successful startup test is recorded. The cause must be established across resource admission, companion drivers and firmware. |
| Explicit RPMh sleep/wake command images | No validated runtime owner | Active transactions and retained firmware images do not establish ownership of new sleep/wake sets. The publication and teardown path requires separate proof. |
| Nonempty preload resource programs | Provider-dependent | Descriptions can be retained; execution requires a verified preload provider and correct INIT/PRELOAD/ENTRY/EXIT semantics. |
| Bandwidth-monitor bus DCVS | Disabled | The current INF disables bandwidth-monitor bus DCVS and LAGG monitoring. CPU frequency scaling does not establish autonomous interconnect scaling. |

## Not yet established

| Area | Missing evidence |
| --- | --- |
| Physical deep sleep / DRIPS | Independent firmware or hardware residency counters and power measurements correlated with Windows entry records. |
| Low-power robustness | Repeated long idle intervals, every supported wake source, interrupted entry, hibernate/resume and resource loss during transitions. |
| Complete device D/F/P coverage | Every declared D0-D3 state, component F state and sparse multi-set performance selector on hardware. |
| Interconnect and clocks | Every route, declared minimum, shared vote, clock domain and rate transition under simultaneous client activity. |
| GPU/display power and performance | Hardware validation of the active GPU/display resource consumers and any populated `G0MD` descriptions. A working desktop is insufficient evidence. |
| PCIe | Validation of the platform's `PEMC` descriptions, consumers, links and power transitions. |
| Thermal, current and power policies | Sensor accuracy, trips, mitigation, recovery, sustained load and interaction with firmware limits. |
| Other peripherals | Device-specific startup, function, concurrency and power-state results for every advertised client. |
| Teardown and crashdump | Complete provider-loss, detach, restart, failure recovery and crashdump tests with ownership retained until safe retirement. |
| Performance overhead | Repeatable CPU, interrupt/DPC, storage, responsiveness and power comparisons against a working baseline. |
| Other SoCs | All reserved branches are empty and have no implementation or validation claim. |

## Interpreting the results

Busy platform-entry refusals are expected while a resource transaction is active.
Windows may send a completion after a declined entry; it must leave resource
ownership unchanged. Ambiguous hardware completion is a different condition and
must retain the owner and close further admission until resolved.

Breaking into KD wakes processors. A stopped CPU snapshot cannot prove that the
CPU remained powered down during the preceding interval. Windows idle records,
firmware counters and external power measurements must be correlated when
validating deep sleep.

For a new port, all required features must pass the
[validation matrix](PORTING.md#required-validation). Unverified, disabled or
stubbed features do not count as working support.
