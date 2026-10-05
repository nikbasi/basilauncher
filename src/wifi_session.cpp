#include "wifi_session.h"

#include "board_hal.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstring>
#include <time.h>

namespace {

constexpr const char* kApSsid = "Basilauncher";
constexpr const char* kDefaultApPass = "basilauncher";
constexpr uint8_t kApChannel = 1;
DNSServer* gDns = nullptr;
WifiMode gMode = WifiMode::Off;
struct SavedNet {
  char ssid[33];
  char pass[65];
};

constexpr int kSavedMax = 8;
SavedNet gSaved[kSavedMax];
int gSavedCount = 0;
char gApPass[64] = {};
bool gCredsLoaded = false;
bool gApLoaded = false;

bool passwordOk(const char* pass) {
  if (!pass) return false;
  const size_t n = strlen(pass);
  if (n < 8 || n > 63) return false;
  for (size_t i = 0; i < n; ++i) {
    const unsigned char c = static_cast<unsigned char>(pass[i]);
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}

void loadApPass() {
  if (gApLoaded) return;
  gApLoaded = true;
  snprintf(gApPass, sizeof(gApPass), "%s", kDefaultApPass);
  Preferences prefs;
  if (!prefs.begin("basil", true)) return;
  const String pass = prefs.getString("apPass", "");
  prefs.end();
  if (passwordOk(pass.c_str())) snprintf(gApPass, sizeof(gApPass), "%s", pass.c_str());
}

void persistNets() {
  Preferences prefs;
  if (!prefs.begin("basil", false)) return;
  if (gSavedCount <= 0) {
    prefs.remove("wifiNets");
    prefs.remove("wifiN");
    prefs.remove("wifiSsid");
    prefs.remove("wifiPass");
  } else {
    prefs.putUChar("wifiN", static_cast<uint8_t>(gSavedCount));
    prefs.putBytes("wifiNets", gSaved, sizeof(SavedNet) * static_cast<size_t>(gSavedCount));
    prefs.putString("wifiSsid", gSaved[0].ssid);
    prefs.putString("wifiPass", gSaved[0].pass);
  }
  prefs.end();
}

void loadCreds() {
  if (gCredsLoaded) return;
  gCredsLoaded = true;
  gSavedCount = 0;
  Preferences prefs;
  if (!prefs.begin("basil", true)) return;
  const uint8_t n = prefs.getUChar("wifiN", 0);
  bool migrated = false;
  if (n > 0 && n <= kSavedMax) {
    const size_t got = prefs.getBytes("wifiNets", gSaved, sizeof(gSaved));
    if (got == sizeof(SavedNet) * n) gSavedCount = n;
  }
  if (gSavedCount == 0) {
    const String ssid = prefs.getString("wifiSsid", "");
    const String pass = prefs.getString("wifiPass", "");
    if (ssid.length() > 0 && ssid.length() < sizeof(gSaved[0].ssid) && pass.length() < sizeof(gSaved[0].pass)) {
      snprintf(gSaved[0].ssid, sizeof(gSaved[0].ssid), "%s", ssid.c_str());
      snprintf(gSaved[0].pass, sizeof(gSaved[0].pass), "%s", pass.c_str());
      gSavedCount = 1;
      migrated = true;
    }
  }
  prefs.end();
  if (migrated) persistNets();
}

void stopDns() {
  if (!gDns) return;
  gDns->stop();
  delete gDns;
  gDns = nullptr;
}

void fillIp(char* out, size_t n, IPAddress ip) {
  snprintf(out, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace

const char* wifiApSsid() {
  return kApSsid;
}

const char* wifiApPassword() {
  loadApPass();
  return gApPass;
}

bool wifiSetApPassword(const char* pass) {
  if (!passwordOk(pass)) return false;
  snprintf(gApPass, sizeof(gApPass), "%s", pass);
  gApLoaded = true;
  Preferences prefs;
  if (!prefs.begin("basil", false)) return true;
  prefs.putString("apPass", gApPass);
  prefs.end();
  Serial.println("WiFi: hotspot password updated");
  return true;
}

bool wifiHasSavedNetwork() {
  loadCreds();
  return gSavedCount > 0 && gSaved[0].ssid[0] != 0;
}

void wifiGetSavedSsid(char* out, size_t outLen) {
  loadCreds();
  if (!out || outLen == 0) return;
  snprintf(out, outLen, "%s", gSavedCount > 0 ? gSaved[0].ssid : "");
}

bool wifiLookupPassword(const char* ssid, char* out, size_t outLen) {
  loadCreds();
  if (!ssid || !ssid[0] || !out || outLen == 0) return false;
  out[0] = 0;
  for (int i = 0; i < gSavedCount; ++i) {
    if (strcmp(gSaved[i].ssid, ssid) != 0) continue;
    if (!gSaved[i].pass[0]) return false;
    snprintf(out, outLen, "%s", gSaved[i].pass);
    return true;
  }
  return false;
}

void wifiSaveNetwork(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return;
  loadCreds();
  SavedNet next[kSavedMax];
  memset(next, 0, sizeof(next));
  snprintf(next[0].ssid, sizeof(next[0].ssid), "%.32s", ssid);
  snprintf(next[0].pass, sizeof(next[0].pass), "%.64s", pass ? pass : "");
  int keep = 1;
  for (int i = 0; i < gSavedCount && keep < kSavedMax; ++i) {
    if (strcmp(gSaved[i].ssid, next[0].ssid) == 0) continue;
    next[keep++] = gSaved[i];
  }
  memcpy(gSaved, next, sizeof(gSaved));
  gSavedCount = keep;
  gCredsLoaded = true;
  persistNets();
  Serial.printf("WiFi: saved network '%s' (%d remembered)\n", gSaved[0].ssid, gSavedCount);
}

void wifiClearSavedNetwork() {
  gSavedCount = 0;
  memset(gSaved, 0, sizeof(gSaved));
  gCredsLoaded = true;
  persistNets();
}

bool wifiStartSoftAp() {
  wifiStop();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  delay(50);

  // Pin SoftAP to 192.168.4.1 before start (Arduino-ESP32 DHCP/gateway).
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, gateway, subnet)) {
    Serial.println("WiFi: softAPConfig failed");
  }

  loadApPass();
  const bool ok = WiFi.softAP(kApSsid, gApPass, kApChannel, 0, 4);
  if (!ok) {
    Serial.println("WiFi: SoftAP failed");
    WiFi.mode(WIFI_OFF);
    gMode = WifiMode::Off;
    return false;
  }

  // Re-apply after softAP — some cores ignore the pre-start config.
  WiFi.softAPConfig(apIp, gateway, subnet);

  for (int i = 0; i < 50; ++i) {
    delay(20);
    if (WiFi.softAPIP() == apIp) break;
  }
  if (WiFi.softAPIP() != apIp) {
    Serial.printf("WiFi: SoftAP IP unexpected: %s\n", WiFi.softAPIP().toString().c_str());
  }
  gMode = WifiMode::SoftAp;

  gDns = new DNSServer();
  if (gDns) {
    gDns->setErrorReplyCode(DNSReplyCode::NoError);
    gDns->start(53, "*", apIp);
  }

  Serial.printf("WiFi: SoftAP '%s' ip=%s\n", kApSsid, WiFi.softAPIP().toString().c_str());
  return true;
}

bool wifiStartStation(uint32_t timeoutMs) {
  loadCreds();
  if (gSavedCount <= 0 || !gSaved[0].ssid[0]) return false;
  wifiStop();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(gSaved[0].ssid, gSaved[0].pass[0] ? gSaved[0].pass : nullptr);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi: STA failed for '%s'\n", gSaved[0].ssid);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    gMode = WifiMode::Off;
    return false;
  }
  gMode = WifiMode::Station;
  Serial.printf("WiFi: STA '%s' ip=%s\n", gSaved[0].ssid, WiFi.localIP().toString().c_str());
  return true;
}

void wifiStop() {
  stopDns();
  if (gMode == WifiMode::SoftAp) {
    WiFi.softAPdisconnect(true);
  } else if (gMode == WifiMode::Station) {
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_OFF);
  gMode = WifiMode::Off;
}

bool wifiJoin(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (!ssid || !ssid[0]) return false;
  wifiSaveNetwork(ssid, pass ? pass : "");
  return wifiStartStation(timeoutMs);
}

int wifiScan(WifiAp* out, int maxOut) {
  if (!out || maxOut <= 0) return 0;
  const bool keepStation = gMode == WifiMode::Station && WiFi.status() == WL_CONNECTED;
  if (gMode == WifiMode::SoftAp) wifiStop();
  WiFi.persistent(false);
  if (WiFi.getMode() != WIFI_STA && WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
    delay(80);
  }
  const int found = WiFi.scanNetworks(false, true);
  int n = 0;
  if (found > 0) {
    for (int i = 0; i < found; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.length() == 0 || ssid.length() >= sizeof(out[0].ssid)) continue;
      const int rssi = WiFi.RSSI(i);
      const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
      int slot = -1;
      for (int j = 0; j < n; ++j) {
        if (strcmp(out[j].ssid, ssid.c_str()) == 0) {
          slot = j;
          break;
        }
      }
      if (slot >= 0) {
        if (rssi > out[slot].rssi) {
          out[slot].rssi = rssi;
          out[slot].open = open;
        }
        continue;
      }
      if (n >= maxOut) {
        int weakest = 0;
        for (int j = 1; j < n; ++j) {
          if (out[j].rssi < out[weakest].rssi) weakest = j;
        }
        if (rssi <= out[weakest].rssi) continue;
        slot = weakest;
      } else {
        slot = n++;
      }
      snprintf(out[slot].ssid, sizeof(out[slot].ssid), "%s", ssid.c_str());
      out[slot].rssi = rssi;
      out[slot].open = open;
    }
  }
  WiFi.scanDelete();
  for (int a = 1; a < n; ++a) {
    const WifiAp key = out[a];
    int b = a;
    while (b > 0 && out[b - 1].rssi < key.rssi) {
      out[b] = out[b - 1];
      --b;
    }
    out[b] = key;
  }
  if (!keepStation && WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    gMode = WifiMode::Off;
  }
  Serial.printf("WiFi: scan %d\n", n);
  return n;
}

