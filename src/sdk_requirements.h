#pragma once

// FreeInk SDK features this launcher depends on.
//
// Several of them came from the aurora line or are T5-specific fixes that
// upstream FreeInk does not carry, so an SDK update can drop one without any
// call site failing to compile. Each one has a marker in the SDK; a missing or
// changed marker stops the build here, before a firmware with a slower or
// ghostier panel can ship. scripts/check_sdk.py checks what a macro cannot
// (the waveform tables and the clean-refresh normalize).
//
// If the SDK changed one on purpose, update freeink-sdk/docs/launcher-features.md
// and this file in the same change.

#include <BoardT5S3.h>
#include <ED047TC2Waveform.h>
#include <EInkDisplay.h>
#include <InputManager.h>

#if !defined(FREEINK_HAS_DEFERRED_WINDOW) || FREEINK_HAS_DEFERRED_WINDOW != 1
#error "freeink-sdk is missing displayWindowDeferred(): drags and key flashes would block touch. See freeink-sdk/docs/launcher-features.md."
#endif

#if !defined(FREEINK_HAS_DEFERRED_FRAME) || FREEINK_HAS_DEFERRED_FRAME != 1
#error "freeink-sdk is missing displayFrameDeferred(): clean refreshes would block touch. See freeink-sdk/docs/launcher-features.md."
#endif

#if !defined(FREEINK_HAS_QUEUED_LONG_PRESS) || FREEINK_HAS_QUEUED_LONG_PRESS != 1
#error "freeink-sdk is missing queued long presses: a hold during a refresh would be dropped. See freeink-sdk/docs/launcher-features.md."
#endif

#if !defined(FREEINK_ED047TC2_CLEAN_FRAMES_PER_DRIVE) || FREEINK_ED047TC2_CLEAN_FRAMES_PER_DRIVE != 2
#error "freeink-sdk is missing aurora's 2*L15 clean bank: every clean refresh would be a third slower. See freeink-sdk/docs/launcher-features.md."
#endif

#if !defined(FREEINK_T5S3_STAGED_RAIL_PARK) || FREEINK_T5S3_STAGED_RAIL_PARK != 1
#error "freeink-sdk is missing the staged T5 rail park: settled pages would fade to grey. See freeink-sdk/docs/launcher-features.md."
#endif

#if !defined(FREEINK_T5S3_WAVEFORM_FLOOR_C) || FREEINK_T5S3_WAVEFORM_FLOOR_C != 26
#error "freeink-sdk is missing the T5 waveform floor: warm panels would not latch. See freeink-sdk/docs/launcher-features.md."
#endif
