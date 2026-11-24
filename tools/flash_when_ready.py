#!/usr/bin/env python3
"""Wait for the ESP32 to enter download mode, then back up the stock flash and flash the build.

The backup is read in small chunks (backup_parts/). Chunks that are already
saved are kept, so a failure only costs the chunk in progress and the next run
resumes where it left off. When every chunk is present they are joined into
stock-backup.bin.

Written for esptool v4.x (the one in your .venv). Run from the repo root:

    .venv/bin/python flash_when_ready.py --backup-only --baud 115200
    .venv/bin/python flash_when_ready.py --baud 115200      # backup (if needed) + flash
    .venv/bin/python flash_when_ready.py --now              # chip is already in download mode

If a chunk fails it retries a couple of times, then goes back to listening so
you can repeat the grounded power-up.
"""
import argparse
import os
import sys
import time

import esptool
import serial  # pyserial, installed with esptool

MARKER = b"waiting for download"
FLASH_SIZE = 0x400000  # 4 MB
CHUNK = 0x40000        # 256 KB per piece (about 25 s at 115200 baud)
PARTS_DIR = "backup_parts"
BACKUP = "stock-backup.bin"
BUILD = "build"
FILES = [
    ("0x1000", "bootloader/bootloader.bin"),
    ("0x8000", "partition_table/partition-table.bin"),
    ("0x9000", "ota_data_initial.bin"),
    ("0x20000", "levoit_matter.bin"),
]
RETRIES = 2


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
    print("+ esptool.py " + " ".join(args) + "\n")
    try:
        esptool.main(args)
        return True
    except SystemExit as e:
        return e.code in (0, None)
    except Exception as e:  # noqa: BLE001
        print(f"esptool error: {e}")
        return False


def part_path(offset):
    return os.path.join(PARTS_DIR, f"part_{offset:08x}.bin")


def part_ok(offset):
    p = part_path(offset)
    return os.path.exists(p) and os.path.getsize(p) == CHUNK


def missing_parts():
    return [o for o in range(0, FLASH_SIZE, CHUNK) if not part_ok(o)]


def backup_ok():
    return os.path.exists(BACKUP) and os.path.getsize(BACKUP) == FLASH_SIZE


def join_parts():
    with open(BACKUP, "wb") as out:
        for o in range(0, FLASH_SIZE, CHUNK):
            with open(part_path(o), "rb") as f:
                out.write(f.read())
    print(f"\nAll {FLASH_SIZE // CHUNK} chunks joined into {BACKUP} "
          f"({os.path.getsize(BACKUP)} bytes). Copy it somewhere safe, twice.\n")


def backup_chunks(common):
    """Read every missing chunk. Returns True when the full backup exists."""
    os.makedirs(PARTS_DIR, exist_ok=True)
    total = FLASH_SIZE // CHUNK
    for offset in missing_parts():
        n = offset // CHUNK + 1
        done = False
        for attempt in range(1 + RETRIES):
            print(f"\n=== Chunk {n}/{total} (offset {hex(offset)}), attempt {attempt + 1} ===")
            p = part_path(offset)
            if os.path.exists(p):
                os.remove(p)
            ok = run(common + ["--before", "no_reset", "--after", "no_reset_stub",
                               "read_flash", hex(offset), hex(CHUNK), p])
            if ok and part_ok(offset):
                done = True
                break
            if os.path.exists(p):
                os.remove(p)
        if not done:
            left = len(missing_parts())
            print(f"\nChunk {n} failed. {total - left}/{total} chunks are saved "
                  "and will be kept. Repeat the grounded power-up.")
            return False
    join_parts()
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=os.environ.get("PORT", "/dev/cu.PL2303G-USBtoUART10"))
    ap.add_argument("--baud", default="115200", help="esptool baud (default 115200; higher is flakier)")
    ap.add_argument("--now", action="store_true", help="skip waiting; chip is already in download mode")
    ap.add_argument("--backup-only", action="store_true", help="back up the stock flash, then stop")
    a = ap.parse_args()

    have_build = all(os.path.exists(os.path.join(BUILD, f)) for _, f in FILES)
    if not a.backup_only and not have_build:
        print(f"No complete build found in ./{BUILD}, so this run will only do the backup.")
        a.backup_only = True

    common = ["--chip", "esp32", "--port", a.port, "--baud", a.baud]
    skip_wait = a.now

    while True:
        if backup_ok():
            print(f"Using existing {BACKUP} (already 4 MB). Delete it and {PARTS_DIR}/ to take a new backup.")
        else:
            if missing_parts() and len(missing_parts()) < FLASH_SIZE // CHUNK:
                done = FLASH_SIZE // CHUNK - len(missing_parts())
                print(f"Resuming backup: {done}/{FLASH_SIZE // CHUNK} chunks already saved.")
            if not skip_wait:
                wait_for_download(a.port)
            skip_wait = False
            if not backup_chunks(common):
                continue
            if a.backup_only:
                print("Backup only: done.")
                return

        if a.backup_only:
            print("Backup only: done.")
            return

        if not skip_wait:
            wait_for_download(a.port)
        skip_wait = False
        flash_args = common + ["--before", "no_reset", "--after", "no_reset", "write_flash"]
        for offset, f in FILES:
            flash_args += [offset, os.path.join(BUILD, f)]
        if run(flash_args):
            print("\nFlashed. Power-cycle the chip (without IO0 grounded) to boot the new firmware.")
            return
        print("\nFlash failed. Your backup is safe. Repeat the grounded power-up; I'm listening again.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
