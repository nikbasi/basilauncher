#include "canvas.h"

#include "font8x16.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr int kPhysW = 960;
constexpr int kPhysH = 540;

// CrossPoint Portrait: logical (lx,ly) on 540x960 → panel (px,py) on 960x540.
inline void toPhysical(int lx, int ly, int& px, int& py) {
  px = ly;
  py = (kPhysH - 1) - lx;
}

inline void setPhysPixel(uint8_t* fb, uint16_t wb, int px, int py, bool black) {
  if (px < 0 || py < 0 || px >= kPhysW || py >= kPhysH || !fb) return;
  const uint32_t idx = static_cast<uint32_t>(py) * wb + static_cast<uint32_t>(px / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (px & 7));
  if (black) {
    fb[idx] = static_cast<uint8_t>(fb[idx] & ~mask);
  } else {
    fb[idx] = static_cast<uint8_t>(fb[idx] | mask);
  }
}

}  // namespace

EInkDisplay display(-1, -1, -1, -1, -1, -1);

void canvasBegin() {
  display.begin();
  canvasClear();
}

void canvasClear() { display.clearScreen(0xFF); }

namespace {
int gUntilCleanRefresh = 1;  // first paint scrub
int gCleanEvery = 8;
int gUiTextSize = 1;  // 0=Small 10×20, 1=Medium 12×24, 2=Large 14×28
}  // namespace

void canvasRequestCleanRefresh() { gUntilCleanRefresh = 1; }

void canvasSetCleanEvery(int n) {
  if (n < 1) n = 1;
  if (n > 30) n = 30;
  gCleanEvery = n;
}

int canvasCleanEvery() { return gCleanEvery; }

void canvasSetUiTextSize(int level) {
  if (level < 0) level = 0;
  if (level > 2) level = 2;
  gUiTextSize = level;
}

int canvasUiTextSize() { return gUiTextSize; }

int canvasBodyCellW() {
  switch (gUiTextSize) {
    case 0:
      return 10;
    case 2:
      return 14;
    default:
      return 12;
  }
}

int canvasBodyCellH() {
  switch (gUiTextSize) {
    case 0:
      return 20;
    case 2:
      return 28;
    default:
      return 24;
  }
}

int canvasTitleCellW() {
  switch (gUiTextSize) {
    case 0:
      return 16;
    case 2:
      return 20;
    default:
      return 18;
  }
}

int canvasTitleCellH() {
  switch (gUiTextSize) {
    case 0:
      return 32;
    case 2:
      return 40;
    default:
      return 36;
  }
}

void canvasPresent(EInkDisplay::RefreshMode mode) { display.displayBuffer(mode, false); }

void canvasPresentAuto() {
  // Same cadence as Aurora: FAST for ordinary UI, HALF every few frames to
  // scrub. Consecutive HALF/FULL on this panel skip unchanged white and leave
  // faint imprints of the previous screen.
  EInkDisplay::RefreshMode mode = EInkDisplay::FAST_REFRESH;
  if (gUntilCleanRefresh <= 1) {
    mode = EInkDisplay::HALF_REFRESH;
    gUntilCleanRefresh = gCleanEvery;
  } else {
    --gUntilCleanRefresh;
  }
  canvasPresent(mode);
}

void canvasSetPixel(int x, int y, bool black) {
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  int px = 0, py = 0;
  toPhysical(x, y, px, py);
  setPhysPixel(fb, display.getDisplayWidthBytes(), px, py, black);
}

void canvasFillRect(int x, int y, int w, int h, bool black) {
  if (w <= 0 || h <= 0) return;
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  const uint16_t wb = display.getDisplayWidthBytes();
  for (int ly = y; ly < y + h; ++ly) {
    for (int lx = x; lx < x + w; ++lx) {
      int px = 0, py = 0;
      toPhysical(lx, ly, px, py);
      setPhysPixel(fb, wb, px, py, black);
    }
  }
}

void canvasDrawRect(int x, int y, int w, int h, bool black) {
  if (w <= 0 || h <= 0) return;
  canvasFillRect(x, y, w, 1, black);
  canvasFillRect(x, y + h - 1, w, 1, black);
  canvasFillRect(x, y, 1, h, black);
  canvasFillRect(x + w - 1, y, 1, h, black);
}

