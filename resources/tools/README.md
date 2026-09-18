# SDR9700 Tools

This directory contains hardware-integration utilities for an IC-9700. They are
not unit tests and are not run by CTest. Every executable has a narrow purpose
expressed by its filename.

The tools use only the Python standard library. Run them from the SDR9700
repository root.

## Tool summary

| Tool | Purpose | Resulting radio state |
| --- | --- | --- |
| `automation_bridge/automation_client.py` | Sends one JSON request to an opted-in SDR9700 automation bridge. | Depends on the requested action. Transmit actions are always rejected. |
| `automation_bridge/ic9700_vfo_hardware_stress.py` | Stress-tests VFO selection, bands, MAIN/SUB exchange, Dual Watch, and busy gates. | Finishes in a documented baseline. |
| `automation_bridge/ic9700_test_receive_controls.py` | Tests frequencies and receive controls independently on MAIN and SUB. | Restores checked control values, but not the initial bands, frequencies, or modes. |
| `automation_bridge/ic9700_shared_control_sweep.py` | Sweeps shared controls, including AF gain, LAN modulation, and TX power without transmitting. | Restores each swept value. |
| `automation_bridge/validate_automation_coverage.py` | Cross-checks backend commands and live UI controls against declared hardware coverage. | Read-only. |
| `ic9700_rsba1_standby.py` | Connects directly to the radio, requests standby, and validates that directed CI-V replies stop. | Standby. |
| `ic9700_rsba1_wake.py` | Connects directly to the radio, runs a bounded wake sequence, and validates directed CI-V command readiness. | Awake. |
| `ic9700_rsba1_lifecycle.py` | Connects directly, wakes when necessary, holds a healthy session for a requested interval, validates it, and disconnects. | Awake and disconnected. |

The `ic9700_rsba1_shared.py` support module provides common IC-9700 RS-BA1
functionality used by other scripts. It exits with an error if run directly.

## SDR9700 automation bridge tools

The scripts under `automation_bridge/` exercise a running SDR9700 instance
through its opt-in local automation bridge. Start a fully synchronized
application explicitly with automation enabled:

```bash
./src/build/bin/SDR9700 --enable-automation --log=radio,udp,ci-v
```

These tools discover the newest `sdr9700-automation-*.json` record beneath
SDR9700's platform configuration directory. They skip missing socket endpoints
and require both the discovery record and application state to say transmit is
unavailable. They never request PTT or DTMF Send, but they do retune the radio
and change controls.

Run the double inventory before hardware stress:

```bash
python3 resources/tools/automation_bridge/validate_automation_coverage.py
```

The audit exits `0` when everything is covered, `1` when only explicitly
tracked coverage gaps remain, and `2` for an inventory error such as an
unclassified, stale, or duplicate backend operation or UI control.

The static half fails when an `IRadioBackend` operation is added or removed
without a disposition. The runtime half fails when a visible interactive
control is unclassified. Reported `coverage-gap` entries remain incomplete
until a test supplies radio-derived confirmation.

### `automation_bridge/automation_client.py`

Use the general-purpose client to send one allowlisted JSON request:

```bash
python3 resources/tools/automation_bridge/automation_client.py '{"action":"get_state"}'
```

Pass `--hold` to keep the connection open briefly after the response or
`--match` to filter `ui_list` results by their control descriptions. Use
`--discovery PATH` to select a particular running instance. All automation
tools also honor `SDR9700_AUTOMATION_DISCOVERY=PATH`.

### `automation_bridge/ic9700_vfo_hardware_stress.py`

A normal run:

- alternates MAIN and SUB selection;
- performs 500 band transitions across 2 m, 70 cm, and 23 cm;
- performs 100 confirmed MAIN/SUB exchanges;
- cycles Dual Watch off and on 25 times;
- sends 25 bursts of 20 exchange requests to test the busy gate; and
- continuously checks radio-derived operating identity, receiver separation,
  readiness, and transmit-disabled state.

Run the complete test:

```bash
python3 resources/tools/automation_bridge/ic9700_vfo_hardware_stress.py
```

Optional flags:

- `--diagnostic` runs a short selection, band, and exchange path.
- `--dual-only` performs only 25 Dual Watch off/on cycles.
- `--skip-band` skips VFO selection and the 500-transition band phase.
- `--list-ui` prints visible automation controls without running stress phases.

A complete run finishes with Dual Watch enabled, MAIN selected, MAIN on
145.250 MHz in the 2 m band, and SUB on 432.100 MHz in the 70 cm band. It does
not restore the original state.

### `automation_bridge/ic9700_test_receive_controls.py`

This tool verifies that controls affect the intended receiver without bleeding
into the other VFO. It:

- tunes MAIN and SUB upward and downward through eight frequencies on each
  supported band;
- toggles ATT and preamp independently on both receivers;
- selects FIL1, FIL2, and FIL3 and sweeps NB, notch, and NR through the
  consolidated FILTERS panel on both receivers;
- sweeps MAIN and SUB squelch and RF gain;
- exercises FAST, MID, and SLOW AGC in USB before returning to FM; and
- confirms every requested change from SDR9700's radio-derived state.

Run it with:

```bash
python3 resources/tools/automation_bridge/ic9700_test_receive_controls.py
```

Each toggle and slider is restored to the value observed before its individual
check. Initial bands, frequencies, modes, selected VFO, and Dual Watch state are
not restored. The final state is printed in the `CONTROL MATRIX COMPLETE` JSON
record.

