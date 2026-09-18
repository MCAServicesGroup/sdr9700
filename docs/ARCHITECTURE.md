# SDR9700 Architecture

SDR9700 is a Qt 6 desktop application for controlling an Icom IC-9700 over the
radio LAN interface.

## High-Level Flow

```text
IC-9700 LAN
    |
    v
src/radio/        IC-9700 UDP, CI-V, audio, and scope protocol handling
    |
    v
src/backend/      RadioBackend adapts radio events to app-facing signals
    |
    v
src/models/       RadioModel, VfoModel, and SpectrumScopeModel hold UI state
    |
    v
src/gui/          MainWindow, dialogs, VFO display, spectrum, and waterfall
```

## Main Components

- `RadioBackend`: owns the radio commander bridge, worker thread, and audio
  device selection.
- `Commander` / `RadioCommander`: parse IC-9700 CI-V responses and dispatch
  radio commands.
- `CachingQueue`: maintains the thread-safe radio-value cache and dispatches
  bounded, deduplicated cache-refresh work. It is not the authoritative path
  for every outbound command.
- `UdpHandler`, `UdpCivData`, `UdpAudio`, `UdpBase`: handle IC-9700 LAN UDP
  control, CI-V data, audio, scope, and network status packets.
- `CivSequenceGate`: suppresses duplicate CI-V UDP payload delivery while
  recording out-of-order arrivals. It does not delay payloads to reorder them.
- `RadioRouter`: converts parsed cache batches into receiver-specific model and
  UI signals while preserving MAIN/SUB routing.
- `RadioState`: holds the latest radio-derived shared and receiver-specific
  values and invalidates them at session and receiver-context boundaries.
- `ScopeController`: coalesces complete scope frames before forwarding them to
  the GUI thread.
- `RadioSessionWatchdog`: evaluates CI-V command/reply liveness independently
  from continuous UDP audio traffic.
- `AudioConverter`: performs bounded sample-format and sample-rate conversion
  for the Qt audio handlers.
- `Ax25Decoder`: decodes AX.25 frames for the data-inspection UI.
- `ScopeAdapter`: converts raw IC-9700 scope bytes to clamped native display
  levels in the range 0–160.
- `RadioModel`: app-level connection and radio state.
- `VfoModel`: active VFO state exposed to the UI.
- `MeterController`: tracks validity and values for receive, transmit, radio,
  and local microphone meters.
- `SpectrumScopeModel`: spectrum range and waterfall/scope data exposed to the UI.
- `SpectrumScopeDisplay`, `SpectrumScopeCanvas`, and `WaterfallCanvas`: display
  IC-9700 scope and waterfall data.
- `SpectrumScopeController` and `WaterfallController`: coordinate scope input,
  rendering state, and operator interaction.
- `MainTitleBar`: custom frameless window title bar housing the menu bar,
  volume/mute/lock controls, and window management buttons.
- `DtmfDialog`: floating DTMF send panel with PTT gating.
- `MemoryController`, `MemorySyncController`, and `MemoryEditorController`:
  synchronize, display, and edit the IC-9700's radio-backed memories.
- `MemoryStore`: validates and serializes memory records for CSV import/export.
- `ApplicationLog` and `LoggingConfiguration`: retain filtered in-application
  diagnostics and configure the independent console, file, and window logging
  destinations.
- `AutomationServer`, `AutomationController`, and `AutomationUiDriver`: expose
  the explicitly enabled local automation socket, enforce its non-transmitting
  command policy, and exercise visible controls through the GUI thread.
- `IcomRC28Manager` (optional, HAVE_HIDAPI): HID driver for the Icom RC-28 rotary
  controller; emits tuning step and button events, and accepts LED state.
- `AppSettings`: JSON-backed client settings at
  `~/.config/SDR9700/sdr9700.json` on Linux and
  `~/Library/Preferences/SDR9700/sdr9700.json` on macOS.

## Threading

- GUI and models live on the main thread.
- The radio commander runs on a worker thread owned by `RadioBackend`.
- `CachingQueue` owns a dedicated `std::thread` for cache batching and queued
  refresh work.
- `RadioRouter` and `ScopeController` live on the radio-data thread so complete
  scope frames can be coalesced before crossing to the GUI.
- Network control, CI-V, and LAN-audio stream objects live on the UDP-handler
  thread. Qt audio input/output and conversion use their own worker threads.
- Cross-thread communication should use queued Qt signals or
  `QMetaObject::invokeMethod` with `Qt::QueuedConnection`.
- Audio callbacks must not block on locks or perform expensive work.

## Radio Definitions

The IC-9700 capability definition is a compiled C++ capability table used by
the radio commander. Startup does not depend on a runtime `radios/` directory
or external radio definition files.

SDR9700 client settings use `AppSettings`; do not add app-owned `QSettings`
persistence.

## Spectrum Data

IC-9700 scope data is converted to display bins by `ScopeAdapter::toLevels`
before it is emitted to the models/UI. The adapter preserves native values in
the range 0–160 and clamps larger bytes to 160:

```cpp
levels[i] = std::min(byte, 160);
```

The scope and waterfall renderers interpret those values against their own
display geometry; they are not calibrated dBm measurements.

## Current Constraints

- The application controls one IC-9700 session while maintaining distinct MAIN
  and SUB receiver state. Receiver-less CI-V replies require serialized
  receiver-context operations because the radio does not identify MAIN/SUB in
  those payloads.
- Imported design documents may describe features that do not exist in SDR9700.
  They are not architecture until they are validated and promoted.
- `resources/manuals/` is research material only.
