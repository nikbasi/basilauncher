#include "ui.h"

#include "board_hal.h"
#include "canvas.h"
#include "gps.h"
#include "file_ops.h"
#include "qr_draw.h"
#include "ui_icons.h"
#include "wifi_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int kPad = 18;
constexpr int kExplorerDockH = 80;   // icon + label tools
constexpr int kHomeFooterH = 76;     // Files / Web / Settings
constexpr int kRadius = 16;
constexpr int kTitle = 2;
constexpr int kBody = 1;
constexpr int kSmall = 0;  // caption: one size below body
constexpr int kHeaderH = 64;  // back arrow + screen title, under the status bar
constexpr int kBtnRadius = 14;
constexpr int kIconRadius = 12;   // dock, file rows, up chip
constexpr int kQsIconRadius = 18;  // quick-settings icon
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

// One row: time and date on the left, Wi-Fi, charge and battery on the right,
// and the shade's pull handle under it. Scales with the text-size setting.
int statusBarH() { return 10 + canvasTextHeight(kBody) + 18; }

int contentTop() { return statusBarH() + kHeaderH; }

void drawIcon(const UiIcon& icon, int cx, int cy, bool black = true) {
  canvasDrawColumnBitmap(cx - icon.w / 2, cy - icon.h / 2, icon.w, icon.h, icon.bits, black);
}

// Screen header: a back arrow (when the screen has somewhere to go back to)
// and the title. The arrow's tap target is the full header height.
constexpr int kHeaderBackW = 56;

void drawHeader(const char* title, bool back) {
  const int top = statusBarH();
  const int cy = top + kHeaderH / 2;
  int textX = kPad;
  if (back) {
    drawIcon(kIconBack28, kPad + 12, cy);
    textX = kPad + kHeaderBackW - 8;
  }
  canvasDrawString(textX, cy - canvasTextHeight(kTitle) / 2, title, true, kTitle);
}

bool hitHeaderBack(int x, int y) {
  const int top = statusBarH();
  return y >= top && y < top + kHeaderH && x < kPad + kHeaderBackW;
}

// Optional icon action at the header's right edge (Scan on Wi-Fi).
void headerActionRect(int& x, int& y, int& w, int& h) {
  w = 56;
  h = 48;
  x = kScreenW - kPad - w;
  y = statusBarH() + (kHeaderH - h) / 2;
}

void drawChevronUp(int cx, int cy) {
  // Tip at smaller y (screen up). Thick strokes for e-ink.
  for (int t = 0; t < 3; ++t) {
    canvasDrawLine(cx - 12 + t, cy + 8, cx, cy + t, true);
    canvasDrawLine(cx + 12 - t, cy + 8, cx, cy + t, true);
  }
}

