#pragma once

#include <Arduino.h>

struct BoardPowerInfo {
  bool known = false;
  int percent = -1;  // 0..100
  bool charging = false;
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

// E-ink scrub cadence (1 = every frame, higher = fewer flashes). Persisted.
int boardCleanEvery();
void boardSetCleanEvery(int n);

// Minutes of idle before auto-sleep (0 = never). Persisted.
int boardSleepAfterMin();
void boardSetSleepAfterMin(int minutes);

BoardPowerInfo boardPower();
BoardClockInfo boardClock();
bool boardAdjustClockMinutes(int deltaMinutes);
bool boardSetClock(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

int boardBrightness();          // 0..100 saved level
bool boardFrontlightOn();
void boardSetBrightness(int percent);  // also turns on if >0
void boardSetFrontlightOn(bool on);
bool boardHasFrontlight();

int boardBatteryPercent();
bool boardSdOk();
bool boardTouchOk();

// Call once per loop before poll helpers.
void boardInputUpdate();
// Tap in portrait logical coords (after boardInputUpdate).
bool boardPollTouch(int& x, int& y);
// Swipe in portrait logical coords (check before tap).
bool boardPollSwipe(int& x0, int& y0, int& x1, int& y1);
bool boardTouchHeld(int& x, int& y);
void boardSuppressTouch();

// BOOT / power key (GPIO0 on LilyGO T5 S3 Pro) — same physical key Aurora uses.
bool boardPowerPressed();
unsigned long boardPowerHeldMs();
