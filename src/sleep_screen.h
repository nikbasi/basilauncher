#pragma once

#include <stdint.h>

// Draw a random BMP from /sleep or /.sleep (same folders many readers use), then
// deep-sleep. Falls back to a plain white frame if none are found.
// quiet=true skips the "Entering sleep..." toast (used on debounce / short-press re-sleep).
void enterSleepWithScreensaver(bool quiet = false);

// Call at the start of setup(), before display and SD init. BOOT wakes from
// deep sleep; hold for `needMs` measured from the wake reset to stay awake.
// Returns false when the press was released early: finish display init, then
// call enterSleepWithScreensaver(true) so a tap still cycles the sleep image.
bool sleepBootHoldKeepsAwake(uint32_t needMs = 600);
