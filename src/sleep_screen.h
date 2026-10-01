#pragma once

#include <stdint.h>

// Draw a random BMP from /sleep or /.sleep (same folders many readers use), then
// deep-sleep. Falls back to a plain white frame if none are found.
// quiet=true skips the "Entering sleep..." toast (used on debounce re-sleep).
void enterSleepWithScreensaver(bool quiet = false);

// Call early in setup(). BOOT wakes from deep sleep; a brief press stays awake
// (USB glitches that only pulse GPIO0 are filtered and re-sleep quietly).
void sleepRequireBootHoldToWake(uint32_t needMs = 1500);
