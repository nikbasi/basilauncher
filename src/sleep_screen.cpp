#include "sleep_screen.h"

#include "board_hal.h"
#include "image_draw.h"
#include "canvas.h"

#include <BoardConfig.h>
#include <SD.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kMaxSleepFiles = 64;
constexpr size_t kMaxNameLen = 80;
constexpr size_t kSleepPathMax = 160;
constexpr size_t kFramePathMax = 192;
constexpr char kFramePath[] = "/sleep/.frame";
constexpr char kFrameMagic[4] = {'B', 'S', 'L', 'F'};

struct FrameHeader {
  char magic[4];
  uint32_t fbBytes;
  char path[kFramePathMax];
};

static_assert(sizeof(FrameHeader) == 4 + 4 + 192, "sleep frame header layout");

char gSleepPaths[kMaxSleepFiles][kSleepPathMax];

bool isSleepImageName(const char* name) { return imageIsSupportedName(name); }

int collectSleepFiles(const char* dirPath, int count) {
  if (count >= kMaxSleepFiles) return count;
  if (!SD.exists(dirPath)) return count;
  File root = SD.open(dirPath);
  if (!root || !root.isDirectory()) return count;
  for (File entry = root.openNextFile(); entry && count < kMaxSleepFiles; entry = root.openNextFile()) {
    if (entry.isDirectory()) continue;
    const char* name = entry.name();
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    if (base[0] == '.' || base[0] == '_') continue;
    if (!isSleepImageName(base)) continue;
    if (strlen(base) >= kMaxNameLen) continue;
    char full[kSleepPathMax];
    const int wrote = snprintf(full, sizeof(full), "%s/%s", dirPath, base);
    if (wrote <= 0 || wrote >= static_cast<int>(sizeof(full))) continue;
    snprintf(gSleepPaths[count], kSleepPathMax, "%s", full);
    ++count;
  }
  return count;
}

int cmpSleepPath(const void* a, const void* b) {
  return strcmp(static_cast<const char*>(a), static_cast<const char*>(b));
}

int collectAllSleepFiles() {
  int count = collectSleepFiles("/sleep", 0);
  count = collectSleepFiles("/.sleep", count);
  if (count > 1) qsort(gSleepPaths, static_cast<size_t>(count), kSleepPathMax, cmpSleepPath);
  return count;
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

// If BOOT is already down, wait for a wake-length hold and abort sleep entry.
bool gSleepAbortWake = false;

bool abortSleepForBootHold(uint32_t needMs = 600) {
  if (!bootPinPressed()) return false;
  if (needMs < 40) needMs = 40;
  const uint32_t start = millis();
  while (bootPinPressed()) {
    if (millis() - start >= needMs) {
      Serial.println("Wake: BOOT hold during sleep entry — staying awake");
      waitBootReleased();
      gSleepAbortWake = true;
      return true;
    }
    delay(10);
  }
  return false;
}

// For imageDrawFile: if BOOT is down, block briefly to see if it becomes a wake hold.
bool abortCheckBootHold() { return abortSleepForBootHold(600); }

bool readFrameHeader(FrameHeader& hdr) {
  memset(&hdr, 0, sizeof(hdr));
  File f = SD.open(kFramePath, FILE_READ);
  if (!f) return false;
  const int n = f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
  f.close();
  if (n != static_cast<int>(sizeof(hdr))) return false;
  if (memcmp(hdr.magic, kFrameMagic, 4) != 0) return false;
  hdr.path[kFramePathMax - 1] = 0;
  return true;
}

bool writeFrame(const uint8_t* bytes, size_t n, const char* sourcePath) {
  if (!bytes || n == 0 || !sourcePath || !sourcePath[0]) return false;
  if (!SD.exists("/sleep")) SD.mkdir("/sleep");
  File f = SD.open(kFramePath, FILE_WRITE);
  if (!f) return false;
  FrameHeader hdr{};
  memcpy(hdr.magic, kFrameMagic, 4);
  hdr.fbBytes = static_cast<uint32_t>(n);
  snprintf(hdr.path, sizeof(hdr.path), "%s", sourcePath);
  bool ok = f.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) == sizeof(hdr);
  size_t put = 0;
  while (ok && put < n) {
    const size_t chunk = n - put > 4096 ? 4096 : n - put;
    const size_t wrote = f.write(bytes + put, chunk);
    if (wrote != chunk) {
      ok = false;
      break;
    }
    put += wrote;
    esp_task_wdt_reset();
  }
  f.close();
  if (!ok) SD.remove(kFramePath);
  return ok;
}

bool blitFrame() {
  File f = SD.open(kFramePath, FILE_READ);
  if (!f) return false;
  FrameHeader hdr{};
  if (f.read(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr)) != static_cast<int>(sizeof(hdr)) ||
      memcmp(hdr.magic, kFrameMagic, 4) != 0) {
    f.close();
    return false;
  }
  uint8_t* fb = display.getFrameBuffer();
  const size_t n = display.getBufferSize();
  if (!fb || hdr.fbBytes != n) {
    f.close();
    return false;
  }
  size_t got = 0;
  while (got < n) {
    const int chunk = f.read(fb + got, n - got);
    if (chunk <= 0) break;
    got += static_cast<size_t>(chunk);
    esp_task_wdt_reset();
  }
  f.close();
  if (got != n) return false;
  canvasDiscardGray();
  return true;
}

