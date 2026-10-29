# QualcommPEP

A Windows ARM64 platform extension plug-in (PEP) for Qualcomm mobile SoCs.
The `sm8845` branch targets SM8845 (Molokai).

The driver connects Windows PoFx power management to ACPI resource descriptions,
firmware CPU performance control, and clock, interconnect, regulator and GPIO
providers. Matching ACPI tables, firmware and peripheral drivers are part of a
working platform.

**Development status:** selected paths have been tested on SM8845 hardware.
Several features remain incomplete or unverified. See the
[feature status](docs/STATUS.md) before installing or starting a port.

- [Feature status and known issues](docs/STATUS.md)
- [Porting to another SoC](docs/PORTING.md)
- [Contribution and code rules](CONTRIBUTING.md)

## Branches

Each SoC has a separate branch. Reserved branches contain an empty initial commit
and no driver source. A reserved branch does not indicate hardware support.

| SoC | Branch | State |
| --- | --- | --- |
| SM8845 | [sm8845](https://github.com/NikkaGames/QualcommPEP/tree/sm8845) | Active development; see feature status |
| SM8250 | [sm8250](https://github.com/NikkaGames/QualcommPEP/tree/sm8250) | Reserved, empty |
| SM8450 | [sm8450](https://github.com/NikkaGames/QualcommPEP/tree/sm8450) | Reserved, empty |
| SM8475 | [sm8475](https://github.com/NikkaGames/QualcommPEP/tree/sm8475) | Reserved, empty |
| SM8650 | [sm8650](https://github.com/NikkaGames/QualcommPEP/tree/sm8650) | Reserved, empty |
| SM8750 | [sm8750](https://github.com/NikkaGames/QualcommPEP/tree/sm8750) | Reserved, empty |
| SM8975 | [sm8975](https://github.com/NikkaGames/QualcommPEP/tree/sm8975) | Reserved, empty |

Ports are accepted only into their existing SoC branch, with complete hardware
validation and compliance with the project's structure and code rules. Partial
ports, unreviewed AI-generated code and unsupported claims of working hardware
are not accepted. The [porting guide](docs/PORTING.md) defines the required proof.

## Build

The tested toolchain is Visual Studio 2022 with ARM64 C++ tools, Windows SDK 26100
and WDK 26100. The project uses the `WindowsKernelModeDriver10.0` toolset.

Open [QcPep8845.sln](QcPep8845.sln), select **ARM64** and **Debug** or **Release**,
then build the solution. From a Visual Studio developer command prompt:

```bat
build_arm64.cmd Release
```

Use `Debug` for a debug build. Omitting the argument also selects `Debug`.
The wrapper invokes MSBuild; it does not install the toolchain.

| Output | Location |
| --- | --- |
| Driver package | `ARM64\<configuration>\QcPep8845\qcpep.sys`, `qcpep.inf`, `qcpep.cat` |
| Symbols | `ARM64\<configuration>\qcpep.pdb` |
| Intermediates and generated WPP files | `build\obj\ARM64\<configuration>\` |

The WDK stamps the INF and generates the catalog. Signing is controlled by
`SignMode` and `TestCertificate` in [Directory.Build.props](Directory.Build.props).
No signing key is included. Supply a test certificate explicitly, or build an
unsigned package:

```bat
msbuild QcPep8845.sln /m /p:Configuration=Release /p:Platform=ARM64 /p:SignMode=Off
```

Install the INF package on a matching Windows ARM64 target with the required
signing policy. The INF provisions CPU topology and resource data; copying only
the SYS is insufficient for an initial installation. Keep a recoverable boot
image and the previous working package when testing changes.

## Configuration

The following DWORD values are read from the driver's `Parameters` key. Changes
take effect after reboot.

| Value | Default | Purpose |
| --- | --- | --- |
| `CpuPerformanceEnable` | `1` | Firmware-backed CPU performance control and feedback |
| `CpuIdleEnable` | `1` | Native ACPI LPI callbacks and the firmware initialization handshake |

Setting either value to `0` disables that feature. The former
`MolokaiAcpiInitDsmEnable` switch is no longer consulted.

The image and package use the `qcpep` name. The INF retains the `qcpep_8480`
service identifier for compatibility. Service identity, device IDs and peer
interfaces require explicit migration when changed.

## Tracing

Diagnostics use WPP and Inflight Trace Recorder (IFR) in both build configurations.
Use the PDB from the exact driver build when decoding traces. For a build under
`C:\src\QualcommPEP`, an attached WinDbg session can read the recorder with:

```text
.sympath+ C:\src\QualcommPEP\ARM64\Release
.reload /f qcpep.sys
!wdfkd.wdflogdump qcpep -d
```

The WPP control GUID is `8d43706c-132e-4c46-9348-b84cbb524bfe`, defined in
[trace.h](Framework/PEP/inc/trace.h). See Microsoft's
[IFR documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/using-wpp-recorder)
for recorder configuration. Runtime traces belong in test artifacts or issue
attachments, outside production source directories.

## Source layout

| Path | Responsibility |
| --- | --- |
| `api/` | Shared structures and client-facing interfaces |
| `Framework/DeviceLib/` | Common Windows device and I/O support |
| `Framework/PEP/src/`, `inc/` | WDF/PoFx callbacks, ACPI, processor power and driver lifecycle |
| `Framework/PEP/8845/` | SM8845 resource graph, state owners, hardware adapters and transport |
| `SystemLibs/` | ARC, clock, command DB, DAL, ICB, NPA, PMIC, RPMh, SCMI, TLMM and thermal modules |
| `SystemLibs/*/8845/` | Target-specific tables and implementations |
| `inf/`, `qcpep8845/` | Package input and WDK project |
| `docs/` | Feature status and porting requirements |

The [porting guide](docs/PORTING.md) maps the hardware-dependent areas that need
review on another SoC. The [contribution rules](CONTRIBUTING.md) apply to every
first-party directory, including `api/` and `SystemLibs/`.

## License and authorship

Project-authored code and documentation are licensed under the [MIT License](LICENSE).
External firmware, vendor drivers and separately licensed material retain their
own terms and attribution.

Copyright (c) 2026 Nika Okromtchedlishvili
<okromtchedlishvilinika@gmail.com>.
