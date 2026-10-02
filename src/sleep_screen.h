#pragma once

#include <stdint.h>

// Draw a random BMP/JPEG from /sleep or /.sleep (same folders many readers use), then
// deep-sleep. Falls back to a plain white frame if none are found.
// quiet=true skips the "Entering sleep..." toast (used on debounce / short-press re-sleep).
// Returns false if BOOT is held long enough during entry so wake can take priority
// (never returns on a successful deep-sleep).
bool enterSleepWithScreensaver(bool quiet = false);

// Call at the start of setup(), before display and SD init. BOOT wakes from
// deep sleep; hold for `needMs` measured from the wake reset to stay awake.
// Returns false when the press was released early: finish display init, then
// call enterSleepWithScreensaver(true) so a tap still cycles the sleep image.
bool sleepBootHoldKeepsAwake(uint32_t needMs = 600);

// True when this boot was caused by the BOOT/ext1 wake pin (deep-sleep wake).
bool sleepWokeFromBootButton();

// If BOOT is held for needMs, returns true (caller should stay awake).
bool sleepTryAbortForBootHold(uint32_t needMs = 600);
