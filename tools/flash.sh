#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [port] [--backup-only] [--now] [--baud N]" >&2
  echo "Backs up the stock firmware if there is no backup yet, then writes the firmware and the device's factory data." >&2
  echo "Start it, then power the chip up with IO0 grounded. It waits for the bootloader, and resumes a broken backup." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
case "$1" in -*|"") PORT_ARG="" ;; *) PORT_ARG="$1"; shift ;; esac
PORT=$(pick_port "$PORT_ARG") || exit 1
exec "$ROOT/tools/flash_when_ready.py" -d "$DEVICE" "$PORT" "$@"
