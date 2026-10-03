#include "ui.h"

#include "board_hal.h"
#include "canvas.h"
#include "file_ops.h"
#include "qr_draw.h"
#include "wifi_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 18;
constexpr int kDockH = 88;           // message / confirm single-row dock
constexpr int kExplorerDockH = 64;   // slim icon toolbar
constexpr int kHomeFooterH = 64;     // Files chip strip
constexpr int kCardGap = 14;
constexpr int kRadius = 16;
constexpr int kTitle = 2;  // 16×32
constexpr int kBody = 1;   // 8×16
constexpr int kSmall = 1;
constexpr int kBtnH = 52;
constexpr int kBtnW = 120;
constexpr int kBtnRadius = 14;
constexpr int kRowH = 58;
constexpr int kBadge = 52;
constexpr int kActionBtnH = 52;  // full-width actions (Light / Scrub / About / Sleep)

// Shade layout — compact sheet under the status bar, not full-page.
constexpr int kGrabPillW = 64;
constexpr int kGrabPillH = 6;
constexpr int kGrabCueH = 22;  // chevron + pill (same on status bar and shade)
constexpr int kShadeGrabH = kGrabCueH + 6;
constexpr int kShadeRadius = 18;
constexpr int kStepBtn = 52;  // +/- steppers — large enough for finger taps
int gBrightX = 0, gBrightY = 0, gBrightW = 0, gBrightH = 0;
int gShadePanelBottom = 0;

int statusBarH() {
  // Time row, brand row, grabber cue — scales with UI text size.
  const int topPad = 8;
  const int timeH = canvasTextHeight(2);
  const int brandH = canvasTextHeight(kBody);
  const int gap = 6;
  const int botPad = 8;
  return topPad + timeH + gap + brandH + gap + kGrabCueH + botPad;
}

void drawChevronUp(int cx, int cy) {
  // Tip at smaller y (screen up). Thick strokes for e-ink.
  for (int t = 0; t < 3; ++t) {
    canvasDrawLine(cx - 12 + t, cy + 8, cx, cy + t, true);
    canvasDrawLine(cx + 12 - t, cy + 8, cx, cy + t, true);
  }
}

void drawChevronDown(int cx, int cy) {
  for (int t = 0; t < 3; ++t) {
    canvasDrawLine(cx - 12 + t, cy, cx, cy + 8 - t, true);
    canvasDrawLine(cx + 12 - t, cy, cx, cy + 8 - t, true);
  }
}

// Shared pull handle — same pill size both states.
// Closed: down cue (pull the sheet down). Open: up cue (push it away).
void drawShadeGrabCue(int topY, bool menuOpen) {
  const int cx = kScreenW / 2;
  const int pillX = cx - kGrabPillW / 2;
  if (menuOpen) {
    drawChevronUp(cx, topY);
    canvasFillRoundRect(pillX, topY + 10, kGrabPillW, kGrabPillH, kGrabPillH / 2, true);
  } else {
    canvasFillRoundRect(pillX, topY, kGrabPillW, kGrabPillH, kGrabPillH / 2, true);
    drawChevronDown(cx, topY + kGrabPillH + 3);
  }
}

void drawLightningBolt(int ox, int oy) {
  // Crisp geometric bolt (scanline-filled polygon). Vertices clockwise from tip.
  static constexpr int8_t kVx[] = {9, 4, 7, 1, 11, 8, 14};
  static constexpr int8_t kVy[] = {0, 9, 9, 20, 11, 11, 0};
  constexpr int kN = 7;
  constexpr int kH = 20;
  for (int y = 0; y <= kH; ++y) {
    int xs[kN];
    int n = 0;
    for (int i = 0; i < kN; ++i) {
      const int x0 = kVx[i];
      const int y0 = kVy[i];
      const int x1 = kVx[(i + 1) % kN];
      const int y1 = kVy[(i + 1) % kN];
      if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
        xs[n++] = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
      }
    }
    if (n < 2) continue;
    // Insertion sort — n is tiny.
    for (int a = 1; a < n; ++a) {
      const int v = xs[a];
      int b = a;
      while (b > 0 && xs[b - 1] > v) {
        xs[b] = xs[b - 1];
        --b;
      }
      xs[b] = v;
    }
    canvasFillRect(ox + xs[0], oy + y, xs[n - 1] - xs[0] + 1, 1, true);
  }
  // Outline edges so the zig-zag reads clearly on FAST refreshes.
  for (int i = 0; i < kN; ++i) {
    canvasDrawLine(ox + kVx[i], oy + kVy[i], ox + kVx[(i + 1) % kN], oy + kVy[(i + 1) % kN], true);
  }
}

struct ShadeGeom {
  int panelX = 0;
  int panelY = 0;
  int panelW = 0;
  int panelH = 0;
  int titleY = 0;
  int tileX = 0;
  int tileY = 0;
  int tileW = 0;
  int tileH = 0;
  int icon = 0;
  int frontRuleY = 0;
  int frontLabelY = 0;
  int sliderY = 0;
  int lightY = 0;
  int scrubY = 0;
  int settingsY = 0;
  int grabY = 0;
};

ShadeGeom shadeGeom() {
  ShadeGeom g;
  g.panelX = kPad / 2;
  g.panelY = statusBarH() + 6;
  g.panelW = kScreenW - kPad;

  int y = g.panelY + 12;
  g.titleY = y;
  y += canvasTextHeight(kTitle) + 12;
  g.icon = 72;
  const int labelW = canvasTextWidth("Wi-Fi", kBody);
  g.tileW = std::max(g.icon, labelW + 8);
  g.tileH = g.icon + 4 + canvasTextHeight(kBody);
  g.tileX = kPad + 8;
  g.tileY = y;
  y += g.tileH + 14;
  g.frontRuleY = y;
  y += 12;
  g.frontLabelY = y;
  y += canvasTextHeight(kBody) + 8;
  g.sliderY = y;
  y += kStepBtn + 10;
  g.lightY = y;
  y += kActionBtnH + 14;
  g.scrubY = y;
  y += kActionBtnH + 12;
  g.settingsY = y;
  y += kActionBtnH + 10;
  g.grabY = y;
  g.panelH = (g.grabY + kShadeGrabH + 8) - g.panelY;
  return g;
}

struct SettingsGeom {
  int titleY = 0;
  int textSizeY = 0;
  int sleepY = 0;
  int cleanY = 0;
  int cleanMinusX = 0, cleanPlusX = 0;
  int timeLabelY = 0;
  int hourY = 0;
  int minuteY = 0;
  int dateLabelY = 0;
  int yearY = 0;
  int monthY = 0;
  int dayY = 0;
  int rowMinusX = 0;
  int rowPlusX = 0;
  int wifiY = 0;
  int powerY = 0;
  int backY = 0;
  int tipY = 0;
};

SettingsGeom settingsGeom() {
  SettingsGeom g;
  constexpr int step = kStepBtn;
  constexpr int valueW = 120;
  g.rowMinusX = kScreenW - kPad - step * 2 - valueW;
  g.rowPlusX = kScreenW - kPad - step;
  g.cleanMinusX = g.rowMinusX;
  g.cleanPlusX = g.rowPlusX;

  int y = statusBarH() + kPad;
  g.titleY = y;
  y += canvasTextHeight(kTitle) + 16;

  g.textSizeY = y;
  y += step + 10;
  g.sleepY = y;
  y += step + 10;
  g.cleanY = y;
  y += step + 18;

  g.timeLabelY = y;
  y += canvasTextHeight(kBody) + 10;
  g.hourY = y;
  y += step + 8;
  g.minuteY = y;
  y += step + 18;

  g.dateLabelY = y;
  y += canvasTextHeight(kBody) + 10;
  g.yearY = y;
  y += step + 8;
  g.monthY = y;
  y += step + 8;
  g.dayY = y;
  y += step + 16;

  g.wifiY = -1;
  g.powerY = y;
  y += kActionBtnH + 12;
  g.backY = y;
  y += kActionBtnH + 12;
  g.tipY = y;
  return g;
}

void fillLightGrayRoundRect(int x, int y, int w, int h, int r) {
  // The ED047TC2 gray overlay can collapse large masks to black on this panel.
  // Keep changing surfaces sparse B/W; their outline is drawn by the caller.
  canvasFillRoundRect(x, y, w, h, r, false);
}

void drawSectionRule(int y, int panelX, int panelW) {
  canvasDrawLine(panelX + 14, y, panelX + panelW - 14, y, true);
}

int homeListTop() {
  // Status → pad → Apps title → gap → first card.
  return statusBarH() + 18 + canvasTextHeight(kTitle) + 18;
}

int slotCardH() {
  const int top = homeListTop();
  const int avail = kScreenH - top - kHomeFooterH - kPad;
  return (avail - (kSlotCount - 1) * kCardGap) / kSlotCount;
}

int slotCardY(int index) { return homeListTop() + index * (slotCardH() + kCardGap); }

void truncate(char* s, size_t maxChars) {
  if (!s) return;
  if (strlen(s) <= maxChars) return;
  if (maxChars < 3) {
    s[maxChars] = 0;
    return;
  }
  s[maxChars - 3] = '.';
  s[maxChars - 2] = '.';
  s[maxChars - 1] = '.';
  s[maxChars] = 0;
}

void drawBatteryGlyph(int x, int y, int percent, bool charging, bool plugged) {
  canvasDrawRect(x, y, 28, 14, true);
  canvasFillRect(x + 28, y + 4, 3, 6, true);
  const int fillW = std::max(0, std::min(24, (24 * percent) / 100));
  if (fillW > 0) canvasFillRect(x + 2, y + 2, fillW, 10, true);
  if (plugged && !charging) {
    // USB present but not charging (e.g. full): small plug tips above the cell
    canvasFillRect(x + 10, y - 4, 8, 3, true);
    canvasDrawLine(x + 12, y - 4, x + 12, y - 1, true);
    canvasDrawLine(x + 16, y - 4, x + 16, y - 1, true);
  }
}

void drawStatusArc(int cx, int cy, int r, bool ink) {
  auto point = [&](int deg, int& x, int& y) {
    const float rad = static_cast<float>(deg) * 0.0174532925f;
    x = cx + static_cast<int>(lroundf(cosf(rad) * static_cast<float>(r)));
    y = cy + static_cast<int>(lroundf(sinf(rad) * static_cast<float>(r)));
  };
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  point(210, x0, y0);
  for (int deg = 230; deg <= 330; deg += 25) {
    point(deg, x1, y1);
    canvasDrawLine(x0, y0, x1, y1, ink);
    x0 = x1;
    y0 = y1;
  }
}

// Compact fan. rightLimit is exclusive: every pixel stays at x < rightLimit,
// which is the left edge of the lightning slot.
void drawStatusWifiIcon(int rightLimit, int midY) {
  constexpr int kR = 10;
  const int cx = rightLimit - (kR + 2);
  const int arcY = midY + 3;
  canvasFillRect(cx - 1, arcY - 1, 3, 3, true);
  drawStatusArc(cx, arcY, 4, true);
  drawStatusArc(cx, arcY, 7, true);
  drawStatusArc(cx, arcY, kR, true);
}