// Shared pull handle. Closed: a plain pill under the status row (pull the
// shade down). Open: an up cue on the shade (push it away).
void drawShadeGrabCue(int topY, bool menuOpen) {
  const int cx = kScreenW / 2;
  const int pillX = cx - kGrabPillW / 2;
  if (menuOpen) {
    drawChevronUp(cx, topY);
    canvasFillRoundRect(pillX, topY + 10, kGrabPillW, kGrabPillH, kGrabPillH / 2, true);
  } else {
    canvasFillRoundRect(pillX, topY, kGrabPillW, kGrabPillH - 1, (kGrabPillH - 1) / 2, true);
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
  int netX = 0;
  int gpsX = 0;
  int settingsX = 0;
  int frontRuleY = 0;
  int frontLabelY = 0;
  int sliderY = 0;
  int lightY = 0;
  int scrubY = 0;
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
  const int labelW = std::max(canvasTextWidth("Transfer", kBody),
                              std::max(canvasTextWidth("Wi-Fi", kBody),
                                       std::max(canvasTextWidth("GPS", kBody), canvasTextWidth("Settings", kBody))));
  g.tileW = std::max(g.icon, labelW + 8);
  g.tileH = g.icon + 6 + canvasTextHeight(kSmall);
  g.tileX = kPad + 8;
  const int gap = 12;
  g.netX = g.tileX + g.tileW + gap;
  g.gpsX = g.netX + g.tileW + gap;
  g.settingsX = g.gpsX + g.tileW + gap;
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
  g.grabY = y;
  g.panelH = (g.grabY + kShadeGrabH + 8) - g.panelY;
  return g;
}

// Settings: Display, Power and System sections. Clock setting lives on its
// own page (Time & date); Wi-Fi sets the clock anyway.
constexpr int kNavRowH = 60;

struct SettingsGeom {
  int rowMinusX = 0;
  int rowPlusX = 0;
  int displayY = 0;
  int textSizeY = 0;
  int cleanY = 0;
  int powerY = 0;
  int sleepY = 0;
  int sleepNowY = 0;
  int systemY = 0;
  int timeY = 0;
  int aboutY = 0;
  int tipY = 0;
};

int stepperValueW() { return 120; }

SettingsGeom settingsGeom() {
  SettingsGeom g;
  g.rowMinusX = kScreenW - kPad - kStepBtn * 2 - stepperValueW();
  g.rowPlusX = kScreenW - kPad - kStepBtn;
  const int section = canvasTextHeight(kSmall) + 10;
  int y = contentTop() + 4;
  g.displayY = y;
  y += section;
  g.textSizeY = y;
  y += kStepBtn + 10;
  g.cleanY = y;
  y += kStepBtn + 22;
  g.powerY = y;
  y += section;
  g.sleepY = y;
  y += kStepBtn + 6;
  g.sleepNowY = y;
  y += kNavRowH + 22;
  g.systemY = y;
  y += section;
  g.timeY = y;
  y += kNavRowH;
  g.aboutY = y;
  y += kNavRowH;
  g.tipY = kScreenH - kPad - canvasTextHeight(kSmall);
  return g;
}

struct SetTimeGeom {
  int nowY = 0;
  int timeLabelY = 0;
  int hourY = 0;
  int minuteY = 0;
  int dateLabelY = 0;
  int yearY = 0;
  int monthY = 0;
  int dayY = 0;
  int tipY = 0;
};

SetTimeGeom setTimeGeom() {
  SetTimeGeom g;
  const int section = canvasTextHeight(kSmall) + 10;
  int y = contentTop() + 4;
  g.nowY = y;
  y += canvasTextHeight(kTitle) + 4 + canvasTextHeight(kSmall) + 24;
  g.timeLabelY = y;
  y += section;
  g.hourY = y;
  y += kStepBtn + 8;
  g.minuteY = y;
  y += kStepBtn + 22;
  g.dateLabelY = y;
  y += section;
  g.yearY = y;
  y += kStepBtn + 8;
  g.monthY = y;
  y += kStepBtn + 8;
  g.dayY = y;
  y += kStepBtn + 24;
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

// Home: optional Continue card, the "Apps" label, four slot cards, footer.
constexpr int kResumeH = 104;
constexpr int kSlotGap = 12;
constexpr int kSlotMaxH = 136;
constexpr int kCardMoreW = 56;  // the card's more button, at its right edge

struct HomeLayout {
  int resumeY = -1;
  int labelY = 0;
  int listY = 0;
  int rowH = 0;
  int footerY = 0;
};

HomeLayout homeLayout(bool resume) {
  HomeLayout g;
  int y = statusBarH() + 16;
  if (resume) {
    g.resumeY = y;
    y += kResumeH + 20;
  }
  g.labelY = y;
  y += canvasTextHeight(kBody) + 12;
  g.listY = y;
  g.footerY = kScreenH - kHomeFooterH;
  g.rowH = std::min(kSlotMaxH, (g.footerY - 14 - y - (kSlotCount - 1) * kSlotGap) / kSlotCount);
  return g;
}

int slotCardY(const HomeLayout& g, int index) { return g.listY + index * (g.rowH + kSlotGap); }

// Slot the Continue card on the frame now on screen points at, or -1.
int gHomeResumeSlot = -1;

// Shorten s with "..." until it fits maxW pixels at this text scale.
void fitText(char* s, int maxW, int scale) {
  if (!s || maxW <= 0) return;
  size_t n = strlen(s);
  if (canvasTextWidth(s, scale) <= maxW) return;
  while (n > 3) {
    --n;
    s[n] = 0;
    s[n - 1] = '.';
    s[n - 2] = '.';
    s[n - 3] = '.';
    if (canvasTextWidth(s, scale) <= maxW) return;
  }
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

void drawStatusBar(const FlashSpace& space, bool showClosedGrabber) {
  (void)space;
  const int barH = statusBarH();
  canvasFillRect(0, 0, kScreenW, barH, false);
  const BoardClockInfo clock = boardClock();
  const BoardPowerInfo power = boardPower();

  const int rowY = 10;
  const int rowH = canvasTextHeight(kBody);
  const int midY = rowY + rowH / 2;
  const char* time = clock.valid ? clock.time : "--:--";
  canvasDrawString(kPad, rowY, time, true, kBody, true);
  if (clock.valid && clock.date[0]) {
    const int dateX = kPad + canvasTextWidth(time, kBody) + 14;
    canvasDrawString(dateX, midY - canvasTextHeight(kSmall) / 2, clock.date, true, kSmall);
  }

  // Right cluster, from the edge inwards: percent, battery, bolt, Wi-Fi. Each
  // keeps a fixed slot so a cable or a network change never shifts the others
  // and leaves fast-refresh ghosts.
  const int clusterRight = kScreenW - kPad;
  const int textSlot = canvasTextWidth("100%", kSmall);
  constexpr int kBatterySpan = 31;
  constexpr int kIconSlot = 24;
  const int battX = clusterRight - textSlot - 6 - kBatterySpan;
  const int boltX = battX - 4 - kIconSlot;
  const int wifiX = boltX - 2 - kIconSlot;
  canvasFillRect(wifiX, rowY - 4, clusterRight - wifiX, rowH + 8, false);
  if (wifiIsActive()) drawIcon(kIconWifi20, wifiX + kIconSlot / 2, midY);
  if (power.known) {
    drawBatteryGlyph(battX, midY - 7, power.percent, power.charging, power.plugged);
    if (power.charging) drawIcon(kIconBolt20, boltX + kIconSlot / 2, midY);
    char batt[8];
    snprintf(batt, sizeof(batt), "%d%%", power.percent);
    canvasDrawString(clusterRight - canvasTextWidth(batt, kSmall), midY - canvasTextHeight(kSmall) / 2, batt, true,
                     kSmall);
  } else {
    canvasDrawString(clusterRight - canvasTextWidth("--", kSmall), midY - canvasTextHeight(kSmall) / 2, "--", true,
                     kSmall);
  }

  if (showClosedGrabber) drawShadeGrabCue(barH - 10, /*menuOpen=*/false);
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

// A button with an icon and a label side by side, both centred.
void drawToolBtn(int x, int y, int w, int h, const UiIcon& icon, const char* label, bool filled = false) {
  if (filled) canvasFillRoundRect(x, y, w, h, kBtnRadius, true);
  else {
    canvasFillRoundRect(x, y, w, h, kBtnRadius, false);
    canvasDrawRoundRect(x, y, w, h, kBtnRadius, true);
  }
  const int gap = 8;
  const int tw = canvasTextWidth(label, kBody);
  const int total = icon.w + gap + tw;
  const int ix = x + (w - total) / 2;
  drawIcon(icon, ix + icon.w / 2, y + h / 2, !filled);
  canvasDrawString(ix + icon.w + gap, y + (h - canvasTextHeight(kBody)) / 2, label, !filled, kBody, true);
}

void drawSlotBadge(int x, int y, char letter, bool filled) {
  const char text[2] = {letter, 0};
  const int lx = x + (kBadge - canvasTextWidth(text, kTitle)) / 2;
  const int ly = y + (kBadge - canvasTextHeight(kTitle)) / 2;
  if (filled) {
    canvasFillRoundRect(x, y, kBadge, kBadge, 12, true);
  } else {
    canvasFillRoundRect(x, y, kBadge, kBadge, 12, false);
    canvasDrawRoundRect(x, y, kBadge, kBadge, 12, true);
  }
  canvasDrawString(lx, ly, text, !filled, kTitle);
}

void drawSlotCard(const HomeLayout& g, int index, const SlotInfo& slot) {
  const int x = kPad;
  const int y = slotCardY(g, index);
  const int w = kScreenW - 2 * kPad;
  const int h = g.rowH;
  canvasDrawRoundRect(x, y, w, h, kRadius, true);
  drawSlotBadge(x + 16, y + (h - kBadge) / 2, slot.label[0], slot.occupied);

  const int textX = x + 16 + kBadge + 16;
  const int textW = x + w - kCardMoreW - textX;
  const int titleH = canvasTextHeight(kTitle);
  const int capH = canvasTextHeight(kSmall);

  if (!slot.occupied) {
    const int blockH = canvasTextHeight(kBody) + 4 + capH;
    const int ty = y + (h - blockH) / 2;
    canvasDrawString(textX, ty, "Empty slot", true, kBody, true);
    char cap[48], capBuf[24];
    appsFormatBytes(slot.capacity, capBuf, sizeof(capBuf));
    snprintf(cap, sizeof(cap), "Up to %s. Tap to install.", capBuf);
    fitText(cap, textW, kSmall);
    canvasDrawString(textX, ty + canvasTextHeight(kBody) + 4, cap, true, kSmall);
    drawIcon(kIconPlus24, x + w - kCardMoreW / 2 - 4, y + h / 2);
    return;
  }

  const bool update = !slot.updatePath.empty();
  constexpr int kBarH = 6;
  const int blockH = titleH + 6 + capH + 8 + kBarH + (update ? 10 + capH : 0);
  int ty = y + (h - blockH) / 2;

  char title[48];
  snprintf(title, sizeof(title), "%s", slot.title.empty() ? slot.name.c_str() : slot.title.c_str());
  fitText(title, textW, kTitle);
  canvasDrawString(textX, ty, title, true, kTitle);
  ty += titleH + 6;

  char used[24], cap[24], line[64];
  const size_t usedBytes = slot.size ? slot.size : slot.capacity;
  appsFormatBytes(usedBytes, used, sizeof(used));
  appsFormatBytes(slot.capacity, cap, sizeof(cap));
  if (!slot.version.empty()) snprintf(line, sizeof(line), "%s  |  %s of %s", slot.version.c_str(), used, cap);
  else snprintf(line, sizeof(line), "%s of %s", used, cap);
  fitText(line, textW, kSmall);
  canvasDrawString(textX, ty, line, true, kSmall);
  ty += capH + 8;

  canvasDrawRoundRect(textX, ty, textW, kBarH, 3, true);
  if (slot.capacity > 0) {
    int fill = static_cast<int>((static_cast<uint64_t>(textW) * usedBytes) / slot.capacity);
    fill = std::max(4, std::min(textW, fill));
    canvasFillRoundRect(textX, ty, fill, kBarH, 3, true);
  }
  ty += kBarH + 10;

  if (update) {
    drawIcon(kIconUpdate20, textX + 10, ty + capH / 2);
    char upd[48];
    snprintf(upd, sizeof(upd), "Update to %s", slot.updateVersion.c_str());
    fitText(upd, textW - 28, kSmall);
    canvasDrawString(textX + 28, ty, upd, true, kSmall, true);
  }
  drawIcon(kIconMore24, x + w - kCardMoreW / 2 - 4, y + h / 2);
}

void drawResumeCard(const HomeLayout& g, const SlotInfo& slot) {
  const int x = kPad;
  const int y = g.resumeY;
  const int w = kScreenW - 2 * kPad;
  canvasDrawRoundRect(x, y, w, kResumeH, kRadius, true);
  canvasDrawRoundRect(x + 1, y + 1, w - 2, kResumeH - 2, kRadius - 1, true);
  constexpr int kPlay = 64;
  const int px = x + 20;
  const int py = y + (kResumeH - kPlay) / 2;
  canvasFillRoundRect(px, py, kPlay, kPlay, 16, true);
  drawIcon(kIconPlay28, px + kPlay / 2 + 2, py + kPlay / 2, false);

  const int textX = px + kPlay + 18;
  const int textW = x + w - 18 - textX;
  const int capH = canvasTextHeight(kSmall);
  const int blockH = capH + 2 + canvasTextHeight(kTitle) + 2 + capH;
  int ty = y + (kResumeH - blockH) / 2;
  canvasDrawString(textX, ty, "Continue", true, kSmall);
  ty += capH + 2;
  char title[48];
  snprintf(title, sizeof(title), "%s", slot.title.empty() ? slot.name.c_str() : slot.title.c_str());
  fitText(title, textW, kTitle);
  canvasDrawString(textX, ty, title, true, kTitle);
  ty += canvasTextHeight(kTitle) + 2;
  char sub[48];
  if (!slot.version.empty()) snprintf(sub, sizeof(sub), "Slot %c  |  %s", slot.label[0], slot.version.c_str());
  else snprintf(sub, sizeof(sub), "Slot %c", slot.label[0]);
  canvasDrawString(textX, ty, sub, true, kSmall);
}

void presentNavigation() { canvasPresentFor(CanvasRefreshIntent::Navigation); }
void presentQuality() { canvasPresentFor(CanvasRefreshIntent::StaticQuality); }

// Word-wrap text into a column, breaking long words when they must. Returns
// the y below the last line drawn.
int drawWrapped(const char* text, int x, int y, int w, int scale, int maxLines, bool bold = false) {
  if (!text) return y;
  const int cellW = std::max(1, canvasTextWidth("M", scale));
  const int perLine = std::max(4, w / cellW);
  const int lineH = canvasTextHeight(scale) + 4;
  const char* p = text;
  for (int line = 0; line < maxLines && *p; ++line) {
    while (*p == ' ') ++p;
    int n = 0;
    int lastSpace = -1;
    while (p[n] && p[n] != '\n' && n < perLine) {
      if (p[n] == ' ') lastSpace = n;
      ++n;
    }
    int take = n;
    if (p[n] && p[n] != '\n' && p[n] != ' ' && lastSpace > 0) take = lastSpace;
    char row[96];
    const int len = std::min(take, static_cast<int>(sizeof(row)) - 1);
    memcpy(row, p, len);
    row[len] = 0;
    if (line == maxLines - 1 && p[take] && p[take] != '\n') fitText(row, w, scale);
    canvasDrawString(x, y, row, true, scale, bold);
    y += lineH;
    p += take;
    if (*p == '\n') ++p;
  }
  return y;
}

}  // namespace

static void markHit(UiHit& hit, int x, int y, int w, int h, int radius = kBtnRadius) {
  hit.rx = x;
  hit.ry = y;
  hit.rw = w;
  hit.rh = h;
  hit.rr = radius;
}

void uiFlashKey(const UiHit& hit) {
  if (hit.rw < 8 || hit.rh < 8) return;
  const CanvasRect key{hit.rx, hit.ry, hit.rw, hit.rh};
  canvasInvertRoundRect(hit.rx, hit.ry, hit.rw, hit.rh, hit.rr);
  const bool shown = canvasFlashInvertedKey(key);
  canvasInvertRoundRect(hit.rx, hit.ry, hit.rw, hit.rh, hit.rr);
  if (shown) canvasArmKeyRestore(key);
}

void uiAcknowledgePress(const UiHit& hit) {
  if (hit.rw < 8 || hit.rh < 8) return;
  // A window this large is promoted to a full fast refresh, which parks white
  // as grey. Press feedback stays on the control itself.
  const uint32_t area = static_cast<uint32_t>(hit.rw) * static_cast<uint32_t>(hit.rh);
  const uint32_t full = static_cast<uint32_t>(kScreenW) * static_cast<uint32_t>(kScreenH);
  if (area * 4u >= full * 3u) return;
  // Queue the inverted control and return, so the action behind the press
  // (SD reads, the next screen's drawing) runs while the flash is on the glass.
  // The framebuffer goes straight back to normal; the restore is pushed once
  // the panel is free, unless the next screen replaces it first.
  const CanvasRect rect{hit.rx, hit.ry, hit.rw, hit.rh};
  canvasInvertRoundRect(hit.rx, hit.ry, hit.rw, hit.rh, hit.rr);
  if (canvasFlashInvertedKey(rect)) {
    canvasInvertRoundRect(hit.rx, hit.ry, hit.rw, hit.rh, hit.rr);
    canvasArmKeyRestore(rect);
    return;
  }
  // The panel is still busy. Wait for it rather than leave the press silent.
  canvasPresentFor(CanvasRefreshIntent::InteractiveLocal, rect);
}

void uiShadeBrightnessTrack(int& x, int& y, int& w, int& h) {
  x = gBrightX;
  y = gBrightY;
  w = gBrightW;
  h = gBrightH;
}

int uiBrightnessFromTouchX(int touchX) {
  if (gBrightW <= 0) return boardBrightness();
  int pct = ((touchX - gBrightX) * 100) / gBrightW;
  if (pct < 1) pct = 1;
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

void homeFooterRect(int index, int& x, int& y, int& w, int& h) {
  constexpr int kGap = 12;
  w = (kScreenW - 2 * kPad - 2 * kGap) / 3;
  h = kHomeFooterH - 24;
  x = kPad + index * (w + kGap);
  y = kScreenH - kHomeFooterH + 12;
}

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space, bool stable, int lastSlot) {
  canvasClear();
  drawStatusBar(space, true);
  const bool resume = lastSlot >= 0 && lastSlot < kSlotCount && slots[lastSlot].occupied;
  gHomeResumeSlot = resume ? lastSlot : -1;
  const HomeLayout g = homeLayout(resume);

  if (resume) drawResumeCard(g, slots[lastSlot]);

  canvasDrawString(kPad, g.labelY, "Apps", true, kBody, true);
  char freeBuf[24], freeLine[40];
  appsFormatBytes(space.guestFree, freeBuf, sizeof(freeBuf));
  snprintf(freeLine, sizeof(freeLine), "%s free", freeBuf);
  canvasDrawString(kScreenW - kPad - canvasTextWidth(freeLine, kSmall),
                   g.labelY + (canvasTextHeight(kBody) - canvasTextHeight(kSmall)) / 2, freeLine, true, kSmall);

  for (int i = 0; i < kSlotCount; ++i) drawSlotCard(g, i, slots[i]);

  canvasDrawLine(0, g.footerY, kScreenW - 1, g.footerY, true);
  const UiIcon* icons[3] = {&kIconFolder24, &kIconGlobe24, &kIconSettings24};
  const char* labels[3] = {"Files", "Web", "Settings"};
  for (int i = 0; i < 3; ++i) {
    int fx, fy, fw, fh;
    homeFooterRect(i, fx, fy, fw, fh);
    drawToolBtn(fx, fy, fw, fh, *icons[i], labels[i]);
  }
  // Boot arrives from a full splash refresh; a FAST home leaves the charging
  // bolt ghosted under the battery until the settle scrub. Paint clean once.
  if (stable) presentQuality();
  else presentNavigation();
}

void uiRedrawStatusBar(const FlashSpace& space, bool showClosedGrabber) {
  const int h = statusBarH();
  canvasFillRect(0, 0, kScreenW, h, false);
  drawStatusBar(space, showClosedGrabber);
  // A coherent full-frame diff drives only changed chrome. If a window update
  // came first, the canvas layer defers this until the full settle clean.
  canvasPresentFullFastDelta(static_cast<uint32_t>(kScreenW) * h);
}

void uiRedrawHomeStatus(const FlashSpace& space) { uiRedrawStatusBar(space, true); }
constexpr int kExplorerRowH = 72;
constexpr int kCrumbH = 52;

int uiExplorerRowHeight() { return kExplorerRowH; }

int explorerListTop() { return contentTop() + kCrumbH + 4; }
int explorerListBottom() { return kScreenH - kExplorerDockH - 4; }

int uiExplorerVisibleRows() {
  return std::max(1, (explorerListBottom() - explorerListTop()) / kExplorerRowH);
}

namespace {

const UiIcon& entryIcon(const DirEntry& e) {
  if (e.isDir) return kIconFolder28;
  if (fileOpsIsBin(e.name.c_str())) return kIconChip28;
  if (fileOpsIsImage(e.name.c_str())) return kIconImage28;
  if (fileOpsIsText(e.name.c_str())) return kIconFileText28;
  return kIconFile28;
}

void drawQsTile(int x, int y, int icon, int tileW, bool filled, const UiIcon& glyph, const char* label) {
  const int iconX = x + (tileW - icon) / 2;
  if (filled) canvasFillRoundRect(iconX, y, icon, icon, kQsIconRadius, true);
  else {
    canvasFillRoundRect(iconX, y, icon, icon, kQsIconRadius, false);
    canvasDrawRoundRect(iconX, y, icon, icon, kQsIconRadius, true);
  }
  drawIcon(glyph, iconX + icon / 2, y + icon / 2, !filled);
  const int lw = canvasTextWidth(label, kSmall);
  canvasDrawString(x + (tileW - lw) / 2, y + icon + 6, label, true, kSmall, true);
}

bool hitRect(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && x < bx + w && y >= by && y < by + h;
}

// --- Breadcrumbs: "SD > books > scifi". Every crumb but the last is a tap
// target that jumps to that folder; an Up button leads when not at the root.
constexpr int kMaxCrumbs = 10;
struct CrumbHit {
  int x = 0;
  int w = 0;
  int depth = 0;
};
CrumbHit gCrumbs[kMaxCrumbs];
int gCrumbCount = 0;
constexpr int kCrumbUpW = 52;

void drawCrumbs(const char* path) {
  const int top = contentTop();
  const int midY = top + kCrumbH / 2;
  const bool atRoot = !path || appsIsRootDir(path);
  canvasFillRect(0, top, kScreenW, kCrumbH, false);
  int x = kPad;
  if (!atRoot) {
    canvasDrawRoundRect(kPad, midY - 20, kCrumbUpW - 8, 40, kIconRadius, true);
    drawIcon(kIconUp24, kPad + (kCrumbUpW - 8) / 2, midY);
    x += kCrumbUpW + 4;
  }

  // Names along the path, root first.
  char labels[kMaxCrumbs][40];
  int depths[kMaxCrumbs];
  int n = 0;
  snprintf(labels[n], sizeof(labels[n]), "SD");
  depths[n++] = 0;
  if (!atRoot) {
    const char* p = path;
    int depth = 0;
    while (*p) {
      while (*p == '/') ++p;
      if (!*p) break;
      const char* e = strchr(p, '/');
      const size_t len = e ? static_cast<size_t>(e - p) : strlen(p);
      ++depth;
      if (n < kMaxCrumbs) {
        snprintf(labels[n], sizeof(labels[n]), "%.*s", static_cast<int>(std::min<size_t>(len, 39)), p);
        depths[n++] = depth;
      } else {
        // Deeper than we track: keep the last slot pointing at the current folder.
        snprintf(labels[n - 1], sizeof(labels[n - 1]), "%.*s", static_cast<int>(std::min<size_t>(len, 39)), p);
        depths[n - 1] = depth;
      }
      p += len;
    }
  }

  // Keep "SD", then as many trailing crumbs as fit, with "..." between.
  constexpr int kSep = 22;
  const int avail = kScreenW - kPad - x;
  int first = 1;
  auto widthFrom = [&](int from) {
    int w = canvasTextWidth(labels[0], kBody);
    if (from > 1) w += kSep + canvasTextWidth("...", kBody);
    for (int i = from; i < n; ++i) w += kSep + canvasTextWidth(labels[i], kBody);
    return w;
  };
  while (first < n - 1 && widthFrom(first) > avail) ++first;
  if (n > 1) fitText(labels[n - 1], avail - (widthFrom(first) - canvasTextWidth(labels[n - 1], kBody)), kBody);

  gCrumbCount = 0;
  const int textY = midY - canvasTextHeight(kBody) / 2;
  auto put = [&](const char* label, int depth, bool current, bool tappable) {
    const int w = canvasTextWidth(label, kBody);
    canvasDrawString(x, textY, label, true, kBody, current);
    if (tappable && gCrumbCount < kMaxCrumbs) gCrumbs[gCrumbCount++] = {x - 6, w + 12, depth};
    x += w;
  };
  put(labels[0], 0, n == 1, n > 1);
  if (first > 1) {
    drawIcon(kIconChevron24, x + kSep / 2, midY);
    x += kSep;
    put("...", depths[first - 1], false, true);
  }
  for (int i = first; i < n; ++i) {
    drawIcon(kIconChevron24, x + kSep / 2, midY);
    x += kSep;
    put(labels[i], depths[i], i == n - 1, i < n - 1);
  }
}

// --- Bottom dock. Browsing: New folder, Paste (with something copied),
// Select, More. Selecting: the count, Open (one item), Delete, More, Done.
struct DockCell {
  UiHit::Kind kind;
  const UiIcon* icon;
  const char* label;
  bool filled;
};

int explorerDockCells(bool selecting, int count, bool clipboardHas, DockCell out[5]) {
  int n = 0;
  if (selecting) {
    out[n++] = {UiHit::Kind::None, nullptr, nullptr, false};  // the "N selected" label
    if (count == 1) out[n++] = {UiHit::Kind::ExplorerOpen, &kIconChevron24, "Open", true};
    out[n++] = {UiHit::Kind::ExplorerDelete, &kIconTrash24, "Delete", false};
    out[n++] = {UiHit::Kind::ExplorerMore, &kIconMore24, "More", false};
    out[n++] = {UiHit::Kind::Back, &kIconCheck24, "Done", false};
    return n;
  }
  out[n++] = {UiHit::Kind::ExplorerNew, &kIconFolderPlus24, "Folder", false};
  if (clipboardHas) out[n++] = {UiHit::Kind::ExplorerPaste, &kIconPaste24, "Paste", false};
  out[n++] = {UiHit::Kind::ExplorerSelectMode, &kIconSelect24, "Select", false};
  out[n++] = {UiHit::Kind::ExplorerMore, &kIconMore24, "More", false};
  return n;
}

void dockCellRect(int index, int count, int& x, int& y, int& w, int& h) {
  const int dockY = kScreenH - kExplorerDockH;
  w = (kScreenW - 2 * kPad) / count;
  h = kExplorerDockH - 16;
  x = kPad + index * w;
  y = dockY + 8;
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
  const int count = explorerSelectionCount(st);
  const bool selecting = st.multiSelect || count > 0;
  DockCell cells[5];
  const int n = explorerDockCells(selecting, count, st.clipboardHas, cells);
  for (int i = 0; i < n; ++i) {
    int x, y, w, h;
    dockCellRect(i, n, x, y, w, h);
    if (!cells[i].icon) {
      char label[24];
      snprintf(label, sizeof(label), "%d selected", count);
      if (count == 0) snprintf(label, sizeof(label), "Tap items");
      canvasDrawString(x + 4, y + (h - canvasTextHeight(kSmall)) / 2, label, true, kSmall, true);
      continue;
    }
    const bool ink = !cells[i].filled;
    if (cells[i].filled) canvasFillRoundRect(x + 4, y, w - 8, h, kBtnRadius, true);
    const int capH = canvasTextHeight(kSmall);
    const int blockH = cells[i].icon->h + 4 + capH;
    const int top = y + (h - blockH) / 2;
    drawIcon(*cells[i].icon, x + w / 2, top + cells[i].icon->h / 2, ink);
    canvasDrawString(x + (w - canvasTextWidth(cells[i].label, kSmall)) / 2, top + cells[i].icon->h + 4,
                     cells[i].label, ink, kSmall);
  }
}

void drawExplorerViewport(const std::vector<DirEntry>& entries, const ExplorerDrawState& st) {
  const int listTop = explorerListTop();
  const int listBottom = explorerListBottom();
  const int visible = uiExplorerVisibleRows();
  const int start = st.scroll;
  const int end = std::min(static_cast<int>(entries.size()), start + visible);
  constexpr int kIconCol = 48;
  const int textX = kPad + kIconCol;
  const int right = kScreenW - kPad;

  canvasFillRect(0, listTop, kScreenW, listBottom - listTop, false);
  if (entries.empty()) {
    const char* msg = boardSdOk() ? "This folder is empty" : "Insert an SD card";
    const int cy = listTop + 120;
    drawIcon(kIconFolder28, kScreenW / 2, cy);
    canvasDrawString((kScreenW - canvasTextWidth(msg, kBody)) / 2, cy + 30, msg, true, kBody);
  } else {
    const int bodyH = canvasTextHeight(kBody);
    const int capH = canvasTextHeight(kSmall);
    for (int i = start; i < end; ++i) {
      const DirEntry& e = entries[i];
      const int rowY = listTop + (i - start) * kExplorerRowH;
      const int midY = rowY + kExplorerRowH / 2;
      const bool sel = explorerEntrySelected(st, i);
      // A lone selection gets a light outline; multi-select shows checkboxes.
      // Neither fills the row: a black bar flipping back to white is what the
      // fast waveform ghosts worst.
      if (sel && !st.multiSelect) {
        canvasDrawRoundRect(kPad - 6, rowY + 4, right - kPad + 12, kExplorerRowH - 8, kIconRadius, true);
        canvasDrawRoundRect(kPad - 5, rowY + 5, right - kPad + 10, kExplorerRowH - 10, kIconRadius - 1, true);
      }
      drawIcon(entryIcon(e), kPad + 16, midY);

      int rightX = right;
      if (st.multiSelect) {
        drawIcon(sel ? kIconSquareCheck24 : kIconSquare24, right - 12, midY);
        rightX -= 36;
      }
      char meta[28] = "";
      if (e.isDir) {
        if (!st.multiSelect) drawIcon(kIconChevron24, right - 12, midY);
        rightX -= st.multiSelect ? 0 : 32;
      } else {
        char sz[24];
        appsFormatBytes(e.size, sz, sizeof(sz));
        const bool over =
            st.mode == ExplorerMode::Install && fileOpsIsBin(e.name.c_str()) && e.size > st.maxBytes;
        snprintf(meta, sizeof(meta), over ? "too big" : "%s", sz);
        const int mw = canvasTextWidth(meta, kSmall);
        canvasDrawString(rightX - mw, midY - capH / 2, meta, true, kSmall, over);
        rightX -= mw + 12;
      }

      char nameBuf[96];
      snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
      fitText(nameBuf, rightX - textX, kBody);
      canvasDrawString(textX, midY - bodyH / 2, nameBuf, true, kBody, sel);
      if (i + 1 < end) canvasDrawLine(textX, rowY + kExplorerRowH - 1, right - 1, rowY + kExplorerRowH - 1, true);
    }
  }

  if (entries.size() > static_cast<size_t>(visible)) {
    const int trackH = listBottom - listTop - 20;
    const int thumbH = std::max(16, trackH * visible / static_cast<int>(entries.size()));
    const int maxScroll = std::max(1, static_cast<int>(entries.size()) - visible);
    const int thumbY = listTop + 10 + (trackH - thumbH) * st.scroll / maxScroll;
    canvasFillRoundRect(kScreenW - 7, thumbY, 4, thumbH, 2, true);
  }
}

// --- More sheet (3 x 3 grid; 7 actions).
struct SheetItem {
  const char* label;
  UiHit::Kind kind;
  const UiIcon* icon;
};

constexpr int kSheetCols = 3;
constexpr int kSheetCount = 7;
constexpr int kSheetCellH = 100;
constexpr int kSheetPad = 14;
constexpr int kSheetGrab = 22;
constexpr int kSheetGap = 10;

int explorerSheetH() { return kSheetGrab + 10 + 3 * kSheetCellH + 2 * kSheetGap + kSheetPad; }

void explorerSheetCell(int index, int& x, int& y, int& w, int& h) {
  const int sheetY = kScreenH - explorerSheetH();
  const int gridTop = sheetY + kSheetGrab + 8;
  const int usable = kScreenW - 2 * kPad;
  w = (usable - (kSheetCols - 1) * kSheetGap) / kSheetCols;
  h = kSheetCellH;
  x = kPad + (index % kSheetCols) * (w + kSheetGap);
  y = gridTop + (index / kSheetCols) * (h + kSheetGap);
}

const SheetItem kSheetItems[kSheetCount] = {
    {"Copy", UiHit::Kind::ExplorerCopy, &kIconCopy40},
    {"Cut", UiHit::Kind::ExplorerCut, &kIconCut40},
    {"Paste", UiHit::Kind::ExplorerPaste, &kIconPaste40},
    {"Rename", UiHit::Kind::ExplorerRename, &kIconRename40},
    {"Delete", UiHit::Kind::ExplorerDelete, &kIconTrash40},
    {"New folder", UiHit::Kind::ExplorerNew, &kIconFolderPlus40},
    {"New file", UiHit::Kind::ExplorerNewFile, &kIconFilePlus40},
};

void explorerTitle(const ExplorerDrawState& st, char* out, size_t len) {
  if (st.mode == ExplorerMode::Install && st.targetSlot >= 0 && st.targetSlot < kSlotCount) {
    snprintf(out, len, "Install to %c", 'A' + st.targetSlot);
  } else if (st.mode == ExplorerMode::Install) {
    snprintf(out, len, "Install");
  } else {
    snprintf(out, len, "Files");
  }
}

}  // namespace

void uiDrawExplorer(const std::vector<DirEntry>& entries, const ExplorerDrawState& st,
                    const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);

  char title[32];
  explorerTitle(st, title, sizeof(title));
  drawHeader(title, true);

  char note[32] = "";
  if (st.mode == ExplorerMode::Install) {
    char maxBuf[24];
    appsFormatBytes(st.maxBytes, maxBuf, sizeof(maxBuf));
    snprintf(note, sizeof(note), "max %s", maxBuf);
  } else if (st.clipboardHas) {
    if (st.clipboardCount > 1) {
      snprintf(note, sizeof(note), st.clipboardCut ? "%u to move" : "%u copied",
               static_cast<unsigned>(st.clipboardCount));
    } else {
      snprintf(note, sizeof(note), "%s", st.clipboardCut ? "1 to move" : "1 copied");
    }
  }
  if (note[0]) {
    canvasDrawString(kScreenW - kPad - canvasTextWidth(note, kSmall),
                     statusBarH() + (kHeaderH - canvasTextHeight(kSmall)) / 2, note, true, kSmall);
  }

  drawCrumbs(st.currentPath);
  drawExplorerViewport(entries, st);
  drawExplorerDock(st);

  if (st.sheetOpen) {
    const int sheetH = explorerSheetH();
    const int sheetY = kScreenH - sheetH;
    canvasFillRect(0, sheetY, kScreenW, sheetH, false);
    canvasDrawLine(0, sheetY, kScreenW - 1, sheetY, true);
    canvasDrawLine(0, sheetY + 1, kScreenW - 1, sheetY + 1, true);
    canvasFillRoundRect(kScreenW / 2 - 28, sheetY + 8, 56, 5, 2, true);
    for (int i = 0; i < kSheetCount; ++i) {
      int cx, cy, cw, ch;
      explorerSheetCell(i, cx, cy, cw, ch);
      canvasDrawRoundRect(cx, cy, cw, ch, kBtnRadius, true);
      drawIcon(*kSheetItems[i].icon, cx + cw / 2, cy + ch / 2 - 14);
      const int tw = canvasTextWidth(kSheetItems[i].label, kSmall);
      canvasDrawString(cx + (cw - tw) / 2, cy + ch - 30, kSheetItems[i].label, true, kSmall);
    }
  }

  presentNavigation();
}

void uiRedrawExplorerViewport(const std::vector<DirEntry>& entries, const ExplorerDrawState& st) {
  drawExplorerViewport(entries, st);
  drawExplorerDock(st);

  const int top = explorerListTop();
  const CanvasRect list{0, top, kScreenW, std::max(1, kScreenH - top)};
  canvasPresentWindowFast(list);
  canvasArmFullClean(600);
}

UiHit uiHitExplorer(int x, int y, int entryCount, int scroll, bool canGoUp, bool sheetOpen,
                    bool clipboardHas, int selectedCount, bool multiSelect) {
  UiHit hit;

  if (!sheetOpen && y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }

  if (sheetOpen) {
    const int sheetY = kScreenH - explorerSheetH();
    hit.kind = UiHit::Kind::ExplorerSheetDismiss;
    if (y < sheetY) return hit;
    for (int i = 0; i < kSheetCount; ++i) {
      int cx, cy, cw, ch;
      explorerSheetCell(i, cx, cy, cw, ch);
      if (hitRect(x, y, cx, cy, cw, ch)) {
        hit.kind = kSheetItems[i].kind;
        markHit(hit, cx, cy, cw, ch, kBtnRadius);
        return hit;
      }
    }
    return hit;
  }

  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }

  const int crumbTop = contentTop();
  if (y >= crumbTop && y < crumbTop + kCrumbH) {
    if (canGoUp && x >= kPad && x < kPad + kCrumbUpW) {
      hit.kind = UiHit::Kind::GoUp;
      markHit(hit, kPad, crumbTop + kCrumbH / 2 - 20, kCrumbUpW - 8, 40, kIconRadius);
      return hit;
    }
    for (int i = 0; i < gCrumbCount; ++i) {
      if (x >= gCrumbs[i].x && x < gCrumbs[i].x + gCrumbs[i].w) {
        hit.kind = UiHit::Kind::GoPath;
        hit.index = gCrumbs[i].depth;
        markHit(hit, gCrumbs[i].x, crumbTop + 6, gCrumbs[i].w, kCrumbH - 12, kIconRadius);
        return hit;
      }
    }
    return hit;
  }

  if (y >= kScreenH - kExplorerDockH) {
    const bool selecting = multiSelect || selectedCount > 0;
    DockCell cells[5];
    const int n = explorerDockCells(selecting, selectedCount, clipboardHas, cells);
    for (int i = 0; i < n; ++i) {
      int cx, cy, cw, ch;
      dockCellRect(i, n, cx, cy, cw, ch);
      if (!cells[i].icon || !hitRect(x, y, cx, cy - 8, cw, ch + 16)) continue;
      hit.kind = cells[i].kind;
      markHit(hit, cx + 4, cy, cw - 8, ch);
      return hit;
    }
    return hit;
  }

  const int listTop = explorerListTop();
  const int visible = uiExplorerVisibleRows();
  for (int row = 0; row < visible; ++row) {
    const int idx = scroll + row;
    if (idx >= entryCount) break;
    const int rowY = listTop + row * kExplorerRowH;
    if (y >= rowY && y < rowY + kExplorerRowH) {
      hit.kind = UiHit::Kind::SelectEntry;
      hit.index = idx;
      markHit(hit, kPad - 6, rowY + 4, kScreenW - 2 * kPad + 12, kExplorerRowH - 8, kIconRadius);
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

  drawQsTile(g.tileX, g.tileY, g.icon, g.tileW, wifiIsHotspot(), kIconTransfer36, "Transfer");
  drawQsTile(g.netX, g.tileY, g.icon, g.tileW, wifiIsStation(), kIconWifi36, "Wi-Fi");
  drawQsTile(g.gpsX, g.tileY, g.icon, g.tileW, gpsSession(), kIconGps36, "GPS");
  drawQsTile(g.settingsX, g.tileY, g.icon, g.tileW, false, kIconSettings36, "Settings");

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
  const uint32_t area = static_cast<uint32_t>(kScreenW) * (g.panelY + g.panelH);
  canvasPresentFullFastDelta(area, 500);
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
    auto markTile = [&](int tileX, UiHit::Kind kind) {
      hit.kind = kind;
      const int iconX = tileX + (g.tileW - g.icon) / 2;
      markHit(hit, iconX, g.tileY, g.icon, g.icon, kQsIconRadius);
    };
    if (x >= g.tileX && x < g.tileX + g.tileW) {
      markTile(g.tileX, UiHit::Kind::Wifi);
      return hit;
    }
    if (x >= g.netX && x < g.netX + g.tileW) {
      markTile(g.netX, UiHit::Kind::Net);
      return hit;
    }
    if (x >= g.gpsX && x < g.gpsX + g.tileW) {
      markTile(g.gpsX, UiHit::Kind::OpenGps);
      return hit;
    }
    if (x >= g.settingsX && x < g.settingsX + g.tileW) {
      markTile(g.settingsX, UiHit::Kind::Settings);
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
    markHit(hit, kPad + 8, g.scrubY, kScreenW - 2 * (kPad + 8), kActionBtnH);
    return hit;
  }

  return hit;
}

void drawSettingsStepper(int y, const char* label, const char* value, int minusX, int plusX) {
  constexpr int step = kStepBtn;
  const int bodyH = canvasTextHeight(kBody);
  canvasDrawString(kPad, y + (step - bodyH) / 2, label, true, kBody);
  drawOutlineBtn(minusX, y, step, step, "-");
  drawOutlineBtn(plusX, y, step, step, "+");
  const int valueW = plusX - (minusX + step);
  const int vx = minusX + step + (valueW - canvasTextWidth(value, kBody)) / 2;
  canvasDrawString(vx, y + (step - bodyH) / 2, value, true, kBody, true);
}

void drawSectionLabel(int y, const char* label) { canvasDrawString(kPad, y, label, true, kSmall, true); }

// A full-width row that opens another page or runs an action: icon, label,
// an optional value, and a chevron when it navigates.
void drawNavRow(int y, const UiIcon& icon, const char* label, const char* value, bool chevron) {
  canvasDrawLine(kPad, y, kScreenW - kPad - 1, y, true);
  const int midY = y + kNavRowH / 2;
  drawIcon(icon, kPad + 12, midY);
  canvasDrawString(kPad + 40, midY - canvasTextHeight(kBody) / 2, label, true, kBody);
  int right = kScreenW - kPad;
  if (chevron) {
    drawIcon(kIconChevron24, right - 12, midY);
    right -= 30;
  }
  if (value && value[0]) {
    canvasDrawString(right - canvasTextWidth(value, kSmall), midY - canvasTextHeight(kSmall) / 2, value, true, kSmall);
  }
}

const char* monthName(int month) {
  static const char* names[] = {"--", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (month < 1 || month > 12) return names[0];
  return names[month];
}

bool hitStepperRow(int x, int y, int rowY, int minusX, int plusX, UiHit::Kind minus, UiHit::Kind plus, UiHit& hit) {
  if (y < rowY || y >= rowY + kStepBtn) return false;
  if (x >= minusX && x < minusX + kStepBtn) {
    hit.kind = minus;
    markHit(hit, minusX, rowY, kStepBtn, kStepBtn);
    return true;
  }
  if (x >= plusX && x < plusX + kStepBtn) {
    hit.kind = plus;
    markHit(hit, plusX, rowY, kStepBtn, kStepBtn);
    return true;
  }
  return false;
}

void uiDrawSettings(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);
  drawHeader("Settings", true);
  const SettingsGeom g = settingsGeom();

  drawSectionLabel(g.displayY, "DISPLAY");
  const char* sizeTxt = "Medium";
  if (boardUiTextSize() <= 0) sizeTxt = "Small";
  else if (boardUiTextSize() >= 2) sizeTxt = "Large";
  drawSettingsStepper(g.textSizeY, "Text size", sizeTxt, g.rowMinusX, g.rowPlusX);
  char every[16];
  snprintf(every, sizeof(every), "%d pages", boardCleanEvery());
  drawSettingsStepper(g.cleanY, "Clean every", every, g.rowMinusX, g.rowPlusX);

  drawSectionLabel(g.powerY, "POWER");
  char sleepTxt[16];
  if (boardSleepAfterMin() <= 0) snprintf(sleepTxt, sizeof(sleepTxt), "Never");
  else snprintf(sleepTxt, sizeof(sleepTxt), "%d min", boardSleepAfterMin());
  drawSettingsStepper(g.sleepY, "Sleep after", sleepTxt, g.rowMinusX, g.rowPlusX);
  drawNavRow(g.sleepNowY, kIconMoon24, "Sleep now", nullptr, false);

  drawSectionLabel(g.systemY, "SYSTEM");
  const BoardClockInfo clock = boardClock();
  char when[32] = "";
  if (clock.valid) snprintf(when, sizeof(when), "%s, %d %s", clock.time, clock.day, monthName(clock.month));
  drawNavRow(g.timeY, kIconClock24, "Time & date", when, true);
  drawNavRow(g.aboutY, kIconInfo24, "About this device", nullptr, true);
  canvasDrawLine(kPad, g.aboutY + kNavRowH, kScreenW - kPad - 1, g.aboutY + kNavRowH, true);

  canvasDrawString(kPad, g.tipY, "Hold BOOT to sleep. Press it to wake.", true, kSmall);
  presentNavigation();
}

UiHit uiHitSettings(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }
  const SettingsGeom g = settingsGeom();
  if (hitStepperRow(x, y, g.textSizeY, g.rowMinusX, g.rowPlusX, UiHit::Kind::FontSizeMinus,
                    UiHit::Kind::FontSizePlus, hit))
    return hit;
  if (hitStepperRow(x, y, g.cleanY, g.rowMinusX, g.rowPlusX, UiHit::Kind::CleanEveryMinus,
                    UiHit::Kind::CleanEveryPlus, hit))
    return hit;
  if (hitStepperRow(x, y, g.sleepY, g.rowMinusX, g.rowPlusX, UiHit::Kind::SleepAfterMinus,
                    UiHit::Kind::SleepAfterPlus, hit))
    return hit;
  auto navRow = [&](int rowY, UiHit::Kind kind) {
    if (y < rowY || y >= rowY + kNavRowH || x < kPad || x >= kScreenW - kPad) return false;
    hit.kind = kind;
    markHit(hit, kPad, rowY + 4, kScreenW - 2 * kPad, kNavRowH - 8);
    return true;
  };
  if (navRow(g.sleepNowY, UiHit::Kind::PowerOff)) return hit;
  if (navRow(g.timeY, UiHit::Kind::SetTime)) return hit;
  if (navRow(g.aboutY, UiHit::Kind::Hardware)) return hit;
  return hit;
}

void uiDrawSetTime(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);
  drawHeader("Time & date", true);
  const SetTimeGeom g = setTimeGeom();
  const SettingsGeom s = settingsGeom();
  const BoardClockInfo clock = boardClock();

  canvasDrawString(kPad, g.nowY, clock.valid ? clock.time : "--:--", true, kTitle);
  char date[32];
  if (clock.valid) snprintf(date, sizeof(date), "%d %s %u", clock.day, monthName(clock.month), clock.year);
  else snprintf(date, sizeof(date), "Clock not set");
  canvasDrawString(kPad, g.nowY + canvasTextHeight(kTitle) + 4, date, true, kSmall);

  drawSectionLabel(g.timeLabelY, "TIME");
  char buf[8];
  snprintf(buf, sizeof(buf), "%02u", clock.valid ? clock.hour : 0);
  drawSettingsStepper(g.hourY, "Hour", buf, s.rowMinusX, s.rowPlusX);
  snprintf(buf, sizeof(buf), "%02u", clock.valid ? clock.minute : 0);
  drawSettingsStepper(g.minuteY, "Minute", buf, s.rowMinusX, s.rowPlusX);

  drawSectionLabel(g.dateLabelY, "DATE");
  snprintf(buf, sizeof(buf), "%u", clock.valid ? clock.year : 2026);
  drawSettingsStepper(g.yearY, "Year", buf, s.rowMinusX, s.rowPlusX);
  drawSettingsStepper(g.monthY, "Month", monthName(clock.valid ? clock.month : 1), s.rowMinusX, s.rowPlusX);
  snprintf(buf, sizeof(buf), "%u", clock.valid ? clock.day : 1);
  drawSettingsStepper(g.dayY, "Day", buf, s.rowMinusX, s.rowPlusX);

  canvasDrawString(kPad, g.tipY, "Joining Wi-Fi sets the clock from the", true, kSmall);
  canvasDrawString(kPad, g.tipY + canvasTextHeight(kSmall) + 2, "internet.", true, kSmall);
  presentNavigation();
}

UiHit uiHitSetTime(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }
  const SetTimeGeom g = setTimeGeom();
  const SettingsGeom s = settingsGeom();
  if (hitStepperRow(x, y, g.hourY, s.rowMinusX, s.rowPlusX, UiHit::Kind::HourMinus, UiHit::Kind::HourPlus, hit))
    return hit;
  if (hitStepperRow(x, y, g.minuteY, s.rowMinusX, s.rowPlusX, UiHit::Kind::MinuteMinus, UiHit::Kind::MinutePlus,
                    hit))
    return hit;
  if (hitStepperRow(x, y, g.yearY, s.rowMinusX, s.rowPlusX, UiHit::Kind::YearMinus, UiHit::Kind::YearPlus, hit))
    return hit;
  if (hitStepperRow(x, y, g.monthY, s.rowMinusX, s.rowPlusX, UiHit::Kind::MonthMinus, UiHit::Kind::MonthPlus, hit))
    return hit;
  if (hitStepperRow(x, y, g.dayY, s.rowMinusX, s.rowPlusX, UiHit::Kind::DayMinus, UiHit::Kind::DayPlus, hit))
    return hit;
  return hit;
}

