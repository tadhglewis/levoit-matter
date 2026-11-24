#!/usr/bin/env python3
"""Back up the stock flash and flash the firmware. tools/flash.sh runs this.

It listens on the port and starts the moment the chip enters download mode,
and it reads the backup in 256 KB chunks. Saved chunks are kept, so a dropped
connection costs one chunk and the next run resumes.

    ./tools/flash.sh [-d device] [port]
    ./tools/flash.sh --backup-only
    ./tools/flash.sh --now        # chip already in download mode

The port is found automatically when one USB serial adapter is plugged in.

The backup goes to devices/<device>/stock-backup.bin. The flash writes the
firmware from build/ (or firmware/ in a bundle) and devices/<device>/fctry.bin.
"""
import argparse
import glob
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
CHUNK = 0x40000
RETRIES = 2
ESPTOOL = os.environ.get("ESPTOOL") or os.path.join(VENV, "bin", "esptool")


def firmware_files(device_dir):
    firmware = os.path.join(ROOT, "firmware")
    if not os.path.isdir(firmware):
        firmware = os.path.join(ROOT, "build")

    def pick(flat, nested):
        path = os.path.join(firmware, flat)
        return path if os.path.exists(path) else os.path.join(firmware, nested)

    return [
        ("0x1000", pick("bootloader.bin", "bootloader/bootloader.bin")),
        ("0x8000", pick("partition-table.bin", "partition_table/partition-table.bin")),
        ("0x9000", os.path.join(firmware, "ota_data_initial.bin")),
        ("0x1A000", os.path.join(device_dir, "fctry.bin")),
        ("0x20000", os.path.join(firmware, "levoit_matter.bin")),
    ]


PORT_PATTERNS = ["/dev/cu.usbserial*", "/dev/cu.wchusbserial*", "/dev/cu.SLAB_USBtoUART*", "/dev/cu.PL2303*",
                 "/dev/cu.usbmodem*", "/dev/ttyUSB*", "/dev/ttyACM*"]


def pick_port(given):
    if given:
        return given
    ports = sorted({p for pattern in PORT_PATTERNS for p in glob.glob(pattern)})
    if len(ports) == 1:
        print(f"Using {ports[0]}")
        return ports[0]
    if not ports:
        sys.exit("No USB serial adapter found. Plug it in, or give the port.")
    sys.exit("More than one serial port. Give one of:\n  " + "\n  ".join(ports))


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
                print("\n\n>>> Chip is in download mode. Starting. <<<\n")
                time.sleep(0.5)
                return


def run(args):
    print("+ esptool " + " ".join(args) + "\n")
    return subprocess.run([ESPTOOL, *args]).returncode == 0


class Backup:
    def __init__(self, device_dir):
        self.path = os.path.join(device_dir, "stock-backup.bin")
        self.parts = os.path.join(device_dir, "backup-parts")

    def part(self, offset):
        return os.path.join(self.parts, f"part_{offset:08x}.bin")

    def part_ok(self, offset):
        p = self.part(offset)
        return os.path.exists(p) and os.path.getsize(p) == CHUNK

    def missing(self):
        return [o for o in range(0, FLASH_SIZE, CHUNK) if not self.part_ok(o)]

    def complete(self):
        return os.path.exists(self.path) and os.path.getsize(self.path) == FLASH_SIZE

    def join(self):
        partial = self.path + ".partial"
        with open(partial, "wb") as out:
            for o in range(0, FLASH_SIZE, CHUNK):
                with open(self.part(o), "rb") as f:
                    out.write(f.read())
        os.replace(partial, self.path)
        print(f"\nAll {FLASH_SIZE // CHUNK} chunks joined into {self.path}. "
              "Back it up somewhere that is not this laptop.\n")

    def read(self, common):
        os.makedirs(self.parts, exist_ok=True)
        total = FLASH_SIZE // CHUNK
        for offset in self.missing():
            n = offset // CHUNK + 1
            done = False
            for attempt in range(1 + RETRIES):
                print(f"\n=== Chunk {n}/{total} (offset {hex(offset)}), attempt {attempt + 1} ===")
                p = self.part(offset)
                if os.path.exists(p):
                    os.remove(p)
                ok = run(common + ["--before", "no-reset", "--after", "no-reset-stub",
                                   "read-flash", hex(offset), hex(CHUNK), p])
                if ok and self.part_ok(offset):
                    done = True
                    break
                if os.path.exists(p):
                    os.remove(p)
            if not done:
                saved = total - len(self.missing())
                print(f"\nChunk {n} failed. {saved}/{total} chunks are saved and will be kept. "
                      "Repeat the grounded power-up.")
                return False
        self.join()
        return True


def main():
    ap = argparse.ArgumentParser(description="Back up and flash over a flaky serial connection.")
    ap.add_argument("-d", "--device", default="purifier", help="device folder under devices/ (default: purifier)")
    ap.add_argument("port", nargs="?", default=os.environ.get("PORT"),
                    help="serial port; found automatically when one USB serial adapter is plugged in")
    ap.add_argument("--baud", default="115200", help="default 115200; higher is faster and flakier")
    ap.add_argument("--now", action="store_true", help="the chip is already in download mode")
    ap.add_argument("--backup-only", action="store_true", help="back up the stock flash, then stop")
    a = ap.parse_args()

    a.port = pick_port(a.port)
    if not a.device or not all(c.isalnum() or c in "-_" for c in a.device):
        ap.error("device name may only use letters, digits, - and _")

    device_dir = os.path.join(ROOT, "devices", a.device)
    os.makedirs(device_dir, exist_ok=True)
    backup = Backup(device_dir)
    files = firmware_files(device_dir)

    if not a.backup_only:
        missing = [f for _, f in files if not os.path.exists(f)]
        if os.path.join(device_dir, "fctry.bin") in missing:
            sys.exit(f"No factory data for '{a.device}'. Run ./tools/factory.sh -d {a.device} first, "
                     "or pass --backup-only.")
        if missing:
            sys.exit("No firmware: " + ", ".join(os.path.relpath(f, ROOT) for f in missing)
                     + ". Build it first, or pass --backup-only.")

    common = ["--chip", "esp32", "--port", a.port, "--baud", a.baud]
    skip_wait = a.now

    while True:
        if backup.complete():
            print(f"Using the existing {os.path.relpath(backup.path, ROOT)}.")
        else:
            left = len(backup.missing())
            if 0 < left < FLASH_SIZE // CHUNK:
                print(f"Resuming the backup: {FLASH_SIZE // CHUNK - left}/{FLASH_SIZE // CHUNK} chunks saved.")
            if not skip_wait:
                wait_for_download(a.port)
            skip_wait = False
            if not backup.read(common):
                continue

        if a.backup_only:
            print("Backup only: done.")
            return

        if not skip_wait:
            wait_for_download(a.port)
        skip_wait = False
        flash = common + ["--before", "no-reset", "--after", "no-reset", "write-flash",
                          "--flash-mode", "dio", "--flash-freq", "40m", "--flash-size", "4MB"]
        for offset, f in files:
            flash += [offset, f]
        if run(flash):
            print(f"\nFlashed. Release IO0, power cycle, then: ./tools/monitor.sh -d {a.device} {a.port}")
            return
        print("\nFlash failed. The backup is safe. Repeat the grounded power-up; listening again.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
