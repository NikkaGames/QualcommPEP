# QualcommPEP

A Windows ARM64 platform extension plug-in (PEP) for Qualcomm mobile SoCs. It provides CPU frequency control, low-power idle and device power management through ACPI, firmware and the platform's clock, interconnect, regulator and GPIO controllers.

The current implementation targets **SM8845 (Molokai)**. Development is ongoing; see [feature status](docs/STATUS.md) for working features and known issues.

[Porting guide](docs/PORTING.md) | [Contributing](CONTRIBUTING.md)

## SoC branches

[`sm8845`](https://github.com/NikkaGames/QualcommPEP/tree/sm8845) contains the active implementation. These branches are reserved for future ports and are currently empty:

- [sm8250](https://github.com/NikkaGames/QualcommPEP/tree/sm8250)
- [sm8450](https://github.com/NikkaGames/QualcommPEP/tree/sm8450)
- [sm8475](https://github.com/NikkaGames/QualcommPEP/tree/sm8475)
- [sm8650](https://github.com/NikkaGames/QualcommPEP/tree/sm8650)
- [sm8750](https://github.com/NikkaGames/QualcommPEP/tree/sm8750)
- [sm8850](https://github.com/NikkaGames/QualcommPEP/tree/sm8850)
- [sm8975](https://github.com/NikkaGames/QualcommPEP/tree/sm8975)

Port submissions go to the existing branch for that SoC. Include a photo or video of the device running Windows, basic testing notes and any known issues. See the [porting guide](docs/PORTING.md#submitting-a-port) for details.

## Building

Requires Visual Studio 2022 with ARM64 C++ tools, Windows SDK 26100 and WDK 26100.

Open [QcPep8845.sln](QcPep8845.sln) and select ARM64, or run this from a Visual Studio developer command prompt:

```bat
build_arm64.cmd Release
```

Use `Debug` for a debug build. The driver package is placed in `ARM64\Release\QcPep8845\` as `qcpep.sys`, `qcpep.inf` and `qcpep.cat`.

Signing settings are in [Directory.Build.props](Directory.Build.props). A signing certificate must be supplied separately. Installation requires matching ACPI tables, firmware and companion drivers. Install the INF together with the SYS and CAT files; it also supplies CPU topology and resource settings.

## Configuration

CPU performance control and low-power idle are enabled by default. The following DWORD values under `HKLM\SYSTEM\CurrentControlSet\Services\qcpep_8480\Parameters` can disable them:

- `CpuPerformanceEnable`: `1` enabled, `0` disabled.
- `CpuIdleEnable`: `1` enabled, `0` disabled.

Changes take effect after a restart. `qcpep_8480` is the existing service name retained by the INF.

## Tracing

The driver uses WPP and Inflight Trace Recorder (IFR). Load the PDB matching the installed driver in WinDbg:

```text
.sympath+ C:\src\QualcommPEP\ARM64\Release
.reload /f qcpep.sys
!wdfkd.wdflogdump qcpep -d
```

Adjust the symbol path to the build directory. Trace definitions are in [trace.h](Framework/PEP/inc/trace.h).

## License

[MIT](LICENSE). Copyright (c) 2026 Nika Okromtchedlishvili <okromtchedlishvilinika@gmail.com>.

Third-party materials retain their own licenses and attribution.
