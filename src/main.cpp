#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <vector>

#include "apps_scan.h"
#include "board_hal.h"
#include "browser/fetch.h"
#include "browser/web.h"
#include "canvas.h"
#include "file_ops.h"
#include "flash_install.h"
#include "gps.h"
#include "image_draw.h"
#include "sd_serial.h"
#include "sleep_screen.h"
#include "ui.h"
#include "wifi_server.h"
#include "wifi_session.h"

#include <SD.h>
#include <esp_task_wdt.h>

extern uint32_t gSdHostActiveMs;

namespace {

Screen gScreen = Screen::Home;
Screen gShadeReturn = Screen::Home;
Screen gGpsReturn = Screen::Home;
std::vector<DirEntry> gEntries;
char gPath[kFilePathMax] = "/";
char gBrowsePath[kFilePathMax] = "/";
SlotInfo gSlots[kSlotCount];
FlashSpace gSpace;
int gScroll = 0;
int gSelected = -1;
std::vector<int> gSelectedEntries;
std::vector<std::string> gSelectedPaths;
bool gMultiSelect = false;
int gAssignSlot = -1;
ExplorerMode gExplorerMode = ExplorerMode::Browse;
bool gSheetOpen = false;
uint8_t gLastClockMinute = 255;
uint32_t gLastActiveMs = 0;
uint32_t gLastHomeScrubMs = 0;
// Set while a brightness drag is active. The e-ink refresh blocks this loop,
// so a separate task keeps the lamp on the finger.
volatile bool gBrightFollow = false;

void brightnessFollowTask(void*) {
  for (;;) {
    if (gBrightFollow) {
      int x = 0;
      int y = 0;
      if (boardTouchHeld(x, y)) {
        const int pct = uiBrightnessFromTouchX(x);
        if (pct != boardBrightness()) boardPreviewBrightness(pct);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(12));
  }
}

char gTextBuf[kTextEditMax];
size_t gTextLen = 0;
char gTextPath[kFilePathMax] = {};
char gImagePath[kFilePathMax] = {};
char gTextTitle[48] = {};
TextEditMode gTextMode = TextEditMode::EditFile;
bool gOskShift = false;
bool gOskSymbols = false;

enum class ConfirmAction { None, Delete };
ConfirmAction gConfirmAction = ConfirmAction::None;
std::vector<std::string> gConfirmPaths;

bool gMessageReturnExplorer = false;
bool gMessageReturnWifi = false;
bool gMessageReturnNet = false;

constexpr int kNetMax = 12;
WifiAp gNets[kNetMax];
int gNetCount = 0;
char gJoinSsid[33] = {};

void noteActivity() { gLastActiveMs = millis(); }

struct ProgressCtx {
  const char* title;
  int lastPct;
};

void refreshSlots() {
  appsAllSlots(gSlots);
  gSpace = appsFlashSpace();
}

void syncExplorerSelection() {
  std::vector<std::string> existingPaths;
  std::vector<int> indices;
  existingPaths.reserve(gSelectedPaths.size());
  indices.reserve(gSelectedPaths.size());
  for (const auto& selectedPath : gSelectedPaths) {
    for (int i = 0; i < static_cast<int>(gEntries.size()); ++i) {
      if (gEntries[i].path == selectedPath) {
        existingPaths.push_back(selectedPath);
        indices.push_back(i);
        break;
      }
    }
  }
  gSelectedPaths = std::move(existingPaths);
  gSelectedEntries = std::move(indices);
  gSelected = gSelectedEntries.size() == 1 ? gSelectedEntries.front() : -1;
}

void clearExplorerSelection() {
  gSelectedEntries.clear();
  gSelectedPaths.clear();
  gSelected = -1;
  gMultiSelect = false;
}

void selectOnlyExplorerEntry(int index) {
  gSelectedPaths.clear();
  if (index >= 0 && index < static_cast<int>(gEntries.size())) {
    gSelectedPaths.push_back(gEntries[index].path);
  }
  gMultiSelect = false;
  syncExplorerSelection();
}

void toggleExplorerEntry(int index) {
  if (index < 0 || index >= static_cast<int>(gEntries.size())) return;
  const std::string& path = gEntries[index].path;
  auto it = std::find(gSelectedPaths.begin(), gSelectedPaths.end(), path);
  if (it == gSelectedPaths.end()) gSelectedPaths.push_back(path);
  else gSelectedPaths.erase(it);
  syncExplorerSelection();
}

std::vector<std::string> selectedExplorerPaths() {
  return gSelectedPaths;
}

// Soft maintenance scrub while the hub sits idle on Home.
void maybeIdleScrubHome() {
  if (gScreen != Screen::Home) return;
  constexpr uint32_t kIdleMs = 3u * 60u * 1000u;
  const uint32_t now = millis();
  if (now - gLastActiveMs < kIdleMs) return;
  if (now - gLastHomeScrubMs < kIdleMs) return;
  gLastHomeScrubMs = now;
  canvasRequestCleanRefresh();
  canvasServiceRefresh();
}

void showMessage(const char* title, const char* body, bool returnExplorer) {
  canvasSetHoldCleanRefresh(false);
  gMessageReturnExplorer = returnExplorer;
  gMessageReturnWifi = false;
  gMessageReturnNet = false;
  gScreen = Screen::Message;
  uiDrawMessage(title, body);
}

void showWifiNotice(const char* title, const char* body) {
  canvasSetHoldCleanRefresh(false);
  gMessageReturnExplorer = false;
  gMessageReturnWifi = true;
  gMessageReturnNet = false;
  gScreen = Screen::Message;
  uiDrawMessage(title, body);
}

void showHome(bool stable = false) {
  canvasSetHoldCleanRefresh(false);
  gScreen = Screen::Home;
  gAssignSlot = -1;
  gScroll = 0;
  clearExplorerSelection();
  gSheetOpen = false;
  refreshSlots();
  uiDrawHome(gSlots, gSpace, stable);
  const BoardClockInfo c = boardClock();
  gLastClockMinute = c.valid ? c.minute : 255;
  gLastHomeScrubMs = millis();
}

size_t installMaxBytes() {
  if (gAssignSlot >= 0 && gAssignSlot < kSlotCount) return gSlots[gAssignSlot].capacity;
  size_t maxCap = 0;
  for (int i = 0; i < kSlotCount; ++i) {
    if (!gSlots[i].occupied && gSlots[i].capacity > maxCap) maxCap = gSlots[i].capacity;
  }
  return maxCap;
}

void buildExplorerList() {
  if (!SD.exists(gPath) && strcmp(gPath, "/firmware") == 0) SD.mkdir("/firmware");
  gEntries = appsScanDir(gPath);
  syncExplorerSelection();
}

void redrawExplorer(bool viewportOnly = false) {
  canvasSetHoldCleanRefresh(false);
  ExplorerDrawState st;
  st.mode = gExplorerMode;
  st.targetSlot = gAssignSlot;
  st.maxBytes = installMaxBytes();
  st.scroll = gScroll;
  st.selected = gSelected;
  st.selectedIndices = &gSelectedEntries;
  st.multiSelect = gMultiSelect;
  st.sheetOpen = gSheetOpen;
  st.clipboardHas = fileClipboard().hasItem;
  st.clipboardCut = fileClipboard().isCut;
  st.clipboardCount = fileClipboard().paths.size();
  st.currentPath = gPath;
  gScreen = Screen::Explorer;
  if (viewportOnly && !st.sheetOpen) uiRedrawExplorerViewport(gEntries, st);
  else uiDrawExplorer(gEntries, st, gSpace);
}

void showExplorer(ExplorerMode mode, int targetSlot) {
  gExplorerMode = mode;
  gAssignSlot = targetSlot;
  gScroll = 0;
  clearExplorerSelection();
  gSheetOpen = false;
  refreshSlots();

  if (mode == ExplorerMode::Install) {
    if (SD.exists("/firmware")) snprintf(gPath, sizeof(gPath), "/firmware");
    else snprintf(gPath, sizeof(gPath), "/");
  } else {
    snprintf(gPath, sizeof(gPath), "%s", gBrowsePath[0] ? gBrowsePath : "/");
    if (!SD.exists(gPath)) snprintf(gPath, sizeof(gPath), "/");
  }

  buildExplorerList();
  redrawExplorer();
}

void rememberBrowsePath() {
  if (gExplorerMode == ExplorerMode::Browse) {
    snprintf(gBrowsePath, sizeof(gBrowsePath), "%s", gPath);
  }
}

void enterDir(const char* path) {
  if (!path || !path[0]) return;
  snprintf(gPath, sizeof(gPath), "%s", path);
  rememberBrowsePath();
  clearExplorerSelection();
  buildExplorerList();
  gScroll = 0;
  redrawExplorer();
}

void goUpDir() {
  char parent[kFilePathMax];
  appsParentDir(gPath, parent, sizeof(parent));
  enterDir(parent);
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
  if (fileIndex < 0 || fileIndex >= static_cast<int>(gEntries.size())) return;
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;

  const DirEntry& file = gEntries[fileIndex];
  if (file.isDir || !fileOpsIsBin(file.name.c_str())) return;
  if (gSlots[slotIndex].occupied) {
    showMessage("Protected", "Clear the slot before assigning a new app.", true);
    return;
  }
  if (file.size > gSlots[slotIndex].capacity) {
    showMessage("Too large", "That firmware does not fit this slot.", true);
    return;
  }

  gScreen = Screen::Progress;
  char title[64];
  snprintf(title, sizeof(title), "%s -> %c", file.name.c_str(), 'A' + slotIndex);
  ProgressCtx ctx{title, -1};
  uiDrawProgress(title, 0);

  const FlashResult res = flashValidateAndWrite(file.path.c_str(), slotIndex, progressCb, &ctx);
  if (res != FlashResult::Ok) {
    char body[96];
    snprintf(body, sizeof(body), "Install failed: %s", flashResultName(res));
    showMessage("Error", body, true);
    return;
  }

  appsSaveSlotLabel(slotIndex, file.name.c_str(), file.size);
  showHome();
}

void installSelectedBin() {
  if (gSelected < 0 || gSelected >= static_cast<int>(gEntries.size())) return;
  const DirEntry& file = gEntries[gSelected];
  if (file.isDir || !fileOpsIsBin(file.name.c_str())) return;

  const size_t maxB = installMaxBytes();
  if (file.size > maxB) {
    char body[96], need[24], cap[24];
    appsFormatBytes(file.size, need, sizeof(need));
    appsFormatBytes(maxB, cap, sizeof(cap));
    snprintf(body, sizeof(body), "%s needs %s; max empty slot is %s.", file.name.c_str(), need, cap);
    showMessage("Too large", body, true);
    return;
  }

  int slot = gAssignSlot;
  if (slot < 0) {
    slot = appsBestFitSlot(file.size);
    if (slot < 0) {
      showMessage("Won't fit", "No empty slot is large enough. Clear one first.", true);
      return;
    }
  }
  installFileToSlot(gSelected, slot);
}

void openImage(const char* path) {
  uiDrawProgress("Opening image...", 30);
  gScreen = Screen::ImageView;
  canvasReleaseCapture();
  gImagePath[0] = 0;
  if (!imageDrawFile(path)) {
    showMessage("Image", "Could not open image.", true);
    return;
  }
  snprintf(gImagePath, sizeof(gImagePath), "%s", path ? path : "");
  canvasCaptureFrame();
  uiDrawImageViewHint(false);
}

void redrawTextEdit(bool scrub = false) {
  canvasDisarmLocalClean();
  canvasSetHoldCleanRefresh(true);
  gScreen = Screen::TextEdit;
  uiDrawTextEdit(gTextTitle, gTextBuf, gOskSymbols, gOskShift, gTextMode, scrub);
}

void redrawTextEditFieldOnly() {
  gScreen = Screen::TextEdit;
  uiRedrawTextEditField(gTextBuf, gTextMode);
}

void openTextEditor(TextEditMode mode, const char* title, const char* initial, const char* path) {
  gTextMode = mode;
  gOskShift = false;
  gOskSymbols = false;
  gTextLen = 0;
  gTextBuf[0] = 0;
  gTextPath[0] = 0;
  snprintf(gTextTitle, sizeof(gTextTitle), "%s", title ? title : "Edit");
  if (path) snprintf(gTextPath, sizeof(gTextPath), "%s", path);
  if (initial) {
    snprintf(gTextBuf, sizeof(gTextBuf), "%s", initial);
    gTextLen = strlen(gTextBuf);
  }
  redrawTextEdit(true);
}

void openTextFile(const char* path, const char* name) {
  size_t n = 0;
  if (!fileOpsLoadText(path, gTextBuf, sizeof(gTextBuf), &n)) {
    showMessage("Too large", "Text files over ~6 KB cannot be edited here.", true);
    return;
  }
  gTextLen = n;
  gTextMode = TextEditMode::EditFile;
  gOskShift = false;
  gOskSymbols = false;
  snprintf(gTextPath, sizeof(gTextPath), "%s", path ? path : "");
  snprintf(gTextTitle, sizeof(gTextTitle), "%s", name ? name : "Edit");
  redrawTextEdit(true);
}

void textAppend(char ch) {
  size_t limit = sizeof(gTextBuf) - 1;
  if (gTextMode == TextEditMode::ApPassword || gTextMode == TextEditMode::StaPassword) limit = 63;
  else if (gTextMode == TextEditMode::WebUrl) limit = 500;
  if (gTextLen >= limit) return;
  gTextBuf[gTextLen++] = ch;
  gTextBuf[gTextLen] = 0;
}

void textBackspace() {
  if (gTextLen == 0) return;
  gTextBuf[--gTextLen] = 0;
}

bool nameLooksSafe(const char* name) {
  if (!name || !name[0]) return false;
  if (strchr(name, '/') || strchr(name, '\\')) return false;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
  return true;
}

void redrawWifi();
void stopWifiSession();
void showShade();
void startWifiHotspot(bool openDetails);

void applyApPassword(bool save) {
  if (!save) {
    redrawWifi();
    return;
  }
  if (!wifiSetApPassword(gTextBuf)) {
    showWifiNotice("Password", "Use 8 to 63 characters.");
    return;
  }
  if (wifiIsHotspot()) {
    stopWifiSession();
    startWifiHotspot(true);
    return;
  }
  redrawWifi();
}

void redrawNet() {
  gScreen = Screen::Net;
  refreshSlots();
  uiDrawNet(gSpace, wifiGetStatus(), gNets, gNetCount);
}

void showNetNotice(const char* title, const char* body) {
  canvasSetHoldCleanRefresh(false);
  gMessageReturnExplorer = false;
  gMessageReturnWifi = false;
  gMessageReturnNet = true;
  gScreen = Screen::Message;
  uiDrawMessage(title, body);
}

void joinNetwork(int index) {
  if (index < 0 || index >= gNetCount) return;
  snprintf(gJoinSsid, sizeof(gJoinSsid), "%s", gNets[index].ssid);
  if (!gNets[index].open) {
    char saved[65];
    if (wifiLookupPassword(gJoinSsid, saved, sizeof(saved))) {
      uiDrawProgress("Joining...", 60);
      fetch_disconnect();
      if (wifiJoin(gJoinSsid, saved)) {
        redrawNet();
        return;
      }
      openTextEditor(TextEditMode::StaPassword, "Wi-Fi password", saved, nullptr);
      return;
    }
    openTextEditor(TextEditMode::StaPassword, "Wi-Fi password", "", nullptr);
    return;
  }
  uiDrawProgress("Joining...", 60);
  fetch_disconnect();
  if (!wifiJoin(gJoinSsid, "")) {
    showNetNotice("Wi-Fi", "Could not join that network.");
    return;
  }
  redrawNet();
}

void showNetworks() {
  if (wifiIsHotspot()) stopWifiSession();
  gScreen = Screen::Net;
  uiDrawProgress("Scanning...", 40);
  gNetCount = wifiScan(gNets, kNetMax);
  redrawNet();
}

int gWebField = -1;

void openWebAddress() {
  openTextEditor(TextEditMode::WebUrl, "Web address", webCurrentUrl(), nullptr);
}

void openWebField(int index) {
  gWebField = index;
  openTextEditor(TextEditMode::WebField, "Search", webFieldValue(index), nullptr);
}

void loadWeb(const char* url, bool record) {
  if (!wifiIsStation()) {
    showNetNotice("Web", "Join a Wi-Fi network first.");
    return;
  }
  gScreen = Screen::Web;
  uiDrawProgress("Loading...", 50);
  webLoad(url, record);
  webDraw();
  gScreen = Screen::Web;
}

void finishTextEdit(bool save) {
  canvasSetHoldCleanRefresh(false);
  canvasDisarmLocalClean();
  canvasRequestCleanRefresh();

  if (gTextMode == TextEditMode::ApPassword) {
    applyApPassword(save);
    return;
  }

  if (gTextMode == TextEditMode::StaPassword) {
    if (!save) {
      redrawNet();
      return;
    }
    uiDrawProgress("Joining...", 60);
    fetch_disconnect();
    if (!wifiJoin(gJoinSsid, gTextBuf)) {
      showNetNotice("Wi-Fi", "Could not join that network.");
      return;
    }
    redrawNet();
    return;
  }

  if (gTextMode == TextEditMode::WebUrl) {
    if (!save) {
      gScreen = Screen::Web;
      webDraw();
      return;
    }
    loadWeb(gTextBuf, true);
    return;
  }

  if (gTextMode == TextEditMode::WebField) {
    if (save) webSetField(gWebField, gTextBuf);
    gScreen = Screen::Web;
    webDraw();
    return;
  }

  if (!save) {
    redrawExplorer();
    return;
  }

  if (gTextMode == TextEditMode::EditFile) {
    if (!fileOpsSaveText(gTextPath, gTextBuf, gTextLen)) {
      showMessage("Save failed", "Could not write the file.", true);
      return;
    }
    buildExplorerList();
    redrawExplorer();
    return;
  }

  if (!nameLooksSafe(gTextBuf)) {
    showMessage("Bad name", "Name cannot be empty or contain /.", true);
    return;
  }

  char dest[kFilePathMax];
  if (!fileOpsJoin(gPath, gTextBuf, dest, sizeof(dest))) {
    showMessage("Error", "Path too long.", true);
    return;
  }

  if (gTextMode == TextEditMode::NewFolder) {
    if (fileOpsExists(dest)) {
      showMessage("Exists", "That name is already used.", true);
      return;
    }
    if (!fileOpsMkdir(dest)) {
      showMessage("Error", "Could not create folder.", true);
      return;
    }
    buildExplorerList();
    redrawExplorer();
    return;
  }

  if (gTextMode == TextEditMode::NewFile) {
    if (fileOpsExists(dest)) {
      showMessage("Exists", "That name is already used.", true);
      return;
    }
    if (!fileOpsSaveText(dest, "", 0)) {
      showMessage("Error", "Could not create file.", true);
      return;
    }
    // Open the new file for editing.
    snprintf(gTextPath, sizeof(gTextPath), "%s", dest);
    gTextMode = TextEditMode::EditFile;
    gTextLen = 0;
    gTextBuf[0] = 0;
    gOskShift = false;
    gOskSymbols = false;
    char base[96];
    if (!fileOpsBasename(dest, base, sizeof(base))) snprintf(base, sizeof(base), "Edit");
    snprintf(gTextTitle, sizeof(gTextTitle), "%s", base);
    redrawTextEdit(true);
    return;
  }

  // Rename
  if (!gTextPath[0] || !fileOpsExists(gTextPath)) {
    showMessage("Error", "Source missing.", true);
    return;
  }
  if (strcmp(gTextPath, dest) == 0) {
    redrawExplorer();
    return;
  }
  if (fileOpsExists(dest)) {
    showMessage("Exists", "That name is already used.", true);
    return;
  }
  if (!fileOpsRename(gTextPath, dest)) {
    showMessage("Error", "Rename failed.", true);
    return;
  }
  buildExplorerList();
  clearExplorerSelection();
  redrawExplorer();
}

void openSelected() {
  if (gSelected < 0 || gSelected >= static_cast<int>(gEntries.size())) return;
  const DirEntry& e = gEntries[gSelected];
  if (e.isDir) {
    enterDir(e.path.c_str());
    return;
  }
  if (gExplorerMode == ExplorerMode::Install && fileOpsIsBin(e.name.c_str())) {
    installSelectedBin();
    return;
  }
  if (fileOpsIsImage(e.name.c_str())) {
    openImage(e.path.c_str());
    return;
  }
  if (fileOpsIsText(e.name.c_str())) {
    openTextFile(e.path.c_str(), e.name.c_str());
    return;
  }
  if (fileOpsIsBin(e.name.c_str())) {
    showMessage("Firmware", "Open an empty slot to install .bin files.", true);
    return;
  }
  showMessage("Unsupported", "Cannot open this file type.", true);
}

void clipboardFromSelection(bool cut) {
  const auto paths = selectedExplorerPaths();
  if (paths.empty()) {
    showMessage("Select first", "Select one or more items, then Copy or Cut.", true);
    return;
  }
  fileClipboardSet(paths, cut);
  clearExplorerSelection();
  gSheetOpen = false;
  redrawExplorer();
}

void doPaste() {
  char err[48];
  if (!fileClipboardPaste(gPath, err, sizeof(err))) {
    // A storage error can happen after part of a batch was written or moved.
    // Rescan so the view always matches the SD card's actual state.
    buildExplorerList();
    showMessage("Paste", err, true);
    return;
  }
  clearExplorerSelection();
  buildExplorerList();
  redrawExplorer();
}

void askDelete() {
  const auto paths = selectedExplorerPaths();
  if (paths.empty()) {
    showMessage("Select first", "Select one or more items, then Delete.", true);
    return;
  }
  for (const auto& path : paths) {
    if (fileOpsIsDir(path.c_str()) && !fileOpsDirEmpty(path.c_str())) {
      showMessage("Not empty", "Only empty folders can be deleted.", true);
      return;
    }
  }
  gConfirmAction = ConfirmAction::Delete;
  gConfirmPaths = paths;
  gScreen = Screen::Confirm;
  char body[96];
  if (paths.size() == 1) {
    char base[64];
    if (!fileOpsBasename(paths.front().c_str(), base, sizeof(base))) snprintf(base, sizeof(base), "item");
    snprintf(body, sizeof(body), "Delete %s?", base);
  } else {
    snprintf(body, sizeof(body), "Delete %u selected items?", static_cast<unsigned>(paths.size()));
  }
  uiDrawConfirm("Delete", body);
}

void confirmYes() {
  if (gConfirmAction == ConfirmAction::Delete) {
    for (const auto& path : gConfirmPaths) {
      if (!fileOpsRemove(path.c_str())) {
        clearExplorerSelection();
        buildExplorerList();
        showMessage("Error", "Delete failed; remaining items were kept.", true);
        gConfirmAction = ConfirmAction::None;
        gConfirmPaths.clear();
        return;
      }
      auto& clip = fileClipboard();
      clip.paths.erase(std::remove(clip.paths.begin(), clip.paths.end(), path), clip.paths.end());
      if (clip.paths.empty()) fileClipboardClear();
    }
    clearExplorerSelection();
    buildExplorerList();
  }
  gConfirmAction = ConfirmAction::None;
  gConfirmPaths.clear();
  redrawExplorer();
}

void bootExisting(int slotIndex) {
  if (slotIndex < 0 || slotIndex >= kSlotCount) return;
  refreshSlots();
  if (!gSlots[slotIndex].occupied) {
    showExplorer(ExplorerMode::Install, slotIndex);
    return;
  }
  gScreen = Screen::Progress;
  uiDrawProgress("Booting...", 100);
  delay(200);
  const FlashResult res = bootSlotPendingVerify(slotIndex);
  if (res != FlashResult::Ok) {
    char body[96];
    snprintf(body, sizeof(body), "Boot failed: %s", flashResultName(res));
    showMessage("Error", body, false);
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
    char body[96];
    snprintf(body, sizeof(body), "Clear failed: %s", flashResultName(res));
    showMessage("Error", body, false);
    return;
  }
  appsClearSlotLabel(slotIndex);
  showHome();
}

void redrawWifi() {
  gScreen = Screen::Wifi;
  const WifiStatus st = wifiGetStatus();
  const bool active = wifiIsHotspot();
  const char* status = !active ? "Off" : "Transfer on";
  char detail[96];
  detail[0] = 0;
  const char* msg = wifiServerLastMessage();
  if (msg && msg[0]) {
    snprintf(detail, sizeof(detail), "%s", msg);
  } else if (st.detail[0]) {
    snprintf(detail, sizeof(detail), "%s", st.detail);
  }
  uiDrawWifi(gSpace, status, st.ssid, st.url, detail, active);
}

void stopWifiSession() {
  fetch_disconnect();
  wifiServerStop();
  wifiStop();
}

void startWifiHotspot(bool openDetails) {
  uiDrawProgress("Starting transfer...", 10);
  if (!wifiStartSoftAp() || !wifiServerStart()) {
    stopWifiSession();
    showMessage("Transfer failed", "Could not start file transfer.", false);
    return;
  }
  wifiServerClearMessage();
  if (openDetails) redrawWifi();
  else showShade();
}

void showWifi() {
  redrawWifi();
}

void showGps() {
  if (gScreen == Screen::Shade) gGpsReturn = gShadeReturn;
  else if (gScreen != Screen::Gps) gGpsReturn = gScreen;
  gpsStart(true);
  gScreen = Screen::Gps;
  refreshSlots();
  uiDrawGps();
}

void resumeGps() {
  gpsStart(false);
  gScreen = Screen::Gps;
  refreshSlots();
  uiDrawGps();
}

void leaveGps() {
  gpsLeave();
  const Screen back = gGpsReturn;
  gGpsReturn = Screen::Home;
  switch (back) {
    case Screen::Explorer:
      redrawExplorer();
      break;
    case Screen::TextEdit:
      redrawTextEdit(true);
      break;
    case Screen::Settings:
      gScreen = Screen::Settings;
      refreshSlots();
      uiDrawSettings(gSpace);
      break;
    case Screen::Hardware:
      gScreen = Screen::Hardware;
      refreshSlots();
      uiDrawHardware(gSpace);
      break;
    case Screen::Wifi:
      redrawWifi();
      break;
    case Screen::Net:
      redrawNet();
      break;
    case Screen::Web:
      gScreen = Screen::Web;
      webDraw();
      break;
    case Screen::Home:
    default:
      showHome();
      break;
  }
}

void showShade() {
  // Remember the underlying screen so Close returns there (not always Home).
  // Settings, Wi-Fi, and file transfer are opened from the shade. They must
  // not become the return target, or Back lands on them again.
  if (gScreen == Screen::Gps) gpsPause();
  if (gScreen == Screen::Home || gScreen == Screen::Explorer || gScreen == Screen::TextEdit ||
      gScreen == Screen::Hardware || gScreen == Screen::Web || gScreen == Screen::Gps) {
    gShadeReturn = gScreen;
  }
  canvasSetHoldCleanRefresh(false);
  canvasDisarmLocalClean();
  gScreen = Screen::Shade;
  refreshSlots();
  uiDrawShade(gSpace);
}

void closeShade() {
  canvasSetHoldCleanRefresh(false);
  const Screen back = gShadeReturn;
  gShadeReturn = Screen::Home;
  switch (back) {
    case Screen::Explorer:
      redrawExplorer();
      break;
    case Screen::TextEdit:
      redrawTextEdit(true);
      break;
    case Screen::Settings:
      gScreen = Screen::Settings;
      refreshSlots();
      uiDrawSettings(gSpace);
      break;
    case Screen::Hardware:
      gScreen = Screen::Hardware;
      refreshSlots();
      uiDrawHardware(gSpace);
      break;
    case Screen::Wifi:
      redrawWifi();
      break;
    case Screen::Net:
      redrawNet();
      break;
    case Screen::Web:
      gScreen = Screen::Web;
      webDraw();
      break;
    case Screen::Gps:
      resumeGps();
      break;
    case Screen::Home:
    default:
      showHome();
      break;
  }
}

void handleShadeHit(const UiHit& hit) {
  uiAcknowledgePress(hit);
  switch (hit.kind) {
    case UiHit::Kind::CloseShade:
      closeShade();
      break;
    case UiHit::Kind::BrightnessMinus:
      boardSetBrightness(boardBrightness() - 5);
      uiRedrawShadeControls(gSpace);
      break;
    case UiHit::Kind::BrightnessPlus:
      boardSetBrightness(boardBrightness() + 5);
      uiRedrawShadeControls(gSpace);
      break;
    case UiHit::Kind::BrightnessSlider:
      if (hit.value >= 0) {
        boardSetBrightness(hit.value);
        uiRedrawShadeControls(gSpace);
      }
      break;
    case UiHit::Kind::LightToggle:
      boardSetFrontlightOn(!boardFrontlightOn());
      uiRedrawShadeControls(gSpace);
      break;
    case UiHit::Kind::ScrubNow: {
      // A multi-pass black/white recovery is reserved for stable external
      // power; on battery a single clean redraw avoids a sustained rail load.
      const Screen back = gShadeReturn;
      gShadeReturn = Screen::Home;
      canvasSetHoldCleanRefresh(false);
      if (boardPower().plugged) canvasNuclearFlash();
      switch (back) {
        case Screen::Explorer:
          redrawExplorer();
          break;
        case Screen::TextEdit:
          redrawTextEdit(true);
          break;
        case Screen::Settings:
          gScreen = Screen::Settings;
          refreshSlots();
          uiDrawSettings(gSpace);
          break;
        case Screen::Hardware:
          gScreen = Screen::Hardware;
          refreshSlots();
          uiDrawHardware(gSpace);
          break;
        case Screen::Wifi:
          redrawWifi();
          break;
        case Screen::Net:
          redrawNet();
          break;
        case Screen::Web:
          gScreen = Screen::Web;
          webDraw();
          break;
        case Screen::Gps:
          resumeGps();
          break;
        case Screen::Home:
        default:
          showHome();
          break;
      }
      canvasPresent(EInkDisplay::HALF_REFRESH);
      break;
    }
    case UiHit::Kind::Settings:
      gScreen = Screen::Settings;
      refreshSlots();
      uiDrawSettings(gSpace);
      break;
    case UiHit::Kind::OpenGps:
      if (gShadeReturn == Screen::Gps) closeShade();
      else showGps();
      break;
    case UiHit::Kind::Wifi:
      if (wifiIsHotspot()) {
        stopWifiSession();
        showShade();
      } else {
        startWifiHotspot(false);
      }
      break;
    case UiHit::Kind::Net:
      showNetworks();
      break;
    default:
      break;
  }
}

void handleExplorerLongPress(int index) {
  if (index < 0 || index >= static_cast<int>(gEntries.size())) return;
  if (!gMultiSelect) {
    clearExplorerSelection();
    gMultiSelect = true;
  }
  toggleExplorerEntry(index);
  gSheetOpen = false;
  redrawExplorer(true);
}

void handleExplorerHit(const UiHit& hit) {
  uiAcknowledgePress(hit);
  const int visible = uiExplorerVisibleRows();
  switch (hit.kind) {
    case UiHit::Kind::Back:
      if (gMultiSelect) {
        clearExplorerSelection();
        redrawExplorer(true);
      } else {
        showHome();
      }
      break;
    case UiHit::Kind::GoUp:
      goUpDir();
      break;
    case UiHit::Kind::ScrollUp:
      if (gScroll > 0) {
        gScroll--;
        redrawExplorer(true);
      }
      break;
    case UiHit::Kind::ScrollDown:
      if (gScroll + visible < static_cast<int>(gEntries.size())) {
        gScroll++;
        redrawExplorer(true);
      }
      break;
    case UiHit::Kind::SelectEntry:
      if (gMultiSelect) {
        toggleExplorerEntry(hit.index);
        gSheetOpen = false;
        redrawExplorer(true);
      } else {
        // Single tap opens (dir / image / text / install). Long-press for multi-select.
        selectOnlyExplorerEntry(hit.index);
        gSheetOpen = false;
        openSelected();
      }
      break;
    case UiHit::Kind::ExplorerOpen:
      if (gSelectedEntries.size() == 1) openSelected();
      break;
    case UiHit::Kind::ExplorerMore:
      gSheetOpen = !gSheetOpen;
      redrawExplorer();
      break;
    case UiHit::Kind::ExplorerSheetDismiss:
      gSheetOpen = false;
      redrawExplorer();
      break;
    case UiHit::Kind::ExplorerCopy:
      gSheetOpen = false;
      clipboardFromSelection(false);
      break;
    case UiHit::Kind::ExplorerCut:
      gSheetOpen = false;
      clipboardFromSelection(true);
      break;
    case UiHit::Kind::ExplorerPaste:
      gSheetOpen = false;
      doPaste();
      break;
    case UiHit::Kind::ExplorerRename: {
      gSheetOpen = false;
      if (gSelectedEntries.size() != 1 || gSelected < 0 ||
          gSelected >= static_cast<int>(gEntries.size())) {
        showMessage("Select one", "Rename works with one selected item.", true);
        break;
      }
      const DirEntry& e = gEntries[gSelected];
      openTextEditor(TextEditMode::Rename, "Rename", e.name.c_str(), e.path.c_str());
      break;
    }
    case UiHit::Kind::ExplorerDelete:
      gSheetOpen = false;
      askDelete();
      break;
    case UiHit::Kind::ExplorerNew:
      gSheetOpen = false;
      openTextEditor(TextEditMode::NewFolder, "New folder", "New Folder", nullptr);
      break;
    case UiHit::Kind::ExplorerNewFile:
      gSheetOpen = false;
      openTextEditor(TextEditMode::NewFile, "New file", "note.txt", nullptr);
      break;
    default:
      break;
  }
}

// Apply one OSK hit. Returns true if the screen left TextEdit (Cancel/Done).
// Sets *layoutChanged when the keyboard glyphs must be redrawn; *textChanged
// when only the field content changed.
bool applyTextHit(const UiHit& hit, bool& textChanged, bool& layoutChanged) {
  if (hit.kind == UiHit::Kind::KeyDone || hit.kind == UiHit::Kind::KeyCancel) uiAcknowledgePress(hit);
  else if (hit.kind != UiHit::Kind::KeyShift && hit.kind != UiHit::Kind::KeySymbols) uiFlashKey(hit);
  switch (hit.kind) {
    case UiHit::Kind::KeyCancel:
      finishTextEdit(false);
      return true;
    case UiHit::Kind::KeyDone:
      finishTextEdit(true);
      return true;
    case UiHit::Kind::KeyChar: {
      // Stay on the 123 page until ABC is tapped (Android-style). Shift is
      // still one-shot for a single capital.
      const bool shiftWasOn = gOskShift;
      if (hit.value > 0 && hit.value < 128) textAppend(static_cast<char>(hit.value));
      gOskShift = false;
      if (shiftWasOn && !gOskSymbols) layoutChanged = true;
      else textChanged = true;
      break;
    }
    case UiHit::Kind::KeySpace:
      textAppend(' ');
      textChanged = true;
      break;
    case UiHit::Kind::KeyBackspace:
      textBackspace();
      textChanged = true;
      break;
    case UiHit::Kind::KeyShift:
      if (gOskSymbols) {
        gOskSymbols = false;
        gOskShift = false;
      } else {
        gOskShift = !gOskShift;
      }
      layoutChanged = true;
      break;
    case UiHit::Kind::KeySymbols:
      gOskSymbols = !gOskSymbols;
      gOskShift = false;
      layoutChanged = true;
      break;
    default:
      break;
  }
  return false;
}

void handleTextHit(const UiHit& hit) {
  bool textChanged = false;
  bool layoutChanged = false;
  if (applyTextHit(hit, textChanged, layoutChanged)) return;

  // Drain any taps that arrived while the previous refresh was blocking so a
  // fast typist gets one present for the whole burst.
  for (;;) {
    int x = 0, y = 0;
    if (!boardPollTouch(x, y)) break;
    const UiHit next = uiHitTextEdit(x, y, gOskSymbols, gOskShift);
    if (next.kind == UiHit::Kind::OpenShade) {
      showShade();
      return;
    }
    if (applyTextHit(next, textChanged, layoutChanged)) return;
  }

  if (layoutChanged) redrawTextEdit(false);
  else if (textChanged) redrawTextEditFieldOnly();
}

void handleTouch(int x, int y) {
  if (gScreen == Screen::Message) {
    uiAcknowledgePress(uiHitMessage(x, y));
    const bool backToWifi = gMessageReturnWifi;
    const bool backToNet = gMessageReturnNet;
    gMessageReturnWifi = false;
    gMessageReturnNet = false;
    if (backToWifi) redrawWifi();
    else if (backToNet) redrawNet();
    else if (gMessageReturnExplorer) redrawExplorer();
    else showHome();
    return;
  }

  if (gScreen == Screen::Progress) return;

  if (gScreen == Screen::Confirm) {
    const UiHit hit = uiHitConfirm(x, y);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::ConfirmYes) confirmYes();
    else if (hit.kind == UiHit::Kind::ConfirmNo) {
      gConfirmAction = ConfirmAction::None;
      gConfirmPaths.clear();
      redrawExplorer();
    }
    return;
  }

  if (gScreen == Screen::ImageView) {
    const UiHit hit = uiHitImageView(x, y);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::SetSleep) {
      if (sleepSaveCapturedFrame(gImagePath)) uiDrawImageViewHint(true);
      else {
        canvasReleaseCapture();
        showMessage("Sleep", "Could not save the screensaver.", true);
      }
      return;
    }
    esp_task_wdt_reset();
    canvasCancelPendingClean();
    canvasReleaseCapture();
    redrawExplorer();
    return;
  }

