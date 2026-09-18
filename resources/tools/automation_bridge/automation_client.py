#!/usr/bin/env python3
"""Send one allowlisted JSON request to an opted-in SDR9700 process."""

import argparse
import json

from automation_common import load_endpoint, request as send_request


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("request", help='JSON object, for example: {"action":"get_state"}')
    parser.add_argument("--hold", type=float, default=0.0,
                        help="Keep the client connected for this many seconds after receiving the response")
    parser.add_argument("--match", help="For ui_list, print only controls whose description contains this text")
    parser.add_argument("--discovery", help="Use a specific automation discovery JSON file")
    args = parser.parse_args()

    request = json.loads(args.request)
    if not isinstance(request, dict):
        raise ValueError("request must be a JSON object")

    decoded = send_request(load_endpoint(args.discovery), request, hold=args.hold)
    if args.match and isinstance(decoded.get("controls"), list):
        needle = args.match.casefold()
        decoded["controls"] = [control for control in decoded["controls"]
                               if needle in json.dumps(control, sort_keys=True).casefold()]
    print(json.dumps(decoded, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