Pass `--skip-frequency` to omit the frequency-transition phase. The transcript
and final JSON record explicitly identify a skipped phase.

### `automation_bridge/ic9700_shared_control_sweep.py`

This tool sweeps LAN modulation, application AF gain, and MAIN VFO transmit
power through representative values, then toggles the radio dial lock twice:

```bash
python3 resources/tools/automation_bridge/ic9700_shared_control_sweep.py
```

The power setting changes, but the automation bridge cannot key the radio and
the tool continuously verifies `transmitAllowed: false` and
`transmitting: false`. Every slider is returned to its observed starting value.
The LAN modulation assertions use the IC-9700's confirmed CI-V value report
after each change. This behavior was verified against a physical IC-9700 over
LAN; the sweep deliberately fails if that confirmation is absent.

## Direct RS-BA1 tools

Each command opens RS-BA1 control, CI-V, and audio transports on the standard
IC-9700 UDP ports, performs its operation, validates it, and cleans up the
acquired session. The stream request negotiates 48 kHz RX and 16 kHz TX LPCM
audio. The tools never key PTT or send transmit audio.

The standby and wake commands use the same minimal arguments:

```text
--radio-ip RADIO_IP --radio-username USERNAME --radio-password PASSWORD
```

Custom RS-BA1/NAT port mappings and timing overrides are intentionally not
supported.

### `ic9700_rsba1_standby.py`

```bash
python3 resources/tools/ic9700_rsba1_standby.py \
  --radio-ip RADIO_IP --radio-username USERNAME --radio-password PASSWORD
```

The tool immediately opens a direct session and proves that it receives a
directed CI-V Transceiver ID reply. It then sends the standby command, waits
eight seconds, and repeats the identity probe. It passes only when the first
probe succeeds and the post-standby probe receives no reply. If the radio keeps
answering, it fails and reports that remote-control standby may be disabled.

### `ic9700_rsba1_wake.py`

```bash
python3 resources/tools/ic9700_rsba1_wake.py \
  --radio-ip RADIO_IP --radio-username USERNAME --radio-password PASSWORD
```

The wake command uses a bounded connection sequence:

1. Open a complete IC-9700 RS-BA1 session and send a directed CI-V
   identity probe.
2. If the command plane is silent, close the owned session and try one entirely
   fresh session. This distinguishes a retained pipe from probable standby.
3. If the fresh command plane is also silent, send the padded CI-V `18 01`
   power-on frame and keep that transport alive for ten seconds.
4. Reconnect and repeat the wake once if necessary.
5. Pass only when a fresh session receives the directed identity reply; fail
   after two wake attempts.

An already-awake radio is a successful outcome because the requested end state
is already satisfied. Login, authentication, stream grant, audio, and
unsolicited data never count as proof that wake completed.

### `ic9700_rsba1_lifecycle.py`

```bash
python3 resources/tools/ic9700_rsba1_lifecycle.py \
  --radio-ip RADIO_IP --radio-username USERNAME --radio-password PASSWORD \
  [--run-time SECONDS]
```

The lifecycle command opens a complete IC-9700 RS-BA1 session and wakes the
radio when necessary. It keeps the proven session connected for 65 seconds by
default, which provides enough time to send and validate three authentication-
token renewals at approximately 20, 40, and 60 seconds. `--run-time` overrides
that interval. RS-BA1 transport keepalives continue throughout the run.

After the radio grants stream ownership, the tool writes a private crash journal
in the operating system's temporary directory. It first records the granted
authentication token, then atomically adds the control, CI-V, and audio endpoint
and session-ID pairs after both media handshakes finish. On the next invocation,
a journal left by a dead process causes the tool to replay departures from the
predecessor's exact transport identities, remove the predecessor token with a
correlated acknowledgement, and only then start a fresh login. If no usable
journal exists but the radio reissues retained authentication, a rejected stream
request triggers acknowledged token removal and one fresh-login retry.

At the end of the interval it performs another directed CI-V identity probe.
It passes only when that post-run command-plane validation succeeds and the
owned session completes its disconnect sequence.

## Logs and exit status

The direct RS-BA1 tools print an ISO-8601 timestamped console log containing every
discrete request, handshake response, control response, CI-V command/reply,
validation result, and owned-session cleanup action. Continuous audio payloads
are drained but not dumped packet-by-packet.

Wake-capable commands emit explicit `RECOVERY` records when a silent command
plane requires a fresh-session replacement and explicit `WAKE` records when a
CI-V power-on command is required. One human-readable `STATE` record describes
the result as normal startup, successful hung-session recovery, or radio wake.
A second `STATE` record contains only structured fields reporting the outcome
and number of wake attempts. The lifecycle command then emits `STATE initial
connection complete; entering maintenance phase` before beginning its timed
token-renewal loop.

Every tool exits nonzero on a rejected command, timeout, missing endpoint,
unsafe transmit state, lost readiness, or failed validation. A zero exit status
means the scripted assertions passed; it does not replace review of application
and radio logs for warnings, retransmissions, disconnects, or recovery events.

Pressing Control-C intentionally simulates an abruptly vanished client. The
process emits one local `ABORT` record and exits immediately with status 130.
It does not close the CI-V pipe, send transport departures, remove its token, or
perform any other radio-side disconnect action. The resulting retained session
and crash journal are deliberate test state for a later recovery run.

Run the hardware-independent protocol and recovery-journal checks with:

```bash
python3 -m unittest resources/tools/tests/test_ic9700_rsba1_shared.py
```
