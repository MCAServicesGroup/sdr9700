# SDR9700 Repository Standard

This file defines repository-wide implementation and maintenance requirements.
The detailed C++ and Qt coding rules remain in `CONVENTIONS.md`.

## Requirements

- Preserve supported behavior on Linux and Apple Silicon macOS.
- Keep builds, packaging, and runtime configuration reproducible and explicit.
- Keep credentials, private keys, production data, and secret-bearing files out
  of Git.
- Use `_workspace/` for ignored per-system files and `_developer/` for shared,
  committed developer documentation and repository tooling. Keep sensitive
  local material beneath the restricted `_workspace/private/` subtree.
- Add or update deterministic automated tests for code changes where practical.
- Run a clean affected build and the complete relevant test suite before
  considering a change complete.
- Record incomplete validation and hardware-dependent verification accurately.
