#pragma once

#include <stdint.h>

// Draw a random BMP from /sleep or /.sleep (same folders Aurora uses), then
// deep-sleep. Falls back to a plain "Sleeping" screen if none are found.
// quiet=true skips the "Entering sleep..." toast (used when a short BOOT
// click woke us and we immediately go back to sleep).
void enterSleepWithScreensaver(bool quiet = false);

// Hardware wakes on any BOOT edge; call early in setup() so only a hold
// keeps the device awake. Short click → quiet re-sleep (does not return).
void sleepRequireBootHoldToWake(uint32_t needMs = 1500);
