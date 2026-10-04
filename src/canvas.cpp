#include "canvas.h"

#include "font8x16.h"

#include <algorithm>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

namespace {

constexpr int kPhysW = 960;
constexpr int kPhysH = 540;

// Portrait mapping: logical (lx,ly) on 540x960 → panel (px,py) on 960x540.
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

inline void setPlanePixel(uint8_t* plane, uint16_t wb, int px, int py, bool set) {
  if (!plane || px < 0 || py < 0 || px >= kPhysW || py >= kPhysH) return;
  const uint32_t idx = static_cast<uint32_t>(py) * wb + static_cast<uint32_t>(px / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (px & 7));
  if (set) plane[idx] = static_cast<uint8_t>(plane[idx] | mask);
  else plane[idx] = static_cast<uint8_t>(plane[idx] & ~mask);
}

}  // namespace

EInkDisplay display(-1, -1, -1, -1, -1, -1);

namespace {
int gCleanEvery = 8;
bool gHoldCleanRefresh = false;
int gUiTextSize = 1;  // 0=Small 10×20, 1=Medium 12×24, 2=Large 14×28
uint8_t* gGrayLsb = nullptr;
uint8_t* gGrayMsb = nullptr;
bool gGrayUsed = false;
uint32_t gGhostDebt = 0;
bool gCleanPending = false;
bool gForceClean = true;
uint32_t gCleanDueMs = 0;

constexpr uint32_t kFullArea = static_cast<uint32_t>(kScreenW) * kScreenH;

const char* intentName(CanvasRefreshIntent intent) {
  switch (intent) {
    case CanvasRefreshIntent::InteractiveLocal: return "local";
    case CanvasRefreshIntent::InteractiveViewport: return "viewport";
    case CanvasRefreshIntent::Navigation: return "navigation";
    case CanvasRefreshIntent::Progress: return "progress";
    case CanvasRefreshIntent::StaticQuality: return "quality";
    case CanvasRefreshIntent::Sleep: return "sleep";
  }
  return "unknown";
}

CanvasRect clippedRect(CanvasRect r) {
  if (r.x < 0) {
    r.w += r.x;
    r.x = 0;
  }
  if (r.y < 0) {
    r.h += r.y;
    r.y = 0;
  }
  if (r.x >= kScreenW || r.y >= kScreenH || r.w <= 0 || r.h <= 0) return {0, 0, 0, 0};
  if (r.w > kScreenW - r.x) r.w = kScreenW - r.x;
  if (r.h > kScreenH - r.y) r.h = kScreenH - r.y;
  return r;
}

CanvasRect toPhysicalRect(CanvasRect logical) {
  logical = clippedRect(logical);
  if (logical.w <= 0 || logical.h <= 0) return {0, 0, 0, 0};
  return {logical.y, kPhysH - logical.x - logical.w, logical.h, logical.w};
}

void clearGrayPlanes() {
  const size_t bytes = display.getBufferSize();
  if (gGrayLsb) memset(gGrayLsb, 0, bytes);
  if (gGrayMsb) memset(gGrayMsb, 0, bytes);
  gGrayUsed = false;
}

void presentCleanFrame(const char* reason) {
  esp_task_wdt_reset();
  const uint32_t started = millis();
  const bool gray = gGrayUsed && gGrayLsb && gGrayMsb && display.supportsGrayFrame();
  if (gray) {
    display.copyGrayscaleBuffers(gGrayLsb, gGrayMsb);
    display.displayGrayscaleFrame(EInkDisplay::HALF_REFRESH, false);
  } else {
    display.displayBuffer(EInkDisplay::HALF_REFRESH, false);
  }
  Serial.printf("[epd] intent=%s mode=clean gray=%d refresh=%lu ms debt=%lu\n", reason, gray ? 1 : 0,
                static_cast<unsigned long>(millis() - started), static_cast<unsigned long>(gGhostDebt));
  gGhostDebt = 0;
  gCleanPending = false;
  gForceClean = false;
}
}  // namespace

