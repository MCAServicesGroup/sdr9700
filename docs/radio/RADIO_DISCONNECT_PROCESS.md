# IC-9700 Radio Disconnect Process

This document records SDR9700's verified disconnect and shutdown sequence for
an authenticated IC-9700 LAN session. Treat the ordering below as a protocol
invariant. Changes to it require an IC-9700 log or packet capture and a
close/reopen stress test.

## Why orderly shutdown matters

The IC-9700 maintains state for three UDP endpoints:

- control and authentication on the configured control port (normally 50001);
- CI-V data on the negotiated CI-V port (normally 50002); and
- audio on the negotiated audio port (normally 50003).

Closing local sockets does not tell the radio that these sessions have ended.
If the client omits the control-port departure, the radio can retain a stale
session and ignore a new client's discovery packets until its internal timeout
expires. During the original defect this made SDR9700 appear unable to reconnect
for roughly 70–80 seconds after an otherwise clean application exit.

## Required sequence

The sequence below applies only after the IC-9700 has granted usable CI-V and
audio stream ports to this process. A successful login response and its token
are negotiation state, not proof of session ownership: the radio can accept
login and then reject the stream because another LAN client already owns it.

When SDR9700 has not received a successful stream grant, shutdown must close
only its local sockets, timers, reserved ports, and credential buffers. It must
not send CI-V/audio stream departures, authentication token removal, or the
control-port departure. Those packets can terminate the established session of
another client on the same host or network.

For an owned session, shutdown proceeds synchronously in this order:

1. Stop local RX audio before hiding the main window. This prevents residual
   playback after the visible application closes.
2. Stop timers and watchdog activity that could produce new traffic.
3. Send the CI-V stream-close message.
4. Send `0x05` UDP departure packets from the CI-V and audio endpoints.
5. Allow 500 ms for stream-close traffic to settle while continuing to process
   incoming control datagrams.
6. Send the authentication token-removal request (`requesttype=0x01`). Retry at
   500 ms intervals only if no acknowledgement arrives.
7. Once the radio acknowledges token removal, send the `0x05` UDP departure
   packet from the main control endpoint.
8. Delete the stream handlers, release reserved ports, and close the control
   socket.

The token-removal response confirms that the radio accepted release of the
authentication token. The IC-9700 did not emit a separate status packet with
`disc=1` during verified client-initiated shutdowns, so SDR9700 does not wait for
that packet. Such a status packet is still parsed independently when received.

`UdpBase::sendDeparture()` is idempotent. Explicit shutdown invokes it while
the socket is valid; the base destructor remains a fallback for owned-session
paths that did not perform staged shutdown. The control handler disables that
fallback until a successful stream grant establishes ownership.

## The original defect

Historically, `UdpBase` sent the control departure from its destructor. Staged
shutdown later began closing and deleting the control socket before the base
destructor ran. Consequently:

- CI-V and audio departures were transmitted;
- token removal was acknowledged;
- the control departure was logged nowhere because it was never sent; and
- the radio sometimes retained the session until its internal timeout.

An intermediate diagnostic change mistakenly treated the token-removal
acknowledgement as if it were a `disc=1` status response. Separating those flags
made the missing control departure visible, but waiting longer for `disc=1` did
not address the stale session.

The fix explicitly sends the control departure after token-removal
acknowledgement and before closing the control socket. Successful shutdown now
takes approximately 0.5 seconds.

## Hardware verification

The final sequence was tested on an IC-9700 on July 31, 2026. Seven consecutive
close-and-immediate-reopen cycles produced:

- token-removal acknowledgement on every shutdown;
- a control departure to port 50001 on every shutdown;
- shutdown completion in 503–506 ms;
- a response to the first discovery probe on every subsequent startup, within
  1–2 ms; and
- no connection timeouts.

For future regression testing, run SDR9700 with UDP, radio, and audio logging,
then repeat rapid close/reopen cycles. Each authenticated shutdown must include
these events in order:

```text
[SHUTDOWN] stage=stream-close
[SHUTDOWN] stage=token-removal
token removal acknowledged
[SHUTDOWN] stage=control-departure
Sending UDP stream departure to <radio-address>:50001
[SHUTDOWN] complete ... tokenAcknowledged=true
```

The next process should receive `I am here` in response to its first `Are You
There` probe.

Rejected-session shutdown was additionally verified on August 31 and September
2, 2026 with independent clients owning the IC-9700 LAN session. SDR9700 reports
the radio-supplied owner IP address, closes only its local resources, and does
not automatically retry. The established client remains connected. The
automated suite also exercises 10,000 rejected ownership lifecycles and 10,000
suppressed control-departure attempts. Crash-recovery authorization and standby
wake are documented separately in
[Radio Connection Recovery](RADIO_CONNECTION_RECOVERY.md).
