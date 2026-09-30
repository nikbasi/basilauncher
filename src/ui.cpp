#include "ui.h"

#include "board_hal.h"
#include "canvas.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 16;
constexpr int kStatusH = 56;
constexpr int kDockH = 64;
constexpr int kCardH = 70;
constexpr int kCardGap = 8;
constexpr int kRowH = 48;
constexpr int kRadius = 10;
constexpr int kTitle = 2;
constexpr int kBody = 2;
constexpr int kBtnW = 70;
constexpr int kBtnH = 36;

int slotsBlockBottom() {
  return kStatusH + kPad + 24 + kSlotCount * (kCardH + kCardGap);
}

void drawStatusBar(const FlashSpace& space) {
  canvasFillRect(0, 0, kScreenW, kStatusH, false);
  char left[40];
  snprintf(left, sizeof(left), "Basilauncher  v%s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, 8, left, true, kTitle);

  char freeBuf[24], totalBuf[24];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  appsFormatBytes(space.guestTotal, totalBuf, sizeof(totalBuf));
  char right[64];
  snprintf(right, sizeof(right), "free %s / %s", freeBuf, totalBuf);
  canvasDrawString(kPad, 32, right, true, kBody);

  const char* sd = boardSdOk() ? "SD ok" : "No SD";
  const int tw = canvasTextWidth(sd, kBody);
  canvasDrawString(kScreenW - kPad - tw, 32, sd, true, kBody);

  canvasDrawLine(0, kStatusH - 1, kScreenW - 1, kStatusH - 1, true);
}

void drawDock() {
  const int y = kScreenH - kDockH;
  canvasDrawLine(0, y, kScreenW - 1, y, true);
  canvasFillRect(0, y + 1, kScreenW, kDockH - 1, false);
  const int tileW = (kScreenW - 3 * kPad) / 2;
  const int tileH = kDockH - 20;
  const int tileY = y + 10;

  canvasDrawRoundRect(kPad, tileY, tileW, tileH, 10, true);
  {
    const char* label = "Apps";
    const int tw = canvasTextWidth(label, kBody);
    canvasDrawString(kPad + (tileW - tw) / 2, tileY + (tileH - 16) / 2, label, true, kBody);
  }

  canvasDrawRoundRect(kPad * 2 + tileW, tileY, tileW, tileH, 10, true);
  {
    const char* label = "Settings";
    const int tw = canvasTextWidth(label, kBody);
    canvasDrawString(kPad * 2 + tileW + (tileW - tw) / 2, tileY + (tileH - 16) / 2, label, true,
                     kBody);
  }
}

void drawBtn(int x, int y, int w, int h, const char* label) {
  canvasDrawRoundRect(x, y, w, h, 8, true);
  const int tw = canvasTextWidth(label, kBody);
  canvasDrawString(x + (w - tw) / 2, y + (h - 16) / 2, label, true, kBody);
}

void drawSlotCard(int x, int y, int w, int h, const SlotInfo& slot) {
  canvasDrawRoundRect(x, y, w, h, kRadius, true);

  char title[40];
  char capBuf[24];
  appsFormatBytes(slot.occupied ? (slot.size ? slot.size : slot.capacity) : slot.capacity, capBuf,
                  sizeof(capBuf));
  snprintf(title, sizeof(title), "%s  %s", slot.label, slot.occupied ? slot.name.c_str() : "empty");
  if (strlen(title) > 22) {
    title[19] = '.';
    title[20] = '.';
    title[21] = '.';
    title[22] = 0;
  }
  canvasDrawString(x + 12, y + 12, title, true, kBody);
  canvasDrawString(x + 12, y + 40, capBuf, true, kBody);

  const int by = y + (h - kBtnH) / 2;
  if (slot.occupied) {
    drawBtn(x + w - 2 * kBtnW - 20, by, kBtnW, kBtnH, "Boot");
    drawBtn(x + w - kBtnW - 12, by, kBtnW, kBtnH, "Clear");
  } else {
    canvasDrawString(x + w - 90, by + 10, "(open)", true, kBody);
  }
}

void present() { canvasPresent(EInkDisplay::HALF_REFRESH); }

}  // namespace