int drawHardwareRow(int y, const char* label, const char* value) {
  const int h = canvasTextHeight(kBody);
  canvasDrawString(kPad, y + (h - canvasTextHeight(kSmall)) / 2, label, true, kSmall);
  char v[48];
  snprintf(v, sizeof(v), "%s", value ? value : "");
  fitText(v, kScreenW - 2 * kPad - 120, kBody);
  canvasDrawString(kScreenW - kPad - canvasTextWidth(v, kBody), y, v, true, kBody);
  return y + h + 12;
}

int drawHardwareRule(int y) {
  y += 4;
  canvasDrawLine(kPad, y, kScreenW - kPad - 1, y, true);
  return y + 16;
}

static int gGpsZoomX = 0;
static int gGpsZoomY = 0;
static int gGpsZoomPlusX = 0;
static int gGpsMeX = -1;
static int gGpsMapX = 0;
static int gGpsMapY = 0;
static int gGpsMapW = 0;
static int gGpsMapH = 0;

void uiDrawHardware(const FlashSpace& space) {
  canvasClear();
  drawStatusBar(space, true);
  drawHeader("About this device", true);
  const BoardDeviceInfo device = boardDeviceInfo();
  int y = contentTop() + 8;

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
  presentNavigation();
}

UiHit uiHitHardware(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }
  return hit;
}

