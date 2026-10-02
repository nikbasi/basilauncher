#pragma once

#include "apps_scan.h"
#include <vector>

enum class Screen {
  Home,
  Explorer,
  Settings,
  Shade,
  Progress,
  Message,
  ImageView,
  TextEdit,
  Confirm,
};

enum class ExplorerMode { Browse, Install };

enum class TextEditMode { EditFile, Rename, NewFolder, NewFile };

struct UiHit {
  enum class Kind {
    None,
    BootSlot,
    ClearSlot,
    AssignSlot,
    OpenFiles,
    Settings,
    Back,
    PowerOff,
    SelectEntry,
    ExplorerOpen,
    ExplorerMore,
    ExplorerCopy,
    ExplorerCut,
    ExplorerPaste,
    ExplorerRename,
    ExplorerDelete,
    ExplorerNew,
    ExplorerNewFile,
    ExplorerSheetDismiss,
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
    CleanEveryMinus,
    CleanEveryPlus,
    ScrubNow,
    SleepAfterMinus,
    SleepAfterPlus,
    FontSizeMinus,
    FontSizePlus,
    YearMinus,
    YearPlus,
    MonthMinus,
    MonthPlus,
    DayMinus,
    DayPlus,
    KeyChar,     // value = character
    KeyBackspace,
    KeyShift,
    KeySymbols,
    KeySpace,
    KeyDone,
    KeyCancel,
    ConfirmYes,
    ConfirmNo,
  };
  Kind kind = Kind::None;
  int index = -1;
  int value = -1;  // e.g. brightness from slider, or key char
};

// Centered brand splash shown once at boot before the home UI.
void uiDrawSplash();

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space);

struct ExplorerDrawState {
  ExplorerMode mode = ExplorerMode::Browse;
  int targetSlot = -1;  // Install mode: 0..3 or -1 best-fit
  size_t maxBytes = 0;
  int scroll = 0;
  int selected = -1;
  const std::vector<int>* selectedIndices = nullptr;
  bool multiSelect = false;
  bool sheetOpen = false;  // action sheet overlay
  bool clipboardHas = false;
  bool clipboardCut = false;
  size_t clipboardCount = 0;
  const char* currentPath = "/";
};

void uiDrawExplorer(const std::vector<DirEntry>& entries, const ExplorerDrawState& st,
                    const FlashSpace& space);
// Incremental list/dock redraw for selection and scroll changes.
void uiRedrawExplorerViewport(const std::vector<DirEntry>& entries, const ExplorerDrawState& st);

void uiDrawSettings(const FlashSpace& space);
void uiDrawShade(const FlashSpace& space);
void uiRedrawShadeControls(const FlashSpace& space);
void uiRedrawHomeStatus(const FlashSpace& space);
void uiDrawProgress(const char* title, int percent);
void uiDrawMessage(const char* title, const char* body);
void uiDrawConfirm(const char* title, const char* body);
void uiDrawImageViewHint();  // overlay tip after BMP drawn
void uiDrawTextEdit(const char* title, const char* text, bool symbols, bool shift,
                    TextEditMode mode, bool scrub = false);
// Fast path: rewrite only the text field in the existing framebuffer (keyboard
// unchanged) and FAST-present. Call after char/space/backspace when layout is
// unchanged.
void uiRedrawTextEditField(const char* text, TextEditMode mode);

UiHit uiHitHome(int x, int y);
UiHit uiHitExplorer(int x, int y, int entryCount, int scroll, bool canGoUp, bool sheetOpen,
                    bool clipboardHas, int selectedCount);
UiHit uiHitSettings(int x, int y);
UiHit uiHitShade(int x, int y);
UiHit uiHitConfirm(int x, int y);
UiHit uiHitTextEdit(int x, int y, bool symbols, bool shift);

// Brightness track geometry for live drag.
void uiShadeBrightnessTrack(int& x, int& y, int& w, int& h);
int uiBrightnessFromTouchX(int touchX);

int uiExplorerRowHeight();
int uiExplorerVisibleRows();
