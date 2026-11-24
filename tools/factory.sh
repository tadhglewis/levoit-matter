#!/bin/sh
set -e
usage() {
  echo "usage: $0 [-d device] [--new]" >&2
  echo "Makes one purifier's setup code, QR label and update token in devices/<device>/." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
VENV="$ROOT/.venv-mfg"
SIZE=0x6000

show() {
  echo
  echo "Device folder:  $DEVICE_DIR"
  echo "QR code image:  $SECRETS/qrcode.png"
  awk -F, 'NR==2 { print "QR code text:   " $1; print "Manual code:    " $2 }' "$SECRETS/setup-codes.csv"
  echo "Update token:   $SECRETS/ota-token.txt"
  echo
  echo "Next: ./tools/flash.sh -d $DEVICE <port>"
  echo "Back up $DEVICE_DIR. It is the only copy of this purifier's setup code and update token."
}

if [ -e "$DEVICE_DIR/fctry.bin" ] && [ "$1" != "--new" ]; then
  echo "Factory data for '$DEVICE' already exists. Pass --new to replace it, which invalidates the old label and token."
  show
  exit 0
fi

if [ ! -x "$VENV/bin/esp-matter-mfg-tool" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q esp-matter-mfg-tool==1.0.29
fi

setting() {
  value=$(grep "^$1=" "$ROOT/sdkconfig.defaults" 2>/dev/null | cut -d= -f2)
  echo "${value:-$2}"
}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
(cd "$WORK" && "$VENV/bin/esp-matter-mfg-tool" \
  -v "$(setting CONFIG_DEVICE_VENDOR_ID 0xFFF1)" -p "$(setting CONFIG_DEVICE_PRODUCT_ID 0x8000)" \
  -s "$SIZE" -dm 2 --no-bin --outdir out >"$WORK/log" 2>&1) || { cat "$WORK/log"; exit 1; }

CSV=$(find "$WORK/out" -path '*/internal/partition.csv')
TOOL_OUT=$(dirname "$(dirname "$CSV")")
"$VENV/bin/python" - "$CSV" "$WORK/ota-token.txt" <<'PY'
import hashlib, secrets, sys
token = secrets.token_urlsafe(18)
salt = secrets.token_bytes(16)
digest = hashlib.sha256(salt + token.encode()).hexdigest()
with open(sys.argv[1], "a") as csv:
    csv.write(f"levoit,namespace,,\nota-salt,data,hex2bin,{salt.hex()}\nota-hash,data,hex2bin,{digest}\n")
with open(sys.argv[2], "w") as out:
    out.write(token + "\n")
PY
"$VENV/bin/python" -m esp_idf_nvs_partition_gen generate "$CSV" "$WORK/fctry.bin" "$SIZE" >"$WORK/log" 2>&1 || { cat "$WORK/log"; exit 1; }

mkdir -p "$SECRETS"
chmod 700 "$ROOT/devices" "$DEVICE_DIR" "$SECRETS"
rm -f "$SECRETS/ota-token.current"
cp "$WORK/fctry.bin" "$DEVICE_DIR/fctry.bin"
cp "$TOOL_OUT"/*-qrcode.png "$SECRETS/qrcode.png"
cp "$TOOL_OUT"/*-onb_codes.csv "$SECRETS/setup-codes.csv"
cp "$WORK/ota-token.txt" "$SECRETS/ota-token.txt"
chmod 600 "$DEVICE_DIR/fctry.bin" "$SECRETS"/*
show
