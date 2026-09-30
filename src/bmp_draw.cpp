#include "bmp_draw.h"

#include "canvas.h"

#include <SD.h>
#include <cstdlib>
#include <cstring>

bool bmpDrawFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54) {
    f.close();
    return false;
  }
  if (hdr[0] != 'B' || hdr[1] != 'M') {
    f.close();
    return false;
  }

  uint32_t dataOff = 0;
  int32_t width = 0, height = 0;
  uint16_t bpp = 0;
  uint32_t compression = 0;
  memcpy(&dataOff, hdr + 10, 4);
  memcpy(&width, hdr + 18, 4);
  memcpy(&height, hdr + 22, 4);
  memcpy(&bpp, hdr + 28, 2);
  memcpy(&compression, hdr + 30, 4);
  if (bpp != 24 || compression != 0 || width <= 0 || width > 2000 || height == 0) {
    f.close();
    return false;
  }

  const bool bottomUp = height > 0;
  const int hAbs = bottomUp ? height : -height;
  if (hAbs > 2000) {
    f.close();
    return false;
  }

  const int rowBytes = ((width * 3 + 3) / 4) * 4;
  auto* row = static_cast<uint8_t*>(malloc(static_cast<size_t>(rowBytes)));
  if (!row) {
    f.close();
    return false;
  }

  canvasClear();
  const int srcX0 = width > kScreenW ? (width - kScreenW) / 2 : 0;
  const int dstX0 = width < kScreenW ? (kScreenW - width) / 2 : 0;
  const int copyW = width < kScreenW ? width : kScreenW;
  const int srcY0 = hAbs > kScreenH ? (hAbs - kScreenH) / 2 : 0;
  const int dstY0 = hAbs < kScreenH ? (kScreenH - hAbs) / 2 : 0;
  const int copyH = hAbs < kScreenH ? hAbs : kScreenH;

  for (int sy = 0; sy < hAbs; ++sy) {
    const int fileRow = bottomUp ? (hAbs - 1 - sy) : sy;
    if (!f.seek(dataOff + static_cast<uint32_t>(fileRow) * static_cast<uint32_t>(rowBytes))) {
      free(row);
      f.close();
      return false;
    }
    if (f.read(row, rowBytes) != rowBytes) {
      free(row);
      f.close();
      return false;
    }
    if (sy < srcY0 || sy >= srcY0 + copyH) continue;
    const int dy = dstY0 + (sy - srcY0);
    for (int sx = 0; sx < copyW; ++sx) {
      const int srcX = srcX0 + sx;
      const uint8_t* px = row + srcX * 3;
      const int lum =
          (static_cast<int>(px[2]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[0]) * 11) /
          100;
      canvasSetPixel(dstX0 + sx, dy, lum < 128);
    }
  }
  free(row);
  f.close();
  return true;
}
