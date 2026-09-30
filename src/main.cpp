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
std::vector<FirmwareFile> gFiles;
SlotInfo gSlots[kSlotCount];
FlashSpace gSpace;
int gScroll = 0;

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
  refreshSlots();
  gFiles = appsScanFirmwareDir();
  gScroll = std::min(gScroll, uiHomeMaxScroll(static_cast<int>(gFiles.size())));
  uiDrawHome(gSlots, gSpace, gFiles, gScroll);
}

void progressCb(size_t written, size_t total, void* ctx) {
  auto* p = static_cast<ProgressCtx*>(ctx);
  const int pct = total ? static_cast<int>((written * 100u) / total) : 0;
  if (pct >= p->lastPct + 5 || pct == 100) {
    p->lastPct = pct;
    uiDrawProgress(p->title, pct);
  }
}

// Install only into an empty best-fit guest slot. Never touches factory / occupied.
void installFileBestFit(int fileIndex) {
  if (fileIndex < 0 || fileIndex >= static_cast<int>(gFiles.size())) return;

  const FirmwareFile& file = gFiles[fileIndex];
  const int slotIndex = appsBestFitSlot(file.size);
  if (slotIndex < 0) {
    gScreen = Screen::Message;
    char body[128];
    char need[24], freeB[24];
    appsFormatBytes(file.size, need, sizeof(need));
    appsFormatBytes(gSpace.guestFree, freeB, sizeof(freeB));
    snprintf(body, sizeof(body), "No empty slot fits %s (free %s). Clear a slot first.", need,
             freeB);
    uiDrawMessage("Won't fit", body);
    return;
  }

  // Refuse if somehow occupied (best-fit should only return empty).
  if (appsSlotInfo(slotIndex).occupied) {
    gScreen = Screen::Message;
    uiDrawMessage("Protected", "That slot is occupied. Clear it first.");
    return;
  }

  gScreen = Screen::Progress;
  ProgressCtx ctx{file.name.c_str(), -1};
  char title[64];
  snprintf(title, sizeof(title), "%s -> %c", file.name.c_str(), 'A' + slotIndex);
  uiDrawProgress(title, 0);
  ctx.title = title;

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

void bootExisting(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;
  const SlotInfo info = appsSlotInfo(slotIndex);
  if (!info.occupied) {
    gScreen = Screen::Message;
    uiDrawMessage("Empty", "Nothing to boot in this slot.");
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
  const SlotInfo info = appsSlotInfo(slotIndex);
  if (!info.occupied) {
    gScreen = Screen::Message;
    uiDrawMessage("Empty", "Slot already empty.");
    return;
  }

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

void handleTouch(int x, int y) {
  if (gScreen == Screen::Message) {
    showHome();
    return;
  }

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

  if (gScreen == Screen::Home) {
    if (y < 56 && gScroll > 0) {
      gScroll--;
      uiDrawHome(gSlots, gSpace, gFiles, gScroll);
      return;
    }

    const UiHit hit = uiHitHome(x, y, static_cast<int>(gFiles.size()), gScroll);
    switch (hit.kind) {
      case UiHit::Kind::Settings:
        gScreen = Screen::Settings;
        uiDrawSettings(gSpace);
        break;
      case UiHit::Kind::BootSlot:
        bootExisting(hit.index);
        break;
      case UiHit::Kind::ClearSlot:
        clearSlot(hit.index);
        break;
      case UiHit::Kind::InstallFile:
        installFileBestFit(hit.index);
        break;
      default:
        if (y > 780 && gScroll < uiHomeMaxScroll(static_cast<int>(gFiles.size()))) {
          gScroll++;
          uiDrawHome(gSlots, gSpace, gFiles, gScroll);
        }
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
  appsLoadSlotLabels();

  canvasClear();
  canvasDrawString(120, 420, "Basilauncher", true, 4);
  canvasDrawString(200, 480, "v" BASILAUNCHER_VERSION, true, 3);
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
  delay(10);
}
