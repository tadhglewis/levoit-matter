#!/usr/bin/env python3
"""Wait for the ESP32 to enter download mode, then verify its flash against stock-backup.bin.

Read-only: nothing is written to the chip. Run it BEFORE the grounded power-up.
It listens like a serial monitor, and the moment it sees "waiting for download"
it hands over to esptool verify_flash. If the connection drops it goes back to
listening so you can just repeat the power-up.

From the repo root:

    .venv/bin/python verify_when_ready.py
    .venv/bin/python verify_when_ready.py --now      # chip is already in download mode
"""
import argparse
import os
import sys
import time

import esptool
import serial  # pyserial, installed with esptool

MARKER = b"waiting for download"
FLASH_SIZE = 0x400000


class Tee:
    """Print everything and keep a copy so we can read esptool's verdict."""

    def __init__(self, real):
        self.real, self.text = real, ""

    def write(self, s):
        self.text += s
        return self.real.write(s)

    def flush(self):
        self.real.flush()


def wait_for_download(port):
    print(f"\nListening on {port}. Power up the chip with IO0 grounded now...\n")
    buf = b""
    with serial.Serial(port, 115200, timeout=0.2) as s:
        while True:
            data = s.read(256)
            if not data:
                continue
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
            buf = (buf + data)[-2000:]
            if MARKER in buf:
                print("\n\n>>> Chip is in download mode. Verifying. <<<\n")
                time.sleep(0.5)
                return


def verify(args):
    real, tee = sys.stdout, None
    tee = Tee(real)
    sys.stdout = tee
    try:
        esptool.main(args)
    except SystemExit:
        pass
    except Exception as e:  # noqa: BLE001
        print(f"esptool error: {e}")
    finally:
        sys.stdout = real
    t = tee.text.lower()
    if "digest matched" in t or "verification successful" in t:
        return "ok"
    if "digest mismatch" in t or "verify failed" in t:
        return "mismatch"
    return "noconnect"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("PORT", "/dev/cu.PL2303G-USBtoUART10"))
    ap.add_argument("--baud", default="115200")
    ap.add_argument("--file", default="stock-backup.bin")
    ap.add_argument("--now", action="store_true", help="skip waiting; chip is already in download mode")
    a = ap.parse_args()

    if not os.path.exists(a.file):
        sys.exit(f"{a.file} not found. Run this from the folder that contains it.")
    size = os.path.getsize(a.file)
    if size != FLASH_SIZE:
        sys.exit(f"{a.file} is {size} bytes, expected {FLASH_SIZE}. Not a complete backup.")

    args = ["--chip", "esp32", "--port", a.port, "--baud", a.baud,
            "--before", "no_reset", "--after", "no_reset", "verify_flash", "0x0", a.file]
    skip_wait = a.now
    while True:
        if not skip_wait:
            wait_for_download(a.port)
        skip_wait = False
        result = verify(args)
        if result == "ok":
            print("\n*** VERIFIED: the chip's flash matches", a.file, "***")
            return
        if result == "mismatch":
            print("\n*** MISMATCH: the chip's flash differs from", a.file, "***")
            print("Redo the backup (delete backup_parts/ and stock-backup.bin) before flashing anything.")
            sys.exit(1)
        print("\nCould not connect. Repeat the grounded power-up; I'm listening again.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
