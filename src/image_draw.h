#pragma once

// Draw BMP or JPEG centered/cropped onto the canvas.
using ImageAbortCheck = bool (*)();
bool imageDrawFile(const char* path, ImageAbortCheck abortCheck = nullptr);
bool imageIsSupportedName(const char* name);
