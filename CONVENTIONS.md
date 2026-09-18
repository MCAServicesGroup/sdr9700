#SDR9700 Coding Conventions

This file defines how SDR9700 code should be written. It applies to source,
tests, build scripts, and documentation that describes implementation details.

## Core Principles

- Build SDR9700 as an IC-9700 application. Do not preserve assumptions from
  other radio clients unless they are explicitly validated for the IC-9700 and
  documented as SDR9700 behavior.
- Keep attribution in project documentation and license files, not as branding
  or upstream project references inside active source comments.
- Treat `resources/manuals/` as read-only research material.
- Treat imported material as reference only until it has been validated.
- Evaluate every code change for its impact on both Linux and Apple Silicon
  macOS users. A change is not complete until behavior, build implications, and
  user-facing differences have been considered for both platforms.
- Prefer Qt APIs and Qt libraries that provide cross-platform behavior. Use
  platform-specific APIs only when Qt has no suitable abstraction; isolate
  those implementations behind explicit platform guards and retain equivalent
  behavior on Linux and macOS.
- Prefer small, direct changes over broad rewrites.

## C++ and Qt

- Use C++20-capable code where the build permits it, and keep compatibility
  with the current CMake configuration until it is deliberately updated.
- Prefer Qt 6 idioms for Qt-owned objects, signals, slots, and event handling.
- Use RAII and Qt parent ownership. Avoid raw owning `new` and manual `delete`.
- Do not use `goto`.
- Prefer `constexpr`, `static constexpr`, or typed constants over new
  preprocessor constants.
- Use `Q_OS_LINUX`, `Q_OS_WIN`, and `Q_OS_MAC` for new platform guards.
- Log recoverable failures instead of throwing exceptions through Qt paths.

## Formatting

Use underscores, not hyphens, as separators in project-owned file and directory
names. Preserve ecosystem-mandated discovery names when renaming would prevent
the owning tool from finding the file. Current exceptions are `.clang-format`,
`.github/copilot-instructions.md`, and `.github/codeql/codeql-config.yml`.

`.clang-format` is the source of truth for C, C++, and Qt source formatting.
Run it before submitting source changes.

- Base style: LLVM with SDR9700 overrides.
- Indentation: 4 spaces. Tabs are not used.
- Column limit: 120.
- Braces: Allman style for classes, enums, functions, namespaces, structs, and
  multi-line control statements.
- Control flow: braces are required on all `if`, `else`, `for`, `while`, and
  `switch` blocks. `clang-format` will format braces but will not add missing
  braces for you.
- Pointer and reference alignment: bind `*` and `&` to the type, for example
  `QObject* object` and `const QString& value`.
- Includes: preserve the existing include order. `clang-format` is configured
  with `SortIncludes: false`.
- Constructor initializers: pack on following lines using the configured
  continuation indentation.
- Short inline functions may stay on one line when the formatter allows it.
  Short `if` statements and loops do not stay on one line.
- Consecutive declarations and assignments are not column-aligned.

## Build Directory

- Use `src/build` for all local CMake builds.
- Do not create agent-specific build directories such as `build-codex`,
  `build-claude`, or similar variants.
- If `src/build` has the wrong CMake generator or stale cache state, clear and
  reconfigure `src/build` instead of creating another build directory.

## Testing

- Attempt to add or update automated tests for every code change wherever
  practical. Favor deterministic tests that run without radio hardware,
  platform-specific services, or user interaction.
- A defect fix should include a regression test that demonstrates the previous
  failure whenever the behavior can be reproduced reliably in the test suite.
- Prefer Qt Test and CTest so the same tests run on Linux and macOS (Apple
  Silicon).
- Before a code change is considered complete, build the affected targets and
  run the complete existing test suite with:

  ```bash
  ctest --test-dir src/build --output-on-failure
  ```

- All existing tests must continue to pass. Do not remove, disable, or weaken a
  test merely to make a code change pass; update a test only when the intended
  behavior has deliberately changed.
- When behavior requires an IC-9700, RC-28, audio device, or other physical
  hardware, document the manual verification performed and still test the
  hardware-independent logic where possible.

## Naming