void drawSkyCircle(int cx, int cy, int r) {
  int px = r;
  int py = 0;
  int err = 1 - r;
  while (px >= py) {
    canvasSetPixel(cx + px, cy + py, true);
    canvasSetPixel(cx + py, cy + px, true);
    canvasSetPixel(cx - py, cy + px, true);
    canvasSetPixel(cx - px, cy + py, true);
    canvasSetPixel(cx - px, cy - py, true);
    canvasSetPixel(cx - py, cy - px, true);
    canvasSetPixel(cx + py, cy - px, true);
    canvasSetPixel(cx + px, cy - py, true);
    ++py;
    if (err < 0) {
      err += 2 * py + 1;
    } else {
      --px;
      err += 2 * (py - px) + 1;
    }
  }
}

int paintGpsHeader() {
  const GpsView view = gpsView();
  int y = contentTop() + 4;

  char line[48];
  const char* fixName = "Searching";
  if (view.fix == 2) fixName = "2D fix";
  else if (view.fix == 3) fixName = "3D fix";
  snprintf(line, sizeof(line), "%s  %d/%d", fixName, view.satsUsed, view.satsView);
  canvasDrawString(kPad, y, line, true, kBody, true);
  y += canvasTextHeight(kBody) + 4;

  if (view.hasPos) {
    snprintf(line, sizeof(line), "%.5f %c", fabs(view.lat), view.lat >= 0 ? 'N' : 'S');
    canvasDrawString(kPad, y, line, true, kBody);
    y += canvasTextHeight(kBody) + 2;
    snprintf(line, sizeof(line), "%.5f %c", fabs(view.lon), view.lon >= 0 ? 'E' : 'W');
    canvasDrawString(kPad, y, line, true, kBody);
    y += canvasTextHeight(kBody) + 2;
  } else {
    canvasDrawString(kPad, y, "No position yet", true, kBody);
    y += canvasTextHeight(kBody) + 2;
  }
  if (view.hasAlt) snprintf(line, sizeof(line), "Alt %d m", view.altM);
  else snprintf(line, sizeof(line), "Alt --");
  canvasDrawString(kPad, y, line, true, kBody);
  y += canvasTextHeight(kBody) + 2;
  canvasDrawString(kPad, y, "NLS Finland", true, kBody);
  y += canvasTextHeight(kBody) + 6;

  const int skyR = 48;
  const int skyCx = kScreenW - kPad - skyR;
  const int skyCy = contentTop() + canvasTextHeight(kBody) + 6 + skyR;
  drawSkyCircle(skyCx, skyCy, skyR);
  canvasDrawString(skyCx - canvasTextWidth("N", kBody) / 2, skyCy - skyR - canvasTextHeight(kBody) - 1, "N",
                   true, kBody);
  const int shown = gpsSatCount();
  for (int i = 0; i < shown; ++i) {
    const GpsSat sat = gpsSat(i);
    if (sat.el < 0 || sat.el > 90) continue;
    const double az = sat.az * 0.017453292519943295;
    const double rr = (90 - sat.el) / 90.0 * skyR;
    const int sx = skyCx + static_cast<int>(rr * sin(az));
    const int sy = skyCy - static_cast<int>(rr * cos(az));
    canvasFillRect(sx - 1, sy - 1, sat.snr > 0 ? 3 : 2, sat.snr > 0 ? 3 : 2, true);
  }
  const int skyBottom = skyCy + skyR + 8;
  if (y < skyBottom) y = skyBottom;
  return y;
}

