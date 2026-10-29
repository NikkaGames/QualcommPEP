# Porting QualcommPEP

[README](../README.md) · [Feature status](STATUS.md) · [Contribution rules](../CONTRIBUTING.md)

A port must establish the hardware and firmware contracts for its target. SoC
names, similar register layouts and a successful boot do not establish those
contracts. Linux and vendor sources are useful references for hardware facts;
Windows ownership, PnP resources, WDF interrupts and PoFx completion still need
their own implementation and validation.

## Branch policy

Ports are accepted only into an existing branch assigned to that SoC:
`sm8250`, `sm8450`, `sm8475`, `sm8650`, `sm8750`, `sm8845` or `sm8975`.
Requests for another target require a maintainer-created branch before submission.

The reserved branches start with an empty root commit and contain no SM8845
source. Create the port's development branch from its reserved branch, import
the required shared implementation, and keep platform adaptation commits
separate from mechanical imports and refactors. Direct the pull request to that
SoC branch.

Acceptance requires a complete implementation, full proof that every required
feature works on the target, and compliance with the project's code and layout
rules. Partial bring-up submissions and ports that depend on permanent debugger
patches are not accepted as working ports. Hardware that genuinely lacks a
feature must have that absence documented; disabling a feature does not satisfy
the requirement.

## Runtime structure

The main resource path is:

```text
WDF / PoFx / peer driver
    -> descriptor and identity validation
    -> retained device or component owner
    -> serialized state transaction
    -> shared resource aggregation
    -> physical provider and completion/readback
    -> verified state publication
```

Parsing a descriptor does not grant hardware ownership. Each provider needs a
valid resource assignment, owner lifetime, generation and teardown path. Keep
the full transaction protected across snapshot, apply, verification and rollback.

Use the target WDK's public PEP/WDF definitions and calling conventions. Preserve
structure sizes, alignment, flags and register widths. Private copies of kernel
structures or offsets from another Windows build are unsuitable ABI definitions.

## Where platform changes belong

The `8845` directories are the current target implementation. A new SoC branch
must use an appropriate target directory and repair every project/include/filter
reference. Keep reusable mechanisms in shared modules and hardware facts in
target data or adapters.

| Area | Current implementation to inspect | Porting responsibility |
| --- | --- | --- |
| Windows integration | `Framework/PEP/src/driver.c`, `device.c`, `main.c`; `Framework/DeviceLib/` | WDF/PoFx registration, PnP ordering, callback context, lifetime and failure handling |
| ACPI and descriptors | `Framework/PEP/src/acpi/`; `Framework/PEP/8845/src/aeob_*`, `runtime_*`, `res_graph*` | Namespace, payload layout, identity, descriptors, ownership and publication |
| Physical providers | `Framework/PEP/8845/src/hw_win.c`, `mmio_win.c`, `init_hw.c`, `hw_backend*` | Resource assignments, mapping rights, native handles, grants and teardown |
| CPU performance | `Framework/PEP/src/cpu_device.c`, `dcvs.c`, `ppm.c`; `Framework/PEP/8845/src/soc_model.c`, `soc_transport.c`; `SystemLibs/SCMI/` | Affinity, firmware domains, protocol transport, policy and delivered feedback |
| Idle | `Framework/PEP/src/idle/lpi.c`; ACPI evaluation and device initialization | Firmware handshake, native LPI metadata, dependency coverage, admission and wake |
| Clocks and domains | `SystemLibs/clock/8845/` | Controller layout, roots, rates, parents, dividers, votes, DFS and GDSC behavior |
| Interconnect | `SystemLibs/ICB/8845/`; target graph/NPA adapters | Routes, BCM topology, command DB metadata, aggregation, units and minimum votes |
| RPMh and interrupts | `SystemLibs/RPMh/8845/`; Windows RSC/IRQ adapters under `Framework/PEP/8845/` | DRV/TCS layout, completion, interrupts, command ownership and sleep/wake images |
| Regulators and GPIO | `SystemLibs/PPP/8845/`, `SystemLibs/TLMM/8845/`, `SystemLibs/ARC/`, `SystemLibs/CmdDb/` | PMIC revision, rail/GPIO identities, transport, dependency ordering and readback |
| Storage and other owners | `Framework/PEP/8845/src/ufs/`, `footswitch/`, state/component/performance adapters | Descriptor-driven state ownership and required-client lifecycle |
| Thermal and limits | `SystemLibs/Tsens/`; PEP thermal, current-limit and power-control modules | Sensor transport, trips, mitigation, recovery and firmware coordination |
| Packaging and ABI | `api/`, `inf/qcpep.inx`, `qcpep8845/`, build properties | Peer interfaces, structure layouts, hardware IDs, registry data, signing and symbols |

