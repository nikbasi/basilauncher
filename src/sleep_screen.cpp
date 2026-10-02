#include "sleep_screen.h"

#include "board_hal.h"
#include "bmp_draw.h"
#include "canvas.h"

#include <BoardConfig.h>
#include <SD.h>
#include <driver/gpio.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <cstdio>
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
    if (bmpDrawFile(path)) return true;
  }
  return false;
}

// Caption chip. `top` places it near the status area; otherwise bottom.
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
  // Clear inherited light-sleep wake sources.
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
    canvasPresentFor(CanvasRefreshIntent::Navigation);
    delay(300);
  }

  if (!pickAndDrawRandom()) {
    canvasClear();
  }
  drawSleepBanner("Hold BOOT to wake", /*top=*/false, /*scale=*/1);
  canvasPresentFor(CanvasRefreshIntent::Sleep);
  delay(200);

  waitBootReleased();
  armBootWakeup();
  boardPrepareDeepSleep();
  esp_deep_sleep_start();
}

void sleepRequireBootHoldToWake(uint32_t needMs) {
  if (!wokeFromBootButton()) return;

  // Debounce USB/JTAG glitches that pulse GPIO0 without a finger on the button.
  delay(40);
  if (!bootPinPressed()) {
    Serial.println("Wake: BOOT glitch — sleeping again");
    enterSleepWithScreensaver(/*quiet=*/true);
    return;
  }

  // Hold to stay awake; short press cycles to another sleep image.
  if (needMs < 40) needMs = 40;
  const uint32_t start = millis();
  while (bootPinPressed()) {
    if ((millis() - start) >= needMs) {
      Serial.println("Wake: BOOT hold — staying awake");
      waitBootReleased();
      return;
    }
    delay(10);
  }

  Serial.println("Wake: BOOT tap — next sleep image");
  enterSleepWithScreensaver(/*quiet=*/true);
}
