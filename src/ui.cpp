#include "ui.h"

#include "board_hal.h"
#include "canvas.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 18;
constexpr int kStatusH = 74;
constexpr int kDockH = 72;
constexpr int kCardGap = 12;
constexpr int kRadius = 14;
constexpr int kTitle = 2;  // 16×32
constexpr int kBody = 1;   // 8×16
constexpr int kSmall = 1;
constexpr int kBtnH = 42;
constexpr int kBtnW = 100;
constexpr int kRowH = 58;
constexpr int kBadge = 44;

// Shade layout
constexpr int kShadeGrabH = 28;
constexpr int kStepBtn = 48;
int gBrightX = 0, gBrightY = 0, gBrightW = 0, gBrightH = 0;

int slotCardH() {
  const int titleBlock = 30;
  const int footer = 24;
  const int avail = kScreenH - kStatusH - kDockH - kPad * 2 - titleBlock - footer;
  return (avail - (kSlotCount - 1) * kCardGap) / kSlotCount;
}

int slotCardY(int index) {
  return kStatusH + kPad + 30 + index * (slotCardH() + kCardGap);
}

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

  char freeBuf[24];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  char freeLine[40];
  snprintf(freeLine, sizeof(freeLine), "free %s", freeBuf);
  canvasDrawString(kScreenW - kPad - canvasTextWidth(freeLine, kSmall), 44, freeLine, true, kSmall);

  if (shadeHint) {
    // Grabber cue in the status bar
    canvasFillRoundRect(kScreenW / 2 - 28, kStatusH - 10, 56, 5, 2, true);
  }
  canvasDrawLine(0, kStatusH - 1, kScreenW - 1, kStatusH - 1, true);
}

void drawOutlineBtn(int x, int y, int w, int h, const char* label) {
  canvasDrawRoundRect(x, y, w, h, 10, true);
  const int tw = canvasTextWidth(label, kBody);
  canvasDrawString(x + (w - tw) / 2, y + (h - 16) / 2, label, true, kBody);
}

void drawFilledBtn(int x, int y, int w, int h, const char* label) {
  canvasFillRoundRect(x, y, w, h, 10, true);
  const int tw = canvasTextWidth(label, kBody);
  canvasDrawString(x + (w - tw) / 2, y + (h - 16) / 2, label, false, kBody);
}

void drawDockTwo(const char* left, const char* right) {
  const int y = kScreenH - kDockH;
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  canvasFillRect(0, y + 1, kScreenW, kDockH - 1, false);
  const int tileW = (kScreenW - 3 * kPad) / 2;
  const int tileH = kDockH - 20;
  const int tileY = y + 10;
  drawOutlineBtn(kPad, tileY, tileW, tileH, left);
  drawFilledBtn(kPad * 2 + tileW, tileY, tileW, tileH, right);
}

void drawSlotCard(int index, const SlotInfo& slot) {
  const int x = kPad;
  const int y = slotCardY(index);
  const int w = kScreenW - 2 * kPad;
  const int h = slotCardH();

  canvasDrawRoundRect(x, y, w, h, kRadius, true);

  canvasFillRoundRect(x + 14, y + 16, kBadge, kBadge, 10, true);
  char letter[2] = {slot.label[0], 0};
  const int lw = canvasTextWidth(letter, 2);
  canvasDrawString(x + 14 + (kBadge - lw) / 2, y + 16 + (kBadge - 32) / 2, letter, false, 2);

  const int textX = x + 14 + kBadge + 14;
  if (slot.occupied) {
    char name[40];
    snprintf(name, sizeof(name), "%s", slot.name.c_str());
    truncate(name, 18);
    canvasDrawString(textX, y + 18, name, true, kTitle);

    char sz[48], used[24], cap[24];
    appsFormatBytes(slot.size ? slot.size : slot.capacity, used, sizeof(used));
    appsFormatBytes(slot.capacity, cap, sizeof(cap));
    snprintf(sz, sizeof(sz), "%s / %s", used, cap);
    canvasDrawString(textX, y + 54, sz, true, kBody);

    const int by = y + h - kBtnH - 14;
    drawOutlineBtn(x + w - 2 * kBtnW - 28, by, kBtnW, kBtnH, "Clear");
    drawFilledBtn(x + w - kBtnW - 14, by, kBtnW, kBtnH, "Boot");
  } else {
    canvasDrawString(textX, y + 18, "Empty", true, kTitle);
    char cap[40], capBuf[24];
    appsFormatBytes(slot.capacity, capBuf, sizeof(capBuf));
    snprintf(cap, sizeof(cap), "holds up to %s", capBuf);
    canvasDrawString(textX, y + 54, cap, true, kBody);
    const int by = y + h - kBtnH - 14;
    drawFilledBtn(x + w - kBtnW - 14, by, kBtnW, kBtnH, "Assign");
  }
}

