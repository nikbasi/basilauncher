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

struct SlotInfo {
  int index = 0;
  char label[2] = {'A', 0};  // A..D
  bool occupied = false;
  size_t capacity = 0;  // partition size bytes
  size_t size = 0;      // installed image size (label)
  std::string name;     // file it was installed from, or "Empty"
  // What to call it: the image's own app description when the build named
  // itself, otherwise parsed from the file name. version may be empty.
  std::string title;
  std::string version;
  // A newer build of the same app in /firmware that fits this slot
  // (filled by appsFindUpdates; updatePath empty when there is none).
  std::string updatePath;
  std::string updateVersion;
  size_t updateSize = 0;
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
// Look in /firmware for newer builds of the installed apps. Reads a few
// hundred bytes of each .bin there, so call it when the list is (re)shown,
// not every frame.
void appsFindUpdates(SlotInfo slots[kSlotCount]);
// Slot booted most recently, or -1.
int appsLastSlot();
void appsSetLastSlot(int slotIndex);
FlashSpace appsFlashSpace();

// Smallest empty slot that can hold `bytes`, or -1.
int appsBestFitSlot(size_t bytes);

// List directories + files in `dirPath` (absolute, e.g. "/" or "/firmware").
// Directories first (A-Z), then files (A-Z). Skips "." / ".." and junk.
std::vector<DirEntry> appsScanDir(const char* dirPath);
// Parent of absolute path, or "/" for root. Never empty.
void appsParentDir(const char* path, char* out, size_t outLen);
bool appsIsRootDir(const char* path);

void appsFormatBytes(size_t bytes, char* buf, size_t bufLen);
