#pragma once

// Draw a 24-bit uncompressed BMP centered/cropped to the canvas.
// Returns false if the file is missing, unsupported, or abortCheck() returns true.
using BmpAbortCheck = bool (*)();
bool bmpDrawFile(const char* path, BmpAbortCheck abortCheck = nullptr);
