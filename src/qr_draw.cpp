#include "qr_draw.h"

#include "canvas.h"

#include <qrcode.h>
#include <cstdlib>
#include <cstring>

bool canvasDrawQr(int x, int y, int maxSize, const char* payload) {
  if (!payload || !payload[0] || maxSize < 21) return false;
  const size_t len = strlen(payload);

  int version = 3;
  if (len > 42) version = 4;
  if (len > 64) version = 5;
  if (len > 86) version = 6;
  if (len > 108) version = 8;
  if (len > 152) version = 10;
  if (len > 230) version = 12;

  const uint32_t bufSize = qrcode_getBufferSize(version);
  auto* buf = static_cast<uint8_t*>(malloc(bufSize));
  if (!buf) return false;

  QRCode qr;
  const int8_t res = qrcode_initText(&qr, buf, version, ECC_LOW, payload);
  if (res != 0) {
    free(buf);
    return false;
  }

  int px = maxSize / qr.size;
  if (px < 2) px = 2;
  const int drawn = qr.size * px;
  const int x0 = x + (maxSize - drawn) / 2;
  const int y0 = y + (maxSize - drawn) / 2;

  // Quiet zone / white background.
  canvasFillRect(x, y, maxSize, maxSize, false);
  for (uint8_t cy = 0; cy < qr.size; ++cy) {
    for (uint8_t cx = 0; cx < qr.size; ++cx) {
      if (qrcode_getModule(&qr, cx, cy)) {
        canvasFillRect(x0 + cx * px, y0 + cy * px, px, px, true);
      }
    }
  }
  free(buf);
  return true;
}
