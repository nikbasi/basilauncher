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

void canvasSetPixel(int x, int y, bool black);
void canvasFillRect(int x, int y, int w, int h, bool black);
void canvasDrawRect(int x, int y, int w, int h, bool black);
void canvasDrawRoundRect(int x, int y, int w, int h, int r, bool black);
void canvasFillRoundRect(int x, int y, int w, int h, int r, bool black);
void canvasDrawLine(int x0, int y0, int x1, int y1, bool black);

// 8x16 ASC16 glyphs. scale=1 → 8×16px, scale=2 → 16×32px.
void canvasDrawString(int x, int y, const char* text, bool black = true, int scale = 1);
int canvasTextWidth(const char* text, int scale = 1);
int canvasTextHeight(int scale = 1);

// Panel-native normalized touch (0..1 landscape) → portrait logical pixels.
void canvasTouchToLogical(float nx, float ny, int& x, int& y);
