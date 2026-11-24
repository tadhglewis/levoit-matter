#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] <port>" >&2
  echo "First run backs up the stock firmware. Second run writes the firmware and the device's factory data." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
PORT="$1"
[ -n "$PORT" ] || usage
[ -e "$DEVICE_DIR/fctry.bin" ] || { echo "no factory data for '$DEVICE'. Run ./tools/factory.sh -d $DEVICE first." >&2; exit 1; }
[ -e "$FIRMWARE/levoit_matter.bin" ] || { echo "no firmware in $FIRMWARE. Build it first." >&2; exit 1; }
ESPTOOL=$(esptool_path)

if [ ! -e "$DEVICE_DIR/stock-backup.bin" ]; then
  echo "== Backing up the stock firmware to $DEVICE_DIR/stock-backup.bin (a few minutes)"
  "$ESPTOOL" --port "$PORT" flash-id
  "$ESPTOOL" --port "$PORT" read-flash 0 ALL "$DEVICE_DIR/stock-backup.partial"
  mv "$DEVICE_DIR/stock-backup.partial" "$DEVICE_DIR/stock-backup.bin"
  echo "== Backup done. Put the chip back in its bootloader (IO0 to ground, power cycle) and run this again."
  exit 0
fi

BOOTLOADER="$FIRMWARE/bootloader.bin"
TABLE="$FIRMWARE/partition-table.bin"
[ -e "$BOOTLOADER" ] || BOOTLOADER="$FIRMWARE/bootloader/bootloader.bin"
[ -e "$TABLE" ] || TABLE="$FIRMWARE/partition_table/partition-table.bin"

echo "== Writing factory data and firmware for '$DEVICE'"
"$ESPTOOL" --chip esp32 --port "$PORT" --baud 460800 write-flash \
  --flash-mode dio --flash-freq 40m --flash-size 4MB \
  0x1000 "$BOOTLOADER" \
  0x8000 "$TABLE" \
  0x9000 "$FIRMWARE/ota_data_initial.bin" \
  0x1A000 "$DEVICE_DIR/fctry.bin" \
  0x20000 "$FIRMWARE/levoit_matter.bin"
echo "== Done. Release IO0, power cycle, then: ./tools/monitor.sh -d $DEVICE $PORT"
