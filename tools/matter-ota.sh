#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] pair <pairing-code>" >&2
  echo "       $0 [-d device] update [image]" >&2
  echo "pair: once. In Apple Home, open the purifier's settings, tap Turn On Pairing Mode and pass the code it shows." >&2
  echo "update: sends a Matter OTA image, by default the one from the last build. Its version must be higher than the running one." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
TOOL="$ROOT/tools/matter-ota"
export MATTER_STORAGE_PATH="$DEVICE_DIR/matter"

if [ ! -d "$TOOL/node_modules" ]; then
  command -v pnpm >/dev/null || { echo "pnpm not found. Install it: https://pnpm.io/installation" >&2; exit 1; }
  pnpm install --silent --dir "$TOOL"
fi
mkdir -p "$MATTER_STORAGE_PATH"
chmod 700 "$MATTER_STORAGE_PATH"

case "$1" in
  pair)
    [ -n "$2" ] || usage
    exec node "$TOOL/matter-ota.mjs" pair "$2"
    ;;
  update)
    IMAGE="${2:-$FIRMWARE/levoit_matter-ota.bin}"
    [ -e "$IMAGE" ] || { echo "no image at $IMAGE. Build it first." >&2; exit 1; }
    exec node "$TOOL/matter-ota.mjs" update "$IMAGE"
    ;;
  *) usage ;;
esac
