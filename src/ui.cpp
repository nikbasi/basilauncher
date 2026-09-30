#include "ui.h"

#include "board_hal.h"
#include "canvas.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 18;
constexpr int kStatusH = 74;
constexpr int kDockH = 88;       // picker / message only — tall enough for fat tap targets
constexpr int kHomeFooterH = 40;
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
constexpr int kShadeGrabH = 28;
constexpr int kShadeRadius = 18;
constexpr int kStepBtn = 56;  // +/- steppers — large enough for finger taps
int gBrightX = 0, gBrightY = 0, gBrightW = 0, gBrightH = 0;
int gShadePanelBottom = 0;

struct ShadeGeom {
  int panelX = 0;
  int panelY = 0;
  int panelW = 0;
  int panelH = 0;
  int titleY = 0;
  int frontRuleY = 0;
  int frontLabelY = 0;
  int sliderY = 0;
  int lightY = 0;
  int clockRuleY = 0;
  int clockLabelY = 0;
  int timeValueY = 0;
  int clockRowY = 0;
  int hourMinusX = 0;
  int hourPlusX = 0;
  int minMinusX = 0;
  int minPlusX = 0;
  int refreshRuleY = 0;
  int refreshLabelY = 0;
  int cleanRowY = 0;
  int cleanMinusX = 0;
  int cleanPlusX = 0;
  int scrubY = 0;
  int aboutY = 0;
  int grabY = 0;
};

ShadeGeom shadeGeom() {
  ShadeGeom g;
  g.panelX = kPad / 2;
  g.panelY = kStatusH + 6;
  g.panelW = kScreenW - kPad;

  int y = g.panelY + 12;
  g.titleY = y;
  y += 34;
  g.frontRuleY = y;
  y += 14;
  g.frontLabelY = y;
  y += 24;
  g.sliderY = y;
  y += kStepBtn + 10;
  g.lightY = y;
  y += kActionBtnH + 12;
  g.clockRuleY = y;
  y += 14;
  g.clockLabelY = y;
  y += 24;
  g.timeValueY = y;
  y += 40;
  g.clockRowY = y;

  constexpr int kStepGap = 56;  // label width between - and +
  const int groupW = kStepBtn + kStepGap + kStepBtn;
  const int gap = 18;
  const int startX = (kScreenW - (groupW * 2 + gap)) / 2;
  g.hourMinusX = startX;
  g.hourPlusX = startX + kStepBtn + kStepGap;
  g.minMinusX = startX + groupW + gap;
  g.minPlusX = g.minMinusX + kStepBtn + kStepGap;
  y += kStepBtn + 14;

  g.refreshRuleY = y;
  y += 14;
  g.refreshLabelY = y;
  y += 24;
  g.cleanRowY = y;
  g.cleanMinusX = kScreenW - kPad - 8 - kStepBtn * 2 - kStepGap;
  g.cleanPlusX = kScreenW - kPad - 8 - kStepBtn;
  y += kStepBtn + 12;
  g.scrubY = y;
  y += kActionBtnH + 12;
  g.aboutY = y;
  y += kActionBtnH + 10;
  g.grabY = y;
  g.panelH = (g.grabY + kShadeGrabH + 8) - g.panelY;
  return g;
}

void fillLightGrayRoundRect(int x, int y, int w, int h, int r) {
  canvasFillRoundRect(x, y, w, h, r, false);
  const int x1 = x + w;
  const int y1 = y + h;
  for (int yy = y; yy < y1; ++yy) {
    for (int xx = x; xx < x1; ++xx) {
      const int dx = (xx < x + r) ? (x + r - xx) : ((xx >= x1 - r) ? (xx - (x1 - r - 1)) : 0);
      const int dy = (yy < y + r) ? (y + r - yy) : ((yy >= y1 - r) ? (yy - (y1 - r - 1)) : 0);
      if (dx * dx + dy * dy > r * r) continue;
      if (((xx + 2 * yy) & 3) == 0) canvasSetPixel(xx, yy, true);
    }
  }
}

void drawDimBackdrop(int fromY) {
  for (int yy = fromY; yy < kScreenH; yy += 5) {
    for (int xx = ((yy / 5) & 1) ? 2 : 0; xx < kScreenW; xx += 5) {
      canvasSetPixel(xx, yy, true);
    }
  }
}

