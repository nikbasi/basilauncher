#pragma once

#include <Arduino.h>

struct BoardPowerInfo {
  bool known = false;
  int percent = -1;  // 0..100
  bool charging = false;
  bool plugged = false;  // USB/external power present (may be full, not charging)
  bool voltageKnown = false;
  int millivolts = 0;
};

struct BoardDeviceInfo {
  char product[40] = {};
  char panel[40] = {};
  char touch[24] = {};
  char light[16] = {};
  char rtc[24] = {};
  char mcu[40] = {};
  char cpu[32] = {};
  char memory[48] = {};
  char mac[20] = {};
  char battery[40] = {};
  char storage[40] = {};
  char lora[32] = {};
  char gps[48] = {};
};

struct BoardClockInfo {
  bool valid = false;
  char time[8] = "--:--";  // HH:MM
  char date[12] = "";      // YYYY-MM-DD
  uint8_t hour = 0;
  uint8_t minute = 255;
  uint8_t day = 1;
  uint8_t month = 1;
  uint16_t year = 2026;
};

bool boardInit();
bool boardInitDisplay();
bool boardInitTouch();
bool boardInitSd();
void boardInitPower();
void boardInitClock();
void boardInitFrontlight();
void boardMarkFactoryValid();
void boardPrepareDeepSleep();

// E-ink FAST budget in full-screen-equivalents (higher = fewer clean flashes).
int boardCleanEvery();
void boardSetCleanEvery(int n);

// Minutes of idle before auto-sleep (0 = never). Persisted.
int boardSleepAfterMin();
void boardSetSleepAfterMin(int minutes);

// UI text size: 0=Small, 1=Medium, 2=Large. Persisted.
int boardUiTextSize();
void boardSetUiTextSize(int level);

BoardPowerInfo boardPower();
// True when a fresh charger read shows the cable was attached or removed.
bool boardPowerConnectionChanged();
BoardDeviceInfo boardDeviceInfo();
BoardClockInfo boardClock();
bool boardAdjustClockMinutes(int deltaMinutes);
bool boardSetClock(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

int boardBrightness();          // 0..100 saved level
bool boardFrontlightOn();
void boardSetBrightness(int percent);  // also turns on if >0
void boardSetFrontlightOn(bool on);
bool boardHasFrontlight();

bool boardSdOk();

// Call once per loop before poll helpers.
void boardInputUpdate();
// Tap in portrait logical coords (after boardInputUpdate).
bool boardPollTouch(int& x, int& y);
// Stationary long-press in portrait logical coords; release will not also tap.
bool boardPollLongPress(int& x, int& y);
// Swipe in portrait logical coords (check before tap).
bool boardPollSwipe(int& x0, int& y0, int& x1, int& y1);
bool boardTouchHeld(int& x, int& y);

// BOOT / power key (GPIO0 on LilyGO T5 S3 Pro) — power / BOOT key on this board.
bool boardPowerPressed();
unsigned long boardPowerHeldMs();
