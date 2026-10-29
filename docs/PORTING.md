# Porting QualcommPEP

Ports are maintained in separate SoC branches. The available branches are `sm8250`, `sm8450`, `sm8475`, `sm8650`, `sm8750`, `sm8845`, `sm8850` and `sm8975`. Pull requests must target the existing branch for the port. Other targets need a branch created by a maintainer first.

The `sm8845` branch contains the current implementation. The reserved branches are empty; bring across the shared source needed for the new port and keep SoC changes separate from general cleanup.

## Source layout

- `Framework/PEP/src/` and `inc/`: Windows callbacks, ACPI, CPU performance and idle support.
- `Framework/PEP/8845/`: SM8845 resource descriptions, hardware access and state handling.
- `SystemLibs/`: clock, ICB, RPMh, SCMI, PMIC, GPIO, command DB and thermal support. Target-specific code lives in the SoC subdirectories.
- `api/`: interfaces shared with other drivers.
- `inf/` and `qcpep8845/`: installation data and WDK project files.

Use the new SoC's name for its target directories and update the project files and includes. Keep addresses, clock names, route tables and firmware details in target code so shared code remains usable by other ports.

## ACPI and device power

Start with the target's DSDT and SSDTs. Match the PEP device, processor identities, resources and firmware initialization calls to the driver and INF. Companion drivers may also submit resource descriptions at runtime.

Review the PEP node's objects and their consumers, including `STND`, `PREL`, `G0MD` for GPU resources and `PEMC` for PCIe where present. Their contents and calling conventions depend on the platform.

`PREL` is an optional list of startup resource programs. It is absent from the current SM8845 tables. The driver validates and retains nonempty preload descriptions but does not execute them; a port that supplies them needs a startup owner separate from device D-state handling.

Device handling should follow the ACPI descriptions. Preserve declared D0-D3 states, component F states and performance sets. New devices should use the same resource handling as existing devices without adding name-based exceptions. UFS must follow these rules as well.

## Hardware data

The SM8845 tables need to be replaced or checked against the target hardware:

- **Clocks:** controller addresses, parents, rate tables, dividers, branch votes and power domains. Include GPU and display controllers where required.
- **Interconnect:** ICB routes, BCM groups, bus widths, units, command DB data and minimum bandwidth votes. Shared votes must survive individual devices entering idle.
- **Bus DCVS:** bandwidth-monitor register revisions, MMIO windows, PnP interrupts, timer rates, counter units, bus limits and LLCC-to-DDR rate mappings. The LAGG counter layout and CPU index mapping are target-specific. Keep these in `bwmon_target.c`; do not reuse another SoC's register assignments.
- **RPMh:** RSC version, DRV assignment, TCS groups, command slots and interrupt routing. Active, sleep and wake command sets need the correct allocation and completion handling.
- **Regulators and GPIO:** PMIC model, resource names, voltage and mode settings, TLMM tiles, pin functions and sequencing.
- **Storage and power domains:** UFS dependencies, ARC levels, required clients, clocks and rails.
- **Thermal:** sensors, limits and any firmware-controlled mitigation.

Vendor and Linux sources can help identify these details. Retain their attribution and license notices when importing code.

## CPU performance and sleep

Match processor affinities to the ACPI topology and firmware performance domains. Update the SCMI transport, supported frequency ranges and board-specific minimums. Check frequency scaling on every CPU group, including after resume.

Idle support depends on the firmware's ACPI LPI descriptions, initialization calls and wake routing. Resource transactions must finish before platform entry, and their state must be restored on wake. Idle callbacks must remain resident and nonblocking.

RPMh bank assignments are defined in `SystemLibs/RPMh/<SoC>/src/sleep_target.c`. Keep active, sleep, wake and fast-path slots separate. The sleep programmer preserves inherited commands and never triggers a sleep or wake bank from software; firmware runs those banks during platform entry and exit. A port must confirm that firmware uses the selected banks.

Interconnect requests remain required during sleep unless their producer explicitly marks them active-only. Bandwidth monitor votes use that policy; ACPI device votes retain their declared requirements. Rails without a separate sleep request retain their active setting. Command sets that exceed the assigned bank capacity prevent platform entry rather than using reserved slots.

Use the target's Windows and firmware interfaces. Preserve API layouts and calling conventions, shared-resource ownership and failure handling when adapting the code.

## Submitting a port

A port submission needs:

- The device model, SoC, Windows build and firmware version.
- A photo or video of the device running Windows with the port installed.
- A short list of working features, known issues and anything not yet tested.
- Build or installation instructions if they differ from the README.

Basic testing should cover boot, restart, storage, input and CPU frequency scaling. Check sleep/wake and the peripherals enabled by the port, and list any failures. The submitted source should contain the fixes used during testing.

Keep the port's [status page](STATUS.md) current and follow the [contribution rules](../CONTRIBUTING.md).
