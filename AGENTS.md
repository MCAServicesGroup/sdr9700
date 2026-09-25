# SDR9700 Repository Instructions

SDR9700 is a Qt/C++ desktop GUI client for controlling an Icom IC-9700 amateur
radio transceiver over the radio's LAN interface on Linux and Apple Silicon
macOS.

## Instruction Authority

This file is the authoritative source for repository-wide agent instructions.
Consolidate all such instructions here.

## GitHub

- Maintain the `sdr9700` repository in the **MCA Services Group**
  organization: <https://github.com/MCAServicesGroup/sdr9700>.
- Use the `jwpauler` GitHub account for this repository.
- Author commits as `Justin Pauler <justin@pauler.org>`.
- Sign every commit with the key whose fingerprint is
  `5BB858A821B6FEFD33D8A148C3D84DCCEF6E8A03`, and verify each signature.
- Keep this repository public.

## Project Goal

Build a Linux and Apple Silicon macOS application that gives IC-9700 operators
a maintainable native GUI for everyday control, spectrum/waterfall display,
audio routing, and station workflows.

## Current Scope

- Radio connection profiles for IC-9700 LAN control.
- UDP/TCP radio control through the IC-9700 LAN ports.
- VFO frequency and mode control with configurable step sizes.
- Scope/waterfall display.
- AX.25 packet decoding and data inspection.
- RX/TX audio routing through Qt Multimedia.
- AF/RF/TX gain, squelch, PTT, and DTMF send with PTT gating.
- Radio-backed IC-9700 memory management with synchronization and CSV
  import/export.
- Main-window lock mode.
- Icom RC-28 rotary controller support for step tuning and button mapping
  (optional, requires libhidapi at build time).

## Development

- Before beginning project work, read the root `README.md`,
  `_developer/STATUS.md`, `_developer/ISSUES.md`, and `CONVENTIONS.md`.
- Keep Qt/CMake builds, packaging, and runtime configuration reproducible,
  secure, and explicit about required configuration.
- Keep credentials, private keys, production data, and secret-bearing
  environment files out of Git.
- Test changes with the checks appropriate to each affected component. Avoid
  live radio, production, signing, notarization, or release changes unless the
  user explicitly authorizes them.
- Document compatibility limitations, operational requirements, hardware
  validation, and incomplete validation accurately.
- Prefer C++20 standard-library facilities when they are direct semantic
  equivalents, and include their owning headers. Use Qt 6 idioms where Qt
  integration or Qt-specific semantics are required; follow the boundary in
  `CONVENTIONS.md` rather than converting between Qt and standard types solely
  for style.
- Evaluate every code change for its effect on both supported desktop
  platforms: Linux and Apple Silicon macOS. Prefer Qt APIs and Qt libraries
  that already provide cross-platform behavior instead of adding
  platform-specific implementations. When Qt does not provide a suitable
  abstraction, keep platform-specific code isolated behind explicit platform
  guards and preserve equivalent behavior on both platforms.
- Keep classes small and single-purpose.
- Use RAII and Qt parent ownership; avoid raw owning `new`/`delete`.
- Use Qt signals/slots for cross-object communication.
- Attempt to add or update automated tests for every code change wherever
  practical. Run the complete existing test suite after code changes and do
  not leave previously passing tests broken.
- Keep IC-9700 protocol decisions grounded in logs, packet captures, Icom
  documentation, or observed radio behavior; ask for captures when behavior is
  uncertain.
- Use `AppSettings` for SDR9700 client settings. Do not add new app-owned
  `QSettings` persistence.
- Radio capability definitions are compiled into the application; do not add
  runtime radio definition files.
- Do not copy code from other projects into SDR9700 source files without an
  explicit decision and license review.
- The `resources/manuals/` directory contains local copies of IC-9700 manuals
  and related research material. Do not treat it as SDR9700 source code.
- Do not remove or rewrite user changes from the working tree unless the user
  explicitly asks.

## Permissions

- Set all directories to mode `0755`.
- Set all files to mode `0644`.
- Preserve these modes when creating or replacing repository content.
- Generated executable files under `_workspace/build/` may use mode `0755`.
- Keep `_workspace/private/` private: set it and its subdirectories to mode
  `0700` and files beneath it to mode `0600`.

## Repository Layout

### `AGENTS.md`

This file is the authoritative source for repository-wide agent instructions.
Consolidate all such instructions here.

### `_workspace/`

- Use `_workspace/` as the ignored, per-system local work area for files that
  directly support SDR9700. Use standard directory and file modes outside its
  `private/` subdirectory.
- Use `_workspace/build/` as the only local CMake build directory.
- Store sensitive local material only beneath `_workspace/private/`.
- Never commit or synchronize anything under `_workspace/`, including guides,
  configuration, credentials, generated artifacts, and temporary files.
- Operator-approved environment files may be stored only in ignored
  task-specific subdirectories below `_workspace/private/`. Never force-add
  or synchronize them. Store all other credentials, keys, and sensitive
  operational data only in `_workspace/private/`, never elsewhere in this
  repository.

### `_developer/`

- Use `_developer/` as the single committed home for repository-wide
  developer documentation, scripts, CI support files, analyzer configuration,
  utilities, and AI context.
- Organize developer artifacts into purpose-specific subdirectories when that
  improves discoverability.
- `_developer/README.md` indexes the repository-wide developer documentation.
- `_developer/STATUS.md` is maintained by agents and records the current
  development status of SDR9700.
- `_developer/ISSUES.md` is maintained by agents and records confirmed issues
  identified during any project work. Add a finding when it is identified,
  update it as work progresses, and retain its resolution history.
