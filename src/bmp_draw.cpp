#include "bmp_draw.h"

#include "canvas.h"

#include <SD.h>
#include <cstdlib>
#include <cstring>

namespace {

// Floyd–Steinberg error diffusion so photos keep midtones on 1-bpp e-ink.
// Two row buffers of (copyW+2) ints — ~4 KB at 540 px, freed before return.
bool ditherRowToCanvas(const uint8_t* row, int copyW, int srcX0, int dstX0, int dy, int* cur,
                       int* nxt) {
  memset(nxt, 0, static_cast<size_t>(copyW + 2) * sizeof(int));
  for (int sx = 0; sx < copyW; ++sx) {
    const uint8_t* px = row + (srcX0 + sx) * 3;
    // BMP is BGR
    int lum = (static_cast<int>(px[2]) * 30 + static_cast<int>(px[1]) * 59 +
               static_cast<int>(px[0]) * 11) /
              100;
    lum += cur[sx + 1];
    if (lum < 0) lum = 0;
    if (lum > 255) lum = 255;
    const bool black = lum < 128;
    const int out = black ? 0 : 255;
    const int err = lum - out;
    canvasSetPixel(dstX0 + sx, dy, black);
    // Diffuse:      * 7/16 right
    //         3/16 5/16 1/16 below
    cur[sx + 2] += (err * 7) / 16;
    nxt[sx] += (err * 3) / 16;
    nxt[sx + 1] += (err * 5) / 16;
    nxt[sx + 2] += (err * 1) / 16;
  }
  return true;
}

}  // namespace

bool bmpDrawFile(const char* path, BmpAbortCheck abortCheck) {
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

  // +2 sentinel slots so left/right neighbors stay in-bounds.
  auto* errA = static_cast<int*>(calloc(static_cast<size_t>(copyW + 2), sizeof(int)));
  auto* errB = static_cast<int*>(calloc(static_cast<size_t>(copyW + 2), sizeof(int)));
  if (!errA || !errB) {
    free(row);
    free(errA);
    free(errB);
    f.close();
    return false;
  }

  int* cur = errA;
  int* nxt = errB;
  for (int sy = 0; sy < hAbs; ++sy) {
    if (abortCheck && (sy & 15) == 0 && abortCheck()) {
      free(row);
      free(errA);
      free(errB);
      f.close();
      return false;
    }
    const int fileRow = bottomUp ? (hAbs - 1 - sy) : sy;
    if (!f.seek(dataOff + static_cast<uint32_t>(fileRow) * static_cast<uint32_t>(rowBytes))) {
      free(row);
      free(errA);
      free(errB);
      f.close();
      return false;
    }
    if (f.read(row, rowBytes) != rowBytes) {
      free(row);
      free(errA);
      free(errB);
      f.close();
      return false;
    }
    if (sy < srcY0 || sy >= srcY0 + copyH) {
      // Still advance diffusion state? Skip rows outside crop — reset errors so
      // crop edges don't smear. Clear both buffers when skipping.
      memset(cur, 0, static_cast<size_t>(copyW + 2) * sizeof(int));
      memset(nxt, 0, static_cast<size_t>(copyW + 2) * sizeof(int));
      continue;
    }
    const int dy = dstY0 + (sy - srcY0);
    ditherRowToCanvas(row, copyW, srcX0, dstX0, dy, cur, nxt);
    int* tmp = cur;
    cur = nxt;
    nxt = tmp;
    memset(nxt, 0, static_cast<size_t>(copyW + 2) * sizeof(int));
  }

  free(row);
  free(errA);
  free(errB);
  f.close();
  return true;
}
