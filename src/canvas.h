#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>

// Logical portrait UI. FreeInk framebuffer is native landscape 960x540;
// canvas maps logical → physical with CrossPoint's Portrait transform
// (90° CW: phyX=ly, phyY=539-lx). Matches how the reader holds the T5
// (USB / home button at the bottom).
constexpr int kScreenW = 540;
constexpr int kScreenH = 960;

extern EInkDisplay display;

void canvasBegin();
void canvasClear();
void canvasPresent(EInkDisplay::RefreshMode mode = EInkDisplay::HALF_REFRESH);
// FAST by default; inserts a HALF scrub every few frames (and when requested).
void canvasPresentAuto();
void canvasRequestCleanRefresh();
void canvasSetCleanEvery(int n);  // 1..30 frames between clean scrubs
int canvasCleanEvery();

void canvasSetPixel(int x, int y, bool black);
void canvasFillRect(int x, int y, int w, int h, bool black);
void canvasDrawRect(int x, int y, int w, int h, bool black);
void canvasDrawRoundRect(int x, int y, int w, int h, int r, bool black);
void canvasFillRoundRect(int x, int y, int w, int h, int r, bool black);
void canvasDrawLine(int x0, int y0, int x1, int y1, bool black);

// 8x16 ASC16 glyphs, scaled. scale=1 uses the UI text-size setting (body).
// scale=2 is the title size (also follows the setting).
void canvasDrawString(int x, int y, const char* text, bool black = true, int scale = 1);
int canvasTextWidth(const char* text, int scale = 1);
int canvasTextHeight(int scale = 1);

// UI text size: 0=Small, 1=Medium (default), 2=Large. Affects scale 1 and 2.
void canvasSetUiTextSize(int level);
int canvasUiTextSize();
int canvasBodyCellW();  // pixel width of one body glyph at current size
int canvasBodyCellH();

// Panel-native normalized touch (0..1 landscape) → portrait logical pixels.
void canvasTouchToLogical(float nx, float ny, int& x, int& y);