void present() { canvasPresent(EInkDisplay::HALF_REFRESH); }

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

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);

  canvasDrawString(kPad, kStatusH + kPad - 2, "Apps", true, kTitle);
  for (int i = 0; i < kSlotCount; ++i) drawSlotCard(i, slots[i]);

  char foot[48], total[24];
  appsFormatBytes(space.guestTotal, total, sizeof(total));
  snprintf(foot, sizeof(foot), "%d empty · %s guest", space.emptySlots, total);
  canvasDrawString(kPad, kScreenH - kDockH - 22, foot, true, kSmall);

  drawDockTwo("Settings", "Install");
  present();
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
  present();
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

  // Panel card
  const int panelY = kStatusH + 8;
  const int panelH = kScreenH - panelY - kPad;
  canvasDrawRoundRect(kPad / 2, panelY, kScreenW - kPad, panelH, 16, true);

  // Grabber
  canvasFillRoundRect(kScreenW / 2 - 32, panelY + 10, 64, 6, 3, true);
  canvasDrawString(kPad + 8, panelY + kShadeGrabH, "Quick settings", true, kTitle);

  int y = panelY + kShadeGrabH + 40;

  // Brightness
  canvasDrawString(kPad + 8, y, "Frontlight", true, kBody);
  y += 28;

  const bool hasLight = boardHasFrontlight();
  const int bright = boardBrightness();
  const bool lightOn = boardFrontlightOn();

  drawOutlineBtn(kPad + 8, y, kStepBtn, kStepBtn, "-");
  drawOutlineBtn(kScreenW - kPad - 8 - kStepBtn, y, kStepBtn, kStepBtn, "+");

  gBrightX = kPad + 8 + kStepBtn + 12;
  gBrightY = y + 8;
  gBrightW = kScreenW - 2 * (kPad + 8 + kStepBtn + 12);
  gBrightH = kStepBtn - 16;
  canvasDrawRoundRect(gBrightX, gBrightY, gBrightW, gBrightH, 8, true);
  if (hasLight && lightOn && bright > 0) {
    const int fill = std::max(4, (gBrightW * bright) / 100);
    canvasFillRoundRect(gBrightX + 2, gBrightY + 2, fill - 4, gBrightH - 4, 6, true);
  }

  char pct[16];
  snprintf(pct, sizeof(pct), hasLight ? "%d%%" : "n/a", bright);
  canvasDrawString(gBrightX + gBrightW / 2 - canvasTextWidth(pct, kBody) / 2, y + kStepBtn + 6, pct,
                   true, kBody);
  y += kStepBtn + 36;

  if (hasLight) {
    if (lightOn) drawFilledBtn(kPad + 8, y, 140, 44, "Light on");
    else drawOutlineBtn(kPad + 8, y, 140, 44, "Light off");
  }
  y += 60;

  // Clock
  canvasDrawString(kPad + 8, y, "Time", true, kBody);
  y += 28;
  const BoardClockInfo clock = boardClock();
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%s", clock.valid ? clock.time : "--:--");
  canvasDrawString(kScreenW / 2 - canvasTextWidth(timeBuf, 2) / 2, y, timeBuf, true, 2);
  y += 40;

  // Hour row
  canvasDrawString(kPad + 8, y + 12, "Hour", true, kSmall);
  drawOutlineBtn(kScreenW / 2 - 60 - kStepBtn, y, kStepBtn, kStepBtn, "-");
  drawOutlineBtn(kScreenW / 2 + 60, y, kStepBtn, kStepBtn, "+");
  y += kStepBtn + 16;

  // Minute row
  canvasDrawString(kPad + 8, y + 12, "Min", true, kSmall);
  drawOutlineBtn(kScreenW / 2 - 60 - kStepBtn, y, kStepBtn, kStepBtn, "-");
  drawOutlineBtn(kScreenW / 2 + 60, y, kStepBtn, kStepBtn, "+");
  y += kStepBtn + 28;

  if (clock.valid) {
    canvasDrawString(kPad + 8, y, clock.date, true, kSmall);
    y += 28;
  }

  drawFilledBtn(kPad + 8, kScreenH - kPad - 56, kScreenW - 2 * kPad - 16, 48, "Done");
  present();
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, false);
  int y = kStatusH + kPad * 2;
  canvasDrawString(kPad, y, "About", true, kTitle);
  y += 36;
  char line[64];
  snprintf(line, sizeof(line), "Version %s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, y, line, true, kBody);
  y += 28;
  canvasDrawString(kPad, y, "Swipe down from the top for", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "brightness and clock.", true, kBody);
  y += 28;
  canvasDrawString(kPad, y, "Launcher flash is protected.", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "Power off only sleeps.", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "Double-press RST in any app", true, kBody);
  y += 22;
  canvasDrawString(kPad, y, "to return to the launcher.", true, kBody);
  y += 40;

  drawOutlineBtn(kPad, y, kScreenW - 2 * kPad, 48, "Sleep / power off");
  y += 64;
  drawFilledBtn(kPad, y, kScreenW - 2 * kPad, 48, "Back");
  present();
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
  drawFilledBtn(kPad, kScreenH - kDockH - 70, kScreenW - 2 * kPad, 48, "OK");
  present();
}

