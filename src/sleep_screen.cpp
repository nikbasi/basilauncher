#include "sleep_screen.h"

#include "board_hal.h"
#include "canvas.h"

#include <BoardConfig.h>
#include <SD.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kMaxSleepFiles = 64;
constexpr size_t kMaxNameLen = 48;

bool isBmpName(const char* name) {
  if (!name) return false;
  const size_t n = strlen(name);
  if (n < 5) return false;
  const char* ext = name + n - 4;
  return (ext[0] == '.' && (ext[1] == 'b' || ext[1] == 'B') && (ext[2] == 'm' || ext[2] == 'M') &&
          (ext[3] == 'p' || ext[3] == 'P'));
}

int collectSleepFiles(char names[][kMaxNameLen], const char* dirPath) {
  if (!SD.exists(dirPath)) return 0;
  File root = SD.open(dirPath);
  if (!root || !root.isDirectory()) return 0;
  int count = 0;
  for (File entry = root.openNextFile(); entry && count < kMaxSleepFiles; entry = root.openNextFile()) {
    if (entry.isDirectory()) continue;
    const char* name = entry.name();
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] == '.' || base[0] == '_') continue;
    if (!isBmpName(base)) continue;
    snprintf(names[count], kMaxNameLen, "%s", base);
    ++count;
  }
  return count;
}

bool drawBmpFile(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;

  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54) return false;
  if (hdr[0] != 'B' || hdr[1] != 'M') return false;

  uint32_t dataOff = 0;
  int32_t width = 0, height = 0;
  uint16_t bpp = 0;
  uint32_t compression = 0;
  memcpy(&dataOff, hdr + 10, 4);
  memcpy(&width, hdr + 18, 4);
  memcpy(&height, hdr + 22, 4);
  memcpy(&bpp, hdr + 28, 2);
  memcpy(&compression, hdr + 30, 4);
  if (bpp != 24 || compression != 0 || width <= 0 || width > 2000 || height == 0) return false;

  const bool bottomUp = height > 0;
  const int hAbs = bottomUp ? height : -height;
  if (hAbs > 2000) return false;

  const int rowBytes = ((width * 3 + 3) / 4) * 4;
  auto* row = static_cast<uint8_t*>(malloc(static_cast<size_t>(rowBytes)));
  if (!row) return false;

  canvasClear();
  const int srcX0 = width > kScreenW ? (width - kScreenW) / 2 : 0;
  const int dstX0 = width < kScreenW ? (kScreenW - width) / 2 : 0;
  const int copyW = width < kScreenW ? width : kScreenW;
  const int srcY0 = hAbs > kScreenH ? (hAbs - kScreenH) / 2 : 0;
  const int dstY0 = hAbs < kScreenH ? (kScreenH - hAbs) / 2 : 0;
  const int copyH = hAbs < kScreenH ? hAbs : kScreenH;

  for (int sy = 0; sy < hAbs; ++sy) {
    const int fileRow = bottomUp ? (hAbs - 1 - sy) : sy;
    if (!f.seek(dataOff + static_cast<uint32_t>(fileRow) * static_cast<uint32_t>(rowBytes))) {
      free(row);
      return false;
    }
    if (f.read(row, rowBytes) != rowBytes) {
      free(row);
      return false;
    }
    if (sy < srcY0 || sy >= srcY0 + copyH) continue;
    const int dy = dstY0 + (sy - srcY0);
    for (int sx = 0; sx < copyW; ++sx) {
      const int srcX = srcX0 + sx;
      const uint8_t* px = row + srcX * 3;
      const int lum = (static_cast<int>(px[2]) * 30 + static_cast<int>(px[1]) * 59 + static_cast<int>(px[0]) * 11) / 100;
      canvasSetPixel(dstX0 + sx, dy, lum < 128);
    }
  }
  free(row);
  return true;
}

