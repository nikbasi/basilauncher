#pragma once

// Draw a baseline JPEG centered/cropped to the canvas (1-bpp dithered).
// Progressive JPEGs are not supported by TJpgDec.
using JpgAbortCheck = bool (*)();
bool jpgDrawFile(const char* path, JpgAbortCheck abortCheck = nullptr);