An audit must also search shared code for target constants. Moving a file into a
shared directory does not make its addresses, masks or assumptions portable.

## Platform inventory

Record the exact SoC revision, board/SKU, boot firmware, ACPI table hashes,
Windows build and companion driver versions. Keep the original working package,
recovery procedure and a kernel-debug connection available during bring-up.

For each hardware fact, record its source and whether it is documented,
recovered, inferred or observed on the target. Retain input provenance and the
regeneration procedure for generated tables. A binary symbol name or a matching
function signature is insufficient evidence of equivalent behavior.

## ACPI, topology and peer drivers

Inventory the PEP device, processor containers, processor identities, dependency
objects, interrupt/memory resources and firmware initialization methods. Check
both DSDT and SSDTs. The INF's hardware IDs and CPU/resource registry payloads
must match that namespace.

Review `STND`, optional `PREL`, runtime resource submissions and their actual
consumers. Preserve INIT, PRELOAD, ENTRY and EXIT distinctions. A package being
present, empty or absent has a different meaning from an executed resource vote.

Account for other PEP-node objects, including populated `G0MD` GPU/display
descriptions and `PEMC` PCIe packages. Establish their caller, arguments, lifetime
and state transition before executing them. Preserve the distinction between a
named package and an AML method. Unlisted objects require an explicit consumer
contract; a broad namespace scan is not an execution policy.

Keep exact ACPI identities and validated descriptor generations. Do not add
device-name allowlists or special success paths for individual SPI, I2C, UART,
Bluetooth or storage devices. New ACPI clients must use the same descriptor and
provider rules as existing clients.

## D, F and P states

Preserve all declared D0, D1, D2 and D3 states. An empty declared state is valid;
an undeclared state must not be fabricated. Validate each component's declared
F0-F31 membership and its real initial state.

Performance selectors can be sparse and can span several sets. Keep descriptor
selectors separate from PoFx ordinals, and serialize D-state, F-state,
performance and clock operations that share an owner. Test partial application,
readback failure, rollback failure, generation changes and concurrent requests.

UFS uses the same ownership and transaction rules. Its rail, clock, interconnect,
ARC and required-client dependencies must be validated through the real storage
path. A successful filesystem boot does not establish D3 or performance-state
correctness.

## Clocks, interconnect, rails and pins

Rebuild the clock description from the target's controller layout and firmware.
Validate register width, parent selection, frequency tables, dividers, branch
halt semantics, shared votes, rate rounding and DFS ownership. GCC, display,
GPU and other controllers require their own owners. Distinguish a controller
root clock from the bus transfer rate selected by a peripheral driver.

For ICB/BCM, validate the complete route graph, bus widths, units, VCD grouping,
enable/keepalive masks, average/peak conversion, aggregation and command order.
Resolve resource addresses and metadata from the target command DB where the
protocol provides them. Preserve each route's declared minimum and votes held
by other clients when a device withdraws its request.

Validate PMIC models, slave/resource identities, rail aliases, voltage/mode
units, dependencies and SPMI/other transport ownership. Review TLMM tile and
pin mappings, mux/function, direction, bias, drive strength and output ordering.
Every write needs a valid owner and meaningful completion/readback.

## RPMh, TCS and wake routing

Determine the target RSC version, DRV assignment, register layout, available TCS
groups, command slots and firmware-reserved resources. The SM8845 implementation
uses HLOS DRV2, three AMC slots and raw GIC INTID 37. These are target facts;
they must not be copied to another SoC without proof.