void drawSectionRule(int y, int panelX, int panelW) {
  canvasDrawLine(panelX + 14, y, panelX + panelW - 14, y, true);
}

int homeListTop() {
  // Status → pad → Apps title → gap → first card.
  return kStatusH + 18 + canvasTextHeight(kTitle) + 18;
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

void drawBatteryGlyph(int x, int y, int percent, bool charging) {
  canvasDrawRect(x, y, 28, 14, true);
  canvasFillRect(x + 28, y + 4, 3, 6, true);
  const int fillW = std::max(0, std::min(24, (24 * percent) / 100));
  if (fillW > 0) canvasFillRect(x + 2, y + 2, fillW, 10, true);
  if (charging) {
    canvasDrawLine(x + 12, y - 2, x + 10, y + 7, true);
    canvasDrawLine(x + 10, y + 7, x + 14, y + 7, true);
    canvasDrawLine(x + 14, y + 7, x + 12, y + 16, true);
  }
}

void drawStatusBar(const FlashSpace& space, bool shadeHint) {
  canvasFillRect(0, 0, kScreenW, kStatusH, false);
  const BoardClockInfo clock = boardClock();
  const BoardPowerInfo power = boardPower();

  canvasDrawString(kPad, 8, clock.valid ? clock.time : "--:--", true, 2);
  if (clock.valid && clock.date[0]) {
    const int timeW = canvasTextWidth(clock.valid ? clock.time : "--:--", 2);
    canvasDrawString(kPad + timeW + 14, 16, clock.date, true, kBody);
  }

  char batt[16];
  if (power.known) {
    snprintf(batt, sizeof(batt), "%d%%", power.percent);
    const int tw = canvasTextWidth(batt, kBody);
    const int bx = kScreenW - kPad - tw - 36;
    drawBatteryGlyph(bx, 14, power.percent, power.charging);
    canvasDrawString(bx + 34, 12, batt, true, kBody);
  } else {
    const char* na = "batt --";
    canvasDrawString(kScreenW - kPad - canvasTextWidth(na, kBody), 12, na, true, kBody);
  }

  char brand[40];
  snprintf(brand, sizeof(brand), "Basilauncher  v%s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, 44, brand, true, kBody);

  if (shadeHint) {
    // Grabber cue in the status bar
    canvasFillRoundRect(kScreenW / 2 - 28, kStatusH - 10, 56, 5, 2, true);
  }
  canvasDrawLine(0, kStatusH - 1, kScreenW - 1, kStatusH - 1, true);
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
  canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, true, scale);
}

void drawFilledBtn(int x, int y, int w, int h, const char* label) {
  canvasFillRoundRect(x, y, w, h, kBtnRadius, true);
  const int scale = btnLabelScale(label);
  const int tw = canvasTextWidth(label, scale);
  const int th = canvasTextHeight(scale);
  canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, false, scale);
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

void present() { canvasPresentAuto(); }
void presentClean() {
  canvasRequestCleanRefresh();
  canvasPresentAuto();
}

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

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);

  const int appsY = kStatusH + 18;
  canvasDrawString(kPad, appsY, "Apps", true, kTitle);

  for (int i = 0; i < kSlotCount; ++i) drawSlotCard(i, slots[i]);

  const int footY = kScreenH - kHomeFooterH + (kHomeFooterH - canvasTextHeight(kSmall)) / 2;
  canvasDrawString(kPad, footY, "Swipe down for settings", true, kSmall);
  char freeBuf[24], freeLine[40];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  snprintf(freeLine, sizeof(freeLine), "free %s", freeBuf);
  canvasDrawString(kScreenW - kPad - canvasTextWidth(freeLine, kSmall), footY, freeLine, true, kSmall);
  presentClean();
}

constexpr int kPickerRowH = 72;

int uiPickerRowHeight() { return kPickerRowH; }

int uiPickerVisibleRows() {
  const int listTop = kStatusH + kPad + 78;
  const int listBottom = kScreenH - kDockH - kPad;
  return std::max(1, (listBottom - listTop) / kPickerRowH);
}

