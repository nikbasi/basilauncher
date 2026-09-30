#pragma once

// Draw a 24-bit uncompressed BMP centered/cropped to the canvas.
// Returns false if the file is missing or unsupported.
bool bmpDrawFile(const char* path);
