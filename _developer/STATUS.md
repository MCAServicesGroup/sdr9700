# SDR9700 Development Status

SDR9700 is under active development for Linux and Apple Silicon macOS. The
current source version is `26.9.4`.

## Release State

- `26.9.4` is the current stable GitHub release, published from signed tag
  `v26.9.4` at commit `8427927394c2688bd09015b1daa88c6a1bd598e5`.
- The Apple Silicon release workflow built, tested, audited, signed, notarized,
  and stapled `SDR9700-26.9.4-macOS-apple-silicon.dmg` before attaching it to
  the GitHub release.
- The GitHub repository is public, as required by the repository policy.

## Current Work

- Keep the normal Linux and Apple Silicon macOS build and test workflows green.
- Preserve hardware-independent automated coverage; radio-dependent behavior
  still requires explicit IC-9700 validation.

## Validation State

- The clean local Release build and all 37 tests pass.
- The clean AddressSanitizer/UndefinedBehaviorSanitizer build and all 37 tests
  pass.
- The clean ThreadSanitizer build and all 31 compatible tests pass. Six tests
  remain covered by normal and ASan/UBSan CI but are excluded from TSan because
  they exercise unsupported process-launch or uninstrumented Qt runtime paths.
- The clang-format 23 and cppcheck 2.21.0 source checks pass.
- The exact `26.9.4` release commit passes the Linux, Linux without HIDAPI,
  Linux GPU panadapter, and Apple Silicon macOS GitHub build jobs.
- The exact `26.9.4` release commit passes the CodeQL GitHub Actions, C/C++,
  and Python analyses.
- The local performance benchmark suite passes, matching every recorded
  nightly workflow run.
- The latest manually dispatched GitHub Nightly Analysis workflow passes its
  ASan/UBSan, TSan, and performance benchmark jobs.
