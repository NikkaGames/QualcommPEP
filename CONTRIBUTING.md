# Contributing

[README](README.md) · [Feature status](docs/STATUS.md) · [Porting requirements](docs/PORTING.md)

These rules apply to changes accepted into this repository. SoC ports must target
their existing branch and meet the complete hardware-validation requirements in
the [porting guide](docs/PORTING.md#required-validation).

## Code and structure

- Keep WDF/PoFx lifecycle and PEP-specific admission under `Framework/PEP`.
  Place reusable mechanisms under the matching `SystemLibs` module and hardware
  facts under the target directory. Keep `api/` limited to shared contracts.
- Give each module a clear responsibility. Split files by ownership and behavior,
  rather than line count. Keep one canonical definition of every object and
  function; avoid duplicated state engines and broad include-all headers.
- Use short, meaningful `snake_case` names for internal files, functions and
  types. Remove redundant project/SoC prefixes when directory scope supplies the
  context. Names should describe the operation or resource being managed.
- Preserve WDK/WDF/PoFx prototypes, exported ABI, peer interfaces, ACPI names and
  generated identifiers where required. Record the external contract that
  justifies each naming or layout exception.
- Update declarations, definitions, function pointers, includes, WPP/TMH names,
  project items and filters together. Check Windows filename collisions and
  ensure each compilation unit is included exactly once.

Use [.editorconfig](.editorconfig) and [.clang-format](.clang-format): four spaces,
normal operator spacing, function braces on a new line, and readable block
separation. Do not compress loops or several operations onto one line. Preserve
required source encodings and Windows project/script line endings.

## Ownership and correctness

Every hardware path needs an explicit owner, valid resource assignment, lifetime,
IRQL contract, lock order, completion/readback and failure behavior. Protect whole
transactions and asynchronous lifetimes. Distinguish descriptor generation from
provider registration and owner epoch.

Preserve shared votes, declared resource minimums and rollback semantics. Failed
or ambiguous completion must remain visible. Do not manufacture successful
states, bypass readback, add device-name exceptions or silently substitute an
unimplemented provider. D0-D3, component F states and multi-set performance
selectors must retain their declared meanings.

## Comments and diagnostics

Comments should explain an ownership invariant, hardware constraint, unusual
ordering or other non-obvious decision. Keep them brief and mostly lowercase.
Preserve required legal notices, SAL annotations and WPP configuration blocks.

Remove code narration, decorative separators, development-stage histories,
speculative claims and references to a conversation or an assistant. Apply this
standard throughout the changed area, including shared libraries and headers.

Use WPP and IFR for driver diagnostics. Events should identify the resource or
device, operation, status and relevant generation/completion information. Keep
high-frequency and idle paths free of diagnostic work that changes timing.
Do not introduce a parallel debug-print backend.

## Source hygiene

Keep production source directories free of build outputs, generated WPP files,
signing keys, firmware dumps, JSON policy snapshots, temporary tests and
investigation notes. Maintained documentation belongs at the repository root or
under `docs/`; validation artifacts belong in review attachments or a separate
test area.

Generated tables need a documented input source and regeneration procedure.
Review the generator or integration point instead of making cosmetic edits that
will be overwritten. Preserve third-party attribution and compatible license
notices. Submission does not establish rights to unrelated firmware or vendor
code.

## AI-assisted submissions

Contributors are responsible for understanding, reviewing, testing and maintaining
every submitted line. AI-generated code must meet the same standards as any
other contribution.

Unreviewed or low-quality generated code is not accepted. This includes invented
hardware behavior, copied target constants without evidence, placeholder
implementations, fake success, redundant wrappers, dead code, excessive narrative
comments and claims that exceed the supplied validation. Tool-generated output
or a passing build cannot replace hardware proof.

## Commits and pull requests

Keep mechanical moves, renames and formatting separate from behavior changes.
Use short, professional commit subjects that name the affected subsystem and
change, for example:

```text
rpmh: retain tickets until completion
clock: preserve shared branch votes
pep: initialize firmware low-power idle
```

A pull request must describe the problem, resulting behavior, ownership or ABI
effects, validation and remaining limitations. Port submissions must include the
full evidence package from the porting guide. Source-only reasoning and runtime
experiments must be identified accurately; the final package must pass without
temporary debugger changes.

The checkout currently has no public automated test harness. Provide reproducible
checks and hardware artifacts with the submission, and document all dependencies
needed to repeat them. Do not claim that unpublished local tooling is available
to reviewers.

Contributions to project-authored code are submitted under the [MIT License](LICENSE).