UiHit uiHitHome(int x, int y) {
  UiHit hit;
  if (y < kStatusH) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }

  const int dockY = kScreenH - kDockH;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (y >= dockY + 10 && y < dockY + kDockH - 10) {
    if (x >= kPad && x < kPad + tileW) {
      hit.kind = UiHit::Kind::Settings;
      return hit;
    }
    if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
      hit.kind = UiHit::Kind::OpenPicker;
      return hit;
    }
  }

  const int cardW = kScreenW - 2 * kPad;
  for (int i = 0; i < kSlotCount; ++i) {
    const int cy = slotCardY(i);
    const int h = slotCardH();
    if (y < cy || y >= cy + h) continue;
    const int by = cy + h - kBtnH - 14;
    if (y >= by && y < by + kBtnH) {
      const int bootX = kPad + cardW - kBtnW - 14;
      const int clearX = kPad + cardW - 2 * kBtnW - 28;
      if (x >= bootX && x < bootX + kBtnW) {
        hit.kind = UiHit::Kind::BootSlot;
        hit.index = i;
        return hit;
      }
      if (x >= clearX && x < clearX + kBtnW) {
        hit.kind = UiHit::Kind::ClearSlot;
        hit.index = i;
        return hit;
      }
    }
    if (x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::BootSlot;
      hit.index = i;
      return hit;
    }
  }
  return hit;
}

UiHit uiHitShade(int x, int y) {
  UiHit hit;
  const int panelY = kStatusH + 8;
  int yy = panelY + kShadeGrabH + 40 + 28;  // after "Frontlight" label

  // - brightness
  if (y >= yy && y < yy + kStepBtn) {
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
  yy += kStepBtn + 36;

  // light toggle
  if (boardHasFrontlight() && y >= yy && y < yy + 44 && x >= kPad + 8 && x < kPad + 148) {
    hit.kind = UiHit::Kind::LightToggle;
    return hit;
  }
  yy += 60 + 28;  // Time label
  yy += 40;       // big time display

  // Hour -/+
  if (y >= yy && y < yy + kStepBtn) {
    if (x >= kScreenW / 2 - 60 - kStepBtn && x < kScreenW / 2 - 60) {
      hit.kind = UiHit::Kind::HourMinus;
      return hit;
    }
    if (x >= kScreenW / 2 + 60 && x < kScreenW / 2 + 60 + kStepBtn) {
      hit.kind = UiHit::Kind::HourPlus;
      return hit;
    }
  }
  yy += kStepBtn + 16;

  // Minute -/+
  if (y >= yy && y < yy + kStepBtn) {
    if (x >= kScreenW / 2 - 60 - kStepBtn && x < kScreenW / 2 - 60) {
      hit.kind = UiHit::Kind::MinuteMinus;
      return hit;
    }
    if (x >= kScreenW / 2 + 60 && x < kScreenW / 2 + 60 + kStepBtn) {
      hit.kind = UiHit::Kind::MinutePlus;
      return hit;
    }
  }

  if (y >= kScreenH - kPad - 56 && y < kScreenH - kPad - 8) {
    hit.kind = UiHit::Kind::CloseShade;
    return hit;
  }
  return hit;
}

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  int yPower = kStatusH + kPad * 2 + 36 + 28 + 22 + 22 + 28 + 22 + 22 + 40;
  if (y >= yPower && y < yPower + 48 && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::PowerOff;
    return hit;
  }
  if (y >= yPower + 64 && y < yPower + 112 && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }
  return hit;
}
