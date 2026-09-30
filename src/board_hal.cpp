#include "board_hal.h"

#include "canvas.h"

#include <BatteryMonitor.h>
#include <BoardT5S3.h>
#include <InputManager.h>
#include <Preferences.h>
#include <Rtc.h>
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <driver/gpio.h>
#include <esp_ota_ops.h>

namespace {

InputManager gInput;
BatteryMonitor gBattery;
Rtc gRtc;
Preferences gPrefs;
bool gSdOk = false;
bool gTouchOk = false;
bool gWasDown = false;
bool gRtcOk = false;
uint8_t gBrightness = 40;
bool gLightOn = false;
bool gLightHw = false;

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
  if (!gTouchOk) Serial.println("GT911 not found");
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
  const auto st = gBattery.readStatus();
  Serial.printf("Battery: supported=%d pct=%u\n", st.supported ? 1 : 0,
                st.percentageKnown ? st.percentage : 0u);
}

void boardInitClock() {
  gRtcOk = gRtc.begin();
  Serial.printf("RTC: %s\n", gRtcOk ? "ok" : "absent/unset");
}

void boardInitFrontlight() {
  gpio_hold_dis(static_cast<gpio_num_t>(kFlGpio));
  gLightHw = ledcAttach(kFlGpio, kFlFreqHz, kFlResBits);
  if (gPrefs.begin("basil", true)) {
    gBrightness = gPrefs.getUChar("bright", 40);
    gLightOn = gPrefs.getBool("lightOn", false);
    gPrefs.end();
  }
  if (gBrightness > 100) gBrightness = 100;
  applyFrontlight();
  Serial.printf("Frontlight: hw=%d on=%d bright=%u\n", gLightHw ? 1 : 0, gLightOn ? 1 : 0, gBrightness);
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
  BoardPowerInfo info;
  const auto st = gBattery.readStatus();
  if (!st.supported || !st.percentageKnown) return info;
  info.known = true;
  info.percent = static_cast<int>(st.percentage);
  info.charging = st.chargingKnown && st.charging;
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

void boardSetBrightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  gBrightness = static_cast<uint8_t>(percent);
  gLightOn = gBrightness > 0;
  applyFrontlight();
  persistLight();
}

void boardSetFrontlightOn(bool on) {
  gLightOn = on;
  if (on && gBrightness == 0) gBrightness = 40;
  applyFrontlight();
  persistLight();
}

int boardBatteryPercent() {
  const BoardPowerInfo p = boardPower();
  return p.known ? p.percent : -1;
}

bool boardSdOk() { return gSdOk; }
bool boardTouchOk() { return gTouchOk; }

void boardInputUpdate() {
  if (gTouchOk) gInput.update();
}

bool boardPollTouch(int& x, int& y) {
  if (!gTouchOk) return false;
  float nx = 0, ny = 0;
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

bool boardPollSwipe(int& x0, int& y0, int& x1, int& y1) {
  if (!gTouchOk) return false;
  float nsx = 0, nsy = 0, nex = 0, ney = 0;
  if (!gInput.wasSwipe(nsx, nsy, nex, ney) && !gInput.popSwipe(nsx, nsy, nex, ney)) return false;
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

void boardSuppressTouch() {
  if (gTouchOk) gInput.suppressTouchContact();
}
