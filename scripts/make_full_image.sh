#!/usr/bin/env bash
# Build a single flashable image: custom bootloader + partitions + factory app.
# Flash with:  esptool.py --chip esp32s3 -p PORT write-flash 0x0 <this-file>
# Then:        esptool.py --chip esp32s3 -p PORT erase-region 0xe000 0x2000
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/.pio/build/basilauncher"
VER="$(grep -E 'BASILAUNCHER_VERSION=' "$ROOT/platformio.ini" | sed -E 's/.*\\"([0-9.]+)\\".*/\1/')"
OUT_DIR="${1:-$BUILD}"
mkdir -p "$OUT_DIR"

BOOT="$ROOT/bootloader/basil-bootloader.bin"
PART="$BUILD/partitions.bin"
APP="$BUILD/firmware.bin"
for f in "$BOOT" "$PART" "$APP"; do
  [[ -f "$f" ]] || { echo "missing $f — run: pio run -e basilauncher" >&2; exit 1; }
done

OUT="$OUT_DIR/basilauncher-${VER}-full.bin"
python3 -m esptool --chip esp32s3 merge-bin \
  -o "$OUT" \
  --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 "$BOOT" \
  0x8000 "$PART" \
  0x10000 "$APP"

# Convenience copy without version in the name
cp -f "$OUT" "$OUT_DIR/basilauncher-full.bin"
ls -la "$OUT" "$OUT_DIR/basilauncher-full.bin"
echo "Flash: esptool.py --chip esp32s3 -p PORT write-flash 0x0 $OUT"
echo "Then:  esptool.py --chip esp32s3 -p PORT erase-region 0xe000 0x2000"
