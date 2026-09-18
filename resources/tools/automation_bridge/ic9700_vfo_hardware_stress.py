#!/usr/bin/env python3
"""Exercise receive-only VFO transitions through SDR9700's automation bridge."""

import json
import sys
import time

from automation_common import load_endpoint, request as send_request


ENDPOINT = load_endpoint()

COUNTS = {"requests": 0, "confirmed_transitions": 0, "expected_rejections": 0,
          "selection_gate_retries": 0}
DIAGNOSTIC = "--diagnostic" in sys.argv
DUAL_ONLY = "--dual-only" in sys.argv
LIST_UI = "--list-ui" in sys.argv
SKIP_BAND = "--skip-band" in sys.argv
EXCHANGE_NUMBER = 0
BAND_TRANSITION_NUMBER = 0


def request(payload):
    COUNTS["requests"] += 1
    return send_request(ENDPOINT, payload)


def state():
    response = request({"action": "get_state"})
    if not response.get("ok"):
        raise RuntimeError(f"get_state failed: {response}")
    value = response["state"]
    if value.get("transmitAllowed") is not False or value.get("transmitting") is not False:
        raise RuntimeError(f"unsafe transmit state observed: {value}")
    if not value.get("connected") or not value.get("ready"):
        raise RuntimeError(f"radio lost ready state: {value}")
    return value


def wait_for(label, predicate, timeout=12.0):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = state()
        if predicate(last):
            COUNTS["confirmed_transitions"] += 1
            return last
        time.sleep(0.05)
    raise RuntimeError(f"timeout waiting for {label}; last state={json.dumps(last, sort_keys=True)}")


def accepted(payload):
    response = request(payload)
    if not response.get("ok"):
        raise RuntimeError(f"request rejected unexpectedly: {payload}: {response}")
    return response


def set_dual(enabled):
    current = state()
    if current.get("dualWatch") == enabled:
        return current
    deadline = time.monotonic() + 12.0
    while True:
        response = request({"action": "set_dual_watch", "enabled": enabled})
        if response.get("ok"):
            break
        if response.get("error") != "request_rejected" or time.monotonic() >= deadline:
            raise RuntimeError(f"Dual Watch gate did not become available: {response}")
        COUNTS["expected_rejections"] += 1
        time.sleep(0.05)
    if enabled:
        return wait_for("Dual Watch enabled with restored SUB", lambda s: s.get("dualWatch") is True and
                        s["receivers"]["SUB"].get("frequencyHz") is not None)
    return wait_for("Dual Watch disabled and SUB invalidated", lambda s: s.get("dualWatch") is False and
                    s["receivers"]["SUB"].get("frequencyHz") is None and
                    s["receivers"]["SUB"].get("band") is None)


def select_vfo(vfo):
    deadline = time.monotonic() + 12.0
    while True:
        response = request({"action": "select_vfo", "vfo": vfo})
        if response.get("ok"):
            break
        if response.get("error") != "request_rejected" or time.monotonic() >= deadline:
            raise RuntimeError(f"VFO selection gate did not become available: {response}")
        COUNTS["selection_gate_retries"] += 1
        time.sleep(0.05)
    return wait_for(f"selected {vfo}", lambda s: s.get("selectedVfo") == vfo)


def set_frequency(vfo, hz):
    accepted({"action": "set_frequency", "vfo": vfo, "frequencyHz": hz})
    return wait_for(f"{vfo} frequency {hz}", lambda s: s["receivers"][vfo].get("frequencyHz") == hz)


def select_band(vfo, band):
    global BAND_TRANSITION_NUMBER
    BAND_TRANSITION_NUMBER += 1
    response = accepted({"action": "select_band", "vfo": vfo, "band": band})
    if BAND_TRANSITION_NUMBER % 25 == 0:
        print(f"BAND PROGRESS {BAND_TRANSITION_NUMBER}/500 request={vfo}:{band} "
              f"accepted_state={json.dumps(response.get('state'), sort_keys=True)}", flush=True)
    settled = wait_for(f"{vfo} complete band {band} identity",
                       lambda s: s["receivers"][vfo].get("band") == band.upper() and
                       s["receivers"][vfo].get("frequencyHz") is not None and
                       s["receivers"][vfo].get("mode") is not None and
                       s["receivers"][vfo].get("filter") is not None)
    # The application deliberately holds the control disabled for a short
    # radio-confirmation quiet period after the final identity field arrives.
    time.sleep(0.3)
    return settled


