# GitHub Copilot Instructions for sdr9700

**Canonical project guide: [`/AGENTS.md`](../AGENTS.md).** Read it and
[`/CONVENTIONS.md`](../CONVENTIONS.md) before suggesting non-trivial code.
This file contains only the highest-priority guidance that fits comfortably
within Copilot's chat context.

## Must-knows before suggesting code

1. **sdr9700 targets the Icom IC-9700.** Do not import assumptions, protocol
   behavior, architecture, or terminology from other radio-control projects
   unless it has been explicitly validated for sdr9700.

2. **Support both Linux and Apple Silicon macOS.** Prefer cross-platform Qt 6
   APIs. If platform-specific behavior is unavoidable, isolate it behind
   `Q_OS_LINUX` or `Q_OS_MAC` guards and preserve equivalent behavior on both
   supported platforms.

3. **Radio-reported state is authoritative.** A command sent to the radio is a
   request, not confirmation that the requested state was applied. Update
   radio-backed UI state from parsed replies or confirmed cache updates whenever
   a response path exists. Ground CI-V decisions in Icom documentation, logs,
   packet captures, or observed IC-9700 behavior.

4. **Use `AppSettings`, not `QSettings`.** sdr9700 client settings are stored as
   JSON beneath `QStandardPaths::GenericConfigLocation`. Use camel-case keys,
   preserve all-capital abbreviations such as `PTT`, `RC28`, and `LAN`, and
   prefer one structured JSON value per feature instead of numerous unrelated
   flat keys. Booleans are stored as `"True"` or `"False"` strings for
   compatibility.

5. **Memory data is a radio-backed cache.** IC-9700 memories are mirrored per
   radio profile in the application-owned SQLite database. The database holds
   last-known state; live radio replies remain authoritative for memory
   contents, slot availability, and write verification.

6. **Protect transmit operations.** PTT, DTMF transmission, transmit audio, and
   other transmit-producing paths require explicit gating. The optional local
   automation bridge must never expose or invoke transmit-producing controls.

7. **Test changes on the complete supported pipeline.** Add or update automated
   tests wherever practical, especially regression tests for defects. Use the
   shared `src/build` directory and run a clean Release build plus the complete
   CTest suite before considering a change complete.

## C++ and Qt 6 style highlights

- Use C++20 and Qt 6 idioms supported by the existing build.
- Use RAII and Qt parent ownership; avoid raw owning `new` and manual `delete`.
- Use Qt signals and slots for cross-object communication.
- Keep GUI objects on the main thread and use queued communication across
  threads.
- Keep classes small and single-purpose.
- Use braces around all control-flow bodies.
- Do not use `goto`.
- Prefer typed `constexpr` constants over new preprocessor constants.
- Classes and structs use `PascalCase`.
- Functions, methods, and local variables use `camelCase`.
- Member variables use `m_camelCase`.
- Constants use `kPascalCase`.
- Use accessible names and descriptions for new interactive controls.
- Do not make unrequested visual-design or UX-direction changes.

## Build and validation

Always use `src/build`; do not create agent-specific build directories.

```bash
make release
ctest --test-dir src/build --output-on-failure
```

Source formatting must use clang-format 23:

```bash
find src -path src/build -prune -o \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' \) -print0 \
  | xargs -0 clang-format-23 --dry-run --Werror
```

Static analysis uses cppcheck 2.21.0 with all source-correctness categories
enabled and the project suppression file. Do not broadly disable categories to
make CI pass. Add a source-specific suppression only for a demonstrated false
positive that cannot reasonably be resolved through clearer code.

## Repository and source boundaries

- Do not remove or rewrite existing user changes unless explicitly requested.
- Do not copy code from another project without an explicit decision and
  license review.
- Keep third-party attribution in `THIRD_PARTY_LICENSES.md`, not in active
  source comments.
- Treat `resources/manuals/` as read-only reference material, not sdr9700
  source code.
- Radio capabilities are compiled into the application; do not add runtime
  radio-definition files.
- Preserve the authenticated shutdown sequence documented in
  `docs/radio/RADIO_DISCONNECT_PROCESS.md`.
- Root documentation must describe functionality that exists now. Put
  unapproved future work in issues or design notes.

For complete project guidance, read `/AGENTS.md` and `/CONVENTIONS.md`.
