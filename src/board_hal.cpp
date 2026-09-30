#include "board_hal.h"

#include "canvas.h"

#include <BatteryMonitor.h>
#include <BoardT5S3.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SD.h>
#include <SPI.h>
#include <cstdio>
#include <esp_ota_ops.h>

namespace {

InputManager gInput;
BatteryMonitor gBattery;
Rtc gRtc;
bool gSdOk = false;
bool gTouchOk = false;
bool gWasDown = false;
bool gRtcOk = false;

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
  // BatteryMonitor reads BoardConfig gauge on construction; touch a read to log.
  const auto st = gBattery.readStatus();
  Serial.printf("Battery: supported=%d pct=%u charging=%d\n", st.supported ? 1 : 0,
                st.percentageKnown ? st.percentage : 0u, st.charging ? 1 : 0);
}

void boardInitClock() {
  gRtcOk = gRtc.begin();
  Serial.printf("RTC: %s\n", gRtcOk ? "ok" : "absent/unset");
}

void boardMarkFactoryValid() {
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    Serial.printf("mark factory valid: %s\n", esp_err_to_name(err));
  }
}

void boardPrepareDeepSleep() { (void)BoardT5S3::parkEpdPowerForSleep(); }

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
  info.minute = dt.minute;
  snprintf(info.time, sizeof(info.time), "%02u:%02u", dt.hour, dt.minute);
  snprintf(info.date, sizeof(info.date), "%04u-%02u-%02u", dt.year, dt.month, dt.day);
  return info;
}

int boardBatteryPercent() {
  const BoardPowerInfo p = boardPower();
  return p.known ? p.percent : -1;
}

bool boardSdOk() { return gSdOk; }
bool boardTouchOk() { return gTouchOk; }

bool boardPollTouch(int& x, int& y) {
  if (!gTouchOk) return false;
  gInput.update();
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
