#!/usr/bin/env python3
"""Check the chip's flash against the stock backup, over a flaky serial connection.

Read-only: nothing is written to the chip. Start it before the grounded
power-up. It listens on the port and runs esptool verify-flash the moment the
chip enters download mode. If the connection drops it listens again.

    ./tools/verify_when_ready.py [-d device] <port>
    ./tools/verify_when_ready.py <port> --now        # chip already in download mode
    ./tools/verify_when_ready.py <port> --file other.bin

Compares against devices/<device>/stock-backup.bin unless --file is given.
Useful straight after a backup, before anything is flashed.
"""
import argparse
import os
import subprocess
import sys
import time
import venv

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENV = os.path.join(ROOT, ".venv")


def ensure_venv():
    python = os.path.join(VENV, "bin", "python")
    try:
        import serial  # noqa: F401
        return
    except ImportError:
        pass
    if os.path.realpath(sys.prefix) == os.path.realpath(VENV):
        sys.exit("pyserial is missing from .venv. Run: .venv/bin/pip install esptool")
    if not os.path.exists(os.path.join(VENV, "bin", "esptool")):
        venv.create(VENV, with_pip=True)
        subprocess.run([python, "-m", "pip", "install", "-q", "esptool"], check=True)
    os.execv(python, [python, os.path.abspath(__file__), *sys.argv[1:]])


ensure_venv()
import serial  # noqa: E402

MARKER = b"waiting for download"
FLASH_SIZE = 0x400000
ESPTOOL = os.environ.get("ESPTOOL") or os.path.join(VENV, "bin", "esptool")


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
    print("+ esptool " + " ".join(args) + "\n")
    proc = subprocess.Popen([ESPTOOL, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    text = ""
    for line in proc.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        text += line.lower()
    proc.wait()
    if proc.returncode == 0 and "verification successful" in text:
        return "ok"
    if "digest mismatch" in text or "verification failed" in text:
        return "mismatch"
    return "noconnect"


def main():
    ap = argparse.ArgumentParser(description="Verify the chip's flash against the stock backup.")
    ap.add_argument("-d", "--device", default="purifier", help="device folder under devices/ (default: purifier)")
    ap.add_argument("port", nargs="?", default=os.environ.get("PORT"), help="serial port, or set PORT")
    ap.add_argument("--baud", default="115200")
    ap.add_argument("--file", help="image to compare against (default: the device's stock-backup.bin)")
    ap.add_argument("--now", action="store_true", help="the chip is already in download mode")
    a = ap.parse_args()

    if not a.port:
        ap.error("give the serial port, or set PORT")
    if not a.device or not all(c.isalnum() or c in "-_" for c in a.device):
        ap.error("device name may only use letters, digits, - and _")

    image = a.file or os.path.join(ROOT, "devices", a.device, "stock-backup.bin")
    if not os.path.exists(image):
        sys.exit(f"{image} not found.")
    size = os.path.getsize(image)
    if size != FLASH_SIZE:
        sys.exit(f"{image} is {size} bytes, expected {FLASH_SIZE}. Not a complete backup.")

    args = ["--chip", "esp32", "--port", a.port, "--baud", a.baud,
            "--before", "no-reset", "--after", "no-reset", "verify-flash", "0x0", image]
    skip_wait = a.now
    while True:
        if not skip_wait:
            wait_for_download(a.port)
        skip_wait = False
        result = verify(args)
        if result == "ok":
            print(f"\n*** VERIFIED: the chip's flash matches {os.path.relpath(image, ROOT)} ***")
            return
        if result == "mismatch":
            print(f"\n*** MISMATCH: the chip's flash differs from {os.path.relpath(image, ROOT)} ***")
            print(f"If this was a fresh backup, delete devices/{a.device}/backup-parts/ and "
                  "stock-backup.bin and take it again before flashing.")
            sys.exit(1)
        print("\nCould not connect. Repeat the grounded power-up; listening again.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