void paintGpsMapChrome(int mapY, int mapH) {
  const GpsView view = gpsView();
  constexpr int btn = 52;
  gGpsZoomY = mapY + mapH - btn - 8;
  gGpsZoomPlusX = kScreenW - kPad - btn;
  gGpsZoomX = gGpsZoomPlusX - 8 - btn;
  if (gGpsZoomY < mapY) gGpsZoomY = mapY;
  gGpsMeX = -1;
  if (view.hasPos) {
    gGpsMeX = gGpsZoomX - 8 - btn;
    if (gGpsMeX < kPad) gGpsMeX = kPad;
    drawChromeOutlineBtn(gGpsMeX, gGpsZoomY, btn, btn, "Me");
  }
  drawChromeOutlineBtn(gGpsZoomX, gGpsZoomY, btn, btn, "-");
  drawChromeOutlineBtn(gGpsZoomPlusX, gGpsZoomY, btn, btn, "+");
  char zoomLabel[8];
  snprintf(zoomLabel, sizeof(zoomLabel), "z%d", view.zoom);
  canvasDrawString(gGpsZoomPlusX + btn - canvasTextWidth(zoomLabel, kBody),
                   gGpsZoomY - canvasTextHeight(kBody) - 2, zoomLabel, true, kBody, true);
}

