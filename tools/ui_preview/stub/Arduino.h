#pragma once
// Host stand-in for the bits of Arduino.h the UI code uses.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

struct HostSerial {
  template <class... A>
  void printf(const char*, A...) {}
  void println(const char*) {}
};
extern HostSerial Serial;

#ifndef BASILAUNCHER_VERSION
#define BASILAUNCHER_VERSION "preview"
#endif
