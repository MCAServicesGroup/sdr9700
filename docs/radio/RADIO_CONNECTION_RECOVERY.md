# IC-9700 Connection Recovery

This document records SDR9700's hardware-verified startup policy for normal,
retained, busy, and remote-standby IC-9700 LAN sessions. The policy is bounded:
it recovers only sessions that SDR9700 can prove it previously owned, and it
does not take over a session owned by another live client.

## Ownership boundary

Login and authentication do not establish ownership. The IC-9700 may accept
both while another client owns its CI-V and audio streams. SDR9700 considers a
session owned only after receiving a correlated, successful stream response
with usable CI-V and audio ports.

Once ownership is established, SDR9700 writes an owner-only crash journal in
the operating system runtime directory. The journal contains the radio address,
random SDR9700 client name, process ID, current authentication fields, and the
local/remote endpoint and session-ID pairs for the control, CI-V, and audio
transports. It contains no username or password and is removed after an
acknowledged normal shutdown.

## Normal startup

For an available and awake radio, SDR9700:

1. Opens and authenticates the control transport.
2. Waits for connection status to confirm that the radio is available.
3. Reserves nonzero local CI-V and audio ports and requests both streams.
4. Completes both media handshakes and opens the CI-V data pipe.
5. Requires a directed Transceiver ID reply before releasing ordinary startup
   commands.
6. Synchronizes radio state, memories, and spectrum scope before reporting the
   radio ready.

Authentication may complete before connection status arrives. This ordering
must not produce a stream request until availability and both local port
reservations are known.

## Recovering an SDR9700 crash

Recovery is allowed only when all of the following match:

- the radio reports an SDR9700 client name from this host address;
- the owner-only journal names that radio and client; and
- the process ID recorded in the journal is no longer running.

The replacement process first replays departures using the predecessor's exact
transport identities. It then resends one correlated token-removal operation at
500 ms intervals, for at most eight attempts. A fresh login begins only after
the radio acknowledges removal. The usual stream and directed-CI-V readiness
checks then apply.

A live SDR9700 process, a missing or malformed journal, mismatched ownership
data, or an unsupported platform does not authorize recovery. This conservative
rule may require waiting for the radio's own timeout, but it cannot disconnect a
session SDR9700 has not proved abandoned and locally owned.

## Radio in use by another client

When the radio advertises a busy session that SDR9700 cannot recover safely,
SDR9700 reports `Radio in use by <address>` using the owner address supplied by
the radio. Generic client names such as `icom-pc` remain in diagnostic logs but
are not used as the operator-facing identifier.

The attempt is terminal and is not retried automatically. SDR9700 responds only
with the protocol's idle acknowledgement, closes its local sockets and
authentication state, and sends no stream close, token removal, or transport
departure for the foreign session.

## Remote standby

An IC-9700 in remote standby still accepts LAN authentication and stream setup,
but its CI-V command plane does not answer the directed identity query. Because
a damaged session can present the same symptom, SDR9700 first replaces the LAN
session once. If the replacement is also silent, it sends the padded CI-V power-
on frame, pauses the normal session watchdog during a ten-second boot interval,
and reconnects.

Wake is limited to two attempts. Success requires a directed identity reply;
control keepalives, authentication, audio, and unsolicited CI-V traffic are not
sufficient. Failure ends the bootstrap without starting an unlimited automatic
reconnect loop. SDR9700 does not change the radio's remote-control power-off
setting.

## Hardware verification

The complete policy was exercised against an IC-9700 on September 2, 2026:

- normal startup and acknowledged disconnect;
- SIGKILL followed by recovery of SDR9700's own journaled session;
- a SIGKILLed independent Python client, producing a busy address notification
  with no stream request, teardown traffic, or reconnect loop; and
- independently commanded standby followed by automatic wake, directed CI-V
  validation, spectrum synchronization, and memory synchronization.

One standby/wake run experienced a later simultaneous CI-V/audio interruption
while control traffic continued. The watchdog detected it after its bounded
restart attempts. A subsequent clean-build run remained healthy for more than
one minute, so this was not reproduced as a deterministic wake failure.