void drawStatusBar(const FlashSpace& space, bool showClosedGrabber) {
  const int barH = statusBarH();
  canvasFillRect(0, 0, kScreenW, barH, false);
  const BoardClockInfo clock = boardClock();
  const BoardPowerInfo power = boardPower();

  const int timeY = 8;
  const int timeH = canvasTextHeight(2);
  canvasDrawString(kPad, timeY, clock.valid ? clock.time : "--:--", true, 2);
  if (clock.valid && clock.date[0]) {
    const int timeW = canvasTextWidth(clock.valid ? clock.time : "--:--", 2);
    const int dateY = timeY + (timeH - canvasTextHeight(kBody)) / 2;
    canvasDrawString(kPad + timeW + 14, dateY, clock.date, true, kBody);
  }

  char batt[20];
  if (power.known) {
    snprintf(batt, sizeof(batt), "%d%%", power.percent);
    // Keep bolt and battery fixed so cable changes do not shove the glyphs
    // left and leave FAST-refresh ghosts.
    constexpr int kBoltSlot = 18;
    constexpr int kBatterySpan = 31;
    constexpr int kTextGap = 3;
    const int textSlot = canvasTextWidth("100%", kBody);
    const int clusterRight = kScreenW - kPad;
    const int battX = clusterRight - textSlot - kTextGap - kBatterySpan;
    const int boltX = battX - kBoltSlot;
    const int battY = timeY + (timeH - 14) / 2;
    // Wi-Fi sits entirely left of the bolt slot so a charging cable cannot
    // draw the lightning through the fan. The slot itself stays put.
    constexpr int kWifiGap = 6;
    constexpr int kWifiSlot = 26;
    const int wifiRight = boltX - kWifiGap;
    const int wifiLeft = wifiRight - kWifiSlot;
    canvasFillRect(wifiLeft, battY - 8, (boltX - wifiLeft) + kBoltSlot + kBatterySpan + 2, 28, false);
    if (wifiIsActive()) drawStatusWifiIcon(wifiRight, battY + 7);
    drawBatteryGlyph(battX, battY, power.percent, power.charging, power.plugged);
    if (power.charging) drawLightningBolt(boltX, battY - 3);
    canvasDrawString(clusterRight - canvasTextWidth(batt, kBody),
                     timeY + (timeH - canvasTextHeight(kBody)) / 2, batt, true, kBody);
  } else {
    const char* na = "batt --";
    const int naX = kScreenW - kPad - canvasTextWidth(na, kBody);
    const int naY = timeY + (timeH - canvasTextHeight(kBody)) / 2;
    constexpr int kWifiGap = 6;
    constexpr int kWifiSlot = 26;
    const int wifiRight = naX - kWifiGap;
    const int wifiLeft = wifiRight - kWifiSlot;
    canvasFillRect(wifiLeft, timeY, kWifiSlot, timeH, false);
    if (wifiIsActive()) drawStatusWifiIcon(wifiRight, timeY + timeH / 2);
    canvasDrawString(naX, naY, na, true, kBody);
  }

  const int brandY = timeY + timeH + 6;
  char brand[40];
  snprintf(brand, sizeof(brand), "Basilauncher  v%s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, brandY, brand, true, kBody);

  // Closed menu: up cue only. Open shade draws its own down cue instead.
  if (showClosedGrabber) {
    const int grabY = brandY + canvasTextHeight(kBody) + 4;
    drawShadeGrabCue(grabY, /*menuOpen=*/false);
  }
  canvasDrawLine(0, barH - 1, kScreenW - 1, barH - 1, true);
}

// Short steppers (+/−) render at scale 2 so the glyph fills the fat tap target.
int btnLabelScale(const char* label) {
  if (label && label[0] && !label[1] && (label[0] == '+' || label[0] == '-')) return 2;
  return kBody;
}

void drawOutlineBtn(int x, int y, int w, int h, const char* label) {
  canvasDrawRoundRect(x, y, w, h, kBtnRadius, true);
  const int scale = btnLabelScale(label);
  const int tw = canvasTextWidth(label, scale);
  const int th = canvasTextHeight(scale);
  canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, true, scale, scale <= 1);
}

void drawFilledBtn(int x, int y, int w, int h, const char* label) {
  canvasFillRoundRect(x, y, w, h, kBtnRadius, true);
  const int scale = btnLabelScale(label);
  const int tw = canvasTextWidth(label, scale);
  const int th = canvasTextHeight(scale);
  canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, false, scale, scale <= 1);
}

void drawChromeOutlineBtn(int x, int y, int w, int h, const char* label) {
  canvasFillRoundRect(x, y, w, h, kBtnRadius, false);
  drawOutlineBtn(x, y, w, h, label);
}

void drawSlotCard(int index, const SlotInfo& slot) {
  const int x = kPad;
  const int y = slotCardY(index);
  const int w = kScreenW - 2 * kPad;
  const int h = slotCardH();

  if (slot.occupied) {
    // Light gray fill marks installed apps as tappable.
    fillLightGrayRoundRect(x, y, w, h, kRadius);
  }
  canvasDrawRoundRect(x, y, w, h, kRadius, true);

  canvasFillRoundRect(x + 16, y + 18, kBadge, kBadge, 12, true);
  char letter[2] = {slot.label[0], 0};
  const int lw = canvasTextWidth(letter, 2);
  canvasDrawString(x + 16 + (kBadge - lw) / 2, y + 18 + (kBadge - 32) / 2, letter, false, 2);

  const int textX = x + 16 + kBadge + 16;
  const int textW = w - (textX - x) - 16;
  if (slot.occupied) {
    char name[40];
    snprintf(name, sizeof(name), "%s", slot.name.c_str());
    truncate(name, 20);
    canvasDrawString(textX, y + 20, name, true, kTitle);

    char sz[48], used[24], cap[24];
    appsFormatBytes(slot.size ? slot.size : slot.capacity, used, sizeof(used));
    appsFormatBytes(slot.capacity, cap, sizeof(cap));
    snprintf(sz, sizeof(sz), "%s / %s", used, cap);
    canvasDrawString(textX, y + 56, sz, true, kBody);

    const int barY = y + 80;
    const int barW = textW;
    const int barH = 10;
    canvasFillRoundRect(textX, barY, barW, barH, 4, false);
    canvasDrawRoundRect(textX, barY, barW, barH, 4, true);
    size_t usedBytes = slot.size ? slot.size : slot.capacity;
    if (slot.capacity > 0 && usedBytes > 0) {
      int fill = static_cast<int>((static_cast<uint64_t>(barW - 4) * usedBytes) / slot.capacity);
      if (fill < 2) fill = 2;
      if (fill > barW - 4) fill = barW - 4;
      canvasFillRoundRect(textX + 2, barY + 2, fill, barH - 4, 3, true);
    }

    const int by = y + h - kBtnH - 14;
    drawChromeOutlineBtn(x + w - kBtnW - 14, by, kBtnW, kBtnH, "Clear");
  } else {
    canvasDrawString(textX, y + 20, "Empty slot", true, kTitle);
    char cap[40], capBuf[24];
    appsFormatBytes(slot.capacity, capBuf, sizeof(capBuf));
    snprintf(cap, sizeof(cap), "Up to %s", capBuf);
    canvasDrawString(textX, y + 56, cap, true, kBody);
    canvasDrawString(textX, y + 80, "Tap to install a .bin", true, kSmall);
    const int by = y + h - kBtnH - 14;
    drawFilledBtn(x + w - kBtnW - 14, by, kBtnW, kBtnH, "Install");
  }
}

void presentNavigation() { canvasPresentFor(CanvasRefreshIntent::Navigation); }
void presentQuality() { canvasPresentFor(CanvasRefreshIntent::StaticQuality); }

void drawWrappedBody(const char* body, int startY) {
  if (!body) return;
  char buf[128];
  strncpy(buf, body, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  int y = startY;
  const char* p = buf;
  for (int line = 0; line < 5 && *p; ++line) {
    char row[34];
    size_t n = 0;
    while (p[n] && n < 32) ++n;
    memcpy(row, p, n);
    row[n] = 0;
    canvasDrawString(kPad, y, row, true, kBody);
    p += n;
    y += 22;
  }
}

}  // namespace

void uiShadeBrightnessTrack(int& x, int& y, int& w, int& h) {
  x = gBrightX;
  y = gBrightY;
  w = gBrightW;
  h = gBrightH;
}

int uiBrightnessFromTouchX(int touchX) {
  if (gBrightW <= 0) return boardBrightness();
  int pct = ((touchX - gBrightX) * 100) / gBrightW;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

void uiDrawSplash() {
  canvasClear();
  const char* name = "Basilauncher";
  constexpr int nameScale = 3;
  const int nameW = canvasTextWidth(name, nameScale);
  const int nameH = canvasTextHeight(nameScale);
  char ver[24];
  snprintf(ver, sizeof(ver), "v%s", BASILAUNCHER_VERSION);
  const int verW = canvasTextWidth(ver, 2);
  const int verH = canvasTextHeight(2);

  const int boxW = std::min(kScreenW - 40, nameW + 56);
  const int boxH = nameH + verH + 56;
  const int boxX = (kScreenW - boxW) / 2;
  const int boxY = (kScreenH - boxH) / 2 - 20;

  canvasFillRoundRect(boxX - 3, boxY - 3, boxW + 6, boxH + 6, 22, true);
  canvasFillRoundRect(boxX, boxY, boxW, boxH, 20, false);
  canvasDrawRoundRect(boxX, boxY, boxW, boxH, 20, true);

  canvasDrawString((kScreenW - nameW) / 2, boxY + 22, name, true, nameScale);
  canvasDrawString((kScreenW - verW) / 2, boxY + 22 + nameH + 14, ver, true, 2);

  canvasPresent(EInkDisplay::FULL_REFRESH);
}

void filesChipRect(int& x, int& y, int& w, int& h) {
  // Full footer height, wide tap target — one tap should open Files reliably.
  x = kPad;
  y = kScreenH - kHomeFooterH;
  w = kScreenW - 2 * kPad - 140;  // leave room for the free-space label
  h = kHomeFooterH;
}

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space, bool stable) {
  canvasClear();
  drawStatusBar(space, true);

  const int appsY = statusBarH() + 18;
  canvasDrawString(kPad, appsY, "Apps", true, kTitle);

  for (int i = 0; i < kSlotCount; ++i) drawSlotCard(i, slots[i]);

  canvasDrawLine(0, kScreenH - kHomeFooterH, kScreenW - 1, kScreenH - kHomeFooterH, true);

  int fx, fy, fw, fh;
  filesChipRect(fx, fy, fw, fh);
  // Inset the drawn chip slightly so it doesn't collide with the footer rule.
  drawChromeOutlineBtn(fx, fy + 8, std::min(fw, 160), fh - 16, "Files");

  char freeBuf[24], freeLine[40];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  snprintf(freeLine, sizeof(freeLine), "free %s", freeBuf);
  const int freeY = fy + (fh - canvasTextHeight(kSmall)) / 2;
  canvasDrawString(kScreenW - kPad - canvasTextWidth(freeLine, kSmall), freeY, freeLine, true, kSmall);
  // Boot arrives from a full splash refresh; a FAST home leaves the charging
  // bolt ghosted under the battery until the settle scrub. Paint clean once.
  if (stable) presentQuality();
  else presentNavigation();
}

void uiRedrawStatusBar(const FlashSpace& space, bool showClosedGrabber) {
  const int h = statusBarH();
  canvasFillRect(0, 0, kScreenW, h, false);
  drawStatusBar(space, showClosedGrabber);
  // A windowed fast refresh drives every pixel in the strip, and the fast LUT
  // parks that white as grey. A full-frame diff only drives the digits that
  // actually changed, so the rest of the bar stays white.
  canvasPresent(EInkDisplay::FAST_REFRESH);
}

void uiRedrawHomeStatus(const FlashSpace& space) { uiRedrawStatusBar(space, true); }

constexpr int kExplorerRowH = 72;

int uiExplorerRowHeight() { return kExplorerRowH; }

int explorerListTop() { return statusBarH() + 116; }
int explorerListBottom() { return kScreenH - kExplorerDockH - 8; }

int uiExplorerVisibleRows() {
  return std::max(1, (explorerListBottom() - explorerListTop()) / kExplorerRowH);
}

enum class EntryIcon { Folder, Bin, Image, Text, File };

EntryIcon entryIconKind(const DirEntry& e) {
  if (e.isDir) return EntryIcon::Folder;
  if (fileOpsIsBin(e.name.c_str())) return EntryIcon::Bin;
  if (fileOpsIsImage(e.name.c_str())) return EntryIcon::Image;
  if (fileOpsIsText(e.name.c_str())) return EntryIcon::Text;
  return EntryIcon::File;
}

