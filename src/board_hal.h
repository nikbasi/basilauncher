#pragma once

#include <Arduino.h>

struct BoardPowerInfo {
  bool known = false;
  int percent = -1;  // 0..100
  bool charging = false;
};

struct BoardClockInfo {
  bool valid = false;
  char time[8] = "--:--";   // HH:MM
  char date[12] = "";       // YYYY-MM-DD
  uint8_t minute = 255;     // for change detection
};

bool boardInit();
bool boardInitDisplay();
bool boardInitTouch();
bool boardInitSd();
void boardInitPower();
void boardInitClock();
void boardMarkFactoryValid();
void boardPrepareDeepSleep();

BoardPowerInfo boardPower();
BoardClockInfo boardClock();
int boardBatteryPercent();  // -1 if unknown
bool boardSdOk();
bool boardTouchOk();

// Returns true once per completed tap with portrait logical coordinates.
bool boardPollTouch(int& x, int& y);
