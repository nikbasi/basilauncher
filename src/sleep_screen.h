#pragma once

#include <stdint.h>

// Show the pinned screensaver, or the next photo in /sleep and /.sleep, then
// deep-sleep. A short BOOT tap passes quiet=true and advances to the next file.
// The finished frame is stored at /sleep/.frame so the same photo is not decoded
// again. Falls back to a plain white frame if none are found.
// Returns false if BOOT is held long enough during entry so wake can take priority
// (never returns on a successful deep-sleep).
bool enterSleepWithScreensaver(bool quiet = false);

// Write the clean photo snapshot (taken before the viewer chips) as /sleep/.frame.
bool sleepSaveCapturedFrame(const char* sourcePath);

// Call at the start of setup(), before display and SD init. BOOT wakes from
// deep sleep; hold for `needMs` measured from the wake reset to stay awake.
// Returns false when the press was released early: finish display init, then
// call enterSleepWithScreensaver(true) so a tap still cycles the sleep image.
bool sleepBootHoldKeepsAwake(uint32_t needMs = 600);

// True when this boot was caused by the BOOT/ext1 wake pin (deep-sleep wake).
bool sleepWokeFromBootButton();

// If BOOT is held for needMs, returns true (caller should stay awake).
bool sleepTryAbortForBootHold(uint32_t needMs = 600);