void drawIconFolder(int cx, int cy, bool ink) {
  canvasFillRect(cx - 12, cy - 4, 14, 4, ink);
  canvasDrawRoundRect(cx - 14, cy - 2, 28, 16, 3, ink);
  canvasDrawLine(cx - 14, cy + 2, cx + 14, cy + 2, ink);
}

void drawIconFile(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 10, cy - 12, 20, 26, 2, ink);
  canvasDrawLine(cx + 2, cy - 12, cx + 10, cy - 4, ink);
  canvasDrawLine(cx + 2, cy - 12, cx + 2, cy - 4, ink);
  canvasDrawLine(cx + 2, cy - 4, cx + 10, cy - 4, ink);
}

void drawIconImage(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 14, cy - 10, 28, 22, 3, ink);
  canvasFillRect(cx - 6, cy - 4, 5, 5, ink);
  canvasDrawLine(cx - 12, cy + 8, cx - 2, cy - 0, ink);
  canvasDrawLine(cx - 2, cy - 0, cx + 4, cy + 5, ink);
  canvasDrawLine(cx + 4, cy + 5, cx + 12, cy - 2, ink);
}

void drawIconText(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 10, cy - 12, 20, 26, 2, ink);
  canvasDrawLine(cx - 5, cy - 5, cx + 5, cy - 5, ink);
  canvasDrawLine(cx - 5, cy + 1, cx + 5, cy + 1, ink);
  canvasDrawLine(cx - 5, cy + 7, cx + 2, cy + 7, ink);
}

void drawIconBin(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 14, cy - 8, 28, 18, 4, ink);
  canvasDrawString(cx - 10, cy - 6, "BIN", ink, kSmall);
}

void drawEntryIcon(EntryIcon kind, int cx, int cy, bool ink) {
  switch (kind) {
    case EntryIcon::Folder:
      drawIconFolder(cx, cy, ink);
      break;
    case EntryIcon::Image:
      drawIconImage(cx, cy, ink);
      break;
    case EntryIcon::Text:
      drawIconText(cx, cy, ink);
      break;
    case EntryIcon::Bin:
      drawIconBin(cx, cy, ink);
      break;
    default:
      drawIconFile(cx, cy, ink);
      break;
  }
}

void drawIconClose(int cx, int cy, bool ink) {
  // Thick X
  for (int d = -1; d <= 1; ++d) {
    canvasDrawLine(cx - 11 + d, cy - 11, cx + 11 + d, cy + 11, ink);
    canvasDrawLine(cx + 11 + d, cy - 11, cx - 11 + d, cy + 11, ink);
  }
}

void drawWifiArc(int cx, int cy, int r, bool ink) {
  auto point = [&](int deg, int& x, int& y) {
    const float rad = static_cast<float>(deg) * 0.0174532925f;
    x = cx + static_cast<int>(lroundf(cosf(rad) * static_cast<float>(r)));
    y = cy + static_cast<int>(lroundf(sinf(rad) * static_cast<float>(r)));
  };
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  point(205, x0, y0);
  for (int deg = 220; deg <= 340; deg += 15) {
    point(deg, x1, y1);
    canvasDrawLine(x0, y0, x1, y1, ink);
    canvasDrawLine(x0, y0 + 1, x1, y1 + 1, ink);
    x0 = x1;
    y0 = y1;
  }
}

void drawIconWifi(int cx, int cy, bool ink) {
  canvasFillRect(cx - 2, cy + 8, 5, 5, ink);
  drawWifiArc(cx, cy + 10, 8, ink);
  drawWifiArc(cx, cy + 10, 14, ink);
  drawWifiArc(cx, cy + 10, 20, ink);
}

void drawQsTile(int x, int y, int icon, int tileW, bool filled, void (*iconFn)(int, int, bool),
                const char* label) {
  const int iconX = x + (tileW - icon) / 2;
  if (filled) canvasFillRoundRect(iconX, y, icon, icon, 18, true);
  else {
    canvasFillRoundRect(iconX, y, icon, icon, 18, false);
    canvasDrawRoundRect(iconX, y, icon, icon, 18, true);
  }
  iconFn(iconX + icon / 2, y + icon / 2, !filled);
  const int lw = canvasTextWidth(label, kBody);
  canvasDrawString(x + (tileW - lw) / 2, y + icon + 4, label, true, kBody, true);
}

void drawIconBack(int cx, int cy, bool ink) {
  canvasDrawLine(cx + 6, cy - 10, cx - 8, cy, ink);
  canvasDrawLine(cx - 8, cy, cx + 6, cy + 10, ink);
  canvasDrawLine(cx - 8, cy, cx + 10, cy, ink);
}

void drawIconUp(int cx, int cy, bool ink) {
  canvasDrawLine(cx, cy - 10, cx - 10, cy + 2, ink);
  canvasDrawLine(cx, cy - 10, cx + 10, cy + 2, ink);
  canvasDrawLine(cx, cy - 10, cx, cy + 12, ink);
}

void drawIconMenu(int cx, int cy, bool ink) {
  canvasFillRoundRect(cx - 3, cy - 12, 6, 6, 2, ink);
  canvasFillRoundRect(cx - 3, cy - 3, 6, 6, 2, ink);
  canvasFillRoundRect(cx - 3, cy + 6, 6, 6, 2, ink);
}

void drawIconPaste(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 10, cy - 6, 20, 18, 2, ink);
  canvasDrawRect(cx - 6, cy - 12, 12, 8, ink);
  canvasDrawLine(cx - 4, cy + 0, cx + 4, cy + 0, ink);
  canvasDrawLine(cx - 4, cy + 5, cx + 4, cy + 5, ink);
}

void drawIconBtn(int x, int y, int w, int h, void (*icon)(int, int, bool), bool filled) {
  if (filled) canvasFillRoundRect(x, y, w, h, 12, true);
  else {
    canvasFillRoundRect(x, y, w, h, 12, false);
    canvasDrawRoundRect(x, y, w, h, 12, true);
  }
  icon(x + w / 2, y + h / 2, !filled);
}

bool hitIconBtn(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && x < bx + w && y >= by && y < by + h;
}

struct SheetItem {
  const char* label;
  UiHit::Kind kind;
  void (*icon)(int, int, bool);
};

// Larger glyphs for the action-sheet grid (~1.7x).
void drawIconCopyLg(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 4, cy - 16, 22, 28, 3, ink);
  canvasDrawRoundRect(cx - 18, cy - 6, 22, 28, 3, ink);
}

void drawIconCutLg(int cx, int cy, bool ink) {
  for (int d = -1; d <= 1; ++d) {
    canvasDrawLine(cx - 14 + d, cy - 14, cx + 14 + d, cy + 14, ink);
    canvasDrawLine(cx + 14 + d, cy - 14, cx - 14 + d, cy + 14, ink);
  }
  canvasDrawLine(cx - 14, cy - 14, cx - 4, cy - 4, ink);
  canvasDrawLine(cx + 14, cy - 14, cx + 4, cy - 4, ink);
}

void drawIconPasteLg(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 14, cy - 6, 28, 26, 3, ink);
  canvasDrawRect(cx - 8, cy - 16, 16, 12, ink);
  canvasDrawLine(cx - 6, cy + 2, cx + 6, cy + 2, ink);
  canvasDrawLine(cx - 6, cy + 9, cx + 6, cy + 9, ink);
}

void drawIconRenameLg(int cx, int cy, bool ink) {
  canvasDrawLine(cx - 14, cy + 14, cx + 8, cy - 12, ink);
  canvasDrawLine(cx + 8, cy - 12, cx + 14, cy - 6, ink);
  canvasDrawLine(cx - 14, cy + 14, cx - 6, cy + 14, ink);
  canvasFillRect(cx + 6, cy - 14, 8, 8, ink);
}

void drawIconDeleteLg(int cx, int cy, bool ink) {
  canvasDrawLine(cx - 12, cy - 12, cx + 12, cy - 12, ink);
  canvasDrawRect(cx - 14, cy - 6, 28, 26, ink);
  canvasDrawLine(cx - 4, cy + 0, cx - 4, cy + 14, ink);
  canvasDrawLine(cx + 4, cy + 0, cx + 4, cy + 14, ink);
  canvasFillRect(cx - 6, cy - 18, 12, 6, ink);
}

void drawIconNewLg(int cx, int cy, bool ink) {
  canvasFillRect(cx - 16, cy - 4, 18, 5, ink);
  canvasDrawRoundRect(cx - 18, cy - 1, 36, 22, 4, ink);
  canvasDrawLine(cx + 10, cy - 14, cx + 10, cy + 4, ink);
  canvasDrawLine(cx + 2, cy - 6, cx + 18, cy - 6, ink);
}

void drawIconCopy(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 4, cy - 10, 16, 20, 2, ink);
  canvasDrawRoundRect(cx - 12, cy - 4, 16, 20, 2, ink);
}

void drawIconCut(int cx, int cy, bool ink) {
  canvasDrawLine(cx - 10, cy - 10, cx + 10, cy + 10, ink);
  canvasDrawLine(cx + 10, cy - 10, cx - 10, cy + 10, ink);
  canvasDrawLine(cx - 10, cy - 10, cx - 4, cy - 4, ink);
  canvasDrawLine(cx + 10, cy - 10, cx + 4, cy - 4, ink);
}

void drawIconRename(int cx, int cy, bool ink) {
  canvasDrawLine(cx - 10, cy + 10, cx + 6, cy - 8, ink);
  canvasDrawLine(cx + 6, cy - 8, cx + 10, cy - 4, ink);
  canvasDrawLine(cx - 10, cy + 10, cx - 6, cy + 10, ink);
}

void drawIconDelete(int cx, int cy, bool ink) {
  canvasDrawLine(cx - 8, cy - 8, cx + 8, cy - 8, ink);
  canvasDrawRect(cx - 10, cy - 4, 20, 18, ink);
  canvasDrawLine(cx - 3, cy + 0, cx - 3, cy + 10, ink);
  canvasDrawLine(cx + 3, cy + 0, cx + 3, cy + 10, ink);
  canvasFillRect(cx - 4, cy - 12, 8, 4, ink);
}

void drawIconNewFileLg(int cx, int cy, bool ink) {
  canvasDrawRoundRect(cx - 14, cy - 18, 28, 36, 3, ink);
  canvasDrawLine(cx + 2, cy - 18, cx + 14, cy - 6, ink);
  canvasDrawLine(cx + 2, cy - 18, cx + 2, cy - 6, ink);
  canvasDrawLine(cx + 2, cy - 6, cx + 14, cy - 6, ink);
  canvasDrawLine(cx + 10, cy - 4, cx + 10, cy + 12, ink);
  canvasDrawLine(cx + 2, cy + 4, cx + 18, cy + 4, ink);
}

// Shared action-sheet geometry (3 cols x 3 rows; 7 actions used).
constexpr int kSheetCols = 3;
constexpr int kSheetCount = 7;
constexpr int kSheetCellH = 100;
constexpr int kSheetPad = 14;
constexpr int kSheetGrab = 22;
constexpr int kSheetGap = 10;

int explorerSheetH() {
  return kSheetGrab + 10 + 3 * kSheetCellH + 2 * kSheetGap + kSheetPad;
}

void explorerSheetCell(int index, int& x, int& y, int& w, int& h) {
  const int sheetH = explorerSheetH();
  const int sheetY = kScreenH - sheetH;
  const int gridTop = sheetY + kSheetGrab + 8;
  const int usable = kScreenW - 2 * kPad;
  w = (usable - (kSheetCols - 1) * kSheetGap) / kSheetCols;
  h = kSheetCellH;
  const int col = index % kSheetCols;
  const int row = index / kSheetCols;
  x = kPad + col * (w + kSheetGap);
  y = gridTop + row * (h + kSheetGap);
}

int explorerSelectionCount(const ExplorerDrawState& st) {
  if (st.selectedIndices) return static_cast<int>(st.selectedIndices->size());
  return st.selected >= 0 ? 1 : 0;
}

bool explorerEntrySelected(const ExplorerDrawState& st, int index) {
  if (!st.selectedIndices) return index == st.selected;
  return std::find(st.selectedIndices->begin(), st.selectedIndices->end(), index) !=
         st.selectedIndices->end();
}