- Classes and structs: `PascalCase`.
- Functions, methods, and local variables: `camelCase`.
- Member variables: `m_camelCase`.
- Constants: `kPascalCase`.
- App settings keys: `camelCase`; use all caps for abbreviations inside a key,
  such as `ID`, `UTC`, `LAN`, `PTT`, `ICOM`, `RC28`, and `HZ`.
- File names should match the main class where practical.

## Settings

- Use `AppSettings` for SDR9700 client settings.
- Store client settings below Qt's `QStandardPaths::GenericConfigLocation` in
  `SDR9700/sdr9700.json`.
- Do not add new app-owned `QSettings` persistence.
- Do not add configuration fallback paths, migration keys, or migration
  holdover code. Configuration import may clean and accept the current schema;
  runtime loading should use only the current schema.
- Radio capability definitions are compiled into the application; do not add
  runtime radio definition files.
- Store booleans as `"True"` or `"False"`.
- Prefer one structured JSON value for feature configuration instead of many
  unrelated flat keys.

## Radio Protocol

- IC-9700 behavior must be verified against the radio, logs, packet captures,
  Icom documentation, or existing local behavior before being documented as
  supported.
- GUI state for radio-backed controls must be derived from parsed radio command
  responses or cache updates whenever a response path exists. UI setters may
  request a change, but they must not treat the request itself as confirmation
  of function status.
- When protocol behavior is uncertain, ask for logs or captures before making
  broad changes.
- Keep protocol parsing defensive: validate packet length before indexing,
  tolerate unknown values, and log unexpected recoverable data.
- Do not change the basic VFO RX flow unless the task explicitly requires it
  and the behavior is tested.
- Preserve the authenticated LAN teardown order documented in
  `docs/radio/RADIO_DISCONNECT_PROCESS.md`. In particular, send the control-port
  departure while
  its socket is still open and after token-removal acknowledgement.

## Logging

- Format structured fields as `key=value`, without whitespace after `=`.
- Use `QDebug::nospace()` with explicit separators when streaming structured
  fields; do not rely on `QDebug`'s automatic spaces around punctuation.
- In source, place each streamed key/value pair on its own line when a log
  statement has multiple fields. Keep the emitted log record on one line.
- Use `QDebug::noquote()` for human-readable strings such as addresses, device
  names, roles, and hexadecimal dumps when quotation marks carry no meaning.
- Format network endpoints as `address:port`, not `address : port`.
- Do not end log messages with ellipses. Log the operation and its resulting
  state as separate events when both are useful.

## Threading and Audio

- Keep GUI objects on the main thread.
- Use queued Qt signals for cross-thread communication.
- Do not hold locks in audio callbacks.
- Audio callbacks should avoid allocations and expensive logging in steady
  state.
- Main-thread controls that affect audio-thread state should communicate
  through atomics, queued updates, or existing thread-safe helpers.

## UI

- Do not make visual design or UX-direction changes without maintainer intent.
- Keep controls dense, predictable, and operator-focused.
- Use accessible names/descriptions for new interactive widgets.
- Avoid hidden behavior copied from another radio client unless it is useful
  for IC-9700 operation and documented.
- Dialog content uses `UiTheme::Size::DialogContentMargin` between the left and
  right window edges and the nearest content. Nested layouts must not add a
  second horizontal margin at a window edge.
- Dialog footers use a full-width, 1-pixel separator in
  `UiTheme::Color::BorderMedium`, with the shared spacing defined by
  `kDialogFooterSpacing` above the separator, between the separator and
  buttons, and below the buttons. The
  footer owns the dialog's bottom spacing; do not add a second bottom margin
  on its parent layout. Arrange dialog buttons with `QDialogButtonBox` and
  appropriate button roles so Qt supplies the native button order on Linux
  and macOS. Do not manually encode platform-specific button ordering.
- The Application Log uses the compact footer variant requested for that
  utility window: no separator and no extra space above its action buttons.
  It must retain `kDialogFooterSpacing` below the buttons.

## Third-Party Code

- Do not copy code from other projects into active SDR9700 source files without
  an explicit decision and license review.
- Keep third-party attribution in `THIRD_PARTY_LICENSES.md`.

## Documentation

- Root documentation must describe SDR9700 as it exists now.
- Future plans belong in issues or design notes until they are approved for the
  active project.
- Do not ship user-facing docs that describe unavailable features.
- Use concrete IC-9700 wording instead of inherited project names.