void uiDrawPicker(const std::vector<DirEntry>& entries, int scroll, int targetSlot,
                  size_t maxBytes, const char* currentPath, const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, false);

  char title[48];
  if (targetSlot >= 0 && targetSlot < kSlotCount) {
    snprintf(title, sizeof(title), "Assign to slot %c", 'A' + targetSlot);
  } else {
    snprintf(title, sizeof(title), "Install (best fit)");
  }
  canvasDrawString(kPad, kStatusH + kPad, title, true, kTitle);

  // Path + max size
  char pathLine[64];
  snprintf(pathLine, sizeof(pathLine), "%s", currentPath ? currentPath : "/");
  // Keep path readable; prefer ending
  if (strlen(pathLine) > 40) {
    char shortP[64];
    snprintf(shortP, sizeof(shortP), "...%s", pathLine + strlen(pathLine) - 37);
    snprintf(pathLine, sizeof(pathLine), "%s", shortP);
  }
  canvasDrawString(kPad, kStatusH + kPad + 34, pathLine, true, kSmall);

  char maxBuf[24], hint[48];
  appsFormatBytes(maxBytes, maxBuf, sizeof(maxBuf));
  snprintf(hint, sizeof(hint), "max %s", maxBuf);
  canvasDrawString(kScreenW - kPad - canvasTextWidth(hint, kSmall), kStatusH + kPad + 34, hint, true,
                   kSmall);

  const int listTop = kStatusH + kPad + 78;
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = uiPickerVisibleRows();
  const int cardW = kScreenW - 2 * kPad;
  const int start = scroll;
  const int end = std::min(static_cast<int>(entries.size()), start + visible);

  // Size column ~ 10 glyphs
  constexpr int kSizeColW = 10 * 8;

  if (entries.empty()) {
    canvasDrawString(kPad, listTop + 8, boardSdOk() ? "Empty folder" : "Insert SD card", true, kBody);
  } else {
    for (int i = start; i < end; ++i) {
      const DirEntry& e = entries[i];
      const int rowY = listTop + (i - start) * kPickerRowH;
      canvasDrawRoundRect(kPad, rowY, cardW, kPickerRowH - 6, 10, true);

      const int nameMaxW = cardW - 28 - kSizeColW - 8;
      const int charsPerLine = std::max(8, nameMaxW / 8);

      if (e.isDir) {
        char nameBuf[96];
        snprintf(nameBuf, sizeof(nameBuf), "[%s]", e.name.c_str());
        // Two lines if needed
        const size_t len = strlen(nameBuf);
        if (len <= static_cast<size_t>(charsPerLine)) {
          canvasDrawString(kPad + 12, rowY + 22, nameBuf, true, kBody);
        } else {
          char line1[64], line2[64];
          snprintf(line1, sizeof(line1), "%.*s", charsPerLine, nameBuf);
          snprintf(line2, sizeof(line2), "%s", nameBuf + charsPerLine);
          if (strlen(line2) > static_cast<size_t>(charsPerLine)) {
            line2[charsPerLine - 3] = '.';
            line2[charsPerLine - 2] = '.';
            line2[charsPerLine - 1] = '.';
            line2[charsPerLine] = 0;
          }
          canvasDrawString(kPad + 12, rowY + 12, line1, true, kBody);
          canvasDrawString(kPad + 12, rowY + 34, line2, true, kBody);
        }
        const char* folder = "folder";
        canvasDrawString(kPad + cardW - 12 - canvasTextWidth(folder, kSmall), rowY + 24, folder, true,
                         kSmall);
      } else {
        const bool fits = e.size <= maxBytes;
        char nameBuf[96];
        snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
        const size_t len = strlen(nameBuf);
        if (len <= static_cast<size_t>(charsPerLine)) {
          canvasDrawString(kPad + 12, rowY + 22, nameBuf, true, kBody);
        } else {
          char line1[64], line2[64];
          snprintf(line1, sizeof(line1), "%.*s", charsPerLine, nameBuf);
          snprintf(line2, sizeof(line2), "%s", nameBuf + charsPerLine);
          // Prefer showing the end of long names on line 2 (extension visible)
          if (strlen(line2) > static_cast<size_t>(charsPerLine)) {
            const char* start2 = nameBuf + len - charsPerLine;
            snprintf(line2, sizeof(line2), "%s", start2);
          }
          canvasDrawString(kPad + 12, rowY + 12, line1, true, kBody);
          canvasDrawString(kPad + 12, rowY + 34, line2, true, kBody);
        }

        char sz[24];
        appsFormatBytes(e.size, sz, sizeof(sz));
        if (!fits) {
          // Mark oversized
          char marked[28];
          snprintf(marked, sizeof(marked), "!%s", sz);
          canvasDrawString(kPad + cardW - 12 - canvasTextWidth(marked, kBody), rowY + 22, marked,
                           true, kBody);
        } else {
          canvasDrawString(kPad + cardW - 12 - canvasTextWidth(sz, kBody), rowY + 22, sz, true,
                           kBody);
        }
      }
    }
  }

  if (scroll > 0) canvasDrawString(kScreenW / 2 - 4, listTop - 16, "^", true, kBody);
  if (end < static_cast<int>(entries.size())) {
    canvasDrawString(kScreenW / 2 - 4, listBottom - 2, "v", true, kBody);
  }

  const int y = kScreenH - kDockH;
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  canvasFillRect(0, y + 1, kScreenW, kDockH - 1, false);
  const bool canUp = currentPath && !appsIsRootDir(currentPath);
  if (canUp) {
    const int tileW = (kScreenW - 3 * kPad) / 2;
    drawOutlineBtn(kPad, y + 10, tileW, kDockH - 20, "Up");
    drawOutlineBtn(kPad * 2 + tileW, y + 10, tileW, kDockH - 20, "Cancel");
  } else {
    drawOutlineBtn(kPad, y + 10, kScreenW - 2 * kPad, kDockH - 20, "Cancel");
  }
  presentClean();
}

