#pragma once

#include "apps_scan.h"
#include "wifi_session.h"
#include <vector>

enum class Screen {
  Home,
  Explorer,
  Settings,
  Hardware,
  Wifi,
  Net,
  Web,
  Gps,
  Shade,
  Progress,
  Message,
  ImageView,
  TextEdit,
  Confirm,
};

enum class ExplorerMode { Browse, Install };

enum class TextEditMode { EditFile, Rename, NewFolder, NewFile, ApPassword, StaPassword, WebUrl, WebField };

struct UiHit {
  enum class Kind {
    None,
    BootSlot,
    ClearSlot,
    AssignSlot,
    OpenFiles,
    Settings,
    Hardware,
    Wifi,
    Net,
    NetScan,
    NetJoin,
    NetDisconnect,
    WebOpen,
    SetSleep,
    WifiStartAp,
    WifiStop,
    WifiChangePass,
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
    OpenGps,
    GpsZoomIn,
    GpsZoomOut,
    GpsRecenter,
  };
  Kind kind = Kind::None;
  int index = -1;
  int value = -1;  // e.g. brightness from slider, or key char
  // Visual control that accepted the tap. Empty when the tap should not flash.
  // rr is the corner radius of that control, matching how it was drawn.
  int rx = 0;
  int ry = 0;
  int rw = 0;
  int rh = 0;
  int rr = 0;
};

// Centered brand splash shown once at boot before the home UI.
void uiDrawSplash();

void uiDrawHome(const SlotInfo slots[kSlotCount], const FlashSpace& space, bool stable = false);

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
void uiDrawHardware(const FlashSpace& space);
void uiDrawWifi(const FlashSpace& space, const char* statusLine, const char* ssidLine,
                const char* urlLine, const char* detailLine, bool active);
void uiDrawShade(const FlashSpace& space);
void uiRedrawShadeControls(const FlashSpace& space);
void uiRedrawHomeStatus(const FlashSpace& space);
void uiRedrawStatusBar(const FlashSpace& space, bool showClosedGrabber);
void uiDrawProgress(const char* title, int percent);
void uiDrawMessage(const char* title, const char* body);
// OK button rect when the tap lands on it; kind stays None either way.
UiHit uiHitMessage(int x, int y);
void uiDrawConfirm(const char* title, const char* body);
void uiDrawImageViewHint(bool saved = false);  // Close / Set sleep chips after the photo
UiHit uiHitImageView(int x, int y);
void uiDrawTextEdit(const char* title, const char* text, bool symbols, bool shift,
                    TextEditMode mode, bool scrub = false);
// Fast path: rewrite only the text box in the existing framebuffer (keyboard
// unchanged) and queue a fast window. Returns without waiting for the panel.
// Call after char/space/backspace when layout is unchanged.
void uiRedrawTextEditField(const char* text, TextEditMode mode);

UiHit uiHitHome(int x, int y);
UiHit uiHitExplorer(int x, int y, int entryCount, int scroll, bool canGoUp, bool sheetOpen,
                    bool clipboardHas, int selectedCount);
UiHit uiHitSettings(int x, int y);
UiHit uiHitHardware(int x, int y);
void uiDrawGps();
// Slide the map with the finger. The waveform may lag; the pixels do not wait
// on a card read.
void uiFollowGpsMap(int dx, int dy);
// Tiles and a full-frame clean once the finger is up.
void uiSettleGpsMap();
// Map content changed (a new fix, or a scroll that had no cached pane).
void uiRedrawGpsMap();
// Update fix and satellite lines. Set present=false when a map refresh will
// present the completed framebuffer in the same loop.
void uiRedrawGpsStatus(bool present = true);
UiHit uiHitGps(int x, int y);
bool uiGpsMapContains(int x, int y);
bool uiGpsMapOffsetFromCenter(int x, int y, int& offsetX, int& offsetY);
UiHit uiHitWifi(int x, int y, bool active);
void uiDrawNet(const FlashSpace& space, const WifiStatus& status, const WifiAp* aps, int count);
UiHit uiHitNet(int x, int y);
UiHit uiHitShade(int x, int y);
UiHit uiHitConfirm(int x, int y);
UiHit uiHitTextEdit(int x, int y, bool symbols, bool shift);

// Paint the hit control black and push only that patch. The following redraw
// replaces it. Empty rects and near-full patches are ignored.
void uiAcknowledgePress(const UiHit& hit);
// Invert one on-screen key and queue it back to normal. Does not wait.
void uiFlashKey(const UiHit& hit);

// Brightness track geometry for live drag.
void uiShadeBrightnessTrack(int& x, int& y, int& w, int& h);
int uiBrightnessFromTouchX(int touchX);

int uiExplorerRowHeight();
int uiExplorerVisibleRows();