void canvasBegin() {
  display.begin();
  const size_t bytes = display.getBufferSize();
  gGrayLsb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gGrayMsb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gGrayLsb || !gGrayMsb) {
    if (gGrayLsb) heap_caps_free(gGrayLsb);
    if (gGrayMsb) heap_caps_free(gGrayMsb);
    gGrayLsb = gGrayMsb = nullptr;
    Serial.println("[epd] grayscale masks unavailable; using black/white");
  }
  canvasClear();
}

void canvasClear() {
  display.clearScreen(0xFF);
  clearGrayPlanes();
}

void canvasRequestCleanRefresh() {
  gForceClean = true;
  gCleanPending = true;
  gCleanDueMs = millis();
}

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

void canvasPresent(EInkDisplay::RefreshMode mode) {
  esp_task_wdt_reset();
  const uint32_t started = millis();
  const bool clean = mode != EInkDisplay::FAST_REFRESH;
  const bool gray = clean && gGrayUsed && gGrayLsb && gGrayMsb && display.supportsGrayFrame();
  if (gray) {
    display.copyGrayscaleBuffers(gGrayLsb, gGrayMsb);
    display.displayGrayscaleFrame(mode, false);
  } else {
    display.displayBuffer(mode, false);
  }
  Serial.printf("[epd] intent=direct mode=%s gray=%d refresh=%lu ms\n", clean ? "clean" : "fast",
                gray ? 1 : 0, static_cast<unsigned long>(millis() - started));
  if (clean) {
    gGhostDebt = 0;
    gCleanPending = false;
    gForceClean = false;
  }
}

void canvasPresentAuto() {
  if (gForceClean && !gHoldCleanRefresh) {
    presentCleanFrame("requested");
    return;
  }
  canvasPresentFor(CanvasRefreshIntent::Navigation);
}

void canvasPresentFor(CanvasRefreshIntent intent, CanvasRect dirty) {
  dirty = clippedRect(dirty);
  if (dirty.w <= 0 || dirty.h <= 0) return;

  if (intent == CanvasRefreshIntent::StaticQuality || intent == CanvasRefreshIntent::Sleep) {
    presentCleanFrame(intentName(intent));
    return;
  }

  const CanvasRect native = toPhysicalRect(dirty);
  const bool full = dirty.x == 0 && dirty.y == 0 && dirty.w == kScreenW && dirty.h == kScreenH;
  esp_task_wdt_reset();
  const uint32_t started = millis();
  if (full) {
    display.displayBuffer(EInkDisplay::FAST_REFRESH, false);
  } else {
    display.displayWindow(native.x, native.y, native.w, native.h, false);
  }
  const uint32_t elapsed = millis() - started;
  const uint32_t area = static_cast<uint32_t>(dirty.w) * dirty.h;
  gGhostDebt = std::min<uint32_t>(UINT32_MAX - area, gGhostDebt) + area;

  uint32_t settleMs = 700;
  switch (intent) {
    case CanvasRefreshIntent::InteractiveLocal: settleMs = 1800; break;
    case CanvasRefreshIntent::InteractiveViewport: settleMs = 800; break;
    case CanvasRefreshIntent::Navigation: settleMs = 650; break;
    case CanvasRefreshIntent::Progress: settleMs = 2000; break;
    default: break;
  }
  if (gForceClean || gGhostDebt >= kFullArea * static_cast<uint32_t>(gCleanEvery)) {
    settleMs = std::min<uint32_t>(settleMs, 200);
  }
  // Clean only when explicitly requested, once the area-weighted fast budget
  // is spent, or when a stable screen has real-gray refinement waiting. This
  // keeps "Clean every N" meaningful and avoids paying a 2.6 s clean waveform
  // after every tiny update.
  const bool budgetSpent = gGhostDebt >= kFullArea * static_cast<uint32_t>(gCleanEvery);
  const bool grayRefinement = gGrayUsed && intent == CanvasRefreshIntent::Navigation;
  const bool pendingRefinement = gCleanPending && gGrayUsed;
  const bool scheduleClean = gForceClean || budgetSpent || grayRefinement || pendingRefinement;
  gCleanPending = scheduleClean;
  if (scheduleClean) {
    if ((grayRefinement || pendingRefinement) && !gForceClean && !budgetSpent) {
      settleMs = std::max<uint32_t>(settleMs, 1800);
    }
    gCleanDueMs = millis() + settleMs;
  }
  Serial.printf(
      "[epd] intent=%s mode=fast logical=%d,%d %dx%d native=%d,%d %dx%d refresh=%lu ms debt=%lu clean=%d due=%lu\n",
      intentName(intent), dirty.x, dirty.y, dirty.w, dirty.h, native.x, native.y, native.w, native.h,
      static_cast<unsigned long>(elapsed), static_cast<unsigned long>(gGhostDebt), scheduleClean ? 1 : 0,
      static_cast<unsigned long>(scheduleClean ? settleMs : 0));
}

