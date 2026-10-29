#!/bin/sh
# Render every launcher screen on the host, from the real src/ui.cpp and
# src/canvas.cpp, into PNGs. No device or flashing needed.
#
#   tools/ui_preview/run.sh [out dir]      (default: tools/ui_preview/out)
#
# Needs a C++20 compiler. PNG and contact-sheet output needs python3 + Pillow;
# without them you get the raw .pgm files.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${1:-$HERE/out}
mkdir -p "$OUT"
${CXX:-c++} -std=gnu++2a -O1 -w -I"$HERE/stub" -I"$ROOT/src" \
  "$HERE/render.cpp" "$HERE/device.cpp" "$ROOT/src/ui.cpp" "$ROOT/src/app_names.cpp" -o "$OUT/render"
${CXX:-c++} -std=gnu++2a -O1 -w -I"$HERE/stub" -I"$ROOT/src" \
  "$HERE/hits.cpp" "$HERE/device.cpp" "$ROOT/src/ui.cpp" "$ROOT/src/app_names.cpp" -o "$OUT/hits"
(cd "$OUT" && ./render && ./hits)
if python3 -c "import PIL" 2>/dev/null; then
  python3 "$HERE/sheet.py" "$OUT"
else
  echo "Pillow not found: left .pgm files in $OUT"
fi