Preserve the Windows raw-to-translated interrupt correlation. Establish the WDF
interrupt owner and physical command-completion path. Verify every accepted
request, dependency/wait ordering, busy arbitration, exact acknowledgement,
interrupt restoration and owner retirement.

An ambiguous submitted command keeps its owner, ticket and resources until
completion is resolved. Do not invent an acknowledgement, replay votes blindly,
clear a quarantine flag or unmap a controller still in use.

Active, sleep and wake command sets have separate ownership requirements.
Preserving firmware sleep/wake images does not authorize replacing them. A new
image owner needs verified group allocation, programming, handoff, completion
and teardown. Also validate PDC/GIC wake routing and context restoration.

## CPU performance and low-power idle

Match physical processor affinities to ACPI/INF identities and firmware
performance domains. Discover supported levels and fast channels, validate
protocol versions and bounds, and honor firmware rate limits, alignment,
mailbox ownership and doorbell semantics. Keep target policy floors in target
data. Firmware owns voltage sequencing and thermal enforcement.

Publish legal Windows performance ranges and report delivered frequency
independently of requested frequency. Validate each domain at idle, under load,
under firmware limits and after resume. The SM8845 frequency policy is not a
fallback table for another processor.

Use the firmware's LPI entry methods, dependencies and architectural context
flags. Complete any required firmware initialization before Windows enumerates
the container states. On SM8845, PEP0's supported `_DSM` handshake exposes the
cluster and platform descriptions guarded by `PEPI`.

Windows executes the native ACPI/PSCI idle path. The PEP fences outstanding
resource/frequency work, checks quiescence and reopens the same verified owner
after wake. Idle callbacks must remain resident and nonblocking. Avoid waits,
allocations, registry reads and verbose tracing in those paths. Do not force a
fixed number of cores awake or treat a clock-frequency reading as proof of
physical sleep.

## Required validation

Every applicable row requires results for the submitted source and final
installable binary. Unsupported or untested functionality blocks acceptance as
a complete port.

| Area | Required proof |
| --- | --- |
| Build/package | Debug and Release ARM64 build logs, INF/catalog validation, exact toolchain, imports/exports, package hashes, matching symbols and reproducible build steps |
| Startup | Repeated cold boots and restarts, desktop availability, clean device-start results and recovery from failed initialization |
| Device states | Every declared D0-D3 state, repeated/duplicate notifications, failure propagation and correct empty-state handling |
| Component/performance states | Every declared F state, each performance set, sparse selector/ordinal mapping and verified initial state |
| Shared resources | Simultaneous users, minimum votes, aggregate readback, one-client withdrawal and no premature clock/rail shutdown |
| RPMh | Real submitted-command completion, IRQ handling, concurrency, timeout/ambiguity behavior, restart and safe owner retention |
| CPU control | Every processor/domain registered, legal minimum/maximum/sustained rates, delivered feedback, load/idle changes and resume recovery |
| Sleep | Per-core, cluster and platform entry, independent hardware/firmware residency evidence, power measurements, every supported wake source and repeated long sleep/wake cycles |
| Peripherals | Functional and power-transition tests for storage, SPI/I2C/UART clients, touch, Bluetooth/Wi-Fi, display/GPU, PCIe, ADSP/CDSP and every other advertised device |
| Thermal/power limits | Sensor accuracy, sustained load, mitigation, enforced limits and recovery |
| Lifetime/failure handling | Partial registration, provider loss, detach, asynchronous cancellation, D0 exit, teardown, quarantine, crashdump and rollback |
| Performance | Responsiveness, CPU/interrupt/DPC load, storage throughput/latency and power compared with a working baseline |

The evidence package must identify the source commit, binary/PDB hashes,
hardware and firmware versions, test procedure, expected/observed results and
repeat count. Attach raw traces and readbacks with durable links. Screenshots
and a statement that the desktop boots cannot replace this matrix.

Runtime patches are useful during investigation. Acceptance requires the final
package to pass the same tests without debugger patches, forced return values,
disabled validation or hidden overrides. Keep the feature status current and
link each working claim to its evidence.
