#include "board_hal.h"

#include "canvas.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <BoardT5S3.h>
#include <Esp.h>
#include <InputManager.h>
#include <Preferences.h>
#include <Rtc.h>
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <cstring>
#include <driver/gpio.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>

namespace {

InputManager gInput;
BatteryMonitor gBattery;
Rtc gRtc;
Preferences gPrefs;
bool gSdOk = false;
bool gTouchOk = false;
bool gAsyncInput = false;
bool gWasDown = false;
bool gRtcOk = false;
uint8_t gBrightness = 40;
bool gLightOn = false;
bool gLightHw = false;
int gSleepAfterMin = 10;
BoardPowerInfo gPowerCache;
bool gChargingKnown = false;
bool gCharging = false;
bool gExternalPowerKnown = false;
bool gExternalPower = false;
uint32_t gLastPowerSampleMs = 0;
constexpr uint32_t kPowerSampleIntervalMs = 5000;

// LilyGO T5 S3 Pro: PT4103 EN on GPIO11, 1 kHz / 12-bit (matches BoardConfig).
constexpr int kFlGpio = 11;
constexpr int kFlFreqHz = 1000;
constexpr int kFlResBits = 12;

uint32_t brightnessToDuty(uint8_t percent) {
  if (percent == 0) return 0;
  if (percent > 100) percent = 100;
  const uint32_t maxDuty = (1u << kFlResBits) - 1u;
  // Mild gamma so low % stays usable on the boost converter.
  const uint32_t p = percent;
  return (maxDuty * p * p) / 10000u;
}

void applyFrontlight() {
  if (!gLightHw) return;
  if (!gLightOn || gBrightness == 0) {
    ledcWrite(kFlGpio, 0);
  } else {
    ledcWrite(kFlGpio, brightnessToDuty(gBrightness));
  }
}

void persistLight() {
  if (!gPrefs.begin("basil", false)) return;
  gPrefs.putUChar("bright", gBrightness);
  gPrefs.putBool("lightOn", gLightOn);
  gPrefs.end();
}

void updatePowerCache(const BatteryMonitor::Status& status) {
  // The gauge and charger are independent I2C reads. Preserve each last-known
  // field when one transaction fails instead of making the status bar flicker
  // to "unknown" during SD and flash activity.
  if (status.supported && status.percentageKnown) {
    gPowerCache.known = true;
    gPowerCache.percent = status.percentage > 100 ? 100 : static_cast<int>(status.percentage);
  }
  if (status.millivoltsKnown && status.millivolts > 0) {
    gPowerCache.voltageKnown = true;
    gPowerCache.millivolts = status.millivolts;
  }
  if (status.chargingKnown) {
    gChargingKnown = true;
    gCharging = status.charging;
  }
  if (status.externalPowerKnown) {
    gExternalPowerKnown = true;
    gExternalPower = status.externalPower;
  }
  gPowerCache.charging = gChargingKnown && gCharging;
  gPowerCache.plugged = (gExternalPowerKnown && gExternalPower) || gPowerCache.charging;
}

}  // namespace

bool boardInit() {
  BoardT5S3::begin();
  return true;
}

bool boardInitDisplay() {
  canvasBegin();
  return display.framebufferReady();
}

bool boardInitTouch() {
  gInput.begin();
  gTouchOk = gInput.hasTouch();
  if (!gTouchOk) {
    Serial.println("GT911 not found");
  } else {
    // Queue taps/swipes while e-ink blocking refreshes so typing stays ahead
    // of the panel. Main loop must drain via pop*, not call update().
    gInput.beginAsync(/*taskPriority=*/2, /*pollMs=*/12, /*queueLen=*/48);
    gAsyncInput = true;
  }
  return gTouchOk;
}

bool boardInitSd() {
  BoardT5S3::prepareSdBus();
  if (!SD.begin(T5S3_SD_CS, SPI, 16000000)) {
    Serial.println("SD mount failed");
    gSdOk = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("No SD card");
    gSdOk = false;
    return false;
  }
  gSdOk = true;
  return true;
}