void drawExplorerDock(const ExplorerDrawState& st) {
  const int dockY = kScreenH - kExplorerDockH;
  canvasFillRect(0, dockY, kScreenW, kExplorerDockH, false);
  canvasDrawLine(0, dockY, kScreenW - 1, dockY, true);

  const int btnH = 48;
  const int btnY = dockY + (kExplorerDockH - btnH) / 2;
  int textX = kPad;
  if (st.clipboardHas) {
    drawIconBtn(kPad, btnY, 64, btnH, drawIconPaste, true);
    textX += 74;
  }

  const int count = explorerSelectionCount(st);
  if (count > 0) {
    char selected[28];
    if (count == 1) snprintf(selected, sizeof(selected), "1 selected");
    else snprintf(selected, sizeof(selected), "%d selected", count);
    canvasDrawString(textX, btnY + (btnH - canvasTextHeight(kSmall)) / 2, selected, true, kSmall);
  } else if (st.multiSelect) {
    canvasDrawString(textX, btnY + (btnH - canvasTextHeight(kSmall)) / 2, "Select items", true, kSmall);
  }

  const int menuX = kScreenW - kPad - 64;
  if (count == 1 && !st.sheetOpen) {
    constexpr int openW = 112;
    drawFilledBtn(menuX - 12 - openW, btnY, openW, btnH, "Open");
  }
  drawIconBtn(menuX, btnY, 64, btnH, drawIconMenu, st.sheetOpen);
}

void drawExplorerViewport(const std::vector<DirEntry>& entries, const ExplorerDrawState& st) {
  const int listTop = explorerListTop();
  const int listBottom = explorerListBottom();
  const int visible = uiExplorerVisibleRows();
  const int cardW = kScreenW - 2 * kPad;
  const int start = st.scroll;
  const int end = std::min(static_cast<int>(entries.size()), start + visible);
  constexpr int kIconCol = 44;

  canvasFillRect(0, listTop, kScreenW, listBottom - listTop, false);
  if (entries.empty()) {
    canvasDrawString(kPad, listTop + 24, boardSdOk() ? "Empty folder" : "Insert SD card", true, kBody);
  } else {
    for (int i = start; i < end; ++i) {
      const DirEntry& e = entries[i];
      const int rowY = listTop + (i - start) * kExplorerRowH;
      const bool sel = explorerEntrySelected(st, i);
      if (sel) canvasFillRoundRect(kPad, rowY, cardW, kExplorerRowH - 8, 12, true);
      else canvasDrawRoundRect(kPad, rowY, cardW, kExplorerRowH - 8, 12, true);

      const bool ink = !sel;
      drawEntryIcon(entryIconKind(e), kPad + 26, rowY + (kExplorerRowH - 8) / 2, ink);

      const int textX = kPad + kIconCol + 8;
      const int nameMaxW = cardW - kIconCol - 24 - (e.isDir ? 0 : 72);
      const int charsPerLine = std::max(6, nameMaxW / canvasBodyCellW());
      char nameBuf[96];
      snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
      truncate(nameBuf, static_cast<size_t>(charsPerLine));
      canvasDrawString(textX, rowY + 14, nameBuf, ink, kBody);

      if (!e.isDir) {
        char sz[24];
        appsFormatBytes(e.size, sz, sizeof(sz));
        const bool over =
            st.mode == ExplorerMode::Install && fileOpsIsBin(e.name.c_str()) && e.size > st.maxBytes;
        char right[28];
        if (over) snprintf(right, sizeof(right), "!%s", sz);
        else snprintf(right, sizeof(right), "%s", sz);
        canvasDrawString(kPad + cardW - 14 - canvasTextWidth(right, kSmall), rowY + 18, right, ink, kSmall);
      } else {
        canvasDrawString(kPad + cardW - 28, rowY + 18, ">", ink, kTitle);
      }
    }
  }

  if (entries.size() > static_cast<size_t>(visible)) {
    const int trackH = listBottom - listTop - 20;
    const int thumbH = std::max(16, trackH * visible / static_cast<int>(entries.size()));
    const int maxScroll = std::max(1, static_cast<int>(entries.size()) - visible);
    const int thumbY = listTop + 10 + (trackH - thumbH) * st.scroll / maxScroll;
    canvasFillRoundRect(kScreenW - 10, thumbY, 4, thumbH, 2, true);
  }
}

void uiDrawExplorer(const std::vector<DirEntry>& entries, const ExplorerDrawState& st,
                    const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);

  char title[48];
  if (st.mode == ExplorerMode::Install) {
    if (st.targetSlot >= 0 && st.targetSlot < kSlotCount) {
      snprintf(title, sizeof(title), "Install -> %c", 'A' + st.targetSlot);
    } else {
      snprintf(title, sizeof(title), "Install");
    }
  } else {
    snprintf(title, sizeof(title), "Files");
  }
  canvasDrawString(kPad, statusBarH() + 12, title, true, kTitle);

  // Close (X) top-right — leaves the explorer.
  constexpr int kCloseW = 52;
  constexpr int kCloseH = 48;
  const int closeX = kScreenW - kPad - kCloseW;
  const int closeY = statusBarH() + 8;
  drawIconBtn(closeX, closeY, kCloseW, kCloseH, drawIconClose, false);

  // Up chip replaces the path bar: icon + truncated current path.
  const bool atRoot = !st.currentPath || appsIsRootDir(st.currentPath);
  char pathLine[48];
  snprintf(pathLine, sizeof(pathLine), "%s", st.currentPath ? st.currentPath : "/");
  if (strlen(pathLine) > 22) {
    char shortP[48];
    snprintf(shortP, sizeof(shortP), "...%s", pathLine + strlen(pathLine) - 20);
    snprintf(pathLine, sizeof(pathLine), "%s", shortP);
  }
  const int upY = statusBarH() + 62;
  const int upH = 44;
  const int upW = kScreenW - 2 * kPad - (st.clipboardHas || st.mode == ExplorerMode::Install ? 110 : 0);
  if (atRoot) {
    canvasDrawRoundRect(kPad, upY, upW, upH, 12, true);
    canvasDrawString(kPad + 14, upY + (upH - canvasTextHeight(kBody)) / 2, pathLine, true, kBody);
  } else {
    drawIconBtn(kPad, upY, upH, upH, drawIconUp, false);
    canvasDrawRoundRect(kPad + upH + 8, upY, upW - upH - 8, upH, 12, true);
    canvasDrawString(kPad + upH + 20, upY + (upH - canvasTextHeight(kBody)) / 2, pathLine, true, kBody);
  }

  if (st.mode == ExplorerMode::Install) {
    char maxBuf[24], hint[40];
    appsFormatBytes(st.maxBytes, maxBuf, sizeof(maxBuf));
    snprintf(hint, sizeof(hint), "max %s", maxBuf);
    canvasDrawString(kScreenW - kPad - canvasTextWidth(hint, kSmall), upY + (upH - canvasTextHeight(kSmall)) / 2,
                     hint, true, kSmall);
  } else if (st.clipboardHas) {
    char clip[24];
    if (st.clipboardCount > 1) {
      snprintf(clip, sizeof(clip), st.clipboardCut ? "%u cut" : "%u copied",
               static_cast<unsigned>(st.clipboardCount));
    } else {
      snprintf(clip, sizeof(clip), "%s", st.clipboardCut ? "cut ready" : "copied");
    }
    canvasDrawString(kScreenW - kPad - canvasTextWidth(clip, kSmall), upY + (upH - canvasTextHeight(kSmall)) / 2,
                     clip, true, kSmall);
  }

  drawExplorerViewport(entries, st);

  drawExplorerDock(st);

  if (st.sheetOpen) {
    const int sheetH = explorerSheetH();
    const int sheetY = kScreenH - sheetH;
    canvasFillRect(0, sheetY, kScreenW, sheetH, false);
    canvasDrawLine(0, sheetY, kScreenW - 1, sheetY, true);
    canvasFillRoundRect(kScreenW / 2 - 28, sheetY + 8, 56, 6, 3, true);

    const SheetItem items[kSheetCount] = {
        {"Copy", UiHit::Kind::ExplorerCopy, drawIconCopyLg},
        {"Cut", UiHit::Kind::ExplorerCut, drawIconCutLg},
        {"Paste", UiHit::Kind::ExplorerPaste, drawIconPasteLg},
        {"Rename", UiHit::Kind::ExplorerRename, drawIconRenameLg},
        {"Delete", UiHit::Kind::ExplorerDelete, drawIconDeleteLg},
        {"New Folder", UiHit::Kind::ExplorerNew, drawIconNewLg},
        {"New File", UiHit::Kind::ExplorerNewFile, drawIconNewFileLg},
    };
    for (int i = 0; i < kSheetCount; ++i) {
      int cx, cy, cw, ch;
      explorerSheetCell(i, cx, cy, cw, ch);
      canvasDrawRoundRect(cx, cy, cw, ch, 14, true);
      items[i].icon(cx + cw / 2, cy + ch / 2 - 12, true);
      const int tw = canvasTextWidth(items[i].label, kBody);
      canvasDrawString(cx + (cw - tw) / 2, cy + ch - 24, items[i].label, true, kBody);
    }
  }

  presentNavigation();
}

void uiRedrawExplorerViewport(const std::vector<DirEntry>& entries, const ExplorerDrawState& st) {
  drawExplorerViewport(entries, st);
  drawExplorerDock(st);

  const int top = explorerListTop();
  canvasPresentFor(CanvasRefreshIntent::InteractiveViewport, {0, top, kScreenW, kScreenH - top});
}

UiHit uiHitExplorer(int x, int y, int entryCount, int scroll, bool canGoUp, bool sheetOpen,
                    bool clipboardHas, int selectedCount) {
  UiHit hit;

  if (!sheetOpen && y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }

  if (sheetOpen) {
    const int sheetH = explorerSheetH();
    const int sheetY = kScreenH - sheetH;
    if (y < sheetY) {
      hit.kind = UiHit::Kind::ExplorerSheetDismiss;
      return hit;
    }
    static const UiHit::Kind kinds[kSheetCount] = {
        UiHit::Kind::ExplorerCopy,   UiHit::Kind::ExplorerCut,     UiHit::Kind::ExplorerPaste,
        UiHit::Kind::ExplorerRename, UiHit::Kind::ExplorerDelete,  UiHit::Kind::ExplorerNew,
        UiHit::Kind::ExplorerNewFile,
    };
    for (int i = 0; i < kSheetCount; ++i) {
      int cx, cy, cw, ch;
      explorerSheetCell(i, cx, cy, cw, ch);
      if (hitIconBtn(x, y, cx, cy, cw, ch)) {
        hit.kind = kinds[i];
        return hit;
      }
    }
    hit.kind = UiHit::Kind::ExplorerSheetDismiss;
    return hit;
  }

  // Close X
  constexpr int kCloseW = 52;
  constexpr int kCloseH = 48;
  const int closeX = kScreenW - kPad - kCloseW;
  const int closeY = statusBarH() + 8;
  if (hitIconBtn(x, y, closeX, closeY, kCloseW, kCloseH)) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }

  // Up chip / path row
  const int upY = statusBarH() + 62;
  const int upH = 44;
  const int upW = kScreenW - 2 * kPad - (clipboardHas ? 110 : 0);
  if (y >= upY && y < upY + upH && x >= kPad && x < kPad + upW) {
    hit.kind = canGoUp ? UiHit::Kind::GoUp : UiHit::Kind::None;
    return hit;
  }

  const int dockY = kScreenH - kExplorerDockH;
  if (y >= dockY) {
    const int btnH = 48;
    const int btnY = dockY + (kExplorerDockH - btnH) / 2;
    if (clipboardHas && hitIconBtn(x, y, kPad, btnY, 64, btnH)) {
      hit.kind = UiHit::Kind::ExplorerPaste;
      return hit;
    }
    const int menuX = kScreenW - kPad - 64;
    if (selectedCount == 1 && hitIconBtn(x, y, menuX - 12 - 112, btnY, 112, btnH)) {
      hit.kind = UiHit::Kind::ExplorerOpen;
      return hit;
    }
    if (hitIconBtn(x, y, menuX, btnY, 64, btnH)) {
      hit.kind = UiHit::Kind::ExplorerMore;
      return hit;
    }
    return hit;
  }

  const int listTop = explorerListTop();
  const int visible = uiExplorerVisibleRows();
  const int cardW = kScreenW - 2 * kPad;

  for (int row = 0; row < visible; ++row) {
    const int idx = scroll + row;
    if (idx >= entryCount) break;
    const int rowY = listTop + row * kExplorerRowH;
    if (y >= rowY && y < rowY + kExplorerRowH - 8 && x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::SelectEntry;
      hit.index = idx;
      return hit;
    }
  }
  return hit;
}

