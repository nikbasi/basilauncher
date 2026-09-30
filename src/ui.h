#pragma once

#include "apps_scan.h"
#include <vector>

enum class Screen { Home, Settings, Progress, Message };

struct UiHit {
  enum class Kind {
    None,
    BootSlot,
    ClearSlot,
    InstallFile,
    Settings,
    Back,
    PowerOff,
  };
  Kind kind = Kind::None;
  int index = -1;
};

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space,
                const std::vector<FirmwareFile>& files, int scroll);
void uiDrawSettings(const FlashSpace& space);
void uiDrawProgress(const char* title, int percent);
void uiDrawMessage(const char* title, const char* body);

UiHit uiHitHome(int x, int y, int fileCount, int scroll);
UiHit uiHitSettings(int x, int y);

int uiHomeMaxScroll(int fileCount);
int uiHomeListTopY();
