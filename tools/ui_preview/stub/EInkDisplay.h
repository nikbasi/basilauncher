#pragma once
// Host stand-in for the FreeInk display facade: a 960x540 1-bit framebuffer
// and refresh calls that do nothing. The preview reads the framebuffer.

#include <cstddef>
#include <cstdint>
#include <cstring>

#define FREEINK_HAS_DEFERRED_WINDOW 1
#define FREEINK_HAS_DEFERRED_FRAME 1

class EInkDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  EInkDisplay(int, int, int, int, int, int) {}

  void begin() {}
  void clearScreen(uint8_t color) { memset(fb_, color, sizeof(fb_)); }
  uint8_t* getFrameBuffer() { return fb_; }
  size_t getBufferSize() const { return sizeof(fb_); }
  uint16_t getDisplayWidthBytes() const { return 960 / 8; }

  void displayBuffer(RefreshMode, bool) {}
  void displayWindow(int, int, int, int, bool) {}
  bool displayWindowDeferred(int, int, int, int, RefreshMode) { return true; }
  bool displayFrameDeferred(RefreshMode, bool = false) { return true; }
  bool windowRefreshBusy() const { return false; }
  bool supportsGrayFrame() const { return true; }
  void copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
  void displayGrayscaleFrame(RefreshMode, bool) {}

 private:
  uint8_t fb_[960 / 8 * 540] = {};
};
