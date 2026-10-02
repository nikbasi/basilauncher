#pragma once

#include <stdint.h>

// Draw a random BMP from /sleep or /.sleep (same folders many readers use), then
// deep-sleep. Falls back to a plain white frame if none are found.
// quiet=true skips the "Entering sleep..." toast (used on debounce / short-press re-sleep).
void enterSleepWithScreensaver(bool quiet = false);

// Call early in setup(). BOOT wakes from deep sleep; hold for `needMs` to stay
// awake. A short press (or USB glitch pulse) picks another sleep image and
// re-sleeps quietly.
void sleepRequireBootHoldToWake(uint32_t needMs = 1500);
