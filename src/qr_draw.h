#pragma once

// Draw a QR code into the canvas (black modules on white). Payload must be short.
// Returns false if encoding fails.
bool canvasDrawQr(int x, int y, int maxSize, const char* payload);
