#!/usr/bin/env python3
"""Push files onto the LilyGO SD card via Basilauncher USB-CDC protocol."""

from __future__ import annotations

import argparse
import glob
import os
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    sys.exit(1)


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    ports = list(list_ports.comports())
    for p in ports:
        desc = f"{p.device} {p.description} {p.hwid}".lower()
        if "usbmodem" in p.device.lower() or "espressif" in desc or "usb jtag" in desc:
            return p.device
    if ports:
        return ports[0].device
    raise SystemExit("No serial port found")


def readline(ser: serial.Serial, timeout: float = 10.0) -> str:
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode("utf-8", errors="replace").rstrip("\r")
        if b != b"\r":
            buf += b
        if len(buf) > 4096:
            buf.clear()
    raise TimeoutError(f"timeout waiting for line; got {buf!r}")


def wait_basi(ser: serial.Serial, tries: int = 40) -> str:
    time.sleep(1.5)  # let post-flash boot finish
    for _ in range(tries):
        ser.reset_input_buffer()
        ser.write(b"BASI\n")
        ser.flush()
        try:
            line = readline(ser, timeout=1.0)
        except TimeoutError:
            time.sleep(0.3)
            continue
        if line.startswith("BASI OK"):
            # Drain any duplicate replies from earlier probes.
            drain_deadline = time.time() + 0.4
            while time.time() < drain_deadline:
                extra = ser.read(256)
                if not extra:
                    time.sleep(0.05)
            return line
    raise SystemExit("Device did not answer BASI (flash Basilauncher with sd_serial?)")


def expect_ok(ser: serial.Serial, what: str, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            line = readline(ser, timeout=1.0)
        except TimeoutError:
            continue
        if line == "OK":
            return
        if line.startswith("ERR"):
            raise SystemExit(f"{what}: {line}")
        # Ignore leftover BASI OK / boot noise
    raise SystemExit(f"{what}: no OK")


def put_file(ser: serial.Serial, local: str, remote: str, chunk: int = 1024) -> None:
    size = os.path.getsize(local)
    print(f"PUT {remote} ({size} bytes) ...", flush=True)
    ser.reset_input_buffer()
    ser.write(f"PUT {remote} {size}\n".encode())
    ser.flush()
    # Wait for READY (ignore noise)
    line = ""
    deadline = time.time() + 15
    while time.time() < deadline:
        try:
            line = readline(ser, timeout=2)
        except TimeoutError:
            continue
        if line == "READY":
            break
        if line.startswith("ERR"):
            raise SystemExit(f"PUT rejected: {line}")
    if line != "READY":
        raise SystemExit(f"PUT rejected: {line!r}")

    sent = 0
    last_pct = -1
    with open(local, "rb") as f:
        while sent < size:
            data = f.read(min(chunk, size - sent))
            if not data:
                break
            # Pace into the CDC RX ring a little at a time.
            off = 0
            while off < len(data):
                piece = data[off : off + 256]
                ser.write(piece)
                ser.flush()
                off += len(piece)
            ack = b""
            deadline = time.time() + 20
            while time.time() < deadline:
                ack = ser.read(1)
                if ack:
                    break
            if ack != b".":
                rest = (ack or b"") + ser.read(256)
                raise SystemExit(f"PUT ack failed at {sent}: {rest!r}")
            sent += len(data)
            pct = (sent * 100) // size if size else 100
            if pct >= last_pct + 5 or pct == 100:
                last_pct = pct
                print(f"  {pct}% ({sent}/{size})", flush=True)
    # Device prints "\nDONE N\n" after the last '.'
    line = ""
    deadline = time.time() + 30
    while time.time() < deadline:
        try:
            line = readline(ser, timeout=2)
        except TimeoutError:
            continue
        if line.startswith("DONE ") or line.startswith("ERR"):
            break
    if not line.startswith("DONE "):
        raise SystemExit(f"PUT failed: {line!r}")
    done = int(line.split()[1])
    if done != size:
        raise SystemExit(f"size mismatch: wrote {done}, expected {size}")
    print(f"  OK {done} bytes", flush=True)


def ls(ser: serial.Serial, path: str) -> None:
    ser.write(f"LS {path}\n".encode())
    ser.flush()
    while True:
        line = readline(ser, timeout=10)
        print(line)
        if line == "END" or line.startswith("ERR"):
            break


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default=None)
    ap.add_argument("--dir", default=".", help="Local folder of .bin files when no paths given")
    ap.add_argument("--remote-dir", default="/firmware")
    ap.add_argument("--ls", action="store_true")
    ap.add_argument("files", nargs="*", help="Specific .bin files (default: all in --dir)")
    args = ap.parse_args()

    port = find_port(args.port)
    print(f"Port {port}", flush=True)
    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(0.3)
    banner = wait_basi(ser)
    print(banner, flush=True)

    remote_root = "/" + args.remote_dir.strip("/")
    ser.reset_input_buffer()
    ser.write(f"MKDIR {remote_root}\n".encode())
    ser.flush()
    expect_ok(ser, f"MKDIR {remote_root}")

    if args.ls:
        ls(ser, remote_root)
        return 0

    files = list(args.files)
    if not files:
        files = sorted(glob.glob(os.path.join(args.dir, "*.bin")))
        # Skip AppleDouble junk if any slipped in
        files = [f for f in files if not os.path.basename(f).startswith("._")]

    if not files:
        raise SystemExit(f"No .bin files in {args.dir}")

    for path in files:
        name = os.path.basename(path)
        put_file(ser, path, f"{remote_root}/{name}")

    print("--- SD /firmware ---", flush=True)
    ls(ser, remote_root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