void paintGpsMap() {
  if (!gpsDrawMap(gGpsMapX, gGpsMapY, gGpsMapW, gGpsMapH)) {
    canvasDrawString(kPad, gGpsMapY + 12, gpsHasMaps() ? "No map for this place" : "No maps on the SD card",
                     true, kBody);
  }
  paintGpsMapChrome(gGpsMapY, gGpsMapH);
}

void uiDrawGps() {
  canvasClear();
  drawStatusBar(appsFlashSpace(), true);
  drawHeader("GPS", true);
  const int y = paintGpsHeader();
  gGpsMapX = 0;
  gGpsMapY = y;
  gGpsMapW = kScreenW;
  gGpsMapH = kScreenH - y;
  paintGpsMap();
  // A fast update turns a mostly white map grey and drops the thin lines.
  canvasPresent(EInkDisplay::HALF_REFRESH);
}

void uiFollowGpsMap(int dx, int dy) {
  canvasSetHoldCleanRefresh(true);
  canvasCancelPendingClean();
  if (gGpsMapH <= 0 || !gpsScrollMap(dx, dy, gGpsMapX, gGpsMapY, gGpsMapW, gGpsMapH)) {
    paintGpsMap();
  } else {
    paintGpsMapChrome(gGpsMapY, gGpsMapH);
  }
  // Only the map. A fast window of the whole glass parks the white apps page grey.
  const CanvasRect map{gGpsMapX, gGpsMapY, gGpsMapW, gGpsMapH};
  canvasPresentWindowFast(map);
}

void uiSettleGpsMap() {
  canvasSetHoldCleanRefresh(false);
  canvasCancelPendingClean();
  if (gGpsMapH <= 0) {
    uiDrawGps();
    return;
  }
  paintGpsMap();
  canvasDiscardGray();
  canvasPresent(EInkDisplay::HALF_REFRESH);
}

void uiRedrawGpsMap() {
  if (gGpsMapH <= 0) {
    uiDrawGps();
    return;
  }
  paintGpsMap();
  canvasDiscardGray();
  canvasCancelPendingClean();
  canvasPresent(EInkDisplay::HALF_REFRESH);
}

void uiRedrawGpsStatus(bool present) {
  const int top = contentTop();
  if (gGpsMapY <= top) {
    uiDrawGps();
    return;
  }
  canvasFillRect(0, top, kScreenW, gGpsMapY - top, false);
  const int y = paintGpsHeader();
  if (y != gGpsMapY) {
    uiDrawGps();
    return;
  }
  canvasDiscardGray();
  if (!present) return;
  const uint32_t area = static_cast<uint32_t>(kScreenW) * (gGpsMapY - top);
  canvasPresentFullFastDelta(area, 500);
}

UiHit uiHitGps(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }
  constexpr int btn = 52;
  if (gGpsMeX >= 0 && y >= gGpsZoomY && y < gGpsZoomY + btn && x >= gGpsMeX && x < gGpsMeX + btn) {
    hit.kind = UiHit::Kind::GpsRecenter;
    markHit(hit, gGpsMeX, gGpsZoomY, btn, btn);
    return hit;
  }
  if (y >= gGpsZoomY && y < gGpsZoomY + btn && x >= gGpsZoomX && x < gGpsZoomX + btn) {
    hit.kind = UiHit::Kind::GpsZoomOut;
    markHit(hit, gGpsZoomX, gGpsZoomY, btn, btn);
    return hit;
  }
  if (y >= gGpsZoomY && y < gGpsZoomY + btn && x >= gGpsZoomPlusX && x < gGpsZoomPlusX + btn) {
    hit.kind = UiHit::Kind::GpsZoomIn;
    markHit(hit, gGpsZoomPlusX, gGpsZoomY, btn, btn);
    return hit;
  }
  return hit;
}

bool uiGpsMapContains(int x, int y) {
  if (gGpsMapW <= 0 || gGpsMapH <= 0) return false;
  if (x < gGpsMapX || y < gGpsMapY || x >= gGpsMapX + gGpsMapW || y >= gGpsMapY + gGpsMapH) return false;
  constexpr int btn = 52;
  if (y >= gGpsZoomY && y < gGpsZoomY + btn && x >= gGpsZoomX - 8) return false;
  if (gGpsMeX >= 0 && y >= gGpsZoomY && y < gGpsZoomY + btn && x >= gGpsMeX && x < gGpsMeX + btn) return false;
  return true;
}

bool uiGpsMapOffsetFromCenter(int x, int y, int& offsetX, int& offsetY) {
  if (!uiGpsMapContains(x, y)) return false;
  offsetX = x - (gGpsMapX + gGpsMapW / 2);
  offsetY = y - (gGpsMapY + gGpsMapH / 2);
  return true;
}

void uiDrawWifi(const FlashSpace& space, const char* statusLine, const char* ssidLine,
                const char* urlLine, const char* detailLine, bool active) {
  canvasClear();
  drawStatusBar(space, true);
  drawHeader("File transfer", true);
  int y = contentTop() + 4;

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
    canvasDrawString(x0, labelY, "Phone joins", true, kSmall);
    canvasDrawString(x0 + qrSize + gap, labelY, "Open page", true, kSmall);
    y = labelY + canvasTextHeight(kSmall) + 12;
  }

  const int btnW = kScreenW - 2 * kPad;
  const int backY = kScreenH - kPad;
  auto placeBtn = [&](const char* label, bool filled) {
    if (y + kActionBtnH + 10 > backY) return;
    if (filled) drawFilledBtn(kPad, y, btnW, kActionBtnH, label);
    else drawOutlineBtn(kPad, y, btnW, kActionBtnH, label);
    y += kActionBtnH + 10;
  };

  placeBtn("Change password", false);
  if (!active) {
    placeBtn("Start transfer", true);
  } else {
    placeBtn("Stop transfer", false);
  }
  presentNavigation();
}

UiHit uiHitWifi(int x, int y, bool active) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  const int btnW = kScreenW - 2 * kPad;
  const int backY = kScreenH - kPad;
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }

  int by = contentTop() + 4;
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
      markHit(hit, kPad, by, btnW, kActionBtnH);
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

int gNetListY = 0;
constexpr int kNetRowH = 58;
int gNetShown = 0;
int gNetBtnY = 0;
bool gNetConnected = false;

// Four bars, filled up to the signal level and outlined beyond it, so a weak
// network still shows the whole scale.
void drawSignalBars(int cx, int cy, int rssi) {
  const int level = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
  constexpr int kBarW = 4;
  constexpr int kGap = 2;
  const int left = cx - (4 * kBarW + 3 * kGap) / 2;
  const int bottom = cy + 9;
  for (int i = 0; i < 4; ++i) {
    const int h = 6 + i * 4;
    const int x = left + i * (kBarW + kGap);
    if (i < level) canvasFillRect(x, bottom - h, kBarW, h, true);
    else canvasDrawRect(x, bottom - h, kBarW, h, true);
  }
}

// Disconnect (when connected) and Browse share the bottom row.
void netButtonRects(bool connected, int& discX, int& browseX, int& w) {
  const int full = kScreenW - 2 * kPad;
  if (connected) {
    w = (full - 12) / 2;
    discX = kPad;
    browseX = kPad + w + 12;
  } else {
    w = full;
    discX = -1;
    browseX = kPad;
  }
}

