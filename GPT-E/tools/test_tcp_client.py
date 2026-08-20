#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import sys


HOST = "127.0.0.1"
PORT = 8765


def send_message(message: dict) -> str:
    raw = json.dumps(message) + "\n"
    print(f"REQUEST  {raw.strip()}")
    with socket.create_connection((HOST, PORT), timeout=2.0) as sock:
        sock.sendall(raw.encode("utf-8"))
        sock.settimeout(3.0)
        response = sock.recv(4096).decode("utf-8", errors="replace").strip()
    print(f"RESPONSE {response}")
    return response


def build_led_message(args: argparse.Namespace) -> dict:
    return {
        "action": "intent",
        "payload": {
            "intent": "set_led",
            "color": args.color,
            "brightness": args.brightness,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Safe TCP client for GPT-E core. Default command is get_state."
    )
    parser.add_argument(
        "--led",
        action="store_true",
        help="Send a safe LED/eyes intent. Does not move servos or motors.",
    )
    parser.add_argument(
        "--color",
        choices=["red", "green", "blue", "yellow", "white", "off"],
        default="off",
        help="LED color used only with --led.",
    )
    parser.add_argument(
        "--brightness",
        type=int,
        default=0,
        help="LED brightness 0-100 used only with --led.",
    )
    args = parser.parse_args()

    if args.brightness < 0 or args.brightness > 100:
        print("ERROR brightness must be between 0 and 100", file=sys.stderr)
        return 2

    message = build_led_message(args) if args.led else {"action": "get_state"}

    try:
        send_message(message)
    except Exception as exc:
        print(f"ERROR {exc!r}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
