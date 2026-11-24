#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/factory"
VENV="$ROOT/.venv-mfg"
OFFSET=0x1A000
SIZE=0x6000

show() {
  echo
  echo "QR code image: $OUT/qrcode.png"
  awk -F, 'NR==2 { print "QR code text:  " $1; print "Manual code:   " $2 }' "$OUT/codes.csv"
  echo
  echo "Flash it once, with the chip in its bootloader:"
  echo "  esptool --port \$PORT write-flash $OFFSET $OUT/fctry.bin"
  echo
  echo "Keep the factory folder. It is the only copy of this purifier's setup code."
}

if [ -e "$OUT/fctry.bin" ] && [ "$1" != "--new" ]; then
  echo "Factory data already exists. Pass --new to replace it, which invalidates the old label."
  show
  exit 0
fi

if [ ! -x "$VENV/bin/esp-matter-mfg-tool" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q esp-matter-mfg-tool==1.0.29
fi

setting() { grep "^$1=" "$ROOT/sdkconfig.defaults" | cut -d= -f2; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
(cd "$WORK" && "$VENV/bin/esp-matter-mfg-tool" \
  -v "$(setting CONFIG_DEVICE_VENDOR_ID)" -p "$(setting CONFIG_DEVICE_PRODUCT_ID)" \
  -s "$SIZE" -dm 2 --outdir out >"$WORK/log" 2>&1) || { cat "$WORK/log"; exit 1; }

BIN=$(find "$WORK/out" -name '*-partition.bin')
DIR=$(dirname "$BIN")
mkdir -p "$OUT"
cp "$BIN" "$OUT/fctry.bin"
cp "$DIR"/*-qrcode.png "$OUT/qrcode.png"
cp "$DIR"/*-onb_codes.csv "$OUT/codes.csv"
show
