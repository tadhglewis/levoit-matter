#!/bin/sh
set -e
usage() {
  echo "usage: $0" >&2
  echo "Packs the built firmware and the tools into dist/, for flashing from a computer without the build tools." >&2
  exit 1
}
. "$(dirname "$0")/common.sh"
BUILD="$ROOT/build"
[ -e "$BUILD/levoit_matter.bin" ] || { echo "nothing built yet" >&2; exit 1; }
MODEL=$(sed -n 's/^CONFIG_LEVOIT_MODEL_\(.*\)=y$/\1/p' "$ROOT/sdkconfig" | tr 'A-Z' 'a-z')
NAME="levoit-matter-${MODEL:-unknown}"
OUT="$ROOT/dist/$NAME"

rm -rf "$OUT" "$ROOT/dist/$NAME.zip"
mkdir -p "$OUT/firmware" "$OUT/tools"
cp "$BUILD/bootloader/bootloader.bin" "$BUILD/partition_table/partition-table.bin" \
  "$BUILD/ota_data_initial.bin" "$BUILD/levoit_matter.bin" "$OUT/firmware/"
cp "$ROOT/tools/common.sh" "$ROOT/tools/factory.sh" "$ROOT/tools/flash.sh" \
  "$ROOT/tools/monitor.sh" "$ROOT/tools/ota.sh" "$ROOT/tools/log.sh" "$ROOT/tools/watch.sh" "$ROOT/tools/filter.sh" "$OUT/tools/"
cp "$ROOT/FLASHING.md" "$ROOT/LICENSE" "$OUT/"
(cd "$OUT" && shasum -a 256 firmware/*.bin > SHA256SUMS)
(cd "$ROOT/dist" && zip -q -r "$NAME.zip" "$NAME")
echo "$ROOT/dist/$NAME.zip"
