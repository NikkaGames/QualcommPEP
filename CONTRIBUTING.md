# Contributing

Ports belong in the existing branch for their SoC. Include a photo or video of Windows running on the device, its model and software versions, and a short list of working features and known issues. The [porting guide](docs/PORTING.md) covers the source layout and basic testing.

## Code style

Follow [.editorconfig](.editorconfig) and [.clang-format](.clang-format).

- Use short, descriptive `snake_case` names for internal files and symbols.
- Keep four-space indentation, normal spacing and function braces on a new line.
- Keep comments brief. Explain hardware constraints, ordering or ownership where the code needs it.
- Use WPP for diagnostics. Keep logs useful and avoid frequent logging in performance-sensitive paths.
- Preserve Windows API names, public interfaces and required structure layouts.
- Update includes, declarations, project files, filters and WPP references when moving or renaming code.

Shared code belongs in the appropriate `SystemLibs` module. SoC-specific tables and hardware code belong in the target directories. `Framework/PEP` handles PEP integration, and `api/` contains shared interfaces.

Power management must handle shared resources, concurrent transitions and failures correctly. Preserve declared state meanings, resource minimums and other clients' votes. Report unsupported operations and hardware failures accurately.

AI-assisted code must be understood, reviewed and tested by its contributor. Sloppy generated code, invented hardware behavior, placeholder implementations and comments copied from an AI conversation are not accepted.

## Repository contents

Keep build artifacts, signing keys, firmware dumps, JSON snapshots, temporary tests and investigation notes out of driver source directories. Documentation belongs at the root or under `docs/`. Preserve third-party license notices and identify the source of imported code and hardware tables.

## Pull requests

Describe the change, how it was tested and any remaining problems. For ports, include the device photo or video and basic testing notes described in the porting guide. Known limitations should be listed in the port's status page.

Keep unrelated changes separate. Use short commit subjects that name the subsystem and change:

```text
clock: preserve shared branch votes
pep: initialize firmware low-power idle
```

Contributions to project-authored code use the project's [MIT License](LICENSE).