void drawShadeControls() {
  const ShadeGeom g = shadeGeom();
  gShadePanelBottom = g.panelY + g.panelH;

  fillLightGrayRoundRect(g.panelX, g.panelY, g.panelW, g.panelH, kShadeRadius);
  canvasDrawRoundRect(g.panelX, g.panelY, g.panelW, g.panelH, kShadeRadius, true);

  // Labels sit on the gray sheet — only interactive chrome gets a white fill.
  canvasDrawString(kPad + 8, g.titleY, "Quick settings", true, kTitle);

  drawQsTile(g.tileX, g.tileY, g.icon, g.tileW, wifiIsActive(), drawIconWifi, "Wi-Fi");

  drawSectionRule(g.frontRuleY, g.panelX, g.panelW);
  canvasDrawString(kPad + 8, g.frontLabelY, "Frontlight", true, kBody);

  const bool hasLight = boardHasFrontlight();
  const int bright = boardBrightness();
  const bool lightOn = boardFrontlightOn();

  drawChromeOutlineBtn(kPad + 8, g.sliderY, kStepBtn, kStepBtn, "-");
  drawChromeOutlineBtn(kScreenW - kPad - 8 - kStepBtn, g.sliderY, kStepBtn, kStepBtn, "+");

  gBrightX = kPad + 8 + kStepBtn + 12;
  gBrightY = g.sliderY + 8;
  gBrightW = kScreenW - 2 * (kPad + 8 + kStepBtn + 12);
  gBrightH = kStepBtn - 16;
  canvasFillRoundRect(gBrightX, gBrightY, gBrightW, gBrightH, 8, false);
  canvasDrawRoundRect(gBrightX, gBrightY, gBrightW, gBrightH, 8, true);
  if (hasLight && lightOn && bright > 0) {
    const int fill = std::max(4, (gBrightW * bright) / 100);
    canvasFillRoundRect(gBrightX + 2, gBrightY + 2, fill - 4, gBrightH - 4, 6, true);
  }

  char pct[16];
  snprintf(pct, sizeof(pct), hasLight ? "%d%%" : "n/a", bright);
  canvasDrawString(kPad + 8, g.lightY + (kActionBtnH - canvasTextHeight(kBody)) / 2, pct, true, kBody);

  if (hasLight) {
    const int bx = kScreenW - kPad - 8 - 160;
    if (lightOn) drawFilledBtn(bx, g.lightY, 160, kActionBtnH, "Light on");
    else drawChromeOutlineBtn(bx, g.lightY, 160, kActionBtnH, "Light off");
  }

  drawChromeOutlineBtn(kPad + 8, g.scrubY, kScreenW - 2 * kPad - 16, kActionBtnH, "Scrub screen now");
  drawChromeOutlineBtn(kPad + 8, g.settingsY, kScreenW - 2 * kPad - 16, kActionBtnH, "Settings");

  // Open menu: down cue only (same pill size as the closed status-bar cue).
  drawShadeGrabCue(g.grabY + 2, /*menuOpen=*/true);
}

void uiDrawShade(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, false);
  drawShadeControls();
  presentNavigation();
}

void uiRedrawShadeControls(const FlashSpace& space) {
  const ShadeGeom g = shadeGeom();
  canvasFillRect(0, 0, kScreenW, g.panelY + g.panelH, false);
  drawStatusBar(space, false);
  drawShadeControls();
  // Same as the status bar: a windowed fast refresh of this white sheet leaves
  // it grey. A full-frame diff only drives the slider and the percent.
  canvasPresent(EInkDisplay::FAST_REFRESH);
}

UiHit uiHitShade(int x, int y) {
  UiHit hit;
  const ShadeGeom g = shadeGeom();
  const int panelBottom = g.panelY + g.panelH;

  if (y >= g.grabY || y >= panelBottom || y < g.panelY) {
    hit.kind = UiHit::Kind::CloseShade;
    return hit;
  }

  if (y >= g.tileY && y < g.tileY + g.tileH) {
    if (x >= g.tileX && x < g.tileX + g.tileW) {
      hit.kind = UiHit::Kind::Wifi;
      return hit;
    }
  }

  if (y >= g.sliderY && y < g.sliderY + kStepBtn) {
    if (x >= kPad + 8 && x < kPad + 8 + kStepBtn) {
      hit.kind = UiHit::Kind::BrightnessMinus;
      return hit;
    }
    if (x >= kScreenW - kPad - 8 - kStepBtn && x < kScreenW - kPad - 8) {
      hit.kind = UiHit::Kind::BrightnessPlus;
      return hit;
    }
    if (x >= gBrightX && x < gBrightX + gBrightW) {
      hit.kind = UiHit::Kind::BrightnessSlider;
      hit.value = uiBrightnessFromTouchX(x);
      return hit;
    }
  }

  if (boardHasFrontlight() && y >= g.lightY && y < g.lightY + kActionBtnH) {
    const int bx = kScreenW - kPad - 8 - 160;
    if (x >= bx && x < bx + 160) {
      hit.kind = UiHit::Kind::LightToggle;
      return hit;
    }
  }

  if (y >= g.scrubY && y < g.scrubY + kActionBtnH && x >= kPad + 8 && x < kScreenW - kPad - 8) {
    hit.kind = UiHit::Kind::ScrubNow;
    return hit;
  }

  if (y >= g.settingsY && y < g.settingsY + kActionBtnH && x >= kPad + 8 && x < kScreenW - kPad - 8) {
    hit.kind = UiHit::Kind::Settings;
    return hit;
  }

  return hit;
}

void drawSettingsStepper(int y, const char* label, const char* value, int minusX, int plusX) {
  constexpr int step = kStepBtn;
  const int bodyH = canvasTextHeight(kBody);
  canvasDrawString(kPad, y + (step - bodyH) / 2, label, true, kBody, true);
  drawOutlineBtn(minusX, y, step, step, "-");
  drawOutlineBtn(plusX, y, step, step, "+");
  const int valueW = plusX - (minusX + step);
  const int vx = minusX + step + (valueW - canvasTextWidth(value, kBody)) / 2;
  canvasDrawString(vx, y + (step - bodyH) / 2, value, true, kBody);
}

const char* monthName(int month) {
  static const char* names[] = {"--", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (month < 1 || month > 12) return names[0];
  return names[month];
}

void hardwareChipRect(int& x, int& y, int& w, int& h) {
  const SettingsGeom g = settingsGeom();
  h = 40;
  w = canvasTextWidth("Hardware", kBody) + 28;
  x = kScreenW - kPad - w;
  const int titleH = canvasTextHeight(kTitle);
  y = g.titleY + (titleH - h) / 2;
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);
  const SettingsGeom g = settingsGeom();
  constexpr int step = kStepBtn;

  canvasDrawString(kPad, g.titleY, "Settings", true, kTitle);
  int hx, hy, hw, hh;
  hardwareChipRect(hx, hy, hw, hh);
  drawOutlineBtn(hx, hy, hw, hh, "Hardware");

  const char* sizeTxt = "Med";
  if (boardUiTextSize() <= 0) sizeTxt = "Small";
  else if (boardUiTextSize() >= 2) sizeTxt = "Large";
  drawSettingsStepper(g.textSizeY, "Text size", sizeTxt, g.rowMinusX, g.rowPlusX);

  char sleepTxt[16];
  if (boardSleepAfterMin() <= 0) snprintf(sleepTxt, sizeof(sleepTxt), "off");
  else snprintf(sleepTxt, sizeof(sleepTxt), "%dm", boardSleepAfterMin());
  drawSettingsStepper(g.sleepY, "Auto-sleep", sleepTxt, g.rowMinusX, g.rowPlusX);

  char every[12];
  snprintf(every, sizeof(every), "%d", boardCleanEvery());
  drawSettingsStepper(g.cleanY, "Fast budget", every, g.cleanMinusX, g.cleanPlusX);

  const BoardClockInfo clock = boardClock();
  canvasDrawString(kPad, g.timeLabelY, "Time", true, kBody);
  if (clock.valid) {
    canvasDrawString(kScreenW - kPad - canvasTextWidth(clock.time, kBody), g.timeLabelY, clock.time, true,
                     kBody);
  }
  char hourBuf[8];
  snprintf(hourBuf, sizeof(hourBuf), "%02u", clock.valid ? clock.hour : 0);
  drawSettingsStepper(g.hourY, "Hour", hourBuf, g.rowMinusX, g.rowPlusX);
  char minBuf[8];
  snprintf(minBuf, sizeof(minBuf), "%02u", clock.valid ? clock.minute : 0);
  drawSettingsStepper(g.minuteY, "Minute", minBuf, g.rowMinusX, g.rowPlusX);

  canvasDrawString(kPad, g.dateLabelY, "Date", true, kBody);
  if (clock.valid) {
    char pretty[24];
    snprintf(pretty, sizeof(pretty), "%d %s %u", clock.day, monthName(clock.month), clock.year);
    canvasDrawString(kScreenW - kPad - canvasTextWidth(pretty, kBody), g.dateLabelY, pretty, true, kBody);
  }
  char yearBuf[8];
  snprintf(yearBuf, sizeof(yearBuf), "%u", clock.valid ? clock.year : 2026);
  drawSettingsStepper(g.yearY, "Year", yearBuf, g.rowMinusX, g.rowPlusX);
  drawSettingsStepper(g.monthY, "Month", monthName(clock.valid ? clock.month : 1), g.rowMinusX, g.rowPlusX);
  char dayBuf[8];
  snprintf(dayBuf, sizeof(dayBuf), "%u", clock.valid ? clock.day : 1);
  drawSettingsStepper(g.dayY, "Day", dayBuf, g.rowMinusX, g.rowPlusX);

  drawOutlineBtn(kPad, g.powerY, kScreenW - 2 * kPad, kActionBtnH, "Sleep / power off");
  drawFilledBtn(kPad, g.backY, kScreenW - 2 * kPad, kActionBtnH, "Back");
  canvasDrawString(kPad, g.tipY, "Hold BOOT to sleep. Press to wake.", true, kSmall);
  presentNavigation();
}

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  const SettingsGeom g = settingsGeom();
  int hx, hy, hw, hh;
  hardwareChipRect(hx, hy, hw, hh);
  if (y >= hy && y < hy + hh && x >= hx && x < hx + hw) {
    hit.kind = UiHit::Kind::Hardware;
    return hit;
  }
  constexpr int step = kStepBtn;
  const int minusX = g.rowMinusX;
  const int plusX = g.rowPlusX;

  auto hitStepper = [&](int rowY, UiHit::Kind minus, UiHit::Kind plus) -> bool {
    if (y < rowY || y >= rowY + step) return false;
    if (x >= minusX && x < minusX + step) {
      hit.kind = minus;
      return true;
    }
    if (x >= plusX && x < plusX + step) {
      hit.kind = plus;
      return true;
    }
    return false;
  };

  if (hitStepper(g.textSizeY, UiHit::Kind::FontSizeMinus, UiHit::Kind::FontSizePlus)) return hit;
  if (hitStepper(g.sleepY, UiHit::Kind::SleepAfterMinus, UiHit::Kind::SleepAfterPlus)) return hit;
  if (hitStepper(g.cleanY, UiHit::Kind::CleanEveryMinus, UiHit::Kind::CleanEveryPlus)) return hit;
  if (hitStepper(g.hourY, UiHit::Kind::HourMinus, UiHit::Kind::HourPlus)) return hit;
  if (hitStepper(g.minuteY, UiHit::Kind::MinuteMinus, UiHit::Kind::MinutePlus)) return hit;
  if (hitStepper(g.yearY, UiHit::Kind::YearMinus, UiHit::Kind::YearPlus)) return hit;
  if (hitStepper(g.monthY, UiHit::Kind::MonthMinus, UiHit::Kind::MonthPlus)) return hit;
  if (hitStepper(g.dayY, UiHit::Kind::DayMinus, UiHit::Kind::DayPlus)) return hit;

  if (y >= g.powerY && y < g.powerY + kActionBtnH && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::PowerOff;
    return hit;
  }
  if (y >= g.backY && y < g.backY + kActionBtnH && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }
  return hit;
}