void boardInitPower() {
  // A prior guest HIZ session can leave the BQ25896 input disabled so USB
  // enumerates but never charges — clear that on every hub boot.
  const bool hizCleared = gBattery.clearChargerInputHiZ();
  uint8_t reg00 = 0, reg0b = 0;
  const bool diag = gBattery.readChargerDiag(reg00, reg0b);
  const auto st = gBattery.readStatus();
  updatePowerCache(st);
  gLastPowerSampleMs = millis();
  Serial.printf("Battery: supported=%d pct=%u chg=%d plug=%d hiz_ok=%d", st.supported ? 1 : 0,
                st.percentageKnown ? st.percentage : 0u, (st.chargingKnown && st.charging) ? 1 : 0,
                (st.externalPowerKnown && st.externalPower) ? 1 : 0, hizCleared ? 1 : 0);
  if (diag) {
    Serial.printf(" reg00=%02X reg0b=%02X vbus=%u chrg=%u\n", reg00, reg0b, (reg0b >> 5) & 7u,
                  (reg0b >> 3) & 3u);
  } else {
    Serial.println(" (no charger diag)");
  }
}

void boardInitClock() {
  gRtcOk = gRtc.begin();
  Serial.printf("RTC: %s\n", gRtcOk ? "ok" : "absent/unset");
}

void boardInitFrontlight() {
  gpio_hold_dis(static_cast<gpio_num_t>(kFlGpio));
  gLightHw = ledcAttach(kFlGpio, kFlFreqHz, kFlResBits);
  int cleanEvery = 8;
  int sleepAfter = 10;
  int uiText = 1;
  if (gPrefs.begin("basil", true)) {
    gBrightness = gPrefs.getUChar("bright", 40);
    gLightOn = gPrefs.getBool("lightOn", false);
    cleanEvery = static_cast<int>(gPrefs.getUChar("cleanEv", 8));
    sleepAfter = static_cast<int>(gPrefs.getUChar("sleepMin", 10));
    uiText = static_cast<int>(gPrefs.getUChar("uiText", 1));
    gPrefs.end();
  }
  if (gBrightness > 100) gBrightness = 100;
  canvasSetCleanEvery(cleanEvery);
  canvasSetUiTextSize(uiText);
  gSleepAfterMin = sleepAfter;
  if (gSleepAfterMin < 0) gSleepAfterMin = 0;
  if (gSleepAfterMin > 60) gSleepAfterMin = 60;
  applyFrontlight();
  Serial.printf("Frontlight: hw=%d on=%d bright=%u cleanEvery=%d sleepAfter=%d uiText=%d\n",
                gLightHw ? 1 : 0, gLightOn ? 1 : 0, gBrightness, canvasCleanEvery(), gSleepAfterMin,
                canvasUiTextSize());
}

int boardCleanEvery() { return canvasCleanEvery(); }

void boardSetCleanEvery(int n) {
  canvasSetCleanEvery(n);
  if (gPrefs.begin("basil", false)) {
    gPrefs.putUChar("cleanEv", static_cast<uint8_t>(canvasCleanEvery()));
    gPrefs.end();
  }
}

int boardSleepAfterMin() { return gSleepAfterMin; }

void boardSetSleepAfterMin(int minutes) {
  if (minutes < 0) minutes = 0;
  if (minutes > 60) minutes = 60;
  gSleepAfterMin = minutes;
  if (gPrefs.begin("basil", false)) {
    gPrefs.putUChar("sleepMin", static_cast<uint8_t>(gSleepAfterMin));
    gPrefs.end();
  }
}

int boardUiTextSize() { return canvasUiTextSize(); }

void boardSetUiTextSize(int level) {
  canvasSetUiTextSize(level);
  if (gPrefs.begin("basil", false)) {
    gPrefs.putUChar("uiText", static_cast<uint8_t>(canvasUiTextSize()));
    gPrefs.end();
  }
}

void boardMarkFactoryValid() {
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) Serial.printf("mark factory valid: %s\n", esp_err_to_name(err));
}

void boardPrepareDeepSleep() {
  if (gLightHw) {
    ledcWrite(kFlGpio, 0);
    ledcDetach(kFlGpio);
    pinMode(kFlGpio, OUTPUT);
    digitalWrite(kFlGpio, LOW);
    gpio_hold_en(static_cast<gpio_num_t>(kFlGpio));
  }
  (void)BoardT5S3::parkEpdPowerForSleep();
}

BoardPowerInfo boardPower() {
  const uint32_t now = millis();
  if (gPowerCache.known && now - gLastPowerSampleMs < kPowerSampleIntervalMs) return gPowerCache;
  const auto st = gBattery.readStatus();
  updatePowerCache(st);
  gLastPowerSampleMs = now;
  return gPowerCache;
}

