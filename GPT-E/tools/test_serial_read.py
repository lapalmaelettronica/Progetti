#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

import serial


SERIAL_LINK = Path("/dev/serial0")
BAUDRATE = 115200


def run(cmd: list[str]) -> tuple[int, str]:
    try:
        proc = subprocess.run(cmd, check=False, capture_output=True, text=True, timeout=2.0)
        return proc.returncode, (proc.stdout + proc.stderr).strip()
    except Exception as exc:
        return 1, repr(exc)


def serial_owner(serial_target: Path) -> str:
    code, output = run(["fuser", "-v", str(serial_target)])
    if code == 0 and output:
        return output
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description="Read /dev/serial0 @ 115200 without sending data.")
    parser.add_argument("--seconds", type=float, default=10.0, help="Read duration in seconds.")
    args = parser.parse_args()

    if args.seconds <= 0:
        print("ERROR --seconds must be positive", file=sys.stderr)
        return 2

    if not SERIAL_LINK.exists():
        print(f"FAIL {SERIAL_LINK} does not exist")
        return 1

    serial_target = SERIAL_LINK.resolve()
    owner = serial_owner(serial_target)
    if owner:
        print("WARN serial port is already owned by another process; not opening it.")
        print(owner)
        print("Suggestion: keep the service running for normal operation, or explicitly stop it later for a manual serial-only test.")
        return 1

    if not os.access(SERIAL_LINK, os.R_OK | os.W_OK):
        print(f"FAIL current user cannot read/write {SERIAL_LINK}")
        return 1

    deadline = time.monotonic() + args.seconds
    print(f"Opening {SERIAL_LINK} -> {serial_target} @ {BAUDRATE} for {args.seconds:.1f}s")
    try:
        with serial.Serial(str(SERIAL_LINK), BAUDRATE, timeout=0.5) as ser:
            while time.monotonic() < deadline:
                raw = ser.readline()
                if raw:
                    print(raw.decode("utf-8", errors="replace").rstrip())
    except Exception as exc:
        print(f"FAIL serial read error: {exc!r}")
        return 1

    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
