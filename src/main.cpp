#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <algorithm>
#include <vector>

#include "apps_scan.h"
#include "board_hal.h"
#include "canvas.h"
#include "flash_install.h"
#include "ui.h"

namespace {

Screen gScreen = Screen::Home;
std::vector<FirmwareFile> gAllFiles;
std::vector<FirmwareFile> gPickFiles;  // filtered for current picker
SlotInfo gSlots[kSlotCount];
FlashSpace gSpace;
int gPickScroll = 0;
int gAssignSlot = -1;  // -1 = best-fit; 0..3 = specific empty slot
uint8_t gLastClockMinute = 255;

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

void buildPickerList(size_t maxBytes) {
  gPickFiles.clear();
  gPickFiles.reserve(gAllFiles.size());
  for (const auto& f : gAllFiles) {
    if (f.size <= maxBytes) gPickFiles.push_back(f);
  }
}

size_t pickerMaxBytes() {
  if (gAssignSlot >= 0 && gAssignSlot < kSlotCount) return gSlots[gAssignSlot].capacity;
  // Best-fit: largest empty capacity (any file that fits somewhere)
  size_t maxCap = 0;
  for (int i = 0; i < kSlotCount; ++i) {
    if (!gSlots[i].occupied && gSlots[i].capacity > maxCap) maxCap = gSlots[i].capacity;
  }
  return maxCap;
}

void showPicker(int targetSlot) {
  gAssignSlot = targetSlot;
  gScreen = Screen::Picker;
  gAllFiles = appsScanFirmwareDir();
  refreshSlots();
  const size_t maxB = pickerMaxBytes();
  buildPickerList(maxB);
  gPickScroll = 0;
  uiDrawPicker(gPickFiles, gPickScroll, gAssignSlot, maxB, gSpace);
}

void redrawPicker() {
  uiDrawPicker(gPickFiles, gPickScroll, gAssignSlot, pickerMaxBytes(), gSpace);
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
  if (fileIndex < 0 || fileIndex >= static_cast<int>(gPickFiles.size())) return;
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;

  const FirmwareFile& file = gPickFiles[fileIndex];
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
  uiDrawProgress("Starting...", 100);
  delay(300);
  bootSlotPendingVerify(slotIndex);
}

void installPicked(int pickIndex) {
  if (pickIndex < 0 || pickIndex >= static_cast<int>(gPickFiles.size())) return;
  const FirmwareFile& file = gPickFiles[pickIndex];

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

int pickerVisibleRows() {
  constexpr int kStatusH = 70;
  constexpr int kDockH = 72;
  constexpr int kPad = 18;
  constexpr int kRowH = 56;
  const int listTop = kStatusH + kPad + 56;
  const int listBottom = kScreenH - kDockH - kPad;
  return std::max(1, (listBottom - listTop) / kRowH);
}

void handleTouch(int x, int y) {
  if (gScreen == Screen::Message) {
    showHome();
    return;
  }

  if (gScreen == Screen::Progress) return;

  if (gScreen == Screen::Settings) {
    const UiHit hit = uiHitSettings(x, y);
    if (hit.kind == UiHit::Kind::Back) {
      showHome();
    } else if (hit.kind == UiHit::Kind::PowerOff) {
      canvasClear();
      canvasDrawString(160, 450, "Powered off", true, 3);
      canvasPresent(EInkDisplay::FULL_REFRESH);
      delay(500);
      boardPrepareDeepSleep();
      esp_deep_sleep_start();
    }
    return;
  }

  if (gScreen == Screen::Picker) {
    const UiHit hit = uiHitPicker(x, y, static_cast<int>(gPickFiles.size()), gPickScroll);
    const int visible = pickerVisibleRows();
    switch (hit.kind) {
      case UiHit::Kind::Back:
        showHome();
        break;
      case UiHit::Kind::ScrollUp:
        if (gPickScroll > 0) {
          gPickScroll--;
          redrawPicker();
        }
        break;
      case UiHit::Kind::ScrollDown:
        if (gPickScroll + visible < static_cast<int>(gPickFiles.size())) {
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
      case UiHit::Kind::Settings:
        gScreen = Screen::Settings;
        refreshSlots();
        uiDrawSettings(gSpace);
        break;
      case UiHit::Kind::OpenPicker:
        showPicker(-1);
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
  appsLoadSlotLabels();

  canvasClear();
  canvasDrawString(120, 400, "Basilauncher", true, 4);
  canvasDrawString(200, 460, "v" BASILAUNCHER_VERSION, true, 3);
  canvasPresent(EInkDisplay::FULL_REFRESH);
  delay(400);

  showHome();
}

void loop() {
  int x = 0, y = 0;
  if (boardPollTouch(x, y)) {
    Serial.printf("tap %d,%d screen=%d\n", x, y, static_cast<int>(gScreen));
    handleTouch(x, y);
  }

  // Refresh home clock when the minute rolls (e-ink, once per minute).
  if (gScreen == Screen::Home) {
    const BoardClockInfo c = boardClock();
    if (c.valid && c.minute != gLastClockMinute) {
      gLastClockMinute = c.minute;
      refreshSlots();
      uiDrawHome(gSlots, gSpace);
    }
  }
  delay(20);
}
