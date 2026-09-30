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

FlashResult flashValidateAndWrite(const char* sdPath, int slotIndex, FlashProgressCb cb, void* ctx);
FlashResult flashEraseSlot(int slotIndex);
FlashResult bootSlotPendingVerify(int slotIndex);
const char* flashResultName(FlashResult r);
size_t flashSlotCapacity(int slotIndex);