void canvasServiceRefresh() {
  if (!gCleanPending || gHoldCleanRefresh) return;
  if (static_cast<int32_t>(millis() - gCleanDueMs) < 0) return;
  presentCleanFrame("settle");
}

void canvasCancelPendingClean() {
  gCleanPending = false;
  gForceClean = false;
}

void canvasSetHoldCleanRefresh(bool hold) {
  gHoldCleanRefresh = hold;
  if (!hold && gCleanPending && static_cast<int32_t>(millis() - gCleanDueMs) >= 0) {
    gCleanDueMs = millis();
  }
}

void canvasNuclearFlash() {
  gHoldCleanRefresh = false;
  // Drive every pixel off white through the fast bank, then scrub toward white.
  clearGrayPlanes();
  display.clearScreen(0x00);
  canvasPresent(EInkDisplay::FAST_REFRESH);
  display.clearScreen(0xFF);
  canvasPresent(EInkDisplay::HALF_REFRESH);
}

void canvasSetPixel(int x, int y, bool black) {
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  int px = 0, py = 0;
  toPhysical(x, y, px, py);
  const uint16_t wb = display.getDisplayWidthBytes();
  setPhysPixel(fb, wb, px, py, black);
  setPlanePixel(gGrayLsb, wb, px, py, false);
  setPlanePixel(gGrayMsb, wb, px, py, false);
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
      setPlanePixel(gGrayLsb, wb, px, py, false);
      setPlanePixel(gGrayMsb, wb, px, py, false);
    }
  }
}

