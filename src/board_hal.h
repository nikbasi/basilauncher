#pragma once

#include <Arduino.h>

bool boardInit();
bool boardInitDisplay();
bool boardInitTouch();
bool boardInitSd();
void boardMarkFactoryValid();
void boardPrepareDeepSleep();
int boardBatteryPercent();  // -1 if unknown
bool boardSdOk();
bool boardTouchOk();

// Returns true once per completed tap with portrait logical coordinates.
bool boardPollTouch(int& x, int& y);
