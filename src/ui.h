#pragma once

#include "apps_scan.h"
#include <vector>

enum class Screen { Home, Picker, Settings, Shade, Progress, Message };

struct UiHit {
  enum class Kind {
    None,
    BootSlot,
    ClearSlot,
    AssignSlot,
    OpenPicker,
    Settings,
    Back,
    PowerOff,
    PickFile,
    EnterDir,
    GoUp,
    ScrollUp,
    ScrollDown,
    OpenShade,
    CloseShade,
    BrightnessMinus,
    BrightnessPlus,
    BrightnessSlider,
    LightToggle,
    HourMinus,
    HourPlus,
    MinuteMinus,
    MinutePlus,
  };
  Kind kind = Kind::None;
  int index = -1;
  int value = -1;  // e.g. brightness from slider
};

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space);
void uiDrawPicker(const std::vector<DirEntry>& entries, int scroll, int targetSlot,
                  size_t maxBytes, const char* currentPath, const FlashSpace& space);
void uiDrawSettings(const FlashSpace& space);
void uiDrawShade(const FlashSpace& space);
void uiDrawProgress(const char* title, int percent);
void uiDrawMessage(const char* title, const char* body);

UiHit uiHitHome(int x, int y);
UiHit uiHitPicker(int x, int y, int entryCount, int scroll, bool canGoUp);
UiHit uiHitSettings(int x, int y);
UiHit uiHitShade(int x, int y);

// Brightness track geometry for live drag.
void uiShadeBrightnessTrack(int& x, int& y, int& w, int& h);
int uiBrightnessFromTouchX(int touchX);

int uiPickerRowHeight();
int uiPickerVisibleRows();