void canvasFillGrayRect(int x, int y, int w, int h, bool light) {
  if (w <= 0 || h <= 0 || !gGrayLsb || !gGrayMsb) return;
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  const uint16_t wb = display.getDisplayWidthBytes();
  gGrayUsed = true;
  for (int ly = y; ly < y + h; ++ly) {
    for (int lx = x; lx < x + w; ++lx) {
      int px = 0, py = 0;
      toPhysical(lx, ly, px, py);
      setPhysPixel(fb, wb, px, py, false);
      setPlanePixel(gGrayLsb, wb, px, py, !light);
      setPlanePixel(gGrayMsb, wb, px, py, light);
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

namespace {

bool roundContains(int lx, int ly, int w, int h, int r) {
  if (lx < 0 || ly < 0 || lx >= w || ly >= h) return false;
  if (r <= 0 || r * 2 >= w || r * 2 >= h) return true;
  if (lx >= r && lx < w - r) return true;
  if (ly >= r && ly < h - r) return true;
  const int dx = lx < r ? (r - 1) - lx : lx - (w - r);
  const int dy = ly < r ? (r - 1) - ly : ly - (h - r);
  return dx >= 0 && dy >= 0 && dx * dx + dy * dy <= r * r;
}

bool pixelBlack(const uint8_t* fb, uint16_t wb, int px, int py) {
  const uint32_t idx = static_cast<uint32_t>(py) * wb + static_cast<uint32_t>(px / 8);
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (px & 7));
  return (fb[idx] & mask) == 0;
}

}  // namespace

void canvasInvertRoundRect(int x, int y, int w, int h, int r) {
  if (w <= 0 || h <= 0) return;
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  const uint16_t wb = display.getDisplayWidthBytes();
  for (int ly = 0; ly < h; ++ly) {
    if ((ly & 31) == 0) esp_task_wdt_reset();
    for (int lx = 0; lx < w; ++lx) {
      if (!roundContains(lx, ly, w, h, r)) continue;
      int px = 0, py = 0;
      toPhysical(x + lx, y + ly, px, py);
      if (px < 0 || py < 0 || px >= kPhysW || py >= kPhysH) continue;
      setPhysPixel(fb, wb, px, py, !pixelBlack(fb, wb, px, py));
      setPlanePixel(gGrayLsb, wb, px, py, false);
      setPlanePixel(gGrayMsb, wb, px, py, false);
    }
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

void canvasFillGrayRoundRect(int x, int y, int w, int h, int r, bool light) {
  if (w <= 0 || h <= 0) return;
  if (r <= 0 || r * 2 >= w || r * 2 >= h) {
    canvasFillGrayRect(x, y, w, h, light);
    return;
  }
  canvasFillGrayRect(x + r, y, w - 2 * r, h, light);
  canvasFillGrayRect(x, y + r, r, h - 2 * r, light);
  canvasFillGrayRect(x + w - r, y + r, r, h - 2 * r, light);
  for (int dy = 0; dy < r; ++dy) {
    for (int dx = 0; dx < r; ++dx) {
      if (dx * dx + dy * dy <= r * r) {
        canvasFillGrayRect(x + r - 1 - dx, y + r - 1 - dy, 1, 1, light);
        canvasFillGrayRect(x + w - r + dx, y + r - 1 - dy, 1, 1, light);
        canvasFillGrayRect(x + r - 1 - dx, y + h - r + dy, 1, 1, light);
        canvasFillGrayRect(x + w - r + dx, y + h - r + dy, 1, 1, light);
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

void canvasDrawString(int x, int y, const char* text, bool black, int scale, bool bold) {
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
        if (bold && scale <= 1) {
          if (ph == 1) canvasSetPixel(x0 + pw, y0, black);
          else canvasFillRect(x0 + pw, y0, 1, ph, black);
        }
      }
    }
    cx += cellW;
  }
}

namespace {
uint8_t* gCapture = nullptr;
size_t gCaptureBytes = 0;
}  // namespace

bool canvasCaptureFrame() {
  canvasReleaseCapture();
  uint8_t* fb = display.getFrameBuffer();
  const size_t n = display.getBufferSize();
  if (!fb || n == 0) return false;
  gCapture = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gCapture) gCapture = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_8BIT));
  if (!gCapture) return false;
  memcpy(gCapture, fb, n);
  gCaptureBytes = n;
  return true;
}

void canvasReleaseCapture() {
  if (gCapture) heap_caps_free(gCapture);
  gCapture = nullptr;
  gCaptureBytes = 0;
}

bool canvasHasCapture() { return gCapture != nullptr && gCaptureBytes > 0; }

const uint8_t* canvasCapturedFrame() { return gCapture; }

size_t canvasCapturedBytes() { return gCaptureBytes; }

void canvasRestoreCapture() {
  uint8_t* fb = display.getFrameBuffer();
  if (!fb || !gCapture || gCaptureBytes != display.getBufferSize()) return;
  memcpy(fb, gCapture, gCaptureBytes);
  clearGrayPlanes();
}

void canvasDiscardGray() { clearGrayPlanes(); }

void canvasTouchToLogical(float nx, float ny, int& x, int& y) {
  // Inverse of portrait tap mapping.
  const int px = static_cast<int>(nx * static_cast<float>(kPhysW - 1) + 0.5f);
  const int py = static_cast<int>(ny * static_cast<float>(kPhysH - 1) + 0.5f);
  x = (kPhysH - 1) - py;
  y = px;
}
