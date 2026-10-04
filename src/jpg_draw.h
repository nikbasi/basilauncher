#pragma once

#include <cstdint>

// Draw a baseline JPEG centered/cropped to the canvas (1-bpp dithered).
// Progressive JPEGs are not supported by TJpgDec.
using JpgAbortCheck = bool (*)();
bool jpgDrawFile(const char* path, JpgAbortCheck abortCheck = nullptr);

// Baseline JPEG into an 8-bit gray buffer. width/height are the buffer size.
// Returns false for a missing file, a progressive JPEG, or a picture larger than the buffer.
bool jpgReadGray(const char* path, uint8_t* gray, int width, int height);