bool boardPowerConnectionChanged() {
  constexpr uint32_t kCablePollMs = 400;
  const uint32_t now = millis();
  if (now - gLastPowerSampleMs < kCablePollMs) return false;
  const bool wasCharging = gPowerCache.charging;
  const bool wasPlugged = gPowerCache.plugged;
  const auto st = gBattery.readStatus();
  updatePowerCache(st);
  gLastPowerSampleMs = now;
  return gPowerCache.known && (gPowerCache.charging != wasCharging || gPowerCache.plugged != wasPlugged);
}

void formatCapacity(uint64_t bytes, char* buf, size_t len) {
  if (bytes >= 1024ull * 1024ull * 1024ull) {
    const unsigned tenths =
        static_cast<unsigned>((bytes * 10ull) / (1024ull * 1024ull * 1024ull));
    snprintf(buf, len, "%u.%u GB", tenths / 10u, tenths % 10u);
  } else if (bytes >= 1024ull * 1024ull) {
    const unsigned tenths = static_cast<unsigned>((bytes * 10ull) / (1024ull * 1024ull));
    snprintf(buf, len, "%u.%u MB", tenths / 10u, tenths % 10u);
  } else if (bytes >= 1024ull) {
    snprintf(buf, len, "%u KB", static_cast<unsigned>(bytes / 1024ull));
  } else {
    snprintf(buf, len, "%u B", static_cast<unsigned>(bytes));
  }
}

#if T5S3_HAS_LORA_GPS
bool waitLoraReady(uint32_t timeoutMs) {
  pinMode(T5S3_LORA_BUSY, INPUT);
  const uint32_t start = millis();
  while (digitalRead(T5S3_LORA_BUSY) == HIGH) {
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
  return true;
}

void loraTransfer(uint8_t* buf, size_t len) {
  digitalWrite(T5S3_SD_CS, HIGH);
  digitalWrite(T5S3_LORA_CS, LOW);
  for (size_t i = 0; i < len; ++i) buf[i] = SPI.transfer(buf[i]);
  digitalWrite(T5S3_LORA_CS, HIGH);
}

uint8_t readLoraRegister(uint16_t address) {
  if (!waitLoraReady(30)) return 0xFF;
  uint8_t buf[5] = {0x1D, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address), 0x00, 0x00};
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  loraTransfer(buf, sizeof(buf));
  SPI.endTransaction();
  return buf[4];
}

void identifyLora(char* out, size_t outLen) {
  snprintf(out, outLen, "not detected");
  pinMode(T5S3_LORA_CS, OUTPUT);
  digitalWrite(T5S3_LORA_CS, HIGH);
  pinMode(T5S3_SD_CS, OUTPUT);
  digitalWrite(T5S3_SD_CS, HIGH);
  pinMode(T5S3_LORA_RST, OUTPUT);
  digitalWrite(T5S3_LORA_RST, LOW);
  delay(5);
  digitalWrite(T5S3_LORA_RST, HIGH);
  if (!waitLoraReady(100)) return;

  // SX1262 leaves its LoRa sync word at 0x1424 after reset.
  const uint8_t syncMsb = readLoraRegister(0x0740);
  const uint8_t syncLsb = readLoraRegister(0x0741);
  digitalWrite(T5S3_LORA_CS, HIGH);
  Serial.printf("[radio] SX1262 sync=%02X%02X\n", syncMsb, syncLsb);
  if (syncMsb == 0x14 && syncLsb == 0x24) snprintf(out, outLen, "SX1262");
}

bool readGpsLine(char* out, size_t outLen, uint32_t timeoutMs) {
  size_t n = 0;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial1.available() > 0) {
      const char c = static_cast<char>(Serial1.read());
      if (c == '\r') continue;
      if (c == '\n') {
        if (n == 0) continue;
        out[n] = 0;
        return true;
      }
      if (n + 1 < outLen) out[n++] = c;
    }
    delay(5);
  }
  if (n == 0) return false;
  out[n] = 0;
  return true;
}

void copyGpsVersion(const char* line, char* out, size_t outLen) {
  const char* sw = strstr(line, "SW=");
  if (!sw) return;
  sw += 3;
  const char* comma = strchr(sw, ',');
  const char* version = comma ? comma + 1 : sw;
  while (*version == ' ') ++version;
  char token[24];
  size_t n = 0;
  while (version[n] && version[n] != '*' && version[n] != ' ' && n + 1 < sizeof(token)) {
    token[n] = version[n];
    ++n;
  }
  token[n] = 0;
  if (token[0]) snprintf(out, outLen, "L76K %s", token);
}