bool frameMatches(const char* path) {
  FrameHeader hdr{};
  if (!path || !path[0] || !readFrameHeader(hdr)) return false;
  if (hdr.fbBytes != display.getBufferSize()) return false;
  return strcmp(hdr.path, path) == 0;
}

// advance=false keeps the pinned or last-shown photo. advance=true steps to the
// next file in alphabetical order.
bool chooseSleepPath(bool advance, char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = 0;
  FrameHeader hdr{};
  const bool have = readFrameHeader(hdr) && hdr.path[0] && SD.exists(hdr.path);
  const int n = collectAllSleepFiles();

  if (!advance && have) {
    snprintf(out, outLen, "%s", hdr.path);
    return true;
  }
  if (n <= 0) {
    if (have) {
      snprintf(out, outLen, "%s", hdr.path);
      return true;
    }
    return false;
  }

  int idx = 0;
  if (have) {
    int found = -1;
    for (int i = 0; i < n; ++i) {
      if (strcmp(gSleepPaths[i], hdr.path) == 0) {
        found = i;
        break;
      }
    }
    if (found >= 0) idx = advance ? (found + 1) % n : found;
  }
  snprintf(out, outLen, "%s", gSleepPaths[idx]);
  return out[0] != 0;
}

// After a short wake tap, wait for a follow-up hold before committing to a
// slow wallpaper redraw (user often taps by mistake then holds to wake).
bool graceWaitForWakeHold(uint32_t windowMs, uint32_t needMs) {
  const uint32_t end = millis() + windowMs;
  while (static_cast<int32_t>(end - millis()) > 0) {
    if (bootPinPressed() && abortSleepForBootHold(needMs)) return true;
    delay(10);
  }
  return false;
}

}  // namespace

bool sleepSaveCapturedFrame(const char* sourcePath) {
  if (!canvasHasCapture()) return false;
  return writeFrame(canvasCapturedFrame(), canvasCapturedBytes(), sourcePath);
}

bool sleepWokeFromBootButton() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT1) return true;
#ifdef ESP_SLEEP_WAKEUP_GPIO
  if (cause == ESP_SLEEP_WAKEUP_GPIO) return true;
#endif
  return false;
}

bool sleepTryAbortForBootHold(uint32_t needMs) { return abortSleepForBootHold(needMs); }

bool enterSleepWithScreensaver(bool quiet) {
  if (!quiet) {
    // Release the hold-to-sleep press before we start listening for wake.
    waitBootReleased();
  } else {
    // Short BOOT wake: give time to hold again before the slow image swap.
    if (graceWaitForWakeHold(1200, 500)) return false;
  }

  if (abortSleepForBootHold()) return false;

  gSleepAbortWake = false;
  char path[kFramePathMax];
  const bool havePath = chooseSleepPath(quiet, path, sizeof(path));
  bool drew = false;
  if (havePath && frameMatches(path)) {
    drew = blitFrame();
  }
  if (havePath && !drew) {
    // A short tap only swaps the photo. The "Entering sleep..." banner is for
    // the first sleep, not for changing the background.
    if (!quiet) {
      drawSleepBanner("Entering sleep...", /*top=*/true, /*scale=*/2);
      canvasPresentFor(CanvasRefreshIntent::Navigation);
      delay(300);
      if (abortSleepForBootHold()) return false;
    }
    if (imageDrawFile(path, abortCheckBootHold)) {
      uint8_t* fb = display.getFrameBuffer();
      const size_t n = display.getBufferSize();
      if (fb && n > 0) writeFrame(fb, n, path);
      drew = true;
    } else if (gSleepAbortWake || abortSleepForBootHold(80)) {
      return false;
    }
  }
  if (!drew) {
    if (gSleepAbortWake || abortSleepForBootHold(80)) return false;
    canvasClear();
  }
  if (abortSleepForBootHold()) return false;

  canvasDiscardGray();
  canvasPresentFor(CanvasRefreshIntent::Sleep);
  // A hold across the long refresh should wake without another full hold.
  if (abortSleepForBootHold(bootPinPressed() ? 80 : 600)) return false;

  delay(200);
  if (abortSleepForBootHold()) return false;

  waitBootReleased();
  armBootWakeup();
  boardPrepareDeepSleep();
  esp_deep_sleep_start();
  return true;  // not reached
}

bool sleepBootHoldKeepsAwake(uint32_t needMs) {
  if (!sleepWokeFromBootButton()) return true;

  // millis() starts at the wake reset, while the button is already down, so
  // this wait is the whole hold. It must run before display and SD init or
  // that startup time gets added on top of needMs.
  if (needMs < 40) needMs = 40;
  while (bootPinPressed()) {
    if (millis() >= needMs) {
      Serial.println("Wake: BOOT hold — staying awake");
      waitBootReleased();
      return true;
    }
    delay(10);
  }

  Serial.printf("Wake: BOOT released after %lums — sleeping again\n",
                static_cast<unsigned long>(millis()));
  return false;
}
