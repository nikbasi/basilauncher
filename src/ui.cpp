#include "ui.h"

#include "board_hal.h"
#include "canvas.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 18;
constexpr int kStatusH = 70;
constexpr int kDockH = 72;
constexpr int kCardGap = 12;
constexpr int kRadius = 14;
constexpr int kTitle = 2;
constexpr int kBody = 2;
constexpr int kSmall = 2;
constexpr int kBtnH = 40;
constexpr int kBtnW = 92;
constexpr int kRowH = 56;
constexpr int kBadge = 40;

int slotCardH() {
  const int titleBlock = 28;
  const int footer = 22;
  const int avail = kScreenH - kStatusH - kDockH - kPad * 2 - titleBlock - footer;
  return (avail - (kSlotCount - 1) * kCardGap) / kSlotCount;
}

int slotCardY(int index) {
  return kStatusH + kPad + 28 + index * (slotCardH() + kCardGap);
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
  // Outline battery 28x14 + nub
  canvasDrawRect(x, y, 28, 14, true);
  canvasFillRect(x + 28, y + 4, 3, 6, true);
  const int fillW = std::max(0, std::min(24, (24 * percent) / 100));
  if (fillW > 0) canvasFillRect(x + 2, y + 2, fillW, 10, true);
  if (charging) {
    // Simple lightning hint
    canvasDrawLine(x + 12, y - 2, x + 10, y + 7, true);
    canvasDrawLine(x + 10, y + 7, x + 14, y + 7, true);
    canvasDrawLine(x + 14, y + 7, x + 12, y + 16, true);
  }
}

void drawStatusBar(const FlashSpace& space) {
  canvasFillRect(0, 0, kScreenW, kStatusH, false);
  const BoardClockInfo clock = boardClock();
  const BoardPowerInfo power = boardPower();

  // Top row: clock + battery
  canvasDrawString(kPad, 10, clock.valid ? clock.time : "--:--", true, 3);

  char batt[16];
  if (power.known) {
    snprintf(batt, sizeof(batt), "%d%%", power.percent);
    const int tw = canvasTextWidth(batt, kBody);
    const int bx = kScreenW - kPad - tw - 36;
    drawBatteryGlyph(bx, 12, power.percent, power.charging);
    canvasDrawString(bx + 34, 12, batt, true, kBody);
  } else {
    const char* na = "batt --";
    const int tw = canvasTextWidth(na, kBody);
    canvasDrawString(kScreenW - kPad - tw, 12, na, true, kBody);
  }

  // Second row: brand + version + free
  char brand[40];
  snprintf(brand, sizeof(brand), "Basilauncher  v%s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, 42, brand, true, kTitle);

  char freeBuf[24];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  char freeLine[40];
  snprintf(freeLine, sizeof(freeLine), "free %s", freeBuf);
  const int fw = canvasTextWidth(freeLine, kSmall);
  canvasDrawString(kScreenW - kPad - fw, 42, freeLine, true, kSmall);

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

  // Letter badge
  canvasFillRoundRect(x + 14, y + 14, kBadge, kBadge, 10, true);
  char letter[2] = {slot.label[0], 0};
  const int lw = canvasTextWidth(letter, 3);
  canvasDrawString(x + 14 + (kBadge - lw) / 2, y + 14 + (kBadge - 24) / 2, letter, false, 3);

  const int textX = x + 14 + kBadge + 14;
  if (slot.occupied) {
    char name[40];
    snprintf(name, sizeof(name), "%s", slot.name.c_str());
    truncate(name, 18);
    canvasDrawString(textX, y + 18, name, true, kTitle);

    char sz[48];
    char used[24], cap[24];
    appsFormatBytes(slot.size ? slot.size : slot.capacity, used, sizeof(used));
    appsFormatBytes(slot.capacity, cap, sizeof(cap));
    snprintf(sz, sizeof(sz), "%s  /  %s", used, cap);
    canvasDrawString(textX, y + 46, sz, true, kBody);

    const int by = y + h - kBtnH - 14;
    drawOutlineBtn(x + w - 2 * kBtnW - 28, by, kBtnW, kBtnH, "Clear");
    drawFilledBtn(x + w - kBtnW - 14, by, kBtnW, kBtnH, "Boot");
  } else {
    canvasDrawString(textX, y + 18, "Empty", true, kTitle);
    char cap[40];
    char capBuf[24];
    appsFormatBytes(slot.capacity, capBuf, sizeof(capBuf));
    snprintf(cap, sizeof(cap), "holds up to %s", capBuf);
    canvasDrawString(textX, y + 46, cap, true, kBody);

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
    y += 26;
  }
}

}  // namespace

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space);

  canvasDrawString(kPad, kStatusH + kPad - 2, "Apps", true, kTitle);
  for (int i = 0; i < kSlotCount; ++i) drawSlotCard(i, slots[i]);

  char foot[48];
  char total[24];
  appsFormatBytes(space.guestTotal, total, sizeof(total));
  snprintf(foot, sizeof(foot), "%d empty · %s guest flash", space.emptySlots, total);
  const int fy = kScreenH - kDockH - 20;
  canvasDrawString(kPad, fy, foot, true, kSmall);

  drawDockTwo("Settings", "Install");
  present();
}