void copyPrintable(const uint8_t* src, size_t srcLen, char* dst, size_t dstLen) {
  size_t n = 0;
  while (n + 1 < dstLen && n < srcLen && src[n] >= 32 && src[n] < 127) {
    dst[n] = static_cast<char>(src[n]);
    ++n;
  }
  dst[n] = 0;
}

bool readUbxMonVer(char* out, size_t outLen, uint32_t timeoutMs) {
  // UBX-MON-VER poll. This identifies a u-blox receiver such as the MIA-M10Q
  // without changing its configuration.
  const uint8_t poll[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
  while (Serial1.available() > 0) Serial1.read();
  Serial1.write(poll, sizeof(poll));

  uint8_t state = 0;
  uint16_t len = 0;
  uint16_t got = 0;
  uint8_t payload[220];
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (Serial1.available() <= 0) {
      delay(2);
      continue;
    }
    const uint8_t c = static_cast<uint8_t>(Serial1.read());
    if (state == 0) {
      state = c == 0xB5 ? 1 : 0;
    } else if (state == 1) {
      state = c == 0x62 ? 2 : (c == 0xB5 ? 1 : 0);
    } else if (state == 2) {
      state = c == 0x0A ? 3 : (c == 0xB5 ? 1 : 0);
    } else if (state == 3) {
      state = c == 0x04 ? 4 : (c == 0xB5 ? 1 : 0);
    } else if (state == 4) {
      len = c;
      state = 5;
    } else if (state == 5) {
      len |= static_cast<uint16_t>(c) << 8;
      got = 0;
      state = (len >= 40 && len <= sizeof(payload)) ? 6 : 0;
    } else {
      payload[got++] = c;
      if (got < len + 2) continue;
      char sw[31];
      char module[24] = {};
      copyPrintable(payload, 30, sw, sizeof(sw));
      if (strncmp(sw, "ROM ", 4) == 0) memmove(sw, sw + 4, strlen(sw + 4) + 1);
      for (uint16_t off = 40; off + 30 <= len; off = static_cast<uint16_t>(off + 30)) {
        if (memcmp(payload + off, "MOD=", 4) == 0) {
          copyPrintable(payload + off + 4, 26, module, sizeof(module));
          break;
        }
      }
      if (!module[0]) snprintf(module, sizeof(module), "u-blox");
      if (sw[0]) snprintf(out, outLen, "%s %s", module, sw);
      else snprintf(out, outLen, "%s", module);
      Serial.printf("[radio] gps: %s\n", out);
      return true;
    }
  }
  return false;
}

uint32_t gGpsBaud = 9600;

void identifyGps(char* out, size_t outLen) {
  snprintf(out, outLen, "not detected");
  gGpsBaud = 9600;
  Serial1.begin(9600, SERIAL_8N1, T5S3_GPS_RXD, T5S3_GPS_TXD);
  delay(250);
  while (Serial1.available() > 0) Serial1.read();
  // Quectel L76K answers this version query. A u-blox MIA-M10Q ignores it.
  Serial1.print("$PCAS06,0*1B\r\n");

  bool sawNmea = false;
  bool sawUblox = false;
  const uint32_t start = millis();
  while (millis() - start < 700) {
    char line[96];
    if (!readGpsLine(line, sizeof(line), 150)) continue;
    Serial.printf("[radio] gps: %s\n", line);
    if (strstr(line, "$GP") || strstr(line, "$GN") || strstr(line, "$BD")) sawNmea = true;
    if (strstr(line, "u-blox") || strstr(line, "MIA-M10")) sawUblox = true;
    if (strncmp(line, "$GPTXT,01,01,02", 15) == 0) {
      copyGpsVersion(line, out, outLen);
      if (strstr(out, "L76K")) {
        Serial1.end();
        return;
      }
    }
  }

  const uint32_t bauds[] = {sawUblox ? 9600u : 38400u, sawUblox ? 38400u : 9600u};
  for (const uint32_t baud : bauds) {
    Serial1.updateBaudRate(baud);
    if (readUbxMonVer(out, outLen, 500)) {
      gGpsBaud = baud;
      Serial1.end();
      return;
    }
  }
  Serial1.end();
  if (sawNmea || sawUblox) snprintf(out, outLen, "detected");
}

