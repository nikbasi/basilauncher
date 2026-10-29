// Fake device for the host preview: the board, slot, Wi-Fi, GPS and file
// helpers the UI calls, answering with one fixed, realistic state.

#include "app_names.h"
#include "apps_scan.h"
#include "board_hal.h"
#include "file_ops.h"
#include "gps.h"
#include "qr_draw.h"
#include "canvas.h"
#include "wifi_session.h"

#include <cstdio>
#include <cstring>

HostSerial Serial;

namespace {

struct FakeSlot {
  const char* file;
  size_t size;
  size_t capacity;
};
const FakeSlot kSlots[kSlotCount] = {
    {"crosspoint-1.5.33-t5s3.bin", 5767168, 6291456},
    {"T5S3-GameBoy-v1.2.bin", 578560, 2621440},
    {"meshtastic-t5s3-inkhud-2.6.bin", 2306867, 2621440},
    {nullptr, 0, 2359296},
};

}  // namespace

SlotInfo appsSlotInfo(int i) {
  SlotInfo s;
  s.index = i;
  s.label[0] = static_cast<char>('A' + i);
  s.capacity = kSlots[i].capacity;
  s.occupied = kSlots[i].file != nullptr;
  s.name = s.occupied ? kSlots[i].file : "Empty";
  s.size = kSlots[i].size;
  if (s.occupied) appsTitleFromFileName(s.name.c_str(), s.title, s.version);
  // /firmware holds a newer CrossPoint.
  if (i == 0) {
    s.updatePath = "/firmware/crosspoint-1.5.34-t5s3.bin";
    s.updateVersion = "1.5.34";
    s.updateSize = 5800000;
  }
  return s;
}

FlashSpace appsFlashSpace() {
  FlashSpace f;
  for (int i = 0; i < kSlotCount; ++i) {
    f.guestTotal += kSlots[i].capacity;
    if (kSlots[i].file) {
      f.guestUsed += kSlots[i].capacity;
      ++f.occupiedSlots;
    } else {
      f.guestFree += kSlots[i].capacity;
      ++f.emptySlots;
    }
  }
  return f;
}

void appsFormatBytes(size_t bytes, char* buf, size_t len) {
  if (bytes >= 1024u * 1024u) {
    const unsigned mb10 = static_cast<unsigned>((bytes * 10u) / (1024u * 1024u));
    snprintf(buf, len, "%u.%u MB", mb10 / 10u, mb10 % 10u);
  } else if (bytes >= 1024u) {
    snprintf(buf, len, "%u KB", static_cast<unsigned>(bytes / 1024u));
  } else {
    snprintf(buf, len, "%u B", static_cast<unsigned>(bytes));
  }
}

bool appsIsRootDir(const char* p) { return !p || !p[0] || strcmp(p, "/") == 0; }

int boardBrightness() { return 42; }
int boardCleanEvery() { return 3; }
bool boardFrontlightOn() { return true; }
bool boardHasFrontlight() { return true; }
bool boardSdOk() { return true; }
int boardSleepAfterMin() { return 10; }
int boardUiTextSize() { return canvasUiTextSize(); }

BoardClockInfo boardClock() {
  BoardClockInfo c;
  c.valid = true;
  snprintf(c.time, sizeof(c.time), "09:41");
  snprintf(c.date, sizeof(c.date), "2026-10-29");
  c.hour = 9;
  c.minute = 41;
  c.day = 29;
  c.month = 10;
  c.year = 2026;
  return c;
}

BoardPowerInfo boardPower() {
  BoardPowerInfo p;
  p.known = true;
  p.percent = 78;
  return p;
}

BoardDeviceInfo boardDeviceInfo() {
  BoardDeviceInfo d;
  snprintf(d.product, sizeof(d.product), "LilyGO T5 E-Paper S3 Pro");
  snprintf(d.panel, sizeof(d.panel), "ED047TC2 960x540");
  snprintf(d.touch, sizeof(d.touch), "GT911");
  snprintf(d.light, sizeof(d.light), "PWM");
  snprintf(d.rtc, sizeof(d.rtc), "PCF8563");
  snprintf(d.mcu, sizeof(d.mcu), "ESP32-S3");
  snprintf(d.cpu, sizeof(d.cpu), "240 MHz x2");
  snprintf(d.memory, sizeof(d.memory), "8 MB PSRAM / 16 MB flash");
  snprintf(d.mac, sizeof(d.mac), "7C:DF:A1:00:11:22");
  snprintf(d.battery, sizeof(d.battery), "BQ27220 3.98 V");
  snprintf(d.storage, sizeof(d.storage), "SD 29.7 GB");
  snprintf(d.lora, sizeof(d.lora), "SX1262");
  snprintf(d.gps, sizeof(d.gps), "L76K");
  return d;
}

bool canvasDrawQr(int x, int y, int size, const char*) {
  // A stand-in pattern: a real QR needs the QRCode library.
  canvasDrawRect(x, y, size, size, true);
  for (int i = 0; i < 3; ++i) {
    const int fx = i == 1 ? x + size - 52 : x + 8;
    const int fy = i == 2 ? y + size - 52 : y + 8;
    canvasFillRect(fx, fy, 44, 44, true);
    canvasFillRect(fx + 8, fy + 8, 28, 28, false);
    canvasFillRect(fx + 14, fy + 14, 16, 16, true);
  }
  return true;
}

bool fileOpsIsBin(const char* n) { return strstr(n, ".bin") != nullptr; }
bool fileOpsIsImage(const char* n) { return strstr(n, ".jpg") || strstr(n, ".bmp"); }
bool fileOpsIsText(const char* n) { return strstr(n, ".txt") != nullptr; }

bool gpsDrawMap(int, int, int, int) { return false; }
bool gpsHasMaps() { return true; }
GpsSat gpsSat(int i) {
  GpsSat s;
  s.el = 20 + i * 9;
  s.az = i * 47;
  s.snr = i % 3 ? 30 : 0;
  return s;
}
int gpsSatCount() { return 7; }
bool gpsScrollMap(int, int, int, int, int, int) { return false; }
bool gpsSession() { return false; }
const GpsView& gpsView() {
  static GpsView v = [] {
    GpsView g;
    g.fix = 3;
    g.satsUsed = 6;
    g.satsView = 9;
    g.hasPos = true;
    g.lat = 60.16952;
    g.lon = 24.93545;
    g.hasAlt = true;
    g.altM = 18;
    return g;
  }();
  return v;
}

const char* wifiApPassword() { return "basilauncher"; }
const char* wifiApSsid() { return "Basilauncher"; }
bool wifiIsActive() { return true; }
bool wifiIsHotspot() { return false; }
bool wifiIsStation() { return true; }
