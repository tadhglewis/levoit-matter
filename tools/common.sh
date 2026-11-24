ROOT=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=purifier
if [ "$1" = "-d" ]; then
  [ -n "$2" ] || usage
  DEVICE="$2"
  shift 2
fi
case "$1" in -h | --help) usage ;; esac
case "$DEVICE" in
  *[!A-Za-z0-9_-]* | "") echo "device name may only use letters, digits, - and _" >&2; exit 1 ;;
esac
DEVICE_DIR="$ROOT/devices/$DEVICE"
SECRETS="$DEVICE_DIR/secrets"
if [ -d "$ROOT/firmware" ]; then FIRMWARE="$ROOT/firmware"; else FIRMWARE="$ROOT/build"; fi

esptool_path() {
  if [ -n "$ESPTOOL" ]; then echo "$ESPTOOL"; return; fi
  if [ ! -x "$ROOT/.venv/bin/esptool" ]; then
    python3 -m venv "$ROOT/.venv"
    "$ROOT/.venv/bin/pip" install -q esptool
  fi
  echo "$ROOT/.venv/bin/esptool"
}