void uiDrawPicker(const std::vector<FirmwareFile>& files, int scroll, int targetSlot,
                  size_t maxBytes, const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space);

  char title[48];
  if (targetSlot >= 0 && targetSlot < kSlotCount) {
    snprintf(title, sizeof(title), "Assign to slot %c", 'A' + targetSlot);
  } else {
    snprintf(title, sizeof(title), "Install (best fit)");
  }
  canvasDrawString(kPad, kStatusH + kPad, title, true, kTitle);

  char hint[56];
  char maxBuf[24];
  appsFormatBytes(maxBytes, maxBuf, sizeof(maxBuf));
  snprintf(hint, sizeof(hint), "Showing files up to %s", maxBuf);
  canvasDrawString(kPad, kStatusH + kPad + 26, hint, true, kSmall);

  const int listTop = kStatusH + kPad + 56;
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = std::max(1, (listBottom - listTop) / kRowH);
  const int cardW = kScreenW - 2 * kPad;
  const int start = scroll;
  const int end = std::min(static_cast<int>(files.size()), start + visible);

  if (files.empty()) {
    canvasDrawString(kPad, listTop + 8,
                     boardSdOk() ? "No fitting .bin on SD" : "Insert SD card", true, kBody);
  } else {
    for (int i = start; i < end; ++i) {
      const int rowY = listTop + (i - start) * kRowH;
      canvasDrawRoundRect(kPad, rowY, cardW, kRowH - 8, 10, true);
      char line[48];
      char sz[24];
      appsFormatBytes(files[i].size, sz, sizeof(sz));
      snprintf(line, sizeof(line), "%s", files[i].name.c_str());
      truncate(line, 22);
      canvasDrawString(kPad + 14, rowY + 8, line, true, kBody);
      canvasDrawString(kPad + 14, rowY + 30, sz, true, kSmall);
    }
  }

  if (scroll > 0) canvasDrawString(kScreenW / 2 - 8, listTop - 20, "^", true, kBody);
  if (end < static_cast<int>(files.size())) {
    canvasDrawString(kScreenW / 2 - 8, listBottom - 2, "v", true, kBody);
  }

  const int y = kScreenH - kDockH;
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  canvasFillRect(0, y + 1, kScreenW, kDockH - 1, false);
  drawOutlineBtn(kPad, y + 10, kScreenW - 2 * kPad, kDockH - 20, "Cancel");
  present();
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space);
  int y = kStatusH + kPad * 2;
  canvasDrawString(kPad, y, "About", true, kTitle);
  y += 32;
  char line[64];
  snprintf(line, sizeof(line), "Version %s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, y, line, true, kBody);
  y += 28;
  canvasDrawString(kPad, y, "Launcher flash is protected.", true, kBody);
  y += 26;
  canvasDrawString(kPad, y, "Install only into empty slots.", true, kBody);
  y += 26;
  canvasDrawString(kPad, y, "Clear a slot to free space.", true, kBody);
  y += 26;
  canvasDrawString(kPad, y, "Reboot always returns here.", true, kBody);
  y += 40;

  const BoardClockInfo clock = boardClock();
  if (clock.valid) {
    snprintf(line, sizeof(line), "Clock  %s  %s", clock.time, clock.date);
    canvasDrawString(kPad, y, line, true, kBody);
    y += 28;
  }
  const BoardPowerInfo power = boardPower();
  if (power.known) {
    snprintf(line, sizeof(line), "Battery  %d%%%s", power.percent,
             power.charging ? "  charging" : "");
    canvasDrawString(kPad, y, line, true, kBody);
    y += 40;
  } else {
    y += 12;
  }

  drawOutlineBtn(kPad, y, kScreenW - 2 * kPad, 48, "Power off");
  y += 64;
  drawFilledBtn(kPad, y, kScreenW - 2 * kPad, 48, "Back");

  present();
}

