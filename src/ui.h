#pragma once

#include "apps_scan.h"
#include <vector>

enum class Screen { Home, Picker, Settings, Progress, Message };

struct UiHit {
  enum class Kind {
    None,
    BootSlot,
    ClearSlot,
    AssignSlot,   // empty slot → open picker for that slot
    OpenPicker,   // dock: best-fit install
    Settings,
    Back,
    PowerOff,
    PickFile,
    ScrollUp,
    ScrollDown,
  };
  Kind kind = Kind::None;
  int index = -1;
};

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space);
void uiDrawPicker(const std::vector<FirmwareFile>& files, int scroll, int targetSlot,
                  size_t maxBytes, const FlashSpace& space);
void uiDrawSettings(const FlashSpace& space);
void uiDrawProgress(const char* title, int percent);
void uiDrawMessage(const char* title, const char* body);

UiHit uiHitHome(int x, int y);
UiHit uiHitPicker(int x, int y, int fileCount, int scroll);
UiHit uiHitSettings(int x, int y);

int uiPickerMaxScroll(int fileCount);
