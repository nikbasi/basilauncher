// Taps at fixed points on each screen (Medium text) and checks the hit test
// lands on the control drawn there. Drawing and hit-testing are separate
// code in ui.cpp; this catches them drifting apart. Run through run.sh.
#include "../../src/canvas.cpp"
#include "apps_scan.h"
#include "ui.h"
#include "wifi_session.h"
#include <cstdio>
#include <vector>
using K = UiHit::Kind;
int fails = 0;
void expect(const char* what, const UiHit& h, K k, int idx = -999) {
  const bool ok = h.kind == k && (idx == -999 || h.index == idx);
  if (!ok) { ++fails; printf("FAIL %-28s kind=%d index=%d (want %d/%d)\n", what, (int)h.kind, h.index, (int)k, idx); }
}
int main() {
  canvasBegin();
  SlotInfo slots[kSlotCount];
  for (int i = 0; i < kSlotCount; ++i) slots[i] = appsSlotInfo(i);
  const FlashSpace sp = appsFlashSpace();
  uiDrawHome(slots, sp, true, 0);
  expect("home status", uiHitHome(270, 20), K::OpenShade);
  expect("home resume", uiHitHome(270, 120), K::ResumeLast, 0);
  expect("home slot A card", uiHitHome(200, 300), K::BootSlot, 0);
  expect("home slot A more", uiHitHome(490, 295), K::SlotMenuOpen, 0);
  expect("home slot B", uiHitHome(200, 440), K::BootSlot, 1);
  expect("home slot D empty right", uiHitHome(490, 740), K::BootSlot, 3);
  expect("home files", uiHitHome(98, 921), K::OpenFiles);
  expect("home web", uiHitHome(270, 921), K::WebOpen);
  expect("home settings", uiHitHome(442, 921), K::Settings);
  uiDrawHome(slots, sp, true, -1);
  expect("home first-run slot A", uiHitHome(200, 140), K::BootSlot, 0);
  uiDrawSlotMenu(slots[0]);
  expect("menu above", uiHitSlotMenu(270, 300, true), K::SlotMenuDismiss);
  expect("menu open", uiHitSlotMenu(200, 732, true), K::BootSlot);
  expect("menu update", uiHitSlotMenu(200, 796, true), K::SlotUpdate);
  expect("menu rename", uiHitSlotMenu(200, 860, true), K::SlotRename);
  expect("menu clear", uiHitSlotMenu(200, 924, true), K::ClearSlot);

  std::vector<DirEntry> e(10);
  for (int i = 0; i < 10; ++i) { e[i].name = "f" + std::to_string(i); e[i].isDir = i < 4; }
  ExplorerDrawState st; st.currentPath = "/books/scifi";
  uiDrawExplorer(e, st, sp);
  expect("files back", uiHitExplorer(30, 84, 10, 0, true, false, false, 0, false), K::Back);
  expect("files up", uiHitExplorer(36, 146, 10, 0, true, false, false, 0, false), K::GoUp);
  expect("files crumb SD", uiHitExplorer(90, 146, 10, 0, true, false, false, 0, false), K::GoPath, 0);
  expect("files crumb books", uiHitExplorer(150, 146, 10, 0, true, false, false, 0, false), K::GoPath, 1);
  expect("files row0", uiHitExplorer(250, 210, 10, 0, true, false, false, 0, false), K::SelectEntry, 0);
  expect("files row3", uiHitExplorer(250, 210 + 3 * 72, 10, 0, true, false, false, 0, false), K::SelectEntry, 3);
  expect("files dock folder", uiHitExplorer(100, 920, 10, 0, true, false, false, 0, false), K::ExplorerNew);
  expect("files dock select", uiHitExplorer(270, 920, 10, 0, true, false, false, 0, false), K::ExplorerSelectMode);
  expect("files dock more", uiHitExplorer(440, 920, 10, 0, true, false, false, 0, false), K::ExplorerMore);
  expect("files dock open (1 sel)", uiHitExplorer(160, 920, 10, 0, true, false, false, 1, true), K::ExplorerOpen);
  expect("files dock done (1 sel)", uiHitExplorer(480, 920, 10, 0, true, false, false, 1, true), K::Back);
  expect("files dock paste", uiHitExplorer(210, 920, 10, 0, true, false, true, 0, false), K::ExplorerPaste);

  uiDrawSettings(sp);
  expect("settings back", uiHitSettings(30, 84), K::Back);
  expect("settings text +", uiHitSettings(505, 182), K::FontSizePlus);
  expect("settings text -", uiHitSettings(335, 182), K::FontSizeMinus);
  expect("settings sleep now", uiHitSettings(200, 412), K::PowerOff);
  expect("settings time", uiHitSettings(200, 522), K::SetTime);
  expect("settings about", uiHitSettings(200, 582), K::Hardware);
  uiDrawSetTime(sp);
  expect("time hour +", uiHitSetTime(505, 268), K::HourPlus);
  expect("time day -", uiHitSetTime(335, 552), K::DayMinus);

  WifiStatus ws; ws.mode = WifiMode::Station; ws.connected = true; snprintf(ws.ssid, 33, "HomeNet");
  const WifiAp aps[] = {{"HomeNet", -58, false}, {"Elisa", -71, false}};
  uiDrawNet(sp, ws, aps, 2);
  expect("net scan", uiHitNet(510, 84), K::NetScan);
  expect("net row1", uiHitNet(250, 216 + 58 + 29), K::NetJoin, 1);
  expect("net disconnect", uiHitNet(120, 924), K::NetDisconnect);
  expect("net browse", uiHitNet(420, 924), K::WebOpen);

  uiDrawConfirm("Delete", "x", "Delete");
  expect("confirm no", uiHitConfirm(120, 917), K::ConfirmNo);
  expect("confirm yes", uiHitConfirm(420, 917), K::ConfirmYes);
  if (!uiIsBackSwipe(10, 500, 260, 520) || uiIsBackSwipe(200, 500, 450, 500) || uiIsBackSwipe(10, 500, 200, 700)) {
    ++fails; puts("FAIL back swipe");
  }
  printf("%s (%d failures)\n", fails ? "HIT TESTS FAILED" : "all hit tests pass", fails);
  return fails != 0;
}
