#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] <port>" >&2
  echo "Shows the serial log and appends it to devices/<device>/boot-log.txt. Exit with Ctrl-]." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
PORT="$1"
[ -n "$PORT" ] || usage
esptool_path >/dev/null
mkdir -p "$DEVICE_DIR"
"$ROOT/.venv/bin/python" -m serial.tools.miniterm --raw "$PORT" 115200 2>&1 | tee -a "$DEVICE_DIR/boot-log.txt"