int drawHardwareRow(int y, const char* label, const char* value) {
  const int h = canvasTextHeight(kBody);
  canvasDrawString(kPad, y, label, true, kBody);
  const int valueW = canvasTextWidth(value ? value : "", kBody);
  canvasDrawString(kScreenW - kPad - valueW, y, value ? value : "", true, kBody);
  return y + h + 14;
}

int drawHardwareRule(int y) {
  y += 4;
  canvasDrawLine(kPad, y, kScreenW - kPad - 1, y, true);
  return y + 16;
}

void uiDrawHardware(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);
  const BoardDeviceInfo device = boardDeviceInfo();
  int y = statusBarH() + kPad;
  canvasDrawString(kPad, y, "Hardware", true, kTitle);
  y += canvasTextHeight(kTitle) + 22;

  y = drawHardwareRow(y, "Board", device.product);
  y = drawHardwareRow(y, "Panel", device.panel);
  y = drawHardwareRow(y, "Touch", device.touch);
  y = drawHardwareRow(y, "Light", device.light);
  y = drawHardwareRule(y);
  y = drawHardwareRow(y, "MCU", device.mcu);
  y = drawHardwareRow(y, "CPU", device.cpu);
  y = drawHardwareRow(y, "Memory", device.memory);
  y = drawHardwareRow(y, "MAC", device.mac);
  y = drawHardwareRow(y, "Launcher", BASILAUNCHER_VERSION);
  y = drawHardwareRule(y);
  y = drawHardwareRow(y, "Battery", device.battery);
  y = drawHardwareRow(y, "RTC", device.rtc);
  y = drawHardwareRow(y, "SD card", device.storage);
  y = drawHardwareRule(y);
  y = drawHardwareRow(y, "LoRa", device.lora);
  y = drawHardwareRow(y, "GPS", device.gps);

  char freeBuf[16], totalBuf[16], slots[40];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  appsFormatBytes(space.guestTotal, totalBuf, sizeof(totalBuf));
  snprintf(slots, sizeof(slots), "%s free / %s", freeBuf, totalBuf);
  drawHardwareRow(y, "App slots", slots);

  const int backY = kScreenH - kPad - kActionBtnH;
  drawFilledBtn(kPad, backY, kScreenW - 2 * kPad, kActionBtnH, "Back");
  presentNavigation();
}

UiHit uiHitHardware(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (y >= kScreenH - kActionBtnH - kPad - 8 && y < kScreenH - kPad && x >= kPad &&
      x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }
  return hit;
}

void uiDrawWifi(const FlashSpace& space, const char* statusLine, const char* ssidLine,
                const char* urlLine, const char* detailLine, bool active) {
  canvasClear();
  drawStatusBar(space, true);
  int y = statusBarH() + kPad;
  canvasDrawString(kPad, y, "Wi-Fi transfer", true, kTitle);
  y += canvasTextHeight(kTitle) + 14;

  auto line = [&](const char* label, const char* value) {
    canvasDrawString(kPad, y, label, true, kSmall);
    y += canvasTextHeight(kSmall) + 2;
    canvasDrawString(kPad, y, value && value[0] ? value : "-", true, kBody);
    y += canvasTextHeight(kBody) + 10;
  };
  line("Status", statusLine);
  line("Network", ssidLine);
  line("Password", wifiApPassword());
  line("Open in browser", urlLine);
  // Always reserve one detail row so hit-testing matches.
  canvasDrawString(kPad, y, (detailLine && detailLine[0]) ? detailLine : " ", true, kSmall);
  y += canvasTextHeight(kSmall) + 12;

  // QR codes when hotspot is live: join SoftAP + open the page URL.
  if (active && urlLine && urlLine[0]) {
    const int qrSize = 180;
    const int qrTop = y;
    char wifiPayload[180];
    snprintf(wifiPayload, sizeof(wifiPayload), "WIFI:T:WPA;S:%s;P:", wifiApSsid());
    size_t used = strlen(wifiPayload);
    for (const char* p = wifiApPassword(); *p && used + 3 < sizeof(wifiPayload); ++p) {
      if (*p == '\\' || *p == ';' || *p == ',' || *p == ':' || *p == '"') wifiPayload[used++] = '\\';
      wifiPayload[used++] = *p;
    }
    wifiPayload[used++] = ';';
    wifiPayload[used++] = ';';
    wifiPayload[used] = 0;
    const int gap = 16;
    const int pairW = qrSize * 2 + gap;
    const int x0 = (kScreenW - pairW) / 2;
    canvasDrawQr(x0, qrTop, qrSize, wifiPayload);
    canvasDrawQr(x0 + qrSize + gap, qrTop, qrSize, urlLine);
    y = qrTop + qrSize + 4;
    const int labelY = y;
    canvasDrawString(x0, labelY, "Join Wi-Fi", true, kSmall);
    canvasDrawString(x0 + qrSize + gap, labelY, "Open page", true, kSmall);
    y = labelY + canvasTextHeight(kSmall) + 12;
  }

  const int btnW = kScreenW - 2 * kPad;
  const int backY = kScreenH - kPad - kActionBtnH;
  auto placeBtn = [&](const char* label, bool filled) {
    if (y + kActionBtnH + 10 > backY) return;
    if (filled) drawFilledBtn(kPad, y, btnW, kActionBtnH, label);
    else drawOutlineBtn(kPad, y, btnW, kActionBtnH, label);
    y += kActionBtnH + 10;
  };

  placeBtn("Change password", false);
  if (!active) {
    placeBtn("Start hotspot", true);
  } else {
    placeBtn("Stop Wi-Fi", false);
  }
  drawFilledBtn(kPad, backY, btnW, kActionBtnH, "Back");
  presentNavigation();
}

UiHit uiHitWifi(int x, int y, bool active) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  const int btnW = kScreenW - 2 * kPad;
  const int backY = kScreenH - kPad - kActionBtnH;
  if (y >= backY && y < backY + kActionBtnH && x >= kPad && x < kPad + btnW) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }

  int by = statusBarH() + kPad + canvasTextHeight(kTitle) + 14;
  by += (canvasTextHeight(kSmall) + 2 + canvasTextHeight(kBody) + 10) * 4;
  by += canvasTextHeight(kSmall) + 12;

  if (active) {
    constexpr int qrSize = 180;
    by += qrSize + 4;
    by += canvasTextHeight(kSmall) + 12;
  }

  auto hitBtn = [&](UiHit::Kind kind) -> bool {
    if (by + kActionBtnH + 10 > backY) return false;
    if (y >= by && y < by + kActionBtnH && x >= kPad && x < kPad + btnW) {
      hit.kind = kind;
      return true;
    }
    by += kActionBtnH + 10;
    return false;
  };

  if (hitBtn(UiHit::Kind::WifiChangePass)) return hit;
  if (!active) {
    if (hitBtn(UiHit::Kind::WifiStartAp)) return hit;
  } else {
    if (hitBtn(UiHit::Kind::WifiStop)) return hit;
  }
  return hit;
}

void uiDrawProgress(const char* title, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const char* shownTitle = title ? title : "Working...";
  static char lastTitle[64] = {};
  static int lastPercent = 101;
  const bool newProgress = strcmp(lastTitle, shownTitle) != 0 || percent <= lastPercent;
  if (newProgress) {
    canvasClear();
    drawStatusBar(appsFlashSpace(), false);
    canvasDrawString(kPad, statusBarH() + 80, shownTitle, true, kTitle);
  }
  const int barX = kPad;
  const int barY = statusBarH() + 140;
  const int barW = kScreenW - 2 * kPad;
  const int barH = 32;
  const int dirtyH = 48 + canvasTextHeight(2);
  if (!newProgress) canvasFillRect(barX - 2, barY - 2, barW + 4, dirtyH + 4, false);
  canvasDrawRoundRect(barX, barY, barW, barH, 10, true);
  const int fill = (barW - 6) * percent / 100;
  if (fill > 0) canvasFillRoundRect(barX + 3, barY + 3, fill, barH - 6, 6, true);
  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  canvasDrawString(kPad, barY + 48, pct, true, 2);
  snprintf(lastTitle, sizeof(lastTitle), "%s", shownTitle);
  lastPercent = percent;
  canvasPresentFor(CanvasRefreshIntent::Progress,
                   newProgress ? CanvasRect{} : CanvasRect{barX - 2, barY - 2, barW + 4, dirtyH + 4});
}

void uiDrawMessage(const char* title, const char* body) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  canvasDrawString(kPad, statusBarH() + 28, title ? title : "Notice", true, kTitle);
  drawWrappedBody(body, statusBarH() + 70);
  drawFilledBtn(kPad, kScreenH - kDockH - 70, kScreenW - 2 * kPad, kActionBtnH, "OK");
  presentQuality();
}

void uiDrawConfirm(const char* title, const char* body) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  canvasDrawString(kPad, statusBarH() + 28, title ? title : "Confirm", true, kTitle);
  drawWrappedBody(body, statusBarH() + 70);
  const int y = kScreenH - kDockH;
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  const int tileW = (kScreenW - 3 * kPad) / 2;
  drawOutlineBtn(kPad, y + 12, tileW, kDockH - 24, "Cancel");
  drawFilledBtn(kPad * 2 + tileW, y + 12, tileW, kDockH - 24, "Yes");
  presentQuality();
}

UiHit uiHitConfirm(int x, int y) {
  UiHit hit;
  const int dockY = kScreenH - kDockH;
  if (y < dockY + 12 || y >= dockY + kDockH - 12) return hit;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (x >= kPad && x < kPad + tileW) {
    hit.kind = UiHit::Kind::ConfirmNo;
    return hit;
  }
  if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
    hit.kind = UiHit::Kind::ConfirmYes;
    return hit;
  }
  return hit;
}

void imageViewChips(int& closeX, int& setX, int& y, int& w, int& h) {
  h = 52;
  w = 168;
  y = kScreenH - h - 24;
  const int gap = 16;
  const int total = w * 2 + gap;
  closeX = (kScreenW - total) / 2;
  setX = closeX + w + gap;
}

void uiDrawImageViewHint(bool saved) {
  if (canvasHasCapture()) canvasRestoreCapture();
  int closeX = 0, setX = 0, y = 0, w = 0, h = 0;
  imageViewChips(closeX, setX, y, w, h);
  drawChromeOutlineBtn(closeX, y, w, h, "Close");
  drawFilledBtn(setX, y, w, h, saved ? "Saved" : "Set sleep");
  presentQuality();
}

UiHit uiHitImageView(int x, int y) {
  UiHit hit;
  int closeX = 0, setX = 0, cy = 0, w = 0, h = 0;
  imageViewChips(closeX, setX, cy, w, h);
  if (y >= cy && y < cy + h && x >= setX && x < setX + w) hit.kind = UiHit::Kind::SetSleep;
  return hit;
}