bool pickAndDrawRandom() {
  char names[kMaxSleepFiles][kMaxNameLen];
  const char* dirs[] = {"/sleep", "/.sleep"};
  for (const char* dir : dirs) {
    const int n = collectSleepFiles(names, dir);
    if (n <= 0) continue;
    const int pick = static_cast<int>(esp_random() % static_cast<uint32_t>(n));
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", dir, names[pick]);
    Serial.printf("Sleep image: %s\n", path);
    if (drawBmpFile(path)) return true;
  }
  return false;
}

// Aurora-style caption chip. `top` places it near the status area; otherwise bottom.
void drawSleepBanner(const char* text, bool top, int scale) {
  if (!text || !text[0]) return;
  if (scale < 1) scale = 1;
  const int tw = canvasTextWidth(text, scale);
  const int th = canvasTextHeight(scale);
  const int padX = 20;
  const int padY = 14;
  int boxW = tw + padX * 2;
  const int boxH = th + padY * 2;
  if (boxW > kScreenW - 16) boxW = kScreenW - 16;
  const int boxX = (kScreenW - boxW) / 2;
  const int boxY = top ? 36 : (kScreenH - boxH - 28);
  canvasFillRoundRect(boxX - 3, boxY - 3, boxW + 6, boxH + 6, 14, true);
  canvasFillRoundRect(boxX, boxY, boxW, boxH, 12, false);
  canvasDrawString(boxX + (boxW - tw) / 2, boxY + padY, text, true, scale);
}

void waitBootReleased() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  const int pressed = activeHigh ? HIGH : LOW;
  const uint32_t start = millis();
  while (digitalRead(pin) == pressed && (millis() - start) < 5000) {
    delay(10);
  }
}

void armBootWakeup() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  // Clear inherited light-sleep wake sources (same reason Aurora does).
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
#if SOC_PM_SUPPORT_EXT1_WAKEUP
  esp_sleep_enable_ext1_wakeup(1ULL << pin,
                               activeHigh ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pin, activeHigh ? ESP_GPIO_WAKEUP_GPIO_HIGH
                                                            : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
  Serial.printf("Sleep: wake on GPIO%d (%s)\n", pin, activeHigh ? "high" : "low");
}

bool bootPinPressed() {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return false;
  const bool activeHigh = BoardConfig::ACTIVE.input.powerActiveHigh;
  pinMode(pin, activeHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  return digitalRead(pin) == (activeHigh ? HIGH : LOW);
}

bool wokeFromBootButton() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT1) return true;
#ifdef ESP_SLEEP_WAKEUP_GPIO
  if (cause == ESP_SLEEP_WAKEUP_GPIO) return true;
#endif
  return false;
}

}  // namespace

void enterSleepWithScreensaver(bool quiet) {
  if (!quiet) {
    // Large top toast so the sleep transition is obvious.
    drawSleepBanner("Entering sleep...", /*top=*/true, /*scale=*/2);
    canvasPresent(EInkDisplay::HALF_REFRESH);
    delay(700);
  }

  if (!pickAndDrawRandom()) {
    canvasClear();
  }
  drawSleepBanner("Hold BOOT to wake", /*top=*/false, /*scale=*/1);
  canvasPresent(EInkDisplay::FULL_REFRESH);
  delay(200);

  waitBootReleased();
  armBootWakeup();
  boardPrepareDeepSleep();
  esp_deep_sleep_start();
}

void sleepRequireBootHoldToWake(uint32_t needMs) {
  if (!wokeFromBootButton()) return;

  // EXT1 wakes on any low; require the same hold as sleep-entry to stay up.
  const uint32_t start = millis();
  bool held = false;
  while (bootPinPressed()) {
    if (millis() - start >= needMs) {
      held = true;
      break;
    }
    delay(10);
  }
  if (held) {
    Serial.println("Wake: BOOT held — staying awake");
    waitBootReleased();
    return;
  }
  Serial.println("Wake: short BOOT click — sleeping again");
  enterSleepWithScreensaver(/*quiet=*/true);
}