void identifyRadios(char* lora, size_t loraLen, char* gps, size_t gpsLen) {
  snprintf(lora, loraLen, "not detected");
  snprintf(gps, gpsLen, "not detected");
  const bool enabled = BoardT5S3::writePca9535Pin(PCA9535_IO00_LORA_GPS_EN, true) &&
                       BoardT5S3::setPca9535PinMode(PCA9535_IO00_LORA_GPS_EN, OUTPUT);
  if (enabled) {
    delay(30);
    identifyLora(lora, loraLen);
    identifyGps(gps, gpsLen);
  }
  // The combined module stays off after identification. Its chip-select is also
  // the display bus dummy pin, and an unpowered radio left selected breaks SD.
  BoardT5S3::disableGpsLora();
}
#endif

uint32_t boardGpsBaud() {
#if T5S3_HAS_LORA_GPS
  return gGpsBaud;
#else
  return 9600;
#endif
}

BoardDeviceInfo boardDeviceInfo() {
  BoardDeviceInfo info;
  const BoardConfig::BoardProfile& board = BoardConfig::ACTIVE;
  if (board.board == BoardConfig::Board::LilyGoT5S3) {
    snprintf(info.product, sizeof(info.product), "LilyGO T5 S3 Pro");
  } else {
    snprintf(info.product, sizeof(info.product), "%s", board.name ? board.name : "Unknown");
  }

  const char* panel =
      board.displayController == BoardConfig::DisplayController::LgfxEpd ? "ED047TC2" : "E-paper";
  snprintf(info.panel, sizeof(info.panel), "%ux%u %s", board.displayWidth, board.displayHeight, panel);
  snprintf(info.touch, sizeof(info.touch), "%s", gTouchOk ? "GT911" : "not detected");
  snprintf(info.light, sizeof(info.light), "%s", gLightHw ? "yes" : "no");
  snprintf(info.rtc, sizeof(info.rtc), "%s", gRtcOk ? "PCF85063" : "not detected");
  snprintf(info.mcu, sizeof(info.mcu), "%s rev %d", ESP.getChipModel(), ESP.getChipRevision());
  snprintf(info.cpu, sizeof(info.cpu), "%d cores, %u MHz", ESP.getChipCores(), ESP.getCpuFreqMHz());

  char flash[16], psram[16];
  formatCapacity(ESP.getFlashChipSize(), flash, sizeof(flash));
  if (ESP.getPsramSize() == 0) snprintf(psram, sizeof(psram), "no PSRAM");
  else formatCapacity(ESP.getPsramSize(), psram, sizeof(psram));
  snprintf(info.memory, sizeof(info.memory), "%s flash, %s%s", flash, psram,
           ESP.getPsramSize() == 0 ? "" : " PSRAM");

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(info.mac, sizeof(info.mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);

  const BoardPowerInfo power = boardPower();
  if (!power.known) {
    snprintf(info.battery, sizeof(info.battery), "unknown");
  } else if (power.voltageKnown) {
    snprintf(info.battery, sizeof(info.battery), "%d%%  %d.%02d V  %s", power.percent,
             power.millivolts / 1000, (power.millivolts % 1000) / 10,
             power.charging ? "charging" : (power.plugged ? "USB" : "on battery"));
  } else {
    snprintf(info.battery, sizeof(info.battery), "%d%%  %s", power.percent,
             power.charging ? "charging" : (power.plugged ? "USB" : "on battery"));
  }

  if (!gSdOk || SD.cardType() == CARD_NONE) {
    snprintf(info.storage, sizeof(info.storage), "not inserted");
  } else {
    char size[16];
    formatCapacity(SD.cardSize(), size, sizeof(size));
    const char* kind = "SD";
    switch (SD.cardType()) {
      case CARD_MMC: kind = "MMC"; break;
      case CARD_SD: kind = "SD"; break;
      case CARD_SDHC: kind = "SDHC"; break;
      default: break;
    }
    snprintf(info.storage, sizeof(info.storage), "%s %s", size, kind);
  }

#if T5S3_HAS_LORA_GPS
  static bool radiosRead = false;
  static char lora[sizeof(info.lora)];
  static char gps[sizeof(info.gps)];
  if (!radiosRead) {
    identifyRadios(lora, sizeof(lora), gps, sizeof(gps));
    radiosRead = true;
  }
  snprintf(info.lora, sizeof(info.lora), "%s", lora);
  snprintf(info.gps, sizeof(info.gps), "%s", gps);
#else
  snprintf(info.lora, sizeof(info.lora), "not fitted");
  snprintf(info.gps, sizeof(info.gps), "not fitted");
#endif
  return info;
}

BoardClockInfo boardClock() {
  BoardClockInfo info;
  if (!gRtcOk) return info;
  Rtc::DateTime dt;
  if (!gRtc.now(dt)) return info;
  info.valid = true;
  info.hour = dt.hour;
  info.minute = dt.minute;
  info.day = dt.day;
  info.month = dt.month;
  info.year = dt.year;
  snprintf(info.time, sizeof(info.time), "%02u:%02u", dt.hour, dt.minute);
  snprintf(info.date, sizeof(info.date), "%04u-%02u-%02u", dt.year, dt.month, dt.day);
  return info;
}

bool boardAdjustClockMinutes(int deltaMinutes) {
  if (!gRtcOk) return false;
  return gRtc.adjust(static_cast<int32_t>(deltaMinutes) * 60);
}

bool boardSetClock(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
  if (!gRtcOk) return false;
  Rtc::DateTime dt;
  dt.year = year;
  dt.month = month;
  dt.day = day;
  dt.hour = hour;
  dt.minute = minute;
  dt.second = 0;
  return gRtc.set(dt);
}

int boardBrightness() { return gBrightness; }
bool boardFrontlightOn() { return gLightOn && gBrightness > 0; }
bool boardHasFrontlight() { return gLightHw; }

static void applyBrightness(int percent, bool persist) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  gBrightness = static_cast<uint8_t>(percent);
  gLightOn = gBrightness > 0;
  applyFrontlight();
  if (persist) persistLight();
}

