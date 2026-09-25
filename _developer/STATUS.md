# SDR9700 Development Status

SDR9700 is under active development for Linux and Apple Silicon macOS. The
current source version is `26.9.4`.

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
- The CodeQL workflow passes its GitHub Actions, C/C++, and Python analyses.
- The local performance benchmark suite passes, matching every recorded
  nightly workflow run.
- The latest manually dispatched GitHub Nightly Analysis workflow passes its
  ASan/UBSan, TSan, and performance benchmark jobs.
