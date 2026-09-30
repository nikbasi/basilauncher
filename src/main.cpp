#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <algorithm>
#include <vector>

#include "apps_scan.h"
#include "board_hal.h"
#include "canvas.h"
#include "flash_install.h"
#include "sd_serial.h"
#include "sleep_screen.h"
#include "ui.h"

#include <SD.h>
#include <cstring>

namespace {

Screen gScreen = Screen::Home;
std::vector<DirEntry> gPickEntries;
char gPickPath[192] = "/firmware";
SlotInfo gSlots[kSlotCount];
FlashSpace gSpace;
int gPickScroll = 0;
int gAssignSlot = -1;  // -1 = best-fit; 0..3 = specific empty slot
uint8_t gLastClockMinute = 255;
uint32_t gLastActiveMs = 0;

void noteActivity() { gLastActiveMs = millis(); }

struct ProgressCtx {
  const char* title;
  int lastPct;
};

void refreshSlots() {
  appsAllSlots(gSlots);
  gSpace = appsFlashSpace();
}

void showHome() {
  gScreen = Screen::Home;
  gAssignSlot = -1;
  gPickScroll = 0;
  refreshSlots();
  uiDrawHome(gSlots, gSpace);
  const BoardClockInfo c = boardClock();
  gLastClockMinute = c.valid ? c.minute : 255;
}

void buildPickerList() {
  if (!SD.exists(gPickPath) && strcmp(gPickPath, "/firmware") == 0) {
    SD.mkdir("/firmware");
  }
  gPickEntries = appsScanDir(gPickPath);
}

size_t pickerMaxBytes() {
  if (gAssignSlot >= 0 && gAssignSlot < kSlotCount) return gSlots[gAssignSlot].capacity;
  size_t maxCap = 0;
  for (int i = 0; i < kSlotCount; ++i) {
    if (!gSlots[i].occupied && gSlots[i].capacity > maxCap) maxCap = gSlots[i].capacity;
  }
  return maxCap;
}

void redrawPicker() {
  uiDrawPicker(gPickEntries, gPickScroll, gAssignSlot, pickerMaxBytes(), gPickPath, gSpace);
}

void showPicker(int targetSlot) {
  gAssignSlot = targetSlot;
  gScreen = Screen::Picker;
  // Prefer /firmware when present; otherwise start at SD root.
  if (SD.exists("/firmware")) {
    snprintf(gPickPath, sizeof(gPickPath), "/firmware");
  } else {
    snprintf(gPickPath, sizeof(gPickPath), "/");
  }
  refreshSlots();
  buildPickerList();
  gPickScroll = 0;
  redrawPicker();
}

void enterPickDir(const char* path) {
  if (!path || !path[0]) return;
  snprintf(gPickPath, sizeof(gPickPath), "%s", path);
  buildPickerList();
  gPickScroll = 0;
  redrawPicker();
}

void goUpPickDir() {
  char parent[192];
  appsParentDir(gPickPath, parent, sizeof(parent));
  enterPickDir(parent);
}

void progressCb(size_t written, size_t total, void* ctx) {
  auto* p = static_cast<ProgressCtx*>(ctx);
  const int pct = total ? static_cast<int>((written * 100u) / total) : 0;
  if (pct >= p->lastPct + 5 || pct == 100) {
    p->lastPct = pct;
    uiDrawProgress(p->title, pct);
  }
}

void installFileToSlot(int fileIndex, int slotIndex) {
  if (fileIndex < 0 || fileIndex >= static_cast<int>(gPickEntries.size())) return;
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;

  const DirEntry& file = gPickEntries[fileIndex];
  if (file.isDir) return;
  if (gSlots[slotIndex].occupied) {
    gScreen = Screen::Message;
    uiDrawMessage("Protected", "Clear the slot before assigning a new app.");
    return;
  }
  if (file.size > gSlots[slotIndex].capacity) {
    gScreen = Screen::Message;
    uiDrawMessage("Too large", "That firmware does not fit this slot.");
    return;
  }

  gScreen = Screen::Progress;
  char title[64];
  snprintf(title, sizeof(title), "%s -> %c", file.name.c_str(), 'A' + slotIndex);
  ProgressCtx ctx{title, -1};
  uiDrawProgress(title, 0);

  const FlashResult res = flashValidateAndWrite(file.path.c_str(), slotIndex, progressCb, &ctx);
  if (res != FlashResult::Ok) {
    gScreen = Screen::Message;
    char body[96];
    snprintf(body, sizeof(body), "Install failed: %s", flashResultName(res));
    uiDrawMessage("Error", body);
    return;
  }

  appsSaveSlotLabel(slotIndex, file.name.c_str(), file.size);
  showHome();
}

void installPicked(int pickIndex) {
  if (pickIndex < 0 || pickIndex >= static_cast<int>(gPickEntries.size())) return;
  const DirEntry& file = gPickEntries[pickIndex];
  if (file.isDir) {
    enterPickDir(file.path.c_str());
    return;
  }

  const size_t maxB = pickerMaxBytes();
  if (file.size > maxB) {
    gScreen = Screen::Message;
    char body[96], need[24], cap[24];
    appsFormatBytes(file.size, need, sizeof(need));
    appsFormatBytes(maxB, cap, sizeof(cap));
    snprintf(body, sizeof(body), "%s needs %s; max empty slot is %s.", file.name.c_str(), need, cap);
    uiDrawMessage("Too large", body);
    return;
  }

  int slot = gAssignSlot;
  if (slot < 0) {
    slot = appsBestFitSlot(file.size);
    if (slot < 0) {
      gScreen = Screen::Message;
      uiDrawMessage("Won't fit", "No empty slot is large enough. Clear one first.");
      return;
    }
  }
  installFileToSlot(pickIndex, slot);
}

void bootExisting(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;
  refreshSlots();
  if (!gSlots[slotIndex].occupied) {
    showPicker(slotIndex);
    return;
  }
  gScreen = Screen::Progress;
  uiDrawProgress("Booting...", 100);
  delay(200);
  const FlashResult res = bootSlotPendingVerify(slotIndex);
  if (res != FlashResult::Ok) {
    gScreen = Screen::Message;
    char body[96];
    snprintf(body, sizeof(body), "Boot failed: %s", flashResultName(res));
    uiDrawMessage("Error", body);
  }
}

void clearSlot(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;
  refreshSlots();
  if (!gSlots[slotIndex].occupied) return;

  gScreen = Screen::Progress;
  uiDrawProgress("Clearing...", 50);
  const FlashResult res = flashEraseSlot(slotIndex);
  if (res != FlashResult::Ok) {
    gScreen = Screen::Message;
    char body[96];
    snprintf(body, sizeof(body), "Clear failed: %s", flashResultName(res));
    uiDrawMessage("Error", body);
    return;
  }
  appsClearSlotLabel(slotIndex);
  showHome();
}

int pickerVisibleRows() { return uiPickerVisibleRows(); }

void showShade() {
  gScreen = Screen::Shade;
  refreshSlots();
  canvasRequestCleanRefresh();
  uiDrawShade(gSpace);
}

void handleShadeHit(const UiHit& hit) {
  switch (hit.kind) {
    case UiHit::Kind::CloseShade:
      showHome();
      break;
    case UiHit::Kind::BrightnessMinus:
      boardSetBrightness(boardBrightness() - 5);
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::BrightnessPlus:
      boardSetBrightness(boardBrightness() + 5);
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::BrightnessSlider:
      if (hit.value >= 0) {
        boardSetBrightness(hit.value);
        uiDrawShade(gSpace);
      }
      break;
    case UiHit::Kind::LightToggle:
      boardSetFrontlightOn(!boardFrontlightOn());
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::HourMinus:
    case UiHit::Kind::HourPlus:
    case UiHit::Kind::MinuteMinus:
    case UiHit::Kind::MinutePlus: {
      int delta = 0;
      if (hit.kind == UiHit::Kind::HourMinus) delta = -60;
      if (hit.kind == UiHit::Kind::HourPlus) delta = 60;
      if (hit.kind == UiHit::Kind::MinuteMinus) delta = -1;
      if (hit.kind == UiHit::Kind::MinutePlus) delta = 1;
      if (!boardAdjustClockMinutes(delta)) {
        // RTC never set / VL flag — seed a default then adjust.
        BoardClockInfo c = boardClock();
        uint16_t y = c.valid ? c.year : 2026;
        uint8_t mo = c.valid ? c.month : 1;
        uint8_t d = c.valid ? c.day : 1;
        uint8_t h = c.valid ? c.hour : 12;
        uint8_t mi = c.valid ? c.minute : 0;
        int total = static_cast<int>(h) * 60 + static_cast<int>(mi) + delta;
        while (total < 0) total += 24 * 60;
        total %= 24 * 60;
        boardSetClock(y, mo, d, static_cast<uint8_t>(total / 60), static_cast<uint8_t>(total % 60));
      }
      uiDrawShade(gSpace);
      break;
    }
    case UiHit::Kind::CleanEveryMinus:
      boardSetCleanEvery(boardCleanEvery() - 1);
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::CleanEveryPlus:
      boardSetCleanEvery(boardCleanEvery() + 1);
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::ScrubNow:
      canvasRequestCleanRefresh();
      uiDrawShade(gSpace);
      break;
    case UiHit::Kind::Settings:
      gScreen = Screen::Settings;
      refreshSlots();
      uiDrawSettings(gSpace);
      break;
    default:
      break;
  }
}

void handleTouch(int x, int y) {
  if (gScreen == Screen::Message) {
    showHome();
    return;
  }

  if (gScreen == Screen::Progress) return;

  if (gScreen == Screen::Shade) {
    handleShadeHit(uiHitShade(x, y));
    return;
  }

  if (gScreen == Screen::Settings) {
    const UiHit hit = uiHitSettings(x, y);
    if (hit.kind == UiHit::Kind::Back) {
      showHome();
    } else if (hit.kind == UiHit::Kind::PowerOff) {
      enterSleepWithScreensaver();
    } else if (hit.kind == UiHit::Kind::SleepAfterMinus) {
      boardSetSleepAfterMin(boardSleepAfterMin() <= 0 ? 0 : boardSleepAfterMin() - 1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::SleepAfterPlus) {
      boardSetSleepAfterMin(boardSleepAfterMin() + 1);
      uiDrawSettings(gSpace);
    }
    return;
  }

  if (gScreen == Screen::Picker) {
    const bool canUp = !appsIsRootDir(gPickPath);
    const UiHit hit =
        uiHitPicker(x, y, static_cast<int>(gPickEntries.size()), gPickScroll, canUp);
    const int visible = pickerVisibleRows();
    switch (hit.kind) {
      case UiHit::Kind::Back:
        showHome();
        break;
      case UiHit::Kind::GoUp:
        goUpPickDir();
        break;
      case UiHit::Kind::ScrollUp:
        if (gPickScroll > 0) {
          gPickScroll--;
          redrawPicker();
        }
        break;
      case UiHit::Kind::ScrollDown:
        if (gPickScroll + visible < static_cast<int>(gPickEntries.size())) {
          gPickScroll++;
          redrawPicker();
        }
        break;
      case UiHit::Kind::PickFile:
        installPicked(hit.index);
        break;
      default:
        break;
    }
    return;
  }

  if (gScreen == Screen::Home) {
    const UiHit hit = uiHitHome(x, y);
    switch (hit.kind) {
      case UiHit::Kind::OpenShade:
        showShade();
        break;
      case UiHit::Kind::BootSlot:
        refreshSlots();
        if (hit.index >= 0 && hit.index < kSlotCount && !gSlots[hit.index].occupied) {
          showPicker(hit.index);
        } else {
          bootExisting(hit.index);
        }
        break;
      case UiHit::Kind::ClearSlot:
        refreshSlots();
        if (hit.index >= 0 && hit.index < kSlotCount && gSlots[hit.index].occupied) {
          clearSlot(hit.index);
        }
        break;
      case UiHit::Kind::AssignSlot:
        showPicker(hit.index);
        break;
      default:
        break;
    }
  }
}

}  // namespace

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.setTxBufferSize(2048);
  Serial.begin(115200);
  delay(200);
  Serial.println("Basilauncher " BASILAUNCHER_VERSION);