void boardSetBrightness(int percent) { applyBrightness(percent, true); }

void boardPreviewBrightness(int percent) { applyBrightness(percent, false); }

void boardCommitBrightness() { persistLight(); }

void boardSetFrontlightOn(bool on) {
  gLightOn = on;
  if (on && gBrightness == 0) gBrightness = 40;
  applyFrontlight();
  persistLight();
}

bool boardSdOk() { return gSdOk; }

void boardInputUpdate() {
  // Async task owns update(); main thread only drains queues.
  if (gAsyncInput) return;
  gInput.update();
}

bool boardPowerPressed() { return gInput.isPressed(InputManager::BTN_POWER); }

unsigned long boardPowerHeldMs() { return gInput.getPowerButtonHeldTime(); }

bool boardPollTouch(int& x, int& y) {
  if (!gTouchOk) return false;
  float nx = 0, ny = 0;
  if (gAsyncInput) {
    if (!gInput.popTouchTap(nx, ny)) return false;
    canvasTouchToLogical(nx, ny, x, y);
    return true;
  }
  if (!gInput.wasTouchTap(nx, ny)) {
    if (gInput.isTouchPressed()) {
      gWasDown = true;
      return false;
    }
    if (gWasDown && gInput.wasTouchReleased()) {
      gWasDown = false;
      const auto pt = gInput.getTouchPoint();
      if (pt.valid) {
        nx = static_cast<float>(pt.x) / 959.0f;
        ny = static_cast<float>(pt.y) / 539.0f;
        canvasTouchToLogical(nx, ny, x, y);
        return true;
      }
    }
    return false;
  }
  gWasDown = false;
  canvasTouchToLogical(nx, ny, x, y);
  return true;
}

bool boardPollLongPress(int& x, int& y) {
  if (!gTouchOk) return false;
  float nx = 0, ny = 0;
  if (gAsyncInput) {
    if (!gInput.popTouchLongPress(nx, ny)) return false;
  } else {
    if (!gInput.wasTouchLongPress(nx, ny)) return false;
    gInput.suppressTouchContact();
  }
  gWasDown = false;
  canvasTouchToLogical(nx, ny, x, y);
  return true;
}

bool boardPollSwipe(int& x0, int& y0, int& x1, int& y1) {
  if (!gTouchOk) return false;
  float nsx = 0, nsy = 0, nex = 0, ney = 0;
  if (gAsyncInput) {
    if (!gInput.popSwipe(nsx, nsy, nex, ney)) return false;
  } else if (!gInput.wasSwipe(nsx, nsy, nex, ney) && !gInput.popSwipe(nsx, nsy, nex, ney)) {
    return false;
  }
  canvasTouchToLogical(nsx, nsy, x0, y0);
  canvasTouchToLogical(nex, ney, x1, y1);
  return true;
}

bool boardTouchHeld(int& x, int& y) {
  if (!gTouchOk) return false;
  float nx = 0, ny = 0;
  if (!gInput.isTouchHeldAt(nx, ny)) return false;
  canvasTouchToLogical(nx, ny, x, y);
  return true;
}
