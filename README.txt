qcpep: SM8845 Molokai Windows ARM64 PEP

Open QcPep8845.sln with Visual Studio 2022 and WDK 26100.
Build Debug or Release for ARM64, or run build_arm64.cmd Release
from a Visual Studio developer prompt.
Package: ARM64\<configuration>\QcPep8845\qcpep.sys, qcpep.inf, qcpep.cat.
Symbols: ARM64\<configuration>\qcpep.pdb. Intermediates: build\obj.
The WDK handles INF stamping, catalog creation and configured test signing.

Maintenance docs, symbol maps, generators and verification tools:
../qcpep8845_development/README.md

Tracing: WPP and Inflight Trace Recorder in both configurations.
Current image marker: rev=422 acpi-low-power-idle

CPU performance control is enabled: Parameters\CpuPerformanceEnable (DWORD), default 1.
It discovers SCMI performance levels and fast channels, then checks the CCGP CPU
identities against physical processor affinity. No fixed frequency table is used.
The platform policy minimums are 556.8 MHz and 806.4 MHz. The maximum and sustained
frequencies come from firmware. Windows requests use supported states within
the published range; feedback reports the delivered frequency in MHz.
CPU registration, frequency scaling and peripheral operation were verified with
kernel-debug corrections. Verify the rebuilt package on its first boot.
Install the INF package to provision CPU topology; copying the SYS is insufficient.
Set CpuPerformanceEnable to 0 and reboot to disable CPU performance control.
The CPU provider leaves voltage sequencing and thermal limits to PDP firmware.

Native ACPI low-power idle is enabled: Parameters\CpuIdleEnable (DWORD), default 1.
Windows uses firmware _LPI entry methods, dependencies and architectural context
flags for processor, cluster and platform suspend. The PEP validates and retains
the dependency descriptions without replacing their PSCI state IDs or CPU policy.
Platform entry requires empty native RPMh queues and no outstanding resource or
frequency operation. A busy transition is declined without waiting at idle IRQL.
Wake releases the same resource registration and reconciles the next CPU request.
Required resource votes and firmware sleep/wake TCS images remain intact. This
implementation does not synthesize sleep votes or power off clocks with active
clients. Explicit RPMh sleep/wake sets require a separate image owner.
Callback timing is diagnostic data, not a hardware DRIPS residency counter.
Suspend, wake sources and hardware residency need validation on the device.
Set CpuIdleEnable to 0 and reboot to disable these PEP LPI callbacks.

ACPI descriptors are discovered through STND and optional PREL, or submitted by
client drivers through the runtime resource interface. Unlisted namespace methods
are not probed. Nonempty preload descriptions are retained separately; execution
requires a verified preload provider. Empty packages require no resource actions.
