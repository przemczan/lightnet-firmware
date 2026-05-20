#!/usr/bin/env python3
"""
flash_panels_serial.py — upload panel firmware to a Lightnet controller over USB serial.

Usage:
    python tools/flash_panels_serial.py <port> <firmware.bin> [--baud BAUD]

Examples:
    python tools/flash_panels_serial.py COM3 .pio/build/panel_atmega328p/firmware.bin
    python tools/flash_panels_serial.py /dev/ttyUSB0 firmware.bin --baud 115200

The controller responds:
    READY   — header accepted, sending data
    OK      — firmware stored, panel flashing started
    ERR:…   — something went wrong

Requires: pip install pyserial
"""

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not found — install it with:  pip install pyserial")

MAGIC = b"LNFW"
DEFAULT_BAUD = 57600
BOOT_WAIT_TIMEOUT = 3.0   # seconds to look for the boot-ready signal
READY_TIMEOUT     = 10.0  # seconds to wait for READY after sending header
OK_TIMEOUT        = 30.0  # seconds to wait for OK after sending data


def crc16(data: bytes) -> int:
    """CRC-16/IBM — matches crc16() in Utils/Crc.cpp (poly 0xA001, init 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def wait_line(ser: "serial.Serial", timeout: float, prefix: str = "") -> str:
    """Read lines until one matches the expected prefix (or timeout)."""
    deadline = time.monotonic() + timeout
    buf = b""
    while time.monotonic() < deadline:
        if ser.in_waiting:
            ch = ser.read(1)
            buf += ch
            if ch == b"\n":
                line = buf.decode("ascii", errors="replace").strip()
                buf = b""
                if not prefix or line.startswith(prefix) or line.startswith("ERR:"):
                    return line
                # debug output from controller — print and keep waiting
                sys.stdout.buffer.write(f"  [{line}]\n".encode("utf-8", errors="replace"))
    return ""


def main():
    parser = argparse.ArgumentParser(description="Upload panel firmware over serial")
    parser.add_argument("port", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("firmware", help="Path to firmware.bin")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help=f"Baud rate (default {DEFAULT_BAUD})")
    args = parser.parse_args()

    with open(args.firmware, "rb") as f:
        data = f.read()

    size = len(data)
    checksum = crc16(data)

    print(f"Firmware : {args.firmware} ({size} bytes, CRC-16=0x{checksum:04X})")
    print(f"Port     : {args.port} @ {args.baud} baud")

    with serial.Serial(args.port, args.baud, timeout=1,
                       dsrdtr=False, rtscts=False) as ser:
        # Prevent DTR/RTS from resetting the ESP, drain any pending bytes
        ser.dtr = False
        ser.rts = False
        time.sleep(0.5)
        ser.reset_input_buffer()

        header = MAGIC + struct.pack("<I", size)

        # Optional: wait briefly for the controller boot signal (only useful if
        # the controller is currently booting; skip gracefully if already running).
        print("Waiting for controller…", end=" ", flush=True)
        deadline = time.monotonic() + BOOT_WAIT_TIMEOUT
        controller_ready = False
        while time.monotonic() < deadline:
            line = wait_line(ser, 0.5)
            if "OTA" in line:
                controller_ready = True
                break
            if line:
                print(".", end="", flush=True)
        if controller_ready:
            print(" ready")
        else:
            print(" (already running, proceeding)")

        # Drain any stale bytes that arrived during boot, then send header once
        time.sleep(0.1)
        ser.reset_input_buffer()

        print("Sending header, waiting for READY…", end=" ", flush=True)
        ser.write(header)
        resp = wait_line(ser, 10.0, "READY")
        if resp != "READY":
            sys.exit(f"\nError: expected READY, got {resp!r}")
        print("OK")

        # Stream firmware data with a progress bar
        chunk_size = 256
        sent = 0
        while sent < size:
            chunk = data[sent : sent + chunk_size]
            ser.write(chunk)
            sent += len(chunk)
            pct = sent * 100 // size
            bar = "#" * (pct // 5) + "." * (20 - pct // 5)
            print(f"\rUploading [{bar}] {pct:3d}%  {sent}/{size} B", end="", flush=True)

        # Send CRC (little-endian uint16)
        ser.write(struct.pack("<H", checksum))
        print()

        print("Waiting for OK…", end=" ", flush=True)
        resp = wait_line(ser, OK_TIMEOUT, "OK")

        if resp == "OK":
            print("OK")
            print("Panel flashing in progress — monitoring serial (Ctrl+C to stop)…")
            try:
                while True:
                    line = wait_line(ser, 2.0)
                    if line:
                        print(f"  {line}")
                        if line.startswith("[FLASHER] all panels") or line.startswith("[FLASHER] ERROR"):
                            break
            except KeyboardInterrupt:
                pass
            print("Done.")
        elif resp.startswith("ERR:"):
            sys.exit(f"\nController error: {resp[4:]}")
        else:
            sys.exit(f"\nUnexpected response: {resp!r}")


if __name__ == "__main__":
    main()
