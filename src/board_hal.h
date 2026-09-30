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
