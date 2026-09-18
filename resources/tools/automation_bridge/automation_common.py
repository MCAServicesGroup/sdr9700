#!/usr/bin/env python3
"""Shared discovery and transport helpers for SDR9700 automation tools."""

import glob
import json
import os
import platform
import socket
import stat
import time


def _config_roots():
    override = os.environ.get("XDG_CONFIG_HOME")
    if override:
        yield override
    if platform.system() == "Darwin":
        yield os.path.expanduser("~/Library/Preferences")
    yield os.path.expanduser("~/.config")


def discovery_files(override=None):
    override = override or os.environ.get("SDR9700_AUTOMATION_DISCOVERY")
    if override:
        return [os.path.expanduser(override)]
    candidates = []
    for root in _config_roots():
        candidates.extend(glob.glob(os.path.join(root, "SDR9700", "automation",
                                                 "sdr9700-automation-*.json")))
    dated = []
    for path in set(candidates):
        try:
            dated.append((os.path.getmtime(path), path))
        except OSError:
            continue
    return [path for _, path in sorted(dated, reverse=True)]


def load_endpoint(discovery_path=None):
    failures = []
    for path in discovery_files(discovery_path):
        try:
            with open(path, encoding="utf-8") as stream:
                endpoint = json.load(stream)
            if endpoint.get("application") != "SDR9700":
                raise RuntimeError("unexpected application identity")
            if endpoint.get("protocol") != 1:
                raise RuntimeError(f"unsupported protocol {endpoint.get('protocol')!r}")
            if endpoint.get("transmitAllowed") is not False:
                raise RuntimeError("endpoint does not explicitly prohibit transmit")
            socket_info = os.stat(endpoint["socket"])
            if not stat.S_ISSOCK(socket_info.st_mode):
                raise RuntimeError("endpoint is not a Unix-domain socket")
            endpoint["discoveryFile"] = path
            return endpoint
        except (OSError, ValueError, KeyError, RuntimeError) as error:
            failures.append(f"{path}: {error}")
    details = "; ".join(failures) if failures else "no discovery records found"
    raise RuntimeError(
        "No live SDR9700 automation bridge found; start the app with "
        f"--enable-automation ({details})"
    )


def request(endpoint, payload, timeout=5, hold=0.0):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(timeout)
        client.connect(endpoint["socket"])
        client.sendall(json.dumps(payload, separators=(",", ":")).encode("utf-8") + b"\n")
        data = bytearray()
        while not data.endswith(b"\n"):
            chunk = client.recv(65536)
            if not chunk:
                raise RuntimeError("automation bridge closed before returning a response")
            data.extend(chunk)
        if hold > 0:
            time.sleep(hold)
    return json.loads(data)
