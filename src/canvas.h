#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>

// Logical portrait UI. FreeInk framebuffer is native landscape 960x540;
// canvas maps logical → physical with a 90° CW portrait transform
// (90° CW: phyX=ly, phyY=539-lx). Matches how the reader holds the T5
// (USB / home button at the bottom).
constexpr int kScreenW = 540;
constexpr int kScreenH = 960;

extern EInkDisplay display;

struct CanvasRect {
  int x = 0;
  int y = 0;
  int w = kScreenW;
  int h = kScreenH;
};

enum class CanvasRefreshIntent : uint8_t {
  InteractiveLocal,
  InteractiveViewport,
  Navigation,
  Progress,
  StaticQuality,
  Sleep,
};

void canvasBegin();
void canvasClear();
void canvasPresent(EInkDisplay::RefreshMode mode = EInkDisplay::HALF_REFRESH);
// FAST by default; inserts a HALF scrub every few frames (and when requested).
void canvasPresentAuto();
// Activity-aware presentation. Interactive intents paint FAST immediately and
// schedule one clean resting frame after the burst. Rectangles are logical
// portrait coordinates and are converted to the panel's native orientation.
void canvasPresentFor(CanvasRefreshIntent intent, CanvasRect dirty = {});
// Paint is already in the 1-bit framebuffer. Queue a fast window and return
// without waiting for the waveform. A later call replaces one still waiting.
void canvasPresentWindowFast(CanvasRect dirty);
// The framebuffer already shows this key inverted. Copy it now if the panel is
// idle, then the caller restores the buffer. The matching normal frame is
// pushed later and is not replaced by a text-field window.
bool canvasFlashInvertedKey(CanvasRect dirty);
void canvasArmKeyRestore(CanvasRect dirty);
// After this rectangle stops changing, scrub just that rectangle.
void canvasArmLocalClean(CanvasRect dirty, uint32_t delayMs);
void canvasDisarmLocalClean();
bool canvasLocalCleanArmed();
void canvasServiceRefresh();
void canvasCancelPendingClean();
// While held, canvasPresentAuto never promotes to HALF — for text entry where
// a mid-type scrub is worse than temporary ghosting (scrub on exit instead).
void canvasSetHoldCleanRefresh(bool hold);
void canvasRequestCleanRefresh();
void canvasSetCleanEvery(int n);  // 1..30 full-screen-equivalents of FAST work
int canvasCleanEvery();

// Black → white flash so the next redraw/FULL can dig out ghosts that a plain
// clean bank would skip on already-white pixels.
void canvasNuclearFlash();

void canvasSetPixel(int x, int y, bool black);
void canvasFillRect(int x, int y, int w, int h, bool black);
void canvasDrawRect(int x, int y, int w, int h, bool black);
void canvasDrawRoundRect(int x, int y, int w, int h, int r, bool black);
void canvasFillRoundRect(int x, int y, int w, int h, int r, bool black);
// Flip the 1-bit pixels inside the same rounded shape canvasFillRoundRect paints.
void canvasInvertRoundRect(int x, int y, int w, int h, int r);
// True grayscale is reserved for stable/refined frames. Interactive FAST
// frames leave these pixels white, avoiding a high-churn 1-bit dither pattern.
void canvasFillGrayRect(int x, int y, int w, int h, bool light = true);
void canvasFillGrayRoundRect(int x, int y, int w, int h, int r, bool light = true);
void canvasDrawLine(int x0, int y0, int x1, int y1, bool black);

// 8x16 ASC16 glyphs, scaled. scale=1 uses the UI text-size setting (body).
// scale=2 is the title size (also follows the setting).
// bold smears body glyphs one pixel right. Leave it off for scale 2.
void canvasDrawString(int x, int y, const char* text, bool black = true, int scale = 1, bool bold = false);
int canvasTextWidth(const char* text, int scale = 1);
int canvasTextHeight(int scale = 1);

// Copy the current 1-bit frame into PSRAM. The viewer chips are drawn after this,
// so the copy stays a clean photo. Restore copies it back without freeing it.
bool canvasCaptureFrame();
void canvasReleaseCapture();
bool canvasHasCapture();
const uint8_t* canvasCapturedFrame();
size_t canvasCapturedBytes();
void canvasRestoreCapture();
// Drop grayscale masks so a restored black/white frame is not composited with old gray.
void canvasDiscardGray();

// UI text size: 0=Small, 1=Medium (default), 2=Large. Affects scale 1 and 2.
void canvasSetUiTextSize(int level);
int canvasUiTextSize();
int canvasBodyCellW();  // pixel width of one body glyph at current size
int canvasBodyCellH();

// Panel-native normalized touch (0..1 landscape) → portrait logical pixels.
void canvasTouchToLogical(float nx, float ny, int& x, int& y);