- `_developer/STANDARD.md` defines repository-wide implementation and
  maintenance requirements. `CONVENTIONS.md` remains the canonical source for
  C++ and Qt coding rules.
- Keep `_developer/STATUS.md`, `_developer/ISSUES.md`,
  `_developer/STANDARD.md`, and `CONVENTIONS.md` accurate and up to date.

### Naming and Documentation

- Name project-owned directories and non-code path components with lowercase
  `snake_case`; do not use hyphens in project names. Reserve hyphens for
  ecosystem-mandated names, command names, distribution names, and documented
  filename-role separators. C++ source names follow `CONVENTIONS.md`.
- Treat top-level directories whose names begin with `_` as repository
  infrastructure directories.
- Treat top-level, non-hidden directories without an `_` prefix as SDR9700
  project content.
- Keep credentials, private keys, production data, and secret-bearing
  configuration out of Git.
- In documentation, use paths relative to the repository root. Do not include
  machine-specific absolute paths such as `/mnt/dev/...` or
  `/home/user/Code/...`.

### Source Tree

- `src/gui/`: Qt widgets and dialogs.
- `src/models/`: UI-facing radio and VFO models.
- `src/backend/`: bridge between models and the IC-9700 radio stack.
- `src/radio/`: IC-9700 LAN/CI-V radio protocol implementation.
- `src/audio/`: Qt Multimedia audio handlers and conversion utilities.
- `src/core/`: shared types, settings, profile storage, logging, and queues.

## Source Code Review

When an AI agent is asked to perform a top-down and bottom-up review of the
source code, the expected result is a detailed, nit-picky review of every file
within the source tree. Findings should be identified for a maintainer to
review and confirm; do not automatically correct issues found during this
review unless the maintainer explicitly asks for fixes.

A code review must always include these automated checks, run from the project
root, before reporting findings:

**clang-format** — apply in-place and report any files changed:

```bash
find src \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0 \
  | xargs -0 clang-format-23 -i
git diff --stat
```

Use clang-format 23 for this command. Linux CI invokes `clang-format-23`
explicitly; developers must not format the tree with a different major
version.

**cppcheck** — pedantic mode, using the project suppressions file:

```bash
cppcheck --enable=all --inconclusive --std=c++20 \
  --library=qt \
  --suppress=missingIncludeSystem \
  --suppress=missingInclude \
  --suppress=normalCheckLevelMaxBranches \
  --suppress=checkersReport \
  --suppressions-list=.cppcheck_suppressions \
  -I src src
```

Use cppcheck 2.21.0 for this command. Linux CI builds that exact upstream
release from its checksum-verified source archive; developers must not use a
different analyzer version when adding or removing suppressions.

Keep cppcheck scoped to `src`; generated CMake, Qt MOC, and resource files live
outside that tree in `_workspace/build` and must not be scanned. The command
suppresses only missing external/include-model details and analyzer status
reports. Keep all source correctness categories enabled. Add a source-specific
suppression only for a demonstrated false positive that cannot reasonably be
expressed more clearly in code, and document its reason in
`.cppcheck_suppressions`.

Any findings from these tools that are not already suppressed must be included
in the review report.

Items to look for include, but are not limited to:

- Adherence to SDR9700 principles, coding requirements, style, syntax
  practices, and formatting rules.
- Security risks, unsafe assumptions, input validation gaps, and resource
  handling problems.
- Leftover code, build paths, assumptions, or branches intended for unsupported
  operating systems other than Linux and Apple Silicon macOS.
- Opportunities to refactor, simplify, optimize, or improve maintainability,
  regardless of size or apparent material benefit.
- Comments that lack detail, completeness, or useful context. During review,
  prefer flagging comments that would benefit from more explicit explanation.

## C++ Style

The canonical coding rules live in `CONVENTIONS.md`. Do not duplicate them
here; update `CONVENTIONS.md` when a coding rule changes.

## Build

```bash
make release
ctest --test-dir _workspace/build --output-on-failure
./_workspace/build/bin/SDR9700
```

Always use `_workspace/build` for local builds. Do not create agent-specific
build directories such as `build-codex`, `build-claude`, or similar variants.
Always do a clean build for verification: remove `_workspace/build`,
reconfigure it, then build. The root `Makefile` release and debug targets
perform this clean rebuild without altering `_workspace/private/`.

Only `Release` and `Debug` are supported CMake build types. Use
`make release` for production behavior and normal verification. Use
`make debug` when debug symbols or debugger-friendly builds are required.
Runtime logging is controlled with `--log=radio,udp,ci-v`, `--log=all`, and
optional `--log-file=<path>`. Debug builds default to `--log=all` when no
log option is supplied.

## Settings

Client-side application settings are stored by `AppSettings` beneath Qt's
`QStandardPaths::GenericConfigLocation`, in an `SDR9700` directory. Typical
locations are:

```text
Linux: ~/.config/SDR9700/sdr9700.json
macOS: ~/Library/Preferences/SDR9700/sdr9700.json
```

Memory records are synchronized with the IC-9700 and mirrored per radio profile
in an application-owned SQLite database beneath
`QStandardPaths::GenericDataLocation`. The database is a last-known cache, not
an independent source of radio truth: live radio replies remain authoritative
for slot availability and write verification. CSV import/export is the
supported offline interchange format.

The settings file is JSON. Boolean settings are stored as `"True"` /
`"False"` strings for compatibility with existing code. Use camel-case keys,
with all-caps abbreviations, grouped under current schema objects such as
`radioChooser`, `audio`, `spectrumScope`, and `radio`. Do not add
configuration fallback paths, migration keys, or migration holdover code;
configuration import is the cleanup boundary for current schema validation.
