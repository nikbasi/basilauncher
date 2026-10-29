// Draws each launcher screen with the real UI code and writes it as a
// portrait 540x960 greyscale image. Run through run.sh.
//
// canvas.cpp is compiled into this file so the preview can read its grey
// planes; everything else links as on the device.

#include "../../src/canvas.cpp"

#include "apps_scan.h"
#include "ui.h"
#include "wifi_session.h"

#include <cstdio>
#include <vector>

namespace {

FILE* gIndex = nullptr;

// Panel pixel (px, py) is logical (539 - py, px); see canvas.cpp toPhysical().
void save(const char* name) {
  char path[64];
  snprintf(path, sizeof(path), "%s.pgm", name);
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P5\n%d %d\n255\n", kScreenW, kScreenH);
  const uint8_t* fb = display.getFrameBuffer();
  for (int ly = 0; ly < kScreenH; ++ly) {
    for (int lx = 0; lx < kScreenW; ++lx) {
      const int px = ly;
      const int py = (kPhysH - 1) - lx;
      const uint32_t i = static_cast<uint32_t>(py) * (kPhysW / 8) + px / 8;
      const uint8_t m = static_cast<uint8_t>(0x80 >> (px & 7));
      uint8_t v = (fb[i] & m) ? 255 : 0;
      if (v && gGrayLsb && (gGrayLsb[i] & m)) v = 110;
      else if (v && gGrayMsb && (gGrayMsb[i] & m)) v = 190;
      fputc(v, f);
    }
  }
  fclose(f);
  fprintf(gIndex, "%s\n", name);
}

std::vector<DirEntry> sampleEntries() {
  struct E {
    const char* name;
    size_t size;
    bool dir;
  };
  const E list[] = {
      {"books", 0, true},          {"firmware", 0, true},         {"gps", 0, true},
      {"sleep", 0, true},          {"notes.txt", 2048, false},    {"cover.jpg", 183000, false},
      {"crosspoint-1.5.34-t5s3.bin", 5800000, false},             {"wallpaper.bmp", 518400, false},
      {"readme.txt", 900, false},  {"meshtastic.log", 41000, false},
  };
  std::vector<DirEntry> out;
  for (const E& e : list) {
    DirEntry d;
    d.name = e.name;
    d.path = std::string("/") + e.name;
    d.size = e.size;
    d.isDir = e.dir;
    out.push_back(d);
  }
  return out;
}

}  // namespace

int main() {
  gIndex = fopen("screens.txt", "w");
  canvasBegin();

  SlotInfo slots[kSlotCount];
  for (int i = 0; i < kSlotCount; ++i) slots[i] = appsSlotInfo(i);
  const FlashSpace space = appsFlashSpace();

  uiDrawHome(slots, space, true, 0);
  save("home");
  uiDrawSlotMenu(slots[0]);
  save("slot_menu");
  uiDrawHome(slots, space, true, -1);
  save("home_first_run");

  const std::vector<DirEntry> entries = sampleEntries();
  ExplorerDrawState st;
  st.currentPath = "/";
  uiDrawExplorer(entries, st, space);
  save("files");

  std::vector<int> picked = {6, 8};
  st.currentPath = "/books/scifi";
  st.selectedIndices = &picked;
  st.multiSelect = true;
  uiDrawExplorer(entries, st, space);
  save("files_selected");

  st.selectedIndices = nullptr;
  st.multiSelect = false;
  st.selected = 4;
  st.sheetOpen = true;
  uiDrawExplorer(entries, st, space);
  save("files_menu");

  uiDrawShade(space);
  save("shade");
  uiDrawSettings(space);
  save("settings");
  uiDrawSetTime(space);
  save("set_time");
  uiDrawHardware(space);
  save("hardware");
  uiDrawWifi(space, "Transfer on", "Basilauncher", "http://192.168.4.1/", "1 client", true);
  save("transfer");

  WifiStatus ws;
  ws.mode = WifiMode::Station;
  ws.connected = true;
  snprintf(ws.ssid, sizeof(ws.ssid), "HomeNet");
  snprintf(ws.ip, sizeof(ws.ip), "192.168.1.42");
  ws.rssi = -58;
  const WifiAp aps[] = {
      {"HomeNet", -58, false}, {"Elisa-5G", -71, false}, {"Cafe Free", -80, true}, {"DIRECT-Printer", -88, false}};
  uiDrawNet(space, ws, aps, 4);
  save("wifi");

  uiDrawGps();
  save("gps");
  uiDrawTextEdit("Rename", "notes.txt", false, false, TextEditMode::Rename);
  save("keyboard");
  uiDrawConfirm("Delete", "Delete crosspoint-1.5.34-t5s3.bin? This cannot be undone.", "Delete");
  save("confirm");
  uiDrawMessage("Too large", "crosspoint-1.5.34-t5s3.bin needs 5.5 MB; the largest empty slot is 2.2 MB.");
  save("message");
  uiDrawProgress("Installing crosspoint-1.5.34-t5s3.bin to slot A", 62,
                 "Keep the device on until this finishes.");
  save("progress");

  // The text-size setting changes every layout; check both ends.
  for (int size : {0, 2}) {
    canvasSetUiTextSize(size);
    const char* tag = size == 0 ? "small" : "large";
    char name[32];
    uiDrawHome(slots, space, true, 0);
    snprintf(name, sizeof(name), "home_%s", tag);
    save(name);
    st = ExplorerDrawState();
    st.currentPath = "/books/scifi";
    uiDrawExplorer(entries, st, space);
    snprintf(name, sizeof(name), "files_%s", tag);
    save(name);
    uiDrawSettings(space);
    snprintf(name, sizeof(name), "settings_%s", tag);
    save(name);
    uiDrawSetTime(space);
    snprintf(name, sizeof(name), "set_time_%s", tag);
    save(name);
  }
  canvasSetUiTextSize(1);

  fclose(gIndex);
  return 0;
}
