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

read_token() {
  if [ -n "$LEVOIT_OTA_TOKEN" ]; then
    echo "$LEVOIT_OTA_TOKEN"
  elif [ -e "$SECRETS/ota-token.current" ]; then
    cat "$SECRETS/ota-token.current"
  elif [ -e "$SECRETS/ota-token.txt" ]; then
    cat "$SECRETS/ota-token.txt"
  else
    echo "no update token for '$DEVICE' in $SECRETS" >&2
    return 1
  fi
}

serial_ports() {
  for p in /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.PL2303* \
           /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$p" ] && echo "$p"
  done
  return 0
}

pick_port() {
  if [ -n "$1" ]; then echo "$1"; return; fi
  if [ -n "$PORT" ]; then echo "$PORT"; return; fi
  ports=$(serial_ports)
  count=$(printf '%s' "$ports" | grep -c . || true)
  if [ "$count" = 1 ]; then echo "$ports"; return; fi
  if [ "$count" = 0 ]; then
    echo "no USB serial adapter found. Plug it in, or give the port." >&2
  else
    echo "more than one serial port. Give one of:" >&2
    printf '%s\n' "$ports" | sed 's/^/  /' >&2
  fi
  return 1
}

is_host() {
  case "$1" in *.bin) return 1 ;; esac
  printf '%s' "$1" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}$|^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$'
}

saved_host() {
  if [ -n "$LEVOIT_HOST" ]; then echo "$LEVOIT_HOST"; return; fi
  if [ -s "$DEVICE_DIR/host" ]; then cat "$DEVICE_DIR/host"; return; fi
  echo "no address saved for '$DEVICE'. Give it once, for example: $0 192.168.1.50" >&2
  return 1
}

remember_host() {
  mkdir -p "$DEVICE_DIR"
  printf '%s\n' "$1" > "$DEVICE_DIR/host"
}
