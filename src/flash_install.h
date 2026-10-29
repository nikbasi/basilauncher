#pragma once

#include <cstddef>
#include <cstdint>

enum class FlashResult {
  Ok,
  OpenFail,
  TooSmall,
  TooLarge,
  BadMagic,
  NoPartition,
  EraseFail,
  WriteFail,
  ReadFail,
  BootFail,
  Forbidden,  // refused: would touch factory / launcher
  Occupied,   // refused: slot already has an app — Clear first
};

using FlashProgressCb = void (*)(size_t written, size_t total, void* ctx);

// allowReplace: overwrite an installed app (an update the user confirmed).
// The file is still checked before the slot is erased, so a bad file leaves
// the old app in place.
FlashResult flashValidateAndWrite(const char* sdPath, int slotIndex, FlashProgressCb cb, void* ctx,
                                  bool allowReplace = false);
FlashResult flashEraseSlot(int slotIndex);
FlashResult bootSlotPendingVerify(int slotIndex);
const char* flashResultName(FlashResult r);