def exchange():
    global EXCHANGE_NUMBER
    EXCHANGE_NUMBER += 1
    before = wait_for(
        "complete MAIN/SUB identity before exchange",
        lambda s: all(s["receivers"][vfo].get(field) is not None
                      for vfo in ("MAIN", "SUB") for field in ("frequencyHz", "band", "mode", "filter")),
    )
    main_before = before["receivers"]["MAIN"]
    sub_before = before["receivers"]["SUB"]
    main_hz = main_before.get("frequencyHz")
    sub_hz = sub_before.get("frequencyHz")
    if main_hz is None or sub_hz is None or main_hz == sub_hz:
        raise RuntimeError(f"exchange precondition failed: {before}")
    deadline = time.monotonic() + 12.0
    while True:
        response = request({"action": "exchange_main_sub"})
        if response.get("ok"):
            break
        if response.get("error") != "request_rejected" or time.monotonic() >= deadline:
            raise RuntimeError(f"exchange gate did not become available: {response}")
        COUNTS["expected_rejections"] += 1
        time.sleep(0.05)
    # Frequency, band, mode, and filter are the operating identity exchanged by
    # the IC-9700. Receiver-scoped controls such as repeater offset are read
    # independently and are not presumed to swap with the VFO identity.
    expected_identity = {
        "MAIN": {key: sub_before.get(key) for key in ("frequencyHz", "band", "mode", "filter")},
        "SUB": {key: main_before.get(key) for key in ("frequencyHz", "band", "mode", "filter")},
    }
    if EXCHANGE_NUMBER <= 4:
        print(f"EXCHANGE {EXCHANGE_NUMBER} BEFORE={json.dumps(before, sort_keys=True)} "
              f"EXPECTED={json.dumps(expected_identity, sort_keys=True)}", flush=True)
    return wait_for(f"complete MAIN/SUB receiver-state exchange expected={expected_identity}",
                    lambda s: s["receivers"]["MAIN"].get("frequencyHz") == sub_hz and
                    s["receivers"]["SUB"].get("frequencyHz") == main_hz and
                    s["receivers"]["MAIN"].get("band") == sub_before.get("band") and
                    s["receivers"]["SUB"].get("band") == main_before.get("band") and
                    s["receivers"]["MAIN"].get("mode") == sub_before.get("mode") and
                    s["receivers"]["SUB"].get("mode") == main_before.get("mode") and
                    s["receivers"]["MAIN"].get("filter") == sub_before.get("filter") and
                    s["receivers"]["SUB"].get("filter") == main_before.get("filter"))


def normalize_pair(main_band, sub_band):
    current = state()
    current_main = current["receivers"]["MAIN"].get("band")
    current_sub = current["receivers"]["SUB"].get("band")
    if (current_main, current_sub) == (main_band.upper(), sub_band.upper()):
        return current
    if (current_main, current_sub) == (sub_band.upper(), main_band.upper()):
        return exchange()
    if current_sub == main_band.upper():
        temporary = next(band for band in ("2m", "70cm", "23cm")
                         if band.upper() not in (current_main, main_band.upper()))
        select_band("SUB", temporary)
    select_band("MAIN", main_band)
    select_band("SUB", sub_band)
    return state()


started = time.monotonic()
initial = state()
print("INITIAL", json.dumps(initial, sort_keys=True), flush=True)

if LIST_UI:
    controls = request({"action": "ui_list"})["controls"]
    useful = [item for item in controls if item.get("visible") and
              (item.get("accessibleName") or item.get("objectName") or item.get("text"))]
    print(json.dumps(useful, indent=2, sort_keys=True), flush=True)
    raise SystemExit(0)

if DUAL_ONLY:
    for _ in range(25):
        set_dual(False)
        set_dual(True)
    print("PHASE dual-watch 50 transitions complete", flush=True)
    print("SUMMARY", json.dumps(COUNTS, sort_keys=True), flush=True)
    raise SystemExit(0)