UiHit uiHitPicker(int x, int y, int entryCount, int scroll, bool canGoUp) {
  UiHit hit;
  const int dockY = kScreenH - kDockH;
  if (y >= dockY + 10 && y < dockY + kDockH - 10) {
    if (canGoUp) {
      const int tileW = (kScreenW - 3 * kPad) / 2;
      if (x >= kPad && x < kPad + tileW) {
        hit.kind = UiHit::Kind::GoUp;
        return hit;
      }
      if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
        hit.kind = UiHit::Kind::Back;
        return hit;
      }
    } else if (x >= kPad && x < kScreenW - kPad) {
      hit.kind = UiHit::Kind::Back;
      return hit;
    }
  }

  const int listTop = kStatusH + kPad + 78;
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = uiPickerVisibleRows();
  const int cardW = kScreenW - 2 * kPad;

  if (y < listTop && scroll > 0) {
    hit.kind = UiHit::Kind::ScrollUp;
    return hit;
  }
  if (y > listBottom - 20 && scroll + visible < entryCount) {
    hit.kind = UiHit::Kind::ScrollDown;
    return hit;
  }

  for (int row = 0; row < visible; ++row) {
    const int idx = scroll + row;
    if (idx >= entryCount) break;
    const int rowY = listTop + row * kPickerRowH;
    if (y >= rowY && y < rowY + kPickerRowH - 6 && x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::PickFile;  // main remaps dirs → EnterDir
      hit.index = idx;
      return hit;
    }
  }
  return hit;
}

