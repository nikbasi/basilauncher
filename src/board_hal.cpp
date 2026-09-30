#include "board_hal.h"

#include "canvas.h"

#include <BoardT5S3.h>
#include <InputManager.h>
#include <SD.h>
#include <SPI.h>
#include <esp_ota_ops.h>

namespace {

InputManager gInput;
bool gSdOk = false;
bool gTouchOk = false;
bool gWasDown = false;

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

void boardMarkFactoryValid() {
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    Serial.printf("mark factory valid: %s\n", esp_err_to_name(err));
  }
}

void boardPrepareDeepSleep() { (void)BoardT5S3::parkEpdPowerForSleep(); }

int boardBatteryPercent() { return -1; }

bool boardSdOk() { return gSdOk; }
bool boardTouchOk() { return gTouchOk; }

bool boardPollTouch(int& x, int& y) {
  if (!gTouchOk) return false;
  gInput.update();
  float nx = 0, ny = 0;
  if (!gInput.wasTouchTap(nx, ny)) {
    // Also accept a simple press-edge while held briefly, in case tap
    // classification is picky during bring-up.
    if (gInput.isTouchPressed()) {
      gWasDown = true;
      return false;
    }
    if (gWasDown && gInput.wasTouchReleased()) {
      gWasDown = false;
      const auto pt = gInput.getTouchPoint();
      if (pt.valid) {
        // getTouchPoint is panel pixels; normalize manually.
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