bool wifiIsActive() {
  return gMode != WifiMode::Off;
}

bool wifiIsHotspot() { return gMode == WifiMode::SoftAp; }

bool wifiIsStation() { return gMode == WifiMode::Station && WiFi.status() == WL_CONNECTED; }

WifiMode wifiCurrentMode() {
  return gMode;
}

WifiStatus wifiGetStatus() {
  WifiStatus st;
  st.mode = gMode;
  if (gMode == WifiMode::SoftAp) {
    st.connected = true;
    snprintf(st.ssid, sizeof(st.ssid), "%s", kApSsid);
    // Always advertise the pinned SoftAP address (matches softAPConfig).
    snprintf(st.ip, sizeof(st.ip), "192.168.4.1");
    snprintf(st.url, sizeof(st.url), "http://192.168.4.1/");
    snprintf(st.detail, sizeof(st.detail), "WPA2 password is on this screen");
  } else if (gMode == WifiMode::Station) {
    st.connected = WiFi.status() == WL_CONNECTED;
    snprintf(st.ssid, sizeof(st.ssid), "%s", WiFi.SSID().c_str());
    if (st.connected) {
      fillIp(st.ip, sizeof(st.ip), WiFi.localIP());
      snprintf(st.url, sizeof(st.url), "http://%s/", st.ip);
      st.rssi = WiFi.RSSI();
      snprintf(st.detail, sizeof(st.detail), "RSSI %d dBm", st.rssi);
    } else {
      snprintf(st.detail, sizeof(st.detail), "Disconnected");
    }
  } else {
    snprintf(st.detail, sizeof(st.detail), "Wi-Fi off");
  }
  return st;
}

void wifiPoll() {
  if (gDns) gDns->processNextRequest();
}

bool wifiSyncClock(uint32_t timeoutMs) {
  if (gMode != WifiMode::Station || WiFi.status() != WL_CONNECTED) return false;

  // UTC; RTC is wall-clock without TZ until we add a setting.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t start = millis();
  time_t now = 0;
  while ((millis() - start) < timeoutMs) {
    now = time(nullptr);
    if (now > 1700000000) break;  // past 2023
    delay(100);
  }
  if (now < 1700000000) {
    Serial.println("WiFi: NTP failed");
    return false;
  }
  struct tm tm;
  gmtime_r(&now, &tm);
  const bool ok =
      boardSetClock(static_cast<uint16_t>(tm.tm_year + 1900), static_cast<uint8_t>(tm.tm_mon + 1),
                    static_cast<uint8_t>(tm.tm_mday), static_cast<uint8_t>(tm.tm_hour),
                    static_cast<uint8_t>(tm.tm_min));
  Serial.printf("WiFi: NTP %s -> %04d-%02d-%02d %02d:%02d UTC\n", ok ? "ok" : "rtc fail",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
  return ok;
}