namespace {

// Phone-style QWERTY: 3 letter rows + bottom bar. Fat keys, small side margins.
constexpr int kOskSide = 6;
constexpr int kOskGap = 6;
constexpr int kOskKeyH = 58;
constexpr int kOskRows = 4;
constexpr int kOskBarH = 46;  // Cancel / Done above keyboard
constexpr int kOskBot = 10;
constexpr int kOskRadius = 10;

int oskUsable() { return kScreenW - 2 * kOskSide; }

int oskUnitW() {
  // 10 equal letter columns across the row.
  return (oskUsable() - 9 * kOskGap) / 10;
}

int oskTopY() {
  return kScreenH - (kOskRows * kOskKeyH + (kOskRows - 1) * kOskGap + kOskBot);
}

int oskActionBarY() { return oskTopY() - kOskBarH - 8; }

int oskRowY(int row) { return oskTopY() + row * (kOskKeyH + kOskGap); }

void drawOskKey(int x, int y, int w, int h, const char* label, bool inverted = false) {
  if (w <= 0 || h <= 0) return;
  if (inverted) {
    canvasFillRoundRect(x, y, w, h, kOskRadius, true);
    const int tw = canvasTextWidth(label, kBody);
    const int th = canvasTextHeight(kBody);
    canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, false, kBody);
  } else {
    canvasDrawRoundRect(x, y, w, h, kOskRadius, true);
    const int tw = canvasTextWidth(label, kBody);
    const int th = canvasTextHeight(kBody);
    canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, true, kBody);
  }
}

// Custom icons — font is ASCII-only, so shift/backspace are drawn.
void drawOskShiftKey(int x, int y, int w, int h, bool active) {
  if (w <= 0 || h <= 0) return;
  if (active) canvasFillRoundRect(x, y, w, h, kOskRadius, true);
  else canvasDrawRoundRect(x, y, w, h, kOskRadius, true);
  const bool ink = !active;
  const int cx = x + w / 2;
  const int cy = y + h / 2;
  // Hollow up-arrow (⇧): chevron + stem.
  const int tipY = cy - 14;
  const int wing = 11;
  const int midY = cy - 2;
  canvasDrawLine(cx, tipY, cx - wing, midY, ink);
  canvasDrawLine(cx, tipY, cx + wing, midY, ink);
  canvasDrawLine(cx - wing, midY, cx - 5, midY, ink);
  canvasDrawLine(cx + wing, midY, cx + 5, midY, ink);
  canvasDrawLine(cx - 5, midY, cx - 5, cy + 12, ink);
  canvasDrawLine(cx + 5, midY, cx + 5, cy + 12, ink);
  canvasDrawLine(cx - 5, cy + 12, cx + 5, cy + 12, ink);
  // Thicken the chevron a pixel.
  canvasDrawLine(cx, tipY + 1, cx - wing + 1, midY, ink);
  canvasDrawLine(cx, tipY + 1, cx + wing - 1, midY, ink);
}

void drawOskBackspaceKey(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  canvasDrawRoundRect(x, y, w, h, kOskRadius, true);
  const int cx = x + w / 2;
  const int cy = y + h / 2;
  // ⌫: left arrowhead + body rectangle with an X.
  const int bodyW = 22;
  const int bodyH = 18;
  const int bodyX = cx - 4;
  const int bodyY = cy - bodyH / 2;
  const int tipX = bodyX - 14;
  canvasDrawLine(tipX, cy, bodyX, bodyY, true);
  canvasDrawLine(tipX, cy, bodyX, bodyY + bodyH - 1, true);
  canvasDrawLine(bodyX, bodyY, bodyX + bodyW - 1, bodyY, true);
  canvasDrawLine(bodyX, bodyY + bodyH - 1, bodyX + bodyW - 1, bodyY + bodyH - 1, true);
  canvasDrawLine(bodyX + bodyW - 1, bodyY, bodyX + bodyW - 1, bodyY + bodyH - 1, true);
  const int ix0 = bodyX + 6;
  const int ix1 = bodyX + bodyW - 7;
  const int iy0 = bodyY + 5;
  const int iy1 = bodyY + bodyH - 6;
  canvasDrawLine(ix0, iy0, ix1, iy1, true);
  canvasDrawLine(ix1, iy0, ix0, iy1, true);
  canvasDrawLine(ix0, iy0 + 1, ix1, iy1 + 1, true);
  canvasDrawLine(ix1, iy0 + 1, ix0, iy1 + 1, true);
}