void uiDrawNet(const FlashSpace& space, const WifiStatus& status, const WifiAp* aps, int count) {
  canvasClear();
  drawStatusBar(space, true);
  drawHeader("Wi-Fi", true);
  int ax, ay, aw, ah;
  headerActionRect(ax, ay, aw, ah);
  drawIcon(kIconRefresh24, ax + aw / 2, ay + ah / 2);

  const bool connected = status.connected && status.mode == WifiMode::Station;
  gNetConnected = connected;
  int y = contentTop() + 4;
  if (connected) {
    drawIcon(kIconCheck24, kPad + 12, y + canvasTextHeight(kBody) / 2);
    char line[48];
    snprintf(line, sizeof(line), "Connected to %s", status.ssid);
    fitText(line, kScreenW - 2 * kPad - 36, kBody);
    canvasDrawString(kPad + 36, y, line, true, kBody, true);
    y += canvasTextHeight(kBody) + 4;
    canvasDrawString(kPad + 36, y, status.ip, true, kSmall);
  } else {
    canvasDrawString(kPad, y, "Not connected", true, kBody, true);
    y += canvasTextHeight(kBody) + 4;
    canvasDrawString(kPad, y, "Pick a network below.", true, kSmall);
  }
  y += canvasTextHeight(kSmall) + 22;
  drawSectionLabel(y, "NETWORKS");
  y += canvasTextHeight(kSmall) + 6;

  gNetBtnY = kScreenH - kPad - kActionBtnH;
  gNetListY = y;
  gNetShown = 0;
  if (aps && count > 0) gNetShown = std::min(count, (gNetBtnY - 12 - gNetListY) / kNetRowH);

  for (int i = 0; i < gNetShown; ++i) {
    const int rowY = gNetListY + i * kNetRowH;
    const int midY = rowY + kNetRowH / 2;
    const bool current = connected && strcmp(aps[i].ssid, status.ssid) == 0;
    canvasDrawLine(kPad, rowY, kScreenW - kPad - 1, rowY, true);
    drawSignalBars(kPad + 12, midY, aps[i].rssi);
    int right = kScreenW - kPad;
    if (current) {
      drawIcon(kIconCheck24, right - 12, midY);
      right -= 32;
    }
    if (!aps[i].open) {
      drawIcon(kIconLock20, right - 10, midY);
      right -= 28;
    }
    char name[40];
    snprintf(name, sizeof(name), "%s", aps[i].ssid[0] ? aps[i].ssid : "(hidden)");
    fitText(name, right - 8 - (kPad + 40), kBody);
    canvasDrawString(kPad + 40, midY - canvasTextHeight(kBody) / 2, name, true, kBody, current);
  }
  if (gNetShown > 0) {
    const int endY = gNetListY + gNetShown * kNetRowH;
    canvasDrawLine(kPad, endY, kScreenW - kPad - 1, endY, true);
  } else {
    canvasDrawString(kPad, gNetListY + 8, count == 0 ? "No networks found. Tap Scan." : "No room for the list.", true,
                     kBody);
  }

  int discX, browseX, w;
  netButtonRects(connected, discX, browseX, w);
  if (discX >= 0) drawOutlineBtn(discX, gNetBtnY, w, kActionBtnH, "Disconnect");
  drawFilledBtn(browseX, gNetBtnY, w, kActionBtnH, "Browse the web");
  presentNavigation();
}

UiHit uiHitNet(int x, int y) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  if (hitHeaderBack(x, y)) {
    hit.kind = UiHit::Kind::Back;
    markHit(hit, kPad - 4, statusBarH() + 10, kHeaderBackW - 8, kHeaderH - 20, kIconRadius);
    return hit;
  }
  int ax, ay, aw, ah;
  headerActionRect(ax, ay, aw, ah);
  if (hitRect(x, y, ax, ay, aw, ah)) {
    hit.kind = UiHit::Kind::NetScan;
    markHit(hit, ax, ay, aw, ah, kIconRadius);
    return hit;
  }
  int discX, browseX, w;
  netButtonRects(gNetConnected, discX, browseX, w);
  if (y >= gNetBtnY && y < gNetBtnY + kActionBtnH) {
    if (discX >= 0 && x >= discX && x < discX + w) {
      hit.kind = UiHit::Kind::NetDisconnect;
      markHit(hit, discX, gNetBtnY, w, kActionBtnH);
    } else if (x >= browseX && x < browseX + w) {
      hit.kind = UiHit::Kind::WebOpen;
      markHit(hit, browseX, gNetBtnY, w, kActionBtnH);
    }
    return hit;
  }
  if (gNetShown > 0 && y >= gNetListY && y < gNetListY + gNetShown * kNetRowH && x >= kPad && x < kScreenW - kPad) {
    const int row = (y - gNetListY) / kNetRowH;
    hit.kind = UiHit::Kind::NetJoin;
    hit.index = row;
    markHit(hit, kPad, gNetListY + row * kNetRowH + 4, kScreenW - 2 * kPad, kNetRowH - 8, 12);
  }
  return hit;
}

void uiDrawProgress(const char* title, int percent, const char* note) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const char* shownTitle = title ? title : "Working...";
  static char lastTitle[64] = {};
  static int lastPercent = 101;
  const bool newProgress = strcmp(lastTitle, shownTitle) != 0 || percent <= lastPercent;
  const int textW = kScreenW - 2 * kPad;
  const int titleY = statusBarH() + 120;
  const int barY = titleY + 2 * (canvasTextHeight(kBody) + 4) + 16;
  if (newProgress) {
    canvasClear();
    drawStatusBar(appsFlashSpace(), false);
    drawWrapped(shownTitle, kPad, titleY, textW, kBody, 2, true);
    if (note && note[0]) drawWrapped(note, kPad, barY + 100, textW, kSmall, 3);
  }
  const int barX = kPad;
  const int barW = textW;
  const int barH = 28;
  const int dirtyH = 48 + canvasTextHeight(kTitle);
  if (!newProgress) canvasFillRect(barX - 2, barY - 2, barW + 4, dirtyH + 4, false);
  canvasDrawRoundRect(barX, barY, barW, barH, 10, true);
  const int fill = (barW - 6) * percent / 100;
  if (fill > 0) canvasFillRoundRect(barX + 3, barY + 3, fill, barH - 6, 6, true);
  char pct[16];
  snprintf(pct, sizeof(pct), "%d%%", percent);
  canvasDrawString(kPad, barY + 44, pct, true, kTitle);
  snprintf(lastTitle, sizeof(lastTitle), "%s", shownTitle);
  lastPercent = percent;
  canvasPresentFor(CanvasRefreshIntent::Progress,
                   newProgress ? CanvasRect{} : CanvasRect{barX - 2, barY - 2, barW + 4, dirtyH + 4});
}

namespace {

// Dialogs: content in the upper half, actions in a row at the bottom.
constexpr int kDialogTop = 150;
int dialogButtonY() { return kScreenH - kPad - kActionBtnH - 8; }

int drawDialogText(const char* title, const char* body, const UiIcon* icon) {
  int y = statusBarH() + kDialogTop;
  if (icon) {
    drawIcon(*icon, kPad + icon->w / 2, y + icon->h / 2);
    y += icon->h + 16;
  }
  y = drawWrapped(title, kPad, y, kScreenW - 2 * kPad, kTitle, 2);
  return drawWrapped(body, kPad, y + 10, kScreenW - 2 * kPad, kBody, 8);
}

}  // namespace

UiHit uiHitMessage(int x, int y) {
  UiHit hit;
  const int okY = dialogButtonY();
  const int okW = kScreenW - 2 * kPad;
  if (x >= kPad && x < kPad + okW && y >= okY && y < okY + kActionBtnH) markHit(hit, kPad, okY, okW, kActionBtnH);
  return hit;
}

void uiDrawMessage(const char* title, const char* body) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  drawDialogText(title ? title : "Notice", body, nullptr);
  drawFilledBtn(kPad, dialogButtonY(), kScreenW - 2 * kPad, kActionBtnH, "OK");
  presentQuality();
}

void uiDrawConfirm(const char* title, const char* body, const char* yesLabel) {
  canvasClear();
  drawStatusBar(appsFlashSpace(), false);
  drawDialogText(title ? title : "Confirm", body, &kIconAlert40);
  const int y = dialogButtonY();
  const int tileW = (kScreenW - 3 * kPad) / 2;
  drawOutlineBtn(kPad, y, tileW, kActionBtnH, "Cancel");
  drawFilledBtn(kPad * 2 + tileW, y, tileW, kActionBtnH, yesLabel && yesLabel[0] ? yesLabel : "OK");
  presentQuality();
}