void uiDrawShade(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, false);

  const ShadeGeom g = shadeGeom();
  gShadePanelBottom = g.panelY + g.panelH;
  drawDimBackdrop(gShadePanelBottom);

  fillLightGrayRoundRect(g.panelX, g.panelY, g.panelW, g.panelH, kShadeRadius);
  canvasDrawRoundRect(g.panelX, g.panelY, g.panelW, g.panelH, kShadeRadius, true);

  // Labels sit on the gray sheet — only interactive chrome gets a white fill.
  canvasDrawString(kPad + 8, g.titleY, "Quick settings", true, kTitle);

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

  drawSectionRule(g.clockRuleY, g.panelX, g.panelW);
  canvasDrawString(kPad + 8, g.clockLabelY, "Time", true, kBody);
  const BoardClockInfo clock = boardClock();
  if (clock.valid) {
    canvasDrawString(kScreenW - kPad - 8 - canvasTextWidth(clock.date, kSmall), g.clockLabelY, clock.date,
                     true, kSmall);
  }

  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%s", clock.valid ? clock.time : "--:--");
  canvasDrawString(kScreenW / 2 - canvasTextWidth(timeBuf, 2) / 2, g.timeValueY, timeBuf, true, 2);

  constexpr int kStepGap = 56;
  drawChromeOutlineBtn(g.hourMinusX, g.clockRowY, kStepBtn, kStepBtn, "-");
  drawChromeOutlineBtn(g.hourPlusX, g.clockRowY, kStepBtn, kStepBtn, "+");
  canvasDrawString(g.hourMinusX + kStepBtn + (kStepGap - canvasTextWidth("Hour", kSmall)) / 2,
                   g.clockRowY + (kStepBtn - canvasTextHeight(kSmall)) / 2, "Hour", true, kSmall);

  drawChromeOutlineBtn(g.minMinusX, g.clockRowY, kStepBtn, kStepBtn, "-");
  drawChromeOutlineBtn(g.minPlusX, g.clockRowY, kStepBtn, kStepBtn, "+");
  canvasDrawString(g.minMinusX + kStepBtn + (kStepGap - canvasTextWidth("Min", kSmall)) / 2,
                   g.clockRowY + (kStepBtn - canvasTextHeight(kSmall)) / 2, "Min", true, kSmall);

  drawSectionRule(g.refreshRuleY, g.panelX, g.panelW);
  canvasDrawString(kPad + 8, g.refreshLabelY, "Display refresh", true, kBody);

  canvasDrawString(kPad + 8, g.cleanRowY + (kStepBtn - canvasTextHeight(kBody)) / 2, "Clean every", true,
                   kBody);
  drawChromeOutlineBtn(g.cleanMinusX, g.cleanRowY, kStepBtn, kStepBtn, "-");
  drawChromeOutlineBtn(g.cleanPlusX, g.cleanRowY, kStepBtn, kStepBtn, "+");
  char every[12];
  snprintf(every, sizeof(every), "%d", boardCleanEvery());
  const int everyX = g.cleanMinusX + kStepBtn + (kStepGap - canvasTextWidth(every, kBody)) / 2;
  canvasDrawString(everyX, g.cleanRowY + (kStepBtn - canvasTextHeight(kBody)) / 2, every, true, kBody);

  drawChromeOutlineBtn(kPad + 8, g.scrubY, kScreenW - 2 * kPad - 16, kActionBtnH, "Scrub screen now");
  drawChromeOutlineBtn(kPad + 8, g.aboutY, kScreenW - 2 * kPad - 16, kActionBtnH, "About");

  canvasFillRoundRect(kScreenW / 2 - 36, g.grabY + (kShadeGrabH - 7) / 2, 72, 7, 3, true);
  present();
}

