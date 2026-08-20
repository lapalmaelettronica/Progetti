#!/usr/bin/env python3
from __future__ import annotations

import glob
import grp
import json
import os
import pwd
import socket
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


TCP_HOST = "127.0.0.1"
TCP_PORT = 8765
SERIAL_LINK = Path("/dev/serial0")


@dataclass
class Check:
    status: str
    name: str
    detail: str


def run(cmd: list[str], timeout: float = 2.0) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            cmd,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        return proc.returncode, (proc.stdout + proc.stderr).strip()
    except Exception as exc:
        return 1, repr(exc)


def add(results: list[Check], status: str, name: str, detail: str) -> None:
    results.append(Check(status, name, detail))


def check_python(results: list[Check]) -> None:
    version = sys.version.split()[0]
    status = "PASS" if sys.version_info >= (3, 11) else "WARN"
    add(results, status, "Python", version)


def check_user_groups(results: list[Check]) -> None:
    user = pwd.getpwuid(os.getuid()).pw_name
    groups = []
    for gid in os.getgroups():
        try:
            groups.append(grp.getgrgid(gid).gr_name)
        except KeyError:
            groups.append(str(gid))
    expected = {"dialout", "gpio", "i2c", "spi"}
    missing = sorted(expected - set(groups))
    status = "PASS" if not missing else "WARN"
    detail = f"user={user} groups={','.join(groups)}"
    if missing:
        detail += f" missing={','.join(missing)}"
    add(results, status, "User/groups", detail)


def check_serial(results: list[Check]) -> Path | None:
    if not SERIAL_LINK.exists():
        add(results, "FAIL", "Serial", f"{SERIAL_LINK} missing")
        return None

    target = SERIAL_LINK.resolve()
    readable = os.access(SERIAL_LINK, os.R_OK)
    writable = os.access(SERIAL_LINK, os.W_OK)
    status = "PASS" if readable and writable else "FAIL"
    add(
        results,
        status,
        "Serial",
        f"{SERIAL_LINK} -> {target} readable={readable} writable={writable}",
    )
    return target


def check_serial_owner(results: list[Check], serial_target: Path | None) -> None:
    if serial_target is None:
        add(results, "WARN", "Serial owner", "skipped because serial target is unknown")
        return

    code, output = run(["fuser", "-v", str(serial_target)])
    if code == 0 and output:
        add(results, "WARN", "Serial owner", output.replace("\n", " | "))
    else:
        add(results, "PASS", "Serial owner", f"no owner reported for {serial_target}")


def check_devices(results: list[Check], pattern: str, name: str, missing_status: str) -> None:
    devices = sorted(glob.glob(pattern))
    if devices:
        add(results, "PASS", name, ", ".join(devices))
    else:
        add(results, missing_status, name, f"no matches for {pattern}")


def check_tcp_listen(results: list[Check]) -> None:
    code, output = run(["ss", "-ltnp"])
    needle = f"{TCP_HOST}:{TCP_PORT}"
    if code == 0 and needle in output:
        lines = [line.strip() for line in output.splitlines() if needle in line]
        add(results, "PASS", "TCP listen", " | ".join(lines))
    else:
        add(results, "FAIL", "TCP listen", f"{needle} not found")


def check_tcp_get_state(results: list[Check]) -> None:
    request = {"action": "get_state"}
    try:
        with socket.create_connection((TCP_HOST, TCP_PORT), timeout=2.0) as sock:
            sock.sendall((json.dumps(request) + "\n").encode("utf-8"))
            sock.settimeout(2.0)
            data = sock.recv(4096).decode("utf-8", errors="replace").strip()
        parsed = json.loads(data)
        status = "PASS" if parsed.get("ok") is True else "WARN"
        add(results, status, "TCP get_state", data)
    except Exception as exc:
        add(results, "FAIL", "TCP get_state", repr(exc))


def print_summary(results: list[Check]) -> None:
    width = max(len(item.name) for item in results)
    for item in results:
        print(f"{item.status:<4} {item.name:<{width}} {item.detail}")

    counts = {"PASS": 0, "WARN": 0, "FAIL": 0}
    for item in results:
        counts[item.status] = counts.get(item.status, 0) + 1
    print()
    print(
        "SUMMARY "
        f"PASS={counts.get('PASS', 0)} "
        f"WARN={counts.get('WARN', 0)} "
        f"FAIL={counts.get('FAIL', 0)}"
    )


def main() -> int:
    results: list[Check] = []
    check_python(results)
    check_user_groups(results)
    serial_target = check_serial(results)
    check_serial_owner(results, serial_target)
    check_devices(results, "/dev/i2c*", "I2C devices", "WARN")
    check_devices(results, "/dev/spidev*", "SPI devices", "WARN")
    check_tcp_listen(results)
    check_tcp_get_state(results)
    print_summary(results)
    return 1 if any(item.status == "FAIL" for item in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