  boardMarkFactoryValid();
  boardInit();

  if (!boardInitDisplay()) {
    Serial.println("Display init failed");
  }
  boardInitTouch();
  boardInitSd();
  boardInitPower();
  boardInitClock();
  boardInitFrontlight();
  appsLoadSlotLabels();

  // Chip wakes on any BOOT press; only a hold keeps us awake (matches sleep).
  sleepRequireBootHoldToWake(1500);

  uiDrawSplash();
  delay(1200);

  showHome();
  noteActivity();
}

void loop() {
  // USB-CDC host can push .bin files onto the internal SD (/firmware/...).
  if (gScreen != Screen::Progress) {
    sdSerialPoll();
  }

  boardInputUpdate();

  // Hold BOOT (top-left, same as Aurora's power hold) → random screensaver + deep sleep.
  static bool bootSleepArmed = true;
  if (!boardPowerPressed()) {
    bootSleepArmed = true;
  } else if (bootSleepArmed && boardPowerHeldMs() > 1500 && gScreen != Screen::Progress) {
    bootSleepArmed = false;
    enterSleepWithScreensaver();
  }

  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (boardPollSwipe(x0, y0, x1, y1)) {
    noteActivity();
    const int dy = y1 - y0;
    const int dx = x1 - x0;
    // Swipe down from top → open shade
    if (gScreen == Screen::Home && y0 < 120 && dy > 80 && abs(dy) > abs(dx)) {
      showShade();
      return;
    }
    // Swipe up while shade open → close
    if (gScreen == Screen::Shade && dy < -80 && abs(dy) > abs(dx)) {
      showHome();
      return;
    }
  }

  // Live brightness drag while shade is open
  if (gScreen == Screen::Shade) {
    int hx = 0, hy = 0;
    if (boardTouchHeld(hx, hy)) {
      noteActivity();
      int bx, by, bw, bh;
      uiShadeBrightnessTrack(bx, by, bw, bh);
      if (hy >= by - 20 && hy <= by + bh + 20 && hx >= bx && hx <= bx + bw) {
        static int lastBright = -1;
        const int pct = uiBrightnessFromTouchX(hx);
        if (pct != lastBright) {
          lastBright = pct;
          boardSetBrightness(pct);
          // Avoid full e-ink redraw every pixel — only on release via tap path,
          // but apply light live. Redraw shade occasionally.
          static uint32_t lastDraw = 0;
          const uint32_t now = millis();
          if (now - lastDraw > 400) {
            lastDraw = now;
            uiDrawShade(gSpace);
          }
        }
      }
    }
  }

  int x = 0, y = 0;
  if (boardPollTouch(x, y)) {
    noteActivity();
    Serial.printf("tap %d,%d screen=%d\n", x, y, static_cast<int>(gScreen));
    handleTouch(x, y);
  }

  if (gScreen == Screen::Home) {
    const BoardClockInfo c = boardClock();
    if (c.valid && c.minute != gLastClockMinute) {
      gLastClockMinute = c.minute;
      refreshSlots();
      uiDrawHome(gSlots, gSpace);
    }
  }

  // Idle screensaver: same random /sleep image path as manual Sleep.
  const int sleepMin = boardSleepAfterMin();
  if (sleepMin > 0 && gScreen != Screen::Progress &&
      (millis() - gLastActiveMs) > static_cast<uint32_t>(sleepMin) * 60u * 1000u) {
    enterSleepWithScreensaver();
  }
  delay(20);
}