  if (gScreen == Screen::TextEdit) {
    const UiHit hit = uiHitTextEdit(x, y, gOskSymbols, gOskShift);
    if (hit.kind == UiHit::Kind::OpenShade) {
      showShade();
      return;
    }
    handleTextHit(hit);
    return;
  }

  if (gScreen == Screen::Shade) {
    handleShadeHit(uiHitShade(x, y));
    return;
  }

  if (gScreen == Screen::Settings) {
    const UiHit hit = uiHitSettings(x, y);
    auto daysInMonth = [](int y, int m) -> int {
      static const int d[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0))) return 29;
      if (m < 1 || m > 12) return 31;
      return d[m];
    };
    auto adjustDate = [&](int dy, int dm, int dd) {
      BoardClockInfo c = boardClock();
      int y = c.valid ? c.year : 2026;
      int mo = c.valid ? c.month : 1;
      int d = c.valid ? c.day : 1;
      int h = c.valid ? c.hour : 12;
      int mi = c.valid ? c.minute : 0;
      y += dy;
      mo += dm;
      d += dd;
      while (mo < 1) {
        mo += 12;
        --y;
      }
      while (mo > 12) {
        mo -= 12;
        ++y;
      }
      if (y < 2000) y = 2000;
      if (y > 2099) y = 2099;
      const int dim = daysInMonth(y, mo);
      if (d < 1) d = 1;
      if (d > dim) d = dim;
      boardSetClock(static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                    static_cast<uint8_t>(h), static_cast<uint8_t>(mi));
    };
    auto adjustTime = [&](int deltaMin) {
      if (!boardAdjustClockMinutes(deltaMin)) {
        BoardClockInfo c = boardClock();
        uint16_t y = c.valid ? c.year : 2026;
        uint8_t mo = c.valid ? c.month : 1;
        uint8_t d = c.valid ? c.day : 1;
        uint8_t h = c.valid ? c.hour : 12;
        uint8_t mi = c.valid ? c.minute : 0;
        int total = static_cast<int>(h) * 60 + static_cast<int>(mi) + deltaMin;
        while (total < 0) total += 24 * 60;
        total %= 24 * 60;
        boardSetClock(y, mo, d, static_cast<uint8_t>(total / 60), static_cast<uint8_t>(total % 60));
      }
    };

    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::Back) {
      showShade();
    } else if (hit.kind == UiHit::Kind::Hardware) {
      gScreen = Screen::Hardware;
      refreshSlots();
      uiDrawHardware(gSpace);
    } else if (hit.kind == UiHit::Kind::Wifi) {
      showWifi();
    } else if (hit.kind == UiHit::Kind::OpenShade) {
      showShade();
    } else if (hit.kind == UiHit::Kind::PowerOff) {
      enterSleepWithScreensaver();
    } else if (hit.kind == UiHit::Kind::SleepAfterMinus) {
      boardSetSleepAfterMin(boardSleepAfterMin() <= 0 ? 0 : boardSleepAfterMin() - 1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::SleepAfterPlus) {
      boardSetSleepAfterMin(boardSleepAfterMin() + 1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::FontSizeMinus) {
      boardSetUiTextSize(boardUiTextSize() - 1);
      canvasRequestCleanRefresh();
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::FontSizePlus) {
      boardSetUiTextSize(boardUiTextSize() + 1);
      canvasRequestCleanRefresh();
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::CleanEveryMinus) {
      boardSetCleanEvery(boardCleanEvery() - 1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::CleanEveryPlus) {
      boardSetCleanEvery(boardCleanEvery() + 1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::HourMinus) {
      adjustTime(-60);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::HourPlus) {
      adjustTime(60);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::MinuteMinus) {
      adjustTime(-1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::MinutePlus) {
      adjustTime(1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::YearMinus) {
      adjustDate(-1, 0, 0);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::YearPlus) {
      adjustDate(1, 0, 0);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::MonthMinus) {
      adjustDate(0, -1, 0);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::MonthPlus) {
      adjustDate(0, 1, 0);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::DayMinus) {
      adjustDate(0, 0, -1);
      uiDrawSettings(gSpace);
    } else if (hit.kind == UiHit::Kind::DayPlus) {
      adjustDate(0, 0, 1);
      uiDrawSettings(gSpace);
    }
    return;
  }

  if (gScreen == Screen::Hardware) {
    const UiHit hit = uiHitHardware(x, y);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::OpenShade) showShade();
    else if (hit.kind == UiHit::Kind::Back) {
      gScreen = Screen::Settings;
      refreshSlots();
      uiDrawSettings(gSpace);
    }
    return;
  }

  if (gScreen == Screen::Net) {
    const UiHit hit = uiHitNet(x, y);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::Back) {
      closeShade();
    } else if (hit.kind == UiHit::Kind::OpenShade) {
      showShade();
    } else if (hit.kind == UiHit::Kind::NetScan) {
      showNetworks();
    } else if (hit.kind == UiHit::Kind::NetDisconnect) {
      fetch_disconnect();
      wifiStop();
      redrawNet();
    } else if (hit.kind == UiHit::Kind::NetJoin) {
      joinNetwork(hit.index);
    } else if (hit.kind == UiHit::Kind::WebOpen) {
      loadWeb(webCurrentUrl(), true);
    }
    return;
  }

  if (gScreen == Screen::Web) {
    const WebHit hit = webHit(x, y);
    if (hit.w >= 8) {
      UiHit flash;
      flash.rx = hit.x;
      flash.ry = hit.y;
      flash.rw = hit.w;
      flash.rh = hit.h;
      flash.rr = 12;
      uiAcknowledgePress(flash);
    }
    if (hit.action == WebAction::Shade || hit.action == WebAction::Close) showShade();
    else if (hit.action == WebAction::Address) openWebAddress();
    else if (hit.action == WebAction::Reload) loadWeb(webCurrentUrl(), false);
    else if (hit.action == WebAction::HistBack) {
      uiDrawProgress("Loading...", 40);
      webGoBack();
      webDraw();
      gScreen = Screen::Web;
    } else if (hit.action == WebAction::HistFwd) {
      uiDrawProgress("Loading...", 40);
      webGoForward();
      webDraw();
      gScreen = Screen::Web;
    }     else if (hit.action == WebAction::Link) {
      const char* url = webLinkUrl(hit.index);
      if (url) loadWeb(url, true);
    } else if (hit.action == WebAction::Field) {
      openWebField(hit.index);
    } else if (hit.action == WebAction::Submit) {
      uiDrawProgress("Searching...", 50);
      webSubmit(hit.index);
      webDraw();
      gScreen = Screen::Web;
    }
    return;
  }

  if (gScreen == Screen::Wifi) {
    const bool active = wifiIsHotspot();
    const UiHit hit = uiHitWifi(x, y, active);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::Back) {
      closeShade();
    } else if (hit.kind == UiHit::Kind::OpenShade) {
      showShade();
    } else if (hit.kind == UiHit::Kind::WifiStartAp) {
      startWifiHotspot(true);
    } else if (hit.kind == UiHit::Kind::WifiStop) {
      stopWifiSession();
      redrawWifi();
    } else if (hit.kind == UiHit::Kind::WifiChangePass) {
      openTextEditor(TextEditMode::ApPassword, "Hotspot password", wifiApPassword(), nullptr);
    }
    return;
  }

  if (gScreen == Screen::Gps) {
    const UiHit hit = uiHitGps(x, y);
    uiAcknowledgePress(hit);
    if (hit.kind == UiHit::Kind::OpenShade) showShade();
    else if (hit.kind == UiHit::Kind::Back) leaveGps();
    else if (hit.kind == UiHit::Kind::GpsZoomIn || hit.kind == UiHit::Kind::GpsZoomOut) {
      if (hit.kind == UiHit::Kind::GpsZoomIn) gpsZoomIn();
      else gpsZoomOut();
      uiDrawGps();
    } else if (hit.kind == UiHit::Kind::GpsRecenter) {
      if (gpsRecenter()) uiDrawGps();
    }
    return;
  }

  if (gScreen == Screen::Explorer) {
    const bool canUp = !appsIsRootDir(gPath);
    const UiHit hit =
        uiHitExplorer(x, y, static_cast<int>(gEntries.size()), gScroll, canUp, gSheetOpen,
                      fileClipboard().hasItem, static_cast<int>(gSelectedEntries.size()));
    if (hit.kind == UiHit::Kind::OpenShade) {
      showShade();
      return;
    }
    handleExplorerHit(hit);
    return;
  }

  if (gScreen == Screen::Home) {
    const UiHit hit = uiHitHome(x, y);
    uiAcknowledgePress(hit);
    switch (hit.kind) {
      case UiHit::Kind::OpenShade:
        showShade();
        break;
      case UiHit::Kind::OpenFiles:
        showExplorer(ExplorerMode::Browse, -1);
        break;
      case UiHit::Kind::WebOpen:
        if (wifiIsStation()) loadWeb(webHomeUrl(), true);
        else showNetworks();
        break;
      case UiHit::Kind::BootSlot:
        refreshSlots();
        if (hit.index >= 0 && hit.index < kSlotCount && !gSlots[hit.index].occupied) {
          showExplorer(ExplorerMode::Install, hit.index);
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
        showExplorer(ExplorerMode::Install, hit.index);
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

  // Decide the wake hold before the slow panel and SD startup.
  const bool fromSleepWake = sleepWokeFromBootButton();
  bool stayAwake = sleepBootHoldKeepsAwake(600);

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
  xTaskCreate(brightnessFollowTask, "bright", 2048, nullptr, 3, nullptr);
  appsLoadSlotLabels();

  // Short tap while asleep: user may start holding during panel/SD init.
  if (!stayAwake && sleepTryAbortForBootHold(500)) {
    stayAwake = true;
  }

  if (!stayAwake) {
    if (!enterSleepWithScreensaver(/*quiet=*/true)) {
      stayAwake = true;  // aborted for wake hold during wallpaper change
    }
  }

  // Logo only on cold / power-on style boots — not when waking from sleep.
  if (!fromSleepWake) {
    uiDrawSplash();
    delay(1200);
  }
  // Clean home so the charging icons are sharp on the first frame.
  showHome(true);
  noteActivity();
}

void loop() {
  if (gScreen != Screen::Progress) {
    sdSerialPoll();
  }
  if (gScreen == Screen::Wifi || wifiIsActive()) {
    wifiPoll();
    wifiServerPoll();
    // Refresh the status line when an upload finishes.
    static char lastMsg[96] = {};
    const char* msg = wifiServerLastMessage();
    if (gScreen == Screen::Wifi && msg && msg[0] && strcmp(lastMsg, msg) != 0) {
      snprintf(lastMsg, sizeof(lastMsg), "%s", msg);
      noteActivity();
      redrawWifi();
    }
    if (wifiServerTakeApRestart()) {
      stopWifiSession();
      if (gScreen == Screen::Wifi) startWifiHotspot(true);
      else if (!wifiStartSoftAp() || !wifiServerStart()) stopWifiSession();
    }
  }

  boardInputUpdate();

  static bool bootSleepArmed = true;
  if (!boardPowerPressed()) {
    bootSleepArmed = true;
  } else if (bootSleepArmed && boardPowerHeldMs() > 700 && gScreen != Screen::Progress) {
    bootSleepArmed = false;
    stopWifiSession();
    enterSleepWithScreensaver();
  }

  // Long presses are classified and queued by the input task, so they survive
  // blocking e-ink refreshes and their release cannot become a normal tap.
  int longPressX = 0, longPressY = 0;
  while (boardPollLongPress(longPressX, longPressY)) {
    if (gScreen == Screen::Explorer && !gSheetOpen) {
      const bool canUp = !appsIsRootDir(gPath);
      const UiHit held =
          uiHitExplorer(longPressX, longPressY, static_cast<int>(gEntries.size()), gScroll, canUp, false,
                        fileClipboard().hasItem, static_cast<int>(gSelectedEntries.size()));
      if (held.kind == UiHit::Kind::SelectEntry) {
        noteActivity();
        uiAcknowledgePress(held);
        handleExplorerLongPress(held.index);
      }
    } else if (gScreen == Screen::Shade) {
      const UiHit held = uiHitShade(longPressX, longPressY);
      if (held.kind == UiHit::Kind::Wifi) {
        noteActivity();
        uiAcknowledgePress(held);
        showWifi();
      }
    }
  }

  // A map drag is any finger move on the map, including a slow one. A swipe
  // only counts if it finishes inside 700 ms, so panning uses the held point.
  static bool mapDrag = false;
  static bool mapDragMoved = false;
  static int mapOriginX = 0, mapOriginY = 0;
  static int mapX0 = 0, mapY0 = 0, mapX1 = 0, mapY1 = 0;
  static bool webDrag = false;
  static bool webDragMoved = false;
  static int webOriginY = 0;
  static int webY0 = 0, webY1 = 0;
  static bool brightTracking = false;
  static bool brightArmed = false;
  static bool brightChanged = false;
  static int brightAtPress = 0;
  static int brightX0 = 0;
  static int brightY0 = 0;
  static int brightShown = -1;
  bool ignoreTap = false;

  if (gScreen != Screen::Gps) mapDrag = false;
  if (gScreen != Screen::Web) webDrag = false;
  if (gScreen != Screen::Shade) {
    brightTracking = false;
    brightArmed = false;
    brightChanged = false;
    brightShown = -1;
    gBrightFollow = false;
  }

  int hx = 0, hy = 0;
  const bool held = boardTouchHeld(hx, hy);
  if (gScreen == Screen::Web && held) {
    if (!webDrag) {
      if (webContentContains(hx, hy)) {
        webDrag = true;
        webDragMoved = false;
        webOriginY = hy;
        webY0 = webY1 = hy;
      }
    } else {
      webY1 = hy;
      // A downward move that starts at the top is the shade pull. Leave it
      // for the swipe path. Anything else scrolls as the finger moves.
      const bool shadePull = webOriginY < 120 && hy > webOriginY;
      const int dy = hy - webY0;
      if (!shadePull && abs(dy) >= 12) {
        webScrollBy(dy);
        webY0 = hy;
        webDragMoved = true;
        webDraw(WebPaint::Follow);
      }
    }
  }
  if (gScreen == Screen::Gps && held) {
    if (!mapDrag) {
      if (uiGpsMapContains(hx, hy)) {
        mapDrag = true;
        mapDragMoved = false;
        mapOriginX = hx;
        mapOriginY = hy;
        mapX0 = mapX1 = hx;
        mapY0 = mapY1 = hy;
      }
    } else {
      mapX1 = hx;
      mapY1 = hy;
      // A downward move that starts at the top is the shade pull.
      const bool shadePull = mapOriginY < 120 && hy > mapOriginY;
      const int dx = hx - mapX0;
      const int dy = hy - mapY0;
      if (!shadePull && (abs(dx) >= 12 || abs(dy) >= 12)) {
        if (gpsPan(dx, dy)) uiRedrawGpsMap();
        mapX0 = hx;
        mapY0 = hy;
        mapDragMoved = true;
      }
    }
  }
  if (gScreen == Screen::Shade && held) {
    noteActivity();
    if (!brightTracking) {
      brightTracking = true;
      int bx = 0, by = 0, bw = 0, bh = 0;
      uiShadeBrightnessTrack(bx, by, bw, bh);
      // The drawn track is inset in the stepper row. The whole row between
      // the - and + buttons starts the gesture.
      brightArmed = boardHasFrontlight() && hy >= by - 16 && hy <= by + bh + 16 && hx >= bx &&
                    hx <= bx + bw;
      brightAtPress = boardBrightness();
      brightShown = brightAtPress;
      brightChanged = false;
      brightX0 = hx;
      brightY0 = hy;
    }
    if (brightArmed) {
      const int dx = hx - brightX0;
      const int dy = hy - brightY0;
      // A pull up to close the shade crosses the bar. Put the lamp back and
      // leave the level for the close gesture.
      if (dy < -48 && abs(dy) > abs(dx)) {
        gBrightFollow = false;
        if (brightChanged) boardPreviewBrightness(brightAtPress);
        brightArmed = false;
        brightChanged = false;
        canvasSetHoldCleanRefresh(false);
      } else {
        canvasSetHoldCleanRefresh(true);
        gBrightFollow = true;
        const int pct = uiBrightnessFromTouchX(hx);
        if (pct != boardBrightness()) {
          boardPreviewBrightness(pct);
          brightChanged = true;
        }
        // The bar and the number chase the lamp. The refresh may lag; the
        // follow task keeps the lamp itself on the finger while it runs.
        const int drawAt = boardBrightness();
        if (drawAt != brightShown) {
          uiRedrawShadeControls(gSpace);
          brightShown = drawAt;
        }
      }
    }
  }

  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool swipePolled = false;
  if (gScreen == Screen::Gps && mapDrag && !held) {
    swipePolled = true;
    boardPollSwipe(x0, y0, x1, y1);
    const int dx = mapX1 - mapOriginX;
    const int dy = mapY1 - mapOriginY;
    mapDrag = false;
    const bool pullDown = mapOriginY < 120 && dy > 80 && abs(dy) > abs(dx);
    if (pullDown) {
      showShade();
      return;
    }
    if (mapDragMoved) ignoreTap = true;
  }

  if (!swipePolled && boardPollSwipe(x0, y0, x1, y1)) {
    noteActivity();
    const int dy = y1 - y0;
    const int dx = x1 - x0;
    const bool fromTop = y0 < 120;
    const bool pullDown = fromTop && dy > 80 && abs(dy) > abs(dx);
    const bool canShade = gScreen == Screen::Home || gScreen == Screen::Explorer ||
                          gScreen == Screen::TextEdit || gScreen == Screen::Settings ||
                          gScreen == Screen::Hardware || gScreen == Screen::Wifi ||
                          gScreen == Screen::Net || gScreen == Screen::Web ||
                          gScreen == Screen::Gps;
    if (canShade && pullDown) {
      showShade();
      return;
    }
    if (gScreen == Screen::Shade && dy < -80 && abs(dy) > abs(dx)) {
      gBrightFollow = false;
      if (brightChanged) boardSetBrightness(brightAtPress);
      brightTracking = false;
      brightArmed = false;
      brightChanged = false;
      brightShown = -1;
      canvasSetHoldCleanRefresh(false);
      closeShade();
      return;
    }
    if (gScreen == Screen::Explorer && !gSheetOpen && abs(dy) > abs(dx) && abs(dy) > 40) {
      const int visible = uiExplorerVisibleRows();
      const int maxScroll =
          std::max(0, static_cast<int>(gEntries.size()) - visible);
      // Finger up → content moves up → scroll down (see more below)
      const int steps = std::max(1, abs(dy) / uiExplorerRowHeight());
      if (dy < 0) gScroll = std::min(maxScroll, gScroll + steps);
      else gScroll = std::max(0, gScroll - steps);
      redrawExplorer(true);
      return;
    }
  }

  if (gScreen == Screen::Shade && brightTracking && !held) {
    gBrightFollow = false;
    brightTracking = false;
    brightArmed = false;
    canvasSetHoldCleanRefresh(false);
    if (brightChanged) {
      boardCommitBrightness();
      if (boardBrightness() != brightShown) {
        uiRedrawShadeControls(gSpace);
        brightShown = boardBrightness();
      }
      ignoreTap = true;
      brightChanged = false;
    }
  } else if (gScreen == Screen::Shade && !brightArmed) {
    canvasSetHoldCleanRefresh(false);
  }

  if (gScreen == Screen::Web && webDrag && !held) {
    const int dy = webY1 - webOriginY;
    webDrag = false;
    if (webDragMoved || abs(dy) >= 24) ignoreTap = true;
  }

  int x = 0, y = 0;
  if (ignoreTap) {
    boardPollTouch(x, y);
  } else if (boardPollTouch(x, y)) {
    noteActivity();
    Serial.printf("tap %d,%d screen=%d\n", x, y, static_cast<int>(gScreen));
    handleTouch(x, y);
  }

  if (gScreen == Screen::Gps && gpsActive() && !mapDrag) {
    const GpsPoll polled = gpsPoll();
    static uint32_t lastGpsDraw = 0;
    const uint32_t now = millis();
    if (lastGpsDraw == 0) lastGpsDraw = now;
    if (polled.fixChanged || (polled.sentence && now - lastGpsDraw > 10000)) {
      const bool mapMoved = gpsMapFollowMoved();
      uiRedrawGpsStatus();
      if (mapMoved) uiRedrawGpsMap();
      lastGpsDraw = now;
    }
  }

  canvasServiceRefresh();
  maybeIdleScrubHome();

  if (gScreen != Screen::ImageView && boardPowerConnectionChanged()) {
    const bool grabber = gScreen == Screen::Home || gScreen == Screen::Explorer ||
                         gScreen == Screen::Settings || gScreen == Screen::Hardware ||
                         gScreen == Screen::Wifi || gScreen == Screen::Net ||
                         gScreen == Screen::Web || gScreen == Screen::TextEdit ||
                         gScreen == Screen::Gps;
    if (gScreen != Screen::Progress) canvasRequestCleanRefresh();
    uiRedrawStatusBar(gSpace, grabber);
  }

  if (gScreen == Screen::Home) {
    const BoardClockInfo c = boardClock();
    if (c.valid && c.minute != gLastClockMinute) {
      gLastClockMinute = c.minute;
      refreshSlots();
      uiRedrawHomeStatus(gSpace);
    }
  }

  const int sleepMin = boardSleepAfterMin();
  const uint32_t lastActive = gLastActiveMs > gSdHostActiveMs ? gLastActiveMs : gSdHostActiveMs;
  if (sleepMin > 0 && gScreen != Screen::Progress && gScreen != Screen::Wifi &&
      (millis() - lastActive) > static_cast<uint32_t>(sleepMin) * 60u * 1000u) {
    stopWifiSession();
    enterSleepWithScreensaver();
  }
  delay(20);
}