// Letter / symbol glyph rows (not including shift/del/space chrome).
const char* oskLetterRow(int row, bool shift) {
  static const char* lower[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  static const char* upper[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
  if (row < 0 || row > 2) return "";
  return shift ? upper[row] : lower[row];
}

const char* oskSymbolRow(int row) {
  // Android-ish first symbols page.
  static const char* sym[] = {"1234567890", "-/:;()$&@\"", ".,?!'#%"};
  if (row < 0 || row > 2) return "";
  return sym[row];
}

int oskWideW() {
  // Shift / delete / 123 — about 1.5 letter keys.
  return oskUnitW() + (oskUnitW() + kOskGap) / 2;
}

void drawOskLetters(bool shift) {
  const int unit = oskUnitW();
  const int usable = oskUsable();
  const int side = kOskSide;

  // Row 0: 10 keys
  {
    const char* keys = oskLetterRow(0, shift);
    const int n = 10;
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int y = oskRowY(0);
    for (int i = 0; i < n; ++i) {
      char lab[2] = {keys[i], 0};
      drawOskKey(startX + i * (unit + kOskGap), y, unit, kOskKeyH, lab);
    }
  }

  // Row 1: 9 keys, centered (half-key inset like iOS/Android)
  {
    const char* keys = oskLetterRow(1, shift);
    const int n = 9;
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int y = oskRowY(1);
    for (int i = 0; i < n; ++i) {
      char lab[2] = {keys[i], 0};
      drawOskKey(startX + i * (unit + kOskGap), y, unit, kOskKeyH, lab);
    }
  }

  // Row 2: shift + 7 letters + delete
  {
    const char* keys = oskLetterRow(2, shift);
    const int n = 7;
    const int wide = oskWideW();
    const int midW = n * unit + (n - 1) * kOskGap;
    const int rowW = wide + kOskGap + midW + kOskGap + wide;
    const int startX = side + (usable - rowW) / 2;
    const int y = oskRowY(2);
    drawOskShiftKey(startX, y, wide, kOskKeyH, shift);
    const int midX = startX + wide + kOskGap;
    for (int i = 0; i < n; ++i) {
      char lab[2] = {keys[i], 0};
      drawOskKey(midX + i * (unit + kOskGap), y, unit, kOskKeyH, lab);
    }
    drawOskBackspaceKey(midX + midW + kOskGap, y, wide, kOskKeyH);
  }

  // Row 3: 123 | , | space | .
  {
    const int wide = oskWideW();
    const int y = oskRowY(3);
    const int punct = unit;
    const int startX = side;
    drawOskKey(startX, y, wide, kOskKeyH, "123");
    int x = startX + wide + kOskGap;
    drawOskKey(x, y, punct, kOskKeyH, ",");
    x += punct + kOskGap;
    const int endX = side + usable;
    const int periodW = wide;
    const int periodX = endX - periodW;
    const int spaceW = periodX - kOskGap - x;
    drawOskKey(x, y, spaceW, kOskKeyH, "space");
    drawOskKey(periodX, y, periodW, kOskKeyH, ".");
  }
}

void drawOskSymbols() {
  const int unit = oskUnitW();
  const int usable = oskUsable();
  const int side = kOskSide;

  for (int row = 0; row < 2; ++row) {
    const char* keys = oskSymbolRow(row);
    const int n = static_cast<int>(strlen(keys));
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int y = oskRowY(row);
    for (int i = 0; i < n; ++i) {
      char lab[2] = {keys[i], 0};
      drawOskKey(startX + i * (unit + kOskGap), y, unit, kOskKeyH, lab);
    }
  }

  // Row 2: shorter symbol strip + delete (wide)
  {
    const char* keys = oskSymbolRow(2);
    const int n = static_cast<int>(strlen(keys));
    const int wide = oskWideW();
    const int midW = n * unit + (n - 1) * kOskGap;
    const int rowW = midW + kOskGap + wide;
    const int startX = side + (usable - rowW) / 2;
    const int y = oskRowY(2);
    for (int i = 0; i < n; ++i) {
      char lab[2] = {keys[i], 0};
      drawOskKey(startX + i * (unit + kOskGap), y, unit, kOskKeyH, lab);
    }
    drawOskBackspaceKey(startX + midW + kOskGap, y, wide, kOskKeyH);
  }

  // Row 3: ABC | space | #+= (extra punct via same symbols toggle stays)
  {
    const int wide = oskWideW();
    const int y = oskRowY(3);
    const int startX = side;
    drawOskKey(startX, y, wide, kOskKeyH, "ABC");
    const int endX = side + usable;
    const int spaceX = startX + wide + kOskGap;
    const int spaceW = endX - spaceX;
    drawOskKey(spaceX, y, spaceW, kOskKeyH, "space");
  }
}

void drawOsk(bool symbols, bool shift) {
  if (symbols) drawOskSymbols();
  else drawOskLetters(shift);
}

// Hit helpers: gaps are split between neighbors so there are no dead strips.
bool hitInBand(int x, int y, int kx, int ky, int kw, int kh, int leftSlop, int rightSlop) {
  return y >= ky && y < ky + kh && x >= kx - leftSlop && x < kx + kw + rightSlop;
}

bool hitOskLetters(int x, int y, bool shift, UiHit& hit) {
  const int unit = oskUnitW();
  const int usable = oskUsable();
  const int side = kOskSide;
  const int halfGap = kOskGap / 2;

  // Row 0
  {
    const char* keys = oskLetterRow(0, shift);
    const int n = 10;
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int ry = oskRowY(0);
    if (y >= ry && y < ry + kOskKeyH) {
      for (int i = 0; i < n; ++i) {
        const int kx = startX + i * (unit + kOskGap);
        const int ls = (i == 0) ? 0 : halfGap;
        const int rs = (i == n - 1) ? 0 : halfGap;
        if (hitInBand(x, y, kx, ry, unit, kOskKeyH, ls, rs)) {
          hit.kind = UiHit::Kind::KeyChar;
          hit.value = static_cast<unsigned char>(keys[i]);
          return true;
        }
      }
    }
  }

  // Row 1
  {
    const char* keys = oskLetterRow(1, shift);
    const int n = 9;
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int ry = oskRowY(1);
    if (y >= ry && y < ry + kOskKeyH) {
      for (int i = 0; i < n; ++i) {
        const int kx = startX + i * (unit + kOskGap);
        const int ls = (i == 0) ? halfGap : halfGap;
        const int rs = (i == n - 1) ? halfGap : halfGap;
        if (hitInBand(x, y, kx, ry, unit, kOskKeyH, ls, rs)) {
          hit.kind = UiHit::Kind::KeyChar;
          hit.value = static_cast<unsigned char>(keys[i]);
          return true;
        }
      }
    }
  }

  // Row 2: shift + letters + del
  {
    const char* keys = oskLetterRow(2, shift);
    const int n = 7;
    const int wide = oskWideW();
    const int midW = n * unit + (n - 1) * kOskGap;
    const int rowW = wide + kOskGap + midW + kOskGap + wide;
    const int startX = side + (usable - rowW) / 2;
    const int ry = oskRowY(2);
    if (y >= ry && y < ry + kOskKeyH) {
      if (hitInBand(x, y, startX, ry, wide, kOskKeyH, 0, halfGap)) {
        hit.kind = UiHit::Kind::KeyShift;
        return true;
      }
      const int midX = startX + wide + kOskGap;
      for (int i = 0; i < n; ++i) {
        const int kx = midX + i * (unit + kOskGap);
        if (hitInBand(x, y, kx, ry, unit, kOskKeyH, halfGap, halfGap)) {
          hit.kind = UiHit::Kind::KeyChar;
          hit.value = static_cast<unsigned char>(keys[i]);
          return true;
        }
      }
      if (hitInBand(x, y, midX + midW + kOskGap, ry, wide, kOskKeyH, halfGap, 0)) {
        hit.kind = UiHit::Kind::KeyBackspace;
        return true;
      }
    }
  }

  // Row 3: 123 , space .
  {
    const int wide = oskWideW();
    const int punct = unit;
    const int ry = oskRowY(3);
    if (y >= ry && y < ry + kOskKeyH) {
      const int startX = side;
      if (hitInBand(x, y, startX, ry, wide, kOskKeyH, 0, halfGap)) {
        hit.kind = UiHit::Kind::KeySymbols;
        return true;
      }
      const int commaX = startX + wide + kOskGap;
      if (hitInBand(x, y, commaX, ry, punct, kOskKeyH, halfGap, halfGap)) {
        hit.kind = UiHit::Kind::KeyChar;
        hit.value = ',';
        return true;
      }
      const int spaceX = commaX + punct + kOskGap;
      const int endX = side + usable;
      const int periodW = wide;
      const int periodX = endX - periodW;
      const int spaceW = periodX - kOskGap - spaceX;
      if (hitInBand(x, y, spaceX, ry, spaceW, kOskKeyH, halfGap, halfGap)) {
        hit.kind = UiHit::Kind::KeySpace;
        return true;
      }
      if (hitInBand(x, y, periodX, ry, periodW, kOskKeyH, halfGap, 0)) {
        hit.kind = UiHit::Kind::KeyChar;
        hit.value = '.';
        return true;
      }
    }
  }
  return false;
}

bool hitOskSymbols(int x, int y, UiHit& hit) {
  const int unit = oskUnitW();
  const int usable = oskUsable();
  const int side = kOskSide;
  const int halfGap = kOskGap / 2;

  for (int row = 0; row < 2; ++row) {
    const char* keys = oskSymbolRow(row);
    const int n = static_cast<int>(strlen(keys));
    const int rowW = n * unit + (n - 1) * kOskGap;
    const int startX = side + (usable - rowW) / 2;
    const int ry = oskRowY(row);
    if (y < ry || y >= ry + kOskKeyH) continue;
    for (int i = 0; i < n; ++i) {
      const int kx = startX + i * (unit + kOskGap);
      if (hitInBand(x, y, kx, ry, unit, kOskKeyH, halfGap, halfGap)) {
        hit.kind = UiHit::Kind::KeyChar;
        hit.value = static_cast<unsigned char>(keys[i]);
        return true;
      }
    }
  }

  {
    const char* keys = oskSymbolRow(2);
    const int n = static_cast<int>(strlen(keys));
    const int wide = oskWideW();
    const int midW = n * unit + (n - 1) * kOskGap;
    const int rowW = midW + kOskGap + wide;
    const int startX = side + (usable - rowW) / 2;
    const int ry = oskRowY(2);
    if (y >= ry && y < ry + kOskKeyH) {
      for (int i = 0; i < n; ++i) {
        const int kx = startX + i * (unit + kOskGap);
        if (hitInBand(x, y, kx, ry, unit, kOskKeyH, halfGap, halfGap)) {
          hit.kind = UiHit::Kind::KeyChar;
          hit.value = static_cast<unsigned char>(keys[i]);
          return true;
        }
      }
      if (hitInBand(x, y, startX + midW + kOskGap, ry, wide, kOskKeyH, halfGap, 0)) {
        hit.kind = UiHit::Kind::KeyBackspace;
        return true;
      }
    }
  }

  {
    const int wide = oskWideW();
    const int ry = oskRowY(3);
    if (y >= ry && y < ry + kOskKeyH) {
      const int startX = side;
      if (hitInBand(x, y, startX, ry, wide, kOskKeyH, 0, halfGap)) {
        hit.kind = UiHit::Kind::KeySymbols;
        return true;
      }
      const int spaceX = startX + wide + kOskGap;
      const int spaceW = side + usable - spaceX;
      if (hitInBand(x, y, spaceX, ry, spaceW, kOskKeyH, halfGap, 0)) {
        hit.kind = UiHit::Kind::KeySpace;
        return true;
      }
    }
  }
  return false;
}

bool hitOsk(int x, int y, bool symbols, bool shift, UiHit& hit) {
  if (symbols) return hitOskSymbols(x, y, hit);
  return hitOskLetters(x, y, shift, hit);
}

}  // namespace

namespace {

void drawTextEditField(const char* text, TextEditMode mode) {
  const bool nameMode = mode == TextEditMode::Rename || mode == TextEditMode::NewFolder ||
                        mode == TextEditMode::NewFile || mode == TextEditMode::ApPassword;

  const int actionY = oskActionBarY();
  const int fieldTop = statusBarH() + kPad + 40;
  const int fieldBottom = actionY - 10;
  const char* body = text ? text : "";
  const size_t len = strlen(body);

  if (nameMode) {
    const char* hint = "New name";
    if (mode == TextEditMode::NewFolder) hint = "Folder name";
    else if (mode == TextEditMode::NewFile) hint = "File name";
    else if (mode == TextEditMode::ApPassword) hint = "8 to 63 characters";
    // Clear hint + name box (leave keyboard alone).
    const int boxY = fieldTop + 22;
    const int boxH = 56;
    canvasFillRect(kPad, fieldTop, kScreenW - 2 * kPad, boxY + boxH + 36 - fieldTop, false);
    canvasDrawString(kPad, fieldTop, hint, true, kSmall);
    canvasDrawRoundRect(kPad, boxY, kScreenW - 2 * kPad, boxH, 10, true);

    const int maxCols = std::max(8, (kScreenW - 2 * kPad - 28) / canvasBodyCellW());
    size_t start = 0;
    if (len > static_cast<size_t>(maxCols)) start = len - static_cast<size_t>(maxCols);
    char line[80];
    snprintf(line, sizeof(line), "%s", body + start);
    if (strlen(line) > static_cast<size_t>(maxCols)) line[maxCols] = 0;
    const int textY = boxY + (boxH - canvasTextHeight(kBody)) / 2;
    canvasDrawString(kPad + 14, textY, line, true, kBody);
    const int caretX = kPad + 14 + canvasTextWidth(line, kBody);
    canvasFillRect(caretX + 2, textY, 2, canvasTextHeight(kBody), true);

    if (mode == TextEditMode::NewFile) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done to create and edit.", true, kSmall);
    } else if (mode == TextEditMode::NewFolder) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done to create the folder.", true, kSmall);
    } else if (mode == TextEditMode::ApPassword) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done. The hotspot restarts.", true, kSmall);
    }
    return;
  }

  canvasFillRect(kPad, fieldTop - 2, kScreenW - 2 * kPad, fieldBottom - (fieldTop - 2), false);
  canvasDrawString(kPad, fieldTop - 2, "Content", true, kSmall);
  const int boxTop = fieldTop + 18;
  canvasDrawRoundRect(kPad, boxTop, kScreenW - 2 * kPad, fieldBottom - boxTop, 10, true);

  const int maxCols = std::max(8, (kScreenW - 2 * kPad - 24) / canvasBodyCellW());
  const int lineStep = canvasBodyCellH() + 2;
  const int maxRows = std::max(1, (fieldBottom - boxTop - 20) / lineStep);
  size_t start = 0;
  size_t linesNeeded = 1;
  {
    size_t col = 0;
    linesNeeded = 1;
    for (size_t i = 0; i < len; ++i) {
      if (body[i] == '\n' || col >= static_cast<size_t>(maxCols)) {
        ++linesNeeded;
        col = 0;
        if (body[i] == '\n') continue;
      }
      ++col;
    }
  }
  if (linesNeeded > static_cast<size_t>(maxRows)) {
    size_t probe = 0;
    while (probe < len) {
      size_t col = 0, rows = 1;
      for (size_t i = probe; i < len; ++i) {
        if (body[i] == '\n' || col >= static_cast<size_t>(maxCols)) {
          ++rows;
          col = 0;
          if (body[i] == '\n') continue;
        }
        ++col;
      }
      if (rows <= static_cast<size_t>(maxRows)) break;
      size_t n = 0;
      while (probe + n < len && n < static_cast<size_t>(maxCols) && body[probe + n] != '\n') ++n;
      probe += n;
      if (probe < len && body[probe] == '\n') ++probe;
      if (n == 0) ++probe;
    }
    start = probe;
  }

  int y = boxTop + 10;
  size_t pos = start;
  int caretX = kPad + 12;
  int caretY = y;
  for (int r = 0; r < maxRows && pos <= len; ++r) {
    char line[80];
    size_t n = 0;
    while (pos + n < len && n < static_cast<size_t>(maxCols) && body[pos + n] != '\n') ++n;
    memcpy(line, body + pos, n);
    line[n] = 0;
    canvasDrawString(kPad + 12, y, line, true, kBody);
    if (pos + n >= len) {
      caretX = kPad + 12 + canvasTextWidth(line, kBody);
      caretY = y;
    }
    pos += n;
    if (pos < len && body[pos] == '\n') ++pos;
    y += lineStep;
    if (pos >= len) break;
  }
  if (len == 0) {
    caretX = kPad + 12;
    caretY = boxTop + 10;
  }
  canvasFillRect(caretX + 2, caretY, 2, canvasBodyCellH(), true);
}

}  // namespace

void uiDrawTextEdit(const char* title, const char* text, bool symbols, bool shift, TextEditMode mode,
                    bool scrub) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), true);

  const char* heading = title ? title : "Edit";
  if (mode == TextEditMode::NewFolder) heading = "New folder";
  else if (mode == TextEditMode::NewFile) heading = "New file";
  else if (mode == TextEditMode::Rename) heading = "Rename";
  else if (mode == TextEditMode::ApPassword) heading = "Hotspot password";
  canvasDrawString(kPad, statusBarH() + kPad, heading, true, kTitle);

  drawTextEditField(text, mode);

  const int actionY = oskActionBarY();
  const char* doneLabel = (mode == TextEditMode::EditFile) ? "Save" : "Done";
  drawOutlineBtn(kPad, actionY, 120, kOskBarH, "Cancel");
  drawFilledBtn(kScreenW - kPad - 120, actionY, 120, kOskBarH, doneLabel);

  drawOsk(symbols, shift);
  if (scrub) presentQuality();
  else presentNavigation();
}

void uiRedrawTextEditField(const char* text, TextEditMode mode) {
  drawTextEditField(text, mode);
  const int actionY = oskActionBarY();
  const int fieldTop = statusBarH() + kPad + 38;
  canvasPresentFor(CanvasRefreshIntent::InteractiveLocal,
                   {kPad, fieldTop, kScreenW - 2 * kPad, actionY - fieldTop});
}

UiHit uiHitTextEdit(int x, int y, bool symbols, bool shift) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  const int actionY = oskActionBarY();
  if (y >= actionY && y < actionY + kOskBarH) {
    if (x >= kPad && x < kPad + 120) {
      hit.kind = UiHit::Kind::KeyCancel;
      return hit;
    }
    if (x >= kScreenW - kPad - 120 && x < kScreenW - kPad) {
      hit.kind = UiHit::Kind::KeyDone;
      return hit;
    }
  }

  if (hitOsk(x, y, symbols, shift, hit)) return hit;
  return hit;
}

UiHit uiHitHome(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }

  int fx, fy, fw, fh;
  filesChipRect(fx, fy, fw, fh);
  if (y >= fy && y < fy + fh && x >= fx && x < fx + fw) {
    hit.kind = UiHit::Kind::OpenFiles;
    return hit;
  }

  const int cardW = kScreenW - 2 * kPad;
  for (int i = 0; i < kSlotCount; ++i) {
    const int cy = slotCardY(i);
    const int h = slotCardH();
    if (y < cy || y >= cy + h) continue;
    const int by = cy + h - kBtnH - 14;
    const int btnX = kPad + cardW - kBtnW - 14;
    if (y >= by && y < by + kBtnH && x >= btnX && x < btnX + kBtnW) {
      hit.kind = appsSlotInfo(i).occupied ? UiHit::Kind::ClearSlot : UiHit::Kind::BootSlot;
      hit.index = i;
      return hit;
    }
    if (x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::BootSlot;
      hit.index = i;
      return hit;
    }
  }
  return hit;
}


