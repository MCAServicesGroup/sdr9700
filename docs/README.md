# SDR9700 Documentation

This directory contains maintained technical and developer documentation. Files
that GitHub, contributors, or development tools conventionally discover at the
repository root remain there; implementation details and research live here.

## Architecture

- [Architecture](ARCHITECTURE.md) describes the application layers, major
  components, threading model, radio definitions, and current constraints.

## Development

- [Debugging](development/DEBUGGING.md) documents debug builds, runtime logging
  categories, and log-file capture.
- [Releasing](development/RELEASING.md) defines versioning, release notes, and
  the verification and publication checklist.

## IC-9700 Radio Protocol

- [Radio Disconnect Process](radio/RADIO_DISCONNECT_PROCESS.md) records the
  hardware-verified authenticated LAN teardown sequence and its regression
  procedure.
- [Radio Connection Recovery](radio/RADIO_CONNECTION_RECOVERY.md) documents
  normal startup, crash recovery, foreign-session refusal, and standby wake.
- [CI-V Command Audit](radio/research/CI_V_COMMAND_AUDIT.md) compares the Icom
  command reference with SDR9700's compiled capability table.
- [VFO Command Scope](radio/research/VFO_COMMAND_SCOPE.md) records the verified
  and inferred receiver scope of MAIN/SUB-related CI-V controls.

The locally retained Icom reference manual is research material under
`resources/manuals/`; it is not SDR9700 source code or runtime configuration.

## Resource Guides

- [Hardware-integration tools](../resources/tools/README.md) documents the
  local automation bridge stress tools and direct IC-9700 lifecycle utilities.
- [macOS release packaging](../resources/packaging/macos/README.md) documents
  signed, notarized Apple Silicon release packaging and required credentials.