int uiHomeListTopY() {
  return slotsBlockBottom() + 8 + 24;
}

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space,
                const std::vector<FirmwareFile>& files, int scroll) {
  canvasClear();
  drawStatusBar(space);

  int y = kStatusH + kPad;
  canvasDrawString(kPad, y, "Guest slots (launcher protected)", true, kTitle);
  y += 24;

  const int cardW = kScreenW - 2 * kPad;
  for (int i = 0; i < kSlotCount; ++i) {
    drawSlotCard(kPad, y, cardW, kCardH, slots[i]);
    y += kCardH + kCardGap;
  }

  y += 8;
  canvasDrawString(kPad, y, "On SD /firmware  (tap = best-fit install)", true, kTitle);
  y += 24;

  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = std::max(1, (listBottom - y) / kRowH);
  const int start = scroll;
  const int end = std::min(static_cast<int>(files.size()), start + visible);
  const int cardW2 = kScreenW - 2 * kPad;

  if (files.empty()) {
    canvasDrawString(kPad, y + 8, boardSdOk() ? "No .bin files" : "Insert SD card", true, kBody);
  } else {
    for (int i = start; i < end; ++i) {
      const int rowY = y + (i - start) * kRowH;
      canvasDrawRoundRect(kPad, rowY, cardW2, kRowH - 6, 8, true);
      char line[56];
      char sz[24];
      appsFormatBytes(files[i].size, sz, sizeof(sz));
      snprintf(line, sizeof(line), "%s  %s", files[i].name.c_str(), sz);
      if (strlen(line) > 30) {
        line[27] = '.';
        line[28] = '.';
        line[29] = '.';
        line[30] = 0;
      }
      canvasDrawString(kPad + 10, rowY + 12, line, true, kBody);
    }
  }

  drawDock();
  present();
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space);
  int y = kStatusH + kPad * 2;
  canvasDrawString(kPad, y, "About", true, kTitle);
  y += 28;
  char line[64];
  snprintf(line, sizeof(line), "Version %s", BASILAUNCHER_VERSION);
  canvasDrawString(kPad, y, line, true, kBody);
  y += 24;
  canvasDrawString(kPad, y, "Factory partition is read-only.", true, kBody);
  y += 24;
  canvasDrawString(kPad, y, "Install never overwrites apps.", true, kBody);
  y += 24;
  canvasDrawString(kPad, y, "Clear a slot to free its space.", true, kBody);
  y += 24;
  canvasDrawString(kPad, y, "Reboot returns here.", true, kBody);
  y += 40;

  canvasDrawRoundRect(kPad, y, kScreenW - 2 * kPad, 48, 10, true);
  canvasDrawString(kPad + 20, y + 16, "Power off", true, kBody);
  y += 64;

  canvasDrawRoundRect(kPad, y, kScreenW - 2 * kPad, 48, 10, true);
  canvasDrawString(kPad + 20, y + 16, "Back", true, kBody);

  drawDock();
  present();
}

void uiDrawProgress(const char* title, int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  canvasClear();
  drawStatusBar(appsFlashSpace());
  canvasDrawString(kPad, kStatusH + 60, title ? title : "Writing...", true, kBody);
  const int barX = kPad;
  const int barY = kStatusH + 110;
  const int barW = kScreenW - 2 * kPad;
  const int barH = 28;
  canvasDrawRoundRect(barX, barY, barW, barH, 8, true);
  const int fill = (barW - 4) * percent / 100;
  if (fill > 0) canvasFillRect(barX + 2, barY + 2, fill, barH - 4, true);
  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  canvasDrawString(kPad, barY + 40, pct, true, kTitle);
  present();
}

void uiDrawMessage(const char* title, const char* body) {
  canvasClear();
  FlashSpace space = appsFlashSpace();
  drawStatusBar(space);
  canvasDrawString(kPad, kStatusH + 20, title ? title : "Notice", true, kTitle);
  // Simple wrap: up to 3 lines of ~30 chars
  if (body) {
    char buf[96];
    strncpy(buf, body, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int y = kStatusH + 56;
    const char* p = buf;
    for (int line = 0; line < 4 && *p; ++line) {
      char row[32];
      size_t n = 0;
      while (p[n] && n < 30) ++n;
      memcpy(row, p, n);
      row[n] = 0;
      canvasDrawString(kPad, y, row, true, kBody);
      p += n;
      y += 24;
    }
  }
  canvasDrawRoundRect(kPad, kScreenH - kDockH - 70, kScreenW - 2 * kPad, 48, 10, true);
  canvasDrawString(kPad + 20, kScreenH - kDockH - 54, "OK", true, kBody);
  present();
}

int uiHomeMaxScroll(int fileCount) {
  const int yList = uiHomeListTopY();
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = std::max(1, (listBottom - yList) / kRowH);
  return std::max(0, fileCount - visible);
}

UiHit uiHitHome(int x, int y, int fileCount, int scroll) {
  UiHit hit;

  const int dockY = kScreenH - kDockH;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (y >= dockY + 10 && y < dockY + kDockH - 10) {
    if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
      hit.kind = UiHit::Kind::Settings;
      return hit;
    }
  }

  int cy = kStatusH + kPad + 24;
  const int cardW = kScreenW - 2 * kPad;
  for (int i = 0; i < kSlotCount; ++i) {
    if (y >= cy && y < cy + kCardH) {
      const int by = cy + (kCardH - kBtnH) / 2;
      const int bootX = kPad + cardW - 2 * kBtnW - 20;
      const int clearX = kPad + cardW - kBtnW - 12;
      if (y >= by && y < by + kBtnH) {
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
    }
    cy += kCardH + kCardGap;
  }

  cy = uiHomeListTopY();
  const int listBottom = kScreenH - kDockH - kPad;
  const int visible = std::max(1, (listBottom - cy) / kRowH);
  for (int i = 0; i < visible; ++i) {
    const int idx = scroll + i;
    if (idx >= fileCount) break;
    const int rowY = cy + i * kRowH;
    if (y >= rowY && y < rowY + kRowH - 6 && x >= kPad && x < kPad + cardW) {
      hit.kind = UiHit::Kind::InstallFile;
      hit.index = idx;
      return hit;
    }
  }
  return hit;
}

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  int sy = kStatusH + kPad * 2 + 28 + 24 * 5 + 40;
  if (y >= sy && y < sy + 48 && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::PowerOff;
    return hit;
  }
  sy += 64;
  if (y >= sy && y < sy + 48 && x >= kPad && x < kScreenW - kPad) {
    hit.kind = UiHit::Kind::Back;
    return hit;
  }
  const int dockY = kScreenH - kDockH;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (y >= dockY + 10 && y < dockY + kDockH - 10) {
    if (x >= kPad && x < kPad + tileW) {
      hit.kind = UiHit::Kind::Back;
      return hit;
    }
  }
  return hit;
}
