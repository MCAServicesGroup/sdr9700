# Contributing to sdr9700

Thanks for helping with sdr9700. The most useful contributions are focused,
verifiable changes that improve everyday IC-9700 operation without making the
radio-control path harder to understand or maintain.

## Before You Start

1. Read `README.md` for project scope.
2. Read `CONVENTIONS.md` for coding rules.
3. Read `AGENTS.md` if you are using an AI coding assistant.
4. Check whether the file you want to use is imported reference material.
   Rewrite and validate it before promoting it to active docs.

## Good First Contributions

- Remove stale references to other projects from active sdr9700 files.
- Replace copied documentation with IC-9700-specific documentation.
- Fix build warnings or obvious memory/resource ownership issues.
- Add narrow tests for protocol parsing or settings behavior.
- Improve accessibility names/descriptions on existing widgets.

## Development Workflow

```bash
make release
./src/build/bin/sdr9700
```

Use `src/build` for all local builds. Do not create agent-specific build
directories such as `build-codex`, `build-claude`, or similar variants. If
`src/build` was configured with a different CMake generator, clear and
reconfigure `src/build`.

Use `make debug` for developer builds that need debug symbols. Runtime logging
is controlled separately in release builds. Debug builds default to `--log=all`:

```bash
./src/build/bin/sdr9700 --log=radio,udp,ci-v --log-file=/tmp/sdr9700.log
```

Other CMake build types are rejected by CMake.

## Change Guidelines

- Keep each change focused on one problem.
- Consider build and behavior impacts for both Linux and Apple Silicon macOS.
  Prefer Qt's cross-platform APIs and isolate unavoidable native code.
- Attempt to add or update automated tests for every code change wherever
  practical. Regression fixes should include a test when the failure can be
  reproduced reliably.
- Preserve user changes already present in the working tree.
- Do not copy material from `resources/manuals/` into `src/`.
- Do not promote imported material to active docs without rewriting it for
  sdr9700 and the IC-9700.
- Do not add new app-owned `QSettings` usage. Use `AppSettings`.

## Bug Reports

Useful bug reports include:

- sdr9700 commit or version.
- Linux distribution or Apple Silicon Mac model, macOS version, and Qt version.
- IC-9700 firmware version.
- Connection type and radio LAN settings.
- Exact steps to reproduce.
- Logs or packet captures when protocol behavior is involved.

## Pull Requests

Create a topic branch and open a pull request into `main`; do not build new work
directly on `main`. A pull request may be small, but it should explain the
operator or maintenance problem, the change, and the verification performed.
Link an issue when one already exists. Small documentation and maintenance
changes do not require a separate issue.

Before opening a pull request, run the automated checks from the project root:

```bash
make check
```

This is the same entry point used by Linux CI. It verifies the required tool
versions, checks formatting without modifying files, and runs cppcheck with
the repository suppressions. The underlying commands are documented below.

**clang-format** — apply in-place and confirm no files changed:
```bash
find src -path src/build -prune -o \( -name '*.cpp' -o -name '*.h' \) -print0 \
  | xargs -0 clang-format-23 -i
git diff --stat
```

Use clang-format 23. CI rejects formatting produced by a different major
version.

**cppcheck** — pedantic static analysis:
```bash
cppcheck --enable=all --inconclusive --std=c++20 \
  --library=qt \
  --suppress=missingIncludeSystem \
  --suppress=missingInclude \
  --suppress=normalCheckLevelMaxBranches \
  --suppress=checkersReport \
  --suppressions-list=.cppcheck_suppressions \
  -I src -i src/build src
```

Use cppcheck 2.21.0 so local findings match CI and the reviewed suppressions
file.

Any findings not already suppressed should be addressed or explained in the PR description.

Then:

- Run a clean Release build with `make release`.
- Run the complete existing test suite with
  `ctest --test-dir src/build --output-on-failure`; all tests must pass.
- Explain what changed and how it was verified.
- Note any behavior that needs validation against real IC-9700 hardware.

Root documentation should stay short and accurate. Long design notes, imported
plans, and future feature concepts should stay out of root docs until ready.