UiHit uiHitConfirm(int x, int y) {
  UiHit hit;
  const int by = dialogButtonY();
  if (y < by - 8 || y >= by + kActionBtnH + 8) return hit;
  const int tileW = (kScreenW - 3 * kPad) / 2;
  if (x >= kPad && x < kPad + tileW) {
    hit.kind = UiHit::Kind::ConfirmNo;
    markHit(hit, kPad, by, tileW, kActionBtnH);
    return hit;
  }
  if (x >= kPad * 2 + tileW && x < kPad * 2 + tileW * 2) {
    hit.kind = UiHit::Kind::ConfirmYes;
    markHit(hit, kPad * 2 + tileW, by, tileW, kActionBtnH);
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
  if (y >= cy && y < cy + h && x >= closeX && x < closeX + w) markHit(hit, closeX, cy, w, h);
  if (y >= cy && y < cy + h && x >= setX && x < setX + w) {
    hit.kind = UiHit::Kind::SetSleep;
    markHit(hit, setX, cy, w, h);
  }
  return hit;
}

namespace {

// Phone-style QWERTY: 3 letter rows + bottom bar. Fat keys, small side margins.
constexpr int kOskSide = 6;
constexpr int kOskGap = 6;
constexpr int kOskKeyH = 74;  // tall enough for a thumb, and for title-size letters
constexpr int kOskRows = 4;
constexpr int kOskBarH = 52;  // Cancel / Done above keyboard
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
  // Single characters at title size, words ("space", "123") at body size.
  const int scale = label[0] && !label[1] && canvasTextWidth(label, kTitle) + 8 <= w ? kTitle : kBody;
  if (inverted) canvasFillRoundRect(x, y, w, h, kOskRadius, true);
  else canvasDrawRoundRect(x, y, w, h, kOskRadius, true);
  const int tw = canvasTextWidth(label, scale);
  const int th = canvasTextHeight(scale);
  canvasDrawString(x + (w - tw) / 2, y + (h - th) / 2, label, !inverted, scale);
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

void setKey(UiHit& hit, UiHit::Kind kind, int value, int x, int y, int w, int h) {
  hit.kind = kind;
  hit.value = value;
  markHit(hit, x, y, w, h, kOskRadius);
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
          setKey(hit, UiHit::Kind::KeyChar, static_cast<unsigned char>(keys[i]), kx, ry, unit, kOskKeyH);
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
          setKey(hit, UiHit::Kind::KeyChar, static_cast<unsigned char>(keys[i]), kx, ry, unit, kOskKeyH);
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
        setKey(hit, UiHit::Kind::KeyShift, 0, startX, ry, wide, kOskKeyH);
        return true;
      }
      const int midX = startX + wide + kOskGap;
      for (int i = 0; i < n; ++i) {
        const int kx = midX + i * (unit + kOskGap);
        if (hitInBand(x, y, kx, ry, unit, kOskKeyH, halfGap, halfGap)) {
          setKey(hit, UiHit::Kind::KeyChar, static_cast<unsigned char>(keys[i]), kx, ry, unit, kOskKeyH);
          return true;
        }
      }
      if (hitInBand(x, y, midX + midW + kOskGap, ry, wide, kOskKeyH, halfGap, 0)) {
        setKey(hit, UiHit::Kind::KeyBackspace, 0, midX + midW + kOskGap, ry, wide, kOskKeyH);
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
        setKey(hit, UiHit::Kind::KeySymbols, 0, startX, ry, wide, kOskKeyH);
        return true;
      }
      const int commaX = startX + wide + kOskGap;
      if (hitInBand(x, y, commaX, ry, punct, kOskKeyH, halfGap, halfGap)) {
        setKey(hit, UiHit::Kind::KeyChar, ',', commaX, ry, punct, kOskKeyH);
        return true;
      }
      const int spaceX = commaX + punct + kOskGap;
      const int endX = side + usable;
      const int periodW = wide;
      const int periodX = endX - periodW;
      const int spaceW = periodX - kOskGap - spaceX;
      if (hitInBand(x, y, spaceX, ry, spaceW, kOskKeyH, halfGap, halfGap)) {
        setKey(hit, UiHit::Kind::KeySpace, 0, spaceX, ry, spaceW, kOskKeyH);
        return true;
      }
      if (hitInBand(x, y, periodX, ry, periodW, kOskKeyH, halfGap, 0)) {
        setKey(hit, UiHit::Kind::KeyChar, '.', periodX, ry, periodW, kOskKeyH);
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
        setKey(hit, UiHit::Kind::KeyChar, static_cast<unsigned char>(keys[i]), kx, ry, unit, kOskKeyH);
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
          setKey(hit, UiHit::Kind::KeyChar, static_cast<unsigned char>(keys[i]), kx, ry, unit, kOskKeyH);
          return true;
        }
      }
      if (hitInBand(x, y, startX + midW + kOskGap, ry, wide, kOskKeyH, halfGap, 0)) {
        setKey(hit, UiHit::Kind::KeyBackspace, 0, startX + midW + kOskGap, ry, wide, kOskKeyH);
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
        setKey(hit, UiHit::Kind::KeySymbols, 0, startX, ry, wide, kOskKeyH);
        return true;
      }
      const int spaceX = startX + wide + kOskGap;
      const int spaceW = side + usable - spaceX;
      if (hitInBand(x, y, spaceX, ry, spaceW, kOskKeyH, halfGap, 0)) {
        setKey(hit, UiHit::Kind::KeySpace, 0, spaceX, ry, spaceW, kOskKeyH);
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

CanvasRect textFieldWindow(TextEditMode mode) {
  const bool nameMode = mode == TextEditMode::Rename || mode == TextEditMode::NewFolder ||
                        mode == TextEditMode::NewFile || mode == TextEditMode::ApPassword ||
                        mode == TextEditMode::StaPassword || mode == TextEditMode::WebUrl ||
                        mode == TextEditMode::WebField || mode == TextEditMode::SlotRename;
  const int actionY = oskActionBarY();
  const int fieldTop = statusBarH() + kPad + 40;
  if (nameMode) {
    const int boxY = fieldTop + 22;
    return {kPad, boxY, kScreenW - 2 * kPad, 56};
  }
  const int boxTop = fieldTop + 18;
  const int fieldBottom = actionY - 10;
  return {kPad, boxTop, kScreenW - 2 * kPad, std::max(1, fieldBottom - boxTop)};
}

void drawTextEditField(const char* text, TextEditMode mode) {
  const bool nameMode = mode == TextEditMode::Rename || mode == TextEditMode::NewFolder ||
                        mode == TextEditMode::NewFile || mode == TextEditMode::ApPassword ||
                        mode == TextEditMode::StaPassword || mode == TextEditMode::WebUrl ||
                        mode == TextEditMode::WebField || mode == TextEditMode::SlotRename;

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
    else if (mode == TextEditMode::StaPassword) hint = "Network password";
    else if (mode == TextEditMode::WebUrl) hint = "example.com or https://...";
    else if (mode == TextEditMode::WebField) hint = "Search text";
    else if (mode == TextEditMode::SlotRename) hint = "Name shown on Home";
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
    } else if (mode == TextEditMode::StaPassword) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done to join.", true, kSmall);
    } else if (mode == TextEditMode::WebUrl) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done to open.", true, kSmall);
    } else if (mode == TextEditMode::WebField) {
      canvasDrawString(kPad, boxY + boxH + 12, "Then tap Done. Tap the search button.", true, kSmall);
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
  else if (mode == TextEditMode::SlotRename) heading = "Rename app";
  canvasDrawString(kPad, statusBarH() + kPad, heading, true, kTitle);

  drawTextEditField(text, mode);

  const int actionY = oskActionBarY();
  const char* doneLabel = (mode == TextEditMode::EditFile) ? "Save" : "Done";
  drawOutlineBtn(kPad, actionY, 140, kOskBarH, "Cancel");
  drawFilledBtn(kScreenW - kPad - 140, actionY, 140, kOskBarH, doneLabel);

  drawOsk(symbols, shift);
  if (scrub) presentQuality();
  else presentNavigation();
}

void uiRedrawTextEditField(const char* text, TextEditMode mode) {
  drawTextEditField(text, mode);
  const CanvasRect box = textFieldWindow(mode);
  canvasPresentWindowFast(box);
  canvasArmFullClean(1000);
}

UiHit uiHitTextEdit(int x, int y, bool symbols, bool shift) {
  UiHit hit;
  if (y < statusBarH()) {
    hit.kind = UiHit::Kind::OpenShade;
    return hit;
  }
  const int actionY = oskActionBarY();
  if (y >= actionY && y < actionY + kOskBarH) {
    if (x >= kPad && x < kPad + 140) {
      hit.kind = UiHit::Kind::KeyCancel;
      markHit(hit, kPad, actionY, 140, kOskBarH);
      return hit;
    }
    if (x >= kScreenW - kPad - 140 && x < kScreenW - kPad) {
      hit.kind = UiHit::Kind::KeyDone;
      markHit(hit, kScreenW - kPad - 140, actionY, 140, kOskBarH);
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
  const HomeLayout g = homeLayout(gHomeResumeSlot >= 0);
  const int cardW = kScreenW - 2 * kPad;

  if (gHomeResumeSlot >= 0 && y >= g.resumeY && y < g.resumeY + kResumeH && x >= kPad && x < kPad + cardW) {
    hit.kind = UiHit::Kind::ResumeLast;
    hit.index = gHomeResumeSlot;
    markHit(hit, kPad, g.resumeY, cardW, kResumeH, kRadius);
    return hit;
  }

  static const UiHit::Kind kFooter[3] = {UiHit::Kind::OpenFiles, UiHit::Kind::WebOpen, UiHit::Kind::Settings};
  for (int i = 0; i < 3; ++i) {
    int fx, fy, fw, fh;
    homeFooterRect(i, fx, fy, fw, fh);
    if (y >= fy - 6 && y < fy + fh + 6 && x >= fx && x < fx + fw) {
      hit.kind = kFooter[i];
      markHit(hit, fx, fy, fw, fh);
      return hit;
    }
  }

  for (int i = 0; i < kSlotCount; ++i) {
    const int cy = slotCardY(g, i);
    if (y < cy || y >= cy + g.rowH || x < kPad || x >= kPad + cardW) continue;
    hit.index = i;
    const bool occupied = appsSlotInfo(i).occupied;
    if (occupied && x >= kPad + cardW - kCardMoreW) {
      hit.kind = UiHit::Kind::SlotMenuOpen;
      markHit(hit, kPad + cardW - kCardMoreW, cy + 8, kCardMoreW - 8, g.rowH - 16);
      return hit;
    }
    hit.kind = UiHit::Kind::BootSlot;
    markHit(hit, kPad, cy, cardW, g.rowH, kRadius);
    return hit;
  }
  return hit;
}



namespace {

// Slot sheet: Open, Update (when /firmware has a newer build), Rename, Clear.
constexpr int kSlotMenuRowH = 64;
constexpr int kSlotMenuHeadH = 96;

struct SlotMenuItem {
  UiHit::Kind kind;
  const UiIcon* icon;
  const char* label;
};

int slotMenuItems(bool hasUpdate, SlotMenuItem out[4]) {
  int n = 0;
  out[n++] = {UiHit::Kind::BootSlot, &kIconPlay28, "Open"};
  if (hasUpdate) out[n++] = {UiHit::Kind::SlotUpdate, &kIconUpdate24, "Update"};
  out[n++] = {UiHit::Kind::SlotRename, &kIconRename24, "Rename"};
  out[n++] = {UiHit::Kind::ClearSlot, &kIconTrash24, "Clear slot"};
  return n;
}

int slotMenuTop(int count) { return kScreenH - (kSlotMenuHeadH + count * kSlotMenuRowH + 16); }

}  // namespace

void uiDrawSlotMenu(const SlotInfo& slot) {
  SlotMenuItem items[4];
  const int n = slotMenuItems(!slot.updatePath.empty(), items);
  const int top = slotMenuTop(n);
  canvasFillRect(0, top, kScreenW, kScreenH - top, false);
  canvasDrawLine(0, top, kScreenW - 1, top, true);
  canvasDrawLine(0, top + 1, kScreenW - 1, top + 1, true);
  canvasFillRoundRect(kScreenW / 2 - 28, top + 10, 56, 5, 2, true);

  char title[48];
  snprintf(title, sizeof(title), "%s", slot.title.empty() ? slot.name.c_str() : slot.title.c_str());
  fitText(title, kScreenW - 2 * kPad, kTitle);
  canvasDrawString(kPad, top + 26, title, true, kTitle);
  char sub[64];
  if (!slot.version.empty()) snprintf(sub, sizeof(sub), "Slot %c  |  version %s", slot.label[0], slot.version.c_str());
  else snprintf(sub, sizeof(sub), "Slot %c", slot.label[0]);
  canvasDrawString(kPad, top + 28 + canvasTextHeight(kTitle), sub, true, kSmall);

  for (int i = 0; i < n; ++i) {
    const int ry = top + kSlotMenuHeadH + i * kSlotMenuRowH;
    canvasDrawLine(kPad, ry, kScreenW - kPad - 1, ry, true);
    drawIcon(*items[i].icon, kPad + 16, ry + kSlotMenuRowH / 2);
    char label[48];
    if (items[i].kind == UiHit::Kind::SlotUpdate) snprintf(label, sizeof(label), "Update to %s", slot.updateVersion.c_str());
    else snprintf(label, sizeof(label), "%s", items[i].label);
    canvasDrawString(kPad + 48, ry + (kSlotMenuRowH - canvasTextHeight(kBody)) / 2, label, true, kBody);
  }
  presentNavigation();
}

UiHit uiHitSlotMenu(int x, int y, bool hasUpdate) {
  UiHit hit;
  SlotMenuItem items[4];
  const int n = slotMenuItems(hasUpdate, items);
  const int top = slotMenuTop(n);
  hit.kind = UiHit::Kind::SlotMenuDismiss;
  if (y < top + kSlotMenuHeadH) return hit;
  const int row = (y - top - kSlotMenuHeadH) / kSlotMenuRowH;
  if (row < 0 || row >= n || x < kPad || x >= kScreenW - kPad) return hit;
  hit.kind = items[row].kind;
  markHit(hit, kPad, top + kSlotMenuHeadH + row * kSlotMenuRowH + 4, kScreenW - 2 * kPad, kSlotMenuRowH - 8);
  return hit;
}

bool uiIsBackSwipe(int x0, int y0, int x1, int y1) {
  const int dx = x1 - x0;
  return x0 < 40 && dx > 120 && abs(y1 - y0) * 2 < dx;
}