void uiDrawProgress(const char* title, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  canvasClear();
  drawStatusBar(appsFlashSpace());
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
  canvasDrawString(kPad, barY + 48, pct, true, 3);
  present();
}

void uiDrawMessage(const char* title, const char* body) {
  canvasClear();
  drawStatusBar(appsFlashSpace());
  canvasDrawString(kPad, kStatusH + 28, title ? title : "Notice", true, kTitle);
  drawWrappedBody(body, kStatusH + 70);
  drawFilledBtn(kPad, kScreenH - kDockH - 70, kScreenW - 2 * kPad, 48, "OK");
  present();
}

int uiPickerMaxScroll(int fileCount) {
  (void)fileCount;
  return 0;  // computed in hit with fitting filter; main tracks fitting count
}

UiHit uiHitHome(int x, int y) {
  UiHit hit;
  const int dockY = kScreenH - kDockH;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (y >= dockY + 10 && y < dockY + kDockH - 10) {
    if (x >= kPad && x < kPad + tileW) {
      hit.kind = UiHit::Kind::Settings;
      return hit;
    }
    if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
      hit.kind = UiHit::Kind::OpenPicker;
      hit.index = -1;  // best-fit
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
      // occupied: Clear then Boot; empty: Assign
      // We don't know occupied here — main checks. Hit both regions.
      const int bootX = kPad + cardW - kBtnW - 14;
      const int clearX = kPad + cardW - 2 * kBtnW - 28;
      if (x >= bootX && x < bootX + kBtnW) {
        // Boot or Assign (same position)
        hit.kind = UiHit::Kind::BootSlot;  // main remaps empty → Assign
        hit.index = i;
        return hit;
      }
      if (x >= clearX && x < clearX + kBtnW) {
        hit.kind = UiHit::Kind::ClearSlot;
        hit.index = i;
        return hit;
      }
    }
    // Tap card body: Boot if occupied, Assign if empty — main decides
    if (x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::BootSlot;
      hit.index = i;
      return hit;
    }
  }
  return hit;
}

UiHit uiHitPicker(int x, int y, int fileCount, int scroll) {
  UiHit hit;
  const int dockY = kScreenH - kDockH;
  if (y >= dockY + 10 && y < dockY + kDockH - 10 && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }

  const int listTop = kStatusH + kPad + 56;
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = std::max(1, (listBottom - listTop) / kRowH);
  const int cardW = kScreenW - 2 * kPad;

  if (y < listTop && scroll > 0) {
    hit.kind = UiHit::Kind::ScrollUp;
    return hit;
  }
  if (y > listBottom - 20) {
    hit.kind = UiHit::Kind::ScrollDown;
    return hit;
  }

  for (int row = 0; row < visible; ++row) {
    const int rowY = listTop + row * kRowH;
    if (y >= rowY && y < rowY + kRowH - 8 && x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::PickFile;
      hit.index = scroll + row;  // index into filtered list; main maps
      return hit;
    }
  }
  (void)fileCount;
  return hit;
}

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  // Approximate: Power off then Back — match draw order with clock/battery optional.
  // Use broad zones near bottom of content.
  int yPower = kStatusH + kPad * 2 + 32 + 28 + 26 * 4 + 40;
  const BoardClockInfo clock = boardClock();
  if (clock.valid) yPower += 28;
  const BoardPowerInfo power = boardPower();
  if (power.known) yPower += 40;
  else yPower += 12;

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
