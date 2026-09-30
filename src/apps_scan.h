#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Guest app slots only (never factory / launcher).
constexpr int kSlotCount = 4;

struct DirEntry {
  std::string name;  // basename for display
  std::string path;  // absolute SD path
  size_t size = 0;
  bool isDir = false;
};

struct FirmwareFile {
  std::string name;
  std::string path;
  size_t size = 0;
};

struct SlotInfo {
  int index = 0;
  char label[2] = {'A', 0};  // A..D
  bool occupied = false;
  size_t capacity = 0;  // partition size bytes
  size_t size = 0;      // installed image size (label)
  std::string name;
};

struct FlashSpace {
  size_t guestTotal = 0;  // sum of all ota_* capacities
  size_t guestUsed = 0;   // sum of occupied partition capacities
  size_t guestFree = 0;   // sum of empty partition capacities
  int emptySlots = 0;
  int occupiedSlots = 0;
};

void appsLoadSlotLabels();
void appsSaveSlotLabel(int slotIndex, const char* name, size_t size);
void appsClearSlotLabel(int slotIndex);
SlotInfo appsSlotInfo(int slotIndex);
void appsAllSlots(SlotInfo out[kSlotCount]);
FlashSpace appsFlashSpace();

// Smallest empty slot that can hold `bytes`, or -1.
int appsBestFitSlot(size_t bytes);
// How many empty slots can hold `bytes`.
int appsFittingEmptySlots(size_t bytes, int* outIndices, int maxOut);

// List directories + files in `dirPath` (absolute, e.g. "/" or "/firmware").
// Directories first (A-Z), then files (A-Z). Skips "." / ".." and junk.
std::vector<DirEntry> appsScanDir(const char* dirPath);
// Parent of absolute path, or "/" for root. Never empty.
void appsParentDir(const char* path, char* out, size_t outLen);
bool appsIsRootDir(const char* path);

std::vector<FirmwareFile> appsScanFirmwareDir();  // legacy: /firmware *.bin only

void appsFormatBytes(size_t bytes, char* buf, size_t bufLen);