# Establish a known, legal two-band baseline through real CI-V operations.
set_dual(True)
normalize_pair("2m", "70cm")
set_frequency("MAIN", 145_250_000)
set_frequency("SUB", 432_100_000)

if not SKIP_BAND:
    # Exercise selection confirmation in both directions repeatedly.
    selection_iterations = 2 if DIAGNOSTIC else 50
    for iteration in range(selection_iterations):
        select_vfo("SUB" if iteration % 2 == 0 else "MAIN")
    print(f"PHASE selection {selection_iterations} complete", flush=True)

    # Rotate every supported band through both receivers. The companion receiver
    # is moved first when needed so the IC-9700 never has to retain one band on
    # both VFOs as the settled result.
    band_pairs = (("2m", "70cm"), ("70cm", "23cm"), ("23cm", "2m"))
    band_iterations = 2 if DIAGNOSTIC else 250
    for iteration in range(band_iterations):
        main_band, sub_band = band_pairs[iteration % len(band_pairs)]
        select_band("SUB", sub_band)
        select_band("MAIN", main_band)
        settled = state()
        if settled["receivers"]["MAIN"].get("band") == settled["receivers"]["SUB"].get("band"):
            raise RuntimeError(f"same-band settled state after rotation: {settled}")
    print(f"PHASE band rotation {band_iterations * 2} transitions complete", flush=True)

# Restore unique frequencies, then verify a large number of fully confirmed
# exchanges. Each iteration waits for the radio-derived state to swap.
normalize_pair("2m", "70cm")
set_frequency("MAIN", 145_250_000)
set_frequency("SUB", 432_100_000)
exchange_iterations = 2 if DIAGNOSTIC else 100
for _ in range(exchange_iterations):
    exchange()
print(f"PHASE confirmed exchange {exchange_iterations} complete", flush=True)

if DIAGNOSTIC:
    print("DIAGNOSTIC COMPLETE", flush=True)
    raise SystemExit(0)

# Repeatedly invalidate and restore SUB through actual Dual Watch transitions.
for _ in range(25):
    set_dual(False)
    set_dual(True)
print("PHASE dual-watch 50 transitions complete", flush=True)

# Hammer the exchange endpoint in bursts. Exactly one request per burst should
# enter the pending state; the rest must be synchronously rejected as busy.
for burst in range(25):
    before = state()
    old_main = before["receivers"]["MAIN"].get("frequencyHz")
    old_sub = before["receivers"]["SUB"].get("frequencyHz")
    deadline = time.monotonic() + 5
    while True:
        response = request({"action": "exchange_main_sub"})
        if response.get("ok"):
            break
        COUNTS["expected_rejections"] += 1
        if time.monotonic() >= deadline:
            raise RuntimeError(f"burst {burst} never accepted after the prior exchange completed")
        time.sleep(0.02)
    for _ in range(19):
        response = request({"action": "exchange_main_sub"})
        if response.get("ok"):
            raise RuntimeError(f"burst {burst} accepted more than one exchange")
        COUNTS["expected_rejections"] += 1
    wait_for(f"burst exchange {burst}", lambda s: s["receivers"]["MAIN"].get("frequencyHz") == old_sub and
             s["receivers"]["SUB"].get("frequencyHz") == old_main)
print("PHASE burst exchange 500 requests complete", flush=True)

# Finish in a deterministic operator-friendly state and verify all safety and
# receiver-separation invariants one final time.
set_dual(True)
normalize_pair("2m", "70cm")
set_frequency("MAIN", 145_250_000)
set_frequency("SUB", 432_100_000)
select_vfo("MAIN")
final = state()
if final["receivers"]["MAIN"].get("band") == final["receivers"]["SUB"].get("band"):
    raise RuntimeError(f"final receivers collapsed onto one band: {final}")
print("FINAL", json.dumps(final, sort_keys=True), flush=True)
print("SUMMARY", json.dumps({**COUNTS, "elapsed_seconds": round(time.monotonic() - started, 2)}, sort_keys=True),
      flush=True)