void canvasDrawRoundRect(int x, int y, int w, int h, int r, bool black) {
  if (r <= 0 || r * 2 >= w || r * 2 >= h) {
    canvasDrawRect(x, y, w, h, black);
    return;
  }
  canvasFillRect(x + r, y, w - 2 * r, 1, black);
  canvasFillRect(x + r, y + h - 1, w - 2 * r, 1, black);
  canvasFillRect(x, y + r, 1, h - 2 * r, black);
  canvasFillRect(x + w - 1, y + r, 1, h - 2 * r, black);
  int f = 1 - r, ddfx = 1, ddfy = -2 * r, cx = 0, cy = r;
  while (cx < cy) {
    if (f >= 0) {
      cy--;
      ddfy += 2;
      f += ddfy;
    }
    cx++;
    ddfx += 2;
    f += ddfx;
    canvasSetPixel(x + r - cx, y + r - cy, black);
    canvasSetPixel(x + r - cy, y + r - cx, black);
    canvasSetPixel(x + w - 1 - r + cx, y + r - cy, black);
    canvasSetPixel(x + w - 1 - r + cy, y + r - cx, black);
    canvasSetPixel(x + r - cx, y + h - 1 - r + cy, black);
    canvasSetPixel(x + r - cy, y + h - 1 - r + cx, black);
    canvasSetPixel(x + w - 1 - r + cx, y + h - 1 - r + cy, black);
    canvasSetPixel(x + w - 1 - r + cy, y + h - 1 - r + cx, black);
  }
}

void canvasFillRoundRect(int x, int y, int w, int h, int r, bool black) {
  if (w <= 0 || h <= 0) return;
  if (r <= 0 || r * 2 >= w || r * 2 >= h) {
    canvasFillRect(x, y, w, h, black);
    return;
  }
  canvasFillRect(x + r, y, w - 2 * r, h, black);
  canvasFillRect(x, y + r, r, h - 2 * r, black);
  canvasFillRect(x + w - r, y + r, r, h - 2 * r, black);
  for (int dy = 0; dy < r; ++dy) {
    for (int dx = 0; dx < r; ++dx) {
      if (dx * dx + dy * dy <= r * r) {
        canvasSetPixel(x + r - 1 - dx, y + r - 1 - dy, black);
        canvasSetPixel(x + w - r + dx, y + r - 1 - dy, black);
        canvasSetPixel(x + r - 1 - dx, y + h - r + dy, black);
        canvasSetPixel(x + w - r + dx, y + h - r + dy, black);
      }
    }
  }
}

void canvasDrawLine(int x0, int y0, int x1, int y1, bool black) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    canvasSetPixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

int canvasTextHeight(int scale) {
  if (scale <= 1) return canvasBodyCellH();
  if (scale == 2) return canvasTitleCellH();
  return kFont8x16H * scale;
}

int canvasTextWidth(const char* text, int scale) {
  if (!text || scale < 1) return 0;
  int n = 0;
  for (const char* p = text; *p && *p != '\n'; ++p) ++n;
  if (scale <= 1) return n * canvasBodyCellW();
  if (scale == 2) return n * canvasTitleCellW();
  return n * kFont8x16W * scale;
}

void canvasDrawString(int x, int y, const char* text, bool black, int scale) {
  if (!text) return;
  if (scale < 1) scale = 1;
  int cellW = kFont8x16W * scale;
  int cellH = kFont8x16H * scale;
  int rowGap = scale;
  if (scale <= 1) {
    cellW = canvasBodyCellW();
    cellH = canvasBodyCellH();
    rowGap = 2;
  } else if (scale == 2) {
    cellW = canvasTitleCellW();
    cellH = canvasTitleCellH();
    rowGap = 2;
  }
  int cx = x;
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      cx = x;
      y += cellH + rowGap;
      continue;
    }
    char ch = *p;
    if (ch < kFont8x16First || ch > kFont8x16Last) ch = '?';
    const uint8_t* glyph = &kFont8x16[(ch - kFont8x16First) * kFont8x16H];
    for (int row = 0; row < kFont8x16H; ++row) {
      const uint8_t bits = pgm_read_byte(&glyph[row]);
      const int y0 = y + (row * cellH) / kFont8x16H;
      const int y1 = y + ((row + 1) * cellH) / kFont8x16H;
      const int ph = std::max(1, y1 - y0);
      for (int col = 0; col < kFont8x16W; ++col) {
        if (!(bits & (0x80u >> col))) continue;
        const int x0 = cx + (col * cellW) / kFont8x16W;
        const int x1 = cx + ((col + 1) * cellW) / kFont8x16W;
        const int pw = std::max(1, x1 - x0);
        if (pw == 1 && ph == 1) canvasSetPixel(x0, y0, black);
        else canvasFillRect(x0, y0, pw, ph, black);
      }
    }
    cx += cellW;
  }
}

void canvasTouchToLogical(float nx, float ny, int& x, int& y) {
  // Inverse of CrossPoint Portrait tapToLogical.
  const int px = static_cast<int>(nx * static_cast<float>(kPhysW - 1) + 0.5f);
  const int py = static_cast<int>(ny * static_cast<float>(kPhysH - 1) + 0.5f);
  x = (kPhysH - 1) - py;
  y = px;
}