UiHit uiHitShade(int x, int y) {
  UiHit hit;
  const ShadeGeom g = shadeGeom();
  const int panelBottom = g.panelY + g.panelH;

  if (y >= g.grabY || y >= panelBottom || y < g.panelY) {
    hit.kind = UiHit::Kind::CloseShade;
    return hit;
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

  if (y >= g.clockRowY && y < g.clockRowY + kStepBtn) {
    if (x >= g.hourMinusX && x < g.hourMinusX + kStepBtn) {
      hit.kind = UiHit::Kind::HourMinus;
      return hit;
    }
    if (x >= g.hourPlusX && x < g.hourPlusX + kStepBtn) {
      hit.kind = UiHit::Kind::HourPlus;
      return hit;
    }
    if (x >= g.minMinusX && x < g.minMinusX + kStepBtn) {
      hit.kind = UiHit::Kind::MinuteMinus;
      return hit;
    }
    if (x >= g.minPlusX && x < g.minPlusX + kStepBtn) {
      hit.kind = UiHit::Kind::MinutePlus;
      return hit;
    }
  }

  if (y >= g.cleanRowY && y < g.cleanRowY + kStepBtn) {
    if (x >= g.cleanMinusX && x < g.cleanMinusX + kStepBtn) {
      hit.kind = UiHit::Kind::CleanEveryMinus;
      return hit;
    }
    if (x >= g.cleanPlusX && x < g.cleanPlusX + kStepBtn) {
      hit.kind = UiHit::Kind::CleanEveryPlus;
      return hit;
    }
  }

  if (y >= g.scrubY && y < g.scrubY + kActionBtnH && x >= kPad + 8 && x < kScreenW - kPad - 8) {
    hit.kind = UiHit::Kind::ScrubNow;
    return hit;
  }

  if (y >= g.aboutY && y < g.aboutY + kActionBtnH && x >= kPad + 8 && x < kScreenW - kPad - 8) {
    hit.kind = UiHit::Kind::Settings;
    return hit;
  }

  return hit;
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, false);
  int y = kStatusH + kPad * 2;
  canvasDrawString(kPad, y, "About", true, kTitle);
  y += 36;
  char line[64];
  snprintf(line, sizeof(line), "Basilauncher  v%s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, y, line, true, kBody);
  y += 32;
  canvasDrawString(kPad, y, "Protected factory hub for", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "guest apps in slots A-D.", true, kBody);
  y += 32;
  canvasDrawString(kPad, y, "Install from an empty slot.", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "Swipe down for quick settings.", true, kBody);
  y += 32;
  canvasDrawString(kPad, y, "Hold BOOT to sleep or wake.", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "Double-press RST in any app", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "to return to the launcher.", true, kBody);
  y += 32;
  canvasDrawString(kPad, y, "Power off only sleeps.", true, kBody);
  y += 36;

  canvasDrawString(kPad, y + 12, "Auto-sleep", true, kBody);
  constexpr int step = kStepBtn;
  constexpr int valueW = 80;
  drawOutlineBtn(kScreenW - kPad - step * 2 - valueW, y, step, step, "-");
  drawOutlineBtn(kScreenW - kPad - step, y, step, step, "+");
  char sleepTxt[16];
  if (boardSleepAfterMin() <= 0) snprintf(sleepTxt, sizeof(sleepTxt), "off");
  else snprintf(sleepTxt, sizeof(sleepTxt), "%dm", boardSleepAfterMin());
  const int sx =
      kScreenW - kPad - step * 2 - valueW + step + (valueW - step - canvasTextWidth(sleepTxt, kBody)) / 2;
  canvasDrawString(sx, y + (step - canvasTextHeight(kBody)) / 2, sleepTxt, true, kBody);
  y += step + 24;

  drawOutlineBtn(kPad, y, kScreenW - 2 * kPad, kActionBtnH, "Sleep / power off");
  y += kActionBtnH + 16;
  drawFilledBtn(kPad, y, kScreenW - 2 * kPad, kActionBtnH, "Back");
  presentClean();
}

void uiDrawProgress(const char* title, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  canvasDrawString(kPad, kStatusH + 80, title ? title : "Working...", true, kTitle);
  const int barX = kPad;
  const int barY = kStatusH + 140;
  const int barW = kScreenW - 2 * kPad;
  const int barH = 32;
  canvasDrawRoundRect(barX, barY, barW, barH, 10, true);
  const int fill = (barW - 6) * percent / 100;
  if (fill > 0) canvasFillRoundRect(barX + 3, barY + 3, fill, barH - 6, 6, true);
  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  canvasDrawString(kPad, barY + 48, pct, true, 2);
  present();
}

void uiDrawMessage(const char* title, const char* body) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  canvasDrawString(kPad, kStatusH + 28, title ? title : "Notice", true, kTitle);
  drawWrappedBody(body, kStatusH + 70);
  drawFilledBtn(kPad, kScreenH - kDockH - 70, kScreenW - 2 * kPad, kActionBtnH, "OK");
  presentClean();
}

UiHit uiHitHome(int x, int y) {
  UiHit hit;
  if (y < kStatusH) {
    hit.kind = UiHit::Kind::OpenShade;
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

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  // Layout must match uiDrawSettings (includes BOOT sleep tip line).
  int yCursor = kStatusH + kPad * 2 + 36 + 32 + 22 + 22 + 32 + 22 + 32 + 22 + 22 + 32 + 36;
  constexpr int step = kStepBtn;
  constexpr int valueW = 80;
  if (y >= yCursor && y < yCursor + step) {
    const int minusX = kScreenW - kPad - step * 2 - valueW;
    const int plusX = kScreenW - kPad - step;
    if (x >= minusX && x < minusX + step) {
      hit.kind = UiHit::Kind::SleepAfterMinus;
      return hit;
    }
    if (x >= plusX && x < plusX + step) {
      hit.kind = UiHit::Kind::SleepAfterPlus;
      return hit;
    }
  }
  yCursor += step + 24;
  if (y >= yCursor && y < yCursor + kActionBtnH && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::PowerOff;
    return hit;
  }
  if (y >= yCursor + kActionBtnH + 16 && y < yCursor + kActionBtnH * 2 + 16 && x >= kPad &&
      x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }
  return hit;
}
